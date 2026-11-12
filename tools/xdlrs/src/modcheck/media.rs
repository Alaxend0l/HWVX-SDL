//! The parts of tools/xdl_media.py the index needs: archives, a pack's sections, texture
//! levels decoded to RGBA, sounds, songs and banks, structural ids.
//! Errors are the Python's messages (they end up in the index's notes).

use super::hash::h16;
use crate::pack::Pairing;
use std::collections::HashMap;
use std::path::{Path, PathBuf};

pub type R<T> = std::result::Result<T, String>;

pub const MAGIC0: u32 = 0x020100A0;
pub const TYPE_NAMES: [&str; 11] =
    ["car", "interface", "item", "link", "world", "collision", "world_texture", "geometry", "share", "audio", "music"];
pub const TYPE_GEOMETRY: u32 = 7;
const STD_HEADER: usize = 120;
const QUAD_HEADER: usize = 84;

fn align(v: usize, a: usize) -> usize {
    v.div_ceil(a) * a
}

fn serr(n: usize, at: usize, len: usize) -> String {
    let _ = (at, len);
    format!("unpack_from requires a buffer of at least {n} bytes")
}

/// Reads with Python struct's bounds: an error past the end.
pub fn rd_u32(b: &[u8], o: usize, big: bool) -> R<u32> {
    if o + 4 > b.len() {
        return Err(serr(o + 4, o, b.len()));
    }
    let a = [b[o], b[o + 1], b[o + 2], b[o + 3]];
    Ok(if big { u32::from_be_bytes(a) } else { u32::from_le_bytes(a) })
}

pub fn rd_u16(b: &[u8], o: usize, big: bool) -> R<u16> {
    if o + 2 > b.len() {
        return Err(serr(o + 2, o, b.len()));
    }
    let a = [b[o], b[o + 1]];
    Ok(if big { u16::from_be_bytes(a) } else { u16::from_le_bytes(a) })
}

/// Python's b[lo:hi] for non-negative lo and any hi.
pub fn slice(b: &[u8], lo: usize, hi: i64) -> &[u8] {
    let lo = lo.min(b.len());
    let hi = if hi < 0 { 0 } else { (hi as usize).min(b.len()) };
    if hi <= lo { &b[lo..lo] } else { &b[lo..hi] }
}

pub fn detect_release(b: &[u8], path: &Path) -> R<&'static str> {
    if rd_u32(b, 0, true)? == MAGIC0 {
        return Ok("gc");
    }
    if rd_u32(b, 0, false)? != MAGIC0 {
        return Err(format!("{}: not a Velocity X archive", path.display()));
    }
    let version = rd_u32(b, 8, false)?;
    let count = rd_u32(b, 12, false)?;
    if version < 183 {
        return Ok("proto");
    }
    if count != 0 {
        let first = rd_u32(b, 16 + 20, false)? as u64;
        if first != 0 && (16 + 24 * count as u64 + first) % 2048 == 0 {
            return Ok("ps2");
        }
        return Ok("pc");
    }
    Ok(if path.to_string_lossy().to_lowercase().ends_with(".xps") { "ps2" } else { "pc" })
}

#[derive(Clone, Debug)]
pub struct Entry {
    pub index: usize,
    pub typ: u32,
    pub id: u32,
    pub sub_id: u32,
    pub start: usize,
    pub size: usize,
    pub name: String,
}

pub struct Archive {
    pub bytes: Vec<u8>,
    pub release: &'static str,
    pub name: String,
    pub entries: Vec<Entry>,
}

/// Path.stem
pub fn stem(p: &Path) -> String {
    let name = p.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
    match name.rfind('.') {
        Some(i) if i > 0 && i < name.len() - 1 => name[..i].to_string(),
        _ => name,
    }
}

impl Archive {
    pub fn from_bytes(bytes: Vec<u8>, path: &Path) -> R<Archive> {
        let release = detect_release(&bytes, path)?;
        if release == "proto" {
            return Err(format!("{}: the PS2 prototype's packs are not supported", path.display()));
        }
        let big = release == "gc";
        let count = rd_u32(&bytes, 12, big)?;
        rd_u32(&bytes, 8, big)?;
        let base = 16 + 24 * count as usize;
        let mut entries = vec![];
        for i in 0..count as usize {
            let o = 16 + 24 * i;
            let w: Vec<u32> = (0..6).map(|k| rd_u32(&bytes, o + 4 * k, big)).collect::<R<_>>()?;
            let (typ, pid, sub, size, off) = (w[1], w[2], w[3], w[4], w[5]);
            let tname = TYPE_NAMES.get(typ as usize).map(|s| s.to_string()).unwrap_or(format!("type{typ}"));
            let name = format!("{tname}_{pid}")
                + &(if typ == TYPE_GEOMETRY { format!("_q{}", sub & 0xFFFF) } else { String::new() });
            entries.push(Entry { index: i, typ, id: pid, sub_id: sub, start: base + off as usize, size: size as usize, name });
        }
        Ok(Archive { bytes, release, name: stem(path), entries })
    }

    pub fn pack_bytes(&self, e: &Entry) -> &[u8] {
        slice(&self.bytes, e.start, (e.start + e.size) as i64)
    }
}

const STD_WORDS: [&str; 20] = [
    "data_size",
    "sound_data_size",
    "num_sound_pointers",
    "num_pointers",
    "num_tex_relocs",
    "num_textures",
    "texture_data_offset",
    "num_sound_relocs",
    "num_sounds",
    "sfx_offset",
    "num_palettes",
    "palette_offset",
    "num_shaders",
    "num_shader_relocs",
    "num_ref_relocs",
    "shader_offset",
    "num_triggers",
    "trigger_offset",
    "song_offset",
    "song_bank_offset",
];

pub struct RawPack<'a> {
    pub release: &'static str,
    pub typ: u32,
    pub b: &'a [u8],
    pub name: String,
    pub big: bool,
    pub quad: bool,
    pub sec: HashMap<&'static str, (usize, usize)>,
    pub h: HashMap<&'static str, u32>,
}

impl<'a> RawPack<'a> {
    pub fn new(release: &'static str, typ: u32, b: &'a [u8], name: &str) -> R<RawPack<'a>> {
        let big = release == "gc";
        let quad = typ == TYPE_GEOMETRY;
        let mut sec = HashMap::new();
        let mut hh = HashMap::new();
        let end;
        if quad {
            let w: Vec<usize> = (0..11).map(|k| rd_u32(b, 4 * k, big).map(|x| x as usize)).collect::<R<_>>()?;
            let mut pos = QUAD_HEADER;
            for (n, len) in [("texture_relocs", w[2] * 8), ("shader_relocs", w[7] * 8), ("ref_relocs", w[10] * 8), ("pointers", w[1] * 4)] {
                sec.insert(n, (pos, len));
                pos += len;
            }
            pos += w[9];
            sec.insert("data", (pos, w[0]));
            end = pos + w[0];
        } else {
            let w: Vec<u32> = (0..20).map(|k| rd_u32(b, 4 * k, big)).collect::<R<_>>()?;
            for (k, v) in STD_WORDS.iter().zip(&w) {
                hh.insert(*k, *v);
            }
            let h = |k: &str| hh[k] as usize;
            let mut pos = STD_HEADER;
            if release == "ps2" {
                pos = align(pos, 2048);
            }
            if h("sound_data_size") != 0 {
                sec.insert("sound_pointers", (pos, h("num_sound_pointers") * 4));
                pos += align(h("num_sound_pointers") * 4, 32);
                sec.insert("sound_data", (pos, h("sound_data_size")));
                pos += h("sound_data_size");
                if release == "ps2" {
                    pos = align(pos, 2048);
                }
            }
            let n = (h("num_tex_relocs") + h("num_shader_relocs") + h("num_sound_relocs")) * 8;
            sec.insert("texture_relocs", (pos, h("num_tex_relocs") * 8));
            sec.insert("shader_relocs", (pos + h("num_tex_relocs") * 8, h("num_shader_relocs") * 8));
            sec.insert("sound_relocs", (pos + (h("num_tex_relocs") + h("num_shader_relocs")) * 8, h("num_sound_relocs") * 8));
            pos += align(n, 32);
            sec.insert("data", (pos, h("data_size")));
            pos += h("data_size");
            sec.insert("ref_relocs", (pos, h("num_ref_relocs") * 8));
            pos += h("num_ref_relocs") * 8;
            sec.insert("pointers", (pos, h("num_pointers") * 4));
            end = pos + h("num_pointers") * 4;
        }
        if end != b.len() {
            return Err(format!("{name}: pack layout ends at 0x{end:X}, the pack is 0x{:X} bytes", b.len()));
        }
        Ok(RawPack { release, typ, b, name: name.to_string(), big, quad, sec, h: hh })
    }

    pub fn hget(&self, k: &str) -> u32 {
        self.h.get(k).copied().unwrap_or(0)
    }

    pub fn section(&self, name: &str) -> &'a [u8] {
        let (s, n) = self.sec.get(name).copied().unwrap_or((0, 0));
        slice(self.b, s, (s + n) as i64)
    }

    pub fn words(&self, name: &str) -> Vec<u32> {
        let d = self.section(name);
        d.chunks_exact(4)
            .map(|c| if self.big { u32::from_be_bytes(c.try_into().unwrap()) } else { u32::from_le_bytes(c.try_into().unwrap()) })
            .collect()
    }
}

// ---- textures ------------------------------------------------------------------------------

/// (release, format) -> (name, block, bytes per block)
pub fn tex_format(rel: &str, fmt: u16) -> Option<(&'static str, usize, usize)> {
    Some(match (rel, fmt) {
        ("gc", 4) => ("CMPR", 8, 32),
        ("gc", 1) => ("RGBA8", 4, 64),
        ("pc", 4) => ("DXT1", 4, 8),
        ("pc", 5) => ("DXT5", 4, 16),
        ("ps2", 1) => ("PSMCT32", 1, 4),
        ("ps2", 2) => ("PSMT8", 1, 1),
        _ => return None,
    })
}

pub fn level_size(rel: &str, fmt: u16, w: usize, h: usize) -> usize {
    let (_, blk, nb) = tex_format(rel, fmt).unwrap();
    w.div_ceil(blk) * h.div_ceil(blk) * nb
}

pub fn level_dims(w: usize, h: usize, k: usize) -> (usize, usize) {
    ((w >> k).max(1), (h >> k).max(1))
}

pub struct Texture {
    pub i: usize,
    pub fmt: u16,
    pub w: usize,
    pub h: usize,
    pub mips: u16,
    pub ptr: u32,
    pub word: u32,
    pub levels: Vec<(usize, usize, usize, usize)>,
    pub clut: Option<usize>,
}

pub fn scan_textures(pk: &RawPack) -> R<Vec<Texture>> {
    if pk.quad || pk.hget("num_textures") == 0 {
        return Ok(vec![]);
    }
    let rel = pk.release;
    let (d0, dn) = pk.sec["data"];
    let stride = if rel == "gc" { 0x20 } else { 0x10 };
    let mut out = vec![];
    for i in 0..pk.hget("num_textures") as usize {
        let o = d0 + pk.hget("texture_data_offset") as usize + stride * i;
        let fmt = rd_u16(pk.b, o, false)?;
        let w = rd_u16(pk.b, o + 2, false)? as usize;
        let h = rd_u16(pk.b, o + 4, false)? as usize;
        let mips = rd_u16(pk.b, o + 6, false)?;
        let ptr = rd_u32(pk.b, o + 8, pk.big)?;
        let word = rd_u32(pk.b, o + 12, pk.big)?;
        if tex_format(rel, fmt).is_none() {
            return Err(format!("{}: texture {i}: {rel} format {fmt} not supported", pk.name));
        }
        out.push(Texture { i, fmt, w, h, mips, ptr, word, levels: vec![], clut: None });
    }
    let mut starts: Vec<usize> = out.iter().map(|t| d0 + t.ptr as usize).collect();
    starts.sort();
    for t in out.iter_mut() {
        let n = if (rel, t.fmt) == ("gc", 1) { 1 } else { (t.mips as usize).max(1) };
        let mut p = d0 + t.ptr as usize;
        let nxt = starts.iter().copied().find(|&s| s > p).unwrap_or(d0 + dn);
        for k in 0..n {
            let (lw, lh) = level_dims(t.w, t.h, k);
            let size = level_size(rel, t.fmt, lw, lh);
            if p + size > nxt && rel == "ps2" && k == n - 1 && k > 0 {
                break;
            }
            t.levels.push((p, size, lw, lh));
            p += size;
        }
        if rel == "ps2" && t.fmt == 2 {
            t.clut = Some(d0 + pk.hget("palette_offset") as usize + 0x400 * t.word as usize);
        }
    }
    Ok(out)
}

/// texture_fingerprint (pairing.md §3.1 `x`)
pub fn texture_fingerprint(pk: &RawPack, t: &Texture) -> String {
    let mut d = vec![];
    for v in [t.fmt, t.w as u16, t.h as u16, t.mips] {
        d.extend_from_slice(&v.to_le_bytes());
    }
    let mut p = pk.sec["data"].0 + t.ptr as usize;
    for k in 0..(t.mips as usize).max(1) {
        let (lw, lh) = level_dims(t.w, t.h, k);
        let n = level_size(pk.release, t.fmt, lw, lh);
        d.extend_from_slice(slice(pk.b, p, (p + n) as i64));
        p += n;
    }
    if let Some(c) = t.clut {
        d.extend_from_slice(slice(pk.b, c, (c + 0x400) as i64));
    }
    h16(&d)
}

fn c565(c: i64) -> [i64; 3] {
    [((c >> 11) & 31) * 255 / 31, ((c >> 5) & 63) * 255 / 63, (c & 31) * 255 / 31]
}

fn palette(c0: i64, c1: i64, four_always: bool, transparent: bool) -> [[i64; 4]; 4] {
    let (a, b) = (c565(c0), c565(c1));
    let four = four_always || c0 > c1;
    let mut p = [[0i64; 4]; 4];
    for ch in 0..3 {
        p[0][ch] = a[ch];
        p[1][ch] = b[ch];
        p[2][ch] = if four { (2 * a[ch] + b[ch]) / 3 } else { (a[ch] + b[ch]) / 2 };
        p[3][ch] = if four { (a[ch] + 2 * b[ch]) / 3 } else { 0 };
    }
    for e in p.iter_mut() {
        e[3] = 255;
    }
    if transparent {
        p[3][3] = if four { 255 } else { 0 };
    }
    p
}

const CSM: fn(usize) -> usize = |i| (i & !0x18) | ((i & 0x08) << 1) | ((i & 0x10) >> 1);

/// decode_level: one level's stored bytes -> RGBA (h * w * 4). A short level is an error, as
/// numpy's reshape is.
pub fn decode_level(rel: &str, fmt: u16, raw: &[u8], w: usize, h: usize, clut: Option<&[u8]>) -> R<Vec<u8>> {
    let (name, _, _) = tex_format(rel, fmt).ok_or_else(|| format!("{rel} texture format {fmt}"))?;
    let need = level_size(rel, fmt, w, h);
    if raw.len() != need {
        return Err(format!("cannot reshape array of size {} into the level's shape", raw.len()));
    }
    let mut img = vec![0u8; w * h * 4];
    let mut put = |x: usize, y: usize, px: [i64; 4]| {
        if x < w && y < h {
            let o = 4 * (y * w + x);
            for c in 0..4 {
                img[o + c] = px[c] as u8;
            }
        }
    };
    match name {
        "DXT1" | "DXT5" => {
            let (bw, bh) = (w.div_ceil(4), h.div_ceil(4));
            let bs = if name == "DXT5" { 16 } else { 8 };
            for by in 0..bh {
                for bx in 0..bw {
                    let blk = &raw[(by * bw + bx) * bs..(by * bw + bx + 1) * bs];
                    let mut alpha = None;
                    let col = if name == "DXT5" {
                        let (a0, a1) = (blk[0] as i64, blk[1] as i64);
                        let mut bits = 0u64;
                        for k in 0..6 {
                            bits |= (blk[2 + k] as u64) << (8 * k);
                        }
                        let mut tab = [0i64; 8];
                        for k in 0..8i64 {
                            tab[k as usize] = if k == 0 {
                                a0
                            } else if k == 1 {
                                a1
                            } else if a0 > a1 {
                                ((8 - k) * a0 + (k - 1) * a1) / 7
                            } else if k == 6 {
                                0
                            } else if k == 7 {
                                255
                            } else {
                                ((6 - k) * a0 + (k - 1) * a1) / 5
                            };
                        }
                        let a: Vec<i64> = (0..16).map(|k| tab[((bits >> (3 * k)) & 7) as usize]).collect();
                        alpha = Some(a);
                        &blk[8..]
                    } else {
                        blk
                    };
                    let c0 = col[0] as i64 | (col[1] as i64) << 8;
                    let c1 = col[2] as i64 | (col[3] as i64) << 8;
                    let pal = palette(c0, c1, name == "DXT5", false);
                    for y in 0..4 {
                        for x in 0..4 {
                            let mut px = pal[((col[4 + y] >> (2 * x)) & 3) as usize];
                            if let Some(a) = &alpha {
                                px[3] = a[y * 4 + x];
                            }
                            put(bx * 4 + x, by * 4 + y, px);
                        }
                    }
                }
            }
        }
        "CMPR" => {
            let (tw, th) = (w.div_ceil(8), h.div_ceil(8));
            for ty in 0..th {
                for tx in 0..tw {
                    for sy in 0..2 {
                        for sx in 0..2 {
                            let o = (((ty * tw + tx) * 2 + sy) * 2 + sx) * 8;
                            let blk = &raw[o..o + 8];
                            let c0 = (blk[0] as i64) << 8 | blk[1] as i64;
                            let c1 = (blk[2] as i64) << 8 | blk[3] as i64;
                            let pal = palette(c0, c1, false, true);
                            for y in 0..4 {
                                for x in 0..4 {
                                    let px = pal[((blk[4 + y] >> (6 - 2 * x)) & 3) as usize];
                                    put(tx * 8 + sx * 4 + x, ty * 8 + sy * 4 + y, px);
                                }
                            }
                        }
                    }
                }
            }
        }
        "RGBA8" => {
            let (tw, th) = (w.div_ceil(4), h.div_ceil(4));
            for ty in 0..th {
                for tx in 0..tw {
                    let o = (ty * tw + tx) * 64;
                    for k in 0..16 {
                        let (a, r) = (raw[o + 2 * k], raw[o + 2 * k + 1]);
                        let (g, b) = (raw[o + 32 + 2 * k], raw[o + 32 + 2 * k + 1]);
                        put(tx * 4 + k % 4, ty * 4 + k / 4, [r as i64, g as i64, b as i64, a as i64]);
                    }
                }
            }
        }
        "PSMCT32" => img.copy_from_slice(raw),
        "PSMT8" => {
            let c = clut.unwrap_or(&[]);
            if c.len() != 0x400 {
                return Err(format!("cannot reshape array of size {} into shape (256,4)", c.len()));
            }
            for (i, &ix) in raw.iter().enumerate() {
                let s = CSM(ix as usize) * 4;
                img[4 * i..4 * i + 4].copy_from_slice(&c[s..s + 4]);
            }
        }
        _ => unreachable!(),
    }
    Ok(img)
}

// ---- sounds ------------------------------------------------------------------------------

pub struct Sound {
    pub i: usize,
    pub ptr: usize,
    pub len: usize,
    pub rate: u32,
}

pub fn scan_sounds(pk: &RawPack) -> R<Vec<Sound>> {
    if pk.hget("num_sounds") == 0 {
        return Ok(vec![]);
    }
    let s0 = pk.sec.get("sound_data").ok_or("'sound_data'")?.0;
    let mut out = vec![];
    for i in 0..pk.hget("num_sounds") as usize {
        let o = s0 + pk.hget("sfx_offset") as usize + 0x20 * i;
        let ptr = rd_u32(pk.b, o, pk.big)?;
        let len = rd_u32(pk.b, o + 4, pk.big)?;
        rd_u32(pk.b, o + 8, pk.big)?;
        let rate = rd_u32(pk.b, o + 12, pk.big)?;
        if o + 0x12 > pk.b.len() {
            return Err("index out of range".into());
        }
        rd_u16(pk.b, o + 0x12, pk.big)?;
        if slice(pk.b, o + 0x14, (o + 0x20) as i64).iter().any(|&x| x != 0) {
            return Err(format!("{}: sound {i}: descriptor bytes 0x14-0x1F aren't zero", pk.name));
        }
        out.push(Sound { i, ptr: s0 + ptr as usize, len: len as usize, rate });
    }
    Ok(out)
}

pub struct BankEntry {
    pub k: usize,
    pub off: usize,
    pub len: usize,
    pub rate: u32,
}

/// scan_song: (the song's SMF range, the bank's entries and data offset)
#[allow(clippy::type_complexity)]
pub fn scan_song(pk: &RawPack) -> R<(Option<(usize, i64)>, Option<(Vec<BankEntry>, usize)>)> {
    if pk.quad || !pk.sec.contains_key("sound_data") {
        return Ok((None, None));
    }
    let s0 = pk.sec["sound_data"].0;
    let b = pk.b;
    let so = s0 + pk.hget("song_offset") as usize;
    let bo = s0 + pk.hget("song_bank_offset") as usize;
    let mut song = None;
    let mut bank = None;
    if slice(b, so, (so + 4) as i64) == b"RIFX" && slice(b, so + 8, (so + 12) as i64) == b"MIDI" {
        let size = rd_u32(b, so + 4, true)? as i64;
        song = Some((so + 12, size - 4));
    }
    let tag = slice(b, bo, (bo + 4) as i64);
    if pk.hget("song_bank_offset") != 0 && (tag == b"RIFX" || tag == b"RIFF") {
        let big = tag == b"RIFX";
        rd_u32(b, bo + 4, big)?;
        if bo + 24 > b.len() {
            return Err("unpack_from requires a buffer of at least 24 bytes".into());
        }
        let kind = &b[bo + 8..bo + 12];
        let count = rd_u32(b, bo + 16, big)? as usize;
        let table = rd_u32(b, bo + 20, big)? as usize;
        if kind != b"sndb" || table != 0x20 + 0x20 * count {
            return Err(format!("{}: bank at 0x{bo:X} isn't a sndb bank this tool reads", pk.name));
        }
        rd_u32(b, bo + 0x1C, big)?;
        let mut ents = vec![];
        for k in 0..count {
            let q = bo + 0x20 + 0x20 * k;
            let off = rd_u32(b, q, big)? as usize;
            let len = rd_u32(b, q + 4, big)? as usize;
            rd_u32(b, q + 8, big)?;
            let rate = rd_u32(b, q + 12, big)?;
            rd_u16(b, q + 0x12, big)?;
            if q + 0x18 > b.len() {
                return Err("index out of range".into());
            }
            for j in 0..4 {
                rd_u16(b, q + 0x18 + 2 * j, big)?;
            }
            ents.push(BankEntry { k, off, len, rate });
        }
        bank = Some((ents, bo + table));
    }
    Ok((song, bank))
}

fn adpcm_decode(buf: &[u8]) -> Vec<i16> {
    const F: [(i64, i64); 5] = [(0, 0), (60, 0), (115, -52), (98, -55), (122, -60)];
    let mut out = Vec::with_capacity(buf.len() / 16 * 28);
    let (mut s1, mut s2) = (0i64, 0i64);
    let mut p = 0;
    while p + 16 <= buf.len() {
        let (shift, filt) = ((buf[p] & 15) as i64, (buf[p] >> 4) as usize);
        let (f0, f1) = if filt < 5 { F[filt] } else { (0, 0) };
        for k in 0..28 {
            let mut n = ((buf[p + 2 + k / 2] >> (4 * (k & 1))) & 15) as i64;
            if n >= 8 {
                n -= 16;
            }
            let mut s = if shift <= 12 { (n << 12) >> shift } else { 0 };
            s += (s1 * f0 + s2 * f1 + 32) >> 6;
            s = s.clamp(-32768, 32767);
            out.push(s as i16);
            s2 = s1;
            s1 = s;
        }
        p += 16;
    }
    out
}

/// decode_sample: a sample's PCM16 as the game plays it.
pub fn decode_sample(rel: &str, raw: &[u8], what: &str) -> R<Vec<i16>> {
    if rel == "ps2" {
        if raw.len() % 16 != 0 || raw.len() < 16 || raw[0] != 0x0C || raw[2..16].iter().any(|&x| x != 0) {
            return Err(format!("{what}: SPU-ADPCM doesn't start with the silent block"));
        }
        return Ok(adpcm_decode(raw)[28..].to_vec());
    }
    if raw.len() % 2 != 0 {
        return Err("buffer size must be a multiple of element size".into());
    }
    Ok(raw
        .chunks_exact(2)
        .map(|c| if rel == "gc" { i16::from_be_bytes([c[0], c[1]]) } else { i16::from_le_bytes([c[0], c[1]]) })
        .collect())
}

// ---- structural ids (pairing.md §1) ------------------------------------------------------

pub struct Ids {
    pub pairing: Pairing,
    pub dir: Option<PathBuf>,
}

impl Ids {
    pub fn new(dir: Option<PathBuf>) -> Ids {
        Ids { pairing: Pairing::new(dir.clone()), dir }
    }

    /// The archive's GameCube spelling, from the pairing table's first line.
    pub fn archive(&self, st: &str) -> String {
        if let Some(d) = &self.dir
            && let Ok(rd) = std::fs::read_dir(d)
        {
            for e in rd.flatten() {
                let p = e.path();
                let n = p.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
                if !n.ends_with(".jsonl") || stem(&p).to_lowercase() != st.to_lowercase() {
                    continue;
                }
                let text = std::fs::read_to_string(&p).unwrap_or_default();
                let line = text.lines().next().unwrap_or("");
                return serde_json::from_str::<serde_json::Value>(line)
                    .ok()
                    .and_then(|v| v.get("archive").and_then(|a| a.as_str()).map(String::from))
                    .unwrap_or_else(|| stem(&p));
            }
        }
        st.to_string()
    }

    pub fn asset(&self, rel: &str, archive: &str, pack: &str, kind: &str, i: usize) -> String {
        self.pairing
            .id(rel, archive, pack, kind, i as i64)
            .unwrap_or_else(|_| format!("base://{archive}/{pack}/{kind}/{rel}:{i}"))
    }
}
