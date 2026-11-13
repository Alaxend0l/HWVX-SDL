//! Button prompts' inventory (docs/plans/v0.4.1.md A1): which textures of each release are a
//! button atlas or a pad picture, which interface objects show them (the cell, the owning
//! interface and player), the texture key animations that pick them, and the text next to them.
//!
//! Reads the archives as they are on the disc (any release; modcheck::media's readers). The
//! interfaces are found as src/game/Port_IfaceScan.c finds them: every target of a pointer slot
//! is tried as an InterfaceHeader and kept when its fields fit. A texture is named by the
//! texture relocation of the slot that holds it ({ slot, id }: id >> 16 == 0 the pack's own
//! texture id & 0xFFFF, else that of the share pack the header's refs name).
//!
//! What a texture is: `atlas` when its decoded pixels are within a small distance of a seed
//! atlas (GC: HW_IPack interface_3 texture 24, by default) at the same size, `pad` likewise
//! for a seed pad picture; the seeds are given as ARCHIVE/PACK:INDEX of a release, so nothing
//! of the game is in this file.

use crate::modcheck::hash::h64;
use crate::modcheck::index::{ARCHIVE_EXT, rglob};
use crate::modcheck::media::{self, Archive, RawPack, Texture};
use std::collections::{BTreeMap, BTreeSet, HashMap};
use std::fmt::Write as _;
use std::path::{Path, PathBuf};

pub type R<T> = std::result::Result<T, String>;

const DEF_SIZE: u32 = 0x6C;
const HDR_SIZE: u32 = 0x40;

/// A pack's data blob with what the scan needs: its byte order, which words are pointer
/// slots, and the texture relocations by slot.
pub struct Blob<'a> {
    pub d: &'a [u8],
    pub big: bool,
    is_ptr: Vec<bool>,
    /// slot -> (pack id, texture index)
    pub tex: HashMap<u32, (u32, u32)>,
}

impl<'a> Blob<'a> {
    pub fn new(pk: &RawPack<'a>) -> Blob<'a> {
        let d = pk.section("data");
        let big = pk.big;
        let mut is_ptr = vec![false; d.len() / 4 + 1];
        for p in pk.words("pointers") {
            if p & 3 == 0 && (p / 4) as usize <= d.len() / 4 {
                is_ptr[(p / 4) as usize] = true;
            }
        }
        let refs = pk.words("ref_relocs");
        for r in refs.chunks_exact(2) {
            if r[1] >> 16 == 0 && r[0] & 3 == 0 && (r[0] / 4) as usize <= d.len() / 4 {
                is_ptr[(r[0] / 4) as usize] = true;
            }
        }
        let mut tex = HashMap::new();
        for r in pk.words("texture_relocs").chunks_exact(2) {
            tex.insert(r[0], (r[1] >> 16, r[1] & 0xFFFF));
        }
        Blob { d, big, is_ptr, tex }
    }

    fn inb(&self, off: u32, n: u32) -> bool {
        (off as usize) <= self.d.len() && (n as usize) <= self.d.len() - off as usize
    }
    pub fn u8(&self, o: u32) -> u8 {
        if self.inb(o, 1) { self.d[o as usize] } else { 0 }
    }
    pub fn u16(&self, o: u32) -> u16 {
        media::rd_u16(self.d, o as usize, self.big).unwrap_or(0)
    }
    pub fn u32(&self, o: u32) -> u32 {
        media::rd_u32(self.d, o as usize, self.big).unwrap_or(0)
    }
    pub fn f32(&self, o: u32) -> f32 {
        f32::from_bits(self.u32(o))
    }
    fn ptr_slot(&self, o: u32) -> bool {
        o & 3 == 0 && self.inb(o, 4) && self.is_ptr[(o / 4) as usize]
    }
    /// The blob offset the slot at `o` points to.
    pub fn to(&self, o: u32) -> Option<u32> {
        let v = self.u32(o);
        (v != 0 && (v as usize) < self.d.len()).then_some(v)
    }
    fn null_or_ptr(&self, o: u32, n: u32) -> bool {
        if self.u32(o) == 0 {
            return true;
        }
        self.ptr_slot(o) && self.to(o).is_some_and(|t| self.inb(t, n))
    }
    fn is_def(&self, d: u32) -> bool {
        if !matches!(self.u8(d + 1), 0 | 1 | 2 | 3 | 4 | 6 | 7 | 10 | 11 | 12) {
            return false;
        }
        let (ax, ay) = (self.f32(d + 0x30), self.f32(d + 0x34));
        if !((-1000.0..=1000.0).contains(&ax) && (-1000.0..=1000.0).contains(&ay)) {
            return false;
        }
        let ns = self.u32(d + 0x64);
        if ns > 64 || !self.null_or_ptr(d + 0x60, 0x28) || !self.null_or_ptr(d + 0x14, 4) || !self.null_or_ptr(d + 0x68, ns * 0xC) {
            return false;
        }
        !matches!(self.u8(d + 1), 6 | 7) || self.ptr_slot(d + 0x3C)
    }
    fn is_header(&self, h: u32) -> bool {
        if h & 3 != 0 || !self.inb(h, HDR_SIZE) {
            return false;
        }
        let n = self.u32(h + 0xC);
        let ev = self.u32(h + 0x14);
        if n == 0 || n > 200 || ev > 256 || self.u8(h + 0x3C) > 4 || self.u32(h + 0x34) > 64 || !self.ptr_slot(h + 0x10) {
            return false;
        }
        if !self.null_or_ptr(h + 0x18, ev * 0xC) || !self.null_or_ptr(h + 0x1C, 8) || !self.null_or_ptr(h + 0x30, 8) {
            return false;
        }
        let Some(objs) = self.to(h + 0x10) else { return false };
        self.inb(objs, n * DEF_SIZE) && (0..n).all(|i| self.is_def(objs + i * DEF_SIZE))
    }
    /// Every interface header of the pack (blob offsets, ascending).
    pub fn headers(&self) -> Vec<u32> {
        let mut seen = BTreeSet::new();
        for (i, &p) in self.is_ptr.iter().enumerate() {
            if !p {
                continue;
            }
            if let Some(t) = self.to(i as u32 * 4)
                && t & 3 == 0
                && !seen.contains(&t)
                && self.is_header(t)
            {
                seen.insert(t);
            }
        }
        seen.into_iter().collect()
    }
    /// A C string at `o`.
    pub fn cstr(&self, o: u32) -> String {
        let s = &self.d[(o as usize).min(self.d.len())..];
        let n = s.iter().position(|&c| c == 0).unwrap_or(s.len()).min(200);
        String::from_utf8_lossy(&s[..n]).into_owned()
    }
}

/// A texture as a reference names it.
#[derive(Clone, Debug, PartialEq, Eq, PartialOrd, Ord, Hash)]
pub struct TexKey {
    pub file: String, // archive file stem
    pub pack: String, // entry name (interface_3)
    pub index: u32,
}

impl std::fmt::Display for TexKey {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}/{}:{}", self.file, self.pack, self.index)
    }
}

/// One way an interface object draws a texture.
#[derive(Clone, Debug)]
pub struct Use {
    pub rel: &'static str,
    pub file: String,
    pub pack: String,
    pub header: u32,
    pub header_id: u16,
    pub mode: u8,
    pub owner: String,
    pub obj: u32,
    pub role: u8,
    pub def_id: u16,
    pub input: u8,
    /// the object it is positioned at (IfaceObjectDef.anchor), as an index
    pub anchor: Option<i64>,
    /// image, texanim[k] (var V), dialog idle/talk/special
    pub how: String,
    pub tex: Option<TexKey>,
    /// (state, u0, v0, u1, v1): the default animation's first UV key ("default"), the states'
    pub uvs: Vec<(String, [f32; 4])>,
    /// the default position and size (first keys)
    pub pos: Option<[f32; 2]>,
    pub size: Option<[f32; 2]>,
}

/// A text of an interface (its first string).
#[derive(Clone, Debug)]
pub struct Text {
    pub rel: &'static str,
    pub file: String,
    pub pack: String,
    pub header: u32,
    pub header_id: u16,
    pub obj: u32,
    pub anchor: Option<i64>,
    pub text: String,
    pub pos: Option<[f32; 2]>,
}

fn uv_track(b: &Blob, track: u32) -> Vec<[f32; 4]> {
    let n = b.u32(track);
    let Some(keys) = b.to(track + 4) else { return vec![] };
    (0..n.min(64))
        .filter(|k| b.inb(keys + k * 0x28, 0x28))
        .map(|k| {
            let o = keys + k * 0x28;
            [b.f32(o), b.f32(o + 4), b.f32(o + 0x18), b.f32(o + 0x1C)]
        })
        .collect()
}

fn vec_first(b: &Blob, track: u32) -> Option<[f32; 2]> {
    if b.u32(track) == 0 {
        return None;
    }
    let k = b.to(track + 4)?;
    Some([b.f32(k), b.f32(k + 4)])
}

const STATE_NAMES: [&str; 6] = ["off", "on", "active", "pressed", "load-in", "load-out"];

/// The UV rects of an object: default animation, then each state's.
fn object_uvs(b: &Blob, d: u32) -> Vec<(String, [f32; 4])> {
    let mut out = vec![];
    if let Some(anim) = b.to(d + 0x60) {
        for (k, r) in uv_track(b, anim + 0x20).into_iter().enumerate() {
            out.push((if k == 0 { "default".into() } else { format!("default[{k}]") }, r));
        }
    }
    if let Some(st) = b.to(d + 0x68) {
        for i in 0..b.u32(d + 0x64).min(64) {
            let s = st + i * 0xC;
            let ty = b.u8(s);
            let name = STATE_NAMES.get(ty as usize).map(|s| s.to_string()).unwrap_or(format!("state{ty}"));
            if let Some(sa) = b.to(s + 8) {
                for (k, r) in uv_track(b, sa + 0x20).into_iter().enumerate() {
                    out.push((if k == 0 { name.clone() } else { format!("{name}[{k}]") }, r));
                }
            }
        }
    }
    out
}

/// The share packs a pack's refs name: (pack id) -> (archive name, id).
fn pack_refs(pk: &RawPack) -> Vec<(String, u32)> {
    let mut out = vec![];
    if pk.quad || pk.b.len() < 120 {
        return out;
    }
    for k in 0..2 {
        let o = 80 + k * 0x14;
        let name = &pk.b[o..o + 0xC];
        let n = name.iter().position(|&c| c == 0).unwrap_or(0xC);
        let id = media::rd_u32(pk.b, o + 0xC, pk.big).unwrap_or(0);
        if n > 0 {
            out.push((String::from_utf8_lossy(&name[..n]).into_owned(), id));
        }
    }
    out
}

/// Who owns each header: roots of interface packs, the car packs' HUDs, the worlds' pause
/// menu, child slots of other headers.
fn owners(b: &Blob, pk: &RawPack, headers: &[u32]) -> HashMap<u32, String> {
    let mut own: HashMap<u32, String> = HashMap::new();
    let typ = pk.typ;
    // triggers (Port_Swap.c Claim_TACTrigger): type 7 InterfaceEnd param -> { header };
    // actions 0x14 / else 0x18 (counts 0x1C / 0x20, 0x18 each): 4 InterfacePlay data -> { header },
    // 31 EndInterface data + 4 -> header
    let to = pk.hget("trigger_offset");
    for t in 0..pk.hget("num_triggers").min(0x10000) {
        let tr = to + t * 0x2C;
        if !b.inb(tr, 0x2C) {
            break;
        }
        if b.u32(tr) == 7
            && let Some(p) = b.to(tr + 4)
            && let Some(h) = b.to(p)
        {
            own.entry(h).or_insert("a trigger (InterfaceEnd)".to_string());
        }
        for (lo, co, what) in [(0x14, 0x1C, "action"), (0x18, 0x20, "else action")] {
            let Some(list) = b.to(tr + lo) else { continue };
            for a in 0..b.u32(tr + co).min(0x100) {
                let ao = list + a * 0x18;
                let Some(data) = b.to(ao + 4) else { continue };
                let h = match b.u32(ao) {
                    4 => b.to(data),
                    31 => b.to(data + 4),
                    _ => None,
                };
                if let Some(h) = h {
                    own.entry(h).or_insert(format!("a trigger {what} (type {})", b.u32(ao)));
                }
            }
        }
    }
    match typ {
        4 => {
            if let Some(gd) = b.to(0x9C)
                && let Some(h) = b.to(gd + 0xCC)
            {
                own.insert(h, "pause menu (world game data 0xCC)".into());
            }
        }
        5 | 8 => {}
        _ => {
            if let Some(table) = b.to(4) {
                for i in 0..b.u32(0).min(4096) {
                    if let Some(obj) = b.to(table + i * 8) {
                        if typ == 1 {
                            own.entry(obj).or_insert(format!("root {i}"));
                        } else if typ == 0
                            && let Some(p) = b.to(obj + 0x84)
                        {
                            for (o, n) in [(0x10, "1P"), (0x18, "2P"), (0x1C, "3-4P")] {
                                if let Some(h) = b.to(p + o) {
                                    own.entry(h).or_insert(format!("car root {i} HUD {n}"));
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    // child interfaces: "obj I (input N) of" the parent's owner, as a chain
    let mut parent: HashMap<u32, (u32, u32, u8)> = HashMap::new();
    for &h in headers {
        let Some(objs) = b.to(h + 0x10) else { continue };
        for i in 0..b.u32(h + 0xC) {
            let d = objs + i * DEF_SIZE;
            if matches!(b.u8(d + 1), 6 | 7)
                && let Some(slot) = b.to(d + 0x3C)
                && let Some(c) = b.to(slot)
            {
                parent.entry(c).or_insert((h, i, b.u8(d + 9)));
            }
        }
    }
    // input events that open another interface (IfaceEventDef types 1, 0x20: target -> header):
    // the header's own (0x14) and its objects' records' (record 0x10)
    let mut opened: HashMap<u32, String> = HashMap::new();
    for &h in headers {
        let mut lists = vec![(h + 0x14, None)];
        if let Some(objs) = b.to(h + 0x10) {
            for i in 0..b.u32(h + 0xC) {
                if let Some(rec) = b.to(objs + i * DEF_SIZE + 0x44) {
                    lists.push((rec + 0x10, Some(i)));
                }
            }
        }
        for (l, obj) in lists {
            let Some(evs) = b.to(l + 4) else { continue };
            for e in 0..b.u32(l).min(256) {
                if let Some(def) = b.to(evs + e * 0xC + 8)
                    && matches!(b.u8(def + 5), 1 | 0x20)
                    && let Some(t) = b.to(def)
                    && t != h
                {
                    let from = obj.map(|i| format!("obj {i} of ")).unwrap_or_default();
                    opened.entry(t).or_insert(format!("opened from {from}id {}", b.u16(h)));
                }
            }
        }
    }
    for (t, s) in opened {
        own.entry(t).or_insert(s);
    }
    let mut out = HashMap::new();
    for &h in headers {
        let mut s = String::new();
        let mut cur = h;
        for _ in 0..8 {
            if let Some(o) = own.get(&cur) {
                s += o;
                break;
            }
            match parent.get(&cur) {
                Some(&(p, i, inp)) => {
                    s += &format!("obj {i}{} of id {} < ", if inp != 0 { format!(" (input {inp})") } else { String::new() }, b.u16(p));
                    cur = p;
                }
                None => {
                    s += "?";
                    break;
                }
            }
        }
        out.insert(h, s);
    }
    out
}

/// Scans one pack: its interfaces' texture uses and texts.
pub fn scan_pack(
    pk: &RawPack,
    file: &str,
    tex_name: &dyn Fn(u32, u32) -> Option<TexKey>,
    uses: &mut Vec<Use>,
    texts: &mut Vec<Text>,
) {
    let b = Blob::new(pk);
    let headers = b.headers();
    if headers.is_empty() {
        return;
    }
    let own = owners(&b, pk, &headers);
    // { slot, pack id << 16 | index }: the caller names the pack (0: this one)
    let name = |slot: u32| -> Option<TexKey> {
        let &(pid, idx) = b.tex.get(&slot)?;
        tex_name(pid, idx)
    };
    for &h in &headers {
        let Some(objs) = b.to(h + 0x10) else { continue };
        let hid = b.u16(h);
        let mode = b.u8(h + 0x3C);
        let owner = own.get(&h).cloned().unwrap_or_default();
        for i in 0..b.u32(h + 0xC) {
            let d = objs + i * DEF_SIZE;
            let ty = b.u8(d + 1);
            let anim = b.to(d + 0x60);
            let an = b.u16(d + 0xC) as i16 as i64;
            let anchor = (an != 0).then_some(an + i as i64);
            let pos = anim.and_then(|a| vec_first(&b, a));
            let size = anim.and_then(|a| vec_first(&b, a + 0x10));
            let mk = |how: String, tex: Option<TexKey>| Use {
                rel: pk.release,
                file: file.to_string(),
                pack: pk.name.clone(),
                header: h,
                header_id: hid,
                mode,
                owner: owner.clone(),
                obj: i,
                role: b.u8(d),
                def_id: b.u16(d + 0xA),
                input: b.u8(d + 9),
                anchor,
                how,
                tex,
                uvs: object_uvs(&b, d),
                pos,
                size,
            };
            let Some(sub) = b.to(d + 0x14) else { continue };
            let tex_anim = |a: u32, label: &str, uses: &mut Vec<Use>| {
                let n = b.u32(a);
                let from_var = b.u8(a + 0xC);
                let var = b.u16(a + 0xE);
                if let Some(keys) = b.to(a + 4) {
                    for k in 0..n.min(0x400) {
                        let v = if from_var != 0 { format!(" (by variable {var})") } else { String::new() };
                        uses.push(mk(format!("{label}[{k}/{n}]{v}"), name(keys + k * 4)));
                    }
                }
            };
            match ty {
                0 => match b.u8(sub) {
                    0 => uses.push(mk("image".into(), name(sub + 4))),
                    3 => {
                        if let Some(a) = b.to(sub + 0x10) {
                            tex_anim(a, "texanim", uses);
                        }
                    }
                    _ => {}
                },
                1 | 12 => {
                    if let Some(tab) = b.to(sub)
                        && let Some(sl) = b.to(tab)
                    {
                        texts.push(Text {
                            rel: pk.release,
                            file: file.to_string(),
                            pack: pk.name.clone(),
                            header: h,
                            header_id: hid,
                            obj: i,
                            anchor,
                            text: b.cstr(sl),
                            pos,
                        });
                    }
                }
                2 => {
                    let n = b.u32(sub);
                    if let Some(ents) = b.to(sub + 4) {
                        let mut chars = BTreeSet::new();
                        for e in 0..n.min(0x400) {
                            let eo = ents + e * 0x20;
                            if let Some(t) = b.to(eo + 0x10)
                                && let Some(sl) = b.to(t)
                            {
                                texts.push(Text {
                                    rel: pk.release,
                                    file: file.to_string(),
                                    pack: pk.name.clone(),
                                    header: h,
                                    header_id: hid,
                                    obj: i,
                                    anchor,
                                    text: format!("dialog {e}: {}", b.cstr(sl)),
                                    pos,
                                });
                            }
                            if let Some(c) = b.to(eo + 0x14) {
                                chars.insert(c);
                            }
                        }
                        for c in chars {
                            for (cnt, lst, label) in [(0x10, 0x14, "dialog idle"), (0x18, 0x1C, "dialog talk")] {
                                if let Some(l) = b.to(c + lst) {
                                    for j in 0..b.u32(c + cnt).min(0x100) {
                                        tex_anim(l + j * 0x10, &format!("{label} {j}"), uses);
                                    }
                                }
                            }
                        }
                    }
                }
                _ => {}
            }
        }
    }
}

/// A decoded texture.
pub struct Pix {
    pub w: usize,
    pub h: usize,
    pub fmt: &'static str,
    pub px: Vec<u8>,
}

impl Pix {
    pub fn hash(&self) -> u64 {
        let mut v = (self.w as u32).to_le_bytes().to_vec();
        v.extend_from_slice(&(self.h as u32).to_le_bytes());
        v.extend_from_slice(&self.px);
        h64(&v)
    }
    /// Mean absolute difference per channel (alpha-weighted colour, and alpha), 0..255.
    pub fn distance(&self, o: &Pix) -> f64 {
        if self.w != o.w || self.h != o.h {
            return f64::INFINITY;
        }
        let mut s = 0u64;
        for (a, b) in self.px.chunks_exact(4).zip(o.px.chunks_exact(4)) {
            for c in 0..3 {
                let x = a[c] as i64 * a[3] as i64 / 255;
                let y = b[c] as i64 * b[3] as i64 / 255;
                s += (x - y).unsigned_abs();
            }
            s += (a[3] as i64 - b[3] as i64).unsigned_abs();
        }
        s as f64 / (self.px.len() as f64)
    }
    /// A perceptual hash the game's recognition uses (src/game/Port_Prompts.c Phash, the same
    /// sums): 8 x 8 blocks, a bit each where the block's alpha, then its alpha-weighted luma, is
    /// above the blocks' mean. Re-encoded copies (CMPR, DXT5, a mixed build's) stay a few bits off.
    pub fn phash(&self) -> (u64, u64) {
        let (bw, bh) = ((self.w / 8).max(1), (self.h / 8).max(1));
        let mut a = [0u64; 64];
        let mut l = [0u64; 64];
        for y in 0..self.h.min(bh * 8) {
            for x in 0..self.w.min(bw * 8) {
                let p = &self.px[(y * self.w + x) * 4..][..4];
                let k = (y / bh) * 8 + x / bw;
                let al = p[3] as u64;
                a[k] += al;
                l[k] += (299 * p[0] as u64 + 587 * p[1] as u64 + 114 * p[2] as u64) * al / 255000;
            }
        }
        let bits = |v: &[u64; 64]| {
            let sum: u64 = v.iter().sum();
            v.iter().enumerate().fold(0u64, |h, (k, &x)| if x * 64 > sum { h | 1 << k } else { h })
        };
        (bits(&a), bits(&l))
    }
    pub fn crop(&self, x: usize, y: usize, w: usize, h: usize) -> Pix {
        let mut px = Vec::with_capacity(w * h * 4);
        for r in y..(y + h).min(self.h) {
            let a = (r * self.w + x.min(self.w)) * 4;
            let e = (r * self.w + (x + w).min(self.w)) * 4;
            px.extend_from_slice(&self.px[a..e]);
        }
        Pix { w: w.min(self.w.saturating_sub(x)), h: h.min(self.h.saturating_sub(y)), fmt: self.fmt, px }
    }
}

pub fn decode(pk: &RawPack, t: &Texture) -> R<Pix> {
    let &(o, n, lw, lh) = t.levels.first().ok_or("no levels")?;
    let clut = t.clut.map(|c| media::slice(pk.b, c, (c + 0x400) as i64));
    let px = media::decode_level(pk.release, t.fmt, media::slice(pk.b, o, (o + n) as i64), lw, lh, clut)?;
    let fmt = media::tex_format(pk.release, t.fmt).map(|f| f.0).unwrap_or("?");
    Ok(Pix { w: lw, h: lh, fmt, px })
}

pub fn write_png(path: &Path, p: &Pix) -> R<()> {
    let f = std::fs::File::create(path).map_err(|e| format!("{}: {e}", path.display()))?;
    let mut enc = png::Encoder::new(std::io::BufWriter::new(f), p.w as u32, p.h as u32);
    enc.set_color(png::ColorType::Rgba);
    enc.set_depth(png::BitDepth::Eight);
    let mut wr = enc.write_header().map_err(|e| format!("{}: {e}", path.display()))?;
    wr.write_image_data(&p.px).map_err(|e| format!("{}: {e}", path.display()))
}

/// One release's archives, loaded.
pub struct Release {
    pub rel: &'static str,
    pub archives: Vec<Archive>,
}

/// The game archives under `dir` (any release; .xgc, .xpc, .xps), the audio ones left out.
pub fn load_dir(dir: &Path) -> R<Vec<Archive>> {
    let mut out = vec![];
    for (p, _) in rglob(dir) {
        let n = p.file_name().map(|n| n.to_string_lossy().to_lowercase()).unwrap_or_default();
        if !ARCHIVE_EXT.iter().any(|e| n.ends_with(e)) || n.starts_with("hwb_m") || n.starts_with("hw_music") {
            continue;
        }
        let bytes = std::fs::read(&p).map_err(|e| format!("{}: {e}", p.display()))?;
        match Archive::from_bytes(bytes, &p) {
            Ok(a) => out.push(a),
            Err(e) => eprintln!("{}: {e}", p.display()),
        }
    }
    Ok(out)
}

pub struct Options {
    pub dirs: Vec<PathBuf>,
    /// release -> seed "FILE/PACK:INDEX" for the atlas and the pad picture
    pub seeds: Vec<(String, String, String)>, // (kind, release, key)
    pub png: Option<PathBuf>,
    pub threshold: f64,
    pub all: bool,
}

fn parse_key(s: &str) -> R<TexKey> {
    let (fp, idx) = s.rsplit_once(':').ok_or(format!("{s}: FILE/PACK:INDEX"))?;
    let (file, pack) = fp.split_once('/').ok_or(format!("{s}: FILE/PACK:INDEX"))?;
    Ok(TexKey { file: file.to_lowercase(), pack: pack.to_string(), index: idx.parse().map_err(|_| format!("{s}: index"))? })
}

fn fmt_uv(r: &[f32; 4], w: usize, h: usize) -> String {
    let (x0, y0, x1, y1) = (r[0] * w as f32, r[1] * h as f32, r[2] * w as f32, r[3] * h as f32);
    format!("({:.0},{:.0})-({:.0},{:.0})", x0, y0, x1, y1)
}

/// The cell names of the GC atlas (32 px cells; docs-pending/prompts-inventory.md).
pub fn cell_px(r: &[f32; 4], w: usize, h: usize) -> (i64, i64, i64, i64) {
    let x0 = (r[0].min(r[2]) * w as f32).round() as i64;
    let y0 = (r[1].min(r[3]) * h as f32).round() as i64;
    let x1 = (r[0].max(r[2]) * w as f32).round() as i64;
    let y1 = (r[1].max(r[3]) * h as f32).round() as i64;
    (x0, y0, x1 - x0, y1 - y0)
}

/// `xdlrs prompts inventory`: the report (text).
pub fn inventory(opt: &Options) -> R<String> {
    let mut rels: Vec<Release> = vec![];
    for d in &opt.dirs {
        let archives = load_dir(d)?;
        let rel = archives.first().map(|a| a.release).ok_or(format!("{}: no archives", d.display()))?;
        rels.push(Release { rel, archives });
    }
    let mut out = String::new();
    // every texture: key -> (pix hash, w, h, fmt); decoded pixels kept for the candidates
    let mut info: BTreeMap<(String, TexKey), (u64, usize, usize, &'static str)> = BTreeMap::new();
    let mut pix: HashMap<u64, Pix> = HashMap::new();
    let mut uses: Vec<Use> = vec![];
    let mut texts: Vec<Text> = vec![];
    for r in &rels {
        // archive name (lower) -> index; share packs by (archive, id)
        let by_name: HashMap<String, usize> = r.archives.iter().enumerate().map(|(i, a)| (a.name.to_lowercase(), i)).collect();
        for a in &r.archives {
            for e in &a.entries {
                if matches!(e.typ, 7 | 9 | 10) {
                    continue;
                }
                let Ok(pk) = RawPack::new(a.release, e.typ, a.pack_bytes(e), &e.name) else { continue };
                if let Ok(ts) = media::scan_textures(&pk) {
                    for t in &ts {
                        if t.w * t.h > 512 * 512 {
                            continue;
                        }
                        if let Ok(p) = decode(&pk, t) {
                            let hh = p.hash();
                            info.insert(
                                (r.rel.to_string(), TexKey { file: a.name.to_lowercase(), pack: e.name.clone(), index: t.i as u32 }),
                                (hh, p.w, p.h, p.fmt),
                            );
                            pix.entry(hh).or_insert(p);
                        }
                    }
                }
                let refs = pack_refs(&pk);
                let file = a.name.to_lowercase();
                let ename = e.name.clone();
                let tex_name = |pid: u32, idx: u32| -> Option<TexKey> {
                    if pid == 0 {
                        return Some(TexKey { file: file.clone(), pack: ename.clone(), index: idx });
                    }
                    let (an, _) = refs.iter().find(|(_, id)| *id == pid)?;
                    let ai = *by_name.get(&an.to_lowercase())?;
                    let se = r.archives[ai].entries.iter().find(|x| x.typ == 8 && x.id == pid)?;
                    Some(TexKey { file: r.archives[ai].name.to_lowercase(), pack: se.name.clone(), index: idx })
                };
                scan_pack(&pk, &file, &tex_name, &mut uses, &mut texts);
            }
        }
    }
    // seeds and classes
    let mut seeds: Vec<(String, String, u64)> = vec![]; // (kind, release, hash)
    for (kind, rel, key) in &opt.seeds {
        let k = parse_key(key)?;
        match info.get(&(rel.clone(), k.clone())) {
            Some(&(h, ..)) => seeds.push((kind.clone(), rel.clone(), h)),
            None => writeln!(out, "seed {kind} {rel} {k}: no such texture").unwrap(),
        }
    }
    // a seed classifies its own release's textures only
    let mut hrel: HashMap<u64, BTreeSet<String>> = HashMap::new();
    for ((rel, _), (hh, ..)) in &info {
        hrel.entry(*hh).or_default().insert(rel.clone());
    }
    let mut class: HashMap<u64, (String, f64)> = HashMap::new();
    for (&hh, p) in &pix {
        let mut best: Option<(String, f64)> = None;
        for (kind, srel, sh) in &seeds {
            if !hrel.get(&hh).is_some_and(|r| r.contains(srel)) {
                continue;
            }
            let d = if *sh == hh { 0.0 } else { p.distance(&pix[sh]) };
            if d <= opt.threshold && best.as_ref().is_none_or(|b| d < b.1) {
                best = Some((format!("{kind} ({srel})"), d));
            }
        }
        if let Some(b) = best {
            class.insert(hh, b);
        }
    }
    // the hash table
    writeln!(out, "## Textures (decoded level 0; hash of w, h, RGBA)\n").unwrap();
    writeln!(out, "| class | distance | hash | phash | release | format | size | copies |").unwrap();
    writeln!(out, "|---|---|---|---|---|---|---|---|").unwrap();
    let mut groups: BTreeMap<(String, u64), Vec<&TexKey>> = BTreeMap::new();
    for ((rel, k), (hh, ..)) in &info {
        if class.contains_key(hh) {
            groups.entry((rel.clone(), *hh)).or_default().push(k);
        }
    }
    let mut rows: Vec<_> = groups.iter().collect();
    rows.sort_by(|a, b| class[&a.0.1].0.cmp(&class[&b.0.1].0).then(a.0.0.cmp(&b.0.0)).then(a.1[0].cmp(b.1[0])));
    for ((rel, hh), keys) in rows {
        let p = &pix[hh];
        let (c, d) = &class[hh];
        let ks: Vec<String> = keys.iter().map(|k| k.to_string()).collect();
        let (pa, pl) = p.phash();
        writeln!(out, "| {c} | {d:.1} | `{hh:016x}` | `{pa:016x}:{pl:016x}` | {rel} | {} | {}x{} | {} |", p.fmt, p.w, p.h, ks.join(", "))
            .unwrap();
    }
    // the phash's margin: per class, the nearest texture of the same size outside it
    writeln!(out, "\n### Perceptual hash margin (bits apart, of 128)\n").unwrap();
    let mut margin: BTreeMap<String, (u32, u32)> = BTreeMap::new(); // class -> (max inside, min outside)
    for (&hh, (c, _)) in &class {
        let (pa, pl) = pix[&hh].phash();
        let e = margin.entry(c.clone()).or_insert((0, u32::MAX));
        for (&oh, op) in &pix {
            if op.w != pix[&hh].w || op.h != pix[&hh].h {
                continue;
            }
            let (qa, ql) = op.phash();
            let d = (pa ^ qa).count_ones() + (pl ^ ql).count_ones();
            if class.get(&oh).is_some_and(|o| &o.0 == c) {
                e.0 = e.0.max(d);
            } else {
                e.1 = e.1.min(d);
            }
        }
    }
    for (c, (i, o)) in &margin {
        writeln!(out, "- {c}: copies within {i}, nearest other texture {o}").unwrap();
    }
    // uses, grouped: an interface whose rows (and texts) are the same in several packs once
    let mut cells: BTreeMap<u64, BTreeSet<(i64, i64, i64, i64)>> = BTreeMap::new();
    let f2 = |v: Option<[f32; 2]>| v.map(|v| format!("{:.0},{:.0}", v[0], v[1])).unwrap_or_default();
    // (rel, file, pack, header) -> rows
    let mut per: BTreeMap<(&str, String, String, u32), (String, Vec<String>)> = BTreeMap::new();
    for u in &uses {
        let Some(k) = &u.tex else { continue };
        let Some(&(hh, w, h, _)) = info.get(&(u.rel.to_string(), k.clone())) else { continue };
        let cl = class.get(&hh).map(|c| c.0.clone());
        if cl.is_none() && !opt.all {
            continue;
        }
        let mut cs: Vec<String> = vec![];
        for (st, r) in &u.uvs {
            cells.entry(hh).or_default().insert(cell_px(r, w, h));
            cs.push(format!("{st} {}", fmt_uv(r, w, h)));
        }
        if u.uvs.is_empty() {
            cs.push("(no UV track)".into());
        }
        let an = u.anchor.map(|a| format!(" at obj {a}")).unwrap_or_default();
        let row = format!(
            "| {} | {}, {}, {} | {} | {} {}x{} | {} | {}{} | {} |",
            u.obj,
            u.role,
            u.def_id,
            u.input,
            u.how,
            cl.unwrap_or_else(|| format!("other {k}")),
            w,
            h,
            cs.join("; "),
            f2(u.pos),
            an,
            f2(u.size)
        );
        let e = per
            .entry((u.rel, u.file.clone(), u.pack.clone(), u.header))
            .or_insert_with(|| (format!("id {}, mode {}, owner: {}", u.header_id, u.mode, u.owner), vec![]));
        e.1.push(row);
    }
    let mut htext: BTreeMap<(&str, String, String, u32), Vec<String>> = BTreeMap::new();
    for t in &texts {
        let k = (t.rel, t.file.clone(), t.pack.clone(), t.header);
        if per.contains_key(&k) {
            let an = t.anchor.map(|a| format!(" at obj {a}")).unwrap_or_default();
            htext.entry(k).or_default().push(format!("obj {} @ {}{}: {:?}", t.obj, f2(t.pos), an, t.text));
        }
    }
    // group by (release, what the header says, rows, texts)
    let mut groups: BTreeMap<(String, String, Vec<String>, Vec<String>), Vec<String>> = BTreeMap::new();
    for (k, (head, rows)) in &per {
        let tx = htext.get(k).cloned().unwrap_or_default();
        groups.entry((k.0.to_string(), head.clone(), rows.clone(), tx)).or_default().push(format!("{}/{} 0x{:X}", k.1, k.2, k.3));
    }
    writeln!(out, "\n## Interfaces that show them ({} interfaces in {} kinds)\n", per.len(), groups.len()).unwrap();
    for ((rel, head, rows, tx), at) in &groups {
        let more = if at.len() > 6 { format!(" and {} more", at.len() - 6) } else { String::new() };
        writeln!(out, "### {rel}: {head}\n\nIn {}: {}{more}\n", at.len(), at[..at.len().min(6)].join(", ")).unwrap();
        writeln!(out, "| obj | role, id, input | how | texture | cell(s) px (state: UV key) | pos | size |").unwrap();
        writeln!(out, "|---|---|---|---|---|---|---|").unwrap();
        for r in rows {
            writeln!(out, "{r}").unwrap();
        }
        if !tx.is_empty() {
            writeln!(out, "\nTexts:\n").unwrap();
            for t in tx {
                writeln!(out, "- {t}").unwrap();
            }
        }
        writeln!(out).unwrap();
    }
    // every text naming CONTINUE (the briefing's prompt), grouped
    writeln!(out, "## Texts naming CONTINUE\n").unwrap();
    let mut ct: BTreeMap<String, Vec<String>> = BTreeMap::new();
    for t in &texts {
        if t.text.to_uppercase().contains("CONTINUE") {
            let an = t.anchor.map(|a| format!(" at obj {a}")).unwrap_or_default();
            let shows = if per.contains_key(&(t.rel, t.file.clone(), t.pack.clone(), t.header)) { " (its interface shows a prompt)" } else { "" };
            ct.entry(format!("{} id {} obj {} @ {}{}{}: {:?}", t.rel, t.header_id, t.obj, f2(t.pos), an, shows, t.text))
                .or_default()
                .push(format!("{}/{} 0x{:X}", t.file, t.pack, t.header));
        }
    }
    for (k, at) in &ct {
        let more = if at.len() > 4 { format!(" and {} more", at.len() - 4) } else { String::new() };
        writeln!(out, "- {k} in {}: {}{more}", at.len(), at[..at.len().min(4)].join(", ")).unwrap();
    }
    if let Some(dir) = &opt.png {
        std::fs::create_dir_all(dir).map_err(|e| format!("{}: {e}", dir.display()))?;
        for (hh, (c, _)) in &class {
            let p = &pix[hh];
            let kind = c.split(' ').next().unwrap_or("x");
            write_png(&dir.join(format!("{kind}_{hh:016x}.png")), p)?;
            for &(x, y, w, h) in cells.get(hh).into_iter().flatten() {
                if w > 0 && h > 0 && x >= 0 && y >= 0 {
                    let cp = p.crop(x as usize, y as usize, w as usize, h as usize);
                    if cp.w > 0 && cp.h > 0 {
                        write_png(&dir.join(format!("{kind}_{hh:016x}_cell_{x}_{y}_{w}x{h}.png")), &cp)?;
                    }
                }
            }
        }
        writeln!(out, "\nPNGs in {}", dir.display()).unwrap();
    }
    Ok(out)
}
