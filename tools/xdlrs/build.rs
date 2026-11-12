//! 1. Links the port's own audio decoders (src/audiodec over src/third_party's dr_wav, dr_flac,
//!    dr_mp3 and stb_vorbis) for the mod check: a mod's sound decodes to exactly the samples the
//!    game gets (src/game/Port_Mods.c uses the same code).
//! 2. XDLRS_SRC_HASH: a hash of the crate's sources (src/**/*.rs, Cargo.toml), the level tools'
//!    cache keys (a rebuilt tool makes its products again, as the Python's keys hash its files).

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

fn audiodec() {
    let src = std::path::Path::new(env!("CARGO_MANIFEST_DIR")).join("../../src/audiodec");
    for f in ["audiodec.c", "audiodec.h", "stb_vorbis_impl.c", "../third_party/miniaudio.h", "../third_party/stb_vorbis.c"] {
        println!("cargo:rerun-if-changed={}", src.join(f).display());
    }
    cc::Build::new()
        .file(src.join("audiodec.c"))
        .file(src.join("stb_vorbis_impl.c"))
        .include(&src)
        .warnings(false)
        .flag_if_supported("-w")
        .opt_level(2)
        .compile("hwvx_audiodec");
}

fn src_hash() {
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

fn main() {
    audiodec();
    src_hash();
}
