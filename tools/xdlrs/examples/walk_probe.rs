//! walk_probe [PACK_DIR BIG(0|1) PTYPE]...: xdlrs::walk::label_pack of each pack, one JSON line
//! each ({"objs": {offset: label}} or {"error": msg}); with no arguments, reads lines
//! "PACK_DIR\tBIG\tPTYPE" from stdin (the parity script's batch mode).
use std::io::BufRead;
use std::path::Path;

fn one(dir: &str, big: &str, ptype: &str) {
    let r = xdlrs::walk::label_pack(Path::new(dir), big == "1", ptype);
    let v = match r {
        Ok(m) => {
            let o: serde_json::Map<String, serde_json::Value> =
                m.into_iter().map(|(k, v)| (k.to_string(), serde_json::Value::String(v))).collect();
            serde_json::json!({"objs": o})
        }
        Err(e) => serde_json::json!({"error": e.msg}),
    };
    println!("{v}");
}

fn main() {
    let a: Vec<String> = std::env::args().skip(1).collect();
    if a.is_empty() {
        for line in std::io::stdin().lock().lines().map_while(Result::ok) {
            let f: Vec<&str> = line.split('\t').collect();
            if f.len() == 3 {
                one(f[0], f[1], f[2]);
            }
        }
    } else {
        for c in a.chunks(3) {
            one(&c[0], &c[1], &c[2]);
        }
    }
}
