//! One pack of an `hwvx-x extract` tree (xdl_struct.Pack), the pairing table (Pairing) and the
//! base:// link grammar.

use crate::error::{self, Error, Result};
use crate::py;
use regex::Regex;
use serde_json::Value;
use std::cell::{OnceCell, RefCell};
use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet};
use std::path::{Path, PathBuf};
use std::sync::OnceLock;

/// Share packs by the id texture/sound/ref relocations put in their high half.
pub const SHARE: [(u32, &str, &str); 3] = [(1, "HW_share", "share_1"), (2, "HW_Cars", "share_2"), (3, "HW_Items", "share_3")];

/// SHARE[sid]
pub fn share(sid: u32) -> Option<(&'static str, &'static str)> {
    SHARE.iter().find(|s| s.0 == sid).map(|s| (s.1, s.2))
}

/// SHARE_ID[(archive, pack)] (case-sensitive, as the Python's tuple key)
pub fn share_id(archive: &str, pack: &str) -> Option<u32> {
    SHARE.iter().find(|s| s.1 == archive && s.2 == pack).map(|s| s.0)
}

/// Root objects: struct -> how a pack lists them (xdl_struct.ROOTS).
pub fn root_kind(sname: &str) -> Option<&'static str> {
    match sname {
        "CarAInfo" => Some("car"),
        _ => None,
    }
}

/// The parts round-tripped with --parts: CarAInfo field -> struct (xdl_struct.PARTS).
pub const PARTS: [(&str, &str); 5] =
    [("engine", "CarAEngine"), ("accel", "CarAAccel"), ("stunt", "CarAStuntInfo"), ("hud", "CarAHud"), ("collider", "CarACollider")];

pub fn base_re() -> &'static Regex {
    static R: OnceLock<Regex> = OnceLock::new();
    R.get_or_init(|| {
        Regex::new(r"^base://([A-Za-z0-9_]+)/([A-Za-z0-9_]+)/([A-Za-z0-9_]+)/((?:[a-z0-9]+:)?[0-9]+)((?:/[A-Za-z0-9_]+)*)$").unwrap()
    })
}

/// A base:// link's parts: archive, pack, kind, index ("4" or "pc:4"), rest ("/engine" or "").
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BaseLink {
    pub archive: String,
    pub pack: String,
    pub kind: String,
    pub idx: String,
    pub rest: String,
}

impl BaseLink {
    pub fn parse(link: &str) -> Option<BaseLink> {
        let c = base_re().captures(link)?;
        Some(BaseLink {
            archive: c[1].to_string(),
            pack: c[2].to_string(),
            kind: c[3].to_string(),
            idx: c[4].to_string(),
            rest: c[5].to_string(),
        })
    }
    /// the rest's segments
    pub fn path(&self) -> Vec<&str> {
        self.rest.split('/').filter(|s| !s.is_empty()).collect()
    }
}

/// Python int() of a decimal text (the index of a link); ValueError otherwise.
pub(crate) fn int_of(s: &str) -> Result<i64> {
    s.parse::<i64>().map_err(|_| Error::uncaught(format!("invalid literal for int() with base 10: {}", py::str_repr(s))))
}

/// Relocations by kind, offsets relative to the object: {offset: value}.
#[derive(Debug, Clone, Default, PartialEq, Eq)]
pub struct Relocs {
    /// pointer slots: offset -> the u32 stored there (the target)
    pub pointers: BTreeMap<u32, u32>,
    pub texture: BTreeMap<u32, u32>,
    pub sound: BTreeMap<u32, u32>,
    pub r#ref: BTreeMap<u32, u32>,
}

impl Relocs {
    pub fn kinds(&self) -> [(&'static str, &BTreeMap<u32, u32>); 4] {
        [("pointers", &self.pointers), ("texture", &self.texture), ("sound", &self.sound), ("ref", &self.r#ref)]
    }
    pub fn kinds_mut(&mut self) -> [&mut BTreeMap<u32, u32>; 4] {
        [&mut self.pointers, &mut self.texture, &mut self.sound, &mut self.r#ref]
    }
    pub fn count(&self) -> usize {
        self.kinds().iter().map(|k| k.1.len()).sum()
    }
    /// {kind: {offset: value}} as the Python's json.dumps writes it (offsets as strings)
    pub fn to_json(&self) -> Value {
        let mut m = serde_json::Map::new();
        for (k, t) in self.kinds() {
            let mut o = serde_json::Map::new();
            for (a, b) in t {
                o.insert(a.to_string(), Value::from(*b));
            }
            m.insert(k.into(), Value::Object(o));
        }
        Value::Object(m)
    }
}

pub struct Pack {
    pub dir: PathBuf,
    pub release: String,
    /// big-endian (GameCube)
    pub big: bool,
    /// the manifest entry
    pub entry: Value,
    /// the archive folder's name
    pub archive: String,
    /// "<type>_<id>"
    pub name: String,
    pub blob: Vec<u8>,
    /// pack.json's header
    pub header: Value,
    pub ptrs: BTreeSet<u32>,
    pub tex: BTreeMap<u32, u32>,
    pub snd: BTreeMap<u32, u32>,
    pub refs: BTreeMap<u32, u32>,
    geoms: OnceCell<Vec<u32>>,
}

impl Pack {
    /// A pack folder of an extract tree (data.bin, pointers.bin, *_relocs.bin, pack.json; the
    /// archive's manifest.json one level up).
    pub fn open(folder: &Path) -> Result<Pack> {
        let parent = folder.parent().unwrap_or(Path::new("."));
        let man = crate::jsonc::parse(&error::read_text(&parent.join("manifest.json"))?)?;
        let release = py::str_of(&man["platform"]);
        let dname = folder.file_name().and_then(|n| n.to_str()).unwrap_or("").to_string();
        let entry = man["entries"]
            .as_array()
            .and_then(|es| es.iter().find(|x| x["path"].as_str() == Some(dname.as_str())))
            .cloned()
            .ok_or_else(|| Error::xdl(format!("{dname} not in {}/manifest.json", parent.display())))?;
        let archive = parent.file_name().and_then(|n| n.to_str()).unwrap_or("").to_string();
        let name = format!("{}_{}", py::str_of(&entry["type"]), py::str_of(&entry["id"]));
        let blob = error::read(&folder.join("data.bin"))?;
        let header = crate::jsonc::parse(&error::read_text(&folder.join("pack.json"))?)?["header"].clone();
        let big = release == "gc";
        let tab4 = |n: &str| -> Result<Vec<u32>> {
            let p = folder.join(format!("{n}.bin"));
            if !p.exists() {
                return Ok(vec![]);
            }
            let b = error::read(&p)?;
            Ok(b.chunks_exact(4).map(|c| word(c, big)).collect())
        };
        let tab8 = |n: &str| -> Result<BTreeMap<u32, u32>> {
            let p = folder.join(format!("{n}.bin"));
            if !p.exists() {
                return Ok(BTreeMap::new());
            }
            let b = error::read(&p)?;
            if b.len() % 8 != 0 {
                return Err(Error::strct(format!(
                    "unpack_from requires a buffer of at least {} bytes for unpacking 8 bytes at offset {} (actual buffer size is {})",
                    b.len() / 8 * 8 + 8,
                    b.len() / 8 * 8,
                    b.len()
                )));
            }
            Ok(b.chunks_exact(8).map(|c| (word(&c[..4], big), word(&c[4..], big))).collect())
        };
        Ok(Pack {
            dir: folder.to_path_buf(),
            ptrs: tab4("pointers")?.into_iter().collect(),
            tex: tab8("texture_relocs")?,
            snd: tab8("sound_relocs")?,
            refs: tab8("ref_relocs")?,
            release,
            big,
            entry,
            archive,
            name,
            blob,
            header,
            geoms: OnceCell::new(),
        })
    }

    /// A pack from its parts (xdl_graph's XsPack over a Source).
    #[allow(clippy::too_many_arguments)]
    pub fn from_parts(
        release: &str,
        archive: &str,
        name: &str,
        blob: Vec<u8>,
        ptrs: BTreeSet<u32>,
        tex: BTreeMap<u32, u32>,
        snd: BTreeMap<u32, u32>,
        refs: BTreeMap<u32, u32>,
    ) -> Pack {
        Pack {
            dir: PathBuf::new(),
            release: release.into(),
            big: release == "gc",
            entry: Value::Null,
            archive: archive.into(),
            name: name.into(),
            blob,
            header: Value::Null,
            ptrs,
            tex,
            snd,
            refs,
            geoms: OnceCell::new(),
        }
    }

    pub fn u32(&self, o: u32) -> Result<u32> {
        u32_at(&self.blob, o, self.big)
    }

    pub fn u16(&self, o: u32) -> Result<u16> {
        let o = o as usize;
        if o + 2 > self.blob.len() {
            return Err(short(2, o, self.blob.len()));
        }
        let b = [self.blob[o], self.blob[o + 1]];
        Ok(if self.big { u16::from_be_bytes(b) } else { u16::from_le_bytes(b) })
    }

    /// The pointer stored at o, if o is a pointer slot.
    pub fn target(&self, o: u32) -> Result<Option<u32>> {
        if !self.ptrs.contains(&o) {
            return Ok(None);
        }
        self.u32(o).map(Some)
    }

    /// Offsets of the pack's root objects (car/item/interface/audio packs: the table at 0).
    pub fn roots(&self, kind: &str) -> Result<Vec<Option<u32>>> {
        let t = py::str_of(&self.entry["type"]);
        if kind != t {
            return Err(Error::xdl(format!("{}/{} is a {t} pack, not {kind}", self.archive, self.name)));
        }
        let n = self.u32(0)?;
        let Some(tab) = self.target(4)? else { return Ok(vec![]) };
        (0..n).map(|i| self.target(tab.wrapping_add(8 * i))).collect()
    }

    /// Geometry header offsets in scan order (xdl_gltf.scan; pairing.md §1).
    pub fn geometries(&self) -> &[u32] {
        self.geoms.get_or_init(|| scan_geometries(&self.blob, &self.ptrs, &self.release))
    }

    /// Every slot in [lo, hi), offsets relative to lo.
    pub fn relocs_in(&self, lo: u32, hi: u32) -> Result<Relocs> {
        let mut r = Relocs::default();
        for &o in self.ptrs.range(lo..hi.max(lo)) {
            r.pointers.insert(o - lo, self.u32(o)?);
        }
        for (dst, src) in [(&mut r.texture, &self.tex), (&mut r.sound, &self.snd), (&mut r.r#ref, &self.refs)] {
            for (&o, &v) in src.range(lo..hi.max(lo)) {
                dst.insert(o - lo, v);
            }
        }
        Ok(r)
    }
}

pub(crate) fn word(c: &[u8], big: bool) -> u32 {
    let b = [c[0], c[1], c[2], c[3]];
    if big { u32::from_be_bytes(b) } else { u32::from_le_bytes(b) }
}

pub(crate) fn short(n: usize, o: usize, len: usize) -> Error {
    Error::strct(format!(
        "unpack_from requires a buffer of at least {} bytes for unpacking {n} bytes at offset {o} (actual buffer size is {len})",
        o + n
    ))
}

/// struct.unpack_from(e + "I", blob, o)
pub fn u32_at(blob: &[u8], o: u32, big: bool) -> Result<u32> {
    let o = o as usize;
    if o + 4 > blob.len() {
        return Err(short(4, o, blob.len()));
    }
    Ok(word(&blob[o..o + 4], big))
}

/// xdl_gltf.scan: geometry header offsets in scan order (the index of geometry/N).
pub fn scan_geometries(d: &[u8], ptrs: &BTreeSet<u32>, release: &str) -> Vec<u32> {
    let magic: u16 = match release {
        "gc" => 0x2003,
        "pc" => 0x0002,
        _ => 0x1005,
    };
    let big = release == "gc";
    let u16_ = |o: usize| {
        let b = [d[o], d[o + 1]];
        if big { u16::from_be_bytes(b) } else { u16::from_le_bytes(b) }
    };
    let mut out = vec![];
    if d.len() <= 0x40 {
        return out;
    }
    let mut off = 0usize;
    while off < d.len() - 0x40 {
        let o = off as u32;
        if u16_(off) == magic && ptrs.contains(&(o + 8)) && ptrs.contains(&(o + 0xC)) {
            let n = u16_(off + 2);
            if (1..=8).contains(&n) {
                let lods = word(&d[off + 8..off + 12], big);
                let ok = match release {
                    "gc" => (lods as usize) < d.len(),
                    "pc" => lods == o + 0x40,
                    // blob.target: a pointer slot whose value is inside the blob
                    _ => (lods as usize) < d.len(),
                };
                if ok {
                    out.push(o);
                }
            }
        }
        off += 4;
    }
    out
}

/// Release index <-> structural id, from docs/xdl-format/pairing/<archive>.jsonl.
pub struct Pairing {
    dir: Option<PathBuf>,
    fwd: RefCell<HashMap<(String, String, String, String, i64), String>>,
    rev: RefCell<HashMap<(String, String), i64>>,
    loaded: RefCell<HashSet<String>>,
}

impl Pairing {
    pub fn new(dir: Option<PathBuf>) -> Pairing {
        Pairing { dir, fwd: RefCell::default(), rev: RefCell::default(), loaded: RefCell::default() }
    }

    fn load(&self, archive: &str) -> Result<()> {
        let a = archive.to_lowercase();
        if !self.loaded.borrow_mut().insert(a.clone()) {
            return Ok(());
        }
        let Some(dir) = &self.dir else { return Ok(()) };
        let Ok(rd) = std::fs::read_dir(dir) else { return Ok(()) };
        for e in rd.flatten() {
            let p = e.path();
            let Some(fname) = p.file_name().and_then(|n| n.to_str()) else { continue };
            let Some(stem) = fname.strip_suffix(".jsonl") else { continue };
            if stem.to_lowercase() != a {
                continue;
            }
            let text = error::read_text(&p)?;
            for ln in text.lines() {
                let r = crate::jsonc::parse(ln)?;
                let Some(id) = r.get("id") else { continue };
                let id = py::str_of(id);
                let Some(m) = BaseLink::parse(&id) else { continue };
                for rel in ["gc", "pc", "ps2", "proto"] {
                    if let Some(x) = r.get(rel) {
                        let i = x["i"].as_i64().unwrap_or(0);
                        self.fwd.borrow_mut().insert((rel.into(), a.clone(), m.pack.clone(), m.kind.clone(), i), id.clone());
                        self.rev.borrow_mut().insert((rel.into(), id.to_lowercase()), i);
                    }
                }
            }
        }
        Ok(())
    }

    /// The base:// id of a release's `kind` index i in archive/pack.
    pub fn id(&self, rel: &str, archive: &str, pack: &str, kind: &str, i: i64) -> Result<String> {
        self.load(archive)?;
        if let Some(x) = self.fwd.borrow().get(&(rel.into(), archive.to_lowercase(), pack.into(), kind.into(), i)) {
            return Ok(x.clone());
        }
        if rel == "gc" {
            return Ok(format!("base://{archive}/{pack}/{kind}/{i}"));
        }
        Err(Error::xdl(format!(
            "no pairing id for {rel} {archive}/{pack} {kind} {i} (docs/xdl-format/pairing/{archive}.jsonl)"
        )))
    }

    /// (archive, pack, kind, index) of a base:// link in a release.
    pub fn index(&self, rel: &str, link: &str) -> Result<(String, String, String, i64)> {
        // the Python's re.match(...).groups() on a non-matching link: AttributeError
        let m = BaseLink::parse(link).ok_or_else(|| Error::uncaught("'NoneType' object has no attribute 'groups'"))?;
        self.load(&m.archive)?;
        if let Some(&i) = self.rev.borrow().get(&(rel.into(), link.to_lowercase())) {
            return Ok((m.archive, m.pack, m.kind, i));
        }
        if rel == "gc" && !m.idx.contains(':') {
            return Ok((m.archive.clone(), m.pack.clone(), m.kind.clone(), int_of(&m.idx)?));
        }
        if let Some(n) = m.idx.strip_prefix(&format!("{rel}:")) {
            return Ok((m.archive.clone(), m.pack.clone(), m.kind.clone(), int_of(n)?));
        }
        Err(Error::xdl(format!("{link} has no {rel} index in the pairing table")))
    }
}

/// The first `rel` that exists in `from` or a folder above it (xdl_struct.find_up).
pub fn find_up(from: &Path, rel: &str) -> Option<PathBuf> {
    let mut d = Some(from);
    while let Some(x) = d {
        if x.join(rel).exists() {
            return Some(x.join(rel));
        }
        d = x.parent();
    }
    None
}
