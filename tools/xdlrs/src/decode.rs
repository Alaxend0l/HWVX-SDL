//! Pack bytes -> JSON (xdl_struct.Decoder) and object links -> offsets (xdl_struct.Resolver).

use crate::error::{Error, Result};
use crate::pack::{self, BaseLink, Pack, Pairing};
use crate::py;
use crate::schema::{self, Schemas};
use serde_json::{Map, Value};
use std::collections::HashMap;

/// What a 4-byte relocation slot is.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Slot {
    Pointer,
    Texture,
    Ref,
    Sound,
}

impl Slot {
    pub fn name(self) -> &'static str {
        match self {
            Slot::Pointer => "pointer",
            Slot::Texture => "texture",
            Slot::Ref => "ref",
            Slot::Sound => "sound",
        }
    }
}

/// How a decoder names a link once the slot is checked (xdl_graph's GraphDecoder overrides
/// these; `None` on the Decoder is xdl_struct's own naming).
pub trait LinkNamer {
    /// an object pointer at `off` holding `raw` (xdl_struct: the field path itself)
    fn pointer(&self, pk: &Pack, off: u32, raw: u32, path: &str) -> Result<Value>;
    /// a texture slot whose relocation value is `value` (share id << 16 | index)
    fn texture(&self, pk: &Pack, value: u32, path: &str) -> Result<Value>;
    /// a geometry (a local ref reloc of value `value`, its high half 0) at `off` holding `raw`
    fn geometry(&self, pk: &Pack, off: u32, value: u32, raw: u32, path: &str) -> Result<Value>;
}

pub struct Decoder<'a> {
    pub s: &'a Schemas,
    pub pk: &'a Pack,
    pub pair: Option<&'a Pairing>,
    /// offset -> kind of every 4-byte reloc slot (pointers first, then texture, ref, sound)
    pub slots: HashMap<u32, Slot>,
    pub namer: Option<&'a dyn LinkNamer>,
}

impl<'a> Decoder<'a> {
    pub fn new(s: &'a Schemas, pk: &'a Pack, pair: Option<&'a Pairing>) -> Decoder<'a> {
        let mut slots = HashMap::new();
        for &o in &pk.ptrs {
            slots.insert(o, Slot::Pointer);
        }
        for &o in pk.tex.keys() {
            slots.entry(o).or_insert(Slot::Texture);
        }
        for &o in pk.refs.keys() {
            slots.entry(o).or_insert(Slot::Ref);
        }
        for &o in pk.snd.keys() {
            slots.entry(o).or_insert(Slot::Sound);
        }
        Decoder { s, pk, pair, slots, namer: None }
    }

    pub fn with_namer(mut self, n: &'a dyn LinkNamer) -> Self {
        self.namer = Some(n);
        self
    }

    /// A struct at `base` as a JSON object; `oid` is its id (the paths of its links).
    pub fn struct_(&self, sch: &Value, base: u32, oid: &str) -> Result<Map<String, Value>> {
        let mut out = Map::new();
        for (key, p) in schema::fields(sch) {
            let off = base.wrapping_add(schema::offset(p)?);
            out.insert(key.clone(), self.value(p, off, &format!("{oid}/{key}"), schema::ctype(p))?);
        }
        Ok(out)
    }

    pub fn value(&self, p: &Value, off: u32, path: &str, ctype: &str) -> Result<Value> {
        let (base, dims) = schema::parse_ctype(ctype);
        if !dims.is_empty() {
            let sub = schema::sub_ctype(&base, &dims);
            let esz = self.s.size_of(&sub)? as u32;
            let mut v = Vec::with_capacity(dims[0]);
            for i in 0..dims[0] as u32 {
                v.push(self.value(&p["items"], off.wrapping_add(i * esz), &format!("{path}/{i}"), &sub)?);
            }
            return Ok(Value::Array(v));
        }
        if let Some(x) = p.get("x-link") {
            return self.link(x, off, path);
        }
        if p.get("$ref").is_some() {
            let sch = self.s.ref_(p).ok_or_else(|| Error::uncaught(format!("{path}: no schema for {}", py::str_of(&p["$ref"]))))?;
            return Ok(Value::Object(self.struct_(sch, off, path)?));
        }
        let Some(pr) = schema::prim(&base) else {
            return Err(Error::xdl(format!("{path}: no decoder for {ctype}")));
        };
        let mut o = off - off % 4;
        while (o as u64) < off as u64 + pr.size as u64 {
            if let Some(k) = self.slots.get(&o)
                && o + 4 > off
            {
                return Err(Error::xdl(format!(
                    "{path} (0x{off:X}, {ctype}) is a {} slot in {}/{}: the schema should make it a link (@xdl marker)",
                    k.name(),
                    self.pk.archive,
                    self.pk.name
                )));
            }
            o += 4;
        }
        let raw = read_prim(&self.pk.blob, off, pr, self.pk.big)?;
        match raw {
            Raw::F(v) => py::float_value(v, pr.code == 'd'),
            Raw::I(v) => Ok(named(p, v)),
        }
    }

    pub fn link(&self, x: &Value, off: u32, path: &str) -> Result<Value> {
        let pk = self.pk;
        let kind = self.slots.get(&off).copied();
        let raw = pk.u32(off)?;
        let xk = py::str_of(&x["kind"]);
        let want = match xk.as_str() {
            "object" => Slot::Pointer,
            "texture" => Slot::Texture,
            "geometry" => Slot::Ref,
            _ => return Err(Error::uncaught(format!("KeyError: {}", py::str_repr(&xk)))),
        };
        let Some(kind) = kind else {
            if raw == 0 {
                return Ok(Value::Null);
            }
            return Err(Error::xdl(format!("{path}: a {xk} link, but 0x{off:X} holds 0x{raw:X} and no relocation")));
        };
        if kind != want {
            return Err(Error::xdl(format!("{path}: a {xk} link, but 0x{off:X} is a {} slot", kind.name())));
        }
        if kind == Slot::Pointer {
            return match self.namer {
                Some(n) => n.pointer(pk, off, raw, path),
                None => Ok(Value::String(path.into())),
            };
        }
        if kind == Slot::Texture {
            let v = pk.tex[&off];
            if let Some(n) = self.namer {
                return n.texture(pk, v, path);
            }
            let (sid, idx) = (v >> 16, v & 0xFFFF);
            let (arch, pname) = if sid == 0 {
                (pk.archive.clone(), pk.name.clone())
            } else {
                let s = pack::share(sid).ok_or_else(|| Error::uncaught(format!("KeyError: {sid}")))?;
                (s.0.to_string(), s.1.to_string())
            };
            return Ok(Value::String(self.pairing()?.id(&pk.release, &arch, &pname, "texture", idx as i64)?));
        }
        let v = pk.refs[&off];
        if v >> 16 != 0 {
            return Err(Error::xdl(format!("{path}: geometry from another pack (ref 0x{v:X}) not supported yet")));
        }
        if let Some(n) = self.namer {
            return n.geometry(pk, off, v, raw, path);
        }
        let g = pk.geometries();
        let idx = (v & 0xFFFF) as usize;
        if idx >= g.len() || g[idx] != raw {
            return Err(Error::xdl(format!("{path}: ref reloc {idx} does not match the geometry scan")));
        }
        Ok(Value::String(self.pairing()?.id(&pk.release, &pk.archive, &pk.name, "geometry", idx as i64)?))
    }

    fn pairing(&self) -> Result<&Pairing> {
        self.pair.ok_or_else(|| Error::uncaught("'NoneType' object has no attribute 'id'"))
    }
}

pub(crate) enum Raw {
    I(i64),
    F(f64),
}

pub(crate) fn read_prim(blob: &[u8], off: u32, pr: schema::Prim, big: bool) -> Result<Raw> {
    let o = off as usize;
    if o + pr.size > blob.len() {
        return Err(pack::short(pr.size, o, blob.len()));
    }
    let mut b = [0u8; 8];
    b[..pr.size].copy_from_slice(&blob[o..o + pr.size]);
    if big {
        b[..pr.size].reverse();
    }
    // b is little-endian now
    Ok(match pr.code {
        'B' => Raw::I(b[0] as i64),
        'b' => Raw::I(b[0] as i8 as i64),
        'H' => Raw::I(u16::from_le_bytes([b[0], b[1]]) as i64),
        'h' => Raw::I(i16::from_le_bytes([b[0], b[1]]) as i64),
        'I' => Raw::I(u32::from_le_bytes([b[0], b[1], b[2], b[3]]) as i64),
        'i' => Raw::I(i32::from_le_bytes([b[0], b[1], b[2], b[3]]) as i64),
        'f' => Raw::F(f32::from_le_bytes([b[0], b[1], b[2], b[3]]) as f64),
        _ => Raw::F(f64::from_le_bytes(b)),
    })
}

/// An integer field's value as the JSON writes it: its x-enum name (the number when it has
/// none), or for x-flags the list of its bits' names (bits without one as a number).
pub fn named(p: &Value, v: i64) -> Value {
    if let Some(fl) = p.get("x-flags").and_then(Value::as_object) {
        let mut out = vec![];
        let mut rest = v;
        for (bit, nm) in fl {
            let b: i64 = bit.parse().unwrap_or(0);
            if b != 0 && v & b == b {
                out.push(nm.clone());
                rest &= !b;
            }
        }
        if rest != 0 {
            out.push(Value::from(rest));
        }
        return Value::Array(out);
    }
    if let Some(names) = p.get("x-enum").and_then(Value::as_object) {
        if let Some(n) = names.get(&v.to_string()) {
            return n.clone();
        }
    }
    Value::from(v)
}

/// named()'s inverse: the integer of a name or a flag list; anything else unchanged.
pub fn unnamed(p: &Value, v: &Value, path: &str) -> Result<Value> {
    if let (Some(fl), Value::Array(items)) = (p.get("x-flags").and_then(Value::as_object), v) {
        let mut w: i128 = 0;
        for x in items {
            match x {
                Value::String(s) => {
                    let b = fl.iter().find(|(_, nm)| nm.as_str() == Some(s.as_str()));
                    match b {
                        Some((k, _)) => w |= k.parse::<i128>().unwrap_or(0),
                        None => {
                            let all: Vec<String> = fl.values().map(py::str_of).collect();
                            return Err(Error::xdl(format!("{path}: unknown flag {} ({})", py::str_repr(s), all.join(", "))));
                        }
                    }
                }
                Value::Number(_) if !x.is_f64() => {
                    if let py::Num::Int(i) = py::Num::of(x).unwrap() {
                        w |= i;
                    }
                }
                _ => return Err(Error::xdl(format!("{path}: flag names or integers"))),
            }
        }
        return Ok(int_value(w));
    }
    if let (Value::String(s), Some(en)) = (v, p.get("x-enum").and_then(Value::as_object)) {
        for (k, nm) in en {
            if nm.as_str() == Some(s.as_str()) {
                return Ok(int_value(k.parse::<i128>().unwrap_or(0)));
            }
        }
        let all: Vec<String> = en.values().map(py::str_of).collect();
        return Err(Error::xdl(format!("{path}: unknown name {} ({})", py::str_repr(s), all.join(", "))));
    }
    Ok(v.clone())
}

pub(crate) fn int_value(i: i128) -> Value {
    if let Ok(x) = i64::try_from(i) {
        Value::from(x)
    } else if let Ok(x) = u64::try_from(i) {
        Value::from(x)
    } else {
        Value::from(i as f64)
    }
}

// ------------------------------------------------------------------------------------------
// link resolution (object links into the base pack)

#[derive(Debug, Clone)]
enum Node<'s> {
    Struct(String),
    Array(String, &'s Value),
    /// a pointer slot: (C type, property schema or a made-up one for an element)
    Link(#[allow(dead_code)] String, LinkProp<'s>),
    Scalar(String),
}

#[derive(Debug, Clone)]
enum LinkProp<'s> {
    Schema(&'s Value),
    /// {"x-link": {"kind": "object", "target": inner}}
    Elem(String),
}

impl LinkProp<'_> {
    fn target(&self) -> Option<String> {
        match self {
            LinkProp::Schema(p) => p["x-link"].get("target").map(py::str_of),
            LinkProp::Elem(t) => Some(t.clone()),
        }
    }
}

pub struct Resolver<'a> {
    pub s: &'a Schemas,
    pub pk: &'a Pack,
}

impl<'a> Resolver<'a> {
    pub fn new(s: &'a Schemas, pk: &'a Pack) -> Self {
        Resolver { s, pk }
    }

    /// (target offset in the pack, C type name of what it points to) for an object link.
    pub fn resolve(&self, link: &str) -> Result<(u32, Option<String>)> {
        let Some(m) = BaseLink::parse(link) else {
            return Err(Error::xdl(format!("{link}: only base:// object links resolve in this step")));
        };
        if m.archive.to_lowercase() != self.pk.archive.to_lowercase() || m.pack != self.pk.name {
            return Err(Error::xdl(format!(
                "{link}: points into {}/{}; objects can only link inside their own pack ({}/{}) so far",
                m.archive, m.pack, self.pk.archive, self.pk.name
            )));
        }
        let rk = pack::root_kind(&m.kind);
        if self.s.struct_(&m.kind).is_none() || rk.is_none() {
            return Err(Error::xdl(format!("{link}: {} is not a root object kind", m.kind)));
        }
        let roots = self.pk.roots(rk.unwrap())?;
        let i = pack::int_of(&m.idx)?;
        let off = py_index(&roots, i)?;
        let Some(mut off) = off else {
            return Err(Error::uncaught("unsupported operand type(s) for +: 'NoneType' and 'int'"));
        };
        let mut node = Node::Struct(m.kind.clone());
        for seg in m.path() {
            (off, node) = self.step(off, node, seg, link)?;
        }
        if let Node::Link(_, p) = &node {
            let t = self.pk.target(off)?.ok_or_else(|| Error::xdl(format!("{link}: the base's slot is not a pointer")))?;
            return Ok((t, p.target()));
        }
        match node {
            Node::Struct(t) => Ok((off, Some(t))),
            // the Python returns the C type of an array or scalar too
            Node::Array(t, _) | Node::Scalar(t) => Ok((off, Some(t))),
            Node::Link(..) => unreachable!(),
        }
    }

    fn step<'s>(&'s self, off: u32, node: Node<'s>, seg: &str, link: &str) -> Result<(u32, Node<'s>)> {
        match node {
            Node::Link(_, p) => {
                let t = self.pk.target(off)?.ok_or_else(|| Error::xdl(format!("{link}: NULL pointer before /{seg}")))?;
                let target = p.target().unwrap_or_default();
                if self.s.struct_(&target).is_some() {
                    return self.step(t, Node::Struct(target), seg, link);
                }
                if !is_digit(seg) {
                    return Err(Error::xdl(format!("{link}: /{seg} into {target}")));
                }
                let n: u32 = seg.parse().unwrap_or(0);
                if let Some(inner) = target.strip_prefix("PTR32(") {
                    let inner = inner[..inner.len().saturating_sub(1)].to_string();
                    return Ok((t.wrapping_add(4 * n), Node::Link(inner.clone(), LinkProp::Elem(inner))));
                }
                Ok((t.wrapping_add(self.s.size_of(&target)? as u32 * n), Node::Struct(target)))
            }
            Node::Struct(ctype) => {
                let sch = self.s.struct_(&ctype).ok_or_else(|| Error::xdl(format!("{link}: no schema for {ctype}")))?;
                let props = schema::fields(sch);
                let Some((_, q)) = props.iter().rev().find(|(k, _)| k.as_str() == seg) else {
                    return Err(Error::xdl(format!("{link}: {ctype} has no field {seg}")));
                };
                Ok(self.place(off.wrapping_add(schema::offset(q)?), q, schema::ctype(q)))
            }
            Node::Array(ctype, p) => {
                let (base, dims) = schema::parse_ctype(&ctype);
                let sub = schema::sub_ctype(&base, &dims);
                let i: i64 = seg
                    .parse()
                    .map_err(|_| Error::uncaught(format!("invalid literal for int() with base 10: {}", py::str_repr(seg))))?;
                if !(0 <= i && i < dims[0] as i64) {
                    return Err(Error::xdl(format!("{link}: index {i} out of [0, {})", dims[0])));
                }
                Ok(self.place(off.wrapping_add(i as u32 * self.s.size_of(&sub)? as u32), &p["items"], &sub))
            }
            Node::Scalar(_) => Err(Error::xdl(format!("{link}: can't go into /{seg}"))),
        }
    }

    fn place<'s>(&self, off: u32, p: &'s Value, ctype: &str) -> (u32, Node<'s>) {
        let (_, dims) = schema::parse_ctype(ctype);
        if !dims.is_empty() {
            return (off, Node::Array(ctype.into(), p));
        }
        if p.get("x-link").is_some() {
            return (off, Node::Link(ctype.into(), LinkProp::Schema(p)));
        }
        if let Some(r) = schema::ref_name(p) {
            return (off, Node::Struct(r.into()));
        }
        (off, Node::Scalar(ctype.into()))
    }
}

/// str.isdigit() for ASCII text
fn is_digit(s: &str) -> bool {
    !s.is_empty() && s.chars().all(|c| c.is_ascii_digit())
}

/// Python list[i] (negative from the end; IndexError otherwise).
pub(crate) fn py_index<T: Clone>(v: &[T], i: i64) -> Result<T> {
    let n = v.len() as i64;
    let j = if i < 0 { i + n } else { i };
    if j < 0 || j >= n {
        return Err(Error::uncaught("list index out of range"));
    }
    Ok(v[j as usize].clone())
}
