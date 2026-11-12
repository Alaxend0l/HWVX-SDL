//! Whole worlds in the viewer: `worlds` lists the MCP's levels; `world` loads a challenge (ch1_l2)
//! as the game does, its quadrants streaming around the camera: the frames are not blank, some
//! quadrants are loaded and the camera is in one; flying the AI's route (nav points) loads other
//! quadrants and releases them; overlays change the picture; a pick on the start finds the tile
//! car's node in the world pack; a battle arena (bat1_l1) and a joyride world (world1) load too;
//! loading a car pack unloads the world.
//!
//!   HWVX_DATA=<GameCube files> cargo test --manifest-path tools/browser/Cargo.toml --test world -- --nocapture

use hwvx_browser::client::{SpawnOptions, Viewer, default_game, default_schemas};
use hwvx_browser::frames::{Frame, FrameReader};
use serde_json::{Value, json};
use std::collections::HashSet;
use std::path::PathBuf;
use std::time::{Duration, Instant};

fn next_frame(r: &mut FrameReader, v: &mut Viewer) -> Frame {
    let end = Instant::now() + Duration::from_secs(10);
    let _ = r.take(); // the frame from before the last request
    loop {
        v.call("status", json!({})).unwrap(); // (a frame passes)
        if let Some(f) = r.take() {
            return f;
        }
        assert!(Instant::now() < end, "no frame in 10 s");
        std::thread::sleep(Duration::from_millis(5));
    }
}

fn colours(f: &Frame) -> usize {
    let mut seen = HashSet::new();
    for px in f.rgba.chunks_exact(4).step_by(7) {
        seen.insert((px[0] >> 3, px[1] >> 3, px[2] >> 3));
    }
    seen.len()
}

fn differing(a: &Frame, b: &Frame) -> usize {
    a.rgba.chunks_exact(4).zip(b.rgba.chunks_exact(4)).filter(|(p, q)| p[..3] != q[..3]).count()
}

fn loaded(w: &Value) -> Vec<u64> {
    w["loadedQuads"].as_array().unwrap().iter().map(|x| x.as_u64().unwrap()).collect()
}

#[test]
fn whole_worlds() {
    let Some(data) = std::env::var_os("HWVX_DATA").map(PathBuf::from) else {
        eprintln!("HWVX_DATA not set: skipped");
        return;
    };
    let game = std::env::var_os("HWVX_GAME")
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/hwvx"));
    let game = std::fs::canonicalize(&game).unwrap_or_else(|_| default_game());
    let out = PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("world");
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

    let worlds = v.call("worlds", json!({})).unwrap();
    let list = worlds["worlds"].as_array().unwrap();
    for (mode, name) in [("challenge", "ch1_l2"), ("battle", "bat1_l1"), ("joyride", "world1")] {
        assert!(list.iter().any(|w| w["mode"] == mode && w["name"] == name), "worlds lists {mode} {name}");
    }

    let fr = v.call("frames", json!({"width": 400, "height": 300})).unwrap();
    let mut r = FrameReader::open(fr["name"].as_str().unwrap(), fr["key"].as_str().unwrap(), fr["size"].as_u64().unwrap() as usize).unwrap();

    let t0 = Instant::now();
    let w = v.call("world", json!({"name": "ch1_l2"})).unwrap()["world"].clone();
    println!("ch1_l2: {} quadrants, loaded in {:.0} ms ({:.0} ms with the reply)", w["quads"], w["loadMs"].as_f64().unwrap(), t0.elapsed().as_secs_f64() * 1000.0);
    assert!(w["quads"].as_u64().unwrap() > 10, "a streamed world: many quadrants");
    let start = next_frame(&mut r, &mut v);
    assert!(colours(&start) > 50, "the world is drawn: {} colours", colours(&start));
    let st = v.call("status", json!({})).unwrap()["world"].clone();
    let first = loaded(&st);
    println!("at the start: quadrants {first:?}, camera in {}", st["cameraQuad"]);
    assert!(first.len() >= 2 && st["cameraQuad"].as_i64().unwrap() >= 0, "the start's quadrant and the hub loaded");
    assert!(first.len() < st["quads"].as_u64().unwrap() as usize, "not all of them: streamed");

    // overlays change the picture
    v.call("overlays", json!({"regions": true, "loaded": true, "spawns": true, "paths": true})).unwrap();
    let over = next_frame(&mut r, &mut v);
    assert!(differing(&start, &over) > 200, "the overlays are drawn");
    v.call("overlays", json!({"regions": false, "loaded": false, "spawns": false, "paths": false})).unwrap();

    // a pick at the middle, just below the centre: the tile car, in the world pack's outline
    let pk = v.call("pick", json!({"x": 200, "y": 180})).unwrap();
    println!("pick: {pk}");
    assert_eq!(pk["kind"], "tile_car", "the start's car is picked");
    assert!(pk["node"].is_string(), "a node of the world pack");
    let shown = v.call("status", json!({})).unwrap()["shown"].as_str().unwrap().to_string();
    let outline = v.call("outline", json!({"pack": shown})).unwrap();
    assert!(outline["nodes"].as_array().unwrap().iter().any(|n| n["id"] == pk["node"]), "the node is in the outline");

    // fly the AI's route: other quadrants stream in, the first ones go
    let n = st["navPoints"].as_u64().unwrap();
    v.call("camera", json!({"mode": "fly"})).unwrap();
    let mut seen: HashSet<u64> = first.iter().copied().collect();
    let mut released = false;
    for k in (0..n).step_by((n as usize / 30).max(1)) {
        v.call("goto", json!({"nav": k})).unwrap();
        v.call("status", json!({})).unwrap();
        let now = loaded(&v.call("status", json!({})).unwrap()["world"]);
        released |= first.iter().any(|q| *q != 0 && !now.contains(q));
        seen.extend(now);
    }
    let fly = next_frame(&mut r, &mut v);
    println!("along the route: {} quadrants loaded at some point", seen.len());
    assert!(seen.len() > first.len() + 3, "flying loads other quadrants");
    assert!(released, "and releases the ones left behind");
    assert!(colours(&fly) > 50);

    // fps with the frames shared
    let f0 = v.call("status", json!({})).unwrap()["frame"].as_u64().unwrap();
    let t = Instant::now();
    std::thread::sleep(Duration::from_secs(2));
    let f1 = v.call("status", json!({})).unwrap()["frame"].as_u64().unwrap();
    println!("ch1_l2: {:.0} fps at 400x300", (f1 - f0) as f64 / t.elapsed().as_secs_f64());

    for name in ["bat1_l1", "world1"] {
        let w = v.call("world", json!({"name": name})).unwrap()["world"].clone();
        let f = next_frame(&mut r, &mut v);
        println!("{name}: {} quadrants, {:?} loaded, {} colours", w["quads"], loaded(&v.call("status", json!({})).unwrap()["world"]), colours(&f));
        assert!(colours(&f) > 50, "{name} is drawn");
    }

    // a car pack: the world goes
    v.call("load", json!({"archive": "HW_Cars", "type": "car", "id": 5})).unwrap();
    assert!(v.call("status", json!({})).unwrap()["world"].is_null(), "loading a pack unloads the world");
    v.call("quit", json!({})).ok();
}
