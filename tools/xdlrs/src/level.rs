//! Levels of one's own over a base world (tools/xdl_level.py): a level project compiled into
//! the level file the port reads (src/game/Port_Level.c), its checks, the editor's test drive
//! and the mod folder a level is shared in. [`crate::level_pack`] builds a level into a world
//! pack of its own (tools/xdl_level_pack.py).
//!
//! Same input, same output as the Python: the level file's bytes (but `sourceHash`, the cache
//! key, which hashes this crate's sources where the Python hashes its own), the errors and
//! warnings (where the Python's text comes from a Python exception, the text is close, not
//! the same), the mod folder, the test drive's JSON.

use crate::error::{Error, Kind, Result};
use crate::graph::{self, Ctx};
use crate::py::{self, Num};
use crate::schema;
use crate::tac::{self, Tac};
use crate::{bsp, jsonc};
use indexmap::IndexMap;
use regex::Regex;
use serde_json::{Map, Value, json};
use sha2::{Digest, Sha256};
use std::cell::RefCell;
use std::collections::{BTreeMap, HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::sync::OnceLock;
use std::time::Instant;

pub const FORMAT: &str = "hwvx-level";

/// the track families of the world selects (docs/xdl-format §10.5.1), GameCube numbering
pub const MODES: [(&str, u32, u32); 6] =
    [("joyride", 0, 5), ("mission", 5, 19), ("adventure", 5, 19), ("battle", 19, 27), ("drag", 29, 35), ("challenge", 39, 57)];
/// the most active triggers of each type the game keeps at once (TriggerStats[type].max, data_gen.c)
pub const TRIGGER_MAX: [u32; 28] = [
    0, 0x28, 0x14, 0xA, 0x14, 0x14, 0xA, 0x28, 0xA, 0xA, 0xA, 0x3C, 0xA, 0xA, 0xA, 0x5, 0xA, 0xA, 0xA, 0x5, 0xA, 0xA, 0x5, 0x5, 0x5, 0xA, 0xA, 0x28,
];
const PACK_ONLY: [&str; 4] = ["starts", "items", "fxPoints", "objectives"];

fn lerr(m: impl Into<String>) -> Error {
    Error::level(m)
}

/// a Python exception the tool doesn't catch where it happens: "TypeError: ..."
fn pyerr(kind: &str, m: impl std::fmt::Display) -> Error {
    Error::uncaught(format!("{kind}: {m}"))
}

fn re(s: &'static str, cell: &'static OnceLock<Regex>) -> &'static Regex {
    cell.get_or_init(|| Regex::new(s).unwrap())
}

macro_rules! rx {
    ($s:expr) => {{
        static C: OnceLock<Regex> = OnceLock::new();
        re($s, &C)
    }};
}

// ------------------------------------------------------------------------------------------
// Python's view of JSON values

/// isinstance(v, int) (bool included)
pub fn is_int(v: &Value) -> bool {
    v.is_i64() || v.is_u64() || v.is_boolean()
}

/// isinstance(v, (int, float)) (bool included)
pub fn is_num(v: &Value) -> bool {
    v.is_number() || v.is_boolean()
}

pub fn truthy(v: &Value) -> bool {
    match v {
        Value::Null => false,
        Value::Bool(b) => *b,
        Value::Number(n) => n.as_f64() != Some(0.0),
        Value::String(s) => !s.is_empty(),
        Value::Array(a) => !a.is_empty(),
        Value::Object(m) => !m.is_empty(),
    }
}

/// Python int(v)
pub fn py_int(v: &Value) -> Result<i128> {
    match v {
        Value::Bool(b) => Ok(*b as i128),
        Value::Number(n) => match Num::of(v) {
            Some(Num::Int(i)) => Ok(i),
            _ => {
                let f = n.as_f64().unwrap_or(f64::NAN);
                if f.is_nan() {
                    return Err(pyerr("ValueError", "cannot convert float NaN to integer"));
                }
                if f.is_infinite() {
                    return Err(pyerr("OverflowError", "cannot convert float infinity to integer"));
                }
                Ok(f.trunc() as i128)
            }
        },
        Value::String(s) => {
            let t: String = s.trim().replace('_', "");
            t.parse::<i128>().map_err(|_| pyerr("ValueError", format!("invalid literal for int() with base 10: {}", py::str_repr(s))))
        }
        _ => Err(pyerr(
            "TypeError",
            format!("int() argument must be a string, a bytes-like object or a real number, not '{}'", py::type_name(v)),
        )),
    }
}

/// Python float(v)
pub fn py_float(v: &Value) -> Result<f64> {
    match v {
        Value::Bool(b) => Ok(*b as i64 as f64),
        Value::Number(_) => Ok(Num::of(v).map(Num::f64).unwrap_or(0.0)),
        Value::String(s) => py::float_of_str(s).ok_or_else(|| pyerr("ValueError", format!("could not convert string to float: {}", py::str_repr(s)))),
        _ => Err(pyerr("TypeError", format!("float() argument must be a string or a real number, not '{}'", py::type_name(v)))),
    }
}

/// d.get(k) of a dict (AttributeError for anything else)
pub fn dget<'a>(d: &'a Value, k: &str) -> Result<Option<&'a Value>> {
    match d {
        Value::Object(m) => Ok(m.get(k)),
        _ => Err(pyerr("AttributeError", format!("'{}' object has no attribute 'get'", py::type_name(d)))),
    }
}

/// d.items() of a dict
fn ditems(d: &Value) -> Result<Vec<(String, Value)>> {
    match d {
        Value::Object(m) => Ok(m.iter().map(|(k, v)| (k.clone(), v.clone())).collect()),
        _ => Err(pyerr("AttributeError", format!("'{}' object has no attribute 'items'", py::type_name(d)))),
    }
}

/// `x or {}`
fn or_empty(v: Option<&Value>) -> Value {
    match v {
        Some(x) if truthy(x) => x.clone(),
        _ => json!({}),
    }
}

/// dict(v) of a JSON value (a dict; a list of pairs; else the Python's errors)
fn to_dict(v: &Value) -> Result<Map<String, Value>> {
    match v {
        Value::Object(m) => Ok(m.clone()),
        Value::Array(a) => {
            let mut m = Map::new();
            for (i, x) in a.iter().enumerate() {
                match x {
                    Value::Array(p) if p.len() == 2 => {
                        let Some(k) = p[0].as_str() else {
                            return Err(pyerr("TypeError", "keys of a level's records are strings"));
                        };
                        m.insert(k.into(), p[1].clone());
                    }
                    Value::String(s) if s.chars().count() == 2 => {
                        let c: Vec<char> = s.chars().collect();
                        m.insert(c[0].to_string(), Value::String(c[1].to_string()));
                    }
                    _ => {
                        return Err(pyerr(
                            "ValueError",
                            format!("dictionary update sequence element #{i} has length {}; 2 is required", py_len(x).unwrap_or(0)),
                        ));
                    }
                }
            }
            Ok(m)
        }
        Value::String(_) => Err(pyerr("ValueError", "dictionary update sequence element #0 has length 1; 2 is required")),
        _ => Err(pyerr("TypeError", format!("'{}' object is not iterable", py::type_name(v)))),
    }
}

fn py_len(v: &Value) -> Option<usize> {
    match v {
        Value::Array(a) => Some(a.len()),
        Value::Object(m) => Some(m.len()),
        Value::String(s) => Some(s.chars().count()),
        _ => None,
    }
}

/// f"{v}" of a JSON value
fn s(v: &Value) -> String {
    py::str_of(v)
}

fn f64v(x: f64) -> Value {
    serde_json::Number::from_f64(x).map(Value::Number).unwrap_or(Value::Null)
}

/// Python's format(x, "g")
pub fn fmt_g(x: f64) -> String {
    if x.is_nan() {
        return "nan".into();
    }
    if x.is_infinite() {
        return if x > 0.0 { "inf".into() } else { "-inf".into() };
    }
    if x == 0.0 {
        return if x.is_sign_negative() { "-0".into() } else { "0".into() };
    }
    let e = format!("{:.5e}", x);
    let (m, ex) = e.split_once('e').unwrap();
    let exp: i32 = ex.parse().unwrap();
    if !(-4..6).contains(&exp) {
        let m = m.trim_end_matches('0').trim_end_matches('.');
        return format!("{m}e{}{:02}", if exp < 0 { '-' } else { '+' }, exp.abs());
    }
    let prec = (5 - exp).max(0) as usize;
    let f = format!("{:.*}", prec, x);
    if f.contains('.') { f.trim_end_matches('0').trim_end_matches('.').to_string() } else { f }
}

// ------------------------------------------------------------------------------------------
// the schemas, as xdl_level reads them (by file name)

pub struct Env {
    pub g: Ctx,
    pub t: Tac,
    schemas: RefCell<HashMap<String, Option<Value>>>,
    /// the hwvx-x to extract and pack archives with (None: find_hwvx_x)
    pub hwvx_x: Option<PathBuf>,
}

impl Env {
    /// root: the HWVX-SDL checkout (schemas/)
    pub fn new(root: &Path) -> Result<Env> {
        let g = Ctx::new(root)?;
        let t = Tac::from_schemas(&g.s)?;
        Ok(Env { g, t, schemas: RefCell::new(HashMap::new()), hwvx_x: None })
    }

    pub fn root(&self) -> &Path {
        &self.g.root
    }

    fn schema(&self, name: &str) -> Result<Option<Value>> {
        if let Some(v) = self.schemas.borrow().get(name) {
            return Ok(v.clone());
        }
        let p = self.root().join("schemas").join(format!("{name}.schema.json"));
        let v = if p.exists() { Some(jsonc::parse(&crate::error::read_text(&p)?)?) } else { None };
        self.schemas.borrow_mut().insert(name.into(), v.clone());
        Ok(v)
    }

    /// (properties, size) of a struct the schemas describe (Trigger's records too)
    pub fn struct_of(&self, name: &str) -> Result<Option<(Map<String, Value>, usize)>> {
        let name = name.replace("struct ", "");
        let name = name.trim();
        let s = if name == "Trigger" {
            self.schema("Trigger")?
        } else if name == "TriggerAction" {
            self.schema("Trigger")?.map(|t| t["$defs"]["TriggerAction"].clone())
        } else {
            self.schema(name)?
        };
        let Some(s) = s else { return Ok(None) };
        let props = s["properties"].as_object().cloned().unwrap_or_default();
        Ok(Some((props, schema::size(&s)?)))
    }

    /// the hwvx-x it uses: the one given, else target/release/hwvx-x in or above tools/, else
    /// on the PATH
    pub fn find_hwvx_x(&self) -> Option<PathBuf> {
        if let Some(p) = &self.hwvx_x {
            return Some(p.clone());
        }
        let tools = self.root().join("tools");
        for d in tools.ancestors() {
            let p = d.join("target").join("release").join("hwvx-x");
            if p.exists() {
                return Some(p);
            }
        }
        let path = std::env::var_os("PATH")?;
        std::env::split_paths(&path).map(|d| d.join("hwvx-x")).find(|p| p.is_file())
    }
}

pub fn link_of(p: &Value) -> Option<&Value> {
    if let Some(x) = p.get("x-link") {
        return Some(x);
    }
    p.get("anyOf").and_then(Value::as_array).and_then(|a| a.iter().find_map(|alt| alt.get("x-link")))
}

fn off_of(p: &Value) -> Result<u32> {
    schema::offset(p)
}

fn ref_last(p: &Value) -> String {
    s(&p["$ref"]).rsplit('/').next().unwrap_or("").to_string()
}

// ------------------------------------------------------------------------------------------
// base links: structural paths as pointer walks

fn is_digits(s: &str) -> bool {
    !s.is_empty() && s.bytes().all(|c| c.is_ascii_digit())
}

fn ptr_inner(cur: Option<&str>) -> Option<String> {
    let c = cur.unwrap_or("");
    c.strip_prefix("PTR32(").and_then(|r| r.strip_suffix(')')).filter(|r| !r.is_empty()).map(str::to_string)
}

fn po_seg(seg: &str) -> Option<(char, u32)> {
    let c = seg.chars().next()?;
    if (c == 'p' || c == 'o') && seg.len() > 1 && seg[1..].bytes().all(|b| b.is_ascii_hexdigit()) {
        return Some((c, u32::from_str_radix(&seg[1..], 16).unwrap_or(u32::MAX)));
    }
    None
}

/// {"root", "index", "steps"} for a path in a world pack (World/0/..., trigger/N/...)
pub fn base_steps(env: &Env, path: &str, wh: &str) -> Result<Value> {
    let segs: Vec<&str> = path.split('/').collect();
    let (root, index, mut cur, rest): (&str, i128, Option<String>, &[&str]) = if segs.len() >= 2 && segs[0] == "World" && segs[1] == "0" {
        ("world", 0, Some("World".into()), &segs[2..])
    } else if segs.len() >= 2 && segs[0] == "trigger" && is_digits(segs[1]) {
        ("trigger", segs[1].parse().unwrap_or(0), Some("Trigger".into()), &segs[2..])
    } else {
        return Err(lerr(format!("{wh}: {path}: a base link starts at World/0 or trigger/N")));
    };
    let mut steps: Vec<(char, i128)> = vec![];
    let mut item: Option<String> = None;
    let mut item_size: Option<i128> = None;
    for seg in rest {
        let m = po_seg(seg);
        if is_digits(seg) {
            let Some(isz) = item_size else {
                return Err(lerr(format!("{wh}: {path}: {seg}: not an array here")));
            };
            // checked: a huge index is a level error, not an overflow
            let k = seg.parse::<i128>().ok().and_then(|k| k.checked_mul(isz));
            steps.push(('a', k.ok_or_else(|| lerr(format!("{wh}: {path}: {seg}: an index out of range")))?));
            cur = item.take();
            item_size = None;
        } else if *seg == "to" {
            steps.push(('p', 0));
            cur = ptr_inner(cur.as_deref());
            item = None;
            item_size = None;
        } else if let Some((c, n)) = m {
            steps.push((if c == 'p' { 'p' } else { 'a' }, n as i128));
            cur = None;
            item = None;
            item_size = None;
        } else {
            let props = match &cur {
                Some(c) => env.struct_of(c)?.map(|x| x.0),
                None => None,
            };
            let Some(p) = props.as_ref().and_then(|p| p.get(*seg)).filter(|p| p.get("x-offset").is_some()) else {
                return Err(lerr(format!("{wh}: {path}: {seg}: not a field of {}", cur.as_deref().unwrap_or("this record"))));
            };
            let off = off_of(p)? as i128;
            if let Some(ln) = link_of(p) {
                steps.push(('p', off));
                if let Some(isz) = ln.get("item-size") {
                    cur = None;
                    item = Some(s(ln.get("item").unwrap_or(&ln["target"])));
                    item_size = Some(py_int(isz)?);
                } else {
                    cur = Some(s(&ln["target"]));
                    item = None;
                    item_size = None;
                }
            } else if p.get("$ref").is_some() {
                steps.push(('a', off));
                cur = Some(ref_last(p));
                item = None;
                item_size = None;
            } else {
                return Err(lerr(format!("{wh}: {path}: {seg} is a number, not a record")));
            }
        }
    }
    let mut out: Vec<(char, i128)> = vec![];
    for st in steps {
        if st.0 == 'a' && out.last().is_some_and(|l| l.0 == 'a') {
            let l = out.last_mut().unwrap();
            l.1 = l.1.checked_add(st.1).ok_or_else(|| lerr(format!("{wh}: {path}: an offset out of range")))?;
        } else if !(st.0 == 'a' && st.1 == 0) {
            out.push(st);
        }
    }
    let steps: Vec<Value> = out.into_iter().map(|(c, n)| json!([c.to_string(), crate::decode::int_value(n)])).collect();
    Ok(json!({"root": root, "index": crate::decode::int_value(index), "steps": steps}))
}

/// The struct a base path names (None for raw nodes).
pub fn record_struct(env: &Env, path: &str) -> Result<Option<String>> {
    let segs: Vec<&str> = path.split('/').collect();
    let mut cur: Option<String> = Some(if segs[0] == "World" { "World".into() } else { "Trigger".into() });
    let mut item: Option<String> = None;
    for seg in segs.iter().skip(2) {
        if is_digits(seg) {
            cur = item.take();
        } else if *seg == "to" {
            cur = ptr_inner(cur.as_deref());
        } else if po_seg(seg).is_some() {
            cur = None;
        } else {
            let props = match &cur {
                Some(c) => env.struct_of(c)?.map(|x| x.0),
                None => None,
            };
            let Some(p) = props.as_ref().and_then(|p| p.get(*seg)) else { return Ok(None) };
            let ln = link_of(p);
            if let Some(ln) = ln.filter(|l| l.get("item-size").is_some()) {
                cur = None;
                item = Some(s(ln.get("item").unwrap_or(&ln["target"])));
            } else if let Some(ln) = ln {
                cur = Some(s(&ln["target"]).replace("struct ", ""));
            } else if p.get("$ref").is_some() {
                cur = Some(ref_last(p));
            }
        }
    }
    Ok(cur)
}

/// (link, offset, prop) of a number field of a base record named by path
pub fn field_target(env: &Env, path: &str, field: &str, wh: &str) -> Result<(Value, u32, Value)> {
    let link = base_steps(env, path, wh)?;
    let rec = record_struct(env, path)?;
    let props = match &rec {
        Some(r) => env.struct_of(r)?.map(|x| x.0),
        None => None,
    };
    let Some(props) = props else { return Err(lerr(format!("{wh}: {path}: no schema for what it names"))) };
    let rec = rec.unwrap();
    let parts: Vec<&str> = field.split('/').collect();
    let Some(p0) = props.get(parts[0]).filter(|p| p.get("x-offset").is_some()) else {
        return Err(lerr(format!("{wh}: {field}: not a field of {rec}")));
    };
    let mut p = p0.clone();
    let mut off = off_of(&p)?;
    if parts.len() == 2 && p.get("$ref").is_some() {
        let r = ref_last(&p);
        let sch = env.schema(&rec)?.unwrap_or(Value::Null);
        let sub = sch["$defs"].get(&r).map(|d| d["properties"].clone()).ok_or_else(|| pyerr("KeyError", py::str_repr(&r)))?;
        let Some(q) = sub.get(parts[1]) else { return Err(lerr(format!("{wh}: {field}: not a field of {r}"))) };
        p = q.clone();
        off += off_of(&p)?;
    } else if parts.len() != 1 || p.get("$ref").is_some() {
        return Err(lerr(format!("{wh}: {field}: a number field (or field/member of an embedded struct)")));
    }
    if link_of(&p).is_some() {
        return Err(lerr(format!("{wh}: {field}: a link (patches set numbers)")));
    }
    Ok((link, off, p))
}

// ------------------------------------------------------------------------------------------
// records of the level's own

/// A number field's bytes: floats for f32, names for x-enum, lists for arrays.
pub fn scalar(t: &Tac, p: &Value, v: &Value, wh: &str, big: bool) -> Result<Vec<u8>> {
    let (base, dims) = schema::parse_ctype(schema::ctype(p));
    let pr = schema::prim(&base).ok_or_else(|| pyerr("KeyError", py::str_repr(&base)))?;
    if !dims.is_empty() {
        let n: usize = dims.iter().product();
        let Some(l) = v.as_array().filter(|l| l.len() == n) else { return Err(lerr(format!("{wh}: {n} values"))) };
        let q = json!({"x-c-type": base});
        let mut out = vec![];
        for (i, x) in l.iter().enumerate() {
            out.extend(scalar(t, &q, x, &format!("{wh}/{i}"), big)?);
        }
        return Ok(out);
    }
    if pr.float {
        if v.is_boolean() || !v.is_number() {
            return Err(lerr(format!("{wh}: a number")));
        }
        return pack_f(pr.code, py_float(v)?, big);
    }
    match tac::encode_scalar(t, p, v, big, wh) {
        Ok(b) => Ok(b),
        Err(e) if e.kind == Kind::Tac => Err(lerr(e.msg)),
        Err(e) => Err(e),
    }
}

fn pack_f(code: char, v: f64, big: bool) -> Result<Vec<u8>> {
    crate::encode::pack_float(code, v, big).map_err(|e| if e.kind == Kind::Uncaught { pyerr("OverflowError", e.msg) } else { e })
}

fn f32b(v: f64, big: bool) -> Result<Vec<u8>> {
    pack_f('f', v, big)
}

fn u32b(v: u32, big: bool) -> [u8; 4] {
    if big { v.to_be_bytes() } else { v.to_le_bytes() }
}

/// struct.pack integer of a code with Python's range error (struct.error, uncaught)
fn packi(code: char, v: i128, big: bool) -> Result<Vec<u8>> {
    crate::encode::pack_int(code, v, big).map_err(|e| pyerr("struct.error", e))
}

/// A volume's settings as the game reads them, by its type.
fn volume_data(kind: &Value, d: &Value, big: bool, wh: &str) -> Result<Vec<u8>> {
    let get = |k: &str| dget(d, k);
    let f = |k: &str| -> Result<f64> {
        let x = get(k)?.cloned().unwrap_or(json!(0.0));
        if x.is_boolean() || !x.is_number() {
            return Err(lerr(format!("{wh}/{k}: a number")));
        }
        py_float(&x)
    };
    let i = |k: &str, lo: i128, hi: i128| -> Result<i128> {
        let x = get(k)?.cloned().unwrap_or(json!(0));
        let ok = !x.is_boolean() && (x.is_i64() || x.is_u64()) && matches!(Num::of(&x), Some(Num::Int(n)) if lo <= n && n <= hi);
        if !ok {
            return Err(lerr(format!("{wh}/{k}: a whole number {lo}..{hi}")));
        }
        py_int(&x)
    };
    let color = |k: &str, n: usize| -> Result<Vec<u8>> {
        let c = get(k)?.cloned().unwrap_or_else(|| json!(vec![255; n]));
        let ok = c.as_array().is_some_and(|a| a.len() == n && a.iter().all(|x| is_int(x) && matches!(py_int(x), Ok(v) if (0..=255).contains(&v))));
        if !ok {
            return Err(lerr(format!("{wh}/{k}: {n} numbers 0..255")));
        }
        Ok(c.as_array().unwrap().iter().map(|x| py_int(x).unwrap() as u8).collect())
    };
    let floats = |v: &Value, want: usize| -> Result<Vec<u8>> {
        let a = match v {
            Value::Array(a) => a.clone(),
            Value::Object(m) => m.keys().map(|k| Value::String(k.clone())).collect(),
            Value::String(st) => st.chars().map(|c| Value::String(c.to_string())).collect(),
            _ => return Err(pyerr("TypeError", format!("'{}' object is not iterable", py::type_name(v)))),
        };
        let mut out = vec![];
        let fl: Vec<f64> = a.iter().map(py_float).collect::<Result<_>>()?;
        if fl.len() + 1 != want {
            return Err(pyerr("struct.error", format!("pack expected {want} items for packing (got {})", fl.len() + 1)));
        }
        for x in fl {
            out.extend(f32b(x, big)?);
        }
        out.extend(f32b(0.0, big)?);
        Ok(out)
    };
    match kind.as_str() {
        Some("force_box") => {
            let fv = get("force")?.cloned().unwrap_or(json!([0, 0, 0]));
            if !fv.as_array().is_some_and(|a| a.len() == 3) {
                return Err(lerr(format!("{wh}/force: [x, y, z]")));
            }
            let mut out = floats(&fv, 4)?;
            out.extend(packi('b', i("oneShot", -128, 127)?, big)?);
            out.extend(packi('b', i("carOnly", -128, 127)?, big)?);
            out.extend(packi('H', i("type", 0, 65535)?, big)?);
            Ok(out)
        }
        Some("reverb") => {
            let mut out = packi('I', i("type", 0, 0xFFFFFFFF)?, big)?;
            out.extend(packi('H', i("volume", 0, 65535)?, big)?);
            out.extend(packi('H', i("feedback", 0, 65535)?, big)?);
            Ok(out)
        }
        Some("fog") => {
            let mut out = vec![i("type", 0, 255)? as u8, 0, 0, 0];
            out.extend(f32b(f("near")?, big)?);
            out.extend(f32b(f("far")?, big)?);
            out.extend(color("color", 4)?);
            Ok(out)
        }
        Some("light") => {
            let dv = get("direction")?.cloned().unwrap_or(json!([0, 0, 1]));
            let mut out = floats(&dv, 4)?;
            out.extend(color("color", 3)?);
            out.push(0);
            out.extend(u32b(0, big));
            out.push(i("lightType", 0, 255)? as u8);
            out.push((i("subtract", -128, 127)? & 0xFF) as u8);
            out.extend([0, 0]);
            Ok(out)
        }
        Some("creature") => Ok(vec![i("rescue", 0, 255)? as u8, 0, 0, 0]),
        _ => Err(lerr(format!("{wh}: an {} volume has no settings", s(kind)))),
    }
}

/// Python's numbers for the few sums the compiler does (exact ints, floats once a float joins)
#[derive(Clone, Copy)]
enum PN {
    I(i128),
    F(f64),
}

impl PN {
    fn of(v: &Value) -> Result<PN> {
        match v {
            Value::Bool(b) => Ok(PN::I(*b as i128)),
            _ => match Num::of(v) {
                Some(Num::Int(i)) => Ok(PN::I(i)),
                Some(Num::Float(f)) => Ok(PN::F(f)),
                None => Err(pyerr("TypeError", format!("unsupported operand type(s): '{}'", py::type_name(v)))),
            },
        }
    }
    fn f(self) -> f64 {
        match self {
            PN::I(i) => i as f64,
            PN::F(f) => f,
        }
    }
    fn add(self, o: PN) -> PN {
        match (self, o) {
            (PN::I(a), PN::I(b)) => PN::I(a + b),
            _ => PN::F(self.f() + o.f()),
        }
    }
    fn sub(self, o: PN) -> PN {
        match (self, o) {
            (PN::I(a), PN::I(b)) => PN::I(a - b),
            _ => PN::F(self.f() - o.f()),
        }
    }
    fn sq(self) -> PN {
        match self {
            PN::I(a) => PN::I(a * a),
            PN::F(f) => PN::F(f * f),
        }
    }
    fn lt(self, o: PN) -> bool {
        match (self, o) {
            (PN::I(a), PN::I(b)) => a < b,
            _ => self.f() < o.f(),
        }
    }
}

// ------------------------------------------------------------------------------------------
// the compiler

#[derive(Debug, Clone)]
pub struct Part {
    pub id: String,
    pub kind: &'static str,
    pub data: Vec<u8>,
    /// offset -> link, as made
    pub links: BTreeMap<u32, String>,
    /// [offset, resolved] once the links are resolved
    pub resolved: Vec<(u32, Value)>,
}

/// The base world as tools/xdl_graph.py extracts it (for checking links).
pub struct BaseWorld {
    pub dir: PathBuf,
    pub archive: String,
    ids: HashSet<String>,
    counts: RefCell<HashMap<String, Option<usize>>>,
}

pub fn read_jsonc(p: &Path) -> Result<Value> {
    let text = std::fs::read_to_string(p).map_err(|e| lerr(format!("{}: {}", p.display(), io_text(&e))))?;
    jsonc::loads(&text).map_err(|e| if e.kind == Kind::Uncaught { e } else { lerr(format!("{}: {}", p.display(), e.msg)) })
}

/// an OSError's text as Python prints it
fn io_text(e: &std::io::Error) -> String {
    match e.kind() {
        std::io::ErrorKind::NotFound => "[Errno 2] No such file or directory".into(),
        std::io::ErrorKind::IsADirectory => "[Errno 21] Is a directory".into(),
        std::io::ErrorKind::PermissionDenied => "[Errno 13] Permission denied".into(),
        _ => e.to_string(),
    }
}

impl BaseWorld {
    pub fn open(folder: &Path) -> Result<BaseWorld> {
        let pj = read_jsonc(&folder.join("pack.jsonc"))?;
        let archive = s(dget(&pj, "archive")?.ok_or_else(|| pyerr("KeyError", "'archive'"))?);
        let pre = format!("base://{archive}/world_0/");
        let n = pre.chars().count();
        let ids = pj["layout"].as_array().map(|a| a.iter().map(|x| s(&x["id"]).chars().skip(n).collect()).collect()).unwrap_or_default();
        Ok(BaseWorld { dir: folder.to_path_buf(), archive, ids, counts: RefCell::new(HashMap::new()) })
    }

    pub fn count(&self, node: &str) -> Result<Option<usize>> {
        if let Some(c) = self.counts.borrow().get(node) {
            return Ok(*c);
        }
        let f = self.dir.join(format!("{node}.jsonc"));
        let mut n = None;
        if f.exists() {
            let d = read_jsonc(&f)?;
            if let Some(a) = d.get("items").and_then(Value::as_array) {
                n = Some(a.len());
            }
        }
        self.counts.borrow_mut().insert(node.into(), n);
        Ok(n)
    }

    /// None if the path names something of the world, else why not.
    pub fn check(&self, path: &str) -> Result<Option<String>> {
        if self.ids.contains(path) {
            return Ok(None);
        }
        let segs: Vec<&str> = path.split('/').collect();
        for i in (1..segs.len()).rev() {
            let pre = segs[..i].join("/");
            if self.ids.contains(&pre) {
                let rest = &segs[i..];
                if !rest.is_empty() && is_digits(rest[0]) {
                    let n = self.count(&pre)?;
                    if let Some(n) = n
                        && rest[0].parse::<u128>().unwrap_or(u128::MAX) >= n as u128
                    {
                        return Ok(Some(format!("{pre} has {n} entries")));
                    }
                    return Ok(None);
                }
                return Ok(None);
            }
        }
        Ok(Some("the world has no such record".into()))
    }

    fn trigger_files(&self) -> Vec<(i128, PathBuf)> {
        let mut out = vec![];
        if let Ok(rd) = std::fs::read_dir(self.dir.join("trigger")) {
            for e in rd.flatten() {
                let n = e.file_name().to_string_lossy().into_owned();
                if let Some(stem) = n.strip_suffix(".jsonc")
                    && is_digits(stem)
                {
                    out.push((stem.parse().unwrap_or(0), e.path()));
                }
            }
        }
        out.sort_by_key(|x| x.0);
        out
    }

    pub fn num_triggers(&self) -> usize {
        self.trigger_files().len()
    }

    /// {type: count} of the base triggers kept (in file order)
    pub fn trigger_types(&self, keep: &Value) -> Result<IndexMap<String, (Value, u32)>> {
        let mut out: IndexMap<String, (Value, u32)> = IndexMap::new();
        for (i, f) in self.trigger_files() {
            let kept = keep.as_str() == Some("all") || keep.as_array().is_some_and(|a| a.iter().any(|x| matches!(py_int(x), Ok(v) if v == i) && is_int(x)));
            if kept {
                let t = read_jsonc(&f)?.get("type").cloned().unwrap_or(Value::Null);
                let e = out.entry(py::repr(&t)).or_insert((t, 0));
                e.1 += 1;
            }
        }
        Ok(out)
    }
}

/// How links are written: the level file's (pointer walks, parts) or the world pack build's
/// (the rebuilt pack's node ids).
pub enum Mode {
    Memory,
    /// level:// -> the pack's node id
    Pack(IndexMap<String, String>),
}

pub struct Compiler<'e> {
    pub env: &'e Env,
    pub proj: PathBuf,
    pub skip_intro: bool,
    pub spawn_box: Option<SpawnBox>,
    pub warnings: Vec<String>,
    pub lv: Value,
    pack_only: HashSet<String>,
    pub big: bool,
    pub parts: Vec<Part>,
    part_ids: HashSet<String>,
    pub patches: Vec<Value>,
    pub errors: Vec<String>,
    pub base: Option<BaseWorld>,
    /// type (repr) -> (type, count), in the order first made
    trigger_types: IndexMap<String, (Value, u32)>,
    var_ids: i128,
    pub appends: Vec<Value>,
    append_of: IndexMap<String, (usize, usize)>,
    pub mode: Mode,
}

pub type SpawnBox = ([f64; 3], [f64; 3]);

impl<'e> Compiler<'e> {
    pub fn new(env: &'e Env, proj: &Path, base: Option<&Path>, skip_intro: bool, spawn_box: Option<SpawnBox>) -> Result<Compiler<'e>> {
        Ok(Compiler {
            env,
            proj: proj.to_path_buf(),
            skip_intro,
            spawn_box,
            warnings: vec![],
            lv: json!({}),
            pack_only: HashSet::new(),
            big: false,
            parts: vec![],
            part_ids: HashSet::new(),
            patches: vec![],
            errors: vec![],
            base: match base {
                Some(b) => Some(BaseWorld::open(b)?),
                None => None,
            },
            trigger_types: IndexMap::new(),
            var_ids: 250,
            appends: vec![],
            append_of: IndexMap::new(),
            mode: Mode::Memory,
        })
    }

    fn err(&mut self, wh: &str, msg: impl std::fmt::Display) {
        self.errors.push(format!("{wh}: {msg}"));
    }

    fn add_part(&mut self, pid: &str, kind: &'static str, data: Vec<u8>, links: BTreeMap<u32, String>) -> Result<()> {
        if self.part_ids.contains(pid) {
            // the game refuses a level with two parts of one id (Port_Level.c)
            return Err(lerr(format!("{pid}: made twice (two records of one name?)")));
        }
        self.parts.push(Part { id: pid.into(), kind, data, links, resolved: vec![] });
        self.part_ids.insert(pid.into());
        Ok(())
    }

    fn schema_defs(&self, sname: &str) -> Result<Map<String, Value>> {
        Ok(self.env.schema(sname)?.and_then(|s| s.get("$defs").and_then(Value::as_object).cloned()).unwrap_or_default())
    }

    /// bytes and {offset: link} of one record of struct sname from its JSON
    fn encode_record(&mut self, sname: &str, obj: &Map<String, Value>, wh: &str, pid: &str) -> Result<(Vec<u8>, BTreeMap<u32, String>)> {
        let (props, size) = self.env.struct_of(sname)?.ok_or_else(|| pyerr("TypeError", "cannot unpack non-iterable NoneType object"))?;
        let mut buf = vec![0u8; size];
        let mut links = BTreeMap::new();
        let big = self.big;
        for (k, v) in obj {
            if ["$schema", "format_version", "extends", "name", "description"].contains(&k.as_str()) {
                continue;
            }
            let Some(p) = props.get(k).filter(|p| p.get("x-offset").is_some()) else {
                return Err(lerr(format!("{wh}/{k}: not a field of {sname}")));
            };
            let off = off_of(p)? as usize;
            if link_of(p).is_some() {
                if v.is_object() && k == "mode" {
                    let sub = format!("{pid}/mode");
                    let u8p = json!({"x-c-type": "u8"});
                    let mut raw = tac::encode_scalar(&self.env.t, &u8p, v.get("mode").unwrap_or(&json!(0)), big, &format!("{wh}/mode"))?;
                    raw.extend(tac::encode_scalar(&self.env.t, &u8p, v.get("type").unwrap_or(&json!(0)), big, &format!("{wh}/mode"))?);
                    raw.extend([0, 0]);
                    self.add_part(&sub, "data", raw, BTreeMap::new())?;
                    links.insert(off as u32, sub);
                } else if !v.is_null() {
                    let Some(l) = v.as_str() else { return Err(lerr(format!("{wh}/{k}: a link or null"))) };
                    links.insert(off as u32, l.to_string());
                }
            } else if p.get("$ref").is_some() {
                let r = ref_last(p);
                let defs = self.schema_defs(sname)?;
                let Some(vp) = defs.get(&r).map(|d| d["properties"].clone()) else {
                    return Err(lerr(format!("{wh}/{k}: an embedded {r} isn't supported")));
                };
                let v = if let Some(a) = v.as_array() {
                    let mut m = Map::new();
                    for (n, x) in ["x", "y", "z", "w"].iter().zip(a) {
                        m.insert(n.to_string(), x.clone());
                    }
                    Value::Object(m)
                } else {
                    v.clone()
                };
                for (kk, vv) in ditems(&v)? {
                    let Some(q) = vp.get(&kk) else { return Err(lerr(format!("{wh}/{k}/{kk}: not a field of {r}"))) };
                    let o = off + off_of(q)? as usize;
                    let b = scalar(&self.env.t, q, &vv, &format!("{wh}/{k}/{kk}"), big)?;
                    splice(&mut buf, o, &b);
                }
            } else {
                let b = scalar(&self.env.t, p, v, &format!("{wh}/{k}"), big)?;
                splice(&mut buf, off, &b);
            }
        }
        Ok((buf, links))
    }

    // -- the kinds ----------------------------------------------------------------------------

    fn volume(&mut self, name: &str, v: &Value, wh: &str) -> Result<()> {
        let (props, _) = self.env.struct_of("WorldVolume")?.ok_or_else(|| pyerr("TypeError", "no WorldVolume schema"))?;
        let mut v = to_dict(v)?;
        let bx = v.shift_remove("box");
        if let Some(bx) = bx.filter(|b| !b.is_null()) {
            let lo = bx.as_object().and_then(|m| m.get("min")).cloned();
            let hi = bx.as_object().and_then(|m| m.get("max")).cloned();
            let ok = (|| {
                let lo = lo.as_ref()?.as_array()?;
                let hi = hi.as_ref()?.as_array()?;
                if lo.len() != 3 || hi.len() != 3 || !lo.iter().chain(hi).all(is_num) {
                    return None;
                }
                let l: Vec<PN> = lo.iter().map(|x| PN::of(x).unwrap()).collect();
                let h: Vec<PN> = hi.iter().map(|x| PN::of(x).unwrap()).collect();
                if (0..3).all(|i| l[i].lt(h[i])) { Some((l, h)) } else { None }
            })();
            let Some((l, h)) = ok else { return Err(lerr(format!("{wh}/box: min and max [x, y, z], min below max"))) };
            let lf = [l[0].f(), l[1].f(), l[2].f()];
            let hf = [h[0].f(), h[1].f(), h[2].f()];
            let b = bsp::box_bsp(lf, hf, self.big)?;
            let bid = format!("level://volumes/{name}/bsp");
            self.add_part(&bid, "data", b, BTreeMap::new())?;
            v.insert("shape".into(), json!("bsp"));
            v.insert("bsp".into(), json!(bid));
            if !v.contains_key("center") {
                v.insert("center".into(), json!((0..3).map(|i| f64v(l[i].add(h[i]).f() / 2.0)).collect::<Vec<_>>()));
            }
            if !v.contains_key("radius") {
                let mut sum = PN::I(0);
                for i in 0..3 {
                    sum = sum.add(h[i].sub(l[i]).sq());
                }
                v.insert("radius".into(), f64v(0.5 * sum.f().sqrt()));
            }
            if !v.contains_key("radiusSq") {
                v.insert("radiusSq".into(), json!(0.0));
            }
        }
        let data = v.shift_remove("data");
        match data {
            Some(d) if d.is_object() => {
                let kind = v.get("type").cloned().unwrap_or(json!("airbox"));
                let raw = volume_data(&kind, &d, self.big, &format!("{wh}/data"))?;
                let did = format!("level://volumes/{name}/data");
                self.add_part(&did, "data", raw, BTreeMap::new())?;
                v.insert("data".into(), json!(did));
                let t = v.get("type").map(|t| s(t)).unwrap_or_else(|| "None".into());
                self.warnings.push(format!(
                    "{wh}/type: a {t} volume of the level's own isn't in the world's volume grid (VolumeHolder): only the airbox triggers find a volume without it"
                ));
            }
            Some(d) if !d.is_null() => {
                v.insert("data".into(), d);
            }
            _ => {}
        }
        for k in v.keys() {
            if !props.get(k).is_some_and(|p| p.get("x-offset").is_some()) {
                return Err(lerr(format!("{wh}/{k}: not a field of WorldVolume")));
            }
        }
        v.entry("enabled").or_insert(json!(1));
        v.entry("shape").or_insert(json!("sphere"));
        v.entry("type").or_insert(json!("airbox"));
        if let Some(Value::Array(c)) = v.get("center").cloned()
            && c.len() == 3
        {
            let mut c = c;
            c.push(json!(0.0));
            v.insert("center".into(), Value::Array(c));
        }
        let r = v.get("radius").cloned().filter(|r| !r.is_null());
        if let Some(r) = r
            && !v.contains_key("radiusSq")
            && v.get("shape").map(|x| x.as_str() != Some("bsp")).unwrap_or(true)
        {
            let f = py_float(&r)?;
            v.insert("radiusSq".into(), f64v(f * f));
        }
        if v.get("shape").and_then(Value::as_str) == Some("sphere") && !v.get("radiusSq").is_some_and(truthy) {
            return Err(lerr(format!("{wh}: a sphere needs a radius")));
        }
        let pid = format!("level://volumes/{name}");
        let (buf, links) = self.encode_record("WorldVolume", &v, wh, &pid)?;
        self.add_part(&pid, "volume", buf, links)
    }

    fn timer(&mut self, name: &str, v: &Value, wh: &str) -> Result<()> {
        let mut v = to_dict(v)?;
        v.entry("active").or_insert(json!(1));
        v.entry("mode").or_insert(json!({"mode": 0x17, "type": 7}));
        let pid = format!("level://timers/{name}/to");
        let (buf, links) = self.encode_record("WorldTimer", &v, wh, &pid)?;
        self.add_part(&pid, "timer", buf, links)
    }

    fn variable(&mut self, name: &str, v: &Value, wh: &str) -> Result<()> {
        let v = if is_num(v) { json!({"value": v}) } else { v.clone() };
        let mut v = to_dict(&v)?;
        if !v.contains_key("id") {
            // the id interfaces bind a variable by: one no retail screen uses
            self.var_ids -= 1;
            v.insert("id".into(), crate::decode::int_value(self.var_ids));
        }
        let pid = format!("level://variables/{name}/to");
        let (buf, links) = self.encode_record("WorldVariable", &v, wh, &pid)?;
        self.add_part(&pid, "variable", buf, links)
    }

    fn string(&mut self, name: &str, v: &Value, wh: &str) -> Result<()> {
        let paras: Vec<Value> = match v {
            Value::Array(a) => a.clone(),
            x => vec![x.clone()],
        };
        if paras.is_empty() || !paras.iter().all(Value::is_string) {
            return Err(lerr(format!("{wh}: a text or a list of paragraphs")));
        }
        let table = vec![0u8; 4 * (paras.len() + 1)];
        let mut links = BTreeMap::new();
        for (i, t) in paras.iter().enumerate() {
            let raw: Vec<u8> = t.as_str().unwrap().chars().map(|c| if (c as u32) < 256 { c as u32 as u8 } else { b'?' }).collect();
            if raw.len() > 255 {
                return Err(lerr(format!("{wh}: paragraph {i} is over 255 characters")));
            }
            let tid = format!("level://strings/{name}/{i}");
            let mut d = raw.clone();
            d.extend(std::iter::repeat_n(0u8, 4 - raw.len() % 4));
            self.add_part(&tid, "data", d, BTreeMap::new())?;
            links.insert(4 * i as u32, tid);
        }
        self.add_part(&format!("level://strings/{name}"), "data", table, links)
    }

    fn trigger(&mut self, name: &str, doc: &Value, wh: &str) -> Result<()> {
        let tid = format!("level://triggers/{name}");
        let mut doc = to_dict(doc)?;
        for k in ["$schema", "format_version", "id"] {
            doc.shift_remove(k);
        }
        for lst in ["actions", "elseActions"] {
            if let Some(Value::Array(a)) = doc.get_mut(lst) {
                for x in a.iter_mut() {
                    if let Value::Object(m) = x {
                        if m.get("count").and_then(Value::as_str) == Some("always") {
                            m.insert("count".into(), json!(0xFFFFFFFFu32));
                        }
                        m.entry("count").or_insert(json!(1));
                        if let Some(di) = m.get("delayInit").cloned() {
                            m.entry("delay").or_insert(di);
                        }
                    }
                }
            }
        }
        let doc = Value::Object(doc);
        let parts = match tac::encode_trigger(&self.env.t, &doc, &tid, self.big, wh) {
            Ok(p) => p,
            Err(e) if e.kind == Kind::Tac => return Err(lerr(e.msg)),
            Err(e) => return Err(e),
        };
        let t = doc.get("type").cloned().unwrap_or(Value::Null);
        self.trigger_types.entry(py::repr(&t)).or_insert((t, 0)).1 += 1;
        for (i, (pid, data, links)) in parts.into_iter().enumerate() {
            let l = links.into_iter().map(|(o, v)| (o, s(&v))).collect();
            self.add_part(&pid, if i == 0 { "trigger" } else { "data" }, data, l)?;
        }
        Ok(())
    }

    // -- the modes ------------------------------------------------------------------------------

    fn base_starts(&self) -> Result<Option<Vec<Value>>> {
        let Some(b) = &self.base else { return Ok(None) };
        let f = b.dir.join("World").join("0").join("tileCars.jsonc");
        if !f.exists() {
            return Ok(Some(vec![]));
        }
        let d = read_jsonc(&f)?;
        Ok(Some(match dget(&d, "items")? {
            Some(Value::Array(a)) if !a.is_empty() => a.clone(),
            Some(x) if truthy(x) => return Err(pyerr("TypeError", "tileCars.jsonc: items isn't a list")),
            _ => vec![],
        }))
    }

    fn port_of(c: &Value) -> Result<i128> {
        py_int(dget(c, "port")?.unwrap_or(&json!(0)))
    }

    fn mode_checks(&mut self, lv: &Value, mode: &str, wh: &str) -> Result<()> {
        let starts = or_empty(dget(lv, "starts")?);
        let mut own = vec![];
        for (_, st) in ditems(&starts)? {
            if st.is_object() && is_int(st.get("port").unwrap_or(&json!(0))) {
                own.push(st);
            }
        }
        let mut ai_own = 0;
        for st in &own {
            let p = Self::port_of(st)?;
            if !(1..=4).contains(&p) {
                ai_own += 1;
            }
        }
        let base = self.base_starts()?;
        if ai_own > 0 {
            let base_ai = match &base {
                Some(b) => {
                    let mut any = false;
                    for c in b {
                        if !(1..=4).contains(&Self::port_of(c)?) {
                            any = true;
                        }
                    }
                    Some(any)
                }
                None => None,
            };
            if mode == "joyride" {
                self.warnings.push(format!("{wh}/starts: an AI car in Joyride: retail Joyride has none (the AI isn't made for free roam)"));
            } else if base_ai == Some(false) {
                self.warnings.push(format!(
                    "{wh}/starts: an AI car in a world whose retail level has none: the AI only knows the roles and routes retail gives it"
                ));
            } else {
                self.warnings.push(format!(
                    "{wh}/starts: an AI car of the level's own: keep it on the routes retail AI drives (near a retail AI start), or move a retail AI start instead"
                ));
            }
        }
        if let Some(b) = &base {
            let n = b.len() + own.len();
            let mut humans = 0;
            for c in b {
                if (1..=4).contains(&Self::port_of(c)?) {
                    humans += 1;
                }
            }
            let humans = humans + own.len() - ai_own;
            if mode == "drag" && n != 4 {
                self.warnings.push(format!("{wh}/starts: a drag race has 4 lanes: 4 starts (it has {n})"));
            }
            if mode == "battle" && n < 2 {
                self.warnings.push(format!("{wh}/starts: a battle needs 2 starts at least"));
            }
            if humans == 0 {
                self.err(&format!("{wh}/starts"), "no player start (port 1)");
            }
        }
        if mode != "battle" && mode != "drag" && dget(lv, "rules")?.is_some_and(truthy) {
            self.warnings.push(format!("{wh}/rules: the round rules are for battles and drag races"));
        }
        Ok(())
    }

    // -- links --------------------------------------------------------------------------------

    pub fn resolve(&mut self, sl: &str, wh: &str, archive: &str) -> Result<Value> {
        if let Mode::Pack(names) = &self.mode {
            if let Some(n) = names.get(sl) {
                return Ok(json!(n));
            }
            if let Some(c) = rx!(r"^base://([A-Za-z0-9_]+)/world_0/(.+)$").captures(sl) {
                if c[1].to_lowercase() != archive.to_lowercase() {
                    return Err(lerr(format!("{wh}: {sl}: not the level's world ({archive})")));
                }
                if let Some(b) = &self.base
                    && let Some(why) = b.check(&c[2])?
                {
                    return Err(lerr(format!("{wh}: {sl}: {why}")));
                }
                return Ok(json!(sl));
            }
            let mut best: Option<&String> = None;
            for k in names.keys() {
                if sl.starts_with(&format!("{k}/")) && best.is_none_or(|b| k.len() > b.len()) {
                    best = Some(k);
                }
            }
            if let Some(b) = best {
                return Ok(json!(format!("{}{}", names[b], &sl[b.len()..])));
            }
            return Err(lerr(format!("{wh}: {sl}: the level has nothing of that name")));
        }
        if let Some(&(ai, j)) = self.append_of.get(sl) {
            return Ok(json!({"append": ai, "elem": j, "add": 0}));
        }
        if self.part_ids.contains(sl) && !sl.starts_with("level://_") {
            return Ok(json!({"part": sl}));
        }
        if let Some(c) = rx!(r"^(level://[a-z]+/[^/]+(?:/to)?)(?:/o([0-9a-fA-F]+))?$").captures(sl) {
            let base = c[1].to_string();
            let add = c.get(2).map(|m| u64::from_str_radix(m.as_str(), 16).unwrap_or(u64::MAX)).unwrap_or(0);
            let bs: Vec<&str> = base.split('/').collect();
            let kind = bs[2];
            if PACK_ONLY.contains(&kind) {
                let sec = or_empty(dget(&self.lv, kind)?);
                if !dget(&sec, bs[3])?.is_some_and(Value::is_object) {
                    return Err(lerr(format!("{wh}: {sl}: the level has no such {}", &kind[..kind.len() - 1])));
                }
                if let Some(&(ai, j)) = self.append_of.get(&base) {
                    return Ok(json!({"append": ai, "elem": j, "add": add}));
                }
                if !self.part_ids.contains("level://_null") {
                    self.add_part("level://_null", "data", vec![0; 0x70], BTreeMap::new())?;
                }
                let key = format!("pack-only {sl}");
                if !self.pack_only.contains(&key) {
                    self.pack_only.insert(key);
                    self.warnings.push(format!("{wh}: {sl} is in the level's world pack (a test drive), not the editor's preview"));
                }
                return Ok(json!({"part": "level://_null"}));
            }
            if (kind == "timers" || kind == "variables") && !base.ends_with("/to") {
                if !self.part_ids.contains(&format!("{base}/to")) {
                    return Err(lerr(format!("{wh}: {sl}: the level has no such {}", &kind[..kind.len() - 1])));
                }
                return Ok(json!({"slot": format!("{base}/to")}));
            }
            if !self.part_ids.contains(&base) {
                return Err(lerr(format!("{wh}: {sl}: the level has nothing of that name")));
            }
            return Ok(json!({"part": base, "add": add}));
        }
        if let Some(c) = rx!(r"^base://([A-Za-z0-9_]+)/world_0/(.+)$").captures(sl) {
            if c[1].to_lowercase() != archive.to_lowercase() {
                return Err(lerr(format!("{wh}: {sl}: not the level's world ({archive})")));
            }
            if let Some(b) = &self.base
                && let Some(why) = b.check(&c[2])?
            {
                return Err(lerr(format!("{wh}: {sl}: {why}")));
            }
            return base_steps(self.env, &c[2], wh);
        }
        Err(lerr(format!("{wh}: {sl}: a link is base://{archive}/world_0/..., level://... or a part")))
    }

    // -- records the port appends to the base world's arrays ----------------------------------

    fn group(&mut self, groups: &mut HashMap<(String, String), usize>, rec_path: &str, rec_struct: &str, field: &str, w: &str) -> Result<usize> {
        let key = (rec_path.to_string(), field.to_string());
        if let Some(&g) = groups.get(&key) {
            return Ok(g);
        }
        let (props, _) = self.env.struct_of(rec_struct)?.ok_or_else(|| pyerr("TypeError", "no schema"))?;
        let p = props.get(field).ok_or_else(|| pyerr("KeyError", py::str_repr(field)))?;
        let ln = link_of(p).ok_or_else(|| pyerr("TypeError", "'NoneType' object is not subscriptable"))?;
        let cnt = props.get(&s(&ln["count"])).ok_or_else(|| pyerr("KeyError", py::repr(&ln["count"])))?;
        let g = self.appends.len();
        self.appends.push(json!({
            "record": base_steps(self.env, rec_path, w)?,
            "pointer": off_of(p)?,
            "count": off_of(cnt)?,
            "elemSize": ln["item-size"].clone(),
            "what": format!("{rec_path}/{field}"),
            "elems": [],
        }));
        groups.insert(key, g);
        Ok(g)
    }

    fn put(&mut self, kind: &str, name: &str, ai: usize, el: Value) {
        let elems = self.appends[ai]["elems"].as_array_mut().unwrap();
        self.append_of.insert(format!("level://{kind}/{name}"), (ai, elems.len()));
        elems.push(el);
    }

    fn sets(&self, sname: &str, fields: &Map<String, Value>, w: &str) -> Result<Vec<Value>> {
        let (props, _) = self.env.struct_of(sname)?.ok_or_else(|| pyerr("TypeError", "no schema"))?;
        let mut out = vec![];
        for (k, v) in fields {
            if k == "pos" {
                let p = props.get("pos").ok_or_else(|| pyerr("KeyError", "'pos'"))?;
                let a = v.as_array().ok_or_else(|| pyerr("TypeError", format!("'{}' object is not iterable", py::type_name(v))))?;
                let fl: Vec<f64> = a.iter().map(py_float).collect::<Result<_>>()?;
                if fl.len() != 3 {
                    return Err(pyerr("struct.error", format!("pack expected 3 items for packing (got {})", fl.len())));
                }
                let mut b = vec![];
                for x in fl {
                    b.extend(f32b(x, false)?);
                }
                out.push(json!([off_of(p)?, graph::hexstr(&b)]));
                continue;
            }
            let Some(p) = props.get(k).filter(|p| p.get("x-offset").is_some() && link_of(p).is_none() && p.get("$ref").is_none()) else {
                return Err(lerr(format!("{w}/{k}: not a number field of {sname}")));
            };
            out.push(json!([off_of(p)?, graph::hexstr(&scalar(&self.env.t, p, v, &format!("{w}/{k}"), false)?)]));
        }
        Ok(out)
    }

    /// starts, items, effect points and objectives as elements the port appends to the base
    /// world's arrays (Port_Level.c "appends").
    fn build_appends(&mut self, lv: &Value, wh: &str) -> Result<()> {
        self.appends.clear();
        self.append_of.clear();
        if !matches!(self.mode, Mode::Memory) {
            return Ok(());
        }
        let mut groups: HashMap<(String, String), usize> = HashMap::new();
        for (n, st) in ditems(&or_empty(dget(lv, "starts")?))? {
            let w = format!("{wh}/starts/{n}");
            let mut f = Map::new();
            for (k, v) in ditems(&st)? {
                if k != "pos" {
                    f.insert(k, v);
                }
            }
            f.entry("rotY").or_insert(json!(0.0));
            f.entry("port").or_insert(json!(0));
            f.insert("pos".into(), st.get("pos").cloned().ok_or_else(|| pyerr("KeyError", "'pos'"))?);
            let g = self.group(&mut groups, "World/0", "World", "tileCars", &w)?;
            let set = self.sets("TileCar", &f, &w)?;
            self.put("starts", &n, g, json!({"fromPort": 1, "set": set}));
        }
        for (n, it) in ditems(&or_empty(dget(lv, "items")?))? {
            let w = format!("{wh}/items/{n}");
            let q = s(it.get("quad").ok_or_else(|| pyerr("KeyError", "'quad'"))?);
            if let Some(b) = &self.base
                && !b.count(&format!("World/0/quads/{q}/contents/items"))?.is_some_and(|c| c > 0)
            {
                return Err(lerr(format!("{w}/quad: quadrant {q} has no items of its own to copy")));
            }
            let g = self.group(&mut groups, &format!("World/0/quads/{q}/contents"), "QuadContents", "items", &w)?;
            let mut f = Map::new();
            f.insert("pos".into(), it.get("pos").cloned().ok_or_else(|| pyerr("KeyError", "'pos'"))?);
            f.insert("mode".into(), json!("on"));
            f.insert("id".into(), crate::decode::int_value(py_int(it.get("id").unwrap_or(&json!(0)))?));
            f.insert("respawn".into(), crate::decode::int_value(py_int(it.get("respawn").unwrap_or(&json!(0)))?));
            let mut set = self.sets("QuadItem", &f, &w)?;
            set.push(json!([0x0C, graph::hexstr(&f32b(0.0, false)?)]));
            self.put("items", &n, g, json!({"fromIndex": 0, "set": set}));
        }
        for (n, fx) in ditems(&or_empty(dget(lv, "fxPoints")?))? {
            let w = format!("{wh}/fxPoints/{n}");
            let tmpl = s(fx.get("template").ok_or_else(|| pyerr("KeyError", "'template'"))?);
            let Some(c) = rx!(r"^base://[A-Za-z0-9_]+/world_0/(World/0/quads/\d+/contents/fxPoints/\d+)$").captures(&tmpl) else {
                return Err(lerr(format!("{w}/template: a base effect point (World/0/quads/Q/contents/fxPoints/N)")));
            };
            let from_path = c[1].to_string();
            let q = s(fx.get("quad").ok_or_else(|| pyerr("KeyError", "'quad'"))?);
            let g = self.group(&mut groups, &format!("World/0/quads/{q}/contents"), "QuadContents", "fxPoints", &w)?;
            let mut f = Map::new();
            f.insert("pos".into(), fx.get("pos").cloned().ok_or_else(|| pyerr("KeyError", "'pos'"))?);
            if let Some(m) = fx.get("mode") {
                f.insert("mode".into(), m.clone());
            }
            let from = base_steps(self.env, &from_path, &w)?;
            let set = self.sets("QuadFxPoint", &f, &w)?;
            self.put("fxPoints", &n, g, json!({"from": from, "set": set}));
        }
        for (n, ob) in ditems(&or_empty(dget(lv, "objectives")?))? {
            let w = format!("{wh}/objectives/{n}");
            let kind = match ob.get("kind").and_then(Value::as_str).unwrap_or("none") {
                "none" => 0u8,
                "counter" => 1,
                "timer" => 2,
                k => return Err(pyerr("KeyError", py::str_repr(k))),
            };
            let hud = (py_int(ob.get("hud").unwrap_or(&json!(0)))? & 0xFF) as u8;
            let mut info = vec![kind, if ob.get("shown").map(truthy).unwrap_or(true) { 0x17 } else { 0 }, hud];
            info.extend([0u8; 25]);
            let mut links = BTreeMap::new();
            for (off, k) in [(0x0C, "done"), (0x10, "value"), (0x14, "text"), (0x18, "doneText")] {
                if let Some(x) = ob.get(k).and_then(Value::as_str) {
                    links.insert(off, x.to_string());
                }
            }
            self.add_part(&format!("level://objectives/{n}/info"), "data", info, links)?;
            let mut el = packi('I', py_int(ob.get("arg").unwrap_or(&json!(0)))?, false)?;
            el.extend([0u8; 4]);
            self.add_part(&format!("level://objectives/{n}"), "element", el, BTreeMap::from([(4, format!("level://objectives/{n}/info"))]))?;
            let g = self.group(&mut groups, "World/0", "World", "objectives", &w)?;
            self.put("objectives", &n, g, json!({"part": format!("level://objectives/{n}")}));
        }
        Ok(())
    }

    fn catch(&mut self, r: Result<()>) -> Result<()> {
        match r {
            Ok(()) => Ok(()),
            Err(e) if e.kind == Kind::Level || e.kind == Kind::Tac => {
                self.errors.push(e.msg);
                Ok(())
            }
            Err(e) => Err(e),
        }
    }

    // -- the whole --------------------------------------------------------------------------------

    /// The level file, or None with self.errors.
    pub fn run(&mut self) -> Result<Option<Value>> {
        let lv = read_jsonc(&self.proj.join("level.jsonc"))?;
        self.lv = lv.clone();
        let wh = "level.jsonc";
        for k in ["id", "name", "mode", "extends", "world"] {
            if !dget(&lv, k)?.is_some_and(Value::is_string) {
                self.err(wh, format!("{k}: missing (a string)"));
            }
        }
        if !self.errors.is_empty() {
            return Ok(None);
        }
        if !py::eq(lv.get("format_version").unwrap_or(&Value::Null), &json!(1)) {
            self.err(wh, "format_version: 1");
        }
        let mode = s(&lv["mode"]);
        let ext = s(&lv["extends"]);
        let m = rx!(r"^base://mcp/track/(\d+)$").captures(&ext).map(|c| c[1].parse::<u128>().unwrap_or(u128::MAX));
        match MODES.iter().find(|x| x.0 == mode) {
            None => {
                let names: Vec<&str> = MODES.iter().map(|x| x.0).collect();
                self.err(wh, format!("mode: one of {}", names.join(", ")));
            }
            Some(&(_, a, b)) => {
                if !m.is_some_and(|n| n >= a as u128 && n < b as u128) {
                    self.err(wh, format!("extends: base://mcp/track/N with N a {mode} level's ({a}..{})", b - 1));
                }
            }
        }
        if !rx!(r"^uid://[0-9a-z]{1,13}$").is_match(&s(&lv["id"])) {
            self.err(wh, "id: uid:// and 1 to 13 of 0-9 a-z");
        }
        let world = s(&lv["world"]).replace('/', "\\");
        let archive = world.rsplit('\\').next().unwrap_or("").to_lowercase();
        let mut keep = lv.get("baseTriggers").cloned().unwrap_or(json!("all"));
        let keep_ok = keep.as_str() == Some("all") || keep.as_str() == Some("none") || keep.as_array().is_some_and(|a| a.iter().all(is_int));
        if !keep_ok {
            self.err(wh, "baseTriggers: \"all\", \"none\" or a list of trigger numbers");
            keep = json!("none");
        }
        if let Some(b) = &self.base {
            if b.archive.to_lowercase() != archive {
                let a = b.archive.clone();
                self.err(wh, format!("world: {world} isn't the base given ({a})"));
            } else if let Some(a) = keep.as_array() {
                let n = b.num_triggers() as i128;
                for x in a.clone() {
                    let v = py_int(&x)?;
                    if !(0 <= v && v < n) {
                        self.err(wh, format!("baseTriggers: the world has no trigger {} ({n})", s(&x)));
                    }
                }
            }
        }
        // own records
        for kind in ["volumes", "timers", "variables", "strings"] {
            let sec = or_empty(dget(&lv, kind)?);
            let Some(m) = sec.as_object() else {
                self.err(&format!("{wh}/{kind}"), "an object of named records");
                continue;
            };
            for (name, v) in m.clone() {
                if !rx!(r"^[A-Za-z0-9_-]+$").is_match(&name) {
                    self.err(&format!("{wh}/{kind}/{name}"), "names are letters, digits, _ and -");
                    continue;
                }
                let w = format!("{wh}/{kind}/{name}");
                let r = match kind {
                    "volumes" => self.volume(&name, &v, &w),
                    "timers" => self.timer(&name, &v, &w),
                    "variables" => self.variable(&name, &v, &w),
                    _ => self.string(&name, &v, &w),
                };
                self.catch(r)?;
            }
        }
        // starts and items (they go into the world pack: xdl_level_pack)
        for (n, st) in ditems(&or_empty(dget(&lv, "starts")?))? {
            let w = format!("{wh}/starts/{n}");
            let pos_ok = st.is_object() && st.get("pos").and_then(Value::as_array).is_some_and(|a| a.len() == 3);
            if !pos_ok {
                self.err(&w, "pos [x, y, z]");
            } else if !is_num(st.get("rotY").unwrap_or(&json!(0))) || !is_int(st.get("port").unwrap_or(&json!(0))) {
                self.err(&w, "rotY (radians) and port (1-4 a player, 0 the AI's)");
            }
        }
        self.mode_checks(&lv, &mode, wh)?;
        for (n, it) in ditems(&or_empty(dget(&lv, "items")?))? {
            let w = format!("{wh}/items/{n}");
            let pos_ok = it.is_object() && it.get("pos").and_then(Value::as_array).is_some_and(|a| a.len() == 3);
            if !pos_ok {
                self.err(&w, "pos [x, y, z]");
            } else if !it.get("quad").is_some_and(is_int) || !is_int(it.get("id").unwrap_or(&json!(0))) {
                self.err(&w, "quad (the quadrant it is in) and id (the world's item pack's)");
            }
        }
        for (n, fx) in ditems(&or_empty(dget(&lv, "fxPoints")?))? {
            let w = format!("{wh}/fxPoints/{n}");
            let ok = fx.is_object()
                && fx.get("pos").and_then(Value::as_array).is_some_and(|a| a.len() == 3)
                && fx.get("quad").is_some_and(is_int)
                && fx.get("template").is_some_and(Value::is_string);
            if !ok {
                self.err(&w, "pos [x, y, z], quad and template (a base effect point to copy)");
            } else {
                let r = self.resolve(fx["template"].as_str().unwrap(), &format!("{w}/template"), &archive).map(|_| ());
                self.catch_level(r)?;
            }
        }
        for (n, ob) in ditems(&or_empty(dget(&lv, "objectives")?))? {
            let w = format!("{wh}/objectives/{n}");
            if !ob.is_object() {
                self.err(&w, "an object");
                continue;
            }
            let kind = ob.get("kind").cloned().unwrap_or(json!("none"));
            if !["none", "counter", "timer"].iter().any(|k| kind.as_str() == Some(k)) {
                self.err(&format!("{w}/kind"), "none, counter (a variable's value) or timer (mm:ss)");
            }
            for k in ["text", "doneText", "done", "value"] {
                match ob.get(k) {
                    Some(Value::Null) | None => {}
                    Some(Value::String(x)) => {
                        let r = self.resolve(x, &format!("{w}/{k}"), &archive).map(|_| ());
                        self.catch_level(r)?;
                    }
                    Some(_) => self.err(&format!("{w}/{k}"), "a link or null"),
                }
            }
        }
        if self.errors.is_empty() {
            let r = self.build_appends(&lv, wh);
            self.catch(r)?;
        }
        // triggers
        let tdir = self.proj.join("triggers");
        let names: Vec<Value> = match lv.get("triggers") {
            Some(t) if truthy(t) => match t {
                Value::Array(a) => a.clone(),
                Value::Object(m) => m.keys().map(|k| json!(k)).collect(),
                Value::String(x) => x.chars().map(|c| json!(c.to_string())).collect(),
                _ => return Err(pyerr("TypeError", format!("'{}' object is not iterable", py::type_name(t)))),
            },
            _ => trigger_stems(&tdir).into_iter().map(Value::String).collect(),
        };
        let mut docs: Vec<(String, Value)> = vec![];
        for name in names {
            let name = s(&name);
            // the name is a file stem: never a path out of triggers/
            if let Err(e) = check_trigger_name(&name) {
                self.err(wh, format!("triggers: {e}"));
                continue;
            }
            let f = tdir.join(format!("{name}.jsonc"));
            if !f.exists() {
                self.err(wh, format!("triggers: {name}: no triggers/{name}.jsonc"));
                continue;
            }
            match read_jsonc(&f) {
                Ok(d) => docs.push((name, d)),
                Err(e) if e.kind == Kind::Level => self.errors.push(e.msg),
                Err(e) => return Err(e),
            }
        }
        match generated_triggers(&lv, &format!("{wh}/rules")) {
            Ok(g) => docs.extend(g),
            Err(e) if e.kind == Kind::Level => self.errors.push(e.msg),
            Err(e) => return Err(e),
        }
        if self.skip_intro {
            docs = skip_intro(docs)?;
        }
        for (name, d) in docs {
            let r = self.trigger(&name, &d, &format!("triggers/{name}.jsonc"));
            self.catch_level(r)?;
        }
        // links
        let mut i = 0;
        while i < self.parts.len() {
            let links: Vec<(u32, String)> = self.parts[i].links.iter().map(|(o, l)| (*o, l.clone())).collect();
            let pid = self.parts[i].id.clone();
            let mut out = vec![];
            for (off, l) in links {
                match self.resolve(&l, &format!("{pid} +0x{off:x}"), &archive) {
                    Ok(v) => out.push((off, v)),
                    Err(e) if e.kind == Kind::Level => self.errors.push(e.msg),
                    Err(e) => return Err(e),
                }
            }
            self.parts[i].resolved = out;
            i += 1;
        }
        // patches: {base path: {field: value}}
        for (path, fields) in ditems(&or_empty(dget(&lv, "patches")?))? {
            let pw = format!("{wh}/patches/{path}");
            let mm = rx!(r"^base://([A-Za-z0-9_]+)/world_0/(.+)$").captures(&path);
            let Some(mm) = mm.filter(|c| c[1].to_lowercase() == archive) else {
                self.err(&pw, format!("patches name base://{archive}/world_0/... records"));
                continue;
            };
            let rel = mm[2].to_string();
            let Some(fm) = fields.as_object() else {
                self.err(&pw, "an object of fields");
                continue;
            };
            for (field, v) in fm {
                let r = (|| -> Result<Value> {
                    if let Some(b) = &self.base
                        && let Some(why) = b.check(&rel)?
                    {
                        return Err(lerr(format!("{pw}: {why}")));
                    }
                    let (link, off, p) = field_target(self.env, &rel, field, &pw)?;
                    let b = scalar(&self.env.t, &p, v, &format!("{pw}/{field}"), false)?;
                    Ok(json!({"target": link, "offset": off, "hex": graph::hexstr(&b), "field": field, "path": rel}))
                })();
                match r {
                    Ok(p) => self.patches.push(p),
                    Err(e) if e.kind == Kind::Level || e.kind == Kind::Tac => self.errors.push(e.msg),
                    Err(e) => return Err(e),
                }
            }
        }
        // limits
        let kept = match &self.base {
            Some(b) => b.trigger_types(&keep)?,
            None => IndexMap::new(),
        };
        let names_of: HashMap<String, i128> = self.env.t.sch["properties"]["type"]
            .get("x-enum")
            .and_then(Value::as_object)
            .map(|m| m.iter().map(|(k, v)| (py::repr(v), k.parse::<i128>().unwrap_or(0))).collect())
            .unwrap_or_default();
        let mut all: IndexMap<String, Value> = IndexMap::new();
        for (k, (t, _)) in self.trigger_types.iter().chain(kept.iter()) {
            all.entry(k.clone()).or_insert(t.clone());
        }
        for (k, t) in all {
            let n = self.trigger_types.get(&k).map(|x| x.1).unwrap_or(0) + kept.get(&k).map(|x| x.1).unwrap_or(0);
            let idx = names_of.get(&k).copied().or(if t.is_i64() || t.is_u64() || t.is_boolean() { py_int(&t).ok() } else { None });
            if let Some(i) = idx {
                let len = TRIGGER_MAX.len() as i128;
                let j = if i < 0 { i + len } else { i };
                if i < len && (0..len).contains(&j) && n > TRIGGER_MAX[j as usize] {
                    self.err("triggers", format!("{n} {} triggers: the game keeps {} at once", s(&t), TRIGGER_MAX[j as usize]));
                }
            }
        }
        // starts: the cars' box clear of each other
        if let (Some(bx), Some(b)) = (&self.spawn_box, &self.base) {
            let w = start_overlaps(b, &or_empty(dget(&lv, "patches")?), &archive, bx, lv.get("starts"))?;
            self.warnings.extend(w);
        }
        if !self.errors.is_empty() {
            return Ok(None);
        }
        let parts: Vec<Value> = self
            .parts
            .iter()
            .map(|p| {
                json!({"id": p.id, "kind": p.kind, "hex": graph::hexstr(&p.data),
                       "links": p.resolved.iter().map(|(o, v)| json!([o, v])).collect::<Vec<_>>()})
            })
            .collect();
        let mut out = Map::new();
        out.insert("format".into(), json!(FORMAT));
        out.insert("format_version".into(), json!(1));
        for k in ["id", "name", "mode", "extends"] {
            out.insert(k.into(), lv[k].clone());
        }
        out.insert("world".into(), json!(world));
        out.insert("baseTriggers".into(), keep);
        out.insert("parts".into(), Value::Array(parts));
        out.insert("patches".into(), Value::Array(self.patches.clone()));
        if !self.appends.is_empty() {
            out.insert("appends".into(), Value::Array(self.appends.clone()));
        }
        Ok(Some(Value::Object(out)))
    }

    /// `except LevelError` (a TacError goes through, as in the Python's loops that catch only
    /// LevelError)
    fn catch_level(&mut self, r: Result<()>) -> Result<()> {
        match r {
            Ok(()) => Ok(()),
            Err(e) if e.kind == Kind::Level => {
                self.errors.push(e.msg);
                Ok(())
            }
            Err(e) => Err(e),
        }
    }
}

fn splice(buf: &mut Vec<u8>, o: usize, b: &[u8]) {
    let lo = o.min(buf.len());
    let hi = (o + b.len()).min(buf.len()).max(lo);
    if o > buf.len() {
        // a slice assignment past the end appends
        buf.extend_from_slice(b);
        return;
    }
    buf.splice(lo..hi, b.iter().copied());
}

/// A trigger's name is its file's stem (triggers/NAME.jsonc): letters, digits, '_' and '-'
/// only, so a name from level.jsonc never reaches outside triggers/ (no '/', '\', '..', ':',
/// nothing empty or absolute). Shared with the browser's level editor.
pub fn check_trigger_name(name: &str) -> std::result::Result<(), String> {
    if !name.is_empty() && name.bytes().all(|b| b.is_ascii_alphanumeric() || b == b'_' || b == b'-') {
        Ok(())
    } else {
        Err(format!("{}: not a trigger name (letters, digits, '_' and '-' only)", py::str_repr(name)))
    }
}

/// sorted(p.stem for p in tdir.glob("*.jsonc"))
pub fn trigger_stems(tdir: &Path) -> Vec<String> {
    let mut v: Vec<String> = std::fs::read_dir(tdir)
        .map(|rd| {
            rd.flatten()
                .filter_map(|e| {
                    let n = e.file_name().to_string_lossy().into_owned();
                    // pathlib's glob matches hidden files too
                    n.strip_suffix(".jsonc").map(str::to_string)
                })
                .collect()
        })
        .unwrap_or_default();
    v.sort();
    v
}

// ------------------------------------------------------------------------------------------

/// the multi-round modes' rules (GameRules.c): code variables the level sets as it starts
const RULE_VARS: [(&str, u32); 8] =
    [("on", 0x12C), ("mode", 0x12D), ("time", 0x12E), ("kills", 0x12F), ("points", 0x130), ("rounds", 0x131), ("countWins", 0x132), ("jettison", 0x135)];
const RULE_MODES: [(&str, i64); 3] = [("points", 1), ("kills", 2), ("none", 3)];

/// Triggers the level's settings make: "rules" as a level_start trigger setting the game's
/// rule variables (and the round timer).
pub fn generated_triggers(lv: &Value, wh: &str) -> Result<Vec<(String, Value)>> {
    let r = dget(lv, "rules")?.cloned().unwrap_or(Value::Null);
    if !truthy(&r) {
        return Ok(vec![]);
    }
    let Some(rm) = r.as_object() else {
        return Err(lerr(format!("{wh}: an object (mode, kills, points, rounds, time, countWins, jettison)")));
    };
    let mut vals: IndexMap<String, Value> = IndexMap::new();
    vals.insert("on".into(), json!(1));
    for (k, v) in rm {
        if k == "roundTimer" {
            continue;
        }
        if !RULE_VARS.iter().any(|x| x.0 == k) {
            let names: Vec<&str> = RULE_VARS.iter().map(|x| x.0).filter(|k| *k != "on").collect();
            return Err(lerr(format!("{wh}/{k}: not a rule ({}, roundTimer)", names.join(", "))));
        }
        let v = if k == "mode" {
            match RULE_MODES.iter().find(|x| v.as_str() == Some(x.0)) {
                Some(x) => json!(x.1),
                None => return Err(lerr(format!("{wh}/mode: points, kills or none"))),
            }
        } else if let Value::Bool(b) = v {
            json!(*b as i64)
        } else if !(v.is_i64() || v.is_u64()) || matches!(Num::of(v), Some(Num::Int(i)) if i < 0) {
            return Err(lerr(format!("{wh}/{k}: a whole number")));
        } else {
            v.clone()
        };
        vals.insert(k.clone(), v);
    }
    let mut acts: Vec<Value> = vals
        .iter()
        .map(|(k, v)| {
            let var = RULE_VARS.iter().find(|x| x.0 == k).unwrap().1;
            json!({"type": "code_variable_modify_value", "data": {"op": "set", "var": {"var": var, "arg": 0}, "value": {"const": v}}})
        })
        .collect();
    if let Some(Value::String(rt)) = rm.get("roundTimer")
        && let Some(t) = vals.get("time")
    {
        acts.push(json!({"type": "timer_change_value", "data": {"op": "set", "timers": [rt], "value": {"const": t}}}));
    }
    Ok(vec![("_rules".into(), json!({"type": "level_start", "actions": acts}))])
}

/// The test drive's quick start: the intro screen a level_start trigger plays is left out, the
/// interface_end trigger waiting for it runs at the start instead, and the start's actions run
/// without their delays.
pub fn skip_intro(docs: Vec<(String, Value)>) -> Result<Vec<(String, Value)>> {
    let mut docs = docs;
    let mut waits: HashMap<String, String> = HashMap::new();
    for (n, d) in &docs {
        if dget(d, "type")?.and_then(Value::as_str) == Some("interface_end")
            && let Some(p) = d.get("param").filter(|p| p.is_object())
        {
            waits.insert(py::repr(p.get("interface").unwrap_or(&Value::Null)), n.clone());
        }
    }
    let mut played: HashSet<String> = HashSet::new();
    for (_, d) in docs.iter_mut() {
        if dget(d, "type")?.and_then(Value::as_str) != Some("level_start") {
            continue;
        }
        let acts = match d.get("actions") {
            Some(a) if truthy(a) => a.as_array().cloned().ok_or_else(|| pyerr("TypeError", "actions isn't a list"))?,
            _ => vec![],
        };
        let mut keep = vec![];
        for a in acts {
            if a.is_object() {
                let data = or_empty(a.get("data"));
                let itf = dget(&data, "interface")?.cloned().unwrap_or(Value::Null);
                if itf.is_object() || itf.is_array() {
                    return Err(pyerr("TypeError", format!("unhashable type: '{}'", py::type_name(&itf))));
                }
                if a.get("type").and_then(Value::as_str) == Some("interface_play")
                    && let Some(w) = waits.get(&py::repr(&itf))
                {
                    played.insert(w.clone());
                    continue;
                }
            }
            keep.push(a);
        }
        d.as_object_mut().unwrap().insert("actions".into(), Value::Array(keep));
    }
    for (n, d) in docs.iter_mut() {
        let Some(m) = d.as_object_mut() else { continue };
        if played.contains(n) {
            m.insert("type".into(), json!("level_start"));
            m.shift_remove("param");
            m.shift_remove("paramWords");
        }
        if m.get("type").and_then(Value::as_str) == Some("level_start")
            && let Some(Value::Array(a)) = m.get_mut("actions")
        {
            for x in a.iter_mut() {
                if let Value::Object(am) = x {
                    am.shift_remove("delayInit");
                    am.shift_remove("delay");
                }
            }
        }
    }
    let mut out = vec![];
    for (n, d) in docs {
        let keep = d.get("actions").is_some_and(truthy) || d.get("type").and_then(Value::as_str) != Some("level_start");
        if keep {
            out.push((n, d));
        }
    }
    Ok(out)
}

// ------------------------------------------------------------------------------------------
// cache keys

/// this crate's sources, hashed when it was built (the Python hashes its own tools)
pub const TOOL_ID: &str = concat!("xdlrs ", env!("CARGO_PKG_VERSION"), " ", env!("XDLRS_SRC_HASH"));

thread_local! {
    static SCHEMA_HASH: RefCell<HashMap<PathBuf, String>> = RefCell::new(HashMap::new());
}

/// A hash of the tools (this crate) and every schemas/*.json, once per process.
pub fn tools_hash(root: &Path) -> String {
    SCHEMA_HASH.with(|c| {
        if let Some(h) = c.borrow().get(root) {
            return h.clone();
        }
        let mut h = Sha256::new();
        h.update(TOOL_ID.as_bytes());
        h.update(b"\0");
        let mut files: Vec<PathBuf> = std::fs::read_dir(root.join("schemas"))
            .map(|rd| rd.flatten().map(|e| e.path()).filter(|p| p.extension().is_some_and(|x| x == "json")).collect())
            .unwrap_or_default();
        files.sort();
        for f in files {
            h.update(f.file_name().unwrap().to_string_lossy().as_bytes());
            h.update(b"\0");
            match std::fs::read(&f) {
                Ok(b) => h.update(&b),
                Err(_) => h.update(b"-"),
            }
            h.update(b"\0");
        }
        let x = hex(&h.finalize());
        c.borrow_mut().insert(root.to_path_buf(), x.clone());
        x
    })
}

fn hex(b: &[u8]) -> String {
    graph::hexstr(b)
}

/// hwvx-x as a key: its size and modification time
pub fn exe_stamp(exe: Option<&Path>) -> String {
    let Some(m) = exe.and_then(|e| std::fs::metadata(e).ok()) else { return "none".into() };
    let ns = m.modified().ok().and_then(|t| t.duration_since(std::time::UNIX_EPOCH).ok()).map(|d| d.as_nanos()).unwrap_or(0);
    format!("{}:{ns}", m.len())
}

/// A hash of what a compile reads: the project's files, the tools and schemas, the options.
pub fn source_hash(env: &Env, proj: &Path, options: &str) -> String {
    let mut h = Sha256::new();
    h.update(options.as_bytes());
    let mut files = vec![proj.join("level.jsonc")];
    files.extend(trigger_stems(&proj.join("triggers")).into_iter().map(|n| proj.join("triggers").join(format!("{n}.jsonc"))));
    h.update(tools_hash(env.root()).as_bytes());
    for f in files {
        h.update(f.file_name().unwrap().to_string_lossy().as_bytes());
        h.update(b"\0");
        match std::fs::read(&f) {
            Ok(b) => h.update(&b),
            Err(_) => h.update(b"-"),
        }
        h.update(b"\0");
    }
    hex(&h.finalize())
}

/// What the build depends on of the base: its extracted project's folder name carries the
/// archive's hash; "" without one.
pub fn base_identity(base: Option<&Path>) -> String {
    base.and_then(|b| b.parent()).and_then(|p| p.file_name()).map(|n| n.to_string_lossy().into_owned()).unwrap_or_default()
}

pub fn write_json(path: &Path, doc: &Value) -> Result<()> {
    if let Some(d) = path.parent() {
        std::fs::create_dir_all(d).map_err(|e| Error::io(format!("{}: {e}", d.display())))?;
    }
    let tmp = PathBuf::from(format!("{}.part", path.display()));
    std::fs::write(&tmp, format!("{}\n", py::dumps_indent(doc, 1, false))).map_err(|e| Error::io(format!("{}: {e}", tmp.display())))?;
    std::fs::rename(&tmp, path).map_err(|e| Error::io(format!("{}: {e}", path.display())))
}

/// (doc, errors, warnings)
pub fn compile_project_full(env: &Env, proj: &Path, base: Option<&Path>, skip: bool, spawn_box: Option<SpawnBox>) -> Result<(Option<Value>, Vec<String>, Vec<String>)> {
    let mut c = Compiler::new(env, proj, base, skip, spawn_box)?;
    let doc = match c.run() {
        Ok(d) => d,
        Err(e) if e.kind == Kind::Level => {
            c.errors.push(e.msg);
            None
        }
        Err(e) => return Err(e),
    };
    Ok((doc, c.errors, c.warnings))
}

pub fn compile_project(env: &Env, proj: &Path, base: Option<&Path>, skip: bool) -> Result<(Option<Value>, Vec<String>)> {
    let (d, e, _) = compile_project_full(env, proj, base, skip, None)?;
    Ok((d, e))
}

/// (doc, errors, cached): the level file at out, compiled again only when a source changed.
pub fn compile_cached(env: &Env, proj: &Path, out: &Path, base: Option<&Path>, skip: bool) -> Result<(Option<Value>, Vec<String>, bool)> {
    let key = source_hash(env, proj, &format!("skip_intro={};base={}", skip as u8, base_identity(base)));
    if let Ok(t) = std::fs::read_to_string(out)
        && let Ok(old) = jsonc::parse(&t)
        && old.get("sourceHash").and_then(Value::as_str) == Some(key.as_str())
    {
        return Ok((Some(old), vec![], true));
    }
    let (doc, errors) = compile_project(env, proj, base, skip)?;
    let doc = doc.map(|mut d| {
        d.as_object_mut().unwrap().insert("sourceHash".into(), json!(key));
        d
    });
    if let Some(d) = &doc {
        write_json(out, d)?;
    }
    Ok((doc, errors, false))
}

/// One problem of the project, where it is (the editor's list).
#[derive(Debug, Clone, PartialEq, Eq, Default)]
pub struct Record {
    /// level.jsonc or triggers/NAME.jsonc ("" when it isn't one place)
    pub file: String,
    pub path: String,
    pub message: String,
}

impl Record {
    pub fn to_json(&self) -> Value {
        json!({"file": self.file, "path": self.path, "message": self.message})
    }
}

/// error_records: the file, place and message of each error text.
pub fn error_records(errors: &[String]) -> Vec<Record> {
    errors.iter().map(|e| error_record(e)).collect()
}

fn error_record(e: &str) -> Record {
    // ((?:level\.jsonc|triggers/NAME\.jsonc))(/(?:[^:]|:(?=//))*)?: (.*)
    let file = if e.starts_with("level.jsonc") {
        Some("level.jsonc".to_string())
    } else {
        rx!(r"^triggers/[A-Za-z0-9_-]+\.jsonc").find(e).map(|m| m.as_str().to_string())
    };
    if let Some(f) = file {
        let rest = &e[f.len()..];
        let parse = |rest: &str| -> Option<(String, String)> {
            if let Some(m) = rest.strip_prefix(": ") {
                return Some((String::new(), m.to_string()));
            }
            if !rest.starts_with('/') {
                return None;
            }
            let b = rest.as_bytes();
            let mut i = 0;
            while i < b.len() {
                if b[i] == b':' {
                    if rest[i + 1..].starts_with("//") {
                        i += 1;
                        continue;
                    }
                    break;
                }
                i += 1;
            }
            if i < b.len() && rest[i..].starts_with(": ") {
                return Some((rest[..i].to_string(), rest[i + 2..].to_string()));
            }
            None
        };
        if let Some((p, m)) = parse(rest) {
            return Record { file: f, path: p.trim_start_matches('/').to_string(), message: m };
        }
    }
    let r = rx!(r"(?s)^level://(triggers|volumes|timers|variables|strings)/([A-Za-z0-9_-]+)(/[^ :]*)?(?: \+0x[0-9a-f]+)?: (.*)$");
    if let Some(c) = r.captures(e) {
        let (kind, name, rest, msg) = (&c[1], &c[2], c.get(3).map(|m| m.as_str()).unwrap_or(""), &c[4]);
        if kind == "triggers" {
            return Record { file: format!("triggers/{name}.jsonc"), path: rest.trim_start_matches('/').into(), message: msg.into() };
        }
        return Record { file: "level.jsonc".into(), path: format!("{kind}/{name}{rest}"), message: msg.into() };
    }
    Record { file: String::new(), path: String::new(), message: e.into() }
}

/// The starts' box: the viewer's `spawnbox` written to a file ({"box": {"min", "max"}}).
pub fn read_spawn_box(path: Option<&Path>) -> Result<Option<SpawnBox>> {
    let Some(p) = path else { return Ok(None) };
    let bad = || lerr(format!("{}: not a spawn box (the viewer's spawnbox writes one)", p.display()));
    let t = std::fs::read_to_string(p).map_err(|_| bad())?;
    let v = jsonc::parse(&t).map_err(|_| bad())?;
    let b = v.get("box").ok_or_else(bad)?;
    let get = |k: &str| -> Result<[f64; 3]> {
        let a = b.get(k).and_then(Value::as_array).ok_or_else(bad)?;
        let f: Vec<f64> = a.iter().map(|x| py_float(x).map_err(|_| bad())).collect::<Result<_>>()?;
        if f.len() != 3 {
            // the Python keeps any length; three is what the checks read
            return Err(bad());
        }
        Ok([f[0], f[1], f[2]])
    };
    Ok(Some((get("min")?, get("max")?)))
}

type Corners = ([(f64, f64); 4], (f64, f64));

/// The box's footprint corners (x, z) at a start turned by rotY, and its y range.
pub fn start_corners(pos: [f64; 3], rot_y: f64, bx: &SpawnBox) -> Corners {
    let (lo, hi) = bx;
    let (s_, c) = (rot_y.sin(), rot_y.cos());
    let (side, fwd) = ((c, -s_), (s_, c));
    let mut pts = vec![];
    for x in [lo[0], hi[0]] {
        for z in [lo[2], hi[2]] {
            pts.push((pos[0] + side.0 * x + fwd.0 * z, pos[2] + side.1 * x + fwd.1 * z));
        }
    }
    ([pts[0], pts[1], pts[3], pts[2]], (pos[1] + lo[1], pos[1] + hi[1]))
}

pub fn boxes_overlap(a: &Corners, b: &Corners) -> bool {
    let ((pa, ya), (pb, yb)) = (a, b);
    if ya.1 <= yb.0 || yb.1 <= ya.0 {
        return false;
    }
    for poly in [pa, pb] {
        for i in 0..4 {
            let (x0, z0) = poly[i];
            let (x1, z1) = poly[(i + 1) % 4];
            let (ax, az) = (-(z1 - z0), x1 - x0);
            let pra: Vec<f64> = pa.iter().map(|(x, z)| ax * x + az * z).collect();
            let prb: Vec<f64> = pb.iter().map(|(x, z)| ax * x + az * z).collect();
            let mx = |v: &[f64]| v.iter().copied().fold(f64::NEG_INFINITY, f64::max);
            let mn = |v: &[f64]| v.iter().copied().fold(f64::INFINITY, f64::min);
            if mx(&pra) <= mn(&prb) || mx(&prb) <= mn(&pra) {
                return false;
            }
        }
    }
    true
}

/// Warnings: starts (the base world's tile cars, as the level's patches move them, and the
/// level's own) whose boxes overlap.
pub fn start_overlaps(base: &BaseWorld, patches: &Value, archive: &str, bx: &SpawnBox, own: Option<&Value>) -> Result<Vec<String>> {
    let f = base.dir.join("World").join("0").join("tileCars.jsonc");
    if !f.exists() {
        return Ok(vec![]);
    }
    let d = read_jsonc(&f)?;
    let cars = d.get("items").filter(|x| truthy(x)).and_then(Value::as_array).cloned().unwrap_or_default();
    let mut starts: Vec<(String, Corners)> = vec![];
    for (i, c) in cars.iter().enumerate() {
        let path = format!("World/0/tileCars/{i}");
        let pt = or_empty(dget(patches, &format!("base://{archive}/world_0/{path}"))?);
        let cpos = or_empty(c.get("pos"));
        let mut pos = [0.0; 3];
        for (k, a) in ["x", "y", "z"].iter().enumerate() {
            let dflt = dget(&cpos, a)?.cloned().unwrap_or(json!(0));
            pos[k] = py_float(dget(&pt, &format!("pos/{a}"))?.unwrap_or(&dflt))?;
        }
        let dr = c.get("rotY").cloned().unwrap_or(json!(0));
        let rot = py_float(dget(&pt, "rotY")?.unwrap_or(&dr))?;
        starts.push((path, start_corners(pos, rot, bx)));
    }
    for (n, st) in ditems(&or_empty(own))? {
        if let Some(p) = st.get("pos").and_then(Value::as_array).filter(|a| a.len() == 3 && st.is_object()) {
            let pos = [py_float(&p[0])?, py_float(&p[1])?, py_float(&p[2])?];
            let rot = py_float(st.get("rotY").unwrap_or(&json!(0)))?;
            starts.push((format!("starts/{n}"), start_corners(pos, rot, bx)));
        }
    }
    let mut out = vec![];
    for i in 0..starts.len() {
        for j in i + 1..starts.len() {
            if boxes_overlap(&starts[i].1, &starts[j].1) {
                let a = &starts[i].0;
                let wh = if a.starts_with("starts/") { format!("level.jsonc/{a}") } else { format!("level.jsonc/patches/base://{archive}/world_0/{a}") };
                out.push(format!(
                    "{wh}: its start's box overlaps {}'s (the cars' box, {} x {} x {})",
                    starts[j].0,
                    fmt_g(bx.1[0] - bx.0[0]),
                    fmt_g(bx.1[1] - bx.0[1]),
                    fmt_g(bx.1[2] - bx.0[2])
                ));
            }
        }
    }
    Ok(out)
}

// ------------------------------------------------------------------------------------------
// the base world, extracted once into the cache

/// The cache folder: `cache`, else $XDG_CACHE_HOME (or ~/.cache)/hwvx-sdl/levels.
pub fn cache_dir(cache: Option<&Path>) -> PathBuf {
    if let Some(c) = cache {
        return c.to_path_buf();
    }
    let root = std::env::var_os("XDG_CACHE_HOME")
        .filter(|x| !x.is_empty())
        .map(PathBuf::from)
        .unwrap_or_else(|| PathBuf::from(std::env::var_os("HOME").unwrap_or_default()).join(".cache"));
    root.join("hwvx-sdl").join("levels")
}

fn find_archive(data: &Path, name: &str) -> Option<PathBuf> {
    let mut stack = vec![data.to_path_buf()];
    let mut found = vec![];
    while let Some(d) = stack.pop() {
        let Ok(rd) = std::fs::read_dir(&d) else { continue };
        let mut ents: Vec<PathBuf> = rd.flatten().map(|e| e.path()).collect();
        ents.sort();
        for p in ents {
            if p.is_dir() {
                stack.push(p);
            } else if p.is_file() {
                let stem = p.file_stem().map(|s| s.to_string_lossy().to_lowercase()).unwrap_or_default();
                let ext = p.extension().map(|s| s.to_string_lossy().to_lowercase()).unwrap_or_default();
                if stem == name && ["xgc", "xpc", "xps"].contains(&ext.as_str()) {
                    found.push(p);
                }
            }
        }
    }
    found.sort();
    found.into_iter().next()
}

/// The base world's extracted project, made once into the cache (game data: local only).
pub fn base_project(env: &Env, world: &str, data: &Path, cache: &Path) -> Result<PathBuf> {
    let world = world.replace('\\', "/");
    let name = world.rsplit('/').next().unwrap_or("").to_lowercase();
    let Some(src) = find_archive(data, &name) else {
        return Err(lerr(format!("{world}: no world archive {name} under {}", data.display())));
    };
    let exe = env.find_hwvx_x();
    let mut h = Sha256::new();
    h.update(crate::error::read(&src)?);
    h.update(tools_hash(env.root()).as_bytes());
    h.update(exe_stamp(exe.as_deref()).as_bytes());
    let h = hex(&h.finalize());
    let out = cache.join(format!("{name}-{}", &h[..16]));
    let proj = out.join("project");
    if proj.join("pack.jsonc").exists()
        && let Ok(pj) = read_jsonc(&proj.join("pack.jsonc"))
        && pj.get("archive").map(s).unwrap_or_default().to_lowercase() == name
    {
        return Ok(proj);
    }
    let Some(exe) = exe else { return Err(lerr("hwvx-x not found (target/release/hwvx-x)")) };
    let ext = out.join("extract").join(&name);
    let ep = ext.parent().unwrap();
    if ep.exists() {
        std::fs::remove_dir_all(ep).map_err(|e| Error::io(format!("{}: {e}", ep.display())))?;
    }
    std::fs::create_dir_all(ep).map_err(|e| Error::io(format!("{}: {e}", ep.display())))?;
    let r = std::process::Command::new(&exe)
        .arg("extract")
        .arg(&src)
        .arg(&ext)
        .stdin(std::process::Stdio::null())
        .output()
        .map_err(|e| lerr(format!("hwvx-x extract: {e}")))?;
    if !r.status.success() {
        let t = String::from_utf8_lossy(&r.stderr).trim().to_string();
        let tail: String = t.chars().rev().take(300).collect::<Vec<_>>().into_iter().rev().collect();
        return Err(lerr(format!("hwvx-x extract: {tail}")));
    }
    let man = jsonc::parse(&crate::error::read_text(&ext.join("manifest.json"))?)?;
    let pack = man["entries"]
        .as_array()
        .and_then(|a| a.iter().find(|e| e["type"].as_str() == Some("world")))
        .map(|e| ext.join(s(&e["path"])))
        .ok_or_else(|| lerr(format!("{}: no world pack in it", src.display())))?;
    if proj.exists() {
        std::fs::remove_dir_all(&proj).map_err(|e| Error::io(format!("{}: {e}", proj.display())))?;
    }
    let srcp = graph::Source::open(&pack, &env.g)?;
    graph::Extractor::new(&env.g, srcp, &proj, false).run()?;
    Ok(proj)
}

// ------------------------------------------------------------------------------------------
// the commands, as library calls

/// An error the commands stop with, as xdl_level.py's main reports it (without "error: "): a
/// level or trigger problem as it is, a Python exception of a wrongly shaped level.jsonc as
/// "level.jsonc: TypeError: ...".
pub fn top_message(e: &Error) -> String {
    match e.kind {
        Kind::Uncaught => format!("level.jsonc: {}", e.msg),
        _ => e.msg.clone(),
    }
}

/// Where the base world comes from: an extracted project, or the game's data (extracted once
/// into the cache).
#[derive(Clone, Debug, Default)]
pub struct BaseOpts {
    pub base: Option<PathBuf>,
    pub data: Option<PathBuf>,
    pub cache: Option<PathBuf>,
}

impl BaseOpts {
    pub fn resolve(&self, env: &Env, proj: &Path) -> Result<Option<PathBuf>> {
        if let Some(b) = &self.base {
            return Ok(Some(b.clone()));
        }
        if let Some(d) = &self.data {
            let lv = read_jsonc(&proj.join("level.jsonc"))?;
            let w = dget(&lv, "world")?.ok_or_else(|| pyerr("KeyError", "'world'"))?;
            let Some(w) = w.as_str() else { return Err(pyerr("AttributeError", format!("'{}' object has no attribute 'replace'", py::type_name(w)))) };
            return base_project(env, w, d, &cache_dir(self.cache.as_deref())).map(Some);
        }
        Ok(None)
    }
}

/// `compile --json`'s result.
#[derive(Debug, Clone)]
pub struct Compiled {
    pub ok: bool,
    pub out: PathBuf,
    pub cached: bool,
    pub ms: f64,
    pub errors: Vec<Record>,
    pub warnings: Vec<Record>,
    /// the texts the records come from
    pub error_texts: Vec<String>,
    pub warning_texts: Vec<String>,
    pub doc: Option<Value>,
}

fn round1(x: f64) -> f64 {
    (x * 10.0).round() / 10.0
}

/// xdl_level.py compile: the level file at `out` (compiled again only when a source changed);
/// warnings only with a spawn box (as the Python: they come from a second check). Err: what
/// the Python reports as one error and no result (a base that can't be made, a level.jsonc
/// of the wrong shape).
pub fn compile(env: &Env, proj: &Path, out: Option<&Path>, base: &BaseOpts, skip: bool, spawn_box: Option<&Path>) -> Result<Compiled> {
    let t0 = Instant::now();
    let out = out.map(Path::to_path_buf).unwrap_or_else(|| proj.join("build").join("level.json"));
    let b = base.resolve(env, proj)?;
    let (doc, errors, cached) = compile_cached(env, proj, &out, b.as_deref(), skip)?;
    let mut warnings = vec![];
    if spawn_box.is_some() && errors.is_empty() {
        let b2 = base.resolve(env, proj)?;
        let sb = read_spawn_box(spawn_box)?;
        warnings = compile_project_full(env, proj, b2.as_deref(), skip, sb)?.2;
    }
    Ok(Compiled {
        ok: errors.is_empty(),
        out,
        cached,
        ms: round1(t0.elapsed().as_secs_f64() * 1000.0),
        errors: error_records(&errors),
        warnings: error_records(&warnings),
        doc: if errors.is_empty() { doc } else { None },
        error_texts: errors,
        warning_texts: warnings,
    })
}

/// xdl_level.py check: (ok, errors, warnings) as records, and the raw texts.
pub fn check(env: &Env, proj: &Path, base: &BaseOpts, spawn_box: Option<&Path>) -> Result<(Vec<String>, Vec<String>)> {
    let b = base.resolve(env, proj)?;
    let sb = read_spawn_box(spawn_box)?;
    let (_, e, w) = compile_project_full(env, proj, b.as_deref(), false, sb)?;
    Ok((e, w))
}

pub fn start_mode(mode: &str) -> &str {
    match mode {
        "adventure" => "mission",
        m => m,
    }
}

/// HWVX_START for a test drive: race:mode=M,world=ID[,car=N]. The game splits it at ',' and
/// '=', so the mode and id must be the compiler's own (a mode of MODES, a uid:// id), never
/// text that could add a key of its own; the compile refuses others, and this checks again.
pub fn start_string(mode: &str, id: &str, car: Option<i64>) -> Result<String> {
    if !MODES.iter().any(|m| m.0 == mode) {
        return Err(lerr(format!("level.jsonc: mode: {}: not a mode", py::str_repr(mode))));
    }
    if !rx!(r"^uid://[0-9a-z]{1,13}$").is_match(id) {
        return Err(lerr(format!("level.jsonc: id: {}: uid:// and 1 to 13 of 0-9 a-z", py::str_repr(id))));
    }
    let mut start = format!("race:mode={},world={id}", start_mode(mode));
    if let Some(c) = car {
        start += &format!(",car={c}");
    }
    Ok(start)
}

#[derive(Clone, Debug, Default)]
pub struct DriveOpts {
    pub car: Option<i64>,
    pub skip_intro: bool,
    /// the level applied in memory (Port_Level.c), not built into a world pack
    pub memory: bool,
    pub base: BaseOpts,
}

/// xdl_level.py testdrive: the project compiled (if it changed) into SCRATCH/mods/editor-test, a
/// mod whose track is the level, and what to start the game with: the JSON the Python prints.
pub fn testdrive(env: &Env, proj: &Path, scratch: &Path, o: &DriveOpts) -> Result<Value> {
    let t0 = Instant::now();
    let mod_dir = scratch.join("mods").join("editor-test");
    let mut res = Map::new();
    res.insert("ok".into(), json!(false));
    res.insert("errors".into(), json!([]));
    let mut lv = Value::Null;
    let mut pack: Option<PathBuf> = None;
    let r = (|| -> Result<(Option<Value>, Vec<String>, bool)> {
        lv = read_jsonc(&proj.join("level.jsonc"))?;
        for k in ["id", "name", "mode", "extends", "world"] {
            if !dget(&lv, k)?.is_some_and(Value::is_string) {
                return Err(lerr(format!("level.jsonc/{k}: missing (a string)")));
            }
        }
        let b = o.base.resolve(env, proj)?;
        let (doc, mut errors, mut cached) = compile_cached(env, proj, &mod_dir.join("level.json"), b.as_deref(), o.skip_intro)?;
        if errors.is_empty()
            && let Some(d) = o.base.data.as_ref().filter(|_| !o.memory)
        {
            // option 1: the level built into its own world pack (level_pack)
            match crate::level_pack::build_cached(env, proj, d, &cache_dir(o.base.cache.as_deref()), &mod_dir.join("worlds"), o.skip_intro) {
                Ok((p, pc, _)) => {
                    pack = Some(p);
                    cached = cached && pc;
                }
                Err(e) if e.kind == Kind::Level => errors = e.msg.lines().map(str::to_string).collect(),
                Err(e) => return Err(e),
            }
        }
        Ok((doc, errors, cached))
    })();
    let (doc, errors, cached) = match r {
        Ok(x) => x,
        Err(e) if e.kind == Kind::Level => (None, vec![e.msg], false),
        Err(e) => return Err(e),
    };
    res.insert("errors".into(), Value::Array(error_records(&errors).iter().map(Record::to_json).collect()));
    if let Some(doc) = doc.filter(|_| errors.is_empty()) {
        let mut track = Map::new();
        track.insert("extends".into(), lv["extends"].clone());
        track.insert("name".into(), lv["name"].clone());
        track.insert("unlocked".into(), json!(true));
        if let Some(p) = &pack {
            track.insert("worldPack".into(), json!(format!("worlds/{}", p.file_name().unwrap().to_string_lossy())));
        } else {
            track.insert("world".into(), doc["world"].clone());
            track.insert("level".into(), json!("level.json"));
        }
        let mut tracks = Map::new();
        tracks.insert(s(&lv["id"]), Value::Object(track));
        let m = json!({"$schema": "../../schemas/Mod.schema.json", "format_version": 1, "id": "uid://editortest",
                       "name": "Editor test drive", "version": "1.0.0", "description": format!("{}, on test", s(&lv["name"])),
                       "tracks": tracks});
        let text = format!("{}\n", py::dumps_indent(&m, 2, false));
        let mj = mod_dir.join("mod.jsonc");
        if std::fs::read_to_string(&mj).ok().as_deref() != Some(text.as_str()) {
            graph::write_text(&mj, &text)?;
        }
        let start = start_string(&s(&lv["mode"]), &s(&lv["id"]), o.car)?;
        let level = mod_dir.join("level.json");
        let mut products = vec![json!({"kind": "level", "path": level.display().to_string(), "cached": cached})];
        if let Some(p) = &pack {
            products.push(json!({"kind": "world", "path": p.display().to_string(), "cached": cached}));
        }
        res.insert("ok".into(), json!(true));
        res.insert("cached".into(), json!(products.iter().all(|p| p["cached"] == true)));
        res.insert("start".into(), json!(start));
        res.insert("mods".into(), json!(scratch.join("mods").display().to_string()));
        res.insert("level".into(), json!(level.display().to_string()));
        res.insert("products".into(), Value::Array(products));
    }
    res.insert("ms".into(), f64v(round1(t0.elapsed().as_secs_f64() * 1000.0)));
    Ok(Value::Object(res))
}

/// A mod folder: mod.jsonc with the level's track, the level file, the project's sources.
pub fn make_mod(proj: &Path, out: &Path, doc: &Value) -> Result<PathBuf> {
    let lv = read_jsonc(&proj.join("level.jsonc"))?;
    std::fs::create_dir_all(out).map_err(|e| Error::io(format!("{}: {e}", out.display())))?;
    let id = s(&lv["id"]);
    let name = rx!(r"[^a-z0-9]+").replace_all(id.get("uid://".len()..).unwrap_or(""), "-").to_string();
    write_json(&out.join("level").join(format!("{name}.json")), doc)?;
    let src = out.join("level").join(format!("{name}-src"));
    if src.exists() {
        std::fs::remove_dir_all(&src).map_err(|e| Error::io(format!("{}: {e}", src.display())))?;
    }
    std::fs::create_dir_all(&src).map_err(|e| Error::io(format!("{}: {e}", src.display())))?;
    copy2(&proj.join("level.jsonc"), &src.join("level.jsonc"))?;
    if proj.join("triggers").is_dir() {
        copytree(&proj.join("triggers"), &src.join("triggers"))?;
    }
    let mut track = Map::new();
    track.insert("extends".into(), lv["extends"].clone());
    track.insert("name".into(), lv["name"].clone());
    track.insert("unlocked".into(), json!(truthy(lv.get("unlocked").unwrap_or(&json!(true)))));
    track.insert("world".into(), doc["world"].clone());
    track.insert("level".into(), json!(format!("level/{name}.json")));
    if let Some(t) = lv.get("thumbnail").filter(|t| truthy(t)) {
        track.insert("thumbnail".into(), t.clone());
    }
    let mod_id = match lv.get("modId") {
        Some(m) => m.clone(),
        None => {
            use sha1::Digest as _;
            let h = sha1::Sha1::digest(id.as_bytes());
            json!(format!("uid://{}", &hex(&h)[..13]))
        }
    };
    let mut tracks = Map::new();
    tracks.insert(id.clone(), Value::Object(track));
    let mut m = Map::new();
    m.insert("$schema".into(), json!("../../schemas/Mod.schema.json"));
    m.insert("format_version".into(), json!(1));
    m.insert("id".into(), mod_id);
    m.insert("name".into(), lv["name"].clone());
    m.insert("version".into(), lv.get("version").cloned().unwrap_or(json!("1.0.0")));
    m.insert("author".into(), lv.get("author").cloned().unwrap_or(json!("")));
    m.insert(
        "description".into(),
        lv.get("description").cloned().unwrap_or_else(|| json!(format!("A {} level: {}", s(&lv["mode"]), s(&lv["name"])))),
    );
    m.insert("tracks".into(), Value::Object(tracks));
    graph::write_text(&out.join("mod.jsonc"), &format!("// made by tools/xdl_level.py mod\n{}\n", py::dumps_indent(&Value::Object(m), 2, false)))?;
    Ok(out.to_path_buf())
}

/// shutil.copy2: the bytes and the modification time
fn copy2(a: &Path, b: &Path) -> Result<()> {
    std::fs::copy(a, b).map_err(|e| Error::io(format!("{}: {e}", a.display())))?;
    if let Ok(m) = std::fs::metadata(a).and_then(|m| m.modified())
        && let Ok(f) = std::fs::File::options().write(true).open(b)
    {
        let _ = f.set_modified(m);
    }
    Ok(())
}

fn copytree(a: &Path, b: &Path) -> Result<()> {
    std::fs::create_dir_all(b).map_err(|e| Error::io(format!("{}: {e}", b.display())))?;
    let rd = std::fs::read_dir(a).map_err(|e| Error::io(format!("{}: {e}", a.display())))?;
    for e in rd.flatten() {
        let p = e.path();
        let t = b.join(e.file_name());
        if p.is_dir() {
            copytree(&p, &t)?;
        } else {
            copy2(&p, &t)?;
        }
    }
    Ok(())
}

/// xdl_level.py mod: the mod folder; with `xmod`, packed with the no-game-data check
/// ([`crate::level_share::check_and_pack`]). Ok: the line to show.
pub fn share(env: &Env, proj: &Path, out: &Path, xmod: Option<&Path>, base: &BaseOpts, index: Option<&Path>) -> Result<std::result::Result<String, Vec<String>>> {
    let b = base.resolve(env, proj)?;
    let (doc, errors) = compile_project(env, proj, b.as_deref(), false)?;
    if !errors.is_empty() {
        return Ok(Err(errors));
    }
    let out = make_mod(proj, out, doc.as_ref().unwrap())?;
    let Some(xmod) = xmod else { return Ok(Ok(format!("{}: mod.jsonc and the level", out.display()))) };
    let line = crate::level_share::check_and_pack(env.root(), &out, xmod, index, base.data.as_deref(), &cache_dir(base.cache.as_deref()))?;
    Ok(Ok(line))
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn g_format() {
        for (x, t) in [(10.0, "10"), (6.65, "6.65"), (27.0, "27"), (0.0001, "0.0001"), (1234567.0, "1.23457e+06"), (0.00001, "1e-05"), (-2.5, "-2.5")] {
            assert_eq!(fmt_g(x), t);
        }
    }

    #[test]
    fn records() {
        let r = error_records(&[
            "level.jsonc/patches/base://ch1_l2/world_0/World/0/timers/0/to/time: a number".into(),
            "level://triggers/cp1/param +0x4: level://volumes/nope: the level has nothing of that name".into(),
            "level.jsonc/volumes/cp1/radious: not a field of WorldVolume".into(),
            "level.jsonc: id: missing (a string)".into(),
        ]);
        assert_eq!(r[0], Record { file: "level.jsonc".into(), path: "patches/base://ch1_l2/world_0/World/0/timers/0/to/time".into(), message: "a number".into() });
        assert_eq!((r[1].file.as_str(), r[1].path.as_str()), ("triggers/cp1.jsonc", "param"));
        assert_eq!(r[2].path, "volumes/cp1/radious");
        assert_eq!((r[3].path.as_str(), r[3].message.as_str()), ("", "id: missing (a string)"));
    }

    #[test]
    fn trigger_names() {
        for ok in ["cp1", "Gate_2", "rules", "a-b"] {
            assert!(check_trigger_name(ok).is_ok(), "{ok}");
        }
        for bad in ["", "a/b", "../x", "..", "/abs", "a\\b", "c:x", "a.b", "a b", "\u{e9}"] {
            assert!(check_trigger_name(bad).is_err(), "{bad}");
        }
    }

    #[test]
    fn base_step_overflow_is_a_level_error() {
        let env = Env::new(Path::new(env!("CARGO_MANIFEST_DIR")).join("../..").as_path()).unwrap();
        let st = |p: &str| base_steps(&env, p, "level.jsonc/x");
        assert_eq!(st("World/0/quads/2/o4").unwrap()["steps"], json!([["p", 0x24], ["a", 164]]));
        for p in [
            "World/0/quads/170141183460469231731687303715884105727",   // i128::MAX * 80
            "World/0/quads/99999999999999999999999999999999999999999", // past i128
            &format!("World/0/quads/{}/offffffff", i128::MAX / 80),     // the sum
        ] {
            let e = st(p).unwrap_err();
            assert_eq!(e.kind, Kind::Level, "{p}: {}", e.msg);
            assert!(e.msg.contains("out of range"), "{}", e.msg);
        }
    }

    #[test]
    fn test_drive_start_string() {
        assert_eq!(start_string("adventure", "uid://abc123", Some(3)).unwrap(), "race:mode=mission,world=uid://abc123,car=3");
        assert_eq!(start_string("joyride", "uid://a", None).unwrap(), "race:mode=joyride,world=uid://a");
        for (m, id) in [("joyride,car=9", "uid://a"), ("joyride=x", "uid://a"), ("joyride", "uid://a,car=9"), ("joyride", "uid://a=b"), ("joyride", "")] {
            let e = start_string(m, id, None);
            assert!(e.is_err(), "{m} {id}");
        }
    }

    #[test]
    fn boxes() {
        let bx: SpawnBox = ([-5.0, -1.4, -14.0], [5.0, 5.25, 13.0]);
        let a = start_corners([0.0, 0.0, 0.0], 0.0, &bx);
        assert!(boxes_overlap(&a, &start_corners([9.0, 0.0, 0.0], 0.0, &bx)));
        assert!(!boxes_overlap(&a, &start_corners([11.0, 0.0, 0.0], 0.0, &bx)));
        assert!(!boxes_overlap(&a, &start_corners([0.0, 0.0, 28.0], 0.0, &bx)));
        assert!(boxes_overlap(&a, &start_corners([11.0, 0.0, 0.0], std::f64::consts::FRAC_PI_2, &bx)));
        assert!(!boxes_overlap(&a, &start_corners([0.0, 8.0, 0.0], 0.0, &bx)));
    }
}
