//! The generated schemas (schemas/*.schema.json): struct layouts by title and by `$defs` name,
//! C types and their sizes (xdl_struct.Schemas, parse_ctype, fields, leaf, PRIM).

use crate::error::{self, Error, Result};
use crate::py;
use serde_json::Value;
use std::collections::HashMap;
use std::path::{Path, PathBuf};

/// A primitive C type: struct code, size, float.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct Prim {
    pub code: char,
    pub size: usize,
    pub float: bool,
}

/// xdl_struct.PRIM
pub fn prim(name: &str) -> Option<Prim> {
    let (code, size, float) = match name {
        "u8" => ('B', 1, false),
        "s8" | "char" => ('b', 1, false),
        "u16" => ('H', 2, false),
        "s16" => ('h', 2, false),
        "u32" => ('I', 4, false),
        "s32" | "BOOL" | "int" => ('i', 4, false),
        "f32" => ('f', 4, true),
        "f64" => ('d', 8, true),
        _ => return None,
    };
    Some(Prim { code, size, float })
}

/// 'f32[3]' -> ('f32', [3]); 'PTR32(PTR32(char))' -> ('PTR32(PTR32(char))', []).
pub fn parse_ctype(ct: &str) -> (String, Vec<usize>) {
    // the trailing run of [N] groups
    let b = ct.as_bytes();
    let mut end = b.len();
    let mut dims_start = end;
    loop {
        if end == 0 || b[end - 1] != b']' {
            break;
        }
        let Some(open) = ct[..end].rfind('[') else { break };
        let inner = &ct[open + 1..end - 1];
        if inner.is_empty() || !inner.bytes().all(|c| c.is_ascii_digit()) {
            break;
        }
        end = open;
        dims_start = open;
    }
    let dims = ct[dims_start..]
        .split('[')
        .skip(1)
        .map(|d| d.trim_end_matches(']').parse().unwrap())
        .collect();
    (ct[..dims_start].to_string(), dims)
}

/// The C type of one element of an array type (dims[1:]), or the type itself.
pub fn sub_ctype(base: &str, dims: &[usize]) -> String {
    let mut s = base.to_string();
    for d in dims.iter().skip(1) {
        s.push_str(&format!("[{d}]"));
    }
    s
}

/// [(json key, property schema)] of a struct schema's own fields (not $schema/extends).
pub fn fields(sch: &Value) -> Vec<(&String, &Value)> {
    match sch.get("properties").and_then(Value::as_object) {
        Some(m) => m.iter().filter(|(_, p)| p.get("x-offset").is_some()).collect(),
        None => vec![],
    }
}

/// The schema of an array's innermost item (`depth` levels of "items").
pub fn leaf(p: &Value, depth: usize) -> &Value {
    let mut p = p;
    for _ in 0..depth {
        p = &p["items"];
    }
    p
}

/// A property's x-offset.
pub fn offset(p: &Value) -> Result<u32> {
    Ok(py::hex(p["x-offset"].as_str().unwrap_or(""))? as u32)
}

/// A property's x-c-type.
pub fn ctype(p: &Value) -> &str {
    p["x-c-type"].as_str().unwrap_or("")
}

/// A struct schema's x-size.
pub fn size(sch: &Value) -> Result<usize> {
    Ok(py::hex(sch["x-size"].as_str().unwrap_or(""))? as usize)
}

/// The last segment of a "$ref" ("#/$defs/Vector3" -> "Vector3").
pub fn ref_name(p: &Value) -> Option<&str> {
    p.get("$ref").and_then(Value::as_str).map(|r| r.rsplit('/').next().unwrap())
}

pub struct Schemas {
    /// title -> (schema, file), in file name order
    pub top: Vec<(String, Value, PathBuf)>,
    index: HashMap<String, usize>,
    pub defs: HashMap<String, Value>,
}

impl Schemas {
    /// Every *.schema.json of `folder`. Titles and $defs as the Python: a later file's title
    /// replaces an earlier one; the first $defs of a name wins.
    pub fn load(folder: &Path) -> Result<Schemas> {
        let mut paths: Vec<PathBuf> = std::fs::read_dir(folder)
            .map_err(|e| Error::io(format!("{}: {e}", folder.display())))?
            .filter_map(|e| e.ok().map(|e| e.path()))
            .filter(|p| p.file_name().and_then(|n| n.to_str()).is_some_and(|n| n.ends_with(".schema.json")))
            .collect();
        paths.sort();
        let mut s = Schemas { top: vec![], index: HashMap::new(), defs: HashMap::new() };
        for p in paths {
            let v = crate::jsonc::parse(&error::read_text(&p)?).map_err(|e| Error::json(format!("{}: {e}", p.display())))?;
            let title = py::str_of(&v["title"]);
            if let Some(d) = v.get("$defs").and_then(Value::as_object) {
                for (k, x) in d {
                    s.defs.entry(k.clone()).or_insert_with(|| x.clone());
                }
            }
            match s.index.get(&title) {
                Some(&i) => s.top[i] = (title, v, p),
                None => {
                    s.index.insert(title.clone(), s.top.len());
                    s.top.push((title, v, p));
                }
            }
        }
        Ok(s)
    }

    /// A top-level schema by title.
    pub fn top(&self, name: &str) -> Option<&Value> {
        self.index.get(name).map(|&i| &self.top[i].1)
    }

    /// A struct's schema: a top-level title, else a $defs name.
    pub fn struct_(&self, name: &str) -> Option<&Value> {
        self.top(name).or_else(|| self.defs.get(name))
    }

    /// The file of a top-level schema.
    pub fn path(&self, name: &str) -> Option<&Path> {
        self.index.get(name).map(|&i| self.top[i].2.as_path())
    }

    /// The struct a "$ref" property points to.
    pub fn ref_(&self, node: &Value) -> Option<&Value> {
        ref_name(node).and_then(|n| self.struct_(n))
    }

    /// Size in bytes of a C type (arrays included).
    pub fn size_of(&self, ctype: &str) -> Result<usize> {
        let (base, dims) = parse_ctype(ctype);
        let mut n = if base.starts_with("PTR32(") || base == "FPTR32" {
            4
        } else if let Some(p) = prim(&base) {
            p.size
        } else {
            match self.struct_(&base) {
                Some(s) => size(s)?,
                None => return Err(Error::xdl(format!("size of {base} unknown (no schema)"))),
            }
        };
        for d in dims {
            n *= d;
        }
        Ok(n)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn ctypes() {
        assert_eq!(parse_ctype("f32[3]"), ("f32".into(), vec![3]));
        assert_eq!(parse_ctype("u8[2][4]"), ("u8".into(), vec![2, 4]));
        assert_eq!(parse_ctype("PTR32(PTR32(char))"), ("PTR32(PTR32(char))".into(), vec![]));
        assert_eq!(parse_ctype("PTR32(char[4])"), ("PTR32(char[4])".into(), vec![]));
        assert_eq!(sub_ctype("u8", &[2, 4]), "u8[4]");
    }
}
