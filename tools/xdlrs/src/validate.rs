//! xdl_struct.MiniValidator: the keywords the generated schemas use (type, const, pattern,
//! minimum/maximum, minItems/maxItems, items, properties, additionalProperties: false,
//! required, if/then/else on required). anyOf, oneOf, allOf, enum, propertyNames and
//! uniqueItems are not checked (the Python uses the jsonschema package when it is installed,
//! which does; this is its fallback).

use crate::py::{self, Num};
use regex::Regex;
use serde_json::Value;
use std::cell::RefCell;
use std::collections::HashMap;

pub struct MiniValidator<'a> {
    root: &'a Value,
    res: RefCell<HashMap<String, Option<Regex>>>,
}

impl<'a> MiniValidator<'a> {
    pub fn new(sch: &'a Value) -> Self {
        MiniValidator { root: sch, res: RefCell::default() }
    }

    /// The messages, in the Python's order.
    pub fn errors(&self, inst: &Value) -> Vec<String> {
        let mut errs = vec![];
        self.check(self.root, inst, "", &mut errs);
        errs
    }

    fn search(&self, pat: &str, s: &str) -> bool {
        let mut m = self.res.borrow_mut();
        let r = m.entry(pat.to_string()).or_insert_with(|| Regex::new(pat).ok());
        r.as_ref().is_none_or(|r| r.is_match(s))
    }

    fn check(&self, s: &Value, v: &Value, path: &str, errs: &mut Vec<String>) {
        if s == &Value::Bool(true) {
            return;
        }
        let mut s = s;
        if let Some(r) = s.get("$ref").and_then(Value::as_str) {
            let name = r.rsplit('/').next().unwrap();
            s = &self.root["$defs"][name];
        }
        if let Some(t) = s.get("type") {
            let ts: Vec<&str> = match t {
                Value::Array(a) => a.iter().filter_map(Value::as_str).collect(),
                _ => t.as_str().into_iter().collect(),
            };
            if !ts.iter().any(|x| is(v, x)) {
                errs.push(format!("{path}: not {}", py::str_of(t)));
                return;
            }
        }
        if let Some(c) = s.get("const")
            && !py::eq(v, c)
        {
            errs.push(format!("{path}: must be {}", py::str_of(c)));
        }
        if let (Value::String(x), Some(p)) = (v, s.get("pattern").and_then(Value::as_str))
            && !self.search(p, x)
        {
            errs.push(format!("{path}: {} doesn't match the link grammar", py::str_repr(x)));
        }
        if let Some(n) = Num::of(v).filter(|_| !v.is_boolean()) {
            let lo = s.get("minimum").and_then(Num::of).is_some_and(|m| n.py_lt(m));
            let hi = s.get("maximum").and_then(Num::of).is_some_and(|m| m.py_lt(n));
            if lo || hi {
                errs.push(format!("{path}: {} out of range", py::str_of(v)));
            }
        }
        if let Value::Array(a) = v {
            let min = s.get("minItems").and_then(Value::as_u64).unwrap_or(0) as usize;
            let max = s.get("maxItems").and_then(Value::as_u64).unwrap_or(1 << 30) as usize;
            if a.len() < min || a.len() > max {
                errs.push(format!("{path}: {} items", a.len()));
            }
            let items = s.get("items").unwrap_or(&Value::Bool(true));
            for (i, x) in a.iter().enumerate() {
                self.check(items, x, &format!("{path}/{i}"), errs);
            }
        }
        if let Value::Object(m) = v {
            let empty = serde_json::Map::new();
            let props = s.get("properties").and_then(Value::as_object).unwrap_or(&empty);
            for (k, x) in m {
                if let Some(p) = props.get(k) {
                    self.check(p, x, &format!("{path}/{k}"), errs);
                } else if s.get("additionalProperties") == Some(&Value::Bool(false)) {
                    errs.push(format!("{path}: unknown field {k}"));
                }
            }
            let mut req: Vec<String> = req_of(s);
            if let Some(cond) = s.get("if") {
                let ok = req_of(cond).iter().all(|k| m.contains_key(k));
                let branch = if ok { s.get("then") } else { s.get("else") };
                if let Some(b) = branch.filter(|b| b.is_object()) {
                    req.extend(req_of(b));
                }
            }
            for k in req {
                if !m.contains_key(&k) {
                    errs.push(format!("{path}: missing {k}"));
                }
            }
        }
    }
}

fn req_of(s: &Value) -> Vec<String> {
    s.get("required").and_then(Value::as_array).map(|a| a.iter().map(py::str_of).collect()).unwrap_or_default()
}

fn is(v: &Value, t: &str) -> bool {
    match t {
        "string" => v.is_string(),
        "null" => v.is_null(),
        "integer" => v.is_i64() || v.is_u64(),
        "number" => v.is_number(),
        "array" => v.is_array(),
        "object" => v.is_object(),
        "boolean" => v.is_boolean(),
        _ => false,
    }
}
