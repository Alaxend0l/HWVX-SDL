//! The ripper on tiny images built here: a GameCube image (and its .gcz), PS2 DVDs (release and
//! prototype), the PC CD (.iso, .cue/.bin, a folder) with a cabinet, extracted folders.

use super::iso::tests::Node;
use super::*;
use std::collections::BTreeMap;

/// A scratch folder of the test's own, removed when dropped.
struct Tmp(PathBuf);

impl Tmp {
    fn new(name: &str) -> Tmp {
        let p = std::env::temp_dir().join(format!("xdlrs-disc-{}-{name}", std::process::id()));
        let _ = fs::remove_dir_all(&p);
        fs::create_dir_all(&p).unwrap();
        Tmp(p)
    }
    fn write(&self, rel: &str, b: &[u8]) -> PathBuf {
        let p = self.0.join(rel);
        fs::create_dir_all(p.parent().unwrap()).unwrap();
        fs::write(&p, b).unwrap();
        p
    }
}

impl Drop for Tmp {
    fn drop(&mut self) {
        let _ = fs::remove_dir_all(&self.0);
    }
}

/// Every file under `dir`: relative path -> bytes.
fn tree(dir: &Path) -> BTreeMap<String, Vec<u8>> {
    let mut v = vec![];
    walk_dir(dir, "", &mut v, 0).unwrap();
    v.into_iter().map(|e| (e.path.clone(), match e.origin { Origin::File(p) => fs::read(p).unwrap(), _ => unreachable!() })).collect()
}

fn gc_files() -> Vec<(&'static str, Vec<u8>)> {
    vec![
        ("HW_share.xgc", b"share".to_vec()),
        ("HW_Cars.xgc", (0..70_000u32).map(|i| (i % 253) as u8).collect()),
        ("HW_IPack.xgc", b"ipack".to_vec()),
        ("worlds_/world1.xgc", vec![1u8; 3000]),
        ("worlds_/sub/world2.xgc", vec![2u8; 10]),
        ("opening.bnr", vec![]),
    ]
}

fn gc_image() -> Vec<u8> {
    let f = gc_files();
    let refs: Vec<(&str, &[u8])> = f.iter().map(|(p, b)| (*p, b.as_slice())).collect();
    gcm::tests::make("GHWE78", &refs)
}

fn check_gc(d: &Disc, out: &Path) {
    assert_eq!(d.release, Release::Gc);
    assert_eq!(d.game_id.as_deref(), Some("GHWE78"));
    assert_eq!(d.entries.len(), 6);
    assert_eq!(d.read("hw_ipack.xgc").unwrap(), b"ipack");
    assert_eq!(d.read("worlds_\\SUB\\world2.xgc").unwrap(), vec![2u8; 10]);
    assert_eq!(d.find_archive("hw_cars").unwrap().path, "HW_Cars.xgc");
    let (n, bytes) = d.extract(out, &[], &mut |_| {}).unwrap();
    assert_eq!(n, 6);
    assert_eq!(bytes, d.total_size());
    let want: BTreeMap<String, Vec<u8>> = gc_files().into_iter().map(|(p, b)| (p.to_string(), b)).collect();
    assert_eq!(tree(out), want);
    // the extraction reads back as the GameCube's files
    let again = Disc::open(out).unwrap();
    assert_eq!((again.kind, again.release, again.entries.len()), (Kind::Extracted, Release::Gc, 6));
}

#[test]
fn gamecube_image() {
    let t = Tmp::new("gc");
    let img = t.write("Hot Wheels.iso", &gc_image());
    let d = Disc::open(&img).unwrap();
    assert_eq!(d.kind, Kind::GcImage);
    assert!(!d.nkit);
    check_gc(&d, &t.0.join("out"));
    // --only
    let only = t.0.join("only");
    let (n, _) = d.extract(&only, &["worlds_/*".into()], &mut |_| {}).unwrap();
    assert_eq!(n, 2);
    assert_eq!(tree(&only).keys().collect::<Vec<_>>(), ["worlds_/sub/world2.xgc", "worlds_/world1.xgc"]);
    let (n, _) = d.extract(&t.0.join("only2"), &["hw_*.XGC".into()], &mut |_| {}).unwrap();
    assert_eq!(n, 3);
    // read one without extracting
    assert_eq!(find_archive(&img, "HW_IPack").unwrap().1, b"ipack");
    assert_eq!(read_file(&img, "HW_share.xgc").unwrap(), b"share");
}

#[test]
fn gamecube_nkit_and_gcz() {
    let t = Tmp::new("gcz");
    let mut img = gc_image();
    img[0x200..0x204].copy_from_slice(b"NKIT");
    let gz = t.write("hw.nkit.gcz", &gcz::tests::make(&img, 4096));
    let d = Disc::open(&gz).unwrap();
    assert_eq!(d.kind, Kind::Gcz);
    assert!(d.nkit);
    assert_eq!(d.disc_size, img.len() as u64);
    check_gc(&d, &t.0.join("out"));
}

#[test]
fn not_velocity_x() {
    let t = Tmp::new("other");
    let other = gcm::tests::make("GXXE01", &[("game.bin", b"x")]);
    assert!(Disc::open(&t.write("other.iso", &other)).unwrap_err().contains("not Hot Wheels"));
    assert!(Disc::open(&t.write("junk.iso", &[0u8; 40000])).is_err());
    assert!(Disc::open(&t.0).unwrap_err().contains("no Velocity X data"));
    let iso = iso::tests::make(&[Node::File("README.TXT", b"hi")]);
    assert!(Disc::open(&t.write("cd.iso", &iso)).unwrap_err().contains("neither"));
}

fn share_xps(version: u32) -> Vec<u8> {
    let mut b = vec![0u8; 64];
    b[0..4].copy_from_slice(b"\x01\x02\x03\x04");
    b[8..12].copy_from_slice(&version.to_le_bytes());
    b
}

#[test]
fn ps2_disc_and_prototype() {
    let t = Tmp::new("ps2");
    for (v, rel) in [(183, Release::Ps2), (177, Release::Ps2Beta), (179, Release::Ps2Beta)] {
        let share = share_xps(v);
        let img = iso::tests::make(&[
            Node::File("SLUS_204.12", b"elf"),
            Node::Dir("IOP", vec![Node::File("SIO2MAN.IRX", b"iop")]),
            Node::Dir("XPS", vec![Node::Dir("ENGLISH", vec![Node::File("HW_SHARE.XPS", &share), Node::Dir("AUDIO_", vec![Node::File("MUSIC.VAG", &[9u8; 4100])])])]),
        ]);
        let p = t.write(&format!("ps2-{v}.iso"), &img);
        let d = Disc::open(&p).unwrap();
        assert_eq!((d.kind, d.release, d.archive_version), (Kind::Iso, rel, Some(v)));
        let out = t.0.join(format!("out{v}"));
        d.extract(&out, &[], &mut |_| {}).unwrap();
        let got = tree(&out);
        assert_eq!(got.keys().collect::<Vec<_>>(), ["XPS/ENGLISH/AUDIO_/MUSIC.VAG", "XPS/ENGLISH/HW_SHARE.XPS"]);
        assert_eq!(got["XPS/ENGLISH/HW_SHARE.XPS"], share);
        // the extraction (and a PS2 disc's files as a folder) read back as that release
        let again = Disc::open(&out).unwrap();
        assert_eq!((again.kind, again.release, again.entries.len()), (Kind::Extracted, rel, 2));
        assert_eq!(find_archive(&p, "hw_share").unwrap().1, share);
    }
    let bad = iso::tests::make(&[Node::Dir("XPS", vec![Node::Dir("ENGLISH", vec![Node::File("HW_SHARE.XPS", &share_xps(150))])])]);
    assert!(Disc::open(&t.write("bad.iso", &bad)).unwrap_err().contains("unknown version"));
}

fn pc_tree(cab: &[u8], movie: &[u8]) -> Vec<u8> {
    iso::tests::make(&[
        Node::File("AUTORUN.INF", b"[autorun]"),
        Node::File("HWVX.CAB", cab),
        Node::Dir("FMV_", vec![Node::File("HWBOOT.BIK", movie), Node::File("OTHER.BIK", b"no")]),
    ])
}

fn check_pc(d: &Disc, out: &Path, want: &[(&str, Vec<u8>)], movie: &[u8]) {
    assert_eq!(d.release, Release::Pc);
    let names: Vec<&str> = d.entries.iter().map(|e| e.path.as_str()).collect();
    assert_eq!(names, ["a.xpc", "b.xpc", "c.bin", "e", "FMV_/hwboot.bik"]);
    for (n, b) in want {
        assert_eq!(&d.read(n).unwrap(), b, "{n}");
    }
    let (n, _) = d.extract(out, &[], &mut |_| {}).unwrap();
    assert_eq!(n, 5);
    let got = tree(out);
    for (name, b) in want {
        assert_eq!(&got[*name], b, "{name}");
    }
    assert_eq!(got["FMV_/hwboot.bik"], movie);
    assert_eq!(got.len(), 5);
}

#[test]
fn pc_cd_image_bin_and_folder() {
    let t = Tmp::new("pc");
    let (cab, want) = cab::tests::sample();
    let movie: Vec<u8> = (0..5000u32).map(|i| i as u8).collect();
    let iso = pc_tree(&cab, &movie);
    // .iso
    let p = t.write("HWVX.iso", &iso);
    let d = Disc::open(&p).unwrap();
    assert_eq!(d.kind, Kind::Iso);
    check_pc(&d, &t.0.join("out-iso"), &want, &movie);
    // only the cabinet's second folder
    let (n, _) = d.extract(&t.0.join("only"), &["c.bin".into()], &mut |_| {}).unwrap();
    assert_eq!(n, 1);
    assert_eq!(tree(&t.0.join("only")).keys().collect::<Vec<_>>(), ["c.bin"]);
    // .cue naming a raw .bin
    t.write("disc/HWVX (Track 1).bin", &iso::tests::to_raw(&iso));
    let cue = t.write("disc/HWVX.cue", b"FILE \"HWVX (Track 1).bin\" BINARY\r\n  TRACK 01 MODE1/2352\r\n    INDEX 01 00:00:00\r\n");
    let d = Disc::open(&cue).unwrap();
    assert!(d.raw_sectors);
    check_pc(&d, &t.0.join("out-cue"), &want, &movie);
    // the CD as a folder
    t.write("cd/HWVX.cab", &cab);
    t.write("cd/FMV_/HWBOOT.BIK", &movie);
    t.write("cd/AUTORUN.INF", b"x");
    let d = Disc::open(&t.0.join("cd")).unwrap();
    assert_eq!(d.kind, Kind::CdFolder);
    check_pc(&d, &t.0.join("out-cd"), &want, &movie);
}

#[test]
fn folders() {
    let t = Tmp::new("folders");
    t.write("rip/files/HW_share.xgc", b"s");
    t.write("rip/files/worlds_/w.xgc", b"w");
    t.write("rip/sys/main.dol", b"d");
    let d = Disc::open(&t.0.join("rip")).unwrap();
    assert_eq!((d.kind, d.release), (Kind::Extracted, Release::Gc));
    assert_eq!(d.entries.iter().map(|e| e.path.as_str()).collect::<Vec<_>>(), ["HW_share.xgc", "worlds_/w.xgc"]);
    t.write("pc/HW_share.xpc", &share_xps(183));
    t.write("pc/Hot_Wheels.exe", b"exe");
    let d = Disc::open(&t.0.join("pc")).unwrap();
    assert_eq!((d.release, d.entries.len()), (Release::Pc, 2));
    // a PS2 disc's ENGLISH folder on its own
    t.write("eng/HW_SHARE.XPS", &share_xps(183));
    let d = Disc::open(&t.0.join("eng")).unwrap();
    assert_eq!(d.release, Release::Ps2);
    assert_eq!(d.entries[0].path, "XPS/ENGLISH/HW_SHARE.XPS");
    // a nested folder: find_archive searches down
    t.write("deep/x/y/HW_IPack.xgc", b"ip");
    assert_eq!(find_archive(&t.0.join("deep"), "HW_IPack").unwrap().1, b"ip");
}

#[test]
fn globs() {
    assert!(glob("*.xgc", "HW_share.XGC"));
    assert!(glob("hw_?ars.*", "HW_Cars.xgc"));
    assert!(!glob("*.xgc", "a.xgcz"));
    assert!(glob("worlds_/*", "worlds_/a/b.xgc"));
    assert!(glob("*", ""));
    assert!(!glob("a*b", "acd"));
}

#[test]
fn cue_paths() {
    let t = Tmp::new("cue");
    let c = t.write("a.cue", b"REM x\nfile track.bin binary\n");
    assert_eq!(cue_track(&c).unwrap(), t.0.join("track.bin"));
    let c = t.write("b.cue", b"FILE \"/abs/x y.bin\" BINARY\n");
    assert_eq!(cue_track(&c).unwrap(), PathBuf::from("/abs/x y.bin"));
}

/// The retail PC .cue names IMS-HWVX.BIN; the file beside it is ims-hwvx.bin (found without case,
/// as Windows and macOS would); a name with no match at all keeps the .cue's spelling.
#[test]
fn cue_track_nocase() {
    let t = Tmp::new("cue-case");
    t.write("ims-hwvx.bin", b"x");
    let c = t.write("ims-hwvx.cue", b"FILE \"IMS-HWVX.BIN\" BINARY\r\n  TRACK 01 MODE1/2352\r\n");
    assert_eq!(cue_track(&c).unwrap(), t.0.join("ims-hwvx.bin"));
    t.write("Sub/Track.Bin", b"x");
    let c = t.write("sub.cue", b"FILE \"SUB\\TRACK.BIN\" BINARY\n");
    assert_eq!(cue_track(&c).unwrap(), t.0.join("Sub/Track.Bin"));
    let c = t.write("none.cue", b"FILE \"MISSING.BIN\" BINARY\n");
    assert_eq!(cue_track(&c).unwrap(), t.0.join("MISSING.BIN"));
}

/// extract_until stops after the file its callback refuses, image files and cabinet files alike.
#[test]
fn extract_cancelled() {
    let t = Tmp::new("cancel");
    let d = Disc::open(&t.write("gc.iso", &gc_image())).unwrap();
    let mut n = 0;
    let r = d.extract_until(&t.0.join("gc"), &[], &mut |_| {
        n += 1;
        n < 2
    });
    assert_eq!(r.unwrap_err(), CANCELLED);
    assert_eq!(tree(&t.0.join("gc")).len(), 2);
    let (cab, _) = cab::tests::sample();
    let d = Disc::open(&t.write("pc.iso", &pc_tree(&cab, b"movie"))).unwrap();
    let r = d.extract_until(&t.0.join("pc"), &[], &mut |e| e.path != "a.xpc");
    assert_eq!(r.unwrap_err(), CANCELLED);
    // the movie, the empty file, a.xpc; not b.xpc (in a.xpc's folder) nor c.bin
    assert_eq!(tree(&t.0.join("pc")).keys().collect::<Vec<_>>(), ["FMV_/hwboot.bik", "a.xpc", "e"]);
}
