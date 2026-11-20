//! A track's picture of the mod's own, in the game: a PNG made here (a gradient, red across and
//! blue down, a yellow square in the middle) as a new Challenge track's thumbnail, the game
//! started in the Challenge world select on that track (HWVX_START=worldselect, HWVX_DUMP), and
//! the frame's big picture checked against the PNG: its middle yellow, its corners the
//! gradient's (so it is the right way up and not the base picture).
//!
//! Needs the game data (HWVX_TEST_DATA) and the game (HWVX_TEST_GAME, else the checkout's
//! build/hwvx); skipped without them. HWVX_TEST_OUT: where the run's folder goes (default:
//! target/track-picture-game), kept for a look (its frames are deleted, but the one checked).

use hwvx_browser::content::ModProject;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{Duration, Instant};

fn gradient_png(w: u32, h: u32) -> Vec<u8> {
    let mut px = vec![];
    for y in 0..h {
        for x in 0..w {
            let inside = x >= w / 4 && x < w * 3 / 4 && y >= h / 4 && y < h * 3 / 4;
            px.extend_from_slice(&if inside { [255, 230, 0, 255] } else { [(x * 255 / (w - 1)) as u8, 40, (y * 255 / (h - 1)) as u8, 255] });
        }
    }
    let mut out = vec![];
    let mut e = png::Encoder::new(&mut out, w, h);
    e.set_color(png::ColorType::Rgba);
    e.set_depth(png::BitDepth::Eight);
    e.write_header().unwrap().write_image_data(&px).unwrap();
    out
}

/// A BMP SDL_SaveBMP wrote: (w, h, RGB rows top first).
fn read_bmp(p: &Path) -> (usize, usize, Vec<[u8; 3]>) {
    let b = std::fs::read(p).unwrap();
    let u32_at = |o: usize| u32::from_le_bytes(b[o..o + 4].try_into().unwrap());
    let off = u32_at(10) as usize;
    let w = u32_at(18) as i32;
    let h = u32_at(22) as i32;
    let bpp = u16::from_le_bytes(b[28..30].try_into().unwrap()) as usize;
    assert!(bpp == 24 || bpp == 32, "{bpp} bits");
    let (w, up) = (w as usize, h > 0);
    let h = h.unsigned_abs() as usize;
    let stride = (w * bpp / 8).div_ceil(4) * 4;
    let mut px = vec![[0u8; 3]; w * h];
    for y in 0..h {
        let row = if up { h - 1 - y } else { y };
        for x in 0..w {
            let o = off + row * stride + x * bpp / 8;
            px[y * w + x] = [b[o + 2], b[o + 1], b[o]];
        }
    }
    (w, h, px)
}

/// The mean colour of a box of the frame, at fractions of its size.
fn mean(f: &(usize, usize, Vec<[u8; 3]>), x0: f64, y0: f64, x1: f64, y1: f64) -> [f64; 3] {
    let (w, h, px) = f;
    let (xa, xb, ya, yb) = ((x0 * *w as f64) as usize, (x1 * *w as f64) as usize, (y0 * *h as f64) as usize, (y1 * *h as f64) as usize);
    let mut s = [0.0; 3];
    let mut n = 0.0;
    for y in ya..yb {
        for x in xa..xb {
            for c in 0..3 {
                s[c] += px[y * w + x][c] as f64;
            }
            n += 1.0;
        }
    }
    s.map(|v| v / n)
}

#[test]
fn own_picture_in_the_world_select() {
    let Some(data) = std::env::var_os("HWVX_TEST_DATA").map(PathBuf::from) else {
        eprintln!("HWVX_TEST_DATA not set: skipped");
        return;
    };
    let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../..");
    let game = std::env::var_os("HWVX_TEST_GAME").map(PathBuf::from).unwrap_or_else(|| root.join("build/hwvx"));
    if !game.is_file() {
        eprintln!("{}: no game: skipped", game.display());
        return;
    }
    let out = std::env::var_os("HWVX_TEST_OUT").map(PathBuf::from).unwrap_or_else(|| Path::new(env!("CARGO_MANIFEST_DIR")).join("target/track-picture-game"));
    let _ = std::fs::remove_dir_all(&out);
    let (mods, pref, dump) = (out.join("mods"), out.join("pref"), out.join("dump"));
    for d in [&mods, &pref, &dump] {
        std::fs::create_dir_all(d).unwrap();
    }

    // the mod: one Challenge track over Challenge 1, the generated PNG its picture
    let mut p = ModProject::create(&mods.join("own-picture"), "Own Picture", "").unwrap();
    let t = p.add_track(39, "My Picture", true);
    std::fs::write(out.join("gradient.png"), gradient_png(320, 192)).unwrap();
    let rel = p.set_track_own_picture(&t, &out.join("gradient.png")).unwrap();
    p.save().unwrap();
    assert!(p.doc["tracks"][&t]["thumbnail"] == rel.as_str());

    // the world select on it: the mode's 18 levels, then the mod's (level 18)
    let log = std::fs::File::create(out.join("game.log")).unwrap();
    let mut child = Command::new(&game)
        .args(["--windowed", "--window", "1280x960", "--data"])
        .arg(&data)
        .envs([
            ("HWVX_NO_MOVIE", "1"),
            ("HWVX_NO_DIALOG", "1"),
            ("HWVX_DETERMINISTIC", "1"),
            ("HWVX_SEED", "3"),
            ("HWVX_NO_VIWAIT", "1"),
            ("HWVX_NO_VSYNC", "1"),
            ("HWVX_MENUDUMP", "1"),
            ("HWVX_START", "worldselect:mode=challenge,world=18"),
        ])
        .env("HWVX_MODS_DIR", &mods)
        .env("HWVX_PREF_DIR", &pref)
        .env("HWVX_DUMP", &dump)
        .stdout(log.try_clone().unwrap())
        .stderr(log)
        .spawn()
        .unwrap();
    let t0 = Instant::now();
    while t0.elapsed() < Duration::from_secs(12) && child.try_wait().unwrap().is_none() {
        std::thread::sleep(Duration::from_millis(200));
    }
    let _ = child.kill();
    let _ = child.wait();
    let text = std::fs::read_to_string(out.join("game.log")).unwrap();
    assert!(text.contains("drawn as 256 x 256"), "the picture was made (game.log)");
    let pics = text.lines().find(|l| l.contains("pictures 19 (obj 5)")).expect("the challenge select's pictures, 19 of them");
    let last = pics.rsplit('|').next().unwrap().trim();
    assert!(last.starts_with("18 ") && last[3..].parse::<u32>().unwrap() >= 0x7FF + 16, "level 18's picture is the port's own texture: {last}");

    // the last frame: the select settled on level 18
    let mut frames: Vec<PathBuf> = std::fs::read_dir(&dump).unwrap().flatten().map(|e| e.path()).collect();
    frames.sort();
    let f = frames.last().expect("frames");
    std::fs::copy(f, out.join("frame.bmp")).unwrap();
    for x in &frames {
        let _ = std::fs::remove_file(x);
    }
    let fr = read_bmp(&out.join("frame.bmp"));
    // the big picture: about x 0.22..0.78, y 0.51..0.82 of the frame (inside its gold frame)
    let (x0, x1, y0, y1) = (0.22, 0.78, 0.515, 0.82);
    let at = |fx: f64, fy: f64| {
        let (x, y) = (x0 + (x1 - x0) * fx, y0 + (y1 - y0) * fy);
        mean(&fr, x - 0.01, y - 0.01, x + 0.01, y + 0.01)
    };
    let mid = at(0.5, 0.5);
    let (tl, tr, bl, br) = (at(0.1, 0.1), at(0.9, 0.1), at(0.1, 0.9), at(0.9, 0.9));
    eprintln!("middle {mid:?}\ntop left {tl:?} top right {tr:?}\nbottom left {bl:?} bottom right {br:?}");
    assert!(mid[0] > 200.0 && mid[1] > 180.0 && mid[2] < 60.0, "the middle is the yellow square: {mid:?}");
    // red grows to the right, blue downwards; green stays low
    assert!(tr[0] > tl[0] + 120.0 && br[0] > bl[0] + 120.0, "red across: {tl:?} {tr:?} / {bl:?} {br:?}");
    assert!(bl[2] > tl[2] + 120.0 && br[2] > tr[2] + 120.0, "blue down: {tl:?} {bl:?} / {tr:?} {br:?}");
    for c in [tl, tr, bl, br] {
        assert!(c[1] < 90.0, "green low: {c:?}");
    }
}
