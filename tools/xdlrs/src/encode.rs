//! JSON -> pack bytes (xdl_struct.Encoder; xdl_graph.GraphEncoder as LinkMode::Symbolic), and
//! Python's struct.pack rules (ranges, messages) for one value.

use crate::decode::{self, Decoder, Resolver};
use crate::error::{Error, Kind, Result};
use crate::pack::{self, Pack, Pairing, Relocs};
use crate::py::{self, Num};
use crate::schema::{self, Prim, Schemas};
use serde_json::{Map, Value};
use std::collections::BTreeMap;

/// struct.pack(e + code, v) of an int; Err(struct.error's message).
pub fn pack_int(code: char, v: i128, big: bool) -> std::result::Result<Vec<u8>, String> {
    let out_of_i64 = v < i64::MIN as i128 || v > i64::MAX as i128;
    let err = |m: &str| Err(m.to_string());
    let le: Vec<u8> = match code {
        'B' | 'b' if out_of_i64 => return err("argument out of range"),
        'B' => {
            if !(0..=255).contains(&v) {
                return err("ubyte format requires 0 <= number <= 255");
            }
            vec![v as u8]
        }
        'b' => {
            if !(-128..=127).contains(&v) {
                return err("byte format requires -128 <= number <= 127");
            }
            vec![v as i8 as u8]
        }
        'H' => {
            if out_of_i64 || (big && v < 0) {
                return err("argument out of range");
            }
            if !(0..=65535).contains(&v) {
                return err(if big {
                    "'H' format requires 0 <= number <= 65535"
                } else {
                    "ushort format requires 0 <= number <= (32767 * 2 + 1)"
                });
            }
            (v as u16).to_le_bytes().to_vec()
        }
        'h' => {
            if out_of_i64 {
                return err("argument out of range");
            }
            if !(-32768..=32767).contains(&v) {
                return err(if big {
                    "'h' format requires -32768 <= number <= 32767"
                } else {
                    "short format requires (-32767 -1) <= number <= 32767"
                });
            }
            (v as i16).to_le_bytes().to_vec()
        }
        'I' => {
            if v < 0 || v > u64::MAX as i128 {
                return err("argument out of range");
            }
            if v > u32::MAX as i128 {
                return err("'I' format requires 0 <= number <= 4294967295");
            }
            (v as u32).to_le_bytes().to_vec()
        }
        'i' => {
            if out_of_i64 {
                return err("argument out of range");
            }
            if !(i32::MIN as i128..=i32::MAX as i128).contains(&v) {
                return err("'i' format requires -2147483648 <= number <= 2147483647");
            }
            (v as i32).to_le_bytes().to_vec()
        }
        _ => return err("required argument is not a float"),
    };
    Ok(order(le, big))
}

/// struct.pack(e + "f"/"d", v); Err: OverflowError (f32 out of range; uncaught in the Python).
pub fn pack_float(code: char, v: f64, big: bool) -> Result<Vec<u8>> {
    let le = if code == 'f' {
        let f = v as f32;
        if f.is_infinite() && v.is_finite() {
            return Err(Error::uncaught("float too large to pack with f format"));
        }
        f.to_le_bytes().to_vec()
    } else {
        v.to_le_bytes().to_vec()
    };
    Ok(order(le, big))
}

fn order(mut le: Vec<u8>, big: bool) -> Vec<u8> {
    if big {
        le.reverse();
    }
    le
}

/// Python float(v) (Encoder's conversion of an f32/f64 field's value).
pub fn py_float(v: &Value) -> Result<f64> {
    match v {
        Value::String(s) => py::float_of_str(s)
            .ok_or_else(|| Error::uncaught(format!("could not convert string to float: {}", py::str_repr(s)))),
        _ => match Num::of(v) {
            Some(n) => Ok(n.f64()),
            None => Err(Error::uncaught(format!("float() argument must be a string or a number, not '{}'", py::type_name(v)))),
        },
    }
}

/// The bytes of one primitive field value as Encoder.value writes them (after unnamed()).
/// Errors as the Python's: Xdl for "not an integer" and struct.error, Uncaught for float().
pub fn pack_prim(pr: Prim, v: &Value, big: bool, path: &str) -> Result<Vec<u8>> {
    if pr.float {
        return pack_float(pr.code, py_float(v)?, big);
    }
    let mut shown = py::str_of(v);
    let iv = match Num::of(v) {
        Some(Num::Float(f)) => {
            if f.is_nan() {
                return Err(Error::uncaught("cannot convert float NaN to integer"));
            }
            if f.is_infinite() {
                return Err(Error::uncaught("cannot convert float infinity to integer"));
            }
            if f != f.trunc() {
                return Err(Error::xdl(format!("{path}: {} is not an integer", py::float_repr(f))));
            }
            shown = py::int_text_of_float(f);
            // past i128: as far out of every range as Python's int is
            Some(if f.abs() < 1e38 { f as i128 } else if f < 0.0 { i128::MIN } else { i128::MAX })
        }
        Some(Num::Int(i)) => {
            shown = i.to_string();
            Some(i)
        }
        None => None,
    };
    let r = match iv {
        Some(i) => pack_int(pr.code, i, big),
        None => Err("required argument is not an integer".to_string()),
    };
    r.map_err(|e| Error::xdl(format!("{path}: {shown}: {e}")))
}

/// How links are written.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum LinkMode {
    /// xdl_struct: resolved now (objects in the base pack, textures and geometry by pairing)
    Resolve,
    /// xdl_graph.GraphEncoder: kept as (kind, link) in `links` for the layout to resolve; every
    /// field must be present (but top-level texture links)
    Symbolic,
}

/// Writes a JSON object over its base: (bytes, relocations) of the object.
pub struct Encoder<'a> {
    pub s: &'a Schemas,
    pub pk: &'a Pack,
    pub pair: Option<&'a Pairing>,
    pub sch: &'a Value,
    pub size: usize,
    pub base_off: Option<u32>,
    pub out: Vec<u8>,
    pub rel: Relocs,
    pub base_id: Option<String>,
    pub mode: LinkMode,
    /// Symbolic mode: offset -> (kind, link)
    pub links: BTreeMap<u32, (String, String)>,
    dec: Decoder<'a>,
    res: Resolver<'a>,
}

impl<'a> Encoder<'a> {
    /// base_off: the base object in the pack (None: no base, zeros); base: (bytes, relocations)
    /// to start from in place of the pack's (the round trip's scrubbed base).
    pub fn new(
        s: &'a Schemas,
        pk: &'a Pack,
        pair: Option<&'a Pairing>,
        struct_name: &str,
        base_off: Option<u32>,
        base: Option<(Vec<u8>, Relocs)>,
    ) -> Result<Encoder<'a>> {
        let sch = s.struct_(struct_name).ok_or_else(|| Error::uncaught("'NoneType' object is not subscriptable"))?;
        let size = schema::size(sch)?;
        let (out, rel) = match (base, base_off) {
            (Some(b), _) => b,
            (None, None) => (vec![0; size], Relocs::default()),
            (None, Some(o)) => {
                let lo = (o as usize).min(pk.blob.len());
                let hi = (o as usize + size).min(pk.blob.len());
                (pk.blob[lo..hi].to_vec(), pk.relocs_in(o, o + size as u32)?)
            }
        };
        Ok(Encoder {
            s,
            pk,
            pair,
            sch,
            size,
            base_off,
            out,
            rel,
            base_id: None,
            mode: LinkMode::Resolve,
            links: BTreeMap::new(),
            dec: Decoder::new(s, pk, pair),
            res: Resolver::new(s, pk),
        })
    }

    /// xdl_graph.GraphEncoder: links kept symbolic over `base_bytes`, no relocations.
    pub fn symbolic(s: &'a Schemas, pk: &'a Pack, pair: Option<&'a Pairing>, sname: &str, base_bytes: Vec<u8>) -> Result<Encoder<'a>> {
        let mut e = Encoder::new(s, pk, pair, sname, None, Some((base_bytes, Relocs::default())))?;
        e.mode = LinkMode::Symbolic;
        Ok(e)
    }

    pub fn clear(&mut self, lo: u32, hi: u32) {
        let lo = lo as i64;
        let hi = hi as i64;
        for k in self.rel.kinds_mut() {
            k.retain(|&o, _| !(lo <= o as i64 + 3 && (o as i64) < hi));
        }
        self.links.retain(|&o, _| !(lo <= o as i64 + 3 && (o as i64) < hi));
    }

    pub fn encode(&mut self, obj: &Value, base_id: Option<&str>) -> Result<(Vec<u8>, Relocs)> {
        self.base_id = base_id.map(str::to_string);
        let sch = self.sch;
        self.struct_(sch, 0, obj, "")?;
        Ok((self.out.clone(), self.rel.clone()))
    }

    fn struct_(&mut self, sch: &'a Value, off: u32, obj: &Value, path: &str) -> Result<()> {
        let empty = Map::new();
        let m = obj.as_object().unwrap_or(&empty);
        if self.mode == LinkMode::Symbolic {
            for (key, p) in schema::fields(sch) {
                if m.contains_key(key) {
                    continue;
                }
                let lf = schema::leaf(p, schema::parse_ctype(schema::ctype(p)).1.len());
                let tex = lf.get("x-link").and_then(|x| x.get("kind")).and_then(Value::as_str) == Some("texture");
                if !(path.is_empty() && tex) {
                    return Err(Error::xdl(format!("{path}/{key}: missing (a project file lists every field; its base has 0 there)")));
                }
            }
        }
        for (key, p) in schema::fields(sch) {
            let Some(v) = m.get(key) else { continue }; // from the base
            let o = off.wrapping_add(schema::offset(p)?);
            self.value(p, o, v, &format!("{path}/{key}"), schema::ctype(p))?;
        }
        Ok(())
    }

    fn value(&mut self, p: &'a Value, off: u32, v: &Value, path: &str, ctype: &str) -> Result<()> {
        let (base, dims) = schema::parse_ctype(ctype);
        if !dims.is_empty() {
            let sub = schema::sub_ctype(&base, &dims);
            let esz = self.s.size_of(&sub)? as u32;
            let Some(a) = v.as_array().filter(|a| a.len() == dims[0]) else {
                return Err(Error::xdl(format!("{path}: expected {} values", dims[0])));
            };
            for (i, x) in a.iter().enumerate() {
                self.value(&p["items"], off.wrapping_add(i as u32 * esz), x, &format!("{path}/{i}"), &sub)?;
            }
            return Ok(());
        }
        if let Some(x) = p.get("x-link") {
            return self.link(x, off, v, path);
        }
        if p.get("$ref").is_some() {
            if !v.is_object() {
                return Err(Error::xdl(format!("{path}: expected an object")));
            }
            let sch = self.s.ref_(p).ok_or_else(|| Error::uncaught(format!("{path}: no schema for its $ref")))?;
            return self.struct_(sch, off, v, path);
        }
        let pr = schema::prim(&base).ok_or_else(|| Error::uncaught(format!("KeyError: {}", py::str_repr(&base))))?;
        self.clear(off, off + pr.size as u32);
        let v = decode::unnamed(p, v, path)?;
        let b = pack_prim(pr, &v, self.pk.big, path)?;
        self.put(off, &b)
    }

    fn put(&mut self, off: u32, b: &[u8]) -> Result<()> {
        let o = off as usize;
        if o + b.len() > self.out.len() {
            return Err(Error::strct(format!(
                "pack_into requires a buffer of at least {} bytes for packing {} bytes at offset {o} (actual buffer size is {})",
                o + b.len(),
                b.len(),
                self.out.len()
            )));
        }
        self.out[o..o + b.len()].copy_from_slice(b);
        Ok(())
    }

    fn put_u32(&mut self, off: u32, v: u32) -> Result<()> {
        let b = if self.pk.big { v.to_be_bytes() } else { v.to_le_bytes() };
        self.put(off, &b)
    }

    fn link(&mut self, x: &Value, off: u32, v: &Value, path: &str) -> Result<()> {
        let xk = py::str_of(&x["kind"]);
        if self.mode == LinkMode::Symbolic {
            if !v.is_null() && !v.is_string() {
                return Err(Error::xdl(format!("{path}: a link is a string or null")));
            }
            if v.as_str().is_some_and(|s| s.starts_with("uid://")) {
                return Err(Error::xdl(format!("{path}: uid:// links need the project's id table (not in this step)")));
            }
            self.clear(off, off + 4);
            let Some(s) = v.as_str() else { return self.put_u32(off, 0) };
            if xk == "texture" {
                self.links.insert(off, ("texture".into(), s.into())); // bytes: the base's
                return Ok(());
            }
            self.put_u32(off, 0)?;
            self.links.insert(off, (xk, s.into()));
            return Ok(());
        }
        // a texture slot whose link didn't change keeps the base's (loader-overwritten) bytes
        let mut keep = false;
        if xk == "texture"
            && let Some(b) = self.base_off
        {
            match self.dec.link(x, b.wrapping_add(off), path) {
                Ok(old) => keep = py::eq(&old, v),
                Err(e) if e.kind == Kind::Xdl => keep = false,
                Err(e) => return Err(e),
            }
        }
        self.clear(off, off + 4);
        if v.is_null() {
            return self.put_u32(off, 0);
        }
        let Some(link) = v.as_str() else {
            return Err(Error::xdl(format!("{path}: a link is a string or null")));
        };
        if link.starts_with("uid://") {
            return Err(Error::xdl(format!("{path}: uid:// links need the project's id table (not in this step)")));
        }
        let pk = self.pk;
        if xk == "object" {
            let (t, ttype) = self.res.resolve(link)?;
            let want = x.get("target").map(py::str_of).filter(|w| !w.is_empty());
            if let (Some(w), Some(tt)) = (&want, &ttype)
                && !tt.is_empty()
                && w != "void"
                && tt != "void"
                && w != tt
            {
                return Err(Error::xdl(format!("{path}: {link} is a {tt}, the field wants a {w}")));
            }
            self.put_u32(off, t)?;
            self.rel.pointers.insert(off, t);
            return Ok(());
        }
        let pair = self.pair.ok_or_else(|| Error::uncaught("'NoneType' object has no attribute 'index'"))?;
        let (arch, pname, kind, idx) = pair.index(&pk.release, link)?;
        if kind != xk {
            return Err(Error::xdl(format!("{path}: {link} is not a {xk}")));
        }
        if xk == "texture" {
            let sid = if arch.to_lowercase() == pk.archive.to_lowercase() && pname == pk.name {
                0
            } else if let Some(s) = pack::share_id(&arch, &pname) {
                s
            } else {
                return Err(Error::xdl(format!("{path}: {link}: textures come from the own pack or a share pack")));
            };
            if !keep {
                self.put_u32(off, 0)?;
            }
            self.rel.texture.insert(off, (sid << 16) | idx as u32);
            return Ok(());
        }
        // geometry: own pack only
        if arch.to_lowercase() != pk.archive.to_lowercase() || pname != pk.name {
            return Err(Error::xdl(format!("{path}: {link}: geometry from another pack not supported yet")));
        }
        let g = decode::py_index(pk.geometries(), idx)?;
        self.put_u32(off, g)?;
        self.rel.r#ref.insert(off, idx as u32);
        Ok(())
    }
}
