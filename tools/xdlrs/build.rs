//! Links the port's own audio decoders (src/audiodec over src/third_party's dr_wav, dr_flac,
//! dr_mp3 and stb_vorbis) for the mod check: a mod's sound decodes to exactly the samples the
//! game gets (src/game/Port_Mods.c uses the same code).

fn main() {
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
