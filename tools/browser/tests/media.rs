//! The viewer's media end to end: textures and sounds as the game decodes them, through the media
//! blob, compared with what tools/xdl_media.py extracts from the same pack (its own decoders, in
//! Python); play and stop through the game's voices and MIDI player, heard in the mixed output
//! (HWVX_AUDIODUMP).
//!
//!   HWVX_DATA=<GameCube files>              the GameCube's (skipped without it)
//!   HWVX_DATA_PC=<PC folder>                also the PC's   (optional)
//!   HWVX_DATA_PS2=<PS2 folder>              also the PS2's  (optional)
//!   cargo test --manifest-path tools/browser/Cargo.toml --test media -- --nocapture
//!
//! What "the same" means, per format (the comparison prints its counts):
//! - GameCube RGBA8, PS2 PSMT8 (palette) and PSMCT32: pixel-identical.
//! - GameCube CMPR: the game decodes it as the GameCube's texture unit does (gx_tex.c BlockPalette:
//!   the middle colours 5/8 and 3/8 of the way, endpoints widened by repeating the high bits),
//!   xdl_media as plain DXT1 (1/3 and 2/3, x * 255 / 31): texels from an endpoint can differ by 1
//!   (the widening), the middle ones by a few steps more. Bounded here, and every texel's alpha agrees.
//!   A transparent texel's colour (not seen) is the average of the endpoints on the GameCube, black
//!   in DXT1: counted apart.
//! - PC DXT1/DXT5 and RGB(A): the game draws the PC's colours squared (Port_PCTexturesLookGC, flag
//!   `squared`): xdl_media's pixels are squared the same way before comparing; DXT's middle colours
//!   round differently (Apple's BC1 rounding, measured, vs. xdl_media's // 3) by 1-2.
//! - Sounds and bank samples: sample-identical (PCM16 as stored; the PS2's SPU-ADPCM decoded by
//!   the loader and by xdl_media alike, without its silent first block).

use hwvx_browser::client::{SpawnOptions, Viewer, default_game, default_schemas};
use hwvx_browser::media::{MediaReader, Sound, Texture, read_png, read_wav};
use serde_json::{Value, json};
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

fn game() -> PathBuf {
    let game = std::env::var_os("HWVX_GAME")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/hwvx"));
    std::fs::canonicalize(&game).unwrap_or_else(|_| default_game())
}

fn spawn(data: &Path, out: &Path) -> Viewer {
    std::fs::create_dir_all(out).unwrap();
    Viewer::spawn(&SpawnOptions {
        schemas: default_schemas(&game()),
        game: game(),
        data: Some(data.to_path_buf()),
        pref: out.join("pref"),
        window: Some("320x240".into()),
        renderer: None,
        log_file: Some(out.join("viewer.log")),
        token: None,
        viewer_window: false,
    })
    .expect("the viewer starts")
}

/// The pack file (any case, any depth) of an archive: HW_Cars -> .../HW_CARS.XPS.
fn archive_file(dir: &Path, stem: &str) -> Option<PathBuf> {
    let mut stack = vec![dir.to_path_buf()];
    while let Some(d) = stack.pop() {
        for e in std::fs::read_dir(&d).ok()?.flatten() {
            let p = e.path();
            if p.is_dir() {
                stack.push(p);
            } else if let Some(name) = p.file_name().and_then(|n| n.to_str()) {
                let l = name.to_lowercase();
                let s = stem.to_lowercase();
                if l == format!("{s}.xgc") || l == format!("{s}.xpc") || l == format!("{s}.xps") {
                    return Some(p);
                }
            }
        }
    }
    None
}

/// xdl_media.py extract ARCHIVE:PACK OUT (fresh).
fn xdl_extract(archive: &Path, pack: &str, out: &Path) {
    let _ = std::fs::remove_dir_all(out);
    let tool = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../xdl_media.py");
    let st = std::process::Command::new("/usr/bin/python3")
        .arg(&tool)
        .arg("extract")
        .arg(format!("{}:{pack}", archive.display()))
        .arg(out)
        .status()
        .expect("python3 runs");
    assert!(st.success(), "xdl_media.py extract {pack} failed");
}

fn squared(c: u8) -> u8 {
    ((c as u32 * c as u32 + 127) / 255) as u8 // gx_tex.c SquareColours
}

#[derive(Default, Debug)]
struct Diff {
    texels: usize,
    same: usize,
    max: u8,
    alpha_differs: usize,
    hidden: usize, // transparent in both, colours apart: not seen
}

fn compare(a: &[u8], b: &[u8], square_b: bool) -> Diff {
    let mut d = Diff::default();
    for (p, q) in a.chunks_exact(4).zip(b.chunks_exact(4)) {
        let q = if square_b { [squared(q[0]), squared(q[1]), squared(q[2]), q[3]] } else { [q[0], q[1], q[2], q[3]] };
        d.texels += 1;
        if p[3] == 0 && q[3] == 0 && p[..3] != q[..3] {
            d.hidden += 1;
            d.same += 1;
            continue;
        }
        let m = (0..4).map(|k| p[k].abs_diff(q[k])).max().unwrap();
        if m == 0 {
            d.same += 1;
        }
        d.max = d.max.max(m);
        if p[3] != q[3] {
            d.alpha_differs += 1;
        }
    }
    d
}

fn texture(v: &mut Viewer, r: &mut MediaReader, node: &str) -> Texture {
    let reply = v.call("texture", json!({"node": node})).unwrap();
    let data = r.read(&reply["blob"]).expect("the blob reads");
    Texture::from_reply(&reply, &data).unwrap()
}

fn sound(v: &mut Viewer, r: &mut MediaReader, node: &str) -> Sound {
    let reply = v.call("sound", json!({"node": node})).unwrap();
    let data = r.read(&reply["blob"]).expect("the blob reads");
    Sound::from_reply(&reply, &data)
}

/// A car pack's own textures and sounds against xdl_media's files.
fn check_car(v: &mut Viewer, r: &mut MediaReader, release: &str, xdl: &Path) {
    let list = v.call("textures", json!({})).unwrap();
    let own: Vec<&Value> = list["textures"].as_array().unwrap().iter().filter(|t| t["shared"] != true).collect();
    assert!(!own.is_empty(), "{release}: the car has textures");
    for t in &own {
        let id = t["id"].as_str().unwrap();
        let tex = texture(v, r, id);
        assert_eq!(tex.levels.len() as u64, t["levels"].as_u64().unwrap(), "{release} {id}: every level drawn");
        for l in &tex.levels {
            let png = xdl.join(format!("{id}/level{}.png", l.level));
            let Ok((w, h, px)) = read_png(&png) else {
                println!("{release} {id} level {}: no {} (xdl_media keeps the stored levels)", l.level, png.display());
                continue;
            };
            assert_eq!((w, h), (l.width, l.height), "{release} {id} level {}: size", l.level);
            let sq = tex.info["flags"]["squared"] == true;
            let d = compare(&l.rgba, &px, sq);
            let fmt = tex.info["format"].as_str().unwrap_or("?").to_string();
            println!(
                "{release} {id} {fmt} {}x{} level {}: {}/{} texels identical, max channel difference {}{}{}",
                w,
                h,
                l.level,
                d.same,
                d.texels,
                d.max,
                if d.hidden > 0 { format!(" ({} transparent ones' hidden colour differs)", d.hidden) } else { String::new() },
                if sq { " (xdl's squared)" } else { "" }
            );
            match fmt.as_str() {
                "CMPR" => {
                    assert!(d.max <= 12, "{release} {id}: CMPR off by {}", d.max);
                    assert_eq!(d.alpha_differs, 0, "{release} {id}: CMPR alpha");
                }
                "PC_DXT1" | "PC_DXT5" => assert!(d.max <= 8, "{release} {id}: DXT off by {}", d.max),
                _ => assert_eq!(d.same, d.texels, "{release} {id} {fmt}: not pixel-identical ({d:?})"),
            }
        }
    }
    let list = v.call("sounds", json!({})).unwrap();
    for s in list["sounds"].as_array().unwrap() {
        let id = s["id"].as_str().unwrap();
        assert_eq!(s["loaded"], true, "{release} {id} loaded");
        let snd = sound(v, r, id);
        let (rate, pcm) = read_wav(&std::fs::read(xdl.join(format!("{id}.wav"))).unwrap()).unwrap();
        assert_eq!(snd.rate, rate, "{release} {id}: rate");
        let same = snd.pcm.iter().zip(&pcm).filter(|(a, b)| a == b).count();
        println!("{release} {id}: {} Hz, {} samples (xdl_media {}), {same} identical", snd.rate, snd.pcm.len(), pcm.len());
        assert_eq!(snd.pcm.len(), pcm.len(), "{release} {id}: length");
        assert_eq!(same, pcm.len(), "{release} {id}: samples");
    }
}

/// HW_Cars car 5 on one release's data.
fn release_check(data: &Path, release: &str) {
    let out = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join(format!("media-{release}"));
    let archive = archive_file(data, "HW_Cars").expect("HW_Cars in the data");
    xdl_extract(&archive, "car_5", &out.join("xdl"));
    let mut v = spawn(data, &out);
    let mut r = MediaReader::new();
    assert_eq!(v.hello["media"], 1, "the viewer has the media commands");
    v.call("load", json!({"archive": "HW_Cars", "type": "car", "id": 5})).unwrap();
    check_car(&mut v, &mut r, release, &out.join("xdl"));
    let _ = v.call("quit", json!({}));
}

#[test]
fn media_gamecube() {
    let Some(data) = std::env::var_os("HWVX_DATA").map(PathBuf::from) else {
        eprintln!("HWVX_DATA not set: skipped");
        return;
    };
    let out = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("media-gc");
    std::fs::create_dir_all(&out).unwrap();
    let dump = out.join("audio.f32");
    let _ = std::fs::remove_file(&dump);
    // the mixed output, raw float32 stereo at 48 kHz (ax.c): what was heard
    unsafe { std::env::set_var("HWVX_AUDIODUMP", &dump) };
    let archive = archive_file(&data, "HW_Cars").unwrap();
    xdl_extract(&archive, "car_5", &out.join("xdl"));
    xdl_extract(&archive, "share_2", &out.join("xdl-share"));
    let music = archive_file(&data, "HW_music").unwrap();
    xdl_extract(&music, "music_1", &out.join("xdl-music"));
    let mut v = spawn(&data, &out);
    unsafe { std::env::remove_var("HWVX_AUDIODUMP") };
    let mut r = MediaReader::new();
    v.call("load", json!({"archive": "HW_Cars", "type": "car", "id": 5})).unwrap();

    // the listing: format, size, levels, palette, flags; the share packs' textures it uses
    let list = v.call("textures", json!({})).unwrap();
    let all = list["textures"].as_array().unwrap();
    let t0 = &all[0];
    for k in ["format", "width", "height", "levels", "palette", "flags", "usedBy", "source"] {
        assert!(t0.get(k).is_some(), "textures: {k} in {t0}");
    }
    assert!(all.iter().any(|t| t["shared"] == true && t["id"].as_str().unwrap().starts_with("base://")), "shared textures listed");
    assert!(t0["usedBy"].as_array().unwrap().iter().any(|u| u.as_str().unwrap().starts_with("geometry/")), "texture/0 is a geometry's");
    // a shared texture decodes too, named by its base:// id
    let shared = all.iter().find(|t| t["shared"] == true).unwrap()["id"].as_str().unwrap().to_string();
    let st = texture(&mut v, &mut r, &shared);
    assert!(st.levels[0].rgba.chunks_exact(4).any(|p| p != [0, 0, 0, 0]), "{shared} decodes");
    // the GameCube's RGBA8 (the car's share pack has them): pixel-identical
    let rgba8 = all.iter().find(|t| t["format"] == "RGBA8" && t["pack"] == "HW_Cars/share_2").expect("an RGBA8 texture");
    let id = rgba8["id"].as_str().unwrap();
    let tex = texture(&mut v, &mut r, id);
    let (w, h, px) = read_png(&out.join(format!("xdl-share/texture/{}/level0.png", rgba8["index"]))).unwrap();
    assert_eq!((w, h), (tex.levels[0].width, tex.levels[0].height));
    let d = compare(&tex.levels[0].rgba, &px, false);
    println!("gc {id} RGBA8 {w}x{h}: {}/{} texels identical", d.same, d.texels);
    assert_eq!(d.same, d.texels, "{id}: RGBA8 pixel-identical");
    // one level alone
    let one = v.call("texture", json!({"node": "texture/1", "level": 1})).unwrap();
    assert_eq!(one["levelData"].as_array().unwrap().len(), 1);
    assert!(v.call("texture", json!({"node": "texture/99"})).is_err(), "a texture that isn't there");
    assert!(v.call("texture", json!({"node": "texture/1", "level": 9})).is_err(), "a level that isn't there");

    check_car(&mut v, &mut r, "gc", &out.join("xdl"));

    // play and stop through the game: acknowledged, the voice runs, the mix isn't silent
    let start = std::fs::metadata(&dump).map(|m| m.len()).unwrap_or(0);
    let p = v.call("play", json!({"node": "sound/0"})).unwrap();
    assert_eq!(p["playing"], "base://HW_Cars/car_5/sound/0");
    assert!(p["voice"].as_u64().unwrap() != 0);
    std::thread::sleep(Duration::from_millis(400));
    let st = v.call("status", json!({})).unwrap();
    assert!(st["audio"]["voices"].as_u64().unwrap() >= 1, "a voice plays: {}", st["audio"]);
    let s = v.call("stop", json!({})).unwrap();
    assert!(s["voices"].as_u64().unwrap() >= 1, "stop stopped it");
    let st = v.call("status", json!({})).unwrap();
    assert_eq!(st["audio"]["voices"], 0, "nothing plays after stop");
    let heard = std::fs::read(&dump).map(|b| b[start as usize..].chunks_exact(4).map(|c| f32::from_le_bytes(c.try_into().unwrap()).abs()).fold(0f32, f32::max)).unwrap_or(0.0);
    println!("sound/0 through the game's mixer: peak {heard:.3}");
    assert!(heard > 0.01, "the mix had the sound in it");
    assert!(v.call("play", json!({"node": "sound/99"})).is_err());

    // a song: listed with its tempo, played by the game's sequencer
    v.call("load", json!({"archive": "HW_music", "type": "music", "id": 1})).unwrap();
    let snd = v.call("sounds", json!({})).unwrap();
    let song = &snd["song"];
    println!("HW_music music_1 song: {song}");
    assert_eq!(song["kind"], "midi");
    assert!(song["bpm"].as_f64().unwrap() > 0.0 && song["ticks"].as_u64().unwrap() > 0);
    let insts = snd["instruments"].as_array().unwrap();
    assert!(!insts.is_empty(), "the bank's instruments");
    let p = v.call("play", json!({"node": "song"})).unwrap();
    assert_eq!(p["playing"], "base://HW_music/music_1/song");
    let t0 = Instant::now();
    let mut tick = 0;
    while t0.elapsed() < Duration::from_secs(3) && tick == 0 {
        std::thread::sleep(Duration::from_millis(100));
        tick = v.call("status", json!({})).unwrap()["audio"]["tick"].as_u64().unwrap_or(0);
    }
    assert!(tick > 0, "the song advances");
    let s = v.call("stop", json!({})).unwrap();
    assert_eq!(s["song"], true, "stop stopped the song");
    assert_eq!(v.call("status", json!({})).unwrap()["audio"]["song"], false);
    // a bank sample against xdl_media's
    for (k, inst) in insts.iter().enumerate().take(4) {
        let id = inst["id"].as_str().unwrap();
        let s = sound(&mut v, &mut r, id);
        let (rate, pcm) = read_wav(&std::fs::read(out.join(format!("xdl-music/song/bank/{k}.wav"))).unwrap()).unwrap();
        assert_eq!((s.rate, s.pcm.len()), (rate, pcm.len()), "{id}: rate and length");
        assert!(s.pcm == pcm, "{id}: samples");
        println!("{id}: {} Hz, {} samples, identical to xdl_media's", rate, pcm.len());
    }
    let p = v.call("play", json!({"node": "instrument/0", "note": 60})).unwrap();
    assert!(p["voice"].as_u64().unwrap() != 0);
    v.call("stop", json!({})).unwrap();
    let _ = v.call("quit", json!({}));
}

#[test]
fn media_pc_ps2() {
    let mut any = false;
    for (var, release) in [("HWVX_DATA_PC", "pc"), ("HWVX_DATA_PS2", "ps2")] {
        if let Some(d) = std::env::var_os(var).map(PathBuf::from) {
            release_check(&d, release);
            any = true;
        }
    }
    if !any {
        eprintln!("HWVX_DATA_PC / HWVX_DATA_PS2 not set: skipped");
    }
}
