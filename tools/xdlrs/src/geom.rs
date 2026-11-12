//! Geometry in a pack's blob as xdl_graph's Extractor finds it (tools/xdl_gltf.py `scan` and
//! `decode`): the headers, and each one's extent (`owned`) and structural names.
//!
//! Only what the Extractor uses of a decode is kept: the owned ranges (in the order the Python
//! appends them) and the names by offset (in dict order: a later name for the same offset
//! replaces the text and keeps the place). Vertices and triangles are walked but not kept, so
//! that a decode fails exactly where the Python's does:
//!
//! * `Kind::Xdl`: what the Extractor catches (GeomError, struct.error, IndexError, ValueError,
//!   ps2_geom's FormatError): the geometry stays raw bytes with a 0x40 claim. The message is the
//!   Python exception's text.
//! * `Kind::Uncaught`: a TypeError in the Python (a PC level with an index list but no
//!   position array; a PS2 skin record whose pointer isn't a relocated slot): a traceback there.
//!
//! PS2 groups are checked as docs/ps2-version/scripts/ps2_geom.py's group_triangles reads them
//! (DMA tag, VIF stream, batches, strips); its `orient` can't fail and isn't needed.

use crate::error::{Error, Kind, Result};
use std::collections::{HashMap, HashSet};

const F_ALPHA: u16 = 0x81;
const F_LITENV: u16 = 0x16;
const F_SHADOW: u16 = 0x4000;

/// The geometry header's magic per release.
pub fn magic(release: &str) -> u16 {
    match release {
        "gc" => 0x2003,
        "pc" => 0x0002,
        _ => 0x1005,
    }
}

/// data.bin with its pointer slots: what every geometry function reads (xdl_gltf.Blob).
pub struct Blob<'a> {
    pub d: &'a [u8],
    pub ptrs: &'a HashSet<u32>,
    pub release: &'a str,
    pub big: bool,
}

fn struct_err(off: i64, size: i64, len: usize) -> Error {
    Error::xdl(format!(
        "unpack_from requires a buffer of at least {} bytes for unpacking {} bytes at offset {} (actual buffer size is {})",
        off + size,
        size,
        off,
        len
    ))
}

fn index_err() -> Error {
    Error::xdl("index out of range")
}

impl<'a> Blob<'a> {
    pub fn new(d: &'a [u8], ptrs: &'a HashSet<u32>, release: &'a str) -> Self {
        Blob { d, ptrs, release, big: release == "gc" }
    }

    fn raw(&self, o: i64, n: i64) -> Result<&[u8]> {
        if o < 0 || o + n > self.d.len() as i64 {
            return Err(struct_err(o, n, self.d.len()));
        }
        Ok(&self.d[o as usize..(o + n) as usize])
    }

    pub fn u16(&self, o: i64) -> Result<u16> {
        let b = self.raw(o, 2)?;
        let a = [b[0], b[1]];
        Ok(if self.big { u16::from_be_bytes(a) } else { u16::from_le_bytes(a) })
    }

    pub fn u32(&self, o: i64) -> Result<u32> {
        let b = self.raw(o, 4)?;
        let a = [b[0], b[1], b[2], b[3]];
        Ok(if self.big { u32::from_be_bytes(a) } else { u32::from_le_bytes(a) })
    }

    fn is_slot(&self, o: i64) -> bool {
        o >= 0 && o <= u32::MAX as i64 && self.ptrs.contains(&(o as u32))
    }

    /// The target of the pointer slot at o (None: no slot, or past the blob).
    pub fn target(&self, o: i64) -> Result<Option<i64>> {
        if !self.is_slot(o) {
            return Ok(None);
        }
        let t = self.u32(o)? as i64;
        Ok(if t < self.d.len() as i64 { Some(t) } else { None })
    }

    /// d[o] (IndexError past the end)
    fn byte(&self, o: i64) -> Result<u8> {
        if o < 0 || o >= self.d.len() as i64 {
            return Err(index_err());
        }
        Ok(self.d[o as usize])
    }

    /// ps2_geom's Pack accessors (via xdl_gltf._Ps2Shim): little-endian, 0 past the end.
    fn shim_u32(&self, o: i64) -> u32 {
        if o >= 0 && o + 4 <= self.d.len() as i64 {
            let o = o as usize;
            u32::from_le_bytes([self.d[o], self.d[o + 1], self.d[o + 2], self.d[o + 3]])
        } else {
            0
        }
    }

    fn shim_target(&self, o: i64) -> Option<i64> {
        if !self.is_slot(o) {
            return None;
        }
        let t = self.shim_u32(o) as i64;
        if t < self.d.len() as i64 { Some(t) } else { None }
    }
}

/// Geometry header offsets in scan order (pairing.md §1: the index of geometry/N).
pub fn scan(b: &Blob) -> Vec<u32> {
    let n = b.d.len() as i64;
    let mg = magic(b.release);
    let mut out = vec![];
    let mut off = 0i64;
    while off < n - 0x40 {
        let ok = (|| -> Result<bool> {
            if b.u16(off)? != mg || !b.is_slot(off + 8) || !b.is_slot(off + 0xC) {
                return Ok(false);
            }
            let k = b.u16(off + 2)?;
            if !(1..=8).contains(&k) {
                return Ok(false);
            }
            let lods = b.u32(off + 8)? as i64;
            Ok(match b.release {
                "gc" => lods < n,
                "pc" => lods == off + 0x40,
                _ => b.target(off + 8)?.is_some(),
            })
        })()
        .unwrap_or(false);
        if ok {
            out.push(off as u32);
        }
        off += 4;
    }
    out
}

/// What the Extractor keeps of a decoded geometry.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Geom {
    /// (lo, hi) ranges, in the order the Python appends them
    pub owned: Vec<(u32, u32)>,
    /// offset -> structural name ("" for the header), in dict order
    pub names: Vec<(u32, String)>,
}

#[derive(Default)]
struct Acc {
    owned: Vec<(i64, i64)>,
    names: Vec<(i64, String)>,
    at: HashMap<i64, usize>,
}

impl Acc {
    fn own(&mut self, lo: i64, hi: i64) {
        self.owned.push((lo, hi));
    }
    fn name(&mut self, o: i64, n: String) {
        if let Some(&i) = self.at.get(&o) {
            self.names[i].1 = n;
        } else {
            self.at.insert(o, self.names.len());
            self.names.push((o, n));
        }
    }
}

fn lit(flags: u16) -> bool {
    flags & F_LITENV != 0 && flags & F_ALPHA == 0
}

/// _group_rec: the reads it makes (any can fail); the flags.
fn group_rec(b: &Blob, g: i64) -> Result<u16> {
    b.u16(g)?;
    let flags = b.u16(g + 2)?;
    b.u32(g + 8)?;
    b.u16(g + 0x14)?;
    b.u16(g + 0x16)?;
    b.u32(g + 0x18)?;
    Ok(flags)
}

fn uvscroll(b: &Blob, g: i64, acc: &mut Acc, path: &str) -> Result<()> {
    match b.target(g + 0xC)? {
        None => {
            b.u32(g + 0xC)?;
        }
        Some(u) => {
            acc.own(u, u + 0x10);
            acc.name(u, format!("{path}/uvscroll"));
        }
    }
    Ok(())
}

/// _names: the byte length of NUL-terminated strings up to an empty one.
fn names_len(b: &Blob, o: i64) -> Result<i64> {
    let d = b.d;
    let mut i = o;
    while i >= 0 && (i as usize) < d.len() && d[i as usize] != 0 {
        match d[i as usize..].iter().position(|&c| c == 0) {
            Some(j) => i += j as i64 + 1,
            None => return Err(Error::xdl("subsection not found")),
        }
    }
    Ok(i + 1 - o)
}

/// xdl_gltf.decode, reduced to what the Extractor keeps.
pub fn decode(b: &Blob, off: u32) -> Result<Geom> {
    let off = off as i64;
    let rel = b.release;
    let mut acc = Acc::default();
    b.u16(off)?;
    b.u16(off + 4)?;
    let skinned = b.u16(off + 4)? != 0;
    b.u16(off + 6)?;
    let n = b.u16(off + 2)? as i64;
    let lods = b.target(off + 8)?;
    let dist = b.target(off + 0xC)?;
    let (Some(lods), Some(dist)) = (lods, dist) else {
        return Err(Error::xdl(format!("0x{off:X}: levels or distances not a pointer")));
    };
    acc.own(off, off + 0x40);
    acc.own(lods, lods + 0x18 * n);
    acc.own(dist, dist + 4 * n);
    acc.name(off, String::new());
    acc.name(lods, "levels".into());
    acc.name(dist, "distances".into());
    for li in 0..n {
        let l = lods + 0x18 * li;
        acc.name(l, format!("level/{li}"));
        let ng = b.u16(l + 2)? as i64;
        let Some(grp) = b.target(l + 8)? else {
            return Err(Error::xdl(format!("0x{off:X} level {li}: groups not a pointer")));
        };
        acc.name(grp, format!("level/{li}/groups"));
        acc.own(grp, grp + 0x20 * ng);
        b.u16(l)?;
        b.u32(l + 4)?;
        let shadow = b.u16(grp + 2)? & F_SHADOW != 0 || (rel == "ps2" && b.u16(l)? == 3);
        match b.target(l + 0x14)? {
            Some(nm) => {
                let ln = names_len(b, nm)?;
                acc.own(nm, nm + ln);
                acc.name(nm, format!("level/{li}/names"));
            }
            None => {
                b.u32(l + 0x14)?;
            }
        }
        match b.target(l + 0x10)? {
            Some(sk) => {
                for i in 0..8 {
                    b.u32(sk + 4 * i)?;
                }
                acc.own(sk, sk + 0x20);
                acc.name(sk, format!("level/{li}/bones"));
            }
            None => {
                b.u32(l + 0x10)?;
            }
        }
        if rel == "ps2" && !shadow {
            b.u32(l + 0xC)?;
        }
        if shadow {
            decode_shadow(b, l, grp, ng, &mut acc, li)?;
        } else if rel == "gc" {
            decode_gc(b, l, grp, ng, &mut acc, li, skinned)?;
        } else if rel == "pc" {
            decode_pc(b, l, grp, ng, &mut acc, li)?;
        } else {
            decode_ps2(b, l, grp, ng, &mut acc, li)?;
        }
        for k in 0..ng {
            acc.name(grp + 0x20 * k, format!("level/{li}/group/{k}"));
        }
    }
    let u = |x: i64| x as u32;
    Ok(Geom {
        owned: acc.owned.iter().map(|&(a, z)| (u(a), u(z))).collect(),
        names: acc.names.into_iter().map(|(o, s)| (u(o), s)).collect(),
    })
}

fn decode_shadow(b: &Blob, l: i64, grp: i64, ng: i64, acc: &mut Acc, li: i64) -> Result<()> {
    let pos = b.target(l + 0xC)?;
    if pos.is_none() {
        b.u32(l + 0xC)?;
    }
    let mut top = 0i64;
    for k in 0..ng {
        let g = grp + 0x20 * k;
        group_rec(b, g)?;
        b.u32(g + 0x10)?;
        b.u32(g + 0xC)?;
        let e = b.target(g + 0x1C)?;
        if b.release == "ps2" {
            continue;
        }
        match e {
            Some(e) => {
                let ne = b.u32(g + 0x10)? as i64;
                acc.own(e, e + 0x20 * ne);
                acc.name(e, format!("level/{li}/group/{k}/edges"));
                for i in 0..ne {
                    let ee = e + 0x20 * i;
                    let v0 = b.u16(ee + 0xC)? as i64;
                    let v1 = b.u16(ee + 0xE)? as i64;
                    b.u32(ee + 0x1C)?;
                    top = top.max(v0 + 1).max(v1 + 1);
                }
            }
            None => {
                b.u32(g + 0x1C)?;
            }
        }
    }
    if let Some(pos) = pos {
        acc.own(pos, pos + 12 * top);
        acc.name(pos, format!("level/{li}/positions"));
    }
    Ok(())
}

fn decode_gc(b: &Blob, l: i64, grp: i64, ng: i64, acc: &mut Acc, li: i64, skinned: bool) -> Result<()> {
    let Some(vb) = b.target(l + 0xC)? else {
        return Err(Error::xdl("vertex buffer not a pointer"));
    };
    let uvo = b.u32(vb)? as i64;
    let clo = b.u32(vb + 4)? as i64;
    let nro = b.u32(vb + 8)? as i64;
    let end = b.u32(vb + 12)? as i64;
    let data = vb + 0x10;
    acc.own(vb, data + end);
    acc.name(vb, format!("level/{li}/vertices"));
    let bound = |start: i64| [uvo, clo, nro].into_iter().filter(|&o| o > start).chain([end]).min().unwrap();
    let npos = uvo.div_euclid(12);
    let nuv = (bound(uvo) - uvo).div_euclid(8);
    for k in 0..ng {
        let g = grp + 0x20 * k;
        let flags = group_rec(b, g)?;
        uvscroll(b, g, acc, &format!("level/{li}/group/{k}"))?;
        let size = b.u32(g + 0x10)? as i64;
        let Some(dl) = b.target(g + 0x1C)? else {
            return Err(Error::xdl("display list not a pointer"));
        };
        acc.own(dl, dl + size);
        acc.name(dl, format!("level/{li}/group/{k}/draw"));
        let mtx = skinned && lit(flags);
        let (mut p, stop) = (dl, dl + size);
        while p < stop {
            let cmd = b.byte(p)?;
            p += 1;
            if cmd == 0 {
                continue;
            }
            let prim = cmd & 0xF8;
            if !matches!(prim, 0x90 | 0x98 | 0xA0) {
                return Err(Error::xdl(format!("display-list command 0x{cmd:02X}")));
            }
            let cb = b.raw(p, 2)?;
            let cnt = u16::from_be_bytes([cb[0], cb[1]]);
            p += 2;
            for _ in 0..cnt {
                if mtx {
                    b.byte(p)?;
                    p += 1;
                }
                let r = b.raw(p, 6)?;
                let a = u16::from_be_bytes([r[0], r[1]]) as i64;
                let c = u16::from_be_bytes([r[4], r[5]]) as i64;
                p += 6;
                if a >= npos || c >= nuv {
                    return Err(Error::xdl("index past its array"));
                }
            }
        }
    }
    Ok(())
}

fn decode_pc(b: &Blob, l: i64, grp: i64, ng: i64, acc: &mut Acc, li: i64) -> Result<()> {
    let Some(vb) = b.target(l + 0xC)? else {
        return Err(Error::xdl("vertex buffer not a pointer"));
    };
    let mut arrays = [None; 4];
    for (i, a) in arrays.iter_mut().enumerate() {
        *a = b.target(vb + 4 * i as i64)?;
    }
    acc.name(vb, format!("level/{li}/vertices"));
    let mut nonempty = vec![];
    let mut top = 0i64;
    for k in 0..ng {
        let g = grp + 0x20 * k;
        let n = b.u32(g + 0x10)? as i64;
        let Some(ix) = b.target(g + 0x1C)? else {
            return Err(Error::xdl("index list not a pointer"));
        };
        if b.u16(ix)? & 0x8000 != 0 {
            return Err(Error::xdl("strip records (no PC group has them)"));
        }
        let mut mx = -1i64;
        for i in 0..3 * n {
            mx = mx.max(b.u16(ix + 2 * i)? as i64);
        }
        acc.own(ix, ix + 6 * n);
        acc.name(ix, format!("level/{li}/group/{k}/draw"));
        nonempty.push(n > 0);
        if n > 0 {
            top = top.max(mx + 1);
        }
    }
    acc.own(vb, vb + 0x20);
    for (p, stride) in arrays.iter().zip([16, 4, 8, 16]) {
        if let Some(p) = p {
            acc.own(*p, p + stride * top);
        }
    }
    for k in 0..ng {
        let g = grp + 0x20 * k;
        group_rec(b, g)?;
        uvscroll(b, g, acc, &format!("level/{li}/group/{k}"))?;
        if nonempty[k as usize] && arrays[0].is_none() {
            // blob.le4(pp + 16 * i, 3) with pp None
            return Err(Error::uncaught("unsupported operand type(s) for +: 'NoneType' and 'int'"));
        }
    }
    Ok(())
}

fn decode_ps2(b: &Blob, l: i64, grp: i64, ng: i64, acc: &mut Acc, li: i64) -> Result<()> {
    let kind = b.u16(l)?;
    for k in 0..ng {
        let g = grp + 0x20 * k;
        group_rec(b, g)?;
        b.u32(g + 0x10)?;
        b.u32(g + 0x1C)?;
        let Some(pkt) = b.target(g + 0xC)? else {
            return Err(Error::xdl("PS2 group packet not a pointer"));
        };
        acc.name(pkt, format!("level/{li}/group/{k}/draw"));
        if kind == LEVEL_SKINNED {
            let mut r = pkt + 0x10;
            while b.u32(r + 4)? != 0 {
                let Some(sub) = b.target(r + 4)? else {
                    // blob.u32(None)
                    return Err(Error::uncaught("'NoneType' object cannot be interpreted as an integer"));
                };
                let tag = b.u32(sub)? as i64;
                acc.own(sub, sub + 16 + (tag & 0xFFFF) * 16);
                r += 8;
            }
            acc.own(pkt, r + 8);
        } else {
            let tag = b.u32(pkt + 0x10)? as i64;
            acc.own(pkt, pkt + 0x20 + (tag & 0xFFFF) * 16);
        }
        group_triangles(b, g, kind).map_err(|e| match e {
            Ps2Err::Format(m) => Error::xdl(format!("PS2 packet: {m}")),
            Ps2Err::Other(e) => e,
        })?;
    }
    Ok(())
}

// ------------------------------------------------------------------------------------------
// ps2_geom.py: the checks group_triangles makes

const LEVEL_SKINNED: u16 = 2;

enum Ps2Err {
    /// FormatError (xdl_gltf turns it into GeomError "PS2 packet: ...")
    Format(String),
    /// struct.error / IndexError: through as they are
    Other(Error),
}

impl From<Error> for Ps2Err {
    fn from(e: Error) -> Self {
        Ps2Err::Other(e)
    }
}

type PResult<T> = std::result::Result<T, Ps2Err>;

fn fmt_err<T>(m: impl Into<String>) -> PResult<T> {
    Err(Ps2Err::Format(m.into()))
}

#[derive(Default)]
struct Batch {
    nloop: u64,
    prim: u64,
    nreg: u64,
    /// the w of each position (ADC flag or matrix slot)
    pos: Option<Vec<i32>>,
    uv: Option<usize>,
    nrm: Option<usize>,
    col: Option<usize>,
}

fn read_batches(b: &Blob, dma: i64) -> PResult<Vec<Batch>> {
    let d = b.d;
    let tag = b.shim_u32(dma) as i64;
    if (tag >> 28) & 7 != 6 {
        return fmt_err(format!("DMA tag id {}, not ret", (tag >> 28) & 7));
    }
    let end = dma + 16 + (tag & 0xFFFF) * 16;
    if end > d.len() as i64 {
        return fmt_err("packet runs past the blob");
    }
    let le = |o: i64, n: i64| -> Result<&[u8]> {
        if o < 0 || o + n > d.len() as i64 {
            return Err(struct_err(o, n, d.len()));
        }
        Ok(&d[o as usize..(o + n) as usize])
    };
    let mut batches: Vec<Batch> = vec![];
    let mut cycle: Option<u32> = None;
    let mut p = dma + 8;
    while p + 4 <= end {
        let w = u32::from_le_bytes(le(p, 4)?.try_into().unwrap());
        let (cmd, num, imm) = ((w >> 24) & 0x7F, (w >> 16) & 0xFF, w & 0xFFFF);
        let q = p + 4;
        let mut n: i64 = 0;
        if cmd & 0x60 == 0x60 {
            let (vn, vl) = ((cmd >> 2) & 3, cmd & 3);
            let size = if vl == 3 { 2 } else { (vn as i64 + 1) * (4 >> vl) };
            n = ((if num == 0 { 256 } else { num as i64 }) * size + 3) & !3;
        } else if cmd == 0x20 {
            n = 4;
        } else if cmd == 0x30 || cmd == 0x31 {
            n = 16;
        } else if cmd == 0x4A {
            n = (if num == 0 { 256 } else { num as i64 }) * 8;
        } else if cmd == 0x50 || cmd == 0x51 {
            n = (if imm == 0 { 65536 } else { imm as i64 }) * 16;
        }
        // the code
        if cmd == 0x01 {
            cycle = Some(imm);
        } else if cmd & 0x60 == 0x60 {
            let (addr, count) = (imm & 0x3FF, if num == 0 { 256 } else { num as i64 });
            if addr == 0 {
                if cmd != 0x6C || count != 1 || cycle != Some(0x0404) {
                    return fmt_err(format!("GIF tag unpack 0x{cmd:02X} x{count}"));
                }
                let lo = u64::from_le_bytes(le(q, 16)?[..8].try_into().unwrap());
                let prim = (lo >> 47) & 0x7FF;
                batches.push(Batch { nloop: lo & 0x7FFF, prim: prim & 7, nreg: (lo >> 60) & 0xF, ..Default::default() });
            } else {
                if batches.is_empty() || cycle != Some(0x0103) {
                    return fmt_err("vertex unpack outside a batch");
                }
                let cur = batches.last_mut().unwrap();
                if addr == 1 && cmd == 0x6C {
                    let mut ws = Vec::with_capacity(count as usize);
                    for i in 0..count {
                        let r = le(q + 16 * i, 16)?;
                        ws.push(i32::from_le_bytes(r[12..16].try_into().unwrap()));
                    }
                    cur.pos = Some(ws);
                } else if addr == 2 && cmd == 0x64 {
                    for i in 0..count {
                        le(q + 8 * i, 8)?;
                    }
                    cur.uv = Some(count as usize);
                } else if addr == 3 && cmd == 0x68 {
                    for i in 0..count {
                        le(q + 12 * i, 12)?;
                    }
                    cur.nrm = Some(count as usize);
                } else if addr == 3 && cmd == 0x6E {
                    cur.col = Some(count as usize);
                } else {
                    return fmt_err(format!("unpack 0x{cmd:02X} to VU address {addr}"));
                }
            }
        } else if matches!(cmd, 0x00 | 0x02 | 0x03 | 0x17) {
        } else {
            return fmt_err(format!("VIF command 0x{cmd:02X}"));
        }
        p = q + n;
    }
    for bt in &batches {
        if bt.pos.is_none() || bt.uv.is_none() || bt.nrm.is_some() == bt.col.is_some() {
            return fmt_err("batch without positions, UVs and exactly one of normals/colours");
        }
        let np = bt.pos.as_ref().unwrap().len() as u64;
        let attr = bt.nrm.or(bt.col).unwrap() as u64;
        if !(np == bt.uv.unwrap() as u64 && np == attr && attr == bt.nloop * (bt.nreg / 3)) {
            return fmt_err("attribute counts differ from the GIF tag");
        }
    }
    Ok(batches)
}

fn batch_triangles(bt: &Batch, bones: Option<&[u8]>) -> PResult<()> {
    let pos = bt.pos.as_ref().unwrap();
    if let Some(bones) = bones {
        for &w in pos {
            if !(-1..=1).contains(&w) {
                return fmt_err(format!("matrix slot {w}"));
            }
            match bones.get((w + 1) as usize) {
                None => return Err(Ps2Err::Other(index_err())),
                Some(0xFF) => return fmt_err(format!("matrix slot {w}")),
                _ => {}
            }
        }
    }
    match bt.prim {
        4 => {
            for (i, &w) in pos.iter().enumerate() {
                if w & 0x8000 != 0 {
                    continue;
                }
                if i < 2 {
                    return fmt_err("strip vertex draws without two before it");
                }
            }
        }
        3 => {
            if !pos.len().is_multiple_of(3) {
                return fmt_err(format!("list of {} vertices", pos.len()));
            }
        }
        p => return fmt_err(format!("GIF primitive {p}")),
    }
    Ok(())
}

fn group_triangles(b: &Blob, grp: i64, kind: u16) -> PResult<()> {
    let Some(pkt) = b.shim_target(grp + 0xC) else {
        return fmt_err("group packet is not a pointer");
    };
    if kind == LEVEL_SKINNED {
        let mut r = pkt + 0x10;
        while b.shim_u32(r + 4) != 0 {
            let Some(sub) = b.shim_target(r + 4) else {
                return fmt_err("skin record pointer is not a slot");
            };
            let lo = (r.max(0) as usize).min(b.d.len());
            let bones = &b.d[lo..(lo + 3).min(b.d.len())];
            for bt in read_batches(b, sub)? {
                batch_triangles(&bt, Some(bones))?;
            }
            r += 8;
        }
    } else {
        for bt in read_batches(b, pkt + 0x10)? {
            batch_triangles(&bt, None)?;
        }
    }
    Ok(())
}

/// Whether a decode error is one the Extractor catches (the geometry stays raw).
pub fn caught(e: &Error) -> bool {
    e.kind != Kind::Uncaught
}
