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
