//! TAC triggers (Trigger.h) as JSON (tools/xdl_tac.py): the Trigger / TriggerAction records
//! and their tagged-union parameter lists, driven by schemas/Trigger.schema.json.

use crate::decode::{self, Raw};
use crate::encode;
use crate::error::{Error, Kind, Result};
use crate::pack;
use crate::py::{self, Num};
use crate::schema::{self, Schemas};
use serde_json::{Map, Value};
use std::collections::{BTreeMap, HashMap, HashSet};
use std::path::Path;

/// The trigger schema, digested.
pub struct Tac {
    pub sch: Value,
    pub structs: HashMap<String, Value>,
    pub variants: Map<String, Value>,
    pub records: Map<String, Value>,
    /// {enum: {value: {"kind", "items"}}}
    pub cases: Map<String, Value>,
    pub tokens: BTreeMap<i64, Value>,
    pub token_val: HashMap<String, i64>,
    pub operands: BTreeMap<i64, String>,
    pub operand_val: HashMap<String, i64>,
    pub size: HashMap<String, u32>,
}

fn obj_of(v: &Value) -> Map<String, Value> {
    v.as_object().cloned().unwrap_or_default()
}

impl Tac {
    pub fn load(path: &Path) -> Result<Tac> {
        let sch = crate::jsonc::parse(&crate::error::read_text(path)?)?;
        Tac::new(sch)
    }

    pub fn from_schemas(s: &Schemas) -> Result<Tac> {
        let sch = s.top("Trigger").ok_or_else(|| Error::uncaught("KeyError: 'Trigger'"))?;
        Tac::new(sch.clone())
    }

    pub fn new(sch: Value) -> Result<Tac> {
        let mut structs = HashMap::new();
        structs.insert("Trigger".to_string(), sch.clone());
        structs.insert("TriggerAction".to_string(), sch["$defs"]["TriggerAction"].clone());
        let mut tokens = BTreeMap::new();
        let mut token_val = HashMap::new();
        for (k, v) in obj_of(&sch["x-tokens"]) {
            let i: i64 = k.parse().unwrap_or(0);
            tokens.insert(i, v.clone());
        }
        for (k, v) in &tokens {
            token_val.insert(py::str_of(v), *k);
        }
        let mut operands = BTreeMap::new();
        let mut operand_val = HashMap::new();
        for (k, v) in obj_of(&sch["x-operands"]) {
            operands.insert(k.parse().unwrap_or(0), py::str_of(&v));
        }
        for (k, v) in &operands {
            operand_val.insert(v.clone(), *k);
        }
        let mut size = HashMap::new();
        for (k, s) in &structs {
            size.insert(k.clone(), schema::size(s)? as u32);
        }
        Ok(Tac {
            variants: obj_of(&sch["x-variants"]),
            records: obj_of(&sch["x-records"]),
            cases: obj_of(&sch["x-cases"]),
            sch,
            structs,
            tokens,
            token_val,
            operands,
            operand_val,
            size,
        })
    }

    pub fn fields(&self, sname: &str) -> Vec<(&String, &Value)> {
        schema::fields(&self.structs[sname])
    }

    /// The items of the parameter list of `kind` of `enum` (a name; None for a number).
    pub fn layout(&self, en: &str, kind: &Value) -> Option<&Value> {
        kind.as_str().and_then(|k| self.variants.get(en).and_then(|v| v.get(k)))
    }

    pub fn name_of(p: &Value, v: &Value) -> Value {
        if let Some(names) = p.get("x-enum").and_then(Value::as_object) {
            if let Some(n) = names.get(&py::str_of(v)) {
                return n.clone();
            }
        }
        v.clone()
    }

    pub fn value_of(p: &Value, v: &Value, path: &str) -> Result<Value> {
        if let Value::String(s) = v {
            if let Some(names) = p.get("x-enum").and_then(Value::as_object) {
                for (k, n) in names {
                    if n.as_str() == Some(s) {
                        return Ok(decode::int_value(k.parse::<i128>().unwrap_or(0)));
                    }
                }
            }
            return Err(Error::tac(format!("{path}: unknown name {}", py::str_repr(s))));
        }
        if !is_int(v) {
            return Err(Error::tac(format!("{path}: a name or an integer")));
        }
        Ok(v.clone())
    }

    pub fn token(&self, v: u32) -> Value {
        self.tokens.get(&(v as i64)).cloned().unwrap_or(Value::from(v))
    }

    pub fn token_value(&self, v: &Value, path: &str) -> Result<i128> {
        if let Value::String(s) = v {
            return self
                .token_val
                .get(s)
                .map(|x| *x as i128)
                .ok_or_else(|| Error::tac(format!("{path}: unknown keyword {} (Trigger.h TacToken)", py::str_repr(s))));
        }
        match (is_int(v), Num::of(v)) {
            (true, Some(Num::Int(i))) => Ok(i),
            _ => Err(Error::tac(format!("{path}: a keyword or an integer"))),
        }
    }
}

/// isinstance(v, int) and not isinstance(v, bool)
fn is_int(v: &Value) -> bool {
    v.is_i64() || v.is_u64()
}

/// isinstance(v, (int, float)) and not isinstance(v, bool)
fn is_num(v: &Value) -> bool {
    v.is_number()
}

fn item_str(it: &Value, i: usize) -> String {
    it.get(i).map(py::str_of).unwrap_or_default()
}

// ------------------------------------------------------------------------------------------
// reading

/// Words of a pack: its bytes, byte order and the link text of each pointer slot.
pub struct Reader<'a> {
    pub blob: &'a [u8],
    pub big: bool,
    /// offset -> link text
    pub slots: HashMap<u32, Value>,
}

impl Reader<'_> {
    pub fn u32(&self, o: u32) -> Result<u32> {
        pack::u32_at(self.blob, o, self.big)
    }
}

struct Dec<'a, 'b> {
    t: &'a Tac,
    r: &'a Reader<'b>,
    o: u32,
    end: u32,
    path: String,
}

fn f32_num(w: u32) -> Result<Value> {
    py::float_value(f32::from_bits(w) as f64, false)
}

impl Dec<'_, '_> {
    fn need(&self, name: &str) -> Result<()> {
        if self.o as u64 + 4 > self.end as u64 {
            return Err(Error::tac(format!("{}/{name}: past the end of the list's node", self.path)));
        }
        Ok(())
    }

    fn plain(&mut self, name: &str) -> Result<u32> {
        self.need(name)?;
        if self.r.slots.contains_key(&self.o) {
            return Err(Error::tac(format!("{}/{name}: a number on a pointer slot", self.path)));
        }
        let w = self.r.u32(self.o)?;
        self.o += 4;
        Ok(w)
    }

    fn link(&mut self, name: &str) -> Result<Value> {
        self.need(name)?;
        let v = if let Some(v) = self.r.slots.get(&self.o) {
            v.clone()
        } else {
            let w = self.r.u32(self.o)?;
            if w != 0 {
                return Err(Error::tac(format!("{}/{name}: a link slot holds 0x{w:X} without relocation", self.path)));
            }
            Value::Null
        };
        self.o += 4;
        Ok(v)
    }

    fn any(&mut self, name: &str) -> Result<Value> {
        self.need(name)?;
        if self.r.slots.contains_key(&self.o) {
            return self.link(name);
        }
        Ok(Value::from(self.plain(name)?))
    }

    fn count_fits(&self, n: u32, per: u64) -> bool {
        self.o as u64 + per * n as u64 <= self.end as u64
    }

    fn items(&mut self, its: &Value, sub: &str) -> Result<Map<String, Value>> {
        let mut obj = Map::new();
        for it in its.as_array().map(Vec::as_slice).unwrap_or(&[]) {
            let k = item_str(it, 0);
            let name = item_str(it, 1);
            let v = match k.as_str() {
                "u32" => Value::from(self.plain(&name)?),
                "s32" => Value::from(self.plain(&name)? as i32),
                "f32" => f32_num(self.plain(&name)?)?,
                "vec3" => {
                    let mut ws = vec![];
                    for i in 0..3 {
                        ws.push(self.plain(&format!("{name}/{i}"))?);
                    }
                    Value::Array(ws.into_iter().map(f32_num).collect::<Result<_>>()?)
                }
                "switch" => {
                    let w = self.plain(&name)?;
                    let en = item_str(it, 2);
                    let case = self.t.cases.get(&en).and_then(|c| c.get(w.to_string()));
                    let Some(case) = case else {
                        return Err(Error::tac(format!("{sub}/{name}: {w} is no {en}")));
                    };
                    let mut m = Map::new();
                    m.insert("kind".into(), case["kind"].clone());
                    for (a, b) in self.items(&case["items"], &format!("{sub}/{name}"))? {
                        m.insert(a, b);
                    }
                    Value::Object(m)
                }
                "token" => {
                    let w = self.plain(&name)?;
                    self.t.token(w)
                }
                "any" => self.any(&name)?,
                "link" => self.link(&name)?,
                "list" => {
                    let n = self.plain(&format!("{name}/count"))?;
                    if !self.count_fits(n, 4) {
                        return Err(Error::tac(format!("{sub}/{name}: count {n} runs past the node")));
                    }
                    let mut l = vec![];
                    for i in 0..n {
                        l.push(self.link(&format!("{name}/{i}"))?);
                    }
                    Value::Array(l)
                }
                "scratch" => {
                    let of = item_str(it, 2);
                    let n = match obj.get(&of) {
                        Some(Value::Array(a)) => a.len(),
                        Some(x) => return Err(Error::uncaught(format!("object of type '{}' has no len()", py::type_name(x)))),
                        None => return Err(Error::uncaught(format!("KeyError: {}", py::str_repr(&of)))),
                    };
                    let mut l = vec![];
                    for i in 0..n {
                        l.push(Value::from(self.plain(&format!("{name}/{i}"))?));
                    }
                    Value::Array(l)
                }
                "operand" => {
                    let t = self.plain(&format!("{name}/type"))?;
                    let mut m = Map::new();
                    match self.t.operands.get(&(t as i64)).map(String::as_str) {
                        Some("constant") => {
                            let w = self.plain(&name)?;
                            m.insert("const".into(), f32_num(w)?);
                        }
                        Some("variable") => {
                            m.insert("variable".into(), self.link(&name)?);
                        }
                        Some("code_variable") => {
                            let var = self.plain(&format!("{name}/code"))?;
                            m.insert("code".into(), Value::from(var));
                            m.insert("arg".into(), self.any(&format!("{name}/arg"))?);
                        }
                        _ => return Err(Error::tac(format!("{sub}/{name}: operand type {t}"))),
                    }
                    Value::Object(m)
                }
                "codevar" => {
                    let var = self.plain(&name)?;
                    let mut m = Map::new();
                    m.insert("var".into(), Value::from(var));
                    if var < 100 {
                        let n = self.plain(&format!("{name}/count"))?;
                        if !self.count_fits(n, 4) {
                            return Err(Error::tac(format!("{sub}/{name}: count {n} runs past the node")));
                        }
                        let mut l = vec![];
                        for i in 0..n {
                            l.push(self.link(&format!("{name}/cars/{i}"))?);
                        }
                        m.insert("cars".into(), Value::Array(l));
                    } else {
                        m.insert("arg".into(), self.any(&format!("{name}/arg"))?);
                    }
                    Value::Object(m)
                }
                "string" => {
                    let blob = self.r.blob;
                    let lo = (self.o as usize).min(blob.len());
                    let b = &blob[lo..(self.end as usize).clamp(lo, blob.len())];
                    let Some(z) = b.iter().position(|&c| c == 0) else {
                        return Err(Error::tac(format!("{sub}/{name}: no end to the string in its node")));
                    };
                    let ln = ((z + 4) & !3) as u32;
                    let mut x = self.o;
                    while x < self.o + ln {
                        if self.r.slots.contains_key(&x) {
                            return Err(Error::tac(format!("{sub}/{name}: a string over a pointer slot")));
                        }
                        x += 4;
                    }
                    let ps = (self.o as usize + z + 1).min(blob.len());
                    let pad = &blob[ps..(self.o as usize + ln as usize).clamp(ps, blob.len())];
                    if pad.iter().any(|&c| c != 0) || self.o + ln > self.end {
                        return Err(Error::tac(format!("{sub}/{name}: string padding not zero")));
                    }
                    if !b[..z].is_ascii() {
                        return Err(Error::tac(format!("{sub}/{name}: not ASCII")));
                    }
                    let s = String::from_utf8(b[..z].to_vec()).unwrap();
                    self.o += ln;
                    Value::String(s)
                }
                "table" => {
                    let n = self.plain(&format!("{name}/count"))?;
                    let recn = item_str(it, 2);
                    let rec = self.t.records.get(&recn).cloned().ok_or_else(|| Error::uncaught(format!("KeyError: {}", py::str_repr(&recn))))?;
                    let nrec = rec.as_array().map(Vec::len).unwrap_or(0) as u64;
                    if !self.count_fits(n, 4 * nrec) {
                        return Err(Error::tac(format!("{sub}/{name}: {n} records run past the node")));
                    }
                    let mut l = vec![];
                    for i in 0..n {
                        l.push(Value::Object(self.items(&rec, &format!("{sub}/{name}/{i}"))?));
                    }
                    Value::Array(l)
                }
                "words" => {
                    let mut ws = vec![];
                    while self.o as u64 + 4 <= self.end as u64 {
                        ws.push(self.any(&name)?);
                    }
                    Value::Array(ws)
                }
                _ => return Err(Error::tac(format!("{sub}: item {k}"))),
            };
            obj.insert(name, v);
        }
        Ok(obj)
    }
}

/// (json object, length) of a parameter list at off (bytes up to `end` belong to it).
pub fn decode_params(t: &Tac, r: &Reader, off: u32, end: u32, items: &Value, path: &str) -> Result<(Value, u32)> {
    let mut d = Dec { t, r, o: off, end, path: path.into() };
    let mut out = d.items(items, path)?;
    let blob = r.blob;
    let o = d.o;
    let lo = (o as usize).min(blob.len());
    let tail = &blob[lo..(end as usize).clamp(lo, blob.len())];
    if !tail.is_empty() {
        let mut x = o - o % 4;
        let mut slot = false;
        while x < end {
            if x >= o && r.slots.contains_key(&x) {
                slot = true;
                break;
            }
            x += 4;
        }
        if slot {
            // links in what follows: the tail as words
            if (end - o) % 4 != 0 || o % 4 != 0 {
                return Err(Error::tac(format!("{path}: a pointer slot in an unaligned tail")));
            }
            let mut ws = vec![];
            while d.o < end {
                ws.push(d.any("tail")?);
            }
            out.insert("tail".into(), Value::Array(ws));
        } else {
            out.insert("tail".into(), Value::String(tail.iter().map(|b| format!("{b:02x}")).collect()));
        }
    }
    Ok((Value::Object(out), end.wrapping_sub(off)))
}

/// bytes.fromhex
pub fn fromhex(s: &str) -> Option<Vec<u8>> {
    let b: Vec<char> = s.chars().collect();
    let mut out = vec![];
    let mut i = 0;
    while i < b.len() {
        if b[i].is_ascii_whitespace() {
            i += 1;
            continue;
        }
        if i + 1 >= b.len() {
            return None;
        }
        let h = b[i].to_digit(16)?;
        let l = b[i + 1].to_digit(16)?;
        out.push((h * 16 + l) as u8);
        i += 2;
    }
    Some(out)
}

struct Enc<'a> {
    t: &'a Tac,
    big: bool,
    out: Vec<u8>,
    links: BTreeMap<u32, Value>,
    path: String,
}

impl Enc<'_> {
    fn word(&mut self, v: i128) {
        let w = (v & 0xFFFF_FFFF) as u32;
        self.out.extend(if self.big { w.to_be_bytes() } else { w.to_le_bytes() });
    }

    fn num(&mut self, v: &Value, name: &str) -> Result<()> {
        let err = Error::tac(format!("{}/{name}: an integer", self.path));
        let i = match (v.is_boolean(), Num::of(v)) {
            (false, Some(Num::Int(i))) => i,
            (false, Some(Num::Float(f))) => {
                if f.is_nan() {
                    return Err(Error::uncaught("cannot convert float NaN to integer"));
                }
                if f.is_infinite() {
                    return Err(Error::uncaught("cannot convert float infinity to integer"));
                }
                if f != f.trunc() {
                    return Err(err);
                }
                if !(-(2f64.powi(31)) <= f && f < 2f64.powi(32)) {
                    return Err(Error::tac(format!("{}/{name}: {} out of range", self.path, py::int_text_of_float(f))));
                }
                f as i128
            }
            _ => return Err(err),
        };
        if !(-(1i128 << 31) <= i && i < (1i128 << 32)) {
            return Err(Error::tac(format!("{}/{name}: {i} out of range", self.path)));
        }
        self.word(i);
        Ok(())
    }

    fn flt(&mut self, v: &Value, name: &str) -> Result<()> {
        if v.is_boolean() || !is_num(v) {
            return Err(Error::tac(format!("{}/{name}: a number", self.path)));
        }
        let b = encode::pack_float('f', Num::of(v).unwrap().f64(), self.big)?;
        self.out.extend(b);
        Ok(())
    }

    fn link(&mut self, v: &Value, name: &str) -> Result<()> {
        match v {
            Value::Null => {}
            Value::String(_) => {
                self.links.insert(self.out.len() as u32, v.clone());
            }
            _ => return Err(Error::tac(format!("{}/{name}: a link (string) or null", self.path))),
        }
        self.word(0);
        Ok(())
    }

    fn any(&mut self, v: &Value, name: &str) -> Result<()> {
        if v.is_string() || v.is_null() { self.link(v, name) } else { self.num(v, name) }
    }

    fn lst<'v>(&self, v: &'v Value, name: &str) -> Result<&'v Vec<Value>> {
        v.as_array().ok_or_else(|| Error::tac(format!("{}/{name}: a list", self.path)))
    }

    fn items(&mut self, its: &Value, o: &Value, sub: &str) -> Result<()> {
        let Some(m) = o.as_object() else {
            return Err(Error::tac(format!("{sub}: an object")));
        };
        let its = its.as_array().map(Vec::as_slice).unwrap_or(&[]);
        let mut known: Vec<String> = its.iter().map(|it| item_str(it, 1)).collect();
        if sub == self.path {
            known.push("tail".into());
        }
        for k in m.keys() {
            if !known.contains(k) {
                let mut ks = known.clone();
                ks.sort();
                ks.dedup();
                return Err(Error::tac(format!("{sub}/{k}: not a parameter of this kind (has: {})", ks.join(", "))));
            }
        }
        for it in its {
            let k = item_str(it, 0);
            let name = item_str(it, 1);
            let Some(v) = m.get(&name) else {
                return Err(Error::tac(format!("{sub}/{name}: missing")));
            };
            match k.as_str() {
                "u32" | "s32" => self.num(v, &name)?,
                "f32" => self.flt(v, &name)?,
                "vec3" => {
                    let l = self.lst(v, &name)?;
                    if l.len() != 3 {
                        return Err(Error::tac(format!("{sub}/{name}: [x, y, z]")));
                    }
                    for (i, x) in l.iter().enumerate() {
                        self.flt(x, &format!("{name}/{i}"))?;
                    }
                }
                "switch" => {
                    let en = item_str(it, 2);
                    let kind = v.as_object().and_then(|x| x.get("kind")).and_then(Value::as_str);
                    let Some(kind) = kind else {
                        return Err(Error::tac(format!("{sub}/{name}: {{\"kind\": a {en} name, ...}}")));
                    };
                    let empty = Map::new();
                    let cs = self.t.cases.get(&en).and_then(Value::as_object).unwrap_or(&empty);
                    let hit = cs.iter().find(|(_, c)| c["kind"].as_str() == Some(kind));
                    let Some((val, c)) = hit else {
                        let all: Vec<String> = cs.values().map(|c| py::str_of(&c["kind"])).collect();
                        return Err(Error::tac(format!(
                            "{sub}/{name}/kind: {} is no {en} ({})",
                            py::str_repr(kind),
                            all.join(", ")
                        )));
                    };
                    let val: i128 = val.parse().unwrap_or(0);
                    let c = c.clone();
                    self.word(val);
                    let rest: Map<String, Value> =
                        v.as_object().unwrap().iter().filter(|(a, _)| a.as_str() != "kind").map(|(a, b)| (a.clone(), b.clone())).collect();
                    self.items(&c["items"], &Value::Object(rest), &format!("{sub}/{name}"))?;
                }
                "token" => {
                    let w = self.t.token_value(v, &format!("{sub}/{name}"))?;
                    self.word(w);
                }
                "any" => self.any(v, &name)?,
                "link" => self.link(v, &name)?,
                "list" => {
                    let l = self.lst(v, &name)?.clone();
                    self.word(l.len() as i128);
                    for (i, x) in l.iter().enumerate() {
                        self.link(x, &format!("{name}/{i}"))?;
                    }
                }
                "scratch" => {
                    let l = self.lst(v, &name)?.clone();
                    let of = item_str(it, 2);
                    let n = match m.get(&of) {
                        Some(Value::Array(a)) => a.len(),
                        Some(x) => return Err(Error::uncaught(format!("object of type '{}' has no len()", py::type_name(x)))),
                        None => return Err(Error::uncaught(format!("KeyError: {}", py::str_repr(&of)))),
                    };
                    if l.len() != n {
                        return Err(Error::tac(format!("{sub}/{name}: one word per {of} ({n})")));
                    }
                    for (i, x) in l.iter().enumerate() {
                        self.num(x, &format!("{name}/{i}"))?;
                    }
                }
                "operand" => {
                    let bad = || {
                        Error::tac(format!(
                            "{sub}/{name}: {{\"const\": x}}, {{\"variable\": link}} or {{\"code\": id, \"arg\": w}}"
                        ))
                    };
                    let Some(x) = v.as_object() else { return Err(bad()) };
                    if !(x.len() == 1 || x.len() == 2) {
                        return Err(bad());
                    }
                    let ov = |n: &str| self.t.operand_val.get(n).copied().unwrap_or(0) as i128;
                    if let Some(c) = x.get("const") {
                        let w = ov("constant");
                        self.word(w);
                        self.flt(c, &name)?;
                    } else if let Some(l) = x.get("variable") {
                        let w = ov("variable");
                        self.word(w);
                        self.link(l, &name)?;
                    } else if let (Some(c), Some(a)) = (x.get("code"), x.get("arg")) {
                        let w = ov("code_variable");
                        self.word(w);
                        self.num(c, &name)?;
                        self.any(a, &name)?;
                    } else {
                        let mut ks: Vec<String> = x.keys().map(|k| py::str_repr(k)).collect();
                        ks.sort_by(|a, b| a[1..a.len() - 1].cmp(&b[1..b.len() - 1]));
                        return Err(Error::tac(format!("{sub}/{name}: unknown operand [{}]", ks.join(", "))));
                    }
                }
                "codevar" => {
                    let x = v.as_object().filter(|x| x.contains_key("var")).ok_or_else(|| {
                        Error::tac(format!("{sub}/{name}: {{\"var\": id, \"cars\": [...]}} or {{\"var\": id, \"arg\": w}}"))
                    })?;
                    let var = &x["var"];
                    self.num(var, &name)?;
                    let keys: HashSet<&str> = x.keys().map(String::as_str).collect();
                    if Num::of(var).unwrap().py_lt(Num::Int(100)) {
                        if keys != HashSet::from(["var", "cars"]) {
                            return Err(Error::tac(format!("{sub}/{name}: a code variable below 100 takes \"cars\"")));
                        }
                        let cars = self.lst(&x["cars"], &name)?.clone();
                        self.word(cars.len() as i128);
                        for (i, c) in cars.iter().enumerate() {
                            self.link(c, &format!("{name}/cars/{i}"))?;
                        }
                    } else {
                        if keys != HashSet::from(["var", "arg"]) {
                            return Err(Error::tac(format!("{sub}/{name}: a code variable from 100 takes \"arg\"")));
                        }
                        self.any(&x["arg"], &name)?;
                    }
                }
                "string" => {
                    let Some(s) = v.as_str() else {
                        return Err(Error::tac(format!("{sub}/{name}: a string")));
                    };
                    if !s.is_ascii() {
                        return Err(Error::tac(format!("{sub}/{name}: the game's strings are ASCII")));
                    }
                    let mut b = s.as_bytes().to_vec();
                    b.push(0);
                    while b.len() % 4 != 0 {
                        b.push(0);
                    }
                    self.out.extend(b);
                }
                "table" => {
                    let l = self.lst(v, &name)?.clone();
                    let recn = item_str(it, 2);
                    let rec = self.t.records.get(&recn).cloned().ok_or_else(|| Error::uncaught(format!("KeyError: {}", py::str_repr(&recn))))?;
                    self.word(l.len() as i128);
                    for (i, x) in l.iter().enumerate() {
                        self.items(&rec, x, &format!("{sub}/{name}/{i}"))?;
                    }
                }
                "words" => {
                    let l = self.lst(v, &name)?.clone();
                    for (i, x) in l.iter().enumerate() {
                        self.any(x, &format!("{name}/{i}"))?;
                    }
                }
                _ => return Err(Error::tac(format!("{sub}: item {k}"))),
            }
        }
        Ok(())
    }
}

/// (bytes, {offset: link}) of a parameter list.
pub fn encode_params(t: &Tac, obj: &Value, items: &Value, big: bool, path: &str) -> Result<(Vec<u8>, BTreeMap<u32, Value>)> {
    if !obj.is_object() {
        return Err(Error::tac(format!("{path}: the parameters are an object")));
    }
    let mut e = Enc { t, big, out: vec![], links: BTreeMap::new(), path: path.into() };
    e.items(items, obj, path)?;
    match obj.get("tail") {
        Some(Value::Array(l)) => {
            for (i, x) in l.iter().enumerate() {
                e.any(x, &format!("tail/{i}"))?;
            }
        }
        Some(x) => {
            let b = x.as_str().and_then(fromhex).ok_or_else(|| Error::tac(format!("{path}/tail: hex bytes, or words")))?;
            e.out.extend(b);
        }
        None => {}
    }
    Ok((e.out, e.links))
}

// ------------------------------------------------------------------------------------------
// records (Trigger, TriggerAction): their scalar fields

fn slot_in(r: &Reader, o: u32, size: u32) -> bool {
    let mut x = o - o % 4;
    while x < o + size {
        if x + 4 > o && r.slots.contains_key(&x) {
            return true;
        }
        x += 4;
    }
    false
}

/// A record's non-link field, by its C type: names for x-enum / x-flags.
pub fn decode_scalar(_t: &Tac, r: &Reader, p: &Value, off: u32, path: &str) -> Result<Value> {
    let (base, dims) = schema::parse_ctype(schema::ctype(p));
    let pr = schema::prim(&base).ok_or_else(|| Error::uncaught(format!("KeyError: {}", py::str_repr(&base))))?;
    let n: usize = dims.iter().product();
    let mut vals = vec![];
    for i in 0..n {
        let o = off + (i * pr.size) as u32;
        if slot_in(r, o, pr.size as u32) {
            return Err(Error::tac(format!("{path}: a number on a pointer slot")));
        }
        vals.push(match decode::read_prim(r.blob, o, pr, r.big)? {
            Raw::I(v) => Value::from(v),
            // a plain Python float (not Num): json.dumps writes its repr
            Raw::F(f) => serde_json::Number::from_f64(f).map(Value::Number).unwrap_or(Value::Null),
        });
    }
    if !dims.is_empty() {
        return Ok(Value::Array(vals));
    }
    let v = vals.remove(0);
    if let (Some(fl), Some(iv)) = (p.get("x-flags").and_then(Value::as_object), v.as_i64()) {
        let mut names = vec![];
        let mut rest = iv;
        for (bit, nm) in fl {
            let b: i64 = bit.parse().unwrap_or(0);
            if iv & b != 0 {
                names.push(nm.clone());
                rest &= !b;
            }
        }
        if rest != 0 {
            names.push(Value::from(rest));
        }
        return Ok(Value::Array(names));
    }
    Ok(Tac::name_of(p, &v))
}

pub fn encode_scalar(t: &Tac, p: &Value, v: &Value, big: bool, path: &str) -> Result<Vec<u8>> {
    let (base, dims) = schema::parse_ctype(schema::ctype(p));
    let pr = schema::prim(&base).ok_or_else(|| Error::uncaught(format!("KeyError: {}", py::str_repr(&base))))?;
    if !dims.is_empty() {
        let n: usize = dims.iter().product();
        let Some(l) = v.as_array().filter(|l| l.len() == n) else {
            return Err(Error::tac(format!("{path}: {n} values")));
        };
        let q = serde_json::json!({ "x-c-type": base });
        let mut out = vec![];
        for (i, x) in l.iter().enumerate() {
            out.extend(encode_scalar(t, &q, x, big, &format!("{path}/{i}"))?);
        }
        return Ok(out);
    }
    let v = if let Some(fl) = p.get("x-flags").and_then(Value::as_object) {
        let Some(l) = v.as_array() else {
            return Err(Error::tac(format!("{path}: a list of flag names")));
        };
        let mut w: i128 = 0;
        for x in l {
            if let Some(s) = x.as_str() {
                match fl.iter().find(|(_, nm)| nm.as_str() == Some(s)) {
                    Some((k, _)) => w |= k.parse::<i128>().unwrap_or(0),
                    None => return Err(Error::tac(format!("{path}: unknown flag {}", py::str_repr(s)))),
                }
            } else if is_int(x) {
                if let Some(Num::Int(i)) = Num::of(x) {
                    w |= i;
                }
            } else {
                return Err(Error::tac(format!("{path}: flag names or integers")));
            }
        }
        decode::int_value(w)
    } else {
        Tac::value_of(p, v, path)?
    };
    let r = if pr.float {
        encode::pack_float(pr.code, Num::of(&v).map(Num::f64).unwrap_or(0.0), big)
    } else {
        let Some(Num::Int(i)) = Num::of(&v) else {
            return Err(Error::tac(format!("{path}: {}: required argument is not an integer", py::str_of(&v))));
        };
        encode::pack_int(pr.code, i, big).map_err(|e| Error::tac(format!("{path}: {i}: {e}")))
    };
    r
}

// ------------------------------------------------------------------------------------------
// a trigger with its parts, as one JSON object (xdl_graph.py's trigger files)

fn steps(rec: &str, off: u32) -> Option<&'static str> {
    match (rec, off) {
        ("Trigger", 0x4) => Some("param"),
        ("Trigger", 0xC) => Some("conditions"),
        ("Trigger", 0x14) => Some("actions"),
        ("Trigger", 0x18) => Some("elseActions"),
        ("TriggerAction", 0x4) => Some("data"),
        _ => None,
    }
}

fn array_of(key: &str) -> &'static str {
    if key == "conditions" { "Trigger" } else { "TriggerAction" }
}

/// The id step of a pointer slot `rel` bytes into a TAC node: a trigger record ("param",
/// "actions"...) or an array of `count` records ("3/data").
pub fn step_of(role: &str, count: Option<u32>, rel: u32) -> Option<String> {
    if role == "Trigger" {
        return steps("Trigger", rel).map(str::to_string);
    }
    let size = if role == "conditions" { 0x2C } else { 0x18 };
    let rec = if role == "conditions" { "Trigger" } else { "TriggerAction" };
    let st = steps(rec, rel % size)?;
    // count None: the Python compares an int with None (TypeError); arrays always have one
    if rel / size < count.unwrap_or(0) { Some(format!("{}/{st}", rel / size)) } else { None }
}

/// {offset: (role, count)}: the trigger records of the pack's table, and the condition and
/// action arrays they point to (conditions nest: their records are walked too).
pub fn tac_shapes(
    u32_: &dyn Fn(u32) -> Result<u32>,
    ptr: &BTreeMap<u32, u32>,
    header: &Value,
    n_blob: u32,
) -> Result<BTreeMap<u32, (String, Option<u32>)>> {
    let mut out = BTreeMap::new();
    fn trig(
        t: u32,
        depth: u32,
        u32_: &dyn Fn(u32) -> Result<u32>,
        ptr: &BTreeMap<u32, u32>,
        n_blob: u32,
        out: &mut BTreeMap<u32, (String, Option<u32>)>,
    ) -> Result<()> {
        out.entry(t).or_insert(("Trigger".into(), None));
        for (lo, no, role) in [(0xC, 0x10, "conditions"), (0x14, 0x1C, "actions"), (0x18, 0x20, "elseActions")] {
            let a = ptr.get(&(t + lo)).copied();
            let n = u32_(t + no)?;
            let Some(a) = a else { continue };
            if n == 0 || n > 0x1000 {
                continue;
            }
            let size = if role == "conditions" { 0x2C } else { 0x18 };
            if a as u64 + n as u64 * size > n_blob as u64 {
                continue;
            }
            out.entry(a).or_insert((role.into(), Some(n)));
            if role == "conditions" && depth < 4 {
                for j in 0..n {
                    trig(a + j * size as u32, depth + 1, u32_, ptr, n_blob, out)?;
                }
            }
        }
        Ok(())
    }
    let n = header.get("num_triggers").and_then(Value::as_u64).unwrap_or(0);
    let base = header.get("trigger_offset").and_then(Value::as_u64).unwrap_or(0);
    for i in 0..n {
        let o = base + i * 0x2C;
        if o + 0x2C <= n_blob as u64 {
            trig(o as u32, 0, u32_, ptr, n_blob, &mut out)?;
        }
    }
    Ok(out)
}

/// A node of the host's graph, as Extract sees it.
#[derive(Debug, Clone)]
pub struct TacNode {
    pub start: u32,
    pub end: u32,
    /// the parent node's handle
    pub parent: Option<usize>,
    /// kind == "raw"
    pub raw: bool,
    pub fixed: bool,
    pub label: Option<String>,
}

/// What Extract needs of xdl_graph's Extractor (`ex`): the pack (src) and its nodes.
pub trait TacHost {
    fn blob(&self) -> &[u8];
    fn big(&self) -> bool;
    /// src.ptr: pointer slot offset -> target
    fn ptr(&self) -> &BTreeMap<u32, u32>;
    /// src.rel[kind] for "ref", "texture", "sound", "shader"
    fn rel(&self, kind: &str) -> Option<&BTreeMap<u32, u32>>;
    /// ex.link_to(t, elem=True)
    fn link_to_elem(&self, t: u32) -> Result<String>;
    /// ex.by_start.get(t)
    fn by_start(&self, t: u32) -> Option<usize>;
    fn node(&self, h: usize) -> &TacNode;
    /// label in ex.kinds
    fn is_kind(&self, label: &str) -> bool;
}

/// One trigger table record and the nodes it owns, as JSON. A part is inlined when it is a node
/// of its own whose canonical parent is the part pointing to it, and decodes; otherwise it stays
/// a link.
pub struct Extract<'a> {
    pub t: &'a Tac,
    pub ex: &'a dyn TacHost,
    /// node handles now written in the trigger's file
    pub parts: Vec<usize>,
    /// node start -> zero bytes after its JSON
    pub pads: BTreeMap<u32, u32>,
}

impl<'a> Extract<'a> {
    pub fn new(t: &'a Tac, ex: &'a dyn TacHost) -> Self {
        Extract { t, ex, parts: vec![], pads: BTreeMap::new() }
    }

    pub fn reader(&self, nd: &TacNode) -> Result<Reader<'a>> {
        let mut slots = HashMap::new();
        for (&o, &t) in self.ex.ptr().range(nd.start..nd.end.max(nd.start)) {
            slots.insert(o, Value::String(self.ex.link_to_elem(t)?));
        }
        if self.ex.rel("ref").is_some_and(|r| r.range(nd.start..nd.end.max(nd.start)).next().is_some()) {
            return Err(Error::tac("a geometry ref in a parameter list"));
        }
        for k in ["texture", "sound", "shader"] {
            if self.ex.rel(k).is_some_and(|r| r.range(nd.start..nd.end.max(nd.start)).next().is_some()) {
                return Err(Error::tac(format!("a {k} slot in a TAC node")));
            }
        }
        Ok(Reader { blob: self.ex.blob(), big: self.ex.big(), slots })
    }

    /// The node at the pointer in slot `off`, if it's `parent`'s to inline.
    pub fn own(&self, parent: usize, off: u32) -> Option<usize> {
        let t = *self.ex.ptr().get(&off)?;
        let h = self.ex.by_start(t)?;
        let nd = self.ex.node(h);
        let kinded = nd.label.as_deref().is_some_and(|l| self.ex.is_kind(l) && l != "Trigger" && l != "Action[](TAC)");
        if nd.parent != Some(parent) || !nd.raw || nd.fixed || kinded {
            return None;
        }
        Some(h)
    }

    /// The JSON of a trigger table record node.
    pub fn trigger(&mut self, h: usize) -> Result<Value> {
        self.parts.push(h);
        let start = self.ex.node(h).start;
        self.record("Trigger", h, start, h).map(Value::Object)
    }

    pub fn zero_tail(&mut self, h: usize, used: u32) -> bool {
        let nd = self.ex.node(h);
        if (nd.end as i64 - nd.start as i64) < used as i64 {
            return false;
        }
        let blob = self.ex.blob();
        let lo = ((nd.start + used) as usize).min(blob.len());
        if blob[lo..(nd.end as usize).clamp(lo, blob.len())].iter().any(|&b| b != 0) {
            return false;
        }
        if self.ex.ptr().range(nd.start + used..nd.end.max(nd.start + used)).next().is_some() {
            return false;
        }
        if nd.end - nd.start > used {
            self.pads.insert(nd.start, nd.end - nd.start - used);
        }
        true
    }

    fn u32(&self, o: u32) -> Result<u32> {
        pack::u32_at(self.ex.blob(), o, self.ex.big())
    }

    fn null_link(&self, o: u32, key: &str) -> Result<Value> {
        let w = self.u32(o)?;
        if w != 0 {
            return Err(Error::tac(format!("{key}: 0x{w:X} without relocation")));
        }
        Ok(Value::Null)
    }

    fn slot_or_null(&self, r: &Reader, o: u32, key: &str) -> Result<Value> {
        match r.slots.get(&o) {
            Some(v) => Ok(v.clone()),
            None => self.null_link(o, key),
        }
    }

    /// A Trigger / TriggerAction record at `off` (inside `holder`, the node whose children its
    /// parts are).
    pub fn record(&mut self, sname: &str, holder: usize, off: u32, node: usize) -> Result<Map<String, Value>> {
        let t = self.t;
        let r = self.reader(&self.ex.node(node).clone())?;
        let mut out = Map::new();
        let props: Vec<(String, Value)> = t.fields(sname).into_iter().map(|(k, p)| (k.clone(), p.clone())).collect();
        let mut inline_counts: Vec<String> = vec![];
        for (key, p) in &props {
            let o = off + schema::offset(p)?;
            if let Some(xp) = p.get("x-params") {
                let kind = out.get("type").cloned().ok_or_else(|| Error::uncaught("KeyError: 'type'"))?;
                let en = py::str_of(&xp["enum"]);
                let items = if kind.is_string() { t.variants.get(&en).and_then(|v| v.get(kind.as_str().unwrap())).cloned() } else { None };
                let child = self.own(holder, o);
                if let (Some(c), Some(items)) = (child, items) {
                    let cn = self.ex.node(c).clone();
                    let res = self.reader(&cn).and_then(|pr| decode_params(t, &pr, cn.start, cn.end, &items, key));
                    match res {
                        Ok((mut val, _)) => {
                            if let Some(Value::String(tail)) = val.get("tail") {
                                let zero = fromhex(tail).is_some_and(|b| b.iter().all(|&x| x == 0));
                                if zero {
                                    let n = tail.chars().count() as u32 / 2;
                                    val.as_object_mut().unwrap().shift_remove("tail");
                                    self.pads.insert(cn.start, n);
                                }
                            }
                            out.insert(key.clone(), val);
                            self.parts.push(c);
                            continue;
                        }
                        Err(e) if e.kind == Kind::Tac => {}
                        Err(e) => return Err(e),
                    }
                }
                out.insert(key.clone(), self.slot_or_null(&r, o, key)?);
            } else if let Some(xl) = p.get("x-link").filter(|_| p.get("x-inline").is_some_and(truthy)) {
                let child = self.own(holder, o);
                let cnt_key = py::str_of(&xl["count"]);
                let cnt_p = t.structs[sname]["properties"].get(&cnt_key).ok_or_else(|| Error::uncaught(format!("KeyError: {}", py::str_repr(&cnt_key))))?;
                let n = self.u32(off + schema::offset(cnt_p)?)?;
                let rec = array_of(key);
                let size = t.size[rec];
                if let Some(c) = child {
                    let cn = self.ex.node(c).clone();
                    if n != 0 && (cn.end as u64).saturating_sub(cn.start as u64) >= n as u64 * size as u64 && self.zero_tail(c, n * size) {
                        let mark = self.parts.len();
                        self.parts.push(c);
                        let mut recs = vec![];
                        let mut failed = None;
                        for i in 0..n {
                            match self.record(rec, c, cn.start + i * size, c) {
                                Ok(x) => recs.push(Value::Object(x)),
                                Err(e) => {
                                    failed = Some(e);
                                    break;
                                }
                            }
                        }
                        match failed {
                            None => {
                                out.insert(key.clone(), Value::Array(recs));
                                inline_counts.push(cnt_key);
                                continue;
                            }
                            Some(e) if e.kind == Kind::Tac => {
                                self.parts.truncate(mark);
                                self.pads.remove(&cn.start);
                            }
                            Some(e) => return Err(e),
                        }
                    }
                }
                out.insert(key.clone(), self.slot_or_null(&r, o, key)?);
            } else if p.get("x-link").is_some() {
                out.insert(key.clone(), self.slot_or_null(&r, o, key)?);
            } else {
                out.insert(key.clone(), decode_scalar(t, &r, p, o, key)?);
            }
        }
        for c in &inline_counts {
            out.shift_remove(c);
        }
        let blob = self.ex.blob();
        for (key, p) in &props {
            if key == "type" || !out.contains_key(key) {
                continue;
            }
            let o = off + schema::offset(p)?;
            let (base, dims) = schema::parse_ctype(schema::ctype(p));
            let mut n = if p.get("x-link").is_none() && p.get("x-params").is_none() {
                schema::prim(&base).map(|x| x.size as u32).ok_or_else(|| Error::uncaught(format!("KeyError: {}", py::str_repr(&base))))?
            } else {
                4
            };
            for d in dims {
                n *= d as u32;
            }
            let lo = (o as usize).min(blob.len());
            let zero = blob[lo..((o + n) as usize).clamp(lo, blob.len())].iter().all(|&b| b == 0);
            let mut slot = false;
            let mut x = o - o % 4;
            while x < o + n {
                if o <= x && self.ex.ptr().contains_key(&x) {
                    slot = true;
                }
                x += 4;
            }
            if zero && !slot {
                out.shift_remove(key);
            }
        }
        Ok(out)
    }
}

fn truthy(v: &Value) -> bool {
    !matches!(v, Value::Null | Value::Bool(false)) && v != &Value::from(0) && v.as_str() != Some("")
}

/// One part of an encoded trigger: (node id, bytes, {offset: link}).
pub type Part = (String, Vec<u8>, BTreeMap<u32, Value>);

/// The parts of a trigger file: the record first, then each inlined part in the order the
/// extract finds them (the record, its parameters, its condition array and each condition's
/// parts, its actions and each action's data, its else-actions).
pub fn encode_trigger(t: &Tac, doc: &Value, tid: &str, big: bool, path: &str) -> Result<Vec<Part>> {
    struct Later {
        params: bool,
        cid: String,
        v: Value,
        /// params: the layout; array: the record struct name
        x: Value,
        sub: String,
    }
    struct Ctx<'a> {
        t: &'a Tac,
        big: bool,
        path: &'a str,
        parts: Vec<Part>,
    }

    fn put_link(v: &Value, o: u32, links: &mut BTreeMap<u32, Value>, sub: &str) -> Result<()> {
        match v {
            Value::Null => Ok(()),
            Value::String(_) => {
                links.insert(o, v.clone());
                Ok(())
            }
            _ => Err(Error::tac(format!("{sub}: a link or null (or the list itself)"))),
        }
    }

    #[allow(clippy::too_many_arguments)]
    fn record(
        c: &Ctx,
        sname: &str,
        obj: &Value,
        at: u32,
        buf: &mut [u8],
        links: &mut BTreeMap<u32, Value>,
        holder_id: &str,
        prefix: &str,
        sub: &str,
    ) -> Result<Vec<Later>> {
        let t = c.t;
        let Some(m) = obj.as_object() else {
            return Err(Error::tac(format!("{sub}: an object")));
        };
        let props = t.fields(sname);
        let top = sub == c.path;
        for k in m.keys() {
            let known = props.iter().any(|(pk, _)| *pk == k) || (top && ["$schema", "format_version", "id", "children"].contains(&k.as_str()));
            if !known {
                return Err(Error::tac(format!("{sub}/{k}: not a field of {sname}")));
            }
        }
        let kind = m.get("type").cloned().unwrap_or(Value::Null);
        let mut later = vec![];
        let mut counts: HashMap<String, usize> = HashMap::new();
        for (key, p) in &props {
            if let Some(xl) = p.get("x-link")
                && p.get("x-inline").is_some_and(truthy)
                && let Some(Value::Array(a)) = m.get(*key)
            {
                counts.insert(py::str_of(&xl["count"]), a.len());
            }
        }
        for (key, p) in &props {
            let po = schema::offset(p)?;
            let o = (at + po) as usize;
            if let Some(&n) = counts.get(*key) {
                let w = n as u32;
                buf[o..o + 4].copy_from_slice(&if c.big { w.to_be_bytes() } else { w.to_le_bytes() });
                continue;
            }
            let Some(v) = m.get(*key) else {
                if key.as_str() != "type" {
                    continue; // zero
                }
                return Err(Error::tac(format!("{sub}/{key}: missing")));
            };
            let step = steps(sname, po).map(|s| format!("{prefix}{s}"));
            let step_s = step.clone().unwrap_or_else(|| "None".into());
            if let Some(xp) = p.get("x-params") {
                if v.is_object() {
                    let en = py::str_of(&xp["enum"]);
                    let lay = kind.as_str().and_then(|k| t.variants.get(&en).and_then(|x| x.get(k)));
                    let Some(lay) = lay else {
                        return Err(Error::tac(format!("{sub}/type: {} has no parameter layout (use its name)", py::repr(&kind))));
                    };
                    let cid = format!("{holder_id}/{step_s}");
                    links.insert(o as u32, Value::String(cid.clone()));
                    later.push(Later { params: true, cid, v: v.clone(), x: lay.clone(), sub: format!("{sub}/{key}") });
                } else {
                    put_link(v, o as u32, links, &format!("{sub}/{key}"))?;
                }
            } else if p.get("x-link").is_some() && p.get("x-inline").is_some_and(truthy) && v.is_array() {
                if v.as_array().unwrap().is_empty() {
                    return Err(Error::tac(format!("{sub}/{key}: an empty list (use null)")));
                }
                let cid = format!("{holder_id}/{step_s}");
                links.insert(o as u32, Value::String(cid.clone()));
                later.push(Later { params: false, cid, v: v.clone(), x: Value::String(array_of(key).into()), sub: format!("{sub}/{key}") });
            } else if p.get("x-link").is_some() {
                put_link(v, o as u32, links, &format!("{sub}/{key}"))?;
            } else {
                let b = encode_scalar(t, p, v, c.big, &format!("{sub}/{key}"))?;
                buf[o..o + b.len()].copy_from_slice(&b);
            }
        }
        Ok(later)
    }

    fn expand(c: &mut Ctx, later: Vec<Later>) -> Result<()> {
        for l in later {
            if l.params {
                let (b, ln) = encode_params(c.t, &l.v, &l.x, c.big, &l.sub)?;
                c.parts.push((l.cid, b, ln));
            } else {
                let rec = py::str_of(&l.x);
                let size = c.t.size[&rec];
                let items = l.v.as_array().unwrap();
                let mut buf = vec![0u8; size as usize * items.len()];
                let mut links = BTreeMap::new();
                let mut more = vec![];
                for (i, item) in items.iter().enumerate() {
                    more.extend(record(c, &rec, item, i as u32 * size, &mut buf, &mut links, &l.cid, &format!("{i}/"), &format!("{}/{i}", l.sub))?);
                }
                c.parts.push((l.cid, buf, links));
                expand(c, more)?;
            }
        }
        Ok(())
    }

    let mut c = Ctx { t, big, path, parts: vec![] };
    let mut buf = vec![0u8; t.size["Trigger"] as usize];
    let mut links = BTreeMap::new();
    let later = record(&c, "Trigger", doc, 0, &mut buf, &mut links, tid, "", path)?;
    c.parts.push((tid.to_string(), buf, links));
    expand(&mut c, later)?;
    Ok(c.parts)
}
