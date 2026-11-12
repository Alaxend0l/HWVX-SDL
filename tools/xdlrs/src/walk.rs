//! The Port_Swap claim walkers as xdl_graph's Extractor uses them (comp_survey.label_pack over
//! docs/beta-cars/research/scripts/walk.py and walk_im.py): object labels of a pack.
//!
//! A faithful port: the claims are kept (a walker's targets depend on them), the order of the
//! walks is the Python's, and the first label at an offset wins (`s.objs`). Where the Python
//! raises (an index past the blob, a table file of odd length, a header key missing), this
//! returns an error: xdl_graph catches every exception there and uses no labels at all.
//! Offsets are i64 (Python ints: sums past 2^32 stay out of range, they don't wrap).

use crate::error::{Error, Result};
use serde_json::Value;
use std::collections::{BTreeMap, BTreeSet};
use std::path::Path;

/// comp_survey.KINDS: the walker labels that are component kinds (own files in a project).
pub const KINDS: &[&str] = &[
    "EventSeq",
    "ParticleEmitter",
    "Particle",
    "Projectile",
    "Blast",
    "Tracer",
    "AttachData",
    "EmitterData",
    "LineDef",
    "RadarBlip",
    "LightDef",
    "SoundSeq",
    "Sound",
    "CarASoundSet*[]",
    "CarAEngine",
    "CameraSubMode[]",
    "CarAHud",
    "CarAStuntInfo",
    "CarAAccel",
    "CarACollider",
    "CarAForcePoints",
    "CarTrailBone[]",
    "CarAGear[]",
    "CarAStartSlave[]",
    "CarASlaveSet[](slaves)",
    "CarASlaveSet[](weapons)",
    "CarASlaveSet[](pickup)",
    "SlaveTrigger[]",
    "AICarScript",
    "AIStateInfo",
    "CarAModelRef",
    "MotionAnim",
    "KeyAnim",
    "Skeleton",
    "CarAInfo",
    "ThingRecord",
    "DestructibleInfo",
    "TileCar",
    "CreatureInfo",
    "WorldGameData",
    "TilePath",
    "Trigger",
    "Shader",
    "ItemInfo",
    "WeaponInfo",
    "IMHeader",
    "IMObject[]",
    "Font",
    "StateAnim",
    "ObjAnim",
    "EventDef",
    "Geometry",
];

const DEFAULT: u8 = 0;
const PTR: u8 = 1;
const U16: u8 = 2;
const BYTES: u8 = 3;
const U32: u8 = 4;
const TEXREF: u8 = 5;
const SNDREF: u8 = 6;
const REFREL: u8 = 7;
const STR: u8 = 10;
const NONE: i64 = 0xFFFF_FFFF;

#[derive(Clone, Copy)]
enum Keys {
    Active,
    Vec4,
    Color,
    Uv,
    F32,
    Point,
}

struct W {
    blob: Vec<u8>,
    big: bool,
    size: i64,
    claim: Vec<u8>,
    objs: BTreeMap<u32, String>,
    geom_roots: BTreeSet<i64>,
    pointers: Vec<u32>,
    refs: Vec<(u32, u32)>,
    header: Value,
    es_depth: i32,
    sl_depth: i32,
}

fn index_error(what: &str, i: i64) -> Error {
    Error::uncaught(format!("IndexError: {what} index out of range ({i})"))
}

/// Python's `b[lo:hi] = v` on a bytearray (indices clipped; past the end it grows).
fn slice_assign(b: &mut Vec<u8>, lo: usize, hi: usize, v: &[u8]) {
    let n = b.len();
    let lo = lo.min(n);
    let hi = hi.min(n).max(lo);
    b.splice(lo..hi, v.iter().copied());
}

fn table(dir: &Path, name: &str, w: usize, big: bool) -> Result<Vec<(u32, u32)>> {
    let p = dir.join(format!("{name}.bin"));
    if !p.exists() {
        return Ok(vec![]);
    }
    let b = crate::error::read(&p)?;
    let rd = |o: usize| {
        let x: [u8; 4] = b[o..o + 4].try_into().unwrap();
        if big { u32::from_be_bytes(x) } else { u32::from_le_bytes(x) }
    };
    let mut out = vec![];
    if w == 4 {
        if b.len() % 4 != 0 {
            return Err(Error::strct(format!("unpack requires a buffer of {} bytes", b.len() / 4 * 4)));
        }
        for o in (0..b.len()).step_by(4) {
            out.push((rd(o), 0));
        }
    } else {
        for o in (0..b.len()).step_by(8) {
            if o + 8 > b.len() {
                return Err(Error::strct("unpack_from requires a buffer of at least 8 bytes".to_string()));
            }
            out.push((rd(o), rd(o + 4)));
        }
    }
    Ok(out)
}

impl W {
    fn open(dir: &Path, big: bool) -> Result<W> {
        let blob = crate::error::read(&dir.join("data.bin"))?;
        let js: Value = serde_json::from_str(&crate::error::read_text(&dir.join("pack.json"))?)
            .map_err(|e| Error::json(format!("pack.json: {e}")))?;
        let header = js.get("header").cloned().ok_or_else(|| Error::uncaught("KeyError: 'header'"))?;
        let pointers: Vec<u32> = table(dir, "pointers", 4, big)?.into_iter().map(|x| x.0).collect();
        let tex = table(dir, "texture_relocs", 8, big)?;
        let snd = table(dir, "sound_relocs", 8, big)?;
        let refs = table(dir, "ref_relocs", 8, big)?;
        let _shd = table(dir, "shader_relocs", 8, big)?;
        let size = blob.len() as i64;
        let mut claim = vec![DEFAULT; blob.len()];
        for &o in &pointers {
            slice_assign(&mut claim, o as usize, o as usize + 4, &[PTR; 4]);
        }
        for &(o, v) in &refs {
            let k = if v >> 16 == 0 { PTR } else { REFREL };
            slice_assign(&mut claim, o as usize, o as usize + 4, &[k; 4]);
        }
        for (t, k) in [(&tex, TEXREF), (&snd, SNDREF)] {
            for &(o, _) in t.iter() {
                let c = *claim.get(o as usize).ok_or_else(|| index_error("bytearray", o as i64))?;
                if c == DEFAULT {
                    slice_assign(&mut claim, o as usize, o as usize + 4, &[k; 4]);
                }
            }
        }
        Ok(W {
            blob,
            big,
            size,
            claim,
            objs: BTreeMap::new(),
            geom_roots: BTreeSet::new(),
            pointers,
            refs,
            header,
            es_depth: 0,
            sl_depth: 0,
        })
    }

    // --- primitive access ---------------------------------------------------------------
    fn inb(&self, off: i64, n: i64) -> bool {
        off != NONE && 0 <= off && off < self.size && n <= self.size - off
    }

    fn word(&self, off: i64) -> i64 {
        if !self.inb(off, 4) {
            return 0;
        }
        let o = off as usize;
        let x: [u8; 4] = self.blob[o..o + 4].try_into().unwrap();
        (if self.big { u32::from_be_bytes(x) } else { u32::from_le_bytes(x) }) as i64
    }

    fn f32(&self, off: i64) -> f64 {
        if !self.inb(off, 4) {
            return 0.0;
        }
        f32::from_bits(self.word(off) as u32) as f64
    }

    fn claim_at(&self, i: i64) -> Result<u8> {
        if i < 0 {
            // Python's negative index counts from the end
            let j = self.claim.len() as i64 + i;
            return self.claim.get(j as usize).copied().filter(|_| j >= 0).ok_or_else(|| index_error("bytearray", i));
        }
        self.claim.get(i as usize).copied().ok_or_else(|| index_error("bytearray", i))
    }

    fn blob_at(&self, i: i64) -> Result<i64> {
        if i < 0 {
            let j = self.blob.len() as i64 + i;
            return self.blob.get(j as usize).map(|&b| b as i64).filter(|_| j >= 0).ok_or_else(|| index_error("index", i));
        }
        self.blob.get(i as usize).map(|&b| b as i64).ok_or_else(|| index_error("index", i))
    }

    fn target(&self, off: i64) -> i64 {
        if !self.inb(off, 4) || self.claim[off as usize] != PTR {
            return NONE;
        }
        let v = self.word(off);
        if v < self.size { v } else { NONE }
    }

    fn cl(&mut self, off: i64, n: i64, kind: u8) {
        if off == NONE || !self.inb(off, n) {
            return;
        }
        for i in off..off + n {
            let c = &mut self.claim[i as usize];
            if *c != PTR && *c != REFREL {
                *c = kind;
            }
        }
    }

    fn u16(&mut self, off: i64) {
        self.cl(off, 2, U16)
    }

    fn bytes_(&mut self, off: i64, n: i64) {
        self.cl(off, n, BYTES)
    }

    fn lab(&mut self, off: i64, _n: i64, name: &str) {
        if off == NONE || !self.inb(off, 1) {
            return;
        }
        self.objs.entry(off as u32).or_insert_with(|| name.to_string());
    }

    // --- walk.py --------------------------------------------------------------------------
    fn key_track(&mut self, track: i64, stride: i64, bytes_at: i64, name: &str) {
        let keys = self.target(track + 4);
        let n = self.word(track);
        if keys == NONE {
            return;
        }
        let n = n.min(0x10000);
        self.lab(keys, n * stride, name);
        for i in 0..n {
            let k = keys + i * stride;
            self.bytes_(k + bytes_at, 4);
            let mut w = 0;
            while w < stride {
                if w != bytes_at && self.inb(k + w, 4) && self.claim[(k + w) as usize] == DEFAULT {
                    self.cl(k + w, 4, U32);
                }
                w += 4;
            }
        }
    }

    fn track(&mut self, t: i64, kind: Keys) {
        match kind {
            Keys::Active => self.key_track(t, 8, 0, "keys.active"),
            Keys::Vec4 => self.key_track(t, 0x20, 0x10, "keys.vec4"),
            Keys::Uv => self.key_track(t, 0x28, 0x20, "keys.uv"),
            Keys::F32 => self.key_track(t, 0xC, 4, "keys.f32"),
            Keys::Point => self.key_track(t, 0x10, 0xC, "keys.point"),
            Keys::Color => {
                self.key_track(t, 0xC, 0, "keys.color");
                self.key_track(t, 0xC, 4, "keys.color");
            }
        }
    }

    fn sound(&mut self, off: i64) {
        if off != NONE {
            self.lab(off, 0x1C, "Sound");
            self.bytes_(off, 8);
        }
    }

    fn sound_seq(&mut self, off: i64) {
        if off == NONE {
            return;
        }
        self.lab(off, 0x10, "SoundSeq");
        self.bytes_(off + 4, 2);
        self.u16(off + 6);
        let keys = self.target(off + 0xC);
        let n = self.word(off + 8);
        if keys == NONE {
            return;
        }
        let n = n.min(0x1000);
        self.lab(keys, n * 0x10, "SoundSeq.keys");
        for i in 0..n {
            let k = keys + i * 0x10;
            let lst = self.target(k + 0xC);
            let ns = self.word(k + 8).min(0x100);
            self.bytes_(k + 4, 4);
            if lst != NONE {
                self.lab(lst, ns * 4, "SoundSeq.soundptrs");
                for j in 0..ns {
                    let t = self.target(lst + j * 4);
                    self.sound(t);
                }
            }
        }
    }

    fn camera_list(&mut self, off: i64) -> Result<()> {
        let lst = self.target(off + 4);
        let n = self.word(off).min(0x40);
        if lst == NONE {
            return Ok(());
        }
        self.lab(lst, n * 0xC, "CameraSubMode[]");
        for i in 0..n {
            let e = lst + i * 0xC;
            self.bytes_(e, 4);
            let bp = self.target(e + 4);
            if bp != NONE {
                let b = self.blob_at(e)?;
                self.lab(bp, if b == 4 { 0x18 } else { 0x10 }, &format!("CameraParams.t{b}"));
            }
            if self.blob_at(e)? == 4 && bp != NONE {
                self.u16(bp + 0x14);
                self.bytes_(bp + 0x16, 2);
            }
        }
        Ok(())
    }

    fn thing_record(&mut self, rec: i64) -> Result<()> {
        if rec == NONE || self.claim_at(rec + 4)? == BYTES {
            return Ok(());
        }
        self.lab(rec, 0x20, "ThingRecord");
        if self.blob_at(rec + 4)? == 1 {
            let t = self.target(rec);
            self.motion_anim(t)?;
        } else {
            let t = self.target(rec);
            self.geom_roots.insert(t);
        }
        self.bytes_(rec + 4, 4);
        let info = self.target(rec + 0x10);
        if info != NONE {
            self.lab(info, 0x14, "DestructibleInfo");
            self.bytes_(info + 0x10, 2);
        }
        self.slave_list(rec + 0x18)
    }

    fn ai_state(&mut self, st: i64, depth: i32) {
        if st == NONE || depth > 32 {
            return;
        }
        let info = self.target(st);
        if info != NONE {
            self.lab(info, 0x60, "AIStateInfo");
            self.bytes_(info, 8);
            let lim = self.target(info + 0x58);
            if lim != NONE {
                self.lab(lim, 0x14, "AILimits");
                self.bytes_(lim + 0x10, 4);
            }
        }
        let ch = self.target(st + 8);
        let n = self.word(st + 4).min(0x100);
        if ch != NONE {
            self.lab(ch, n * 0xC, "AIState[]");
            for i in 0..n {
                self.ai_state(ch + i * 0xC, depth + 1);
            }
        }
    }

    fn ai_script(&mut self, sc: i64) {
        if sc == NONE {
            return;
        }
        self.lab(sc, 0x34, "AICarScript");
        self.bytes_(sc + 0x24, 4);
        self.ai_state(sc + 0x28, 0);
    }

    fn line_def(&mut self, d: i64) -> Result<()> {
        if d == NONE || self.claim_at(d)? == BYTES {
            return Ok(());
        }
        self.lab(d, 0x1C, "LineDef");
        self.bytes_(d, 4);
        self.track(d + 0xC, Keys::F32);
        self.track(d + 0x14, Keys::Color);
        Ok(())
    }

    fn emitter(&mut self, off: i64) -> Result<()> {
        if off == NONE || self.claim_at(off)? == BYTES {
            return Ok(());
        }
        self.lab(off, 0x24, "EmitterData");
        self.bytes_(off, 4);
        let t = self.target(off + 0x20);
        self.sound_seq(t);
        Ok(())
    }

    fn event_seq(&mut self, script: i64) -> Result<()> {
        if script == NONE || self.es_depth > 4 {
            return Ok(());
        }
        self.es_depth += 1;
        self.lab(script, 8, "EventSeq");
        let entries = self.target(script + 4);
        let n = self.word(script);
        if entries != NONE {
            self.lab(entries, n.min(0x400) * 0xC, "EventSeq.entries");
        }
        let cnt = if entries != NONE { n.min(0x400) } else { 0 };
        for i in 0..cnt {
            let e = entries + i * 0xC;
            let a = self.target(e + 4);
            let b = self.target(e + 8);
            let ty = self.word(e);
            if [1, 2, 3, 4, 7, 29].contains(&ty) {
                if b != NONE {
                    self.lab(b, 0x30, "ParticleEmitter");
                }
                if b != NONE && self.claim_at(b + 0x20)? != BYTES {
                    let adv = self.target(b + 0x28);
                    let info = self.target(b + 0x2C);
                    self.bytes_(b + 0x20, 4);
                    if self.blob_at(b + 0x21)? == 2 && adv != NONE {
                        self.lab(adv, 0x10, "PEmitter.adv");
                        let d = self.target(adv + 0xC);
                        self.bytes_(adv + 8, 4);
                        if d != NONE && [1, 2].contains(&self.blob_at(adv + 9)?) {
                            self.lab(d, 0x14, "PEmitter.adv.data");
                            self.bytes_(d + 0x10, 4);
                        }
                    }
                    if info != NONE && self.blob_at(b + 0x22)? == 4 {
                        self.lab(info, 0x14, "PEmitter.info");
                        self.bytes_(info + 0x10, 4);
                    }
                }
                if a != NONE {
                    match ty {
                        3 => self.event_seq(a)?,
                        4 => self.emitter(a)?,
                        7 => self.thing_record(a)?,
                        _ => {}
                    }
                }
                if ty == 2 && a != NONE && self.claim_at(a)? != BYTES {
                    self.lab(a, 0xAC, "Projectile");
                    let g = self.target(a + 0x10);
                    self.geom_roots.insert(g);
                    let tgt = self.target(a + 0xA8);
                    self.bytes_(a, 0xC);
                    self.track(a + 0x8C, Keys::Vec4);
                    self.track(a + 0x94, Keys::Color);
                    self.slave_list(a + 0x9C)?;
                    if tgt != NONE {
                        self.u16(tgt);
                        self.u16(tgt + 2);
                    }
                }
                if ty == 1 && a != NONE {
                    self.lab(a, 0x24, "Particle");
                    let anim = self.target(a + 4);
                    let kind = self.blob_at(a)?;
                    self.bytes_(a, 4);
                    if kind <= 2 {
                        self.track(a + 0x14, Keys::Color);
                    }
                    if kind == 2 {
                        let fx = self.target(a + 0x20);
                        self.line_def(anim)?;
                        if fx != NONE {
                            for k in 0..self.word(a + 0x1C).min(0x40) {
                                let t = self.target(fx + k * 4);
                                self.line_def(t)?;
                            }
                        }
                    } else if anim != NONE {
                        self.lab(anim, 0x30, "Particle.anim");
                        self.bytes_(anim, 4);
                        if kind <= 1 {
                            self.track(anim + 0x28, if kind == 0 { Keys::Vec4 } else { Keys::F32 });
                        }
                    }
                }
            } else if [5, 6, 8, 10, 25, 26].contains(&ty) {
                if b != NONE {
                    self.lab(b, 0x20, "PosEmitter");
                    self.bytes_(b + 0x18, 4);
                }
                if a != NONE {
                    match ty {
                        5 => {
                            self.lab(a, 0x34, "Blast");
                            self.bytes_(a, 8);
                            let dm = self.target(a + 0x1C);
                            if dm != NONE {
                                self.lab(dm, 4, "Blast.damage");
                                self.bytes_(dm, 4);
                            }
                            let c = self.target(a + 0x20);
                            if c != NONE {
                                self.lab(c, 0xC, "FlashDef");
                                self.bytes_(c, 4);
                                self.track(c + 4, Keys::Color);
                            }
                            self.track(a + 0x2C, Keys::Point);
                        }
                        6 => self.thing_record(a)?,
                        10 => {
                            self.lab(a, 4, "EventSeq.t10");
                            self.bytes_(a, 4);
                        }
                        26 => self.sound_seq(a),
                        _ => {}
                    }
                }
            }
        }
        self.es_depth -= 1;
        Ok(())
    }

    fn is_slot_or_null(&self, off: i64) -> bool {
        self.inb(off, 4) && (self.claim[off as usize] == PTR || self.word(off) == 0)
    }

    fn is_event_seq(&self, script: i64) -> bool {
        if script == NONE || !self.inb(script, 8) {
            return false;
        }
        let n = self.word(script);
        let entries = self.target(script + 4);
        if n == 0 || n > 0x100 || entries == NONE || !self.inb(entries, n * 0xC) {
            return false;
        }
        let mut em = 0;
        for i in 0..n {
            let e = entries + i * 0xC;
            let ty = self.word(e);
            if ty > 40 {
                return false;
            }
            if [1, 2, 3, 4, 5, 6, 7, 8, 10, 25, 26, 29].contains(&ty) {
                if !self.is_slot_or_null(e + 4) || self.target(e + 8) == NONE {
                    return false;
                }
                em += 1;
            }
        }
        em != 0
    }

    fn slave_data(&mut self, ty: i64, data: i64) -> Result<()> {
        if data == NONE {
            return Ok(());
        }
        match ty {
            1 => self.event_seq(data)?,
            2 => self.emitter(data)?,
            9 => {
                self.lab(data, 0x24, "RadarBlip");
                self.bytes_(data, 4);
                for o in [0x1C, 0x20] {
                    let t = self.target(data + o);
                    if t != NONE {
                        self.lab(t, 4, "GXColor");
                        self.bytes_(t, 4);
                    }
                }
            }
            3 => self.sound_seq(data),
            5 | 6 => {
                self.lab(data, 0x24, "AttachData");
                self.bytes_(data, 4);
                let g = self.target(data + 8);
                self.geom_roots.insert(g);
                self.track(data + 0xC, Keys::Vec4);
                self.track(data + 0x14, Keys::Vec4);
                self.track(data + 0x1C, Keys::Color);
            }
            7 => {
                self.lab(data, 0x54, "Tracer");
                self.bytes_(data + 0x20, 4);
                self.track(data + 0x44, Keys::Color);
                self.track(data + 0x4C, Keys::Color);
            }
            8 => self.motion_anim(data)?,
            10 => {
                self.lab(data, 0xC, "SharedSlaves");
                self.slave_list(data + 4)?;
            }
            _ => self.lab(data, 4, &format!("SlaveData.t{ty}")),
        }
        Ok(())
    }

    fn slave_list(&mut self, off: i64) -> Result<()> {
        let t = self.target(off + 4);
        let n = self.word(off);
        if self.sl_depth > 8 || t == NONE {
            return Ok(());
        }
        self.sl_depth += 1;
        let n = n.min(0x400);
        self.lab(t, n * 0x20, "SlaveTrigger[]");
        for i in 0..n {
            let e = t + i * 0x20;
            if self.claim_at(e + 0x10)? == BYTES {
                continue;
            }
            self.bytes_(e + 0x10, 2);
            self.u16(e + 0x12);
            self.bytes_(e + 0x1C, 4);
            let ty = self.blob_at(e + 0x10)?;
            let d = self.target(e + 0x18);
            self.slave_data(ty, d)?;
        }
        self.sl_depth -= 1;
        Ok(())
    }

    fn key_anim(&mut self, off: i64) {
        if off == NONE {
            return;
        }
        self.lab(off, 0x2C, "KeyAnim");
        let flags = self.target(off + 0x1C);
        let n = self.word(off + 0x10).min(0x100);
        if flags != NONE {
            self.lab(flags, (n * 2 + 3) & !3, "KeyAnim.chanflags(u16)");
            for i in 0..n {
                self.u16(flags + i * 2);
            }
        }
        let fr = self.word(off + 0x28);
        let t = self.target(off + 0x20);
        if t != NONE && fr < 0x10000 {
            self.lab(t, fr * 4, "KeyAnim.times");
        }
        let d = self.target(off + 0x24);
        if d != NONE {
            self.lab(d, 4, "KeyAnim.data");
        }
    }

    fn motion_anim(&mut self, off: i64) -> Result<()> {
        if off == NONE || self.claim_at(off + 0x1C)? == BYTES {
            return Ok(());
        }
        self.lab(off, 0x3C, "MotionAnim");
        self.bytes_(off + 0x1C, 4);
        let k = self.target(off + 0x10);
        self.key_anim(k);
        self.slave_list(off + 0x2C)?;
        for k in 0..3 {
            let r = self.target(off + 0x20 + 4 * k);
            if r != NONE {
                self.lab(r, 8, "MotionRange");
            }
        }
        let p = self.target(off + 0xC);
        let n = self.word(off + 8).min(0x400);
        if p != NONE {
            self.lab(p, n * 8, "MotionPart[]");
            for i in 0..n {
                self.u16(p + i * 8 + 4);
                self.u16(p + i * 8 + 6);
                let g = self.target(p + i * 8);
                self.geom_roots.insert(g);
            }
        }
        let g = self.target(off + 4);
        let ng = self.word(off);
        if g != NONE && ng < 0x100 {
            self.lab(g, ng * 4, "MotionAnim.skinned[]");
            for i in 0..ng {
                let t = self.target(g + 4 * i);
                self.geom_roots.insert(t);
            }
        }
        let p = self.target(off + 0x14);
        if p != NONE {
            self.lab(p, 0x14, "Skeleton");
            let bones = self.target(p + 0x10);
            let n = self.word(p).min(0x100);
            if bones != NONE {
                self.lab(bones, n * 0x20, "AnimNode[]");
                for i in 0..n {
                    self.bytes_(bones + i * 0x20 + 0x1C, 4);
                }
            }
        }
        Ok(())
    }

    fn is_skeleton(&self, sk: i64) -> bool {
        if sk == NONE || !self.inb(sk, 0x14) {
            return false;
        }
        let n = self.word(sk);
        let bones = self.target(sk + 0x10);
        if n == 0 || n > 0x100 || bones == NONE || !self.inb(bones, n * 0x20) {
            return false;
        }
        let mut ch = 0i64;
        for i in 0..n {
            let b = bones + i * 0x20;
            let mut l = 0.0f64;
            for k in 0..4 {
                let f = self.f32(b + 4 * k);
                l += f * f;
            }
            if !(0.9 < l && l < 1.1) {
                return false;
            }
            ch += self.blob[(b + 0x1C) as usize] as i64;
        }
        ch == n - 1
    }

    fn is_motion_anim(&self, m: i64) -> bool {
        if m == NONE || !self.inb(m, 0x34) || self.word(m) > 0x100 || self.word(m + 8) > 0x400 {
            return false;
        }
        let fps = self.f32(m + 0x18);
        (1.0..=1000.0).contains(&fps) && self.is_skeleton(self.target(m + 0x14))
    }

    fn is_key_anim(&self, a: i64) -> bool {
        if a == NONE || !self.inb(a, 0x34) {
            return false;
        }
        let bones = self.word(a + 0x10);
        let frames = self.word(a + 0x28);
        let t0 = self.f32(a + 8);
        let t1 = self.f32(a + 0xC);
        bones != 0
            && bones <= 0x100
            && frames != 0
            && frames <= 0x10000
            && t0 > -1e7
            && t1 < 1e7
            && t0 <= t1
            && self.target(a + 0x1C) != NONE
            && self.target(a + 0x20) != NONE
            && self.target(a + 0x24) != NONE
            && self.inb(self.target(a + 0x1C), bones * 2)
    }

    fn car_a_info(&mut self, off: i64) -> Result<()> {
        self.lab(off, 0x120, "CarAInfo");
        for i in 0..4 {
            let t = self.target(off + 0x90 + 4 * i);
            self.ai_script(t);
        }
        self.bytes_(off + 0x10, 4);
        self.u16(off + 0x14);
        self.u16(off + 0x16);
        let p = self.target(off + 0x6C);
        if p != NONE {
            self.lab(p, 4, "GXColor(skid)");
            self.bytes_(p, 4);
        }
        let p = self.target(off + 0x78);
        if p != NONE {
            self.lab(p, 0x20, "CarAModelRef");
            let m = self.target(p + 0x10);
            self.motion_anim(m)?;
            if m != NONE {
                let lst = self.target(m + 0x38);
                if lst != NONE {
                    self.lab(lst, 8, "CarPartList");
                    let parts = self.target(lst + 4);
                    if parts != NONE {
                        let n = self.word(lst).min(0x100);
                        self.lab(parts, n * 0x1C, "CarPart[]");
                        for i in 0..n {
                            self.bytes_(parts + i * 0x1C, 4);
                            self.u16(parts + i * 0x1C + 4);
                            self.u16(parts + i * 0x1C + 6);
                            let g = self.target(parts + i * 0x1C + 8);
                            self.geom_roots.insert(g);
                        }
                    }
                }
            }
            for k in [0x14, 0x18, 0x1C] {
                let t = self.target(p + k);
                self.key_anim(t);
            }
        }
        let t = self.target(off + 0x48);
        self.key_anim(t);
        let g = self.target(off + 0x54);
        self.geom_roots.insert(g);
        let p = self.target(off + 0x80);
        if p != NONE {
            self.lab(p, 0x90, "CarAEngine");
            self.camera_list(p + 0x80)?;
            self.camera_list(p + 0x88)?;
        }
        let p = self.target(off + 0x84);
        if p != NONE {
            self.lab(p, 0x38, "CarAHud");
            self.bytes_(p + 0x34, 4);
            for k in [0x10, 0x18, 0x1C] {
                let t = self.target(p + k);
                self.im_header(t)?;
            }
        }
        for (k, n, nm) in [(0x7C, 4, "f32(grip)"), (0x88, 0x3C, "CarAAccel"), (0x8C, 0x58, "CarAStuntInfo")] {
            let t = self.target(off + k);
            if t != NONE {
                self.lab(t, n, nm);
            }
        }
        let st = self.target(off + 0x8C);
        if st != NONE {
            for (cnt, ptr, nm) in [(0x34, 0x38, "u32[](rewardThings)"), (0x4C, 0x50, "u32[](failThings)")] {
                let t = self.target(st + ptr);
                if t != NONE {
                    self.lab(t, self.word(st + cnt) * 4, nm);
                }
            }
            let t = self.target(st + 0x40);
            if t != NONE {
                let n = self.word(st + 0x3C);
                self.lab(t, n * 4, "StuntMsgTables");
                for i in 0..n.min(0x40) {
                    let t2 = self.target(t + 4 * i);
                    if t2 != NONE {
                        self.lab(t2, 4, "StuntMsgTable");
                    }
                }
            }
            let t = self.target(st + 0x48);
            if t != NONE {
                self.lab(t, 4, "StuntFailMsg");
            }
        }
        let p = self.target(off + 0xA0);
        if p != NONE {
            self.lab(p, 0x4C, "CarACollider");
            self.bytes_(p + 0x10, 4);
            for (k, n, nm) in [(0x30, 0x1C, "CarAMaterial"), (0x44, 0x20, "Collider.box/spheres"), (0x48, 0x20, "CarAHull")] {
                let t = self.target(p + k);
                if t != NONE {
                    self.lab(t, n, nm);
                }
            }
            let t = self.target(p + 0x38);
            if t != NONE {
                self.lab(t, self.word(p + 0x34) * 4, "u32[](endFx)");
            }
            let h = self.target(p + 0x48);
            if h != NONE {
                let v = self.target(h + 0x1C);
                if v != NONE {
                    self.lab(v, self.word(h + 0xC) * 0x10, "Vector4[](hull)");
                }
            }
        }
        let p = self.target(off + 0xA4);
        if p != NONE {
            self.lab(p, 0xC, "CarAForcePoints");
            let pts = self.target(p + 8);
            let n = self.word(p + 4).min(0x100);
            if pts != NONE {
                self.lab(pts, n * 0x30, "CarAForcePoint[]");
                for i in 0..n {
                    self.bytes_(pts + i * 0x30 + 0x10, 4);
                }
            }
        }
        let p = self.target(off + 0xE4);
        if p != NONE {
            let n = self.word(off + 0xE0);
            self.lab(p, n.max(11) * 0xC, "CarASlaveSet[](slaves)");
            let cnt = if n < 0x40 { n.max(11) } else { 11 };
            for i in 0..cnt {
                self.slave_list(p + i * 0xC + 4)?;
            }
        }
        for (cnt, ptr, nm) in [(0xE8, 0xEC, "CarASlaveSet[](weapons)"), (0xF0, 0xF4, "CarASlaveSet[](pickup)")] {
            let p = self.target(off + ptr);
            let n = self.word(off + cnt).min(0x40);
            if p != NONE {
                self.lab(p, n * 0xC, nm);
                for i in 0..n {
                    self.slave_list(p + i * 12 + 4)?;
                }
            }
        }
        let t = self.target(off + 0x60);
        self.sound(t);
        let p = self.target(off + 0xDC);
        if p != NONE {
            self.lab(p, self.word(off + 0xD8) * 8, "CarAGear[]");
            let t = self.target(p + 0xC);
            self.sound(t);
        }
        let p = self.target(off + 0x11C);
        let n = self.word(off + 0x118).min(0x40);
        if p != NONE {
            self.lab(p, n * 4, "CarASoundSet*[]");
            for i in 0..n {
                let ss = self.target(p + 4 * i);
                self.sound(ss);
                if ss != NONE {
                    let r = self.target(ss + 0x18);
                    if r != NONE {
                        self.lab(r, 8, "f32[2](range)");
                    }
                }
            }
        }
        let p = self.target(off + 0x104);
        let n = self.word(off + 0x100).min(0x40);
        if p != NONE {
            self.lab(p, n * 0x50, "CarAStartSlave[]");
            for i in 0..n {
                self.u16(p + i * 0x50 + 0x30);
                self.u16(p + i * 0x50 + 0x32);
            }
        }
        let p = self.target(off + 0x10C);
        let n = self.word(off + 0x108).min(0x40);
        if p != NONE {
            self.lab(p, n * 4, "ItemRecord*[](startItems)");
        }
        let p = self.target(off + 0x114);
        let n = self.word(off + 0x110).min(0x40);
        if p != NONE {
            self.lab(p, n * 4, "u32[](shiftSounds)");
        }
        let p = self.target(off + 0xD4);
        let n = self.word(off + 0xD0).min(0x20);
        if p != NONE {
            self.lab(p, n * 0x50, "CarTrailBone[]");
            for i in 0..n {
                let w = p + i * 0x50;
                self.bytes_(w + 0x3C, 4);
                self.u16(w + 0x40);
                self.bytes_(w + 0x42, 2);
            }
        }
        Ok(())
    }

    // --- walk_im.py -----------------------------------------------------------------------
    fn obj_anim(&mut self, off: i64) {
        if off == NONE {
            return;
        }
        self.lab(off, 0x28, "ObjAnim");
        self.track(off, Keys::Vec4);
        self.track(off + 8, Keys::Vec4);
        self.track(off + 0x10, Keys::Vec4);
        self.track(off + 0x18, Keys::Color);
        self.track(off + 0x20, Keys::Uv);
    }

    fn state_anim(&mut self, off: i64) -> Result<()> {
        if off == NONE {
            return Ok(());
        }
        self.lab(off, 0x3C, "StateAnim");
        self.track(off, Keys::Active);
        self.track(off + 8, Keys::Vec4);
        self.track(off + 0x10, Keys::Color);
        self.track(off + 0x18, Keys::Vec4);
        self.track(off + 0x20, Keys::Uv);
        self.track(off + 0x28, Keys::Vec4);
        let t = self.target(off + 0x30);
        self.sound_seq(t);
        self.action_list(off + 0x34)
    }

    fn var_value(&mut self, off: i64) {
        if off != NONE {
            self.lab(off, 4, "VarValue");
            self.bytes_(off, 2);
            self.u16(off + 2);
        }
    }

    fn action_list(&mut self, off: i64) -> Result<()> {
        if off == NONE {
            return Ok(());
        }
        let lst = self.target(off + 4);
        let n = self.word(off).min(0x400);
        if lst == NONE {
            return Ok(());
        }
        self.lab(lst, n * 8, "Action[]");
        for i in 0..n {
            let a = lst + i * 8;
            let data = self.target(a + 4);
            let ty = self.blob_at(a)?;
            self.bytes_(a, 4);
            if data == NONE {
                continue;
            }
            if ty == 1 {
                self.lab(data, 8, "ActionData1");
                self.bytes_(data, 2);
                self.u16(data + 2);
            } else if ty == 2 {
                self.lab(data, 8, "ActionData2");
                let cv = self.target(data);
                if cv != NONE {
                    self.lab(cv, 8, "CodeVar");
                    let t = self.target(cv + 4);
                    self.var_value(t);
                }
                let t = self.target(data + 4);
                self.var_value(t);
            } else if ty == 3 {
                self.var_value(data);
            }
        }
        Ok(())
    }

    fn event_def(&mut self, off: i64) -> Result<()> {
        if off == NONE || self.claim_at(off + 4)? == BYTES {
            return Ok(());
        }
        self.lab(off, 0x24, "EventDef");
        let ty = self.blob_at(off + 5)?;
        if ty == 1 || ty == 0x20 {
            let t = self.target(off);
            self.im_header(t)?;
        } else if ty == 0xB {
            let t = self.target(off);
            self.action_list(t)?;
        }
        self.bytes_(off + 4, 2);
        self.u16(off + 6);
        let t = self.target(off + 0x14);
        self.sound_seq(t);
        let args = self.target(off + 8);
        if args != NONE {
            if ty == 0x11 {
                self.var_value(args);
            } else {
                self.lab(args, 8, "EventArgs");
                let a = self.target(args);
                self.var_value(a);
                let b = self.target(args + 4);
                self.var_value(b);
            }
        }
        let b = self.target(off + 0x18);
        if b != NONE {
            self.lab(b, 4, "EventBytes");
            self.bytes_(b, 4);
        }
        let mods = self.target(off + 0x20);
        let n = self.word(off + 0x1C).min(0x100);
        if mods != NONE {
            self.lab(mods, n * 8, "EventMod[]");
            for i in 0..n {
                self.bytes_(mods + i * 8, 4);
            }
        }
        Ok(())
    }

    fn event_list(&mut self, cnt: i64, lo: i64) -> Result<()> {
        let lst = self.target(lo);
        let n = self.word(cnt).min(0x1000);
        if lst == NONE {
            return Ok(());
        }
        self.lab(lst, n * 0xC, "InputEvent[]");
        for i in 0..n {
            self.bytes_(lst + i * 0xC, 4);
            let t = self.target(lst + i * 0xC + 8);
            self.event_def(t)?;
        }
        Ok(())
    }

    fn im_object_data(&mut self, off: i64) -> Result<()> {
        if off == NONE {
            return Ok(());
        }
        self.lab(off, 0x18, "IMObjectData");
        self.bytes_(off, 8);
        let st = self.target(off + 0xC);
        let n = self.word(off + 8).min(0x1000);
        if st != NONE {
            self.lab(st, n * 0x28, "IMStep[]");
            for i in 0..n {
                self.bytes_(st + i * 0x28 + 0x14, 4);
            }
        }
        self.event_list(off + 0x10, off + 0x14)
    }

    fn font(&mut self, off: i64) {
        if off == NONE {
            return;
        }
        self.lab(off, 0x2C, "Font");
        self.bytes_(off + 8, 4);
        let sp = self.target(off + 0x24);
        let n = self.word(off + 0x20).min(0x200);
        if sp != NONE {
            self.lab(sp, n * 0xC, "FontSpacing[]");
            for i in 0..n {
                self.bytes_(sp + i * 0xC, 4);
            }
        }
    }

    fn cstring(&mut self, off: i64) {
        if off == NONE {
            return;
        }
        let mut n = 0;
        while off + n < self.size && self.blob[(off + n) as usize] != 0 {
            n += 1;
        }
        self.cl(off, n + 1, STR);
        self.lab(off, n + 1, "string");
    }

    fn im_object(&mut self, off: i64) -> Result<()> {
        self.bytes_(off, 10);
        self.u16(off + 0xA);
        self.u16(off + 0xC);
        self.u16(off + 0xE);
        let sub = self.target(off + 0x14);
        let ty = self.blob_at(off + 1)?;
        if sub != NONE {
            if ty == 0 {
                self.lab(sub, 0x14, "ObjSub");
                if self.blob_at(sub)? == 3 {
                    let an = self.target(sub + 0x10);
                    if an != NONE {
                        self.lab(an, 0x10, "TexKeyAnim");
                        self.bytes_(an + 0xC, 2);
                        self.u16(an + 0xE);
                    }
                }
                self.bytes_(sub, 4);
            } else if ty == 2 {
                self.lab(sub, 8, "DialogList");
            } else if ty == 1 || ty == 10 || ty == 12 {
                self.lab(sub, 0x18, "TextInfo");
                self.bytes_(sub + 0xC, 8);
                self.u16(sub + 0x14);
                self.u16(sub + 0x16);
                let st = self.target(sub);
                if st != NONE {
                    self.lab(st, 4, "StringSlots");
                    let t = self.target(st);
                    self.cstring(t);
                }
                if self.blob_at(sub + 0x10)? == 1 {
                    let lst = self.target(sub + 8);
                    if lst != NONE {
                        self.lab(lst, 0xC, "StringList");
                        let texts = self.target(lst + 4);
                        self.bytes_(lst + 8, 2);
                        self.u16(lst + 0xA);
                        let cnt = if texts != NONE { self.word(lst).min(0x400) } else { 0 };
                        for k in 0..cnt {
                            let t = self.target(texts + 4 * k);
                            if t != NONE {
                                let s = self.target(t);
                                self.cstring(s);
                            }
                        }
                    }
                }
                let f = self.target(sub + if ty == 10 { 0x10 } else { 4 });
                self.font(f);
            }
        }
        let t = self.target(off + 0x60);
        self.obj_anim(t);
        let t = self.target(off + 0x44);
        self.im_object_data(t)?;
        if ty == 6 || ty == 7 {
            let h = self.target(off + 0x3C);
            if h != NONE {
                let t = self.target(h);
                self.im_header(t)?;
            }
        }
        let cl_ = self.target(off + 0x48);
        if cl_ != NONE {
            self.lab(cl_, 0xC, "CondList");
            let c = self.target(cl_ + 8);
            let n = self.word(cl_ + 4).min(0x100);
            self.bytes_(cl_, 4);
            if c != NONE {
                self.lab(c, n * 0x1C, "Cond[]");
                for i in 0..n {
                    let cond = c + i * 0x1C;
                    let data = self.target(cond + 8);
                    if self.blob_at(cond)? == 1 {
                        self.var_value(data);
                    } else if self.blob_at(cond)? == 2 && data != NONE {
                        self.lab(data, 8, "CodeVar");
                        let t = self.target(data + 4);
                        self.var_value(t);
                    }
                    self.bytes_(cond, 4);
                }
            }
        }
        let st = self.target(off + 0x68);
        let n = self.word(off + 0x64).min(0x100);
        if st != NONE {
            self.lab(st, n * 0xC, "ObjState[]");
            for i in 0..n {
                self.bytes_(st + i * 0xC, 4);
                let t = self.target(st + i * 0xC + 8);
                self.state_anim(t)?;
            }
        }
        Ok(())
    }

    fn im_header(&mut self, off: i64) -> Result<()> {
        if off == NONE || self.claim_at(off)? == U16 {
            return Ok(());
        }
        self.lab(off, 0x40, "IMHeader");
        self.u16(off);
        self.u16(off + 2);
        self.bytes_(off + 0x3C, 4);
        let objs = self.target(off + 0x10);
        let n = self.word(off + 0xC).min(500);
        if objs != NONE {
            self.lab(objs, n * 0x6C, "IMObject[]");
            for i in 0..n {
                self.im_object(objs + i * 0x6C)?;
            }
        }
        self.event_list(off + 0x14, off + 0x18)?;
        let v = self.target(off + 0x1C);
        if v != NONE {
            self.lab(v, 8, "IMVars");
            let lst = self.target(v + 4);
            let n = self.word(v).min(0x1000);
            if lst != NONE {
                self.lab(lst, n * 0x34, "IMVariable[]");
                for i in 0..n {
                    let vv = lst + i * 0x34;
                    let cv = self.target(vv + 0x10);
                    self.bytes_(vv, 4);
                    if cv != NONE {
                        self.lab(cv, 8, "CodeVar");
                        let val = self.target(cv + 4);
                        if val != NONE {
                            self.lab(val, 4, "VarValue");
                            self.bytes_(val, 2);
                            self.u16(val + 2);
                        }
                    }
                }
            }
        }
        for i in (0x20..0x30).step_by(4) {
            let t = self.target(off + i);
            self.sound_seq(t);
        }
        Ok(())
    }

    // --- comp_survey.py -------------------------------------------------------------------
    fn tac_trigger(&mut self, t: i64, depth: i32) -> Result<()> {
        self.lab(t, 0x2C, "Trigger");
        self.bytes_(t + 0x28, 4);
        if self.word(t) == 7 {
            let p = self.target(t + 4);
            if p != NONE {
                let h = self.target(p);
                self.im_header(h)?;
            }
        }
        for (lo, no) in [(0x14, 0x1C), (0x18, 0x20)] {
            let lst = self.target(t + lo);
            let n = self.word(t + no).min(0x100);
            if lst == NONE {
                continue;
            }
            self.lab(lst, n * 0x18, "Action[](TAC)");
            for i in 0..n {
                let a = lst + i * 0x18;
                let data = self.target(a + 4);
                let ty = self.word(a);
                if data == NONE {
                    continue;
                }
                if ty == 4 {
                    let h = self.target(data);
                    self.im_header(h)?;
                } else if ty == 31 {
                    let h = self.target(data + 4);
                    self.im_header(h)?;
                }
            }
        }
        let conds = self.target(t + 0xC);
        let n = self.word(t + 0x10);
        if depth < 2 && conds != NONE {
            for i in 0..n.min(0x40) {
                self.tac_trigger(conds + i * 0x2C, depth + 1)?;
            }
        }
        Ok(())
    }

    fn shaders(&mut self, table: i64, count: i64) -> Result<()> {
        for i in 0..count.min(0x1000) {
            let d = self.target(table + i * 0xC);
            if d == NONE || self.claim_at(d)? == BYTES {
                continue;
            }
            self.lab(d, 0x18, "Shader");
            self.bytes_(d, 4);
            self.track(d + 4, Keys::Color);
            self.key_track(d + 0xC, 0x10, 0xC, "keys");
        }
        Ok(())
    }

    fn light_def(&mut self, d: i64) -> Result<()> {
        if d == NONE || self.claim_at(d)? == BYTES {
            return Ok(());
        }
        self.lab(d, 0x1C, "LightDef");
        self.bytes_(d, 4);
        self.track(d + 0xC, Keys::F32);
        self.track(d + 0x14, Keys::Color);
        Ok(())
    }

    fn item_info(&mut self, off: i64) -> Result<()> {
        self.lab(off, 0x3C, "ItemInfo");
        self.bytes_(off, 2);
        self.u16(off + 2);
        let sub = self.target(off + 0x30);
        if sub != NONE {
            self.lab(sub, 0x38, "WeaponInfo");
            let card = self.target(sub);
            if card != NONE && self.is_motion_anim(card) {
                self.motion_anim(card)?;
            }
            self.bytes_(sub + 0x2C, 2);
            self.u16(sub + 0x2E);
            self.bytes_(sub + 0x34, 4);
        }
        self.slave_list(off + 0x38)
    }

    fn world(&mut self) -> Result<()> {
        self.lab(0, 0xA0, "World");
        let lst = self.target(0xC);
        let n = self.word(0x8);
        for i in 0..(if lst != NONE { n.min(0x100) } else { 0 }) {
            let e = lst + i * 0x60;
            self.lab(e, 0x60, "TileCar");
            self.bytes_(e + 0x34, 4);
            self.bytes_(e + 0x58, 8);
            for k in 0..4 {
                let t = self.target(e + 0x40 + k * 4);
                self.ai_script(t);
            }
        }
        let lst = self.target(0x24);
        let n = self.word(0x20);
        for i in 0..(if lst != NONE { n.min(0x1000) } else { 0 }) {
            let q = lst + i * 0x50;
            let il = self.target(q + 0x30);
            if il == NONE {
                continue;
            }
            self.lab(il, 0x1C, "QuadContents");
            let statics = self.target(il + 8);
            let dyn_ = self.target(il + 0x10);
            for j in 0..(if statics != NONE { self.word(il + 4).min(0x1000) } else { 0 }) {
                let t = self.target(statics + j * 0x40 + 0x24);
                self.thing_record(t)?;
            }
            for j in 0..(if dyn_ != NONE { self.word(il + 0xC).min(0x1000) } else { 0 }) {
                let t = self.target(dyn_ + j * 0x40 + 0x24);
                self.emitter(t)?;
            }
        }
        let lst = self.target(0x14);
        let n = self.word(0x10);
        for i in 0..(if lst != NONE { n.min(0x400) } else { 0 }) {
            self.lab(lst + i * 0x20, 0x20, "TilePath");
            let t = self.target(lst + i * 0x20 + 0xC);
            self.thing_record(t)?;
        }
        let lst = self.target(0x3C);
        let n = self.word(0x38);
        for i in 0..(if lst != NONE { n.min(0x400) } else { 0 }) {
            let t = self.target(lst + i * 0x20 + 0x10);
            self.light_def(t)?;
        }
        let lst = self.target(0x44);
        let n = self.word(0x40);
        for i in 0..(if lst != NONE { n.min(0x400) } else { 0 }) {
            let c = self.target(lst + i * 0x40 + 0x18);
            if c != NONE {
                self.lab(c, 0x74, "CreatureInfo");
            }
        }
        let gd = self.target(0x9C);
        if gd != NONE {
            self.lab(gd, 0x110, "WorldGameData");
            self.bytes_(gd, 4);
            let h = self.target(gd + 0xCC);
            self.im_header(h)?;
            self.camera_list(gd + 0x100)?;
            self.camera_list(gd + 0x108)?;
        }
        Ok(())
    }

    /// h.get(key) as Python truthiness and h[key] as an int.
    fn hdr(&self, key: &str) -> Result<Option<i64>> {
        match self.header.get(key) {
            None | Some(Value::Null) => Ok(None),
            Some(Value::Bool(b)) => Ok(Some(*b as i64)),
            Some(v) => v.as_i64().map(Some).ok_or_else(|| Error::uncaught(format!("TypeError: header {key}: {v}"))),
        }
    }

    fn hdr_req(&self, key: &str) -> Result<i64> {
        self.hdr(key)?.ok_or_else(|| Error::uncaught(format!("KeyError: '{key}'")))
    }

    fn label_pack(&mut self, ptype: &str) -> Result<()> {
        let ptrs = self.pointers.clone();
        for &po in &ptrs {
            let m = self.target(po as i64);
            if self.is_motion_anim(m) {
                self.motion_anim(m)?;
            }
        }
        for &po in &ptrs {
            let t = self.target(po as i64);
            if self.is_event_seq(t) {
                self.event_seq(t)?;
            }
        }
        for &po in &ptrs {
            let a = self.target(po as i64);
            if self.is_key_anim(a) {
                self.key_anim(a);
            }
        }
        if ptype == "world" {
            self.world()?;
        } else if ["car", "item", "interface", "audio"].contains(&ptype) {
            let tab = self.target(4);
            let n = self.word(0);
            self.lab(0, 8, "RootHeader");
            if tab != NONE {
                self.lab(tab, n * 8, "RootTable");
                for i in 0..n.min(0x100) {
                    let o = self.target(tab + 8 * i);
                    if o == NONE {
                        continue;
                    }
                    match ptype {
                        "car" => self.car_a_info(o)?,
                        "item" => self.item_info(o)?,
                        "interface" => self.im_header(o)?,
                        _ => self.sound_seq(o),
                    }
                }
            }
        }
        if ptype == "world" {
            let lst = self.target(0xC);
            let n = self.word(8);
            for i in 0..(if lst != NONE { n.min(0x100) } else { 0 }) {
                let ci = self.target(lst + i * 0x60 + 0x24);
                if ci != NONE && self.inb(ci, 0x120) && !self.objs.contains_key(&(ci as u32)) {
                    self.car_a_info(ci)?;
                }
            }
        }
        if let Some(nt) = self.hdr("num_triggers")?.filter(|&v| v != 0) {
            let base = self.hdr_req("trigger_offset")?;
            for i in 0..nt {
                let t = base + i * 0x2C;
                if self.inb(t, 0x2C) {
                    self.tac_trigger(t, 0)?;
                }
            }
        }
        if let Some(ns) = self.hdr("num_shaders")?.filter(|&v| v != 0) {
            let base = self.hdr_req("shader_offset")?;
            self.shaders(base, ns)?;
        }
        let refs = self.refs.clone();
        for (o, v) in refs {
            if v >> 16 == 0 {
                let t = self.target(o as i64);
                self.geom_roots.insert(t);
            }
        }
        self.geom_roots.remove(&NONE);
        let roots: Vec<i64> = self.geom_roots.iter().copied().collect();
        for g in roots {
            if !self.objs.contains_key(&(g as u32)) {
                self.lab(g, 4, "Geometry");
            } else {
                self.objs.insert(g as u32, "Geometry".into());
            }
        }
        Ok(())
    }
}

/// comp_survey.label_pack: the walkers' object labels of one pack (`dir`: an `hwvx-x extract`
/// pack folder; `big`: GameCube byte order; `ptype`: the manifest entry's type), offset -> label.
pub fn label_pack(dir: &Path, big: bool, ptype: &str) -> Result<BTreeMap<u32, String>> {
    let mut w = W::open(dir, big)?;
    let ptype = ptype.to_string();
    // the walkers recurse (interface headers inside interface objects ...): a big stack
    std::thread::scope(|sc| {
        std::thread::Builder::new()
            .stack_size(256 << 20)
            .spawn_scoped(sc, move || w.label_pack(&ptype).map(|_| w.objs))
            .map_err(|e| Error::uncaught(format!("thread: {e}")))?
            .join()
            .map_err(|_| Error::uncaught("the walkers panicked"))?
    })
}
