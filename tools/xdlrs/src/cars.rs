//! `xdlrs cars stats`: the car select's stat bars and the physics behind them, per release and
//! car (docs-pending/car-stats-inventory.md, plan 0.4.1 part C).
//!
//! Reads a release's own archives, read only: `HW_Cars` (every car pack's CarAInfo and its parts:
//! CarAHud.stats, CarAEngine, CarAAccel, CarACollider, CarAStuntInfo, gears, wheels) and
//! `HW_IPack` (the car select, interface header id 2: which objects play roles 70-73, i.e.
//! `InterfaceHudInfo.carStats[0..3]`, the labels next to them, and the key tracks those values
//! drive: InterfaceManager.c sets the track time to the stat byte, ExtractKeys.c plays it).
//!
//! The retail releases are read with modcheck::media's archive reader; the PS2 prototype
//! (archive version 177/179) with its own layout (DataGamePackLoad.c: TOC offsets count from
//! the end of their own entry, no sector padding, pointer tables before the data).
//!
//! Field offsets and C types come from schemas/*.schema.json; the gears (CarAGear, 8 bytes) and
//! wheels (CarTrailBone, 0x50 bytes) have no schema and use src/game/include CarA.h / common.h.

use crate::decode::{Raw, read_prim};
use crate::modcheck::media::{Archive, RawPack};
use crate::schema::{self, Schemas};
use serde_json::{Map, Value, json};
use std::collections::BTreeSet;
use std::path::{Path, PathBuf};

pub type R<T> = std::result::Result<T, String>;

/// A pack's data section and its pointer slots.
pub struct Blob {
    pub d: Vec<u8>,
    pub ptrs: BTreeSet<u32>,
    pub big: bool,
}

impl Blob {
    pub fn u8(&self, o: u32) -> Option<u8> {
        self.d.get(o as usize).copied()
    }
    pub fn u16(&self, o: u32) -> Option<u16> {
        let o = o as usize;
        let b: [u8; 2] = self.d.get(o..o + 2)?.try_into().ok()?;
        Some(if self.big { u16::from_be_bytes(b) } else { u16::from_le_bytes(b) })
    }
    pub fn u32(&self, o: u32) -> Option<u32> {
        let o = o as usize;
        let b: [u8; 4] = self.d.get(o..o + 4)?.try_into().ok()?;
        Some(if self.big { u32::from_be_bytes(b) } else { u32::from_le_bytes(b) })
    }
    pub fn f32(&self, o: u32) -> Option<f32> {
        self.u32(o).map(f32::from_bits)
    }
    /// The pointer stored at o (None: no pointer slot there).
    pub fn ptr(&self, o: u32) -> Option<u32> {
        if self.ptrs.contains(&o) { self.u32(o) } else { None }
    }
    /// A NUL-ended string at o.
    pub fn cstr(&self, o: u32) -> Option<String> {
        let s = self.d.get(o as usize..)?;
        let n = s.iter().position(|&c| c == 0)?;
        Some(String::from_utf8_lossy(&s[..n]).into_owned())
    }
}

/// One pack of an archive: (type, id, blob).
pub struct PackData {
    pub typ: u32,
    pub id: u32,
    pub blob: Blob,
}

fn le(b: &[u8], o: usize) -> R<u32> {
    b.get(o..o + 4).map(|x| u32::from_le_bytes(x.try_into().unwrap())).ok_or_else(|| format!("short read at 0x{o:X}"))
}

/// The PS2 prototype's packs (DataGamePackLoad.c, proto): header (20 words of the standard
/// 120-byte header), sound pointers and sound data unpadded, the relocation tables (texture,
/// shader, sound), the ref relocations and pointers, then the data.
fn proto_packs(b: &[u8]) -> R<Vec<PackData>> {
    let count = le(b, 12)? as usize;
    let mut out = vec![];
    for i in 0..count {
        let e = 16 + 24 * i;
        let (typ, id, size, off) = (le(b, e + 4)?, le(b, e + 8)?, le(b, e + 16)? as usize, le(b, e + 20)? as usize);
        let start = off + 0x10 + (i + 1) * 24;
        let p = b.get(start..start + size).ok_or_else(|| format!("prototype pack {typ}_{id} past the archive"))?;
        if typ == 7 {
            continue; // geometry (quadrant) packs: not needed here
        }
        let w: Vec<usize> = (0..20).map(|k| le(p, 4 * k).map(|x| x as usize)).collect::<R<_>>()?;
        // STD_WORDS order (media.rs): data_size 0, sound_data_size 1, num_sound_pointers 2,
        // num_pointers 3, num_tex_relocs 4, ..., num_sound_relocs 7, num_shader_relocs 13,
        // num_ref_relocs 14
        let mut pos = 120 + if w[1] != 0 { w[2] * 4 + w[1] } else { 0 };
        pos += (w[4] + w[13] + w[7]) * 8;
        pos += w[14] * 8;
        let pt = p.get(pos..pos + w[3] * 4).ok_or("prototype pointers past the pack")?;
        pos += w[3] * 4;
        let d = p.get(pos..pos + w[0]).ok_or("prototype data past the pack")?.to_vec();
        if pos + w[0] != p.len() {
            return Err(format!("prototype pack {typ}_{id}: layout ends at 0x{:X}, the pack is 0x{:X}", pos + w[0], p.len()));
        }
        let ptrs = pt.chunks_exact(4).map(|c| u32::from_le_bytes(c.try_into().unwrap())).collect();
        out.push(PackData { typ, id, blob: Blob { d, ptrs, big: false } });
    }
    Ok(out)
}

/// Every non-geometry pack of an archive file, and the archive's release.
pub fn read_archive(path: &Path) -> R<(&'static str, Vec<PackData>)> {
    let bytes = std::fs::read(path).map_err(|e| format!("{}: {e}", path.display()))?;
    let rel = crate::modcheck::media::detect_release(&bytes, path)?;
    if rel == "proto" {
        return Ok(("proto", proto_packs(&bytes)?));
    }
    let arc = Archive::from_bytes(bytes, path)?;
    let mut out = vec![];
    for e in &arc.entries {
        if e.typ == 7 {
            continue;
        }
        let pk = RawPack::new(arc.release, e.typ, arc.pack_bytes(e), &e.name)?;
        let ptrs = pk.words("pointers").into_iter().collect();
        out.push(PackData { typ: e.typ, id: e.id, blob: Blob { d: pk.section("data").to_vec(), ptrs, big: arc.release == "gc" } });
    }
    Ok((arc.release, out))
}

/// A file named `name` (any case, any extension) under `dir`, searched a few levels down.
pub fn find_archive(dir: &Path, name: &str) -> Option<PathBuf> {
    fn walk(d: &Path, name: &str, depth: u32) -> Option<PathBuf> {
        let mut subs = vec![];
        for e in std::fs::read_dir(d).ok()?.flatten() {
            let p = e.path();
            if p.is_dir() {
                subs.push(p);
            } else if crate::modcheck::media::stem(&p).eq_ignore_ascii_case(name) {
                return Some(p);
            }
        }
        subs.sort();
        if depth > 0 {
            for s in subs {
                if let Some(p) = walk(&s, name, depth - 1) {
                    return Some(p);
                }
            }
        }
        None
    }
    walk(dir, name, 3)
}

// ---- cars --------------------------------------------------------------------------------

/// A struct's field: (offset, C type), from the schema.
fn field<'a>(s: &'a Schemas, st: &str, name: &str) -> R<(u32, &'a str)> {
    let sch = s.top(st).or_else(|| s.struct_(st)).ok_or_else(|| format!("no schema {st}"))?;
    let p = &sch["properties"][name];
    if p.is_null() {
        return Err(format!("{st}.{name}: not in the schema"));
    }
    Ok((schema::offset(p).map_err(|e| e.msg)?, schema::ctype(p)))
}

fn num(b: &Blob, off: u32, ctype: &str) -> Value {
    let (base, dims) = schema::parse_ctype(ctype);
    let Some(pr) = schema::prim(&base) else { return Value::Null };
    let one = |o: u32| -> Value {
        match read_prim(&b.d, o, pr, b.big) {
            Ok(Raw::I(v)) => json!(v),
            Ok(Raw::F(v)) => json!(v as f32),
            Err(_) => Value::Null,
        }
    };
    if dims.is_empty() { one(off) } else { Value::Array((0..dims[0] as u32).map(|i| one(off + i * pr.size as u32)).collect()) }
}

/// The fields of one struct at `base`, by name.
fn fields(s: &Schemas, b: &Blob, st: &str, base: u32, names: &[&str]) -> R<Map<String, Value>> {
    let mut m = Map::new();
    for n in names {
        let (o, ct) = field(s, st, n)?;
        m.insert(n.to_string(), num(b, base + o, ct));
    }
    Ok(m)
}

const INFO: [&str; 9] =
    ["damagePercent", "driveMode", "damageLevel", "maxRpm", "upshiftRpm", "downshiftRpm", "turboRpmScale", "turboTorqueScale", "numWheels"];
const ENGINE: [&str; 9] =
    ["steerRate", "torqueCurve", "maxGrade", "damageScale", "maxImpactVel", "maxAngularVel", "selfRightDelay", "selfRightArm", "tiltForceScale"];
const ACCEL: [&str; 11] = [
    "driveRpmScale",
    "coastRpmScale",
    "tractionLift",
    "brakeDecel",
    "engineBrake",
    "reverseThrottleDecel",
    "airDownforceBase",
    "airDownforceRate",
    "airDownforceMax",
    "spinImpulse",
    "spinDecay",
];
const COLLIDER: [&str; 7] = ["mass", "maxHealth", "damageThreshold", "gravityScale", "gravity", "inertiaScale", "type"];
const STUNT: [&str; 11] = [
    "pitchRate",
    "rollRate",
    "spinRate",
    "maxTime",
    "turboPerPoint",
    "pitchScore",
    "rollScore",
    "spinScore",
    "startDelay",
    "minFailTime",
    "maxFailTime",
];

/// One car pack's CarAInfo (root 0) and its parts, as JSON.
pub fn car(s: &Schemas, b: &Blob) -> R<Value> {
    let n = b.u32(0).ok_or("empty car pack")?;
    let tab = b.ptr(4).ok_or("car pack without a root table")?;
    let info = b.ptr(tab).ok_or("car pack without root 0")?;
    let part = |name: &str| -> R<Option<u32>> {
        let (o, _) = field(s, "CarAInfo", name)?;
        Ok(b.ptr(info + o))
    };
    let mut m = Map::new();
    m.insert("roots".into(), json!(n));
    let hud = part("hud")?.ok_or("no hud")?;
    let (so, _) = field(s, "CarAHud", "stats")?;
    m.insert("stats".into(), json!((0..4).map(|i| b.u8(hud + so + i).unwrap_or(0)).collect::<Vec<_>>()));
    m.insert("info".into(), Value::Object(fields(s, b, "CarAInfo", info, &INFO)?));
    if let Some(g) = part("gripScale")? {
        m["info"]["gripScale"] = json!(b.f32(g));
    }
    for (key, st, names) in [
        ("engine", "CarAEngine", &ENGINE[..]),
        ("accel", "CarAAccel", &ACCEL[..]),
        ("collider", "CarACollider", &COLLIDER[..]),
        ("stunt", "CarAStuntInfo", &STUNT[..]),
    ] {
        match part(key)? {
            Some(p) => m.insert(key.into(), Value::Object(fields(s, b, st, p, names)?)),
            None => m.insert(key.into(), Value::Null),
        };
    }
    if let Some(c) = part("collider")?
        && let Some(bx) = b.ptr(c + field(s, "CarACollider", "box")?.0)
    {
        m["collider"]["box"] = json!((0..8).map(|i| b.f32(bx + 4 * i)).collect::<Vec<_>>());
    }
    // gears: CarAGear { f32 ratio; f32 soundSet } (CarA.h)
    let ng = b.u32(info + field(s, "CarAInfo", "numGears")?.0).unwrap_or(0);
    let gears: Vec<Value> = match part("gears")? {
        Some(g) if ng < 32 => (0..ng).map(|i| json!(b.f32(g + 8 * i))).collect(),
        _ => vec![],
    };
    m.insert("gears".into(), json!(gears));
    // wheels: CarTrailBone, 0x50 bytes (common.h)
    let nw = b.u32(info + field(s, "CarAInfo", "numWheels")?.0).unwrap_or(0);
    let mut wheels = vec![];
    if let Some(w) = part("wheels")?
        && nw < 16
    {
        for i in 0..nw {
            let o = w + 0x50 * i;
            wheels.push(json!({
                "offset": b.f32(o), "restLength": b.f32(o + 0x0C), "springRate": b.f32(o + 0x10),
                "driveBlend": b.f32(o + 0x14), "friction": b.f32(o + 0x18), "damping": b.f32(o + 0x1C),
                "maxFriction": b.f32(o + 0x20), "handbrakeGrip": b.f32(o + 0x24), "maxDamping": b.f32(o + 0x38),
                "driven": b.u8(o + 0x3C),
            }));
        }
    }
    m.insert("wheels".into(), json!(wheels));
    Ok(Value::Object(m))
}

// ---- the car select ------------------------------------------------------------------------

const HDR_OBJECTS: u32 = 0x10; // InterfaceHeader.objects (numObjects at 0x0C)
const DEF_SIZE: u32 = 0x6C; // IfaceObjectDef

/// Interface headers in a pack: every pointer slot that can be an InterfaceHeader.objects whose
/// header has id `want`, a sane object count and defs whose anim/sub slots are pointers or 0.
pub fn headers(b: &Blob, want: u16) -> Vec<u32> {
    let mut out = vec![];
    for &slot in &b.ptrs {
        let Some(h) = slot.checked_sub(HDR_OBJECTS) else { continue };
        if b.u16(h) != Some(want) || b.u16(h + 2) != Some(0) {
            continue;
        }
        let Some(n) = b.u32(h + 0xC) else { continue };
        let Some(objs) = b.ptr(slot) else { continue };
        if n == 0 || n > 400 || objs as usize + (n * DEF_SIZE) as usize > b.d.len() {
            continue;
        }
        let ok = (0..n).all(|i| {
            let d = objs + i * DEF_SIZE;
            [0x14u32, 0x60, 0x68].iter().all(|&f| b.ptrs.contains(&(d + f)) || b.u32(d + f) == Some(0))
        });
        if ok {
            out.push(h);
        }
    }
    out
}

/// A key track { count; keys } of `ksize`-byte keys, as [time-of-key, values..., interp].
/// Scale/pos keys (ExtractScaleKey): Vector4 v, u8 active, u8 interp, f32 duration at 0x14.
fn vec_track(b: &Blob, t: u32) -> Value {
    let n = b.u32(t).unwrap_or(0);
    let Some(k) = b.ptr(t + 4) else { return json!([]) };
    json!((0..n.min(64))
        .map(|i| {
            let o = k + 0x20 * i;
            json!({"v": [b.f32(o), b.f32(o + 4), b.f32(o + 8)], "active": b.u8(o + 0x10), "interp": b.u8(o + 0x11), "duration": b.f32(o + 0x14)})
        })
        .collect::<Vec<_>>())
}

/// Colour keys (ExtractColorKey): rgba, active, interp, f32 duration at 8.
fn color_track(b: &Blob, t: u32) -> Value {
    let n = b.u32(t).unwrap_or(0);
    let Some(k) = b.ptr(t + 4) else { return json!([]) };
    json!((0..n.min(64))
        .map(|i| {
            let o = k + 0xC * i;
            json!({"rgba": [b.u8(o), b.u8(o + 1), b.u8(o + 2), b.u8(o + 3)], "active": b.u8(o + 4), "interp": b.u8(o + 5), "duration": b.f32(o + 8)})
        })
        .collect::<Vec<_>>())
}

/// UV keys (ExtractUVKey): 8 f32, active, interp, f32 duration at 0x24.
fn uv_track(b: &Blob, t: u32) -> Value {
    let n = b.u32(t).unwrap_or(0);
    let Some(k) = b.ptr(t + 4) else { return json!([]) };
    json!((0..n.min(64))
        .map(|i| {
            let o = k + 0x28 * i;
            json!({"uv": (0..8).map(|j| b.f32(o + 4 * j)).collect::<Vec<_>>(), "active": b.u8(o + 0x20), "interp": b.u8(o + 0x21), "duration": b.f32(o + 0x24)})
        })
        .collect::<Vec<_>>())
}

/// IfaceObjAnim (pos, rot, scale, color, uv) or IfaceStateAnim (active, pos, color, scale, uv,
/// rot) at a: the tracks with keys.
fn anim(b: &Blob, a: u32, state: bool) -> Value {
    let order: &[(&str, u32)] = if state {
        &[("pos", 0x08), ("color", 0x10), ("scale", 0x18), ("uv", 0x20), ("rot", 0x28)]
    } else {
        &[("pos", 0x00), ("rot", 0x08), ("scale", 0x10), ("color", 0x18), ("uv", 0x20)]
    };
    let mut m = Map::new();
    for &(n, o) in order {
        if b.u32(a + o).unwrap_or(0) == 0 {
            continue;
        }
        let t = match n {
            "color" => color_track(b, a + o),
            "uv" => uv_track(b, a + o),
            _ => vec_track(b, a + o),
        };
        m.insert(n.into(), t);
    }
    Value::Object(m)
}

/// The text an object shows first (IfaceText.strings[0]), or its list's entries.
fn text(b: &Blob, d: u32) -> (Option<String>, Vec<String>) {
    if !matches!(b.u8(d + 1), Some(1 | 12)) {
        return (None, vec![]);
    }
    let Some(t) = b.ptr(d + 0x14) else { return (None, vec![]) };
    let first = b.ptr(t).and_then(|s| b.ptr(s)).and_then(|c| b.cstr(c));
    let mut list = vec![];
    if b.u8(t + 0x10) == Some(1)
        && let Some(l) = b.ptr(t + 8)
    {
        let n = b.u32(l).unwrap_or(0).min(256);
        if let Some(texts) = b.ptr(l + 4) {
            for i in 0..n {
                list.push(b.ptr(texts + 4 * i).and_then(|tb| b.ptr(tb)).and_then(|c| b.cstr(c)).unwrap_or_default());
            }
        }
    }
    (first, list)
}

/// The first position key of an object (its default animation's pos track), x y.
fn pos0(b: &Blob, d: u32) -> Option<(f32, f32)> {
    let a = b.ptr(d + 0x60)?;
    let k = b.ptr(a + 4)?;
    Some((b.f32(k)?, b.f32(k + 4)?))
}

/// The car select (header id 2) of an interface pack: per header found, its objects of roles
/// 70-73 with their tracks and nearest text label, and the car-name list (the longest list).
pub fn car_select(b: &Blob) -> Vec<Value> {
    let mut out = vec![];
    for h in headers(b, 2) {
        let n = b.u32(h + 0xC).unwrap_or(0);
        let objs = b.ptr(h + HDR_OBJECTS).unwrap_or(0);
        let defs: Vec<u32> = (0..n).map(|i| objs + i * DEF_SIZE).collect();
        let labels: Vec<(u32, String, (f32, f32))> = defs
            .iter()
            .enumerate()
            .filter_map(|(i, &d)| {
                if (70..=73).contains(&b.u8(d).unwrap_or(0)) {
                    return None;
                }
                let (t, _) = text(b, d);
                Some((i as u32, t?, pos0(b, d)?))
            })
            .collect();
        let mut names: Vec<String> = vec![];
        for &d in &defs {
            let (_, l) = text(b, d);
            if l.len() > names.len() {
                names = l;
            }
        }
        let mut bars = vec![];
        for (i, &d) in defs.iter().enumerate() {
            let role = b.u8(d).unwrap_or(0);
            if !(70..=73).contains(&role) {
                continue;
            }
            let p = pos0(b, d);
            // the label: the text object nearest the bar's first position key
            let label = p.and_then(|(x, y)| {
                labels
                    .iter()
                    .min_by(|a, c| {
                        let da = (a.2.0 - x).abs() + 4.0 * (a.2.1 - y).abs();
                        let dc = (c.2.0 - x).abs() + 4.0 * (c.2.1 - y).abs();
                        da.partial_cmp(&dc).unwrap()
                    })
                    .map(|l| json!({"object": l.0, "text": l.1, "pos": [l.2.0, l.2.1]}))
            });
            let mut states = vec![];
            let ns = b.u32(d + 0x64).unwrap_or(0).min(16);
            if let Some(st) = b.ptr(d + 0x68) {
                for k in 0..ns {
                    let sd = st + 0xC * k;
                    if let Some(a) = b.ptr(sd + 8) {
                        states.push(json!({"type": b.u8(sd), "value": b.u32(sd + 4), "tracks": anim(b, a, true)}));
                    }
                }
            }
            bars.push(json!({
                "object": i, "role": role, "type": b.u8(d + 1), "anchor": b.u16(d + 0xC).map(|x| x as i16),
                "label": label,
                "anim": b.ptr(d + 0x60).map(|a| anim(b, a, false)), "states": states,
            }));
        }
        if !bars.is_empty() {
            out.push(json!({"header": h, "objects": n, "bars": bars, "names": names}));
        }
    }
    out
}

// ---- the curve ------------------------------------------------------------------------------

/// ExtractScaleKey evaluated as InterfaceManager plays a role value: reset, then dt = value.
/// Returns the track's x, y, z at time t.
pub fn eval_vec(keys: &[Value], t: f32) -> Option<[f32; 3]> {
    let k: Vec<([f32; 3], u8, u8, f32)> = keys
        .iter()
        .map(|x| {
            let v = &x["v"];
            let f = |i: usize| v[i].as_f64().unwrap_or(0.0) as f32;
            ([f(0), f(1), f(2)], x["active"].as_u64().unwrap_or(0) as u8, x["interp"].as_u64().unwrap_or(0) as u8, x["duration"].as_f64().unwrap_or(0.0) as f32)
        })
        .collect();
    if k.is_empty() {
        return None;
    }
    let (mut index, mut current, mut time) = (0usize, 0usize, t);
    // reset: s->time = dt, then the catch-up loop (the reset path always runs it)
    loop {
        if time < k[index].3 {
            break;
        }
        current = index;
        time -= k[index].3;
        index += 1;
        if index >= k.len() {
            match k.iter().position(|x| x.1 != 0) {
                Some(i) => index = i,
                None => return Some(k[current].0),
            }
            break;
        }
    }
    let key = &k[index];
    let slope = |j: usize| k[index].0[j] - k[current].0[j];
    let f = match key.2 {
        0 => {
            let s = if key.3 > 0.0 { 1.0 / key.3 } else { 0.0 };
            time * s
        }
        1 | 2 => {
            let f = if key.3 != 0.0 { time / key.3 } else { 0.0 };
            if key.2 == 1 { 1.0 - (1.0 - f) * (1.0 - f) } else { f * f }
        }
        _ => 0.0,
    };
    Some([0, 1, 2].map(|j| k[current].0[j] + f * slope(j)))
}

// ---- the command ----------------------------------------------------------------------------

/// Everything for one release folder: {release, cars: {id: car}, select: [...]}.
pub fn release(s: &Schemas, dir: &Path) -> R<Value> {
    let cars_path = find_archive(dir, "HW_Cars").ok_or_else(|| format!("{}: no HW_Cars", dir.display()))?;
    let (rel, packs) = read_archive(&cars_path)?;
    let mut cars = Map::new();
    for p in packs.iter().filter(|p| p.typ == 0) {
        cars.insert(p.id.to_string(), car(s, &p.blob).unwrap_or_else(|e| json!({"error": e})));
    }
    let mut select = vec![];
    if let Some(ip) = find_archive(dir, "HW_IPack") {
        let (_, ipacks) = read_archive(&ip)?;
        for p in ipacks.iter().filter(|p| p.typ == 1) {
            for mut x in car_select(&p.blob) {
                x["pack"] = json!(format!("interface_{}", p.id));
                select.push(x);
            }
        }
    }
    Ok(json!({"release": rel, "dir": dir.display().to_string(), "cars": cars, "select": select}))
}

// ---- the report ----------------------------------------------------------------------------

/// The prototype's car n is the release's car PROTO_TWIN[n] (-1: only the prototype has it):
/// src/game/Port_Proto.c s_cars.
pub const PROTO_TWIN: [i8; 36] = [
    0, -1, 5, 3, 4, 17, 9, 1, 2, 6, 7, 8, -1, 10, 14, 15, 13, -1, 12, 16, 11, 18, 19, 20, 21, 22, 23, 24, 25, 29, 27, 28, 26, 30,
    31, 32,
];

/// carStats index -> label, as the retail car select labels roles 70-73.
pub const STAT_NAMES: [&str; 4] = ["SPEED", "ARMOR", "GRIP", "STUNT"];

/// A JSON number as the f32 it was read from (shortest text), ints as they are.
fn f(v: &Value) -> String {
    match v {
        Value::Number(n) if n.is_i64() || n.is_u64() => n.to_string(),
        Value::Number(n) => {
            let x = n.as_f64().unwrap_or(0.0) as f32;
            format!("{x}")
        }
        Value::Array(a) => a.iter().map(f).collect::<Vec<_>>().join(" "),
        Value::Null => "-".into(),
        _ => v.to_string(),
    }
}

/// A JSON pointer's value in a car, formatted ("/accel/brakeDecel", "/wheels/*/friction").
fn cell(car: &Value, path: &str) -> String {
    if let Some((a, b)) = path.split_once("/*/") {
        let Some(arr) = car.pointer(a).and_then(Value::as_array) else { return "-".into() };
        let parts: Vec<String> = arr.iter().map(|w| f(w.pointer(&format!("/{b}")).unwrap_or(&Value::Null))).collect();
        // a pair per axle when the wheels come in equal pairs
        if parts.len() == 4 && parts[0] == parts[1] && parts[2] == parts[3] {
            return format!("{} / {}", parts[0], parts[2]);
        }
        return parts.join(" ");
    }
    f(car.pointer(path).unwrap_or(&Value::Null))
}

/// The car name a list entry gives (the retail lists end each name with '*').
fn name(names: &[Value], i: usize) -> String {
    names.get(i).and_then(Value::as_str).unwrap_or("?").trim_end_matches('*').replace('|', "/")
}

fn select_names(rel: &Value) -> Vec<Value> {
    rel["select"].as_array().and_then(|s| s.first()).map(|s| s["names"].as_array().cloned().unwrap_or_default()).unwrap_or_default()
}

/// Groups of columns: (title, what it feeds, [(column, JSON pointer)]).
type Group = (&'static str, &'static str, &'static [(&'static str, &'static str)]);
const GROUPS: &[Group] = &[
    (
        "SPEED: engine, gears, drive",
        "torque curve, rpm limits, gear ratios (wheel speed per rpm), drive friction, brakes, 4WD turbo, mass",
        &[
            ("maxRpm", "/info/maxRpm"),
            ("up", "/info/upshiftRpm"),
            ("down", "/info/downshiftRpm"),
            ("torqueCurve (t0 tMax s0 sMax)", "/engine/torqueCurve"),
            ("gears (ratio)", "/gears"),
            ("driveRpmScale", "/accel/driveRpmScale"),
            ("coastRpmScale", "/accel/coastRpmScale"),
            ("brakeDecel", "/accel/brakeDecel"),
            ("engineBrake", "/accel/engineBrake"),
            ("turboRpm", "/info/turboRpmScale"),
            ("turboTorque", "/info/turboTorqueScale"),
            ("maxGrade", "/engine/maxGrade"),
            ("mass", "/collider/mass"),
        ],
    ),
    (
        "GRIP: tyres, steering, suspension",
        "wheels: front pair / rear pair when they match, else all four; gripScale is the AI's (AI_CarA.c)",
        &[
            ("driveMode", "/info/driveMode"),
            ("steerRate", "/engine/steerRate"),
            ("tractionLift", "/accel/tractionLift"),
            ("friction", "/wheels/*/friction"),
            ("maxFriction", "/wheels/*/maxFriction"),
            ("handbrakeGrip", "/wheels/*/handbrakeGrip"),
            ("driveBlend", "/wheels/*/driveBlend"),
            ("springRate", "/wheels/*/springRate"),
            ("damping", "/wheels/*/damping"),
            ("restLength", "/wheels/*/restLength"),
            ("inertiaScale", "/collider/inertiaScale"),
            ("gripScale (AI)", "/info/gripScale"),
        ],
    ),
    (
        "ARMOR: health and damage",
        "maxHealth / damageThreshold (CarACollider), bumper damage per closing speed (damageScale), parts breaking off",
        &[
            ("maxHealth", "/collider/maxHealth"),
            ("damageThreshold", "/collider/damageThreshold"),
            ("damageScale", "/engine/damageScale"),
            ("maxImpactVel", "/engine/maxImpactVel"),
            ("damagePercent", "/info/damagePercent"),
            ("damageLevel", "/info/damageLevel"),
            ("mass", "/collider/mass"),
            ("box (min xyz w, max xyz w)", "/collider/box"),
        ],
    ),
    (
        "STUNT: air control and scoring",
        "rotation rates (rad/s), the longest stunt, points per rotation, turbo per point",
        &[
            ("pitchRate", "/stunt/pitchRate"),
            ("rollRate", "/stunt/rollRate"),
            ("spinRate", "/stunt/spinRate"),
            ("maxTime", "/stunt/maxTime"),
            ("pitchScore", "/stunt/pitchScore"),
            ("rollScore", "/stunt/rollScore"),
            ("spinScore", "/stunt/spinScore"),
            ("turboPerPoint", "/stunt/turboPerPoint"),
            ("startDelay", "/stunt/startDelay"),
            ("minFail", "/stunt/minFailTime"),
            ("maxFail", "/stunt/maxFailTime"),
            ("airDownforce (base rate max)", "/accel/airDownforceBase"),
            ("spinImpulse", "/accel/spinImpulse"),
            ("spinDecay", "/accel/spinDecay"),
        ],
    ),
];

fn row(cells: &[String]) -> String {
    format!("| {} |\n", cells.join(" | "))
}

fn header(cols: &[&str]) -> String {
    row(&cols.iter().map(|s| s.to_string()).collect::<Vec<_>>()) + &row(&cols.iter().map(|_| "---".to_string()).collect::<Vec<_>>())
}

/// A value per release: one value when they agree, else "rel value" for each.
fn joined(vals: &[(String, String)]) -> String {
    if vals.iter().all(|v| v.1 == vals[0].1) {
        return vals[0].1.clone();
    }
    vals.iter().map(|(r, v)| format!("**{r}** {v}")).collect::<Vec<_>>().join("; ")
}

/// The bar curve: each role's image bar (its scale track) and number (its pos track) at
/// sample values, as InterfaceManager plays them (track time = the stat byte).
fn curve_table(rel: &Value) -> String {
    let mut out = String::new();
    let Some(sel) = rel["select"].as_array() else { return out };
    let mut seen = BTreeSet::new();
    for s in sel {
        let key = serde_json::to_string(&s["bars"]).unwrap_or_default();
        if !seen.insert(key) {
            continue;
        }
        let packs: Vec<String> = sel
            .iter()
            .filter(|x| x["bars"] == s["bars"])
            .map(|x| format!("{} header 0x{:X} ({} objects)", x["pack"].as_str().unwrap_or(""), x["header"].as_u64().unwrap_or(0), x["objects"]))
            .collect();
        out += &format!("\n{} car select: {}\n\n", rel["release"].as_str().unwrap_or(""), packs.join(", "));
        out += &header(&["object", "role", "carStats", "label", "kind", "track", "at 0", "25", "50", "75", "100", "150", "255"]);
        for b in s["bars"].as_array().into_iter().flatten() {
            let role = b["role"].as_u64().unwrap_or(0);
            let kind = if b["type"] == 1 { "number" } else { "bar" };
            let (tname, axis) = if b["type"] == 1 {
                ("pos x", 0)
            } else {
                // a bar grows along the scale axis whose keys differ
                let k = b["anim"]["scale"].as_array().cloned().unwrap_or_default();
                let ax = if k.len() > 1 && k[0]["v"][0] == k[k.len() - 1]["v"][0] { 1 } else { 0 };
                (if ax == 0 { "scale x" } else { "scale y" }, ax)
            };
            let tr = if b["type"] == 1 { "pos" } else { "scale" };
            let keys = b["anim"][tr].as_array().cloned().unwrap_or_default();
            let mut cells = vec![
                b["object"].to_string(),
                role.to_string(),
                format!("[{}]", role - 70),
                b["label"]["text"].as_str().unwrap_or("?").to_string(),
                kind.into(),
                tname.into(),
            ];
            for t in [0.0, 25.0, 50.0, 75.0, 100.0, 150.0, 255.0] {
                cells.push(eval_vec(&keys, t).map(|v| format!("{}", v[axis])).unwrap_or("-".into()));
            }
            out += &row(&cells);
        }
    }
    out
}

/// Markdown tables of the releases: bar roles and curve, the four bytes, the physics fields.
pub fn report(rels: &[Value]) -> String {
    let mut out = String::new();
    let retail: Vec<&Value> = rels.iter().filter(|r| r["release"] != "proto").collect();
    let proto: Option<&Value> = rels.iter().find(|r| r["release"] == "proto");
    out += "## Bars on the car select (roles 70-73)\n";
    for r in rels {
        out += &curve_table(r);
    }
    // the four bytes
    if let Some(first) = retail.first() {
        let names = select_names(first);
        out += "\n## CarAHud.stats (the four bytes)\n\n";
        let rn: Vec<&str> = retail.iter().map(|r| r["release"].as_str().unwrap_or("")).collect();
        out += &format!("Releases read: {}. Columns are carStats[0..3].\n\n", rn.join(", "));
        out += &header(&["car", "name", "SPEED [0]", "ARMOR [1]", "GRIP [2]", "STUNT [3]", "releases"]);
        let ids: Vec<usize> = {
            let mut v: Vec<usize> = first["cars"].as_object().map(|m| m.keys().filter_map(|k| k.parse().ok()).collect()).unwrap_or_default();
            v.sort();
            v
        };
        for &id in &ids {
            let k = id.to_string();
            let mut cells = vec![k.clone(), name(&names, id)];
            let per: Vec<Vec<String>> = retail.iter().map(|r| (0..4).map(|i| f(&r["cars"][&k]["stats"][i])).collect()).collect();
            for i in 0..4 {
                cells.push(joined(&retail.iter().zip(&per).map(|(r, p)| (r["release"].as_str().unwrap_or("").to_string(), p[i].clone())).collect::<Vec<_>>()));
            }
            cells.push(if per.iter().all(|p| *p == per[0]) { "same".into() } else { "differ".into() });
            out += &row(&cells);
        }
        for (title, note, cols) in GROUPS {
            out += &format!("\n## {title}\n\n{note}. One value: all of {} agree; else per release.\n\n", rn.join("/"));
            let mut h = vec!["car", "name"];
            h.extend(cols.iter().map(|c| c.0));
            out += &header(&h);
            for &id in &ids {
                let k = id.to_string();
                let mut cells = vec![k.clone(), name(&names, id)];
                for (_, p) in cols.iter() {
                    let p = if *p == "/accel/airDownforceBase" { "/accel/airDownforce*" } else { p };
                    let vals: Vec<(String, String)> = retail
                        .iter()
                        .map(|r| {
                            let c = &r["cars"][&k];
                            let v = if p == "/accel/airDownforce*" {
                                ["Base", "Rate", "Max"].iter().map(|s| cell(c, &format!("/accel/airDownforce{s}"))).collect::<Vec<_>>().join(" ")
                            } else {
                                cell(c, p)
                            };
                            (r["release"].as_str().unwrap_or("").to_string(), v)
                        })
                        .collect();
                    cells.push(joined(&vals));
                }
                out += &row(&cells);
            }
        }
    }
    if let Some(p) = proto {
        let names = select_names(p);
        let rnames = retail.first().map(|r| select_names(r)).unwrap_or_default();
        out += "\n## The PS2 prototype's cars\n\nTwin: the release car it is (Port_Proto.c s_cars). Stats differing from the twin's (on the first release read) in bold; the physics groups list only the fields that differ from the twin.\n\n";
        out += &header(&["proto car", "name", "twin", "SPEED [0]", "ARMOR [1]", "GRIP [2]", "STUNT [3]", "physics fields not as the twin"]);
        for id in 0..36usize {
            let k = id.to_string();
            let Some(c) = p["cars"].get(&k) else { continue };
            let twin = PROTO_TWIN.get(id).copied().unwrap_or(-1);
            let tc = (twin >= 0).then(|| retail.first().map(|r| &r["cars"][twin.to_string()])).flatten();
            let mut cells = vec![k.clone(), name(&names, id), if twin >= 0 { format!("{twin} {}", name(&rnames, twin as usize)) } else { "-".into() }];
            for i in 0..4 {
                let v = f(&c["stats"][i]);
                let same = tc.map(|t| f(&t["stats"][i]) == v).unwrap_or(true);
                cells.push(if same { v } else { format!("**{v}**") });
            }
            let mut diffs = vec![];
            if let Some(t) = tc {
                for (_, _, cols) in GROUPS {
                    for (n, path) in cols.iter() {
                        let (a, b) = (cell(c, path), cell(t, path));
                        if a != b && !diffs.iter().any(|d: &String| d.starts_with(&format!("{n} "))) {
                            diffs.push(format!("{n} {a} (twin {b})"));
                        }
                    }
                }
            }
            cells.push(if tc.is_none() { "(no twin)".into() } else if diffs.is_empty() { "none".into() } else { diffs.join("; ") });
            out += &row(&cells);
        }
    }
    out
}
