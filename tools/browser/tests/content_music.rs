//! The Content tab's soundtrack (src/content_music.rs) against the game:
//! * `songs_match_the_viewer`: the editor's list of base songs is the MCP's levels (the viewer's
//!   `worlds`), each with the mode HWVX_START takes; a world's song and a front-end song load and
//!   play through the viewer (the editor's "play the base song").
//! * `replacement_plays_in_the_game`: a mod made with the model (a test tone WAV made here, its
//!   loop points in the entry) replaces Challenge 1-2's song; the game, started in that race with
//!   the mod on, logs the stream (HWVX_MUSICLOG) and its mixed audio (HWVX_AUDIODUMP) holds the
//!   loop's tone and never the part after the loop's end.
//!
//!   HWVX_DATA=<GameCube files> cargo test --release --manifest-path tools/browser/Cargo.toml --test content_music -- --nocapture --test-threads=1

use hwvx_browser::client::{SpawnOptions, Viewer, default_game, default_schemas};
use hwvx_browser::content::ModProject;
use hwvx_browser::content_music::{Track, base_songs, song_for, tone, wav};
use serde_json::json;
use std::path::PathBuf;
use std::process::{Command, Stdio};
use std::time::{Duration, Instant};

fn game() -> PathBuf {
    let g = std::env::var_os("HWVX_GAME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/hwvx"));
    std::fs::canonicalize(&g).unwrap_or_else(|_| default_game())
}

/// HWVX_TEST_OUT, else the target's scratch folder
fn out_dir(name: &str) -> PathBuf {
    let base = std::env::var_os("HWVX_TEST_OUT").map(PathBuf::from).unwrap_or_else(|| PathBuf::from(env!("CARGO_TARGET_TMPDIR")));
    let d = base.join(name);
    let _ = std::fs::remove_dir_all(&d);
    std::fs::create_dir_all(&d).unwrap();
    d
}

#[test]
fn songs_match_the_viewer() {
    let Some(data) = std::env::var_os("HWVX_DATA").map(PathBuf::from) else {
        eprintln!("HWVX_DATA not set: skipped");
        return;
    };
    let game = game();
    let out = out_dir("music-viewer");
    let mut v = Viewer::spawn(&SpawnOptions {
        schemas: default_schemas(&game),
        game,
        data: Some(data.clone()),
        pref: out.join("pref"),
        window: Some("320x240".into()),
        renderer: std::env::var("HWVX_RENDERER").ok(),
        log_file: Some(out.join("viewer.log")),
        token: None,
        viewer_window: false,
    })
    .expect("the viewer starts");
    let songs = base_songs();
    let worlds = v.call("worlds", json!({})).unwrap();
    let list = worlds["worlds"].as_array().unwrap();
    let mut seen = std::collections::HashSet::new();
    for w in list {
        let name = w["name"].as_str().unwrap();
        let mode = w["mode"].as_str().unwrap();
        let s = songs.iter().find(|s| s.archive.eq_ignore_ascii_case(name)).unwrap_or_else(|| panic!("{name}: not in the editor's songs"));
        assert!(s.start.contains(&format!("mode={mode},")) || mode == "mpbattle", "{name}: {mode}, the editor starts {}", s.start);
        seen.insert(s.key());
    }
    // each mode's worlds in the game's order
    for mode in ["adventure", "challenge", "drag", "battle", "joyride", "mpdrag"] {
        let game: Vec<&str> = list.iter().filter(|w| w["mode"] == mode).map(|w| w["name"].as_str().unwrap()).collect();
        let ours: Vec<&str> = songs.iter().filter(|s| s.start.starts_with(&format!("race:mode={mode},"))).map(|s| s.archive).collect();
        assert_eq!(ours, game, "{mode}");
    }
    // every world's archive is in the data (any release's layout), but those the release hasn't:
    // the PC's MCP lists the six multi-player drag worlds its data lacks
    let info = hwvx_browser::content::DataInfo::read(&data).unwrap();
    let missing = hwvx_browser::content_music::songs_missing(&info);
    assert_eq!(missing.len(), if info.release == "pc" { 6 } else { 0 }, "{}: {missing:?}", info.release);
    for s in songs.iter().filter(|s| s.archive != "HW_music") {
        assert!(seen.contains(&s.key()), "{}: no level of the MCP's", s.key());
        assert!(info.has_archive(s.archive) || missing.contains(&s.key()), "{}: no {}", s.key(), s.path);
    }
    // the base songs through the viewer: a world's and a front-end one
    let packs = v.call("packs", json!({})).unwrap();
    for key in ["base://ch1_l2/world_0/song/0", "base://HW_music/music_1/song/0"] {
        let s = song_for(key).unwrap();
        let p = packs["packs"]
            .as_array()
            .unwrap()
            .iter()
            .find(|p| p["pack"].as_str().is_some_and(|x| x.eq_ignore_ascii_case(&format!("{}/{}", s.archive, s.pack))))
            .unwrap_or_else(|| panic!("{key}: no pack in the viewer's packs"));
        v.call("load", json!({"archive": p["archive"], "type": p["type"], "id": p["id"]})).unwrap();
        let r = v.call("play", json!({"node": s.viewer_node()})).unwrap();
        println!("{key}: {r}");
        assert_eq!(r["playing"].as_str(), Some(s.viewer_node().as_str()));
        std::thread::sleep(Duration::from_millis(300));
        let st = v.call("status", json!({})).unwrap();
        assert_eq!(st["audio"]["song"], true, "{st}");
        v.call("stop", json!({})).unwrap();
    }
}

/// One game run (as tools/run_test.sh: a fresh preferences folder, deterministic, unpaced),
/// the mods folder `mods`, HWVX_MUSICLOG and HWVX_AUDIODUMP on: its log and the dump's samples.
fn run_game(data: &std::path::Path, out: &std::path::Path, name: &str, mods: &std::path::Path, start: Option<&str>, secs: u64, extra: &[(&str, &str)]) -> (String, Vec<f32>) {
    let pref = out.join(format!("pref-{name}"));
    let _ = std::fs::remove_dir_all(&pref);
    std::fs::create_dir_all(&pref).unwrap();
    let dump = out.join(format!("{name}.f32"));
    let log = out.join(format!("{name}.log"));
    let mut cmd = Command::new(game());
    cmd.args(["--windowed", "--window", "640x480", "--data"])
        .arg(data)
        .env("HWVX_NO_MOVIE", "1")
        .env("HWVX_NO_DIALOG", "1")
        .env("HWVX_DETERMINISTIC", "1")
        .env("HWVX_SEED", "3")
        .env("HWVX_NO_VIWAIT", "1")
        .env("HWVX_NO_VSYNC", "1")
        .env("SDL_AUDIO_DRIVER", "dummy")
        .env("HWVX_PREF_DIR", &pref)
        .env("HWVX_MODS_DIR", mods)
        .env("HWVX_MUSICLOG", "1")
        .env("HWVX_AUDIODUMP", &dump)
        .env_remove("HWVX_START")
        .env_remove("HWVX_DUMP")
        .stdin(Stdio::null())
        .stdout(Stdio::null());
    let f = std::fs::File::create(&log).unwrap();
    cmd.stderr(f);
    if let Some(s) = start {
        cmd.env("HWVX_START", s);
    }
    for (k, v) in extra {
        cmd.env(k, v);
    }
    let mut child = cmd.spawn().expect("the game starts");
    let end = Instant::now() + Duration::from_secs(secs);
    while Instant::now() < end && child.try_wait().unwrap().is_none() {
        std::thread::sleep(Duration::from_millis(200));
    }
    let _ = child.kill(); // (this child only, by its handle)
    let _ = child.wait();
    let text = std::fs::read_to_string(&log).unwrap_or_default();
    let samples = xdlrs::audiolevel::read_dump(&dump).unwrap_or_default();
    let _ = std::fs::remove_dir_all(&pref);
    (text, samples)
}

/// A tone's share of a stretch of the mono mix: Goertzel power at `hz` over the total power
/// (a pure sine alone scores about 0.5 per side band; 1.0 means all of it).
fn tone_share(samples: &[f32], from_s: f64, to_s: f64, hz: f64) -> (f64, f64) {
    let r = xdlrs::audiolevel::RATE as f64;
    let a = ((from_s * r) as usize).min(samples.len() / 2);
    let b = ((to_s * r) as usize).min(samples.len() / 2);
    let mono: Vec<f64> = (a..b).map(|i| 0.5 * (samples[2 * i] as f64 + samples[2 * i + 1] as f64)).collect();
    if mono.is_empty() {
        return (0.0, 0.0);
    }
    let w = 2.0 * std::f64::consts::PI * hz / r;
    let c = 2.0 * w.cos();
    let (mut s1, mut s2) = (0.0f64, 0.0f64);
    for &x in &mono {
        let s0 = x + c * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    let power = s1 * s1 + s2 * s2 - c * s1 * s2;
    let n = mono.len() as f64;
    let total: f64 = mono.iter().map(|x| x * x).sum::<f64>();
    // a sine of amplitude A: Goertzel power (A n / 2)^2, total power A^2 n / 2
    let share = if total > 0.0 { power / (n * n / 4.0) / (2.0 * total / n) } else { 0.0 };
    let rms = (total / n).sqrt();
    (share, rms)
}

/// Which front-end song plays where: each of HW_music's three replaced by its own tone.
#[test]
#[ignore]
fn menu_songs() {
    let Some(data) = std::env::var_os("HWVX_DATA").map(PathBuf::from) else { return };
    let out = out_dir("music-menus");
    let mods = out.join("mods");
    let mut p = ModProject::create(&mods.join("menus"), "Menu songs", "").unwrap();
    for (i, hz) in [(1, 300.0), (2, 500.0), (3, 700.0)] {
        let f = out.join(format!("m{i}.wav"));
        std::fs::write(&f, wav(44100, 2, &tone(44100, &[(4.0, hz)], 0.4))).unwrap();
        let file = p.import_music(&f).unwrap();
        p.set_music(&format!("base://HW_music/music_{i}"), &[Track::new(&file)]);
    }
    p.save().unwrap();
    let starts = std::env::var("STARTS").unwrap_or_else(|_| "title=".into());
    for (name, start) in starts.split(';').filter_map(|s| s.split_once('=')) {
        let start = Some(start).filter(|s| !s.is_empty());
        let secs = std::env::var("SECS").ok().and_then(|s| s.parse().ok()).unwrap_or(20);
        let (log, samples) = run_game(&data, &out, name, &mods, start, secs, &[("HWVX_AUTOINPUT", std::env::var("AUTOINPUT").unwrap_or_default().as_str())]);
        println!("== {name}: {:.1} s of audio", samples.len() as f64 / 96000.0);
        for l in log.lines().filter(|l| l.contains("music:") || l.contains("mods:")) {
            println!("  {l}");
        }
        for hz in [300.0, 500.0, 700.0] {
            println!("  {hz} Hz: {:?}", tone_share(&samples, 0.0, samples.len() as f64 / 96000.0, hz));
        }
    }
}


/// A mod made as the Content tab makes it (a test tone, its loop points in the entry) replaces
/// Challenge race 1's song (ch1_l2): the game started there with the mod on streams it (the log),
/// its mixed output holds the loop and never what lies past the loop's end (the dump), and Save as
/// mod's check and packer take the mod with its music.
#[test]
fn replacement_plays_in_the_game() {
    let Some(data) = std::env::var_os("HWVX_DATA").map(PathBuf::from) else {
        eprintln!("HWVX_DATA not set: skipped");
        return;
    };
    let out = out_dir("music-game");
    let mods = out.join("mods");
    // 0-2 s 300 Hz, 2-6 s 440 Hz (the loop), 6-8 s 1000 Hz (past the loop's end: never heard)
    let src = out.join("Test Tone.wav");
    std::fs::write(&src, wav(44100, 2, &tone(44100, &[(2.0, 300.0), (4.0, 440.0), (2.0, 1000.0)], 0.4))).unwrap();
    let mut p = ModProject::create(&mods.join("soundtrack"), "Soundtrack test", "").unwrap();
    let file = p.import_music(&src).unwrap();
    let song = song_for("base://ch1_l2/world_0").unwrap();
    p.set_music(&song.key(), &[Track { loop_start: Some(88200), loop_end: Some(264600), ..Track::new(&file) }]);
    assert!(p.music_problems().is_empty());
    p.save().unwrap();
    println!("{}", std::fs::read_to_string(p.dir.join("mod.jsonc")).unwrap());

    let secs = 45;
    let (log, samples) = run_game(&data, &out, "ch1_l2", &mods, Some(&song.start), secs, &[("HWVX_AUTOINPUT", song.input.as_str())]);
    let lines: Vec<&str> = log.lines().filter(|l| l.contains("music:") || l.contains("mods:")).collect();
    for l in lines.iter().take(12) {
        println!("  {l}");
    }
    let streams = lines.iter().any(|l| l.contains("ch1_l2") && l.contains("streams") && l.contains("music/Test Tone.wav (1 track) for its song"));
    assert!(streams, "the game streams the mod's track for ch1_l2's song");
    assert!(lines.iter().any(|l| l.contains("44100 Hz, loop 88200..264600")), "with the entry's loop points");
    let loops = lines.iter().filter(|l| l.contains("loop at 264600 to 88200")).count();
    println!("{loops} loops, {:.1} s of audio", samples.len() as f64 / 96000.0);
    assert!(loops >= 3, "it loops at the loop's end");
    // per second: the tones' share of the mix
    let total = samples.len() as f64 / 96000.0;
    let (mut t440, mut t1000, mut t300) = (0, 0, 0);
    let mut first = None;
    let mut s = 0.0;
    while s + 1.0 <= total {
        let (a, rms) = tone_share(&samples, s, s + 1.0, 440.0);
        let (b, _) = tone_share(&samples, s, s + 1.0, 1000.0);
        let (c, _) = tone_share(&samples, s, s + 1.0, 300.0);
        if a > 0.3 {
            t440 += 1;
            first.get_or_insert(s);
        }
        t1000 += (b > 0.05) as usize;
        t300 += (c > 0.3) as usize;
        if (s as u64) % 5 == 0 {
            println!("  {s:4.0} s: rms {rms:.4}, 300 Hz {c:.3}, 440 Hz {a:.3}, 1000 Hz {b:.3}");
        }
        s += 1.0;
    }
    println!("seconds with 440 Hz: {t440} (from {first:?}), 300 Hz: {t300}, 1000 Hz: {t1000}");
    assert!(t440 >= 15, "the loop's tone is heard");
    assert!(t300 >= 1, "the track's start is heard once");
    assert_eq!(t1000, 0, "nothing past the loop's end");

    // Save as mod: the no-game-data check and the packer take the music
    let root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..");
    let xmod = out.join("soundtrack.xmod");
    let line = xdlrs::level_share::check_and_pack(&root, &p.dir, &xmod, None, Some(&data), &out.join("base-cache")).map_err(|e| xdlrs::level::top_message(&e)).unwrap();
    println!("{line}");
    let bytes = std::fs::read(&xmod).unwrap();
    assert!(bytes.windows(20).any(|w| w == b"music/Test Tone.wav\0" || &w[..19] == b"music/Test Tone.wav"), "the package holds the track");
}
