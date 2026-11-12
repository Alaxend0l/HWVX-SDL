//! XDLRS_SRC_HASH: a hash of the crate's sources (src/**/*.rs, Cargo.toml), the level tools'
//! cache keys (a rebuilt tool makes its products again, as the Python's keys hash its files).

use std::path::Path;

fn walk(d: &Path, out: &mut Vec<std::path::PathBuf>) {
    if let Ok(rd) = std::fs::read_dir(d) {
        for e in rd.flatten() {
            let p = e.path();
            if p.is_dir() {
                walk(&p, out);
            } else if p.extension().is_some_and(|x| x == "rs") {
                out.push(p);
            }
        }
    }
}

fn main() {
    let mut files = vec![Path::new("Cargo.toml").to_path_buf()];
    walk(Path::new("src"), &mut files);
    files.sort();
    // FNV-1a 64 twice (two seeds): no dependency for a build script
    let (mut a, mut b): (u64, u64) = (0xcbf29ce484222325, 0x84222325cbf29ce4);
    for f in &files {
        println!("cargo:rerun-if-changed={}", f.display());
        let mut bytes = f.display().to_string().into_bytes();
        bytes.push(0);
        bytes.extend(std::fs::read(f).unwrap_or_default());
        for x in bytes {
            a = (a ^ x as u64).wrapping_mul(0x100000001b3);
            b = (b ^ x as u64).wrapping_mul(0x100000001b3).rotate_left(5);
        }
    }
    println!("cargo:rerun-if-changed=src");
    println!("cargo:rustc-env=XDLRS_SRC_HASH={a:016x}{b:016x}");
}
