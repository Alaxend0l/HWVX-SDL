//! The geometry scan and decode (xdlrs::geom) of packs, as JSON lines, for
//! build/exp/geom_parity.py: `geom_probe PACK_DIR...` (each one pack of an hwvx-x extract tree).
//! Per pack: {"pack": dir, "geoms": [{"off", "ok", "owned", "names"} | {"off", "ok": false, "kind", "err"}]}

use serde_json::{Value, json};
use std::collections::HashSet;
use std::path::Path;
use xdlrs::geom;

fn main() {
    for dir in std::env::args().skip(1) {
        let d = Path::new(&dir);
        let man: Value = serde_json::from_str(&std::fs::read_to_string(d.parent().unwrap().join("manifest.json")).unwrap()).unwrap();
        let rel = man["platform"].as_str().unwrap().to_string();
        let big = rel == "gc";
        let data = std::fs::read(d.join("data.bin")).unwrap_or_default();
        let raw = std::fs::read(d.join("pointers.bin")).unwrap_or_default();
        let ptrs: HashSet<u32> = raw
            .chunks_exact(4)
            .map(|c| {
                let a = [c[0], c[1], c[2], c[3]];
                if big { u32::from_be_bytes(a) } else { u32::from_le_bytes(a) }
            })
            .collect();
        let b = geom::Blob::new(&data, &ptrs, &rel);
        let mut out = vec![];
        for off in geom::scan(&b) {
            out.push(match geom::decode(&b, off) {
                Ok(g) => json!({"off": off, "ok": true, "owned": g.owned, "names": g.names}),
                Err(e) => json!({"off": off, "ok": false, "kind": if geom::caught(&e) {"caught"} else {"uncaught"}, "err": e.msg}),
            });
        }
        println!("{}", json!({"pack": dir, "geoms": out}));
    }
}
