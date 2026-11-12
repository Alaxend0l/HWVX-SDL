//! xdlrs: the .xdl tools' core in Rust, for the editor (tools/browser) to call as a library.
//!
//! A port of tools/xdl_struct.py and tools/xdl_tac.py that gives the same bytes and the same
//! JSON text for the same input (tools/xdlrs_parity.py checks it against the Python):
//!
//! * [`schema`]: the generated schemas (schemas/*.schema.json): layouts, C types, sizes.
//! * [`pack`]: one pack of an `hwvx-x extract` tree, the pairing table, base:// links.
//! * [`decode`]: pack bytes -> JSON ([`decode::Decoder`]), object links -> offsets
//!   ([`decode::Resolver`]).
//! * [`encode`]: JSON -> bytes and relocations over a base ([`encode::Encoder`];
//!   [`encode::LinkMode::Symbolic`] is xdl_graph's GraphEncoder).
//! * [`ops`]: extract, build, the round trip of one object.
//! * [`validate`]: xdl_struct.MiniValidator.
//! * [`tac`]: TAC triggers: parameter lists, records, a trigger file's parts.
//! * [`jsonc`] / [`jsonc_edit`]: reading JSONC (the Python's rules, and the browser's lenient
//!   reader), writing it as the tools do, and saving edits without losing comments.
//! * [`py`]: Python's number text, json.dumps, repr and == rules the outputs depend on.
//!
//! Errors carry the Python exception they correspond to ([`error::Kind`]).

pub mod decode;
pub mod encode;
pub mod error;
pub mod jsonc;
pub mod jsonc_edit;
pub mod modcheck;
pub mod ops;
pub mod pack;
pub mod py;
pub mod schema;
pub mod tac;
pub mod validate;

pub use error::{Error, Kind, Result};

use std::path::{Path, PathBuf};

/// The HWVX-SDL checkout that holds schemas/ and tools/: the first folder holding
/// schemas/Trigger.schema.json above `from`, else above this crate's source.
pub fn repo_root(from: Option<&Path>) -> Option<PathBuf> {
    let probe = "schemas/Trigger.schema.json";
    if let Some(f) = from
        && let Some(p) = pack::find_up(f, probe)
    {
        return p.parent().and_then(Path::parent).map(Path::to_path_buf);
    }
    let here = Path::new(env!("CARGO_MANIFEST_DIR"));
    pack::find_up(here, probe).and_then(|p| p.parent().and_then(Path::parent).map(Path::to_path_buf))
}

/// docs/xdl-format/pairing above the repo root (the superproject's docs), as find_up finds it.
pub fn pairing_dir(root: &Path) -> Option<PathBuf> {
    pack::find_up(&root.join("tools"), "docs/xdl-format/pairing")
}
