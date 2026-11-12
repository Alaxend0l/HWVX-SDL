//! xdl_mod.Schemas: the pack structs' fields (schemas/*.schema.json with x-c-struct), what a
//! patch may set.

use crate::schema::prim;
use regex::Regex;
use serde_json::Value;
use std::collections::HashMap;
use std::path::Path;
use std::sync::OnceLock;

#[derive(Clone, Debug)]
pub struct Field {
    pub off: usize,
    /// None: a count, a link or a nested struct (not patchable)
    pub prim: Option<String>,
    pub count: usize,
    pub target: Option<String>,
}

pub struct Structs {
    pub s: HashMap<String, Value>,
}

fn re(p: &str) -> Regex {
    Regex::new(p).unwrap()
}

impl Structs {
    pub fn load(root: &Path) -> Result<Structs, String> {
        let mut s = HashMap::new();
        let mut paths: Vec<_> = std::fs::read_dir(root.join("schemas"))
            .map_err(|e| format!("{}: {e}", root.join("schemas").display()))?
            .flatten()
            .map(|e| e.path())
            .filter(|p| p.to_string_lossy().ends_with(".schema.json"))
            .collect();
        paths.sort();
        for p in paths {
            let d: Value = serde_json::from_str(&std::fs::read_to_string(&p).map_err(|e| e.to_string())?)
                .map_err(|e| format!("{}: {e}", p.display()))?;
            if d.get("x-c-struct").is_some()
                && let Some(t) = d.get("title").and_then(Value::as_str)
            {
                s.insert(t.to_string(), d);
            }
        }
        Ok(Structs { s })
    }

    pub fn size(&self, name: &str) -> Option<usize> {
        let x = self.s.get(name)?.get("x-size")?.as_str()?;
        usize::from_str_radix(x.trim_start_matches("0x").trim_start_matches("0X"), 16).ok()
    }

    fn props(&self, name: &str) -> Vec<(&String, &Value)> {
        self.s.get(name).and_then(|d| d.get("properties")).and_then(Value::as_object).map(|o| o.iter().collect()).unwrap_or_default()
    }

    fn link(p: &Value) -> Option<&Value> {
        p.get("x-link").filter(|v| !is_falsy(v)).or_else(|| p.get("items").and_then(|i| i.get("x-link")).filter(|v| !is_falsy(v)))
    }

    /// {count field: [the link fields it counts]}, in the Python's order
    pub fn counts(&self, name: &str) -> Vec<(String, Vec<String>)> {
        static NUM: OnceLock<Regex> = OnceLock::new();
        static BR: OnceLock<Regex> = OnceLock::new();
        let num = NUM.get_or_init(|| re(r"^(num|count)[A-Z0-9_]"));
        let br = BR.get_or_init(|| re(r"\[([A-Za-z_]\w*)\]"));
        let props = self.props(name);
        let has = |k: &str| props.iter().any(|(n, _)| n.as_str() == k);
        let mut out: Vec<(String, Vec<String>)> =
            props.iter().filter(|(k, _)| num.is_match(k)).map(|(k, _)| (k.to_string(), vec![])).collect();
        for (k, p) in &props {
            if Self::link(p).is_none() {
                continue;
            }
            let desc = p.get("description").and_then(Value::as_str).unwrap_or("");
            let c = match br.captures(desc) {
                Some(m) if has(&m[1]) => m[1].to_string(),
                _ => {
                    let mut ch = k.chars();
                    let first = ch.next().map(|c| c.to_uppercase().to_string()).unwrap_or_default();
                    format!("num{first}{}", ch.as_str())
                }
            };
            if has(&c) {
                match out.iter_mut().find(|(n, _)| *n == c) {
                    Some(e) => e.1.push(k.to_string()),
                    None => out.push((c, vec![k.to_string()])),
                }
            }
        }
        out
    }

    /// field -> (offset, prim, count, link target), in the schema's order
    pub fn fields(&self, name: &str) -> Vec<(String, Field)> {
        static CT: OnceLock<Regex> = OnceLock::new();
        let ctre = CT.get_or_init(|| re(r"^(\w+)(?:\[(\d+)\])?$"));
        let counts = self.counts(name);
        let mut out = vec![];
        for (k, p) in self.props(name) {
            let Some(off) = p.get("x-offset").and_then(Value::as_str) else { continue };
            let off = usize::from_str_radix(off.trim_start_matches("0x").trim_start_matches("0X"), 16).unwrap_or(0);
            let ct = p.get("x-c-type").and_then(Value::as_str).unwrap_or("");
            let none = Field { off, prim: None, count: 0, target: None };
            let f = if counts.iter().any(|(c, _)| c == k) {
                none
            } else if let Some(link) = Self::link(p) {
                let t = link.get("target").and_then(Value::as_str);
                if link.get("kind").and_then(Value::as_str) == Some("object")
                    && t.is_some_and(|t| self.s.contains_key(t))
                    && !ct.contains('[')
                {
                    Field { target: t.map(String::from), ..none }
                } else {
                    none
                }
            } else if let Some(m) = ctre.captures(ct).filter(|m| prim(&m[1]).is_some()) {
                Field { off, prim: Some(m[1].to_string()), count: m.get(2).map(|c| c.as_str().parse().unwrap_or(0)).unwrap_or(0), target: None }
            } else {
                none
            };
            out.push((k.to_string(), f));
        }
        out
    }
}

/// Python's truthiness of a JSON value
pub fn is_falsy(v: &Value) -> bool {
    match v {
        Value::Null => true,
        Value::Bool(b) => !b,
        Value::Number(n) => n.as_f64() == Some(0.0),
        Value::String(s) => s.is_empty(),
        Value::Array(a) => a.is_empty(),
        Value::Object(o) => o.is_empty(),
    }
}
