//! The editor's movie player and Audio browser (src/movie.rs, src/ffmpeg.rs, src/audio_index.rs).
//!
//! Without game data: THP's JPEG stuffing, the WAV writer, a PSS audio track built here, the
//! Audio browser's search. With it (each skipped when its variable isn't set):
//!
//!   HWVX_DATA=<GameCube files>  HWVX_DATA_PC=<PC folder>  HWVX_DATA_PS2=<PS2 folder>
//!   HWVX_PSS_PROTO=<a folder with the PS2 prototype's .PSS movies>
//!   cargo test --release --manifest-path tools/browser/Cargo.toml --test movies -- --nocapture
//!
//! one movie per release plays: pictures arrive (not black, changing), a seek lands on its frame,
//! and the soundtrack isn't silent and lasts as long as the picture; and every sound and bank
//! sample of every archive decodes to as many samples as its descriptor says.

use hwvx_browser::audio_index::{self, Kind as AKind};
use hwvx_browser::media::{read_wav, wav_bytes_channels};
use hwvx_browser::movie::{self, Kind, MovieFile, PssAudio, VideoStream};
use std::path::PathBuf;

fn out_dir() -> PathBuf {
    let d = std::env::var_os("HWVX_TEST_OUT").map(PathBuf::from).unwrap_or_else(|| PathBuf::from(env!("CARGO_TARGET_TMPDIR")).join("movies"));
    std::fs::create_dir_all(&d).unwrap();
    d
}

#[test]
fn thp_jpeg_is_stuffed() {
    // SOI, an SOS of length 2 (no fields), entropy data with a bare 0xFF, EOI, padding
    let j = [0xFF, 0xD8, 0xFF, 0xDA, 0x00, 0x02, 0x12, 0xFF, 0x34, 0xFF, 0xD9, 0, 0];
    let s = movie::thp_jpeg_standard(&j).unwrap();
    assert_eq!(s, vec![0xFF, 0xD8, 0xFF, 0xDA, 0x00, 0x02, 0x12, 0xFF, 0x00, 0x34, 0xFF, 0xD9]);
    assert!(movie::thp_jpeg_standard(&[1, 2, 3, 4]).is_none());
}

#[test]
fn stereo_wav_round_trip() {
    let pcm: Vec<i16> = (0..200).map(|i| (i * 100) as i16).collect();
    let w = wav_bytes_channels(32000, 2, &pcm);
    assert_eq!(u16::from_le_bytes([w[22], w[23]]), 2);
    assert_eq!(u32::from_le_bytes(w[28..32].try_into().unwrap()), 32000 * 4);
    let (rate, left) = read_wav(&w).unwrap();
    assert_eq!(rate, 32000);
    assert_eq!(left, pcm.iter().step_by(2).copied().collect::<Vec<_>>());
}

/// A program stream with one 0xBD packet (the PS2's headers and 4 frames of stereo PCM in two
/// 4-byte interleave blocks) and an end code.
#[test]
fn pss_audio_track() {
    let mut payload = vec![0xA0, 0, 0]; // sub-stream, two zero bytes
    payload.extend_from_slice(b"SShd");
    payload.extend_from_slice(&0x18u32.to_le_bytes());
    for v in [1u32, 48000, 2, 4] {
        payload.extend_from_slice(&v.to_le_bytes());
    }
    payload.extend_from_slice(&[0xFF; 8]);
    payload.extend_from_slice(b"SSbd");
    payload.extend_from_slice(&16u32.to_le_bytes());
    // L0 L1 | R0 R1 | L2 L3 | R2 R3
    for s in [1i16, 2, -1, -2, 3, 4, -3, -4] {
        payload.extend_from_slice(&s.to_le_bytes());
    }
    let pts: u64 = 90000; // 1 s
    let ptsb = [0x21 | ((pts >> 29) & 0x0E) as u8, (pts >> 22) as u8, 1 | ((pts >> 14) & 0xFE) as u8, (pts >> 7) as u8, 1 | ((pts << 1) & 0xFE) as u8];
    let mut pes = vec![0x81, 0x80, 5];
    pes.extend_from_slice(&ptsb);
    pes.push(0xFF); // the stuffing byte the header doesn't count
    pes.extend_from_slice(&payload);
    let mut b = vec![0, 0, 1, 0xBD];
    b.extend_from_slice(&(pes.len() as u16).to_be_bytes());
    b.extend_from_slice(&pes);
    b.extend_from_slice(&[0, 0, 1, 0xB9]);
    let f = out_dir().join("synthetic.pss");
    std::fs::write(&f, &b).unwrap();
    let a = PssAudio::read(&f).unwrap();
    assert_eq!((a.channels, a.rate), (2, 48000));
    assert_eq!(a.pcm, vec![1, -1, 2, -2, 3, -3, 4, -4]);
    assert!((a.start.unwrap() - 1.0).abs() < 1e-6);
}

#[test]
fn audio_search() {
    let e = audio_index::Entry {
        kind: AKind::Sound,
        archive: "HW_Cars".into(),
        pack: "car_5".into(),
        typ: 0,
        id: 5,
        index: 3,
        release: "gc",
        rate: 22050,
        samples: 44100,
        loop_start: Some(10),
        file: PathBuf::new(),
        offset: 0,
        bytes: 88200,
        search: "hw_cars car_5 sound/3 sound base://hw_cars/car_5/sound/3".into(),
    };
    assert_eq!(e.id(), "base://HW_Cars/car_5/sound/3");
    assert!((e.seconds() - 2.0).abs() < 1e-9);
    assert!(audio_index::matches(&e, "cars SOUND/3", &[]));
    assert!(audio_index::matches(&e, "car_5", &[AKind::Sound]));
    assert!(!audio_index::matches(&e, "car_5", &[AKind::Song]));
    assert!(!audio_index::matches(&e, "items", &[]));
}

#[test]
fn movie_kinds() {
    let d = out_dir().join("kinds");
    std::fs::create_dir_all(d.join("fmv_")).unwrap();
    std::fs::write(d.join("fmv_/intro"), b"THP\0rest").unwrap();
    std::fs::write(d.join("fmv_/notes"), b"text").unwrap();
    std::fs::write(d.join("a.BIK"), b"BIKi").unwrap();
    std::fs::write(d.join("trailer.mp4"), b"").unwrap();
    std::fs::write(d.join("noext"), b"THP\0").unwrap(); // not in a movie folder: not looked into
    let m = movie::find_movies(&d);
    let got: Vec<(String, Kind)> = m.iter().map(|m| (m.name.clone(), m.kind)).collect();
    assert_eq!(got, vec![("a.BIK".into(), Kind::Bink), ("fmv_/intro".into(), Kind::Thp), ("trailer.mp4".into(), Kind::Other)]);
}

// ---- with game data ------------------------------------------------------------------------

fn releases() -> Vec<(&'static str, PathBuf)> {
    [("gc", "HWVX_DATA"), ("pc", "HWVX_DATA_PC"), ("ps2", "HWVX_DATA_PS2"), ("ps2-proto", "HWVX_PSS_PROTO")]
        .into_iter()
        .filter_map(|(r, v)| std::env::var_os(v).map(|d| (r, PathBuf::from(d))))
        .collect()
}

fn check_movie(release: &str, m: &MovieFile) {
    let info = match movie::probe(m) {
        Ok(i) => i,
        Err(e) if e.contains("FFmpeg not found") => {
            println!("{release} {}: {e} (skipped)", m.name);
            return;
        }
        Err(e) => panic!("{release} {}: {e}", m.name),
    };
    println!("{release} {}: {}", m.name, info.summary());
    assert!(info.width > 0 && info.height > 0 && info.fps > 1.0 && info.frames > 30, "{release} {}: {info:?}", m.name);
    // pictures: 45 from the start, not black, changing
    let t0 = std::time::Instant::now();
    let vs = VideoStream::start(m, 0, 4).unwrap();
    let (mut lit, mut changed, mut prev): (u32, u32, Option<Vec<u8>>) = (0, 0, None);
    for k in 0..45 {
        let p = vs.wait().unwrap_or_else(|| panic!("{release} {}: ended at {k}", m.name)).unwrap();
        assert_eq!(p.index, k);
        assert_eq!(p.rgba.len(), (info.width * info.height * 4) as usize);
        let mean = p.rgba.chunks_exact(4).map(|c| c[0] as u64 + c[1] as u64 + c[2] as u64).sum::<u64>() / (info.width * info.height) as u64;
        if mean > 6 {
            lit += 1;
        }
        if prev.as_ref().is_some_and(|q| q != &p.rgba) {
            changed += 1;
        }
        if k == 44 {
            let path = out_dir().join(format!("{release}-{}-f44.png", m.name.replace('/', "_")));
            hwvx_browser::media::write_png(&path, info.width, info.height, &p.rgba).unwrap();
        }
        prev = Some(p.rgba);
    }
    let dt = t0.elapsed().as_secs_f64();
    println!("  45 pictures in {dt:.2} s ({:.0} fps), {lit} not black, {changed} changed from the one before", 45.0 / dt);
    assert!(lit > 0 && changed > 0, "{release} {}: pictures black or still", m.name);
    drop(vs);
    // a seek: from frame 90 on
    let from = (info.frames / 2).min(90);
    let vs = VideoStream::start(m, from, 2).unwrap();
    let p = vs.wait().unwrap().unwrap();
    assert_eq!(p.index, from, "{release} {}: the seek's first picture", m.name);
    drop(vs);
    // the soundtrack
    let a = movie::decode_audio(m).unwrap().unwrap_or_else(|| panic!("{release} {}: no audio", m.name));
    println!("  audio {} ch {} Hz, {:.2} s (picture {:.2} s), peak {:.3}", a.channels, a.rate, a.seconds(), info.seconds, a.peak());
    assert!(a.peak() > 0.01, "{release} {}: silent", m.name);
    assert!((a.seconds() - info.seconds).abs() < 1.5, "{release} {}: audio {:.2} s against {:.2} s", m.name, a.seconds(), info.seconds);
    std::fs::write(out_dir().join(format!("{release}-{}.wav", m.name.replace('/', "_"))), a.wav_from(0.0)).unwrap();
}

#[test]
fn movies_play_per_release() {
    let rs = releases();
    if rs.is_empty() {
        eprintln!("no game data (HWVX_DATA...): skipped");
        return;
    }
    for (release, data) in rs {
        let all = movie::find_movies(&data);
        println!("{release}: {} movie(s): {}", all.len(), all.iter().map(|m| format!("{} ({})", m.name, m.kind.name())).collect::<Vec<_>>().join(", "));
        let game: Vec<&MovieFile> = all.iter().filter(|m| m.kind != Kind::Other).collect();
        assert!(!game.is_empty(), "{release}: no movies in {}", data.display());
        // the smallest (a logo), and the PS2 prototype's every one
        let mut pick: Vec<&MovieFile> = vec![*game.iter().min_by_key(|m| m.bytes).unwrap()];
        if release == "ps2-proto" {
            pick = game.clone();
        }
        for m in pick {
            check_movie(release, m);
        }
    }
}

#[test]
fn every_sound_decodes() {
    let rs: Vec<_> = releases().into_iter().filter(|(r, _)| !r.contains("proto")).collect();
    if rs.is_empty() {
        eprintln!("no game data (HWVX_DATA...): skipped");
        return;
    }
    for (release, data) in rs {
        let mut notes = Vec::new();
        let all = audio_index::index_all(&data, |f, r| {
            if let Err(e) = r {
                notes.push(format!("{}: {e}", f.display()));
            }
        });
        let count = |k: AKind| all.iter().filter(|e| e.kind == k).count();
        let archives: std::collections::BTreeSet<&str> = all.iter().map(|e| e.archive.as_str()).collect();
        println!(
            "{release}: {} archives with audio, {} sounds, {} songs, {} instruments{}",
            archives.len(),
            count(AKind::Sound),
            count(AKind::Song),
            count(AKind::Instrument),
            if notes.is_empty() { String::new() } else { format!("; not read: {}", notes.join("; ")) }
        );
        assert!(count(AKind::Sound) > 100 && count(AKind::Song) > 0, "{release}: too little audio");
        let (mut ok, mut silent, mut seconds, mut high) = (0, 0, 0.0, 0);
        for e in all.iter().filter(|e| matches!(e.kind, AKind::Sound | AKind::Instrument)) {
            let pcm = e.decode().unwrap_or_else(|err| panic!("{release} {}: {err}", e.id()));
            assert_eq!(pcm.len() as u32, e.samples, "{release} {}: samples", e.id());
            // (a bank sample's rate is at its base note: anywhere from 2920 to 220942 Hz)
            assert!(e.kind == AKind::Instrument || (4000..=96000).contains(&e.rate), "{release} {}: rate {}", e.id(), e.rate);
            if e.rate > 96000 || e.rate < 4000 {
                high += 1;
            }
            if pcm.iter().all(|&s| s.unsigned_abs() < 64) {
                silent += 1;
            }
            seconds += e.seconds();
            ok += 1;
        }
        println!("  {ok} samples decode ({seconds:.0} s of audio), {silent} near-silent, {high} instruments outside 4-96 kHz at their base note");
        for e in all.iter().filter(|e| e.kind == AKind::Song) {
            let b = e.raw().unwrap();
            assert_eq!(&b[0..4], b"MThd", "{release} {}: a standard MIDI file", e.id());
        }
    }
}

/// The Audio browser against the viewer (GameCube data, HWVX_DATA): every indexed pack is one
/// the viewer lists (what "Open in pack" and a song's ▶ load), and a pack's sounds as the viewer's
/// `sounds` reply describes them (rate, length, loop) are the index's.
#[test]
fn audio_index_matches_viewer() {
    use hwvx_browser::client::{SpawnOptions, Viewer, default_game, default_schemas};
    use serde_json::json;
    let Some(data) = std::env::var_os("HWVX_DATA").map(PathBuf::from) else {
        eprintln!("HWVX_DATA not set: skipped");
        return;
    };
    let game = std::env::var_os("HWVX_GAME").map(PathBuf::from).unwrap_or_else(|| PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../../build/hwvx"));
    let game = std::fs::canonicalize(&game).unwrap_or_else(|_| default_game());
    let out = out_dir().join("viewer");
    std::fs::create_dir_all(&out).unwrap();
    let mut v = Viewer::spawn(&SpawnOptions {
        schemas: default_schemas(&game),
        game,
        data: Some(data.clone()),
        pref: out.join("pref"),
        window: Some("320x240".into()),
        renderer: None,
        log_file: Some(out.join("viewer.log")),
        token: None,
        viewer_window: false,
    })
    .expect("the viewer starts");
    let packs = v.call("packs", json!({})).unwrap();
    let list: Vec<(String, String, u64)> = packs["packs"]
        .as_array()
        .unwrap()
        .iter()
        .map(|p| (p["archive"].as_str().unwrap_or("").to_string(), p["type"].as_str().unwrap_or("").to_string(), p["id"].as_u64().unwrap_or(0)))
        .collect();
    let all = audio_index::index_all(&data, |_, _| {});
    let mut missing = std::collections::BTreeSet::new();
    let mut found = 0;
    for e in all.iter().filter(|e| matches!(e.kind, AKind::Sound | AKind::Song | AKind::Instrument)) {
        match audio_index::viewer_pack(e, list.iter().map(|(a, t, i)| (a.as_str(), t.as_str(), *i))) {
            Some(_) => found += 1,
            None => {
                missing.insert(format!("{}/{}", e.archive, e.pack));
            }
        }
    }
    println!("{found} entries in packs the viewer lists; not listed: {missing:?}");
    assert!(missing.is_empty(), "packs the viewer doesn't list: {missing:?}");
    // a car pack's sounds, as the viewer has them
    for (archive, typ, id, pack) in [("HW_Cars", "car", 5u64, "car_5"), ("HW_Items", "item", 1, "item_1")] {
        let p = list.iter().find(|(a, t, i)| a.eq_ignore_ascii_case(archive) && t == typ && *i == id);
        let Some((a, t, i)) = p else { continue };
        v.call("load", json!({"archive": a, "type": t, "id": i})).unwrap();
        let snd = v.call("sounds", json!({})).unwrap();
        let ours: Vec<&audio_index::Entry> = all.iter().filter(|e| e.kind == AKind::Sound && e.archive.eq_ignore_ascii_case(archive) && e.pack == pack).collect();
        let theirs = snd["sounds"].as_array().unwrap();
        assert_eq!(ours.len(), theirs.len(), "{archive}/{pack}: sounds");
        for (e, s) in ours.iter().zip(theirs) {
            assert_eq!(s["rate"].as_u64().unwrap() as u32, e.rate, "{}: rate", e.id());
            assert_eq!(s["length"].as_u64().unwrap() as u32, e.samples, "{}: length", e.id());
            assert_eq!(s["loop"].as_u64().map(|l| l as u32), e.loop_start, "{}: loop", e.id());
        }
        println!("{archive}/{pack}: {} sounds, rate, length and loop as the viewer says", ours.len());
    }
    let _ = v.call("quit", json!({}));
}

/// Without FFmpeg (HWVX_FFMPEG_DIR naming an empty folder, in a child process: FFmpeg loads once a
/// process): a Bink or PSS movie says "FFmpeg not found", and a THP movie (HWVX_DATA) still plays.
#[test]
fn without_ffmpeg() {
    if std::env::var_os("HWVX_TEST_NO_FFMPEG_CHILD").is_none() {
        let empty = out_dir().join("no-ffmpeg");
        std::fs::create_dir_all(&empty).unwrap();
        let st = std::process::Command::new(std::env::current_exe().unwrap())
            .args(["without_ffmpeg", "--exact", "--nocapture"])
            .env("HWVX_TEST_NO_FFMPEG_CHILD", "1")
            .env("HWVX_FFMPEG_DIR", &empty)
            .status()
            .unwrap();
        assert!(st.success(), "the child run failed");
        return;
    }
    let e = hwvx_browser::ffmpeg::load().err().expect("no FFmpeg in an empty HWVX_FFMPEG_DIR");
    println!("{e}");
    assert!(e.starts_with("FFmpeg not found"));
    let fake = out_dir().join("no-ffmpeg/fake.bik");
    std::fs::write(&fake, b"BIKi").unwrap();
    let m = MovieFile { path: fake, name: "fake.bik".into(), kind: Kind::Bink, bytes: 4 };
    let err = movie::probe(&m).unwrap_err();
    assert!(err.contains("FFmpeg not found"), "{err}");
    if let Some(data) = std::env::var_os("HWVX_DATA").map(PathBuf::from) {
        let thp = movie::find_movies(&data).into_iter().find(|m| m.kind == Kind::Thp).expect("a THP movie");
        let info = movie::probe(&thp).unwrap();
        let vs = VideoStream::start(&thp, 0, 2).unwrap();
        assert!(vs.wait().unwrap().is_ok());
        assert!(movie::decode_audio(&thp).unwrap().unwrap().peak() > 0.01);
        println!("without FFmpeg: {} still plays ({})", thp.name, info.summary());
    }
}
