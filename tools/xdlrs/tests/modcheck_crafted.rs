//! The mod check on crafted inputs: tiny mods and indexes made here (nothing of the game).

use std::path::{Path, PathBuf};
use xdlrs::modcheck::{self, Options, zip};

fn scratch(name: &str) -> PathBuf {
    let d = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../build/xdlrs-test").join(name);
    let _ = std::fs::remove_dir_all(&d);
    std::fs::create_dir_all(&d).unwrap();
    d
}

/// an index of no files: meta only, plus the arrays given
fn index(dir: &Path, sources: &[&str], arrays: &[(&str, Vec<u8>)]) -> PathBuf {
    let meta = serde_json::json!({"tool": modcheck::TOOL, "tool_version": modcheck::TOOL_VERSION, "sources": sources, "files": []});
    let m = meta.to_string();
    let mut a: Vec<(String, Vec<u8>)> = vec![("meta.npy".into(), zip::npy_bytes("|u1", &[m.len()], m.as_bytes()))];
    a.extend(arrays.iter().map(|(n, b)| (format!("{n}.npy"), b.clone())));
    let refs: Vec<(String, &[u8])> = a.iter().map(|(n, b)| (n.clone(), b.as_slice())).collect();
    let p = dir.join("index.npz");
    std::fs::write(&p, zip::write_stored(&refs).unwrap()).unwrap();
    p
}

fn a_mod(dir: &Path) -> PathBuf {
    let m = dir.join("mod");
    std::fs::create_dir_all(&m).unwrap();
    std::fs::write(m.join("mod.jsonc"), r#"{"format_version": 1, "id": "uid://crafted01", "name": "Crafted", "version": "1.0.0"}"#).unwrap();
    std::fs::write(m.join("notes.txt"), "hello").unwrap();
    m
}

fn opt() -> Options {
    Options { strip_unchanged: false, allow_partial_index: true, root: Some(Path::new(env!("CARGO_MANIFEST_DIR")).join("../..")) }
}

#[test]
fn names_the_installer_refuses_are_refused() {
    let d = scratch("names");
    let ix = index(&d, &[], &[]);
    let m = a_mod(&d);
    let ok = modcheck::check_and_pack(&m, &ix, None, &opt());
    assert_eq!(ok.code, 0, "{}{}", ok.stdout, ok.stderr);
    std::fs::write(m.join("a\\b.txt"), "x").unwrap();
    std::fs::write(m.join("c:d.txt"), "x").unwrap();
    let r = modcheck::check_and_pack(&m, &ix, None, &opt());
    assert_eq!(r.code, 1, "{}{}", r.stdout, r.stderr);
    assert!(r.stdout.contains("a\\b.txt") && r.stdout.contains("c:d.txt") && r.stdout.contains("installer refuses"), "{}", r.stdout);
}

fn u32_npy(v: &[u32]) -> Vec<u8> {
    zip::npy_bytes("<u4", &[v.len()], &v.iter().flat_map(|x| x.to_le_bytes()).collect::<Vec<u8>>())
}

fn u64_npy(v: &[u64]) -> Vec<u8> {
    zip::npy_bytes("<u8", &[v.len()], &v.iter().flat_map(|x| x.to_le_bytes()).collect::<Vec<u8>>())
}

#[test]
fn index_values_out_of_range_are_errors_not_panics() {
    let d = scratch("index");
    let load = |p: &Path| modcheck::index::Index::load(p).map(|_| ()).map_err(|e| e.0);
    // a source number past the sources, and a table longer than its sources
    let p = index(&d, &["base://a"], &[("b32", u64_npy(&[1, 2])), ("b32_src", u32_npy(&[0, 7]))]);
    assert!(load(&p).unwrap_err().contains("b32_src"));
    let p = index(&d, &["base://a"], &[("b32", u64_npy(&[1, 2])), ("b32_src", u32_npy(&[0]))]);
    assert!(load(&p).unwrap_err().contains("b32_src"));
    let p = index(&d, &["base://a"], &[("sfp", zip::npy_bytes("<u2", &[1], &[1, 0])), ("sfp_frame", u32_npy(&[0])), ("sfp_src", u32_npy(&[3]))]);
    assert!(load(&p).unwrap_err().contains("sfp"));
    let p = index(&d, &["base://a"], &[("dh", zip::npy_bytes("|u1", &[32], &[0; 32])), ("dh_src", u32_npy(&[]))]);
    assert!(load(&p).unwrap_err().contains("dh_src"));
    let p = index(&d, &["base://a"], &[("b32", u64_npy(&[1, 2])), ("b32_src", u32_npy(&[0, 0]))]);
    load(&p).unwrap();

    // zip64 positions near 2^64: errors, not wrapped sums
    let ok = zip::write_stored(&[("a_long_entry_name.txt".into(), &b"x"[..])]).unwrap();
    assert_eq!(zip::read_stored(&ok).unwrap().len(), 1);
    let eocd = ok.len() - 22;
    let mut z = ok.clone();
    z[eocd + 16..eocd + 20].copy_from_slice(&0xFFFF_FFFFu32.to_le_bytes());
    let mut loc = b"PK\x06\x07".to_vec();
    loc.extend_from_slice(&0u32.to_le_bytes());
    loc.extend_from_slice(&(u64::MAX - 2).to_le_bytes());
    loc.extend_from_slice(&1u32.to_le_bytes());
    z.splice(eocd..eocd, loc);
    assert!(zip::read_stored(&z).is_err());
    // a central entry whose offset / size are zip64 values near 2^64
    let cd = u32::from_le_bytes(ok[eocd + 16..eocd + 20].try_into().unwrap()) as usize;
    for (field, at) in [("offset", 42usize), ("size", 24)] {
        let mut z = ok.clone();
        z[cd + at..cd + at + 4].copy_from_slice(&0xFFFF_FFFFu32.to_le_bytes());
        // the zip64 extra: put it in place of the name's tail (xl = 12)
        let nl = u16::from_le_bytes([z[cd + 28], z[cd + 29]]) as usize;
        assert!(nl >= 12, "{field}");
        z[cd + 28..cd + 30].copy_from_slice(&((nl - 12) as u16).to_le_bytes());
        z[cd + 30..cd + 32].copy_from_slice(&12u16.to_le_bytes());
        let x = cd + 46 + nl - 12;
        z[x..x + 4].copy_from_slice(&[1, 0, 8, 0]);
        z[x + 4..x + 12].copy_from_slice(&(u64::MAX - 8).to_le_bytes());
        assert!(zip::read_stored(&z).is_err(), "{field}");
    }
}

/// A PNG made here: a gradient with a square (no game data in it).
fn gradient_png(w: u32, h: u32) -> Vec<u8> {
    let mut px = vec![];
    for y in 0..h {
        for x in 0..w {
            let inside = x > w / 4 && x < w * 3 / 4 && y > h / 4 && y < h * 3 / 4;
            px.extend_from_slice(&if inside { [255, 255, 0, 255] } else { [(x * 255 / w) as u8, 40, (y * 255 / h) as u8, 255] });
        }
    }
    let mut out = vec![];
    let mut e = png::Encoder::new(&mut out, w, h);
    e.set_color(png::ColorType::Rgba);
    e.set_depth(png::BitDepth::Eight);
    e.write_header().unwrap().write_image_data(&px).unwrap();
    out
}

#[test]
fn a_track_picture_of_the_mods_own() {
    let d = scratch("thumb");
    let ix = index(&d, &[], &[]);
    let m = d.join("mod");
    std::fs::create_dir_all(m.join("images")).unwrap();
    let write = |thumb: &str| {
        std::fs::write(
            m.join("mod.jsonc"),
            format!(r#"{{"format_version": 1, "id": "uid://crafted02", "name": "Crafted", "version": "1.0.0",
                "tracks": {{"uid://trk1": {{"extends": "base://mcp/track/39", "name": "Mine", "unlocked": true, "thumbnail": "{thumb}"}}}}}}"#),
        )
        .unwrap()
    };
    std::fs::write(m.join("images/mine.png"), gradient_png(320, 192)).unwrap();
    write("images/mine.png");
    let r = modcheck::check_and_pack(&m, &ix, Some(&d.join("m.xmod")), &opt());
    assert_eq!(r.code, 0, "{}{}", r.stdout, r.stderr);
    let names: Vec<String> = zip::read_stored(&std::fs::read(d.join("m.xmod")).unwrap()).unwrap().into_iter().map(|e| e.0).collect();
    assert!(names.iter().any(|n| n == "images/mine.png"), "{names:?}");
    // too big, missing, outside the mod, not a PNG
    std::fs::write(m.join("images/big.png"), gradient_png(1100, 8)).unwrap();
    std::fs::write(m.join("images/fake.png"), b"not a png at all, not at all").unwrap();
    for (thumb, says) in [("images/big.png", "at most 1024"), ("images/none.png", "no such file"), ("../x.png", "isn't the form"), ("images/fake.png", "not a PNG")] {
        write(thumb);
        let r = modcheck::check_and_pack(&m, &ix, None, &opt());
        assert_eq!(r.code, 1, "{thumb}: {}{}", r.stdout, r.stderr);
        assert!(r.stdout.contains(says) || r.stderr.contains(says), "{thumb}: {}{}", r.stdout, r.stderr);
    }
}
