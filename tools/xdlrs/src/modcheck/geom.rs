//! Vertex positions of a pack's geometries (xdl_mod._positions over xdl_gltf.Blob, and for the
//! PS2 xdl_gltf.decode with docs/ps2-version/scripts/ps2_geom.py's packet reader), and the
//! per-position hash the index keeps.

use super::media::R;
use std::collections::HashSet;

pub const F_SHADOW: u16 = 0x4000;

pub struct Blob<'a> {
    pub d: &'a [u8],
    pub ptrs: HashSet<u32>,
    pub release: &'static str,
    pub big: bool,
}

impl<'a> Blob<'a> {
    pub fn new(d: &'a [u8], ptrs: &[u32], release: &'static str) -> Blob<'a> {
        Blob { d, ptrs: ptrs.iter().copied().collect(), release, big: release == "gc" }
    }
    pub fn u16(&self, o: usize) -> R<u16> {
        super::media::rd_u16(self.d, o, self.big)
    }
    pub fn u32(&self, o: usize) -> R<u32> {
        super::media::rd_u32(self.d, o, self.big)
    }
    pub fn target(&self, o: usize) -> R<Option<usize>> {
        if o > u32::MAX as usize || !self.ptrs.contains(&(o as u32)) {
            return Ok(None);
        }
        let t = self.u32(o)? as usize;
        Ok(if t < self.d.len() { Some(t) } else { None })
    }
    /// ps2_geom.Pack's reads: 0 past the end
    fn su32(&self, o: usize) -> u32 {
        self.u32(o).unwrap_or(0)
    }
    fn starget(&self, o: usize) -> Option<usize> {
        if o > u32::MAX as usize || !self.ptrs.contains(&(o as u32)) {
            return None;
        }
        let t = self.su32(o) as usize;
        if t < self.d.len() { Some(t) } else { None }
    }
}

/// vertex_hashes: one 64-bit hash per position (little-endian f32 bits, -0 as 0)
pub fn vertex_hash(p: [f32; 3]) -> u64 {
    let u = |x: f32| (x + 0.0f32).to_bits() as u64;
    let (a, b, c) = (u(p[0]), u(p[1]), u(p[2]));
    let mut h = a.wrapping_mul(0x9E3779B97F4A7C15).wrapping_add((b << 21).wrapping_mul(0xC2B2AE3D27D4EB4F));
    h ^= c.wrapping_add(0x165667B19E3779F9).wrapping_mul(0xFF51AFD7ED558CCD);
    h ^= h >> 29;
    h.wrapping_mul(0xC4CEB9FE1A85EC53)
}

fn f32_at(d: &[u8], o: usize, big: bool) -> f32 {
    let a = [d[o], d[o + 1], d[o + 2], d[o + 3]];
    f32::from_bits(if big { u32::from_be_bytes(a) } else { u32::from_le_bytes(a) })
}

/// _positions: the geometry at `off`'s positions, or an error (the geometry is left out).
pub fn positions(blob: &Blob, off: usize) -> R<Vec<[f32; 3]>> {
    let mut out = vec![];
    if blob.release == "ps2" {
        return ps2_positions(blob, off);
    }
    let n = blob.u16(off + 2)?;
    let Some(lods) = blob.target(off + 8)? else { return Ok(out) };
    for li in 0..n as usize {
        let l = lods + 0x18 * li;
        let grp = blob.target(l + 8)?;
        let vb = blob.target(l + 0xC)?;
        let ng = blob.u16(l + 2)?;
        let (Some(grp), Some(vb)) = (grp, vb) else { continue };
        if blob.u16(grp + 2)? & F_SHADOW != 0 {
            continue;
        }
        if blob.release == "gc" {
            let cnt = blob.u32(vb)? as usize / 12;
            let lo = vb + 0x10;
            if cnt > 0 && lo + 12 * cnt <= blob.d.len() {
                for k in 0..cnt {
                    let o = lo + 12 * k;
                    out.push([f32_at(blob.d, o, true), f32_at(blob.d, o + 4, true), f32_at(blob.d, o + 8, true)]);
                }
            }
        } else {
            let pp = blob.target(vb)?;
            let mut top = 0usize;
            for k in 0..ng as usize {
                let g = grp + 0x20 * k;
                let cnt = blob.u32(g + 0x10)? as usize;
                let ix = blob.target(g + 0x1C)?;
                let Some(ix) = ix else { continue };
                if ix + 6 * cnt > blob.d.len() || cnt == 0 {
                    continue;
                }
                let m = (0..3 * cnt).map(|i| u16::from_le_bytes([blob.d[ix + 2 * i], blob.d[ix + 2 * i + 1]])).max().unwrap();
                top = top.max(m as usize + 1);
            }
            if let Some(pp) = pp
                && top > 0
                && pp + 16 * top <= blob.d.len()
            {
                for k in 0..top {
                    let o = pp + 16 * k;
                    out.push([f32_at(blob.d, o, false), f32_at(blob.d, o + 4, false), f32_at(blob.d, o + 8, false)]);
                }
            }
        }
    }
    Ok(out)
}

// ---- PS2: xdl_gltf.decode's checks, ps2_geom's packets -------------------------------------

const LEVEL_SKINNED: u16 = 2;

/// The struct.unpack round trip of an f32 (f32 -> Python float -> f32: a signalling NaN comes
/// back quiet).
fn py_f32(x: f32) -> f32 {
    (x as f64) as f32
}

fn names_len(blob: &Blob, o: usize) -> R<()> {
    let d = blob.d;
    let mut i = o;
    while i < d.len() && d[i] != 0 {
        match d[i..].iter().position(|&c| c == 0) {
            Some(j) => i = i + j + 1,
            None => return Err("subsection not found".into()),
        }
    }
    Ok(())
}

fn ps2_positions(blob: &Blob, off: usize) -> R<Vec<[f32; 3]>> {
    let n = blob.u16(off + 2)? as usize;
    let lods = blob.target(off + 8)?;
    let dist = blob.target(off + 0xC)?;
    let (Some(lods), Some(_)) = (lods, dist) else {
        return Err(format!("0x{off:X}: levels or distances not a pointer"));
    };
    let mut out = vec![];
    for li in 0..n {
        let l = lods + 0x18 * li;
        let ng = blob.u16(l + 2)? as usize;
        let Some(grp) = blob.target(l + 8)? else {
            return Err(format!("0x{off:X} level {li}: groups not a pointer"));
        };
        blob.u16(l)?;
        blob.u32(l + 4)?;
        let shadow = blob.u16(grp + 2)? & F_SHADOW != 0 || blob.u16(l)? == 3;
        match blob.target(l + 0x14)? {
            Some(nm) => names_len(blob, nm)?,
            None => {
                blob.u32(l + 0x14)?;
            }
        }
        match blob.target(l + 0x10)? {
            Some(sk) => {
                for i in 0..8 {
                    blob.u32(sk + 4 * i)?;
                }
            }
            None => {
                blob.u32(l + 0x10)?;
            }
        }
        if !shadow {
            blob.u32(l + 0xC)?;
        }
        let group_rec = |g: usize| -> R<()> {
            blob.u16(g)?;
            blob.u16(g + 2)?;
            blob.u32(g + 8)?;
            blob.u16(g + 0x14)?;
            blob.u16(g + 0x16)?;
            blob.u32(g + 0x18)?;
            Ok(())
        };
        if shadow {
            if blob.target(l + 0xC)?.is_none() {
                blob.u32(l + 0xC)?;
            }
            for k in 0..ng {
                let g = grp + 0x20 * k;
                group_rec(g)?;
                blob.u32(g + 0x10)?;
                blob.u32(g + 0xC)?;
                blob.target(g + 0x1C)?;
            }
            continue;
        }
        let kind = blob.u16(l)?;
        for k in 0..ng {
            let g = grp + 0x20 * k;
            group_rec(g)?;
            blob.u32(g + 0x10)?;
            blob.u32(g + 0x1C)?;
            let Some(pkt) = blob.target(g + 0xC)? else {
                return Err("PS2 group packet not a pointer".into());
            };
            if kind == LEVEL_SKINNED {
                let mut r = pkt + 0x10;
                while blob.u32(r + 4)? != 0 {
                    let sub = blob.target(r + 4)?.ok_or("unsupported operand type(s) for +: 'NoneType' and 'int'")?;
                    blob.u32(sub)?;
                    r += 8;
                }
            } else {
                blob.u32(pkt + 0x10)?;
            }
            group_positions(blob, g, kind, &mut out).map_err(|e| format!("PS2 packet: {e}"))?;
        }
    }
    Ok(out)
}

struct Batch {
    prim: u64,
    nloop: u64,
    nreg: u64,
    pos: Option<Vec<([f32; 3], i32)>>,
    uv: Option<usize>,
    nrm: Option<usize>,
    col: Option<usize>,
}

fn read_batches(blob: &Blob, dma: usize) -> R<Vec<Batch>> {
    let d = blob.d;
    let tag = blob.su32(dma);
    if (tag >> 28) & 7 != 6 {
        return Err(format!("DMA tag id {}, not ret", (tag >> 28) & 7));
    }
    let end = dma + 16 + (tag & 0xFFFF) as usize * 16;
    if end > d.len() {
        return Err("packet runs past the blob".into());
    }
    let mut batches: Vec<Batch> = vec![];
    let mut cycle: Option<u32> = None;
    let mut p = dma + 8;
    let le = |o: usize| u32::from_le_bytes([d[o], d[o + 1], d[o + 2], d[o + 3]]);
    let need = |o: usize, n: usize| -> R<()> {
        if o + n > d.len() { Err(format!("unpack_from requires a buffer of at least {} bytes", o + n)) } else { Ok(()) }
    };
    while p + 4 <= end {
        let w = le(p);
        let (cmd, num, imm) = ((w >> 24) & 0x7F, (w >> 16) & 0xFF, w & 0xFFFF);
        let q = p + 4;
        let mut n = 0usize;
        if cmd & 0x60 == 0x60 {
            let (vn, vl) = ((cmd >> 2) & 3, cmd & 3);
            let size = if vl == 3 { 2 } else { (vn + 1) * (4 >> vl) };
            n = (((if num == 0 { 256 } else { num }) * size + 3) & !3) as usize;
        } else if cmd == 0x20 {
            n = 4;
        } else if cmd == 0x30 || cmd == 0x31 {
            n = 16;
        } else if cmd == 0x4A {
            n = (if num == 0 { 256 } else { num }) as usize * 8;
        } else if cmd == 0x50 || cmd == 0x51 {
            n = (if imm == 0 { 65536 } else { imm }) as usize * 16;
        }
        if cmd == 0x01 {
            cycle = Some(imm);
        } else if cmd & 0x60 == 0x60 {
            let (addr, count) = (imm & 0x3FF, if num == 0 { 256 } else { num } as usize);
            if addr == 0 {
                if cmd != 0x6C || count != 1 || cycle != Some(0x0404) {
                    return Err(format!("GIF tag unpack 0x{cmd:02X} x{count}"));
                }
                need(q, 16)?;
                let lo = u64::from_le_bytes(d[q..q + 8].try_into().unwrap());
                let prim = (lo >> 47) & 0x7FF;
                batches.push(Batch { prim: prim & 7, nloop: lo & 0x7FFF, nreg: (lo >> 60) & 0xF, pos: None, uv: None, nrm: None, col: None });
            } else {
                let Some(cur) = batches.last_mut().filter(|_| cycle == Some(0x0103)) else {
                    return Err("vertex unpack outside a batch".into());
                };
                if addr == 1 && cmd == 0x6C {
                    let mut v = vec![];
                    for i in 0..count {
                        let o = q + 16 * i;
                        need(o, 16)?;
                        v.push((
                            [py_f32(f32_at(d, o, false)), py_f32(f32_at(d, o + 4, false)), py_f32(f32_at(d, o + 8, false))],
                            le(o + 12) as i32,
                        ));
                    }
                    cur.pos = Some(v);
                } else if addr == 2 && cmd == 0x64 {
                    for i in 0..count {
                        need(q + 8 * i, 8)?;
                    }
                    cur.uv = Some(count);
                } else if addr == 3 && cmd == 0x68 {
                    for i in 0..count {
                        need(q + 12 * i, 12)?;
                    }
                    cur.nrm = Some(count);
                } else if addr == 3 && cmd == 0x6E {
                    cur.col = Some(count);
                } else {
                    return Err(format!("unpack 0x{cmd:02X} to VU address {addr}"));
                }
            }
        } else if ![0x00, 0x02, 0x03, 0x17].contains(&cmd) {
            return Err(format!("VIF command 0x{cmd:02X}"));
        }
        p = q + n;
    }
    for b in &batches {
        if b.pos.is_none() || b.uv.is_none() || b.nrm.is_some() == b.col.is_some() {
            return Err("batch without positions, UVs and exactly one of normals/colours".into());
        }
        let np = b.pos.as_ref().unwrap().len() as u64;
        let na = b.nrm.or(b.col).unwrap() as u64;
        if !(np == b.uv.unwrap() as u64 && np == na && np == b.nloop * (b.nreg / 3)) {
            return Err("attribute counts differ from the GIF tag".into());
        }
    }
    Ok(batches)
}

/// batch_triangles' vertices that some triangle uses (their positions), with its errors.
fn batch_positions(b: &Batch, bones: Option<&[u8]>, out: &mut Vec<[f32; 3]>) -> R<()> {
    let pos = b.pos.as_ref().unwrap();
    if let Some(bones) = bones {
        for &(_, w) in pos {
            if !(-1..=1).contains(&w) {
                return Err(format!("matrix slot {w}"));
            }
            let x = *bones.get((w + 1) as usize).ok_or("index out of range")?;
            if x == 0xFF {
                return Err(format!("matrix slot {w}"));
            }
        }
    }
    if b.prim == 4 {
        let mut run = -1i64;
        for (i, &(_, w)) in pos.iter().enumerate() {
            if w & 0x8000 != 0 {
                run = -1;
                continue;
            }
            if i < 2 {
                return Err("strip vertex draws without two before it".into());
            }
            if run < 0 {
                run = 0;
            }
            out.extend([pos[i - 2].0, pos[i - 1].0, pos[i].0]);
            run += 1;
        }
    } else if b.prim == 3 {
        if pos.len() % 3 != 0 {
            return Err(format!("list of {} vertices", pos.len()));
        }
        out.extend(pos.iter().map(|p| p.0));
    } else {
        return Err(format!("GIF primitive {}", b.prim));
    }
    Ok(())
}

fn group_positions(blob: &Blob, grp: usize, kind: u16, out: &mut Vec<[f32; 3]>) -> R<()> {
    let pkt = blob.starget(grp + 0xC).ok_or("group packet is not a pointer")?;
    if kind == LEVEL_SKINNED {
        let mut r = pkt + 0x10;
        while blob.su32(r + 4) != 0 {
            let sub = blob.starget(r + 4).ok_or("skin record pointer is not a slot")?;
            let bones = super::media::slice(blob.d, r, (r + 3) as i64);
            for b in read_batches(blob, sub)? {
                batch_positions(&b, Some(bones), out)?;
            }
            r += 8;
        }
    } else {
        for b in read_batches(blob, pkt + 0x10)? {
            batch_positions(&b, None, out)?;
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    #[test]
    fn vertex_hash_python() {
        // python3 -c "import sys; sys.path.insert(0,'tools'); import xdl_mod as M, numpy as np;
        //   print(hex(M.vertex_hashes(np.array([[1.5,-0.0,2.25]],'<f4'))[0]))"
        assert_eq!(super::vertex_hash([1.5, -0.0, 2.25]), 0x5fcff0936c7ef49c);
        assert_eq!(super::vertex_hash([1.5, 0.0, 2.25]), 0x5fcff0936c7ef49c);
    }
}
