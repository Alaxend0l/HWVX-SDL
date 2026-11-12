//! hwvx-decomp: the decomp workflow tools, one subcommand each (formerly tools/*.py).
//!
//!   hwvx-decomp [--root DIR] COMMAND ARGS...
//!
//! DIR is the HWVX-SDL checkout (src/game, src/port, HWVX-GC-Decomp); by default the
//! nearest directory above the current one that has src/game, else the checkout this
//! binary was built from.

mod util;
mod import_decomp;
mod strip_sdk_decls;
mod gen_protos;
mod slotify;
mod fix_casts;
mod fieldify;
mod view_externs;
mod check_ptr_offsets;
mod offsets_to_fields;
mod asm_args;
mod mipsdis;
mod gen_data;

use std::path::PathBuf;

const COMMANDS: &[(&str, &str)] = &[
    ("import", "import game sources from HWVX-GC-Decomp into src/game (--force F.., --diff, --merge)"),
    ("strip-sdk-decls", "drop local SDK/libc prototypes and SDK typedefs: FILES..."),
    ("gen-protos", "generate src/game/include/game_protos.h [--strip]"),
    ("slotify", "raw pointer-slot accesses -> SLOT_LOAD / SLOT_STORE: FILES..."),
    ("fix-casts", "PTR()/HND() at GCC's pointer<->integer errors: BUILD_DIR (SARIF)"),
    ("fieldify", "pointer fields -> PTR32 handles: [--only A,B] FILES... | --revert BUILD_LOG"),
    ("view-externs", "conflicting block-scope externs -> per-function views: BUILD_LOG"),
    ("check-ptr-offsets", "constant offsets on typed pointers (clang): [--bytes] [BUILD_DIR]"),
    ("offsets-to-fields", "byte offsets -> fields: LAYOUTS FILE FUNC [VAR=Type..] [--rename A=B..]"),
    ("asm-args", "argument registers a function reads (GC asm): NAME..."),
    ("gen-data", "src/game/data_gen.c from the decomp asm (no args: the port's set) [--raw/--hostptr/--bytes/--u16 N].. NAME.. | @FILE"),
    ("mipsdis", "disassemble the PS2 prototype's ELF: ELF FUNC.. | ELF --find S | ELF --addr HEX [N]"),
];

fn default_root() -> PathBuf {
    if let Ok(mut d) = std::env::current_dir() {
        loop {
            if d.join("src/game").is_dir() && d.join("src/port").is_dir() {
                return d;
            }
            if !d.pop() {
                break;
            }
        }
    }
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn usage() -> ! {
    eprintln!("usage: hwvx-decomp [--root DIR] COMMAND ARGS...\n");
    for (c, h) in COMMANDS {
        eprintln!("  {c:18} {h}");
    }
    std::process::exit(2);
}

fn main() {
    let mut args: Vec<String> = std::env::args().skip(1).collect();
    let mut root = None;
    if args.first().map(|s| s.as_str()) == Some("--root") && args.len() > 1 {
        root = Some(PathBuf::from(args[1].clone()));
        args.drain(..2);
    }
    if args.is_empty() || args[0] == "-h" || args[0] == "--help" {
        usage();
    }
    let root = root.unwrap_or_else(default_root);
    let root = root.canonicalize().unwrap_or(root);
    let cmd = args.remove(0);
    let code = match cmd.as_str() {
        "import" => import_decomp::run(&root, args),
        "strip-sdk-decls" => strip_sdk_decls::run(&root, args),
        "gen-protos" => gen_protos::run(&root, args),
        "slotify" => slotify::run(&root, args),
        "fix-casts" => fix_casts::run(&root, args),
        "fieldify" => fieldify::run(&root, args),
        "view-externs" => view_externs::run(&root, args),
        "check-ptr-offsets" => check_ptr_offsets::run(&root, args),
        "offsets-to-fields" => offsets_to_fields::run(&root, args),
        "asm-args" => asm_args::run(&root, args),
        "gen-data" => gen_data::run(&root, args),
        "mipsdis" => mipsdis::run(&root, args),
        _ => usage(),
    };
    std::process::exit(code);
}
