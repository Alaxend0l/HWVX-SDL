//! File > Extract a disc… (src/extract.rs): the destination's choice and refusal, the size check,
//! the errors in plain words, and whole extractions of tiny disc images built in memory
//! (xdlrs::disc::test_images: a GameCube image, the PS2 DVD, the PC CD as .iso and .cue/.bin),
//! each checked as the game checks its data (data_dir::check). Then the real discs, if given:
//!
//!   HWVX_TEST_PC_DISC=<the PC CD's .cue/.bin or .iso>    (skipped without it)
//!   HWVX_TEST_PS2_DISC=<the PS2 DVD's .iso>              (skipped without it)
//!   cargo test --release --manifest-path tools/browser/Cargo.toml --test extract -- --nocapture
//!
//! Scratch folders are under the checkout's test-out/extract-tests (removed after).

use hwvx_browser::data_dir;
use hwvx_browser::extract::{self, Dest, Progress, SPACE_MARGIN};
use std::path::{Path, PathBuf};
use std::sync::Mutex;
use std::sync::atomic::AtomicBool;
use xdlrs::disc::test_images::{Node, cab_sample, gamecube, iso9660, to_raw};
use xdlrs::disc::{CANCELLED, Release};

/// A scratch folder of the test's own under test-out/, removed when dropped.
struct Tmp(PathBuf);

impl Tmp {
    fn new(name: &str) -> Tmp {
        let p = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../test-out/extract-tests").join(format!("{}-{name}", std::process::id()));
        let _ = std::fs::remove_dir_all(&p);
        std::fs::create_dir_all(&p).unwrap();
        Tmp(p)
    }
    fn write(&self, rel: &str, b: &[u8]) -> PathBuf {
        let p = self.0.join(rel);
        std::fs::create_dir_all(p.parent().unwrap()).unwrap();
        std::fs::write(&p, b).unwrap();
        p
    }
}

impl Drop for Tmp {
    fn drop(&mut self) {
        let _ = std::fs::remove_dir_all(&self.0);
    }
}

fn share_xps(version: u32) -> Vec<u8> {
    let mut b = vec![0u8; 64];
    b[8..12].copy_from_slice(&version.to_le_bytes());
    b
}

fn ps2_image() -> Vec<u8> {
    let share = share_xps(183);
    iso9660(&[
        Node::File("SLUS_204.12", b"elf"),
        Node::Dir("XPS", vec![Node::Dir("ENGLISH", vec![Node::File("HW_SHARE.XPS", &share), Node::Dir("AUDIO_", vec![Node::File("MUSIC.VAG", &[9u8; 5000])])])]),
    ])
}

/// The PC CD with the sample cabinet (a.xpc, b.xpc, c.bin, e: no HW_share.xpc, so its extraction
/// is not game data) and a movie.
fn pc_image() -> Vec<u8> {
    let (cab, _) = cab_sample();
    iso9660(&[Node::File("AUTORUN.INF", b"[autorun]"), Node::File("HWVX.CAB", &cab), Node::Dir("FMV_", vec![Node::File("HWBOOT.BIK", b"bink!")])])
}

fn gc_image() -> Vec<u8> {
    gamecube("GHWE78", &[("HW_share.xgc", b"share"), ("HW_Cars.xgc", &[7u8; 9000]), ("worlds_/w1.xgc", &[1u8; 3000])])
}

fn run(image: &Path, dest: &Path) -> Result<PathBuf, String> {
    extract::run(image, dest, &Mutex::new(Progress::default()), &AtomicBool::new(false), &|| {})
}

#[test]
fn what_a_disc_is() {
    let t = Tmp::new("inspect");
    let gc = extract::inspect(&t.write("hw.iso", &gc_image())).unwrap();
    assert_eq!((gc.release, gc.game_id.as_deref(), gc.files, gc.bytes), (Release::Gc, Some("GHWE78"), 3, 5 + 9000 + 3000));
    assert!(gc.plays_directly());
    assert_eq!(gc.largest, 9000);
    let ps2 = extract::inspect(&t.write("ps2.iso", &ps2_image())).unwrap();
    assert_eq!((ps2.release, ps2.archive_version, ps2.files), (Release::Ps2, Some(183), 2));
    assert!(!ps2.plays_directly());
    // the PC CD as .cue/.bin
    t.write("pc/IMS-HWVX.BIN", &to_raw(&pc_image()));
    let pc = extract::inspect(&t.write("pc/ims-hwvx.cue", b"FILE \"IMS-HWVX.BIN\" BINARY\n  TRACK 01 MODE1/2352\n")).unwrap();
    assert_eq!((pc.release, pc.raw_sectors, pc.files), (Release::Pc, true, 5));
    assert!(pc.kind_text().contains(".cue/.bin"), "{}", pc.kind_text());
    // in plain words: not this game, nothing chosen, a missing track, already extracted
    let other = gamecube("GXXE01", &[("game.bin", b"x")]);
    let e = extract::inspect(&t.write("other.iso", &other)).unwrap_err();
    assert!(e.starts_with("This isn't a Hot Wheels: Velocity X disc"), "{e}");
    assert!(extract::inspect(&t.write("junk.bin", &[0u8; 40000])).unwrap_err().starts_with("This isn't"));
    assert!(extract::inspect(Path::new("")).unwrap_err().starts_with("Choose a disc image"));
    let e = extract::inspect(&t.write("lost.cue", b"FILE \"GONE.BIN\" BINARY\n")).unwrap_err();
    assert!(e.contains("track file (.bin) is missing"), "{e}");
    t.write("done/HW_share.xgc", b"s");
    assert!(extract::inspect(&t.0.join("done")).unwrap_err().contains("nothing to extract"));
}

#[test]
fn plain_errors() {
    assert_eq!(extract::plain_error(CANCELLED), "Cancelled.");
    assert!(extract::plain_error("HWVX.cab: compression type 3 (only MSZIP is read)").starts_with("The PC disc's HWVX.cab uses a compression"));
    assert!(extract::plain_error("x.iso: a Wii disc image, not a GameCube one").starts_with("This is a Wii disc"));
    assert!(extract::plain_error("HW_Cars.xgc lies outside the disc image (a trimmed image?)").starts_with("The disc image is cut short"));
    assert!(extract::plain_error("/out/a.part: No space left on device (os error 28)").starts_with("The drive ran out of space"));
    assert!(extract::plain_error("x.iso: a PS2 disc of an unknown version (150)").starts_with("A PS2 disc of a version"));
    assert_eq!(extract::plain_error("something else"), "something else");
}

#[test]
fn where_it_goes() {
    // beside the pref folder, by release
    assert_eq!(extract::default_dest(Path::new("/home/p/.local/share/hwvx-editor/viewer-pref"), Release::Ps2), Path::new("/home/p/.local/share/hwvx-editor/game-data/ps2"));
    assert_eq!(extract::default_dest(Path::new("/x/viewer-pref"), Release::Ps2Beta), Path::new("/x/game-data/ps2beta"));
    let t = Tmp::new("dest");
    let info = extract::inspect(&t.write("ps2.iso", &ps2_image())).unwrap();
    // missing, empty, empty folders only
    assert_eq!(extract::check_dest(&t.0.join("new"), &info).unwrap(), Dest::Fresh);
    std::fs::create_dir_all(t.0.join("empty/sub")).unwrap();
    t.write("empty/.DS_Store", b"x");
    assert_eq!(extract::check_dest(&t.0.join("empty"), &info).unwrap(), Dest::Fresh);
    // nothing chosen, a file
    assert!(extract::check_dest(Path::new(""), &info).unwrap_err().starts_with("Choose a folder"));
    assert!(extract::check_dest(&t.0.join("ps2.iso"), &info).unwrap_err().contains("is a file"));
    // another's files: refused (the image's own folder too)
    t.write("mine/notes.txt", b"x");
    let e = extract::check_dest(&t.0.join("mine"), &info).unwrap_err();
    assert!(e.contains("already holds other files (notes.txt"), "{e}");
    assert!(extract::check_dest(&t.0, &info).is_err());
    // an earlier extraction of this disc, stopped: some files whole, one .part (any case)
    t.write("prev/XPS/ENGLISH/HW_SHARE.XPS", &share_xps(183));
    t.write("prev/XPS/ENGLISH/AUDIO_/MUSIC.VAG.part", &[9u8; 100]);
    assert_eq!(extract::check_dest(&t.0.join("prev"), &info).unwrap(), Dest::Previous { files: 1, bytes: 64, parts: 1 });
    t.write("prev2/xps/english/hw_share.xps", &share_xps(183));
    assert!(matches!(extract::check_dest(&t.0.join("prev2"), &info).unwrap(), Dest::Previous { files: 1, .. }));
    // the same name at another size: another disc's
    t.write("other/XPS/ENGLISH/HW_SHARE.XPS", b"short");
    assert!(extract::check_dest(&t.0.join("other"), &info).unwrap_err().contains("of another size"));
    // a link is not followed: it is another file, even named as the disc's (a .part through it
    // would be written wherever it points)
    #[cfg(unix)]
    {
        let _ = std::fs::create_dir_all(t.0.join("haslink/XPS/ENGLISH"));
        std::os::unix::fs::symlink(t.0.join("ps2.iso"), t.0.join("haslink/XPS/ENGLISH/HW_SHARE.XPS.part")).unwrap();
        assert!(extract::check_dest(&t.0.join("haslink"), &info).unwrap_err().contains("other files"));
        std::os::unix::fs::symlink(t.0.join("prev"), t.0.join("haslink2")).unwrap();
        // (the destination itself may be a link: the player chose it)
        assert!(matches!(extract::check_dest(&t.0.join("haslink2"), &info).unwrap(), Dest::Previous { .. }));
    }
}

#[test]
fn the_size_check() {
    let t = Tmp::new("size");
    let info = extract::inspect(&t.write("ps2.iso", &ps2_image())).unwrap();
    assert_eq!(info.bytes, 64 + 5000);
    assert_eq!(extract::space_needed(&info, &Dest::Fresh), 5064);
    // an earlier extraction's files count, but the largest is rewritten beside its old copy
    assert_eq!(extract::space_needed(&info, &Dest::Previous { files: 1, bytes: 64, parts: 0 }), 5000 + 64);
    assert_eq!(extract::space_needed(&info, &Dest::Previous { files: 2, bytes: 5064, parts: 0 }), 5000);
    // the margin, unknown free space, the message
    assert!(extract::check_space(5064, Some(5064 + SPACE_MARGIN), "x").is_ok());
    assert!(extract::check_space(5064, None, "x").is_ok());
    assert_eq!(extract::human(1_469_300_000_000), "1.5 TB");
    let e = extract::check_space(1_900_000_000, Some(1_200_000_000), &extract::what(Release::Ps2)).unwrap_err();
    assert!(e.starts_with("Not enough free space: the PlayStation 2 disc's files need 2.0 GB, and the drive has 1.2 GB free."), "{e}");
    // plan: the folder's check and then the space (given here)
    let p = extract::plan(&t.0.join("out"), &info, Some(Some(1 << 40))).unwrap();
    assert_eq!((p.found, p.needed), (Dest::Fresh, 5064));
    assert!(extract::plan(&t.0.join("out"), &info, Some(Some(1000))).unwrap_err().starts_with("Not enough free space"));
    // this drive's free space is known (test-out is on one)
    assert!(extract::free_space(&t.0.join("not/yet/made")).is_some_and(|f| f > 0));
}

#[test]
fn extracts_and_the_game_finds_it() {
    let t = Tmp::new("run");
    // the PS2 DVD: XPS/ENGLISH/...
    let img = t.write("ps2.iso", &ps2_image());
    let out = t.0.join("ps2-out");
    assert_eq!(run(&img, &out).unwrap(), out);
    assert_eq!(std::fs::read(out.join("XPS/ENGLISH/AUDIO_/MUSIC.VAG")).unwrap(), vec![9u8; 5000]);
    assert_eq!(data_dir::check(&out).unwrap(), out);
    // again into the same folder: a previous extraction of this disc, accepted
    assert!(matches!(extract::check_dest(&out, &extract::inspect(&img).unwrap()).unwrap(), Dest::Previous { files: 2, parts: 0, .. }));
    run(&img, &out).unwrap();
    // another disc into it: refused, nothing written
    let gc = t.write("gc.iso", &gc_image());
    assert!(run(&gc, &out).unwrap_err().contains("already holds other files"));
    assert!(!out.join("HW_share.xgc").exists());
    // a GameCube image into files
    let gout = t.0.join("gc-out");
    run(&gc, &gout).unwrap();
    assert_eq!(std::fs::read(gout.join("worlds_/w1.xgc")).unwrap(), vec![1u8; 3000]);
    assert_eq!(data_dir::check(&gout).unwrap(), gout);
    // the PC CD's .cue/.bin: extracted (the sample cabinet holds no HW_share.xpc: the game
    // wouldn't find its data, and says so)
    t.write("pc/IMS-HWVX.BIN", &to_raw(&pc_image()));
    let cue = t.write("pc/ims-hwvx.cue", b"FILE \"IMS-HWVX.BIN\" BINARY\n");
    let pout = t.0.join("pc-out");
    let e = run(&cue, &pout).unwrap_err();
    assert!(e.starts_with("The files were extracted, but the game wouldn't find its data there"), "{e}");
    assert_eq!(std::fs::read(pout.join("FMV_/hwboot.bik")).unwrap(), b"bink!");
    assert!(pout.join("a.xpc").is_file() && pout.join("e").is_file());
    // not enough room: refused before writing (a drive with 1000 bytes free)
    let info = extract::inspect(&img).unwrap();
    assert!(extract::plan(&t.0.join("small"), &info, Some(Some(1000))).is_err());
    assert!(!t.0.join("small").exists());
}

#[test]
fn a_job_runs_and_cancels() {
    let t = Tmp::new("job");
    let img = t.write("gc.iso", &gc_image());
    let job = extract::Job::start(&img, &t.0.join("out"), || {});
    let r = loop {
        if let Some(r) = job.poll() {
            break r;
        }
        std::thread::sleep(std::time::Duration::from_millis(5));
    };
    assert_eq!(r.unwrap(), t.0.join("out"));
    let p = job.snapshot();
    assert_eq!((p.files, p.files_total, p.bytes, p.fraction()), (3, 3, 12005, 1.0));
    // cancelled at once: stops after the first file, the rest not written
    let cancel = AtomicBool::new(true);
    let prog = Mutex::new(Progress::default());
    let e = extract::run(&img, &t.0.join("cancelled"), &prog, &cancel, &|| {}).unwrap_err();
    assert_eq!(e, CANCELLED);
    assert_eq!(prog.lock().unwrap().files, 1);
    // ... and what it left is a previous extraction of this disc
    let info = extract::inspect(&img).unwrap();
    assert!(matches!(extract::check_dest(&t.0.join("cancelled"), &info).unwrap(), Dest::Previous { files: 1, .. }));
}

/// The real discs, extracted into test-out and checked as the game checks its data, then removed.
#[test]
fn real_discs() {
    for (var, release) in [("HWVX_TEST_PC_DISC", Release::Pc), ("HWVX_TEST_PS2_DISC", Release::Ps2)] {
        let Some(img) = std::env::var_os(var).map(PathBuf::from).filter(|p| p.exists()) else {
            eprintln!("{var} not set: skipped");
            continue;
        };
        let info = extract::inspect(&img).unwrap();
        assert_eq!(info.release, release);
        assert!(!info.plays_directly());
        let t = Tmp::new(release.key());
        let out = t.0.join("game-data").join(release.key());
        assert_eq!(extract::default_dest(&t.0.join("viewer-pref"), release), out);
        let p = extract::plan(&out, &info, None).unwrap();
        assert_eq!((p.found.clone(), p.needed), (Dest::Fresh, info.bytes));
        let started = std::time::Instant::now();
        let prog = Mutex::new(Progress::default());
        let data = extract::run(&img, &out, &prog, &AtomicBool::new(false), &|| {}).unwrap();
        let pr = prog.lock().unwrap().clone();
        eprintln!(
            "{var}: {} ({}) -> {} files, {} in {:.1} s; data_dir::check: {}",
            img.display(),
            info.kind_text(),
            pr.files,
            extract::human(pr.bytes),
            started.elapsed().as_secs_f64(),
            data.display()
        );
        assert_eq!((pr.files, pr.bytes), (info.files, info.bytes));
        assert_eq!(data, out);
        // nothing left as .part, and the folder is a previous extraction of this disc
        assert!(matches!(extract::check_dest(&out, &info).unwrap(), Dest::Previous { parts: 0, .. }));
        // (Tmp's drop removes the extraction)
    }
}
