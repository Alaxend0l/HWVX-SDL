//! The viewer's embedded frames end to end: the game runs with its window hidden, `frames`
//! shares its frames through shared memory at the size asked for; they arrive, have that size and
//! show something; a resize changes it; input moves the camera so the picture changes; a click
//! picks a node. Prints the readback cost (ms/frame) and the frame rate at a few sizes.
//!
//!   HWVX_DATA=<GameCube files> cargo test --manifest-path tools/browser/Cargo.toml -- --nocapture

use hwvx_browser::client::{SpawnOptions, Viewer, default_game, default_schemas};
use hwvx_browser::frames::{Frame, FrameReader};
use serde_json::{Value, json};
use std::path::PathBuf;
use std::time::{Duration, Instant};

fn open(reply: &Value) -> FrameReader {
    FrameReader::open(
        reply["name"].as_str().unwrap(),
        reply["key"].as_str().unwrap(),
        reply["size"].as_u64().unwrap() as usize,
    )
    .expect("the frames' shared memory opens")
}

/// The next frame (a new one) of w x h, within a few seconds.
fn next_frame(r: &mut FrameReader, w: u32, h: u32) -> Frame {
    let end = Instant::now() + Duration::from_secs(10);
    loop {
        if let Some(f) = r.take() {
            if f.width == w && f.height == h {
                return f;
            }
        }
        assert!(Instant::now() < end, "no {w}x{h} frame in 10 s");
        std::thread::sleep(Duration::from_millis(2));
    }
}

/// How many distinct colours (to a step of 8) a frame has: a blank one has 1.
fn colours(f: &Frame) -> usize {
    let mut seen = std::collections::HashSet::new();
    for px in f.rgba.chunks_exact(4).step_by(7) {
        seen.insert((px[0] >> 3, px[1] >> 3, px[2] >> 3));
    }
    seen.len()
}

fn differing(a: &Frame, b: &Frame) -> usize {
    a.rgba.chunks_exact(4).zip(b.rgba.chunks_exact(4)).filter(|(p, q)| p[..3] != q[..3]).count()
}

#[test]
fn embedded_frames() {
    let Some(data) = std::env::var_os("HWVX_DATA").map(PathBuf::from) else {
        eprintln!("HWVX_DATA not set: skipped");
        return;
    };
    let game = std::env::var_os("HWVX_GAME")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/hwvx"));
    let game = std::fs::canonicalize(&game).unwrap_or_else(|_| default_game());
    let out = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("frames");
    std::fs::create_dir_all(&out).unwrap();
    let mut v = Viewer::spawn(&SpawnOptions {
        schemas: default_schemas(&game),
        game,
        data: Some(data),
        pref: out.join("pref"),
        window: Some("320x240".into()),
        renderer: std::env::var("HWVX_RENDERER").ok(),
        log_file: Some(out.join("viewer.log")),
        token: None,
        viewer_window: false,
    })
    .expect("the viewer starts");

    // (the hello: protocol and token were checked by Viewer::spawn)
    let status = v.call("status", json!({})).unwrap();
    assert_eq!(status["headless"], true, "the game's window is hidden");
    v.call("load", json!({"archive": "HW_Cars", "type": "car", "id": 5})).unwrap();
    v.call("camera", json!({"spin": false})).unwrap();

    // set size: frames of exactly that size, several, not blank
    let r = v.call("frames", json!({"width": 400, "height": 300})).unwrap();
    assert_eq!((r["width"].as_u64(), r["height"].as_u64()), (Some(400), Some(300)));
    let mut fr = open(&r);
    let mut last = None;
    for _ in 0..5 {
        let f = next_frame(&mut fr, 400, 300);
        assert_eq!(f.rgba.len(), 400 * 300 * 4);
        if let Some(n) = last {
            assert!(f.frame > n, "frames move on: {} after {n}", f.frame);
        }
        last = Some(f.frame);
        assert!(colours(&f) > 20, "the car is drawn ({} colours)", colours(&f));
    }

    // the picture is the camera's: a still camera gives the same picture, input changes it
    let a = next_frame(&mut fr, 400, 300);
    let b = next_frame(&mut fr, 400, 300);
    assert_eq!(differing(&a, &b), 0, "a still camera: the same picture");
    let cam0 = v.call("status", json!({})).unwrap()["camera"].clone();
    let c = v
        .call("input", json!({"height": 300, "events": [{"type": "drag", "button": "left", "dx": 60, "dy": 0}]}))
        .unwrap();
    assert_ne!(c["camera"]["yaw"], cam0["yaw"], "a drag orbits");
    // the frames drawn before the input came are still in the pipeline: a few at most
    let mut after = next_frame(&mut fr, 400, 300);
    let mut late = 0;
    while differing(&b, &after) == 0 && late < 30 {
        after = next_frame(&mut fr, 400, 300);
        late += 1;
    }
    eprintln!("the drag shows {late} frames later");
    let d = differing(&b, &after);
    assert!(d > 1000, "the drag changes the picture ({d} pixels)");
    let c2 = v.call("input", json!({"events": [{"type": "wheel", "dy": 2}, {"type": "drag", "button": "right", "dx": 10, "dy": 5}]})).unwrap();
    assert!(c2["camera"]["distance"].as_f64().unwrap() < c["camera"]["distance"].as_f64().unwrap(), "the wheel zooms in");
    assert_ne!(c2["camera"]["target"], c["camera"]["target"], "a right drag pans");
    v.call("input", json!({"events": [{"type": "key", "key": "f"}]})).unwrap();
    assert!(v.call("input", json!({"events": [{"type": "key", "key": "nope"}]})).is_err());

    // a click on the middle of the framed car picks a part of it; a corner nothing
    let _ = next_frame(&mut fr, 400, 300);
    let _ = next_frame(&mut fr, 400, 300);
    let p = v.call("pick", json!({"x": 200, "y": 150})).unwrap();
    eprintln!("pick at the centre: {}", p["node"]);
    assert!(p["node"].as_str().is_some_and(|n| n.starts_with("geometry/")), "{p}");
    let p = v.call("pick", json!({"x": 1, "y": 1})).unwrap();
    assert!(p["node"].is_null(), "{p}");

    // resize: a smaller one fits the same shared memory, a bigger one gets a new one
    let r2 = v.call("frames", json!({"width": 320, "height": 200})).unwrap();
    assert_eq!(r2["name"], r["name"]);
    let f = next_frame(&mut fr, 320, 200);
    assert!(colours(&f) > 20);
    drop(fr);

    // the cost of reading back, at a few sizes
    for (w, h) in [(800u32, 600u32), (1920, 1080), (3840, 2160)] {
        let r = v.call("frames", json!({"width": w, "height": h})).unwrap();
        let mut fr = open(&r);
        let f = next_frame(&mut fr, w, h);
        assert!(colours(&f) > 20);
        let t = Instant::now();
        let mut n = 0;
        while t.elapsed() < Duration::from_millis(2200) {
            if fr.take().is_some_and(|f| f.width == w) {
                n += 1;
            }
            std::thread::sleep(Duration::from_millis(1));
        }
        let st = v.call("status", json!({})).unwrap();
        eprintln!(
            "{w}x{h}: {:.1} frames/s taken here; viewer: {:.1} published/s, {:.3} ms/frame reading back ({:.3} ms a frame copied)",
            n as f64 / t.elapsed().as_secs_f64(),
            st["frames"]["fps"].as_f64().unwrap_or(0.0),
            st["frames"]["ms"].as_f64().unwrap_or(0.0),
            st["frames"]["msCopy"].as_f64().unwrap_or(0.0)
        );
    }
    assert!(v.call("frames", json!({"width": 5000, "height": 300})).is_err(), "capped at 3840x2160");
    let off = v.call("frames", json!({"on": false})).unwrap();
    assert_eq!(off["on"], false);
    v.call("quit", json!({})).unwrap();
}
