//! Test drives (src/testdrive.rs): the sample level (tools/level-samples/checkpoint-run) compiled
//! into a scratch folder, compiled again only when it changed; a broken copy's errors by file and
//! place; the game started into it with the quick start: the level applied, in the race and
//! driving (the times from the start, printed with --nocapture), and leaving the race through the
//! pause menu ends the game by itself; "from here": player 1 starts where asked; Stop ends it
//! (its PID). One game at a time.
//!
//!   HWVX_DATA=<GameCube files> cargo test --manifest-path tools/browser/Cargo.toml --test testdrive -- --nocapture --test-threads=1

use hwvx_browser::client::default_game;
use hwvx_browser::testdrive::{self, Options, TestDrive};
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

fn root() -> PathBuf {
    Path::new(env!("CARGO_MANIFEST_DIR")).join("../..").canonicalize().unwrap()
}

fn options(scratch: &Path, project: &Path, data: Option<PathBuf>) -> Options {
    let game = std::env::var_os("HWVX_GAME").map(PathBuf::from).unwrap_or_else(default_game);
    Options {
        tools: root().join("tools"),
        python: "python3".into(),
        game,
        build_pack: data.is_some(),
        data,
        project: project.to_path_buf(),
        scratch: scratch.to_path_buf(),
        car: Some(5),
        skip_intro: true,
        start_at: None,
        // SDL_GPU on Metal stalls while another window covers the game's
        renderer: Some(std::env::var("HWVX_RENDERER").unwrap_or_else(|_| "opengl".into())),
        env: Vec::new(),
    }
}

fn scratch(name: &str) -> PathBuf {
    let d = root().join("build-browser-test").join(name);
    let _ = std::fs::remove_dir_all(&d);
    std::fs::create_dir_all(&d).unwrap();
    d
}

fn copy_dir(from: &Path, to: &Path) {
    std::fs::create_dir_all(to).unwrap();
    for e in std::fs::read_dir(from).unwrap() {
        let e = e.unwrap();
        if e.file_type().unwrap().is_dir() {
            copy_dir(&e.path(), &to.join(e.file_name()));
        } else {
            std::fs::copy(e.path(), to.join(e.file_name())).unwrap();
        }
    }
}

#[test]
fn compile_and_errors() {
    let sample = root().join("tools/level-samples/checkpoint-run");
    let s = scratch("compile");
    let o = options(&s, &sample, None);
    let p = testdrive::prepare(&o).expect("the sample compiles");
    assert_eq!(p.start, "race:mode=challenge,world=uid://checkpointrun,car=5");
    assert!(p.level.is_file());
    assert!(!p.cached);
    let q = testdrive::prepare(&o).unwrap();
    assert!(q.cached, "nothing changed: the last build");
    println!("compile: {:.0} ms, again (unchanged) {:.0} ms (the tool's own: {:.1} / {:.1} ms)", p.ms, q.ms, p.tool_ms, q.tool_ms);

    // an edit compiles again
    let proj = s.join("project");
    copy_dir(&sample, &proj);
    let o2 = options(&s, &proj, None);
    testdrive::prepare(&o2).unwrap();
    let f = proj.join("triggers/cp1.jsonc");
    let text = std::fs::read_to_string(&f).unwrap().replace("level://volumes/cp1", "level://volumes/cp9");
    std::fs::write(&f, text).unwrap();
    let errs = testdrive::prepare(&o2).expect_err("a link to nothing");
    assert!(errs.iter().any(|e| e.file == "triggers/cp1.jsonc" && e.message.contains("level://volumes/cp9")), "{errs:?}");
    let f = proj.join("level.jsonc");
    let text = std::fs::read_to_string(&f).unwrap().replace("\"radius\": 90 }", "\"radious\": 90 }");
    std::fs::write(&f, text).unwrap();
    let errs = testdrive::prepare(&o2).expect_err("an unknown field");
    assert!(errs.iter().any(|e| e.file == "level.jsonc" && e.path.starts_with("volumes/cp") && e.path.ends_with("radious") && e.message.contains("not a field")), "{errs:?}");
    // the project itself is never written
    assert!(!proj.join("build").exists());
}

fn wait<F: Fn(&testdrive::Progress) -> bool>(d: &mut TestDrive, secs: u64, f: F) -> bool {
    let end = Instant::now() + Duration::from_secs(secs);
    while Instant::now() < end {
        if f(&d.progress.lock().unwrap()) {
            return true;
        }
        if d.poll().is_some() {
            return f(&d.progress.lock().unwrap());
        }
        std::thread::sleep(Duration::from_millis(20));
    }
    false
}

#[test]
fn drive_and_leave() {
    let Some(data) = std::env::var_os("HWVX_DATA").map(PathBuf::from) else {
        eprintln!("HWVX_DATA not set: skipped");
        return;
    };
    let sample = root().join("tools/level-samples/checkpoint-run");
    let s = scratch("drive");
    let mut o = options(&s, &sample, Some(data.clone()));
    // the pause menu's Quit, then Yes (fixed steps, faster than real time)
    o.env = vec![
        ("HWVX_NO_VIWAIT".into(), "1".into()),
        ("HWVX_NO_VSYNC".into(), "1".into()),
        ("HWVX_DETERMINISTIC".into(), "1".into()),
        ("HWVX_AUTOINPUT".into(), "200:start,260:down,300:down,340:down,380:down,420:down,460:a,640:up,700:a".into()),
    ];
    for run in 0..2 {
        let clicked = Instant::now();
        let p = testdrive::prepare(&o).unwrap();
        let mut d = TestDrive::launch(&o, &p, clicked).unwrap();
        assert!(wait(&mut d, 60, |p| p.driving.is_some()), "not driving: {:?}", d.progress.lock().unwrap().tail);
        let pr = d.progress.lock().unwrap().clone();
        println!(
            "test drive {run} ({}): compiled {:.0} ms, level applied {:?}, in the race {:?}, driving {:?} after the click",
            if run == 0 { "the .xdl cache made" } else { "from the .xdl cache" },
            p.ms,
            pr.applied.unwrap(),
            pr.in_race.unwrap(),
            pr.driving.unwrap()
        );
        assert!(pr.not_applied.is_none());
        let end = Instant::now() + Duration::from_secs(60);
        while d.poll().is_none() && Instant::now() < end {
            std::thread::sleep(Duration::from_millis(50));
        }
        let how = d.poll();
        let left = d.progress.lock().unwrap().tail.iter().any(|l| l.contains("testdrive: the race was left"));
        d.stop();
        assert_eq!(how.as_deref(), Some("ended"), "the game didn't end by itself after Quit");
        assert!(left);
    }
}

#[test]
fn from_here_and_stop() {
    let Some(data) = std::env::var_os("HWVX_DATA").map(PathBuf::from) else {
        eprintln!("HWVX_DATA not set: skipped");
        return;
    };
    let sample = root().join("tools/level-samples/checkpoint-run");
    let s = scratch("fromhere");
    let mut o = options(&s, &sample, Some(data));
    // on the road at checkpoint 2, facing +x (rotY pi/2)
    o.start_at = Some(testdrive::start_from([1252.0, 3.0, -730.0], -90.0));
    o.env = vec![("HWVX_CARLOG".into(), "1".into())];
    let s4 = o.start_at.unwrap();
    assert!((s4[3] - std::f32::consts::FRAC_PI_2).abs() < 1e-4, "yaw -90 (looking +x) is rotY pi/2: {s4:?}");
    let p = testdrive::prepare(&o).unwrap();
    let mut d = TestDrive::launch(&o, &p, Instant::now()).unwrap();
    let pid = d.pid;
    assert!(wait(&mut d, 60, |p| p.tail.iter().any(|l| l.contains("carlog:"))), "no car");
    let tail = d.progress.lock().unwrap().tail.clone();
    assert!(tail.iter().any(|l| l.contains("player 1 starts at 1252.0 5.0 -730.0")), "{tail:?}");
    let pos: Vec<f32> = tail
        .iter()
        .filter_map(|l| l.split(" pos ").nth(1))
        .last()
        .unwrap()
        .split_whitespace()
        .filter_map(|x| x.parse().ok())
        .collect();
    assert!((pos[0] - 1252.0).abs() < 30.0 && (pos[2] + 730.0).abs() < 30.0, "the car at {pos:?}");
    d.stop();
    assert_eq!(d.poll().as_deref(), Some("stopped"));
    // that PID is gone (kill -0 fails)
    let alive = std::process::Command::new("kill").args(["-0", &pid.to_string()]).status().map(|s| s.success()).unwrap_or(false);
    assert!(!alive, "pid {pid} still there");
}

/// The viewer's `raycast` (the ground for "from here"): straight down at ch1_l2's start, from a
/// point above it, and a miss outside the world.
#[test]
fn viewer_raycast() {
    use hwvx_browser::client::{SpawnOptions, Viewer, default_schemas};
    use serde_json::json;
    let Some(data) = std::env::var_os("HWVX_DATA").map(PathBuf::from) else {
        eprintln!("HWVX_DATA not set: skipped");
        return;
    };
    let game = std::env::var_os("HWVX_GAME").map(PathBuf::from).unwrap_or_else(default_game);
    let out = scratch("raycast");
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
    assert!(v.call("raycast", json!({"down": [0, 0]})).is_err(), "no world: an error");
    v.call("world", json!({"name": "ch1_l2"})).unwrap();
    let r = v.call("raycast", json!({"from": [-365.0, 60.0, -781.0], "to": [-365.0, -500.0, -781.0]})).unwrap();
    assert_eq!(r["hit"], true, "{r}");
    let y = r["point"][1].as_f64().unwrap();
    assert!((y - 2.0).abs() < 5.0, "the start's road at y {y}");
    let r = v.call("raycast", json!({"down": [-365.0, -781.0]})).unwrap();
    assert_eq!(r["hit"], true, "{r}");
    let r = v.call("raycast", json!({"from": [0.0, 5000.0, 0.0], "to": [0.0, 6000.0, 0.0]})).unwrap();
    assert_eq!(r["hit"], false, "{r}");
    v.call("quit", json!({})).ok();
}

/// Save as mod: the sample packed into a .hwmod with the no-game-data check (HWVX_DATA: the
/// index made from it into the scratch's cache, once); a project with no data folder is refused.
#[test]
fn save_as_mod() {
    let sample = root().join("tools/level-samples/checkpoint-run");
    let s = scratch("share");
    let out = s.join("checkpointrun.hwmod");
    assert!(testdrive::share(&options(&s, &sample, None), &out).is_err(), "no data folder: refused");
    let Some(data) = std::env::var_os("HWVX_DATA").map(PathBuf::from) else {
        eprintln!("skip: HWVX_DATA not set");
        return;
    };
    let mut o = options(&s, &sample, Some(data));
    if let Some(idx) = std::env::var_os("HWVX_MOD_INDEX") {
        // an index already made (tools/xdl_mod.py index), in the cache where share looks
        let d = s.join("base-cache");
        std::fs::create_dir_all(&d).unwrap();
        let key = {
            let p = std::fs::canonicalize(o.data.as_ref().unwrap()).unwrap();
            let r = std::process::Command::new("python3")
                .args(["-c", "import hashlib,sys; print(hashlib.sha1(sys.argv[1].encode()).hexdigest()[:12])"])
                .arg(&p)
                .output()
                .unwrap();
            String::from_utf8_lossy(&r.stdout).trim().to_string()
        };
        std::fs::copy(idx, d.join(format!("mod-index-{key}.npz"))).unwrap();
    }
    o.project = sample;
    let line = testdrive::share(&o, &out).expect("packed");
    assert!(line.contains("no game data found"), "{line}");
    assert!(out.exists());
}

/// Both routes once (the Level tab's default and "Build world pack"): as shared, the test drive's
/// mod has the level file, which the game applies as the world loads; with a world pack, the mod
/// names the pack built from the data. Each is applied and drives.
#[test]
fn both_routes() {
    let Some(data) = std::env::var_os("HWVX_DATA").map(PathBuf::from) else {
        eprintln!("HWVX_DATA not set: skipped");
        return;
    };
    let sample = root().join("tools/level-samples/checkpoint-run");
    for (pack, name) in [(false, "route-shared"), (true, "route-pack")] {
        let s = scratch(name);
        let mut o = options(&s, &sample, Some(data.clone()));
        o.build_pack = pack;
        let p = testdrive::prepare(&o).unwrap();
        let mod_text = std::fs::read_to_string(p.mods.join("editor-test/mod.jsonc")).unwrap();
        assert_eq!(mod_text.contains("\"worldPack\""), pack, "{mod_text}");
        assert_eq!(mod_text.contains("\"level\""), !pack, "{mod_text}");
        let mut d = TestDrive::launch(&o, &p, Instant::now()).unwrap();
        let ok = wait(&mut d, 60, |p| p.driving.is_some());
        let pr = d.progress.lock().unwrap().clone();
        d.stop();
        assert!(ok, "{name}: not driving: {:?}", pr.tail);
        assert!(pr.applied.is_some() && pr.not_applied.is_none(), "{name}: the level not applied: {:?}", pr.tail);
        println!("{name}: compiled {:.0} ms, applied {:?}, driving {:?}", p.ms, pr.applied.unwrap(), pr.driving.unwrap());
    }
}
