//! Whole packs <-> .xdl source projects (tools/xdl_graph.py): the Extractor that cuts a pack
//! into nodes with structural ids and writes them as files, and the Builder that lays a project
//! out again as pack files. What the level tools need of it: a world pack's project (the base
//! a level is checked against and built into) and its build.
//!
//! Geometry is kept in its original encoding only (`<id>.xgeo.jsonc` + `.orig.bin`): the glTF
//! files xdl_graph.py also writes are for editing models, which the level tools don't do. A
//! project with glTF (made by the Python) builds here only while nothing asks to re-encode it.

use crate::decode::{Decoder, LinkNamer};
use crate::encode::Encoder;
use crate::error::{self, Error, Kind, Result};
use crate::pack::{self, Pack, Pairing};
use crate::py;
use crate::schema::{self, Schemas};
use crate::tac::{self, Tac, TacHost, TacNode};
use crate::{geom, jsonc, walk};
use indexmap::IndexMap;
use serde_json::{Map, Value, json};
use std::cell::RefCell;
use std::collections::{BTreeMap, BTreeSet, HashMap, HashSet, VecDeque};
use std::path::{Path, PathBuf};
use std::rc::Rc;

pub const FORMAT: i64 = 1;
const INLINE_MAX: usize = 256;
const OWN_FILE_MIN: u32 = 4096;
pub const SECTIONS: [&str; 5] = ["pointers", "texture", "sound", "ref", "shader"];
const SECTION_ABOUT: [(&str, &str); 5] = [
    ("pointers.bin", "offsets of pointer slots in data"),
    ("texture_relocs.bin", "data offset + texture index (shared pack << 16)"),
    ("sound_relocs.bin", "data offset + sound index (shared pack << 16)"),
    ("ref_relocs.bin", "pointer slot + target (shared pack << 16 | index)"),
    ("shader_relocs.bin", "data offset + shader index"),
];
const SECTION_ORDER: [&str; 8] = [
    "sound_pointers.bin",
    "sound_data.bin",
    "texture_relocs.bin",
    "shader_relocs.bin",
    "sound_relocs.bin",
    "data.bin",
    "ref_relocs.bin",
    "pointers.bin",
];
const HEADER_OFFSETS: [(&str, &str); 4] = [
    ("texture_data_offset", "num_textures"),
    ("palette_offset", "num_palettes"),
    ("shader_offset", "num_shaders"),
    ("trigger_offset", "num_triggers"),
];

pub fn section_file(k: &str) -> &'static str {
    match k {
        "pointers" => "pointers.bin",
        "texture" => "texture_relocs.bin",
        "sound" => "sound_relocs.bin",
        "ref" => "ref_relocs.bin",
        _ => "shader_relocs.bin",
    }
}

fn root_kind(ptype: &str) -> Option<&'static str> {
    match ptype {
        "car" => Some("CarAInfo"),
        "item" => Some("ItemRecord"),
        "interface" => Some("IMHeader"),
        "audio" => Some("SoundSeq"),
        _ => None,
    }
}

/// walker labels that are a schematised struct (LABEL_SCHEMA)
fn label_schema(l: &str) -> Option<&'static str> {
    Some(match l {
        "CarAInfo" => "CarAInfo",
        "CarAEngine" => "CarAEngine",
        "CarAHud" => "CarAHud",
        "CarAStuntInfo" => "CarAStuntInfo",
        "CarAAccel" => "CarAAccel",
        "CarACollider" => "CarACollider",
        "CreatureInfo" => "CreatureInfo",
        "DestructibleInfo" => "DestructibleInfo",
        "WorldGameData" => "WorldGameData",
        "WeaponInfo" => "WeaponInfo",
        "ItemInfo" => "ItemRecord",
        "Projectile" => "ProjectileInfo",
        _ => return None,
    })
}

// ------------------------------------------------------------------------------------------
// small helpers

pub fn graph_err(m: impl Into<String>) -> Error {
    Error::graph(m)
}

pub fn hx(n: u32) -> String {
    format!("0x{n:X}")
}

/// Bytes as lines of 16, words separated.
pub fn hex_lines(b: &[u8]) -> Vec<String> {
    b.chunks(16)
        .map(|c| {
            let h: String = c.iter().map(|x| format!("{x:02x}")).collect();
            h.as_bytes().chunks(8).map(|w| std::str::from_utf8(w).unwrap()).collect::<Vec<_>>().join(" ")
        })
        .collect()
}

pub fn hexstr(b: &[u8]) -> String {
    b.iter().map(|x| format!("{x:02x}")).collect()
}

/// bytes.fromhex of a string or the joined lines.
pub fn unhex(v: &Value) -> Result<Vec<u8>> {
    let s: String = match v {
        Value::Array(a) => a.iter().map(py::str_of).collect(),
        Value::String(s) => s.clone(),
        _ => return Err(Error::uncaught(format!("fromhex() argument must be str, not {}", py::type_name(v)))),
    };
    let s: String = s.chars().filter(|c| *c != ' ').collect();
    crate::tac::fromhex(&s).ok_or_else(|| Error::uncaught("non-hexadecimal number found in fromhex() arg"))
}

pub fn align_of(off: u32) -> u32 {
    if off == 0 {
        return 32;
    }
    let mut a = 1;
    while a < 32 && off % (a * 2) == 0 {
        a *= 2;
    }
    a
}

/// base://A/P/x/y -> "x/y"
pub fn id_path(nid: &str) -> String {
    let r = nid.strip_prefix("base://").unwrap_or(nid);
    let mut it = r.splitn(3, '/');
    let (_, _, rest) = (it.next(), it.next(), it.next());
    rest.unwrap_or("").to_string()
}

pub fn file_stem(rest: &str) -> String {
    let s = rest.replace(':', "_");
    if s.chars().count() > 160 {
        use blake2::digest::{Update, VariableOutput};
        let mut h = blake2::Blake2bVar::new(8).unwrap();
        h.update(rest.as_bytes());
        let mut out = [0u8; 8];
        h.finalize_variable(&mut out).unwrap();
        return format!("deep/{}", hexstr(&out));
    }
    s
}

/// Append a step, counting repeats of the same p-step (linked lists).
pub fn add_step(path: &[String], seg: &str) -> Vec<String> {
    if seg.starts_with('p')
        && let Some(last) = path.last()
    {
        let (head, n) = match last.split_once("_r") {
            Some((h, n)) if !n.is_empty() && n.bytes().all(|c| c.is_ascii_digit()) => (h, Some(n)),
            _ => (last.as_str(), None),
        };
        let ok = head.len() > 1 && head.starts_with('p') && head[1..].bytes().all(|c| c.is_ascii_digit() || (b'a'..=b'f').contains(&c));
        if ok && head == seg {
            let k: u64 = n.map(|x| x.parse().unwrap_or(1)).unwrap_or(1) + 1;
            let mut out = path[..path.len() - 1].to_vec();
            out.push(format!("{seg}_r{k}"));
            return out;
        }
    }
    let mut out = path.to_vec();
    out.push(seg.to_string());
    out
}

pub fn strip_struct(t: &str) -> &str {
    t.strip_prefix("struct ").unwrap_or(t)
}

pub fn pack_name(entry: &Value) -> String {
    let mut n = format!("{}_{}", py::str_of(&entry["type"]), py::str_of(&entry["id"]));
    if entry["type"].as_str() == Some("geometry")
        && let Some(s) = entry.get("sub_id").and_then(Value::as_u64)
        && s != 0
    {
        n += &format!("_q{}", s & 0xFFFF);
    }
    n
}

/// What the graph tools share: the schemas, the pairing table, the archives' GameCube spelling.
pub struct Ctx {
    pub root: PathBuf,
    pub s: Schemas,
    pub pairing: Pairing,
    spelling: HashMap<String, String>,
}

impl Ctx {
    /// root: the HWVX-SDL checkout (schemas/; the pairing table in docs/ above it, if there).
    pub fn new(root: &Path) -> Result<Ctx> {
        let s = Schemas::load(&root.join("schemas"))?;
        let pdir = crate::pairing_dir(root);
        let mut spelling = HashMap::new();
        if let Some(d) = &pdir
            && let Ok(rd) = std::fs::read_dir(d)
        {
            for e in rd.flatten() {
                let n = e.file_name().to_string_lossy().into_owned();
                if let Some(stem) = n.strip_suffix(".jsonl") {
                    spelling.insert(stem.to_lowercase(), stem.to_string());
                }
            }
        }
        Ok(Ctx { root: root.to_path_buf(), s, pairing: Pairing::new(pdir), spelling })
    }

    pub fn archive_spelling(&self, name: &str) -> String {
        self.spelling.get(&name.to_lowercase()).cloned().unwrap_or_else(|| name.to_string())
    }
}

fn shared_pack(archive: &str, name: &str, sid: u32) -> (String, String) {
    if sid == 0 {
        return (archive.into(), name.into());
    }
    if let Some((a, p)) = pack::share(sid) {
        return (a.into(), p.into());
    }
    (format!("pack{sid}"), format!("share_{sid}"))
}

/// texture / sound reloc value -> base:// id (pairing table, or <release>:<index>)
pub fn asset_link(ctx: &Ctx, release: &str, archive: &str, name: &str, kind: &str, value: u32) -> Result<String> {
    let (sid, idx) = (value >> 16, value & 0xFFFF);
    let (arch, pname) = shared_pack(archive, name, sid);
    match ctx.pairing.id(release, &arch, &pname, kind, idx as i64) {
        Ok(s) => Ok(s),
        Err(e) if e.kind == Kind::Xdl => Ok(format!("base://{arch}/{pname}/{kind}/{release}:{idx}")),
        Err(e) => Err(e),
    }
}

/// base:// id -> reloc value (inverse of asset_link)
pub fn asset_value(ctx: &Ctx, release: &str, link: &str, kind: &str, own: (&str, &str)) -> Result<u32> {
    let m = pack::BaseLink::parse(link).ok_or_else(|| graph_err(format!("{link}: not a base:// {kind} id")))?;
    if m.kind != kind {
        return Err(graph_err(format!("{link} is not a {kind}")));
    }
    let sid = if m.archive.to_lowercase() == own.0.to_lowercase() && m.pack == own.1 {
        0
    } else if let Some(s) = pack::share_id(&m.archive, &m.pack) {
        s
    } else if m.archive.starts_with("pack") && m.pack.starts_with("share_") {
        pack::int_of(&m.pack[6..])? as u32
    } else {
        return Err(graph_err(format!("{link}: {kind}s come from the pack itself or a share pack")));
    };
    let i = if let Some(n) = m.idx.strip_prefix(&format!("{release}:")) {
        pack::int_of(n)?
    } else {
        match ctx.pairing.index(release, link) {
            Ok((_, _, _, i)) => i,
            Err(e) if e.kind == Kind::Xdl => return Err(graph_err(e.msg)),
            Err(e) => return Err(e),
        }
    };
    Ok((sid << 16) | (i as u32))
}

fn export_link(archive: &str, name: &str, value: u32) -> String {
    let (a, p) = shared_pack(archive, name, value >> 16);
    format!("base://{a}/{p}/export/{}", value & 0xFFFF)
}

fn export_value(link: &str) -> Result<u32> {
    let m = pack::BaseLink::parse(link).ok_or_else(|| Error::uncaught("'NoneType' object has no attribute 'groups'"))?;
    if m.kind != "export" {
        return Err(graph_err(format!("{link} is not an export")));
    }
    let sid = if let Some(s) = pack::share_id(&m.archive, &m.pack) {
        s
    } else if let Some(n) = m.pack.strip_prefix("share_") {
        pack::int_of(n)? as u32
    } else {
        return Err(graph_err(format!("{link}: unknown shared pack")));
    };
    Ok((sid << 16) | pack::int_of(&m.idx)? as u32)
}

fn u32_be(b: &[u8], o: usize, big: bool) -> u32 {
    let w = [b[o], b[o + 1], b[o + 2], b[o + 3]];
    if big { u32::from_be_bytes(w) } else { u32::from_le_bytes(w) }
}

fn put_u32(b: &mut [u8], o: usize, v: u32, big: bool) -> Result<()> {
    if o + 4 > b.len() {
        return Err(Error::strct(format!(
            "pack_into requires a buffer of at least {} bytes for packing 4 bytes at offset {o} (actual buffer size is {})",
            o + 4,
            b.len()
        )));
    }
    b[o..o + 4].copy_from_slice(&if big { v.to_be_bytes() } else { v.to_le_bytes() });
    Ok(())
}

/// bytearray slice assignment b[lo:hi] = zeros(n) (Python grows the array when hi is past it)
fn zero_slice(b: &mut Vec<u8>, lo: usize, hi: usize) {
    let n = hi - lo;
    let l = lo.min(b.len());
    let h = hi.min(b.len()).max(l);
    b.splice(l..h, std::iter::repeat_n(0u8, n));
}

pub fn write_text(p: &Path, text: &str) -> Result<()> {
    if let Some(d) = p.parent() {
        std::fs::create_dir_all(d).map_err(|e| Error::io(format!("{}: {e}", d.display())))?;
    }
    std::fs::write(p, text).map_err(|e| Error::io(format!("{}: {e}", p.display())))
}

fn write_bytes(p: &Path, b: &[u8]) -> Result<()> {
    if let Some(d) = p.parent() {
        std::fs::create_dir_all(d).map_err(|e| Error::io(format!("{}: {e}", d.display())))?;
    }
    std::fs::write(p, b).map_err(|e| Error::io(format!("{}: {e}", p.display())))
}

pub fn read_jsonc(p: &Path) -> Result<Value> {
    jsonc::loads(&error::read_text(p)?)
}

fn obj_get<'a>(v: &'a Value, k: &str) -> Option<&'a Value> {
    v.as_object().and_then(|m| m.get(k))
}

fn int_key(k: &str) -> Result<u32> {
    py::hex(k).map(|x| x as u32)
}

// ------------------------------------------------------------------------------------------
// the source pack

/// One pack of an `hwvx-x extract` tree, every table kept in its file order.
pub struct Source {
    pub dir: PathBuf,
    pub man: Value,
    pub release: String,
    pub big: bool,
    pub entry: Value,
    pub archive: String,
    pub name: String,
    pub packjson: Value,
    pub header: Value,
    pub blob: Vec<u8>,
    pub pointers: Vec<u32>,
    /// texture, sound, ref, shader tables as (offset, value) in file order
    pub tables: HashMap<&'static str, Vec<(u32, u32)>>,
    /// pointer slot -> target
    pub ptr: BTreeMap<u32, u32>,
    pub rel: HashMap<&'static str, BTreeMap<u32, u32>>,
}

impl Source {
    pub fn open(folder: &Path, ctx: &Ctx) -> Result<Source> {
        let parent = folder.parent().unwrap_or(Path::new("."));
        let man = jsonc::parse(&error::read_text(&parent.join("manifest.json"))?)?;
        let release = py::str_of(&man["platform"]);
        let big = release == "gc";
        let dname = folder.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default();
        let entry = man["entries"]
            .as_array()
            .and_then(|a| a.iter().find(|x| x["path"].as_str() == Some(dname.as_str())))
            .cloned()
            .ok_or_else(|| graph_err(format!("{dname} not in {}/manifest.json", parent.display())))?;
        let archive = ctx.archive_spelling(&parent.file_name().map(|n| n.to_string_lossy().into_owned()).unwrap_or_default());
        let name = pack_name(&entry);
        let packjson = jsonc::parse(&error::read_text(&folder.join("pack.json"))?)?;
        let header = packjson["header"].clone();
        let blob = error::read(&folder.join("data.bin"))?;
        let tab = |n: &str| -> Result<Vec<u8>> {
            let p = folder.join(n);
            if !p.exists() {
                return Ok(vec![]);
            }
            error::read(&p)
        };
        let b = tab("pointers.bin")?;
        let pointers: Vec<u32> = b.chunks_exact(4).map(|c| u32_be(c, 0, big)).collect();
        let mut tables = HashMap::new();
        let mut rel = HashMap::new();
        for k in ["texture", "sound", "ref", "shader"] {
            let b = tab(section_file(k))?;
            if b.len() % 8 != 0 {
                return Err(pack::short(8, b.len() / 8 * 8, b.len()));
            }
            let t: Vec<(u32, u32)> = b.chunks_exact(8).map(|c| (u32_be(c, 0, big), u32_be(c, 4, big))).collect();
            rel.insert(k, t.iter().copied().collect::<BTreeMap<_, _>>());
            tables.insert(k, t);
        }
        let mut ptr = BTreeMap::new();
        for &o in &pointers {
            ptr.insert(o, pack::u32_at(&blob, o, big)?);
        }
        Ok(Source { dir: folder.to_path_buf(), man, release, big, entry, archive, name, packjson, header, blob, pointers, tables, ptr, rel })
    }

    pub fn u32(&self, o: u32) -> Result<u32> {
        pack::u32_at(&self.blob, o, self.big)
    }

    pub fn base(&self, rest: &str) -> String {
        format!("base://{}/{}/{rest}", self.archive, self.name)
    }

    fn ptype(&self) -> String {
        py::str_of(&self.entry["type"])
    }

    /// xdl_struct's view of it (XsPack)
    fn xs_pack(&self) -> Pack {
        Pack::from_parts(
            &self.release,
            &self.archive,
            &self.name,
            self.blob.clone(),
            self.ptr.keys().copied().collect(),
            self.rel["texture"].clone(),
            self.rel["sound"].clone(),
            self.rel["ref"].clone(),
        )
    }

    fn hval(&self, k: &str) -> i64 {
        match self.header.get(k) {
            Some(Value::Number(n)) => n.as_i64().unwrap_or(0),
            Some(Value::Bool(b)) => *b as i64,
            _ => 0,
        }
    }
}

// ------------------------------------------------------------------------------------------
// extraction

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum NodeKind {
    Raw,
    Typed,
    Geometry,
}

#[derive(Debug, Clone)]
pub struct Node {
    pub start: u32,
    pub end: u32,
    pub kind: NodeKind,
    pub schema: Option<String>,
    pub label: Option<String>,
    /// geometry decoded (its names give the anchors)
    pub geom_ok: bool,
    pub gindex: usize,
    pub id: Option<String>,
    pub parent: Option<usize>,
    pub file: Option<String>,
    /// interior name -> offset in the node (insertion order)
    pub anchors: IndexMap<String, u32>,
    pub fixed: bool,
    pub count: Option<u32>,
    pub tac: Option<u32>,
}

impl Node {
    fn new(start: u32, end: u32) -> Node {
        Node {
            start,
            end,
            kind: NodeKind::Raw,
            schema: None,
            label: None,
            geom_ok: false,
            gindex: 0,
            id: None,
            parent: None,
            file: None,
            anchors: IndexMap::new(),
            fixed: false,
            count: None,
            tac: None,
        }
    }
    fn id(&self) -> &str {
        self.id.as_deref().unwrap_or("")
    }
}

/// offset -> (field path, link target type, x-link)
type SlotFields = IndexMap<u32, (String, String, Option<Value>)>;

pub struct Summary {
    pub nodes: usize,
    pub typed: usize,
    pub geometry: usize,
    pub raw: usize,
    pub files: usize,
    pub log: Vec<String>,
}

pub struct Extractor<'c> {
    pub ctx: &'c Ctx,
    pub src: Source,
    pub out: PathBuf,
    pub force: bool,
    pub log: Vec<String>,
    pub nodes: Vec<Node>,
    starts: Vec<u32>,
    by_start: HashMap<u32, usize>,
    tables_at: HashMap<u32, (String, u32)>,
    tac_at: BTreeMap<u32, (String, Option<u32>)>,
    slot_cache: RefCell<HashMap<String, Rc<SlotFields>>>,
    kinds: HashSet<String>,
    tac_docs: HashMap<u32, Value>,
    tac_pads: BTreeMap<u32, u32>,
    kids: HashMap<u32, Map<String, Value>>,
    tacnodes: Vec<TacNode>,
    xp: Pack,
    slot_kind: HashMap<u32, &'static str>,
}

impl<'c> Extractor<'c> {
    pub fn new(ctx: &'c Ctx, src: Source, out: &Path, force: bool) -> Extractor<'c> {
        let xp = src.xs_pack();
        let mut slot_kind = HashMap::new();
        for &o in src.ptr.keys() {
            slot_kind.insert(o, "pointer");
        }
        for (k, n) in [("texture", "texture"), ("ref", "ref"), ("sound", "sound")] {
            for &o in src.rel[k].keys() {
                slot_kind.entry(o).or_insert(n);
            }
        }
        Extractor {
            ctx,
            src,
            out: out.to_path_buf(),
            force,
            log: vec![],
            nodes: vec![],
            starts: vec![],
            by_start: HashMap::new(),
            tables_at: HashMap::new(),
            tac_at: BTreeMap::new(),
            slot_cache: RefCell::new(HashMap::new()),
            kinds: walk::KINDS.iter().map(|s| s.to_string()).collect(),
            tac_docs: HashMap::new(),
            tac_pads: BTreeMap::new(),
            kids: HashMap::new(),
            tacnodes: vec![],
            xp,
            slot_kind,
        }
    }

    fn s(&self) -> &'c Schemas {
        &self.ctx.s
    }

    // -- typing --------------------------------------------------------------------------

    fn labels(&mut self) -> BTreeMap<u32, String> {
        match walk::label_pack(&self.src.dir, self.src.big, &self.src.ptype()) {
            Ok(l) => l,
            Err(e) => {
                self.log.push(format!("walkers failed: {e}"));
                BTreeMap::new()
            }
        }
    }

    /// [(offset, decoded geometry or None)]
    fn find_geometry(&mut self) -> Result<Vec<(u32, Option<geom::Geom>)>> {
        let ptrs: HashSet<u32> = self.src.ptr.keys().copied().collect();
        let blob = geom::Blob::new(&self.src.blob, &ptrs, &self.src.release);
        let mut out = vec![];
        for (i, off) in geom::scan(&blob).into_iter().enumerate() {
            match geom::decode(&blob, off) {
                Ok(g) => out.push((off, Some(g))),
                Err(e) if e.kind == Kind::Uncaught => return Err(e),
                Err(e) => {
                    self.log.push(format!("geometry {i} @0x{off:X} not decoded ({e}): kept as raw bytes"));
                    out.push((off, None));
                }
            }
        }
        Ok(out)
    }

    fn item_size(&self, sname: &str) -> Result<u32> {
        if sname.starts_with("PTR32(") {
            return Ok(4);
        }
        let sch = self.s().struct_(sname).ok_or_else(|| Error::uncaught("'NoneType' object is not subscriptable"))?;
        Ok(schema::size(sch)? as u32)
    }

    fn count_of(&self, sname: &str, base: u32, field: &str) -> Result<Option<u32>> {
        let Some(p) = self.s().struct_(sname).and_then(|s| s["properties"].get(field)) else { return Ok(None) };
        let ct = p.get("x-c-type").and_then(Value::as_str);
        if ct != Some("u32") && ct != Some("s32") {
            return Ok(None);
        }
        let n = self.src.u32(base.wrapping_add(schema::offset(p)?))?;
        Ok(if n < 0x10000 { Some(n) } else { None })
    }

    fn type_objects(&mut self, claims: &mut Vec<(u32, u32)>, labels: &BTreeMap<u32, String>) -> Result<BTreeMap<u32, (String, Option<u32>)>> {
        let s = self.s();
        let mut queue: VecDeque<(u32, String, Option<u32>)> = VecDeque::new();
        let ptype = self.src.ptype();
        let rk = root_kind(&ptype);
        if ptype == "world" && s.struct_("World").is_some() && self.src.blob.len() >= 0xA0 {
            queue.push_back((0, "World".into(), None));
        }
        if let Some(rk) = rk
            && s.struct_(rk).is_some()
        {
            for o in self.root_entries()?.into_iter().flatten() {
                queue.push_back((o, rk.into(), None));
            }
        }
        for (o, lab) in labels {
            if let Some(sn) = label_schema(lab)
                && s.struct_(sn).is_some()
            {
                queue.push_back((*o, sn.into(), None));
            }
        }
        let mut typed: BTreeMap<u32, (String, Option<u32>)> = BTreeMap::new();
        let mut seen: HashSet<(u32, String)> = HashSet::new();
        let blen = self.src.blob.len() as u64;
        while let Some((off, sname, count)) = queue.pop_front() {
            if seen.contains(&(off, sname.clone())) || typed.contains_key(&off) {
                continue;
            }
            seen.insert((off, sname.clone()));
            let isz = self.item_size(&sname)?;
            let size = isz as u64 * count.unwrap_or(1) as u64;
            if count == Some(0) {
                continue;
            }
            let end = off as u64 + size;
            if end > blen || claims.iter().any(|&(a, b)| (a as u64) < end && off < b) {
                continue;
            }
            let mut ok = true;
            for i in 0..count.unwrap_or(1) {
                match self.check_item(&sname, off + i * isz) {
                    Ok(()) => {}
                    Err(e) if e.kind == Kind::Xdl || e.kind == Kind::Struct => {
                        ok = false;
                        break;
                    }
                    Err(e) => return Err(e),
                }
            }
            if !ok {
                continue;
            }
            typed.insert(off, (sname.clone(), count));
            claims.push((off, end as u32));
            let fields = self.slot_fields(&sname, count)?;
            for (rel_off, (_path, target, x)) in fields.iter() {
                let Some(&t) = self.src.ptr.get(&(off.wrapping_add(*rel_off))) else { continue };
                let xo = x.as_ref();
                let target = xo
                    .and_then(|x| x.get("item"))
                    .filter(|v| !matches!(v, Value::Null | Value::Bool(false)) && v.as_str() != Some(""))
                    .map(py::str_of)
                    .unwrap_or_else(|| target.clone());
                let cnt = xo.and_then(|x| x.get("count")).filter(|v| !matches!(v, Value::Null | Value::Bool(false)) && v.as_str() != Some(""));
                let topish = |t: &str| s.struct_(t).is_some() && s.top(t).is_some();
                if let Some(cf) = cnt {
                    let base = if count.is_some() { off + rel_off - rel_off % isz } else { off };
                    let n = self.count_of(&sname, base, &py::str_of(cf))?;
                    if let Some(n) = n
                        && (target.starts_with("PTR32(") || topish(&target))
                    {
                        queue.push_back((t, target, Some(n)));
                    }
                } else if topish(&target) {
                    queue.push_back((t, target, None));
                }
            }
        }
        Ok(typed)
    }

    fn check_item(&self, sname: &str, off: u32) -> Result<()> {
        if sname.starts_with("PTR32(") {
            match self.slot_kind.get(&off) {
                None => {
                    if self.src.u32(off)? != 0 {
                        return Err(Error::xdl("link without relocation"));
                    }
                }
                Some(&k) if k != "pointer" => return Err(Error::xdl(format!("a pointer link on a {k} slot"))),
                _ => {}
            }
            return Ok(());
        }
        let sch = self.s().struct_(sname).ok_or_else(|| Error::uncaught("'NoneType' object is not subscriptable"))?;
        self.check_typed(sch, off)
    }

    fn check_typed(&self, sch: &Value, off: u32) -> Result<()> {
        for (_, p) in schema::fields(sch) {
            self.check_value(p, off.wrapping_add(schema::offset(p)?), schema::ctype(p))?;
        }
        Ok(())
    }

    fn check_value(&self, p: &Value, off: u32, ctype: &str) -> Result<()> {
        let (base, dims) = schema::parse_ctype(ctype);
        if !dims.is_empty() {
            let sub = schema::sub_ctype(&base, &dims);
            let esz = self.s().size_of(&sub)? as u32;
            for i in 0..dims[0] as u32 {
                self.check_value(&p["items"], off.wrapping_add(i * esz), &sub)?;
            }
            return Ok(());
        }
        if let Some(x) = p.get("x-link") {
            let kind = self.slot_kind.get(&off).copied();
            let want = match x["kind"].as_str() {
                Some("object") => "pointer",
                Some("texture") => "texture",
                Some("geometry") => "ref",
                other => return Err(Error::uncaught(format!("KeyError: {}", py::repr(&json!(other))))),
            };
            match kind {
                None => {
                    if self.src.u32(off)? != 0 {
                        return Err(Error::xdl("link without relocation"));
                    }
                }
                Some(k) if k != want => return Err(Error::xdl(format!("a {want} link on a {k} slot"))),
                Some("ref") => {
                    if self.src.rel["ref"][&off] >> 16 != 0 {
                        return Err(Error::xdl("geometry from a share pack"));
                    }
                }
                _ => {}
            }
            return Ok(());
        }
        if p.get("$ref").is_some() {
            let sch = self.s().ref_(p).ok_or_else(|| Error::uncaught("'NoneType' object is not subscriptable"))?;
            for (_, q) in schema::fields(sch) {
                self.check_value(q, off.wrapping_add(schema::offset(q)?), schema::ctype(q))?;
            }
            return Ok(());
        }
        let pr = schema::prim(&base).ok_or_else(|| Error::uncaught(format!("KeyError: {}", py::str_repr(&base))))?;
        let mut o = off - off % 4;
        while (o as u64) < off as u64 + pr.size as u64 {
            if let Some(k) = self.slot_kind.get(&o)
                && o + 4 > off
            {
                return Err(Error::xdl(format!("scalar on a {k} slot")));
            }
            o += 4;
        }
        Ok(())
    }

    /// {offset: (field path, link target type, x-link)} of a struct's link fields; of each
    /// element ("3/info") for an array of `count`.
    fn slot_fields(&self, sname: &str, count: Option<u32>) -> Result<Rc<SlotFields>> {
        if let Some(c) = count {
            let one = self.slot_fields(sname, None)?;
            let size = self.item_size(sname)?;
            let mut out = SlotFields::new();
            for i in 0..c {
                for (o, (p, t, x)) in one.iter() {
                    out.insert(i * size + o, (format!("{i}/{p}"), t.clone(), x.clone()));
                }
            }
            return Ok(Rc::new(out));
        }
        if let Some(inner) = sname.strip_prefix("PTR32(").and_then(|r| r.strip_suffix(')')) {
            let mut out = SlotFields::new();
            out.insert(0, ("to".into(), strip_struct(inner).into(), None));
            return Ok(Rc::new(out));
        }
        if let Some(c) = self.slot_cache.borrow().get(sname) {
            return Ok(c.clone());
        }
        let s = self.s();
        let mut out = SlotFields::new();
        fn walk_(s: &Schemas, p: &Value, off: u32, path: &mut Vec<String>, ctype: &str, out: &mut SlotFields) -> Result<()> {
            let (base, dims) = schema::parse_ctype(ctype);
            if !dims.is_empty() {
                let sub = schema::sub_ctype(&base, &dims);
                let esz = s.size_of(&sub)? as u32;
                for i in 0..dims[0] as u32 {
                    path.push(i.to_string());
                    walk_(s, &p["items"], off + i * esz, path, &sub, out)?;
                    path.pop();
                }
                return Ok(());
            }
            if let Some(x) = p.get("x-link") {
                let t = x.get("target").filter(|v| !v.is_null() && v.as_str() != Some("")).map(py::str_of).unwrap_or_default();
                out.insert(off, (path.join("/"), strip_struct(&t).to_string(), Some(x.clone())));
                return Ok(());
            }
            if p.get("$ref").is_some() {
                let sch = s.ref_(p).ok_or_else(|| Error::uncaught("'NoneType' object is not subscriptable"))?;
                for (key, q) in schema::fields(sch) {
                    path.push(key.clone());
                    walk_(s, q, off + schema::offset(q)?, path, schema::ctype(q), out)?;
                    path.pop();
                }
            }
            Ok(())
        }
        let sch = s.struct_(sname).ok_or_else(|| Error::uncaught("'NoneType' object is not subscriptable"))?;
        for (key, p) in schema::fields(sch) {
            let mut path = vec![key.clone()];
            walk_(s, p, schema::offset(p)?, &mut path, schema::ctype(p), &mut out)?;
        }
        let rc = Rc::new(out);
        self.slot_cache.borrow_mut().insert(sname.into(), rc.clone());
        Ok(rc)
    }

    fn root_entries(&self) -> Result<Vec<Option<u32>>> {
        if root_kind(&self.src.ptype()).is_none() || self.src.blob.len() < 8 {
            return Ok(vec![]);
        }
        let n = self.src.u32(0)?;
        let Some(&tab) = self.src.ptr.get(&4) else { return Ok(vec![]) };
        Ok((0..n.min(0x1000)).map(|i| self.src.ptr.get(&tab.wrapping_add(8 * i)).copied()).collect())
    }

    // -- nodes ---------------------------------------------------------------------------

    pub fn cut(&mut self) -> Result<()> {
        let n = self.src.blob.len() as u32;
        let geoms = self.find_geometry()?;
        let labels = self.labels();
        let mut claims: Vec<(u32, u32)> = vec![];
        let mut gclaims: HashMap<u32, (usize, Option<geom::Geom>)> = HashMap::new();
        for (i, (off, g)) in geoms.into_iter().enumerate() {
            let mut hi = off + 0x40;
            if let Some(g) = &g {
                hi = g.owned.iter().map(|x| x.1).max().unwrap_or(hi);
            }
            claims.push((off, hi));
            gclaims.insert(off, (i, g));
        }
        let typed = self.type_objects(&mut claims, &labels)?;
        let src = &self.src;
        let u32f = |o: u32| src.u32(o);
        self.tac_at = tac::tac_shapes(&u32f, &src.ptr, &src.header, n)?;
        let mut cuts: BTreeSet<u32> = BTreeSet::new();
        cuts.insert(0);
        cuts.insert(n);
        cuts.extend(src.ptr.values().copied().filter(|&t| t <= n));
        for (&o, &v) in &src.rel["ref"] {
            if v >> 16 == 0 {
                cuts.insert(src.u32(o)?);
            }
        }
        cuts.extend(gclaims.keys().copied());
        cuts.extend(typed.keys().copied());
        cuts.extend(labels.keys().copied().filter(|&o| o < n));
        let mut tables_at: HashMap<u32, (String, u32)> = HashMap::new();
        let h = |k: &str| src.hval(k);
        for (key, cnt) in HEADER_OFFSETS {
            let v = h(key);
            if h(cnt) != 0 && 0 < v && v < n as i64 {
                cuts.insert(v as u32);
            }
        }
        let tdo = h("texture_data_offset");
        if h("num_textures") != 0 && 0 < tdo && tdo < n as i64 {
            let stride = if src.release == "gc" { 0x20 } else { 0x10 };
            for i in 0..h("num_textures") {
                let o = tdo + stride * i;
                if o < n as i64 {
                    cuts.insert(o as u32);
                    tables_at.insert(o as u32, ("texture".into(), i as u32));
                }
            }
        }
        let so = h("shader_offset");
        if h("num_shaders") != 0 && 0 < so && so < n as i64 {
            for i in 0..h("num_shaders") {
                let o = so + 0xC * i;
                if o < n as i64 {
                    cuts.insert(o as u32);
                    tables_at.entry(o as u32).or_insert(("shader".into(), i as u32));
                }
            }
        }
        let to = h("trigger_offset");
        if h("num_triggers") != 0 && 0 < to && to < n as i64 {
            for i in 0..h("num_triggers") {
                let o = to + 0x2C * i;
                if o < n as i64 {
                    cuts.insert(o as u32);
                    tables_at.entry(o as u32).or_insert(("trigger".into(), i as u32));
                }
            }
        }
        let po = h("palette_offset");
        if h("num_palettes") != 0 && 0 < po && po < n as i64 {
            tables_at.entry(po as u32).or_insert(("palette".into(), 0));
        }
        let mut inside = claims.clone();
        inside.sort();
        let mut keep = vec![];
        let mut j = 0;
        for c in cuts {
            while j < inside.len() && inside[j].1 <= c {
                j += 1;
            }
            if j < inside.len() && inside[j].0 < c && c < inside[j].1 {
                continue;
            }
            keep.push(c);
        }
        let mut nodes = vec![];
        for w in keep.windows(2) {
            let (a, b) = (w[0], w[1]);
            let mut nd = Node::new(a, b);
            if let Some((i, g)) = gclaims.get(&a) {
                nd.kind = NodeKind::Geometry;
                nd.gindex = *i;
                nd.geom_ok = g.is_some();
                if let Some(g) = g {
                    for (o, nm) in &g.names {
                        if a <= *o && *o < b && !nm.is_empty() {
                            nd.anchors.insert(nm.clone(), o - a);
                        }
                    }
                }
            } else if let Some((sn, c)) = typed.get(&a) {
                nd.kind = NodeKind::Typed;
                nd.schema = Some(sn.clone());
                nd.count = *c;
                if let Some(c) = c {
                    let sz = self.item_size(sn)?;
                    for i in 0..*c {
                        nd.anchors.insert(i.to_string(), i * sz);
                    }
                }
            }
            nd.label = labels.get(&a).cloned();
            nodes.push(nd);
        }
        if nodes.is_empty() {
            nodes.push(Node::new(0, 0));
        }
        self.starts = nodes.iter().map(|n| n.start).collect();
        self.by_start = nodes.iter().enumerate().map(|(i, n)| (n.start, i)).collect();
        self.nodes = nodes;
        self.tables_at = tables_at;
        Ok(())
    }

    pub fn node_at(&self, off: u32) -> Option<usize> {
        let i = self.starts.partition_point(|&s| s <= off);
        if i == 0 {
            return None;
        }
        let i = i - 1;
        let nd = &self.nodes[i];
        if off >= nd.end && !(off == nd.end && nd.end as usize == self.src.blob.len()) {
            return None;
        }
        Some(i)
    }

    // -- ids -----------------------------------------------------------------------------

    pub fn name(&mut self) -> Result<()> {
        let mut fixed = vec![];
        let (rel, arch, pname) = (self.src.release.clone(), self.src.archive.clone(), self.src.name.clone());
        for i in 0..self.nodes.len() {
            let nd = &self.nodes[i];
            if nd.kind == NodeKind::Geometry {
                let id = match self.ctx.pairing.id(&rel, &arch, &pname, "geometry", nd.gindex as i64) {
                    Ok(s) => s,
                    Err(e) if e.kind == Kind::Xdl => self.src.base(&format!("geometry/{rel}:{}", nd.gindex)),
                    Err(e) => return Err(e),
                };
                self.nodes[i].id = Some(id);
                fixed.push(i);
            } else if let Some((kind, k)) = self.tables_at.get(&nd.start) {
                let id = if kind == "texture" {
                    asset_link(self.ctx, &rel, &arch, &pname, "texture", *k)?
                } else {
                    self.src.base(&format!("{kind}/{k}"))
                };
                self.nodes[i].id = Some(id);
                fixed.push(i);
            }
        }
        let mut roots = vec![];
        if let Some(rk) = root_kind(&self.src.ptype()) {
            for (i, o) in self.root_entries()?.into_iter().enumerate() {
                let Some(o) = o else { continue };
                if let Some(&h) = self.by_start.get(&o)
                    && self.nodes[h].id.is_none()
                {
                    self.nodes[h].id = Some(self.src.base(&format!("{rk}/{i}")));
                    roots.push(h);
                }
            }
        }
        let world = self.src.ptype() == "world";
        if self.nodes[0].id.is_none() {
            self.nodes[0].id = Some(self.src.base(if world { "World/0" } else { "header/0" }));
        }
        for &h in roots.iter().chain(fixed.iter()).chain([0usize].iter()) {
            self.nodes[h].fixed = true;
        }
        let mut children: HashMap<u32, Vec<(u32, String, usize)>> = HashMap::new();
        for i in 0..self.nodes.len() {
            for (rel_off, seg, t) in self.out_slots(i)? {
                if let Some(c) = self.node_at(t)
                    && self.nodes[c].start == t
                    && c != i
                {
                    children.entry(self.nodes[i].start).or_default().push((rel_off, seg, c));
                }
            }
        }
        let mut paths: HashMap<u32, Vec<String>> = HashMap::new();
        for nd in &self.nodes {
            if let Some(id) = &nd.id {
                paths.insert(nd.start, id_path(id).split('/').map(str::to_string).collect());
            }
        }
        let mut seeds = vec![];
        if world {
            seeds.push(0);
            seeds.extend(&roots);
            seeds.extend(&fixed);
        } else {
            seeds.extend(&roots);
            seeds.extend(&fixed);
            seeds.push(0);
        }
        for s in seeds {
            self.bfs(s, &children, &mut paths);
        }
        let mut k = 0;
        for i in 0..self.nodes.len() {
            if self.nodes[i].id.is_none() {
                self.nodes[i].id = Some(self.src.base(&format!("orphan/{k}")));
                self.nodes[i].fixed = true;
                paths.insert(self.nodes[i].start, vec!["orphan".into(), k.to_string()]);
                k += 1;
                self.bfs(i, &children, &mut paths);
            }
        }
        let mut seen = HashSet::new();
        let mut dup = vec![];
        for nd in &self.nodes {
            if !seen.insert(nd.id()) && !dup.contains(&nd.id()) {
                dup.push(nd.id());
            }
        }
        if !dup.is_empty() {
            let d: Vec<String> = dup.iter().take(3).map(|s| py::str_repr(s)).collect();
            return Err(graph_err(format!("duplicate ids: [{}]", d.join(", "))));
        }
        Ok(())
    }

    fn bfs(&mut self, seed: usize, children: &HashMap<u32, Vec<(u32, String, usize)>>, paths: &mut HashMap<u32, Vec<String>>) {
        let mut q = VecDeque::from([seed]);
        while let Some(h) = q.pop_front() {
            let start = self.nodes[h].start;
            let Some(ch) = children.get(&start) else { continue };
            let mut ch: Vec<&(u32, String, usize)> = ch.iter().collect();
            ch.sort_by_key(|x| x.0);
            for (_, seg, c) in ch {
                if self.nodes[*c].id.is_some() {
                    continue;
                }
                let path = add_step(&paths[&start], seg);
                let id = self.src.base(&path.join("/"));
                paths.insert(self.nodes[*c].start, path);
                self.nodes[*c].id = Some(id);
                self.nodes[*c].parent = Some(h);
                q.push_back(*c);
            }
        }
    }

    /// [(offset in node, step name, target)] of the node's pointer slots, offset order.
    fn out_slots(&self, h: usize) -> Result<Vec<(u32, String, u32)>> {
        let nd = &self.nodes[h];
        let fields = if nd.kind == NodeKind::Typed { Some(self.slot_fields(nd.schema.as_deref().unwrap_or(""), nd.count)?) } else { None };
        let mut out = vec![];
        let lo = nd.start - nd.start % 4;
        for (&o, &t) in self.src.ptr.range(lo..nd.end.max(lo)) {
            if o < nd.start || (o - lo) % 4 != 0 {
                continue;
            }
            let r = o - nd.start;
            let mut seg = fields.as_ref().and_then(|f| f.get(&r)).map(|x| x.0.clone()).unwrap_or_else(|| format!("p{r:x}"));
            if nd.kind == NodeKind::Raw
                && let Some((role, cnt)) = self.tac_at.get(&nd.start)
                && let Some(st) = tac::step_of(role, *cnt, r)
            {
                seg = st;
            }
            out.push((r, seg, t));
        }
        Ok(out)
    }

    /// The link text of offset t; elem: an element of a typed array is named by its index.
    pub fn link_to(&self, t: u32, elem: bool) -> Result<String> {
        let h = self.node_at(t).ok_or_else(|| graph_err(format!("pointer to 0x{t:X} outside every node")))?;
        let nd = &self.nodes[h];
        let d = t - nd.start;
        if d == 0 && !(elem && nd.count.is_some()) {
            return Ok(nd.id().to_string());
        }
        for (nm, &o) in &nd.anchors {
            if o == d {
                return Ok(format!("{}/{nm}", nd.id()));
            }
        }
        Ok(format!("{}/o{d:x}", nd.id()))
    }

    // -- records -------------------------------------------------------------------------

    /// bytes (pointer and local-ref slots zeroed) and links of a node; `skip`: slot offsets
    /// (node-relative) that the typed JSON carries.
    fn raw_record(&self, h: usize, skip: &HashSet<u32>) -> Result<(Map<String, Value>, Vec<u8>)> {
        let nd = &self.nodes[h];
        let src = &self.src;
        let bl = src.blob.len();
        let mut b = src.blob[(nd.start as usize).min(bl)..(nd.end as usize).min(bl).max((nd.start as usize).min(bl))].to_vec();
        let mut rec = Map::new();
        rec.insert("size".into(), json!(nd.end - nd.start));
        let (mut links, mut tex, mut snd, mut refs, mut shd) = (Map::new(), Map::new(), Map::new(), Map::new(), Map::new());
        let mut o = nd.start - nd.start % 4;
        while o < nd.end {
            if o < nd.start {
                o += 4;
                continue;
            }
            let r = o - nd.start;
            if skip.contains(&r) {
                o += 4;
                continue;
            }
            let ru = r as usize;
            if let Some(&t) = src.ptr.get(&o) {
                links.insert(hx(r), Value::String(self.link_to(t, false)?));
                zero_slice(&mut b, ru, ru + 4);
            }
            if let Some(&v) = src.rel["ref"].get(&o) {
                if v >> 16 == 0 {
                    links.insert(hx(r), Value::String(self.link_to(src.u32(o)?, false)?));
                    refs.insert(hx(r), json!("local"));
                    zero_slice(&mut b, ru, ru + 4);
                } else {
                    refs.insert(hx(r), Value::String(export_link(&src.archive, &src.name, v)));
                }
            }
            if let Some(&v) = src.rel["texture"].get(&o) {
                tex.insert(hx(r), Value::String(asset_link(self.ctx, &src.release, &src.archive, &src.name, "texture", v)?));
            }
            if let Some(&v) = src.rel["sound"].get(&o) {
                snd.insert(hx(r), Value::String(asset_link(self.ctx, &src.release, &src.archive, &src.name, "sound", v)?));
            }
            if let Some(&v) = src.rel["shader"].get(&o) {
                shd.insert(hx(r), json!(v));
            }
            o += 4;
        }
        for (k, v) in [("links", links), ("textures", tex), ("sounds", snd), ("refs", refs), ("shaders", shd)] {
            if !v.is_empty() {
                rec.insert(k.into(), Value::Object(v));
            }
        }
        Ok((rec, b))
    }

    pub fn run(&mut self) -> Result<Summary> {
        let out = self.out.clone();
        if out.exists() && !out.is_dir() {
            return Err(graph_err(format!("{}: not a folder", out.display())));
        }
        if out.exists()
            && !self.force
            && !out.join("pack.jsonc").exists()
            && std::fs::read_dir(&out).map(|mut d| d.next().is_some()).unwrap_or(false)
        {
            return Err(graph_err(format!(
                "{}: not empty and not a previous extract (no pack.jsonc); extracting replaces the folder (--force to do it anyway)",
                out.display()
            )));
        }
        self.cut()?;
        self.name()?;
        if out.exists() {
            std::fs::remove_dir_all(&out).map_err(|e| Error::io(format!("{}: {e}", out.display())))?;
        }
        std::fs::create_dir_all(&out).map_err(|e| Error::io(format!("{}: {e}", out.display())))?;
        for i in 0..self.nodes.len() {
            let nd = &self.nodes[i];
            let own = nd.fixed
                || nd.kind != NodeKind::Raw
                || nd.label.as_ref().is_some_and(|l| self.kinds.contains(l))
                || nd.end - nd.start >= OWN_FILE_MIN
                || nd.parent.is_none();
            if own {
                self.nodes[i].file = Some(file_stem(&id_path(nd.id())));
            }
        }
        self.compose_triggers()?;
        self.write()
    }

    fn compose_triggers(&mut self) -> Result<()> {
        if self.tac_at.is_empty() || self.s().struct_("Trigger").is_none() {
            return Ok(());
        }
        let t = Tac::from_schemas(self.s())?;
        self.tacnodes = self
            .nodes
            .iter()
            .map(|n| TacNode { start: n.start, end: n.end, parent: n.parent, raw: n.kind == NodeKind::Raw, fixed: n.fixed, label: n.label.clone() })
            .collect();
        let tsize = t.size["Trigger"];
        for i in 0..self.nodes.len() {
            let nd = &self.nodes[i];
            if self.tables_at.get(&nd.start).map(|x| x.0.as_str()) != Some("trigger") || nd.kind != NodeKind::Raw {
                continue;
            }
            let (parts, pads, doc) = {
                let mut x = tac::Extract::new(&t, self);
                if (nd.end - nd.start) < tsize || !x.zero_tail(i, tsize) {
                    continue;
                }
                match x.trigger(i) {
                    Ok(doc) => (x.parts.clone(), x.pads.clone(), doc),
                    Err(e) if e.kind == Kind::Tac || e.kind == Kind::Struct => continue,
                    Err(e) => return Err(e),
                }
            };
            let (file, start) = (self.nodes[i].file.clone(), self.nodes[i].start);
            for p in parts {
                self.nodes[p].file = file.clone();
                self.nodes[p].tac = Some(start);
            }
            self.tac_docs.insert(start, doc);
            self.tac_pads.extend(pads);
        }
        Ok(())
    }

    fn home(&self, mut h: usize) -> usize {
        while self.nodes[h].file.is_none() {
            h = self.nodes[h].parent.expect("a node without a file has a parent");
        }
        h
    }

    fn write(&mut self) -> Result<Summary> {
        let mut layout = vec![];
        let none = HashSet::new();
        for i in 0..self.nodes.len() {
            let home = self.home(i);
            let nd = &self.nodes[i];
            let mut ent = Map::new();
            ent.insert("id".into(), json!(nd.id()));
            ent.insert("file".into(), json!(self.nodes[home].file.clone().unwrap_or_default()));
            ent.insert("align".into(), json!(align_of(nd.start)));
            if let Some(p) = self.tac_pads.get(&nd.start) {
                ent.insert("pad".into(), json!(p));
            }
            if home == i
                && let Some(l) = &nd.label
            {
                ent.insert("type".into(), json!(l));
            }
            layout.push(Value::Object(ent));
            if home != i {
                let (rec, b) = self.raw_record(i, &none)?;
                let nd = &self.nodes[i];
                let mut r = Map::new();
                r.insert("id".into(), json!(nd.id()));
                if let Some(l) = &nd.label {
                    r.insert("type".into(), json!(l));
                }
                r.extend(rec);
                self.put_bytes(&mut r, &b, i)?;
                let hs = self.nodes[home].start;
                self.kids.entry(hs).or_default().insert(nd.id().to_string(), Value::Object(r));
            }
        }
        for i in 0..self.nodes.len() {
            let nd = &self.nodes[i];
            let Some(file) = nd.file.clone() else { continue };
            if let Some(t) = nd.tac {
                if t == nd.start {
                    self.write_trigger(i)?;
                }
                continue;
            }
            match nd.kind {
                NodeKind::Geometry => self.write_geometry(i)?,
                NodeKind::Typed => self.write_typed(i)?,
                NodeKind::Raw => {
                    let (rec, b) = self.raw_record(i, &none)?;
                    let nd = &self.nodes[i];
                    let mut doc = Map::new();
                    doc.insert("format_version".into(), json!(FORMAT));
                    doc.insert("id".into(), json!(nd.id()));
                    if let Some(l) = &nd.label {
                        doc.insert("type".into(), json!(l));
                    }
                    doc.extend(rec);
                    self.put_bytes(&mut doc, &b, i)?;
                    if let Some(k) = self.kids.get(&nd.start).filter(|k| !k.is_empty()) {
                        doc.insert("children".into(), Value::Object(k.clone()));
                    }
                    self.write_doc(
                        &format!("{file}.jsonc"),
                        &Value::Object(doc),
                        &["raw object (no schema yet): bytes as stored, pointer slots zeroed; links by slot offset".into()],
                        None,
                    )?;
                }
            }
        }
        self.write_pack(layout)?;
        let c = |k: NodeKind| self.nodes.iter().filter(|n| n.kind == k).count();
        Ok(Summary {
            nodes: self.nodes.len(),
            typed: c(NodeKind::Typed),
            geometry: c(NodeKind::Geometry),
            raw: c(NodeKind::Raw),
            files: self.nodes.iter().filter(|n| n.file.is_some()).count(),
            log: self.log.clone(),
        })
    }

    fn schema_rel(&self, sname: &str, p: &Path) -> Result<String> {
        let sp = self.s().path(sname).ok_or_else(|| Error::uncaught(format!("no schema file for {sname}")))?;
        let a = std::fs::canonicalize(sp).map_err(|e| Error::io(format!("{}: {e}", sp.display())))?;
        let par = p.parent().unwrap_or(Path::new("."));
        let b = std::fs::canonicalize(par).map_err(|e| Error::io(format!("{}: {e}", par.display())))?;
        Ok(crate::ops::relpath(&a, &b))
    }

    fn write_trigger(&mut self, h: usize) -> Result<()> {
        let nd = &self.nodes[h];
        let file = nd.file.clone().unwrap();
        let p = self.out.join(format!("{file}.jsonc"));
        std::fs::create_dir_all(p.parent().unwrap()).map_err(|e| Error::io(e.to_string()))?;
        let mut doc = Map::new();
        doc.insert("$schema".into(), json!(self.schema_rel("Trigger", &p)?));
        doc.insert("format_version".into(), json!(FORMAT));
        doc.insert("id".into(), json!(nd.id()));
        if let Some(Value::Object(m)) = self.tac_docs.get(&nd.start) {
            for (k, v) in m {
                doc.insert(k.clone(), v.clone());
            }
        }
        let mut kids = Map::new();
        for q in self.nodes.iter().filter(|q| q.tac == Some(nd.start)) {
            if let Some(k) = self.kids.get(&q.start) {
                for (a, b) in k {
                    kids.insert(a.clone(), b.clone());
                }
            }
        }
        if !kids.is_empty() {
            doc.insert("children".into(), Value::Object(kids));
        }
        let head = format!(
            "Trigger {} ({}): a TAC trigger with its conditions, actions and parameter lists (Trigger.h; tools/xdl_tac.py)",
            nd.id(),
            self.src.release
        );
        self.write_doc(&format!("{file}.jsonc"), &Value::Object(doc), &[head], None)
    }

    fn put_bytes(&self, rec: &mut Map<String, Value>, b: &[u8], h: usize) -> Result<()> {
        if b.len() <= INLINE_MAX {
            rec.insert("bytes".into(), json!(hex_lines(b)));
        } else {
            let stem = file_stem(&id_path(self.nodes[h].id()));
            rec.insert("bin".into(), json!(format!("{stem}.bin")));
            write_bytes(&self.out.join(format!("{stem}.bin")), b)?;
        }
        Ok(())
    }

    fn write_doc(&self, rel: &str, doc: &Value, header: &[String], sch: Option<&Value>) -> Result<()> {
        let text = match sch {
            Some(s) => jsonc::to_jsonc(doc, s, header),
            None => {
                let mut t: String = header.iter().map(|h| format!("// {h}\n")).collect();
                t.push_str(&jsonc::dump_value(doc, 0, None));
                t.push('\n');
                t
            }
        };
        write_text(&self.out.join(rel), &text)
    }

    fn write_typed(&mut self, h: usize) -> Result<()> {
        let nd = self.nodes[h].clone();
        let sname = nd.schema.clone().unwrap_or_default();
        let arr = nd.count.is_some();
        let stem = nd.file.clone().unwrap_or_else(|| file_stem(&id_path(nd.id())));
        let p = self.out.join(format!("{stem}.jsonc"));
        std::fs::create_dir_all(p.parent().unwrap()).map_err(|e| Error::io(e.to_string()))?;
        let size = self.item_size(&sname)?;
        let n = nd.count.unwrap_or(1);
        let ptrs = sname.starts_with("PTR32(");
        let s = self.s();
        let sch = if ptrs { None } else { s.struct_(&sname) };
        let namer = Namer { ex: self };
        let dec = Decoder::new(s, &self.xp, Some(&self.ctx.pairing)).with_namer(&namer);
        let id = nd.id().to_string();
        if ptrs {
            let x = json!({"kind": "object"});
            let mut items = vec![];
            for i in 0..n {
                items.push(dec.link(&x, nd.start + i * 4, &format!("{id}/{i}"))?);
            }
            let doc = json!({"format_version": 1, "extends": id, "of": sname, "items": items});
            self.write_doc(
                &format!("{stem}.jsonc"),
                &doc,
                &[format!("{n} links ({sname}) {id} ({}), from tools/xdl_graph.py", self.src.release)],
                None,
            )?;
        } else {
            let sch = sch.ok_or_else(|| Error::uncaught("'NoneType' object is not subscriptable"))?;
            let mut doc = Map::new();
            doc.insert("$schema".into(), json!(self.schema_rel(&sname, &p)?));
            doc.insert("format_version".into(), json!(1));
            doc.insert("extends".into(), json!(id));
            if arr {
                let mut items = vec![];
                for i in 0..n {
                    items.push(Value::Object(dec.struct_(sch, nd.start + i * size, &format!("{id}/{i}"))?));
                }
                doc.insert("items".into(), Value::Array(items));
            } else {
                for (k, v) in dec.struct_(sch, nd.start, &id)? {
                    doc.insert(k, v);
                }
            }
            let what = if arr { format!("{n} x {sname}") } else { sname.clone() };
            let mut head = vec![format!("{what} {id} ({}), from tools/xdl_graph.py", self.src.release)];
            if arr {
                head.push("an array: `items` holds one object per element (the schema's)".into());
            }
            self.write_doc(&format!("{stem}.jsonc"), &Value::Object(doc), &head, if arr { None } else { Some(sch) })?;
        }
        // the base: what the JSON leaves out
        let js: HashSet<u32> = self.slot_fields(&sname, nd.count)?.keys().copied().collect();
        let (mut rec, mut b) = self.raw_record(h, &js)?;
        for i in 0..n {
            let o = (i * size) as usize;
            if ptrs {
                zero_slice(&mut b, o, o + 4);
                continue;
            }
            for (_, q) in schema::fields(sch.unwrap()) {
                let lo = o + schema::offset(q)? as usize;
                let hi = lo + s.size_of(schema::ctype(q))?;
                let lf = schema::leaf(q, schema::parse_ctype(schema::ctype(q)).1.len());
                let tex = lf.get("x-link").and_then(|x| x.get("kind")).and_then(Value::as_str) == Some("texture");
                if !tex {
                    zero_slice(&mut b, lo, hi);
                }
            }
        }
        if let Some(Value::Object(t)) = rec.get_mut("textures") {
            for r in &js {
                t.shift_remove(&hx(*r));
            }
        }
        if rec.get("textures").and_then(Value::as_object).is_some_and(|t| t.is_empty()) {
            rec.shift_remove("textures");
        }
        let mut base = Map::new();
        base.insert("format_version".into(), json!(FORMAT));
        base.insert("id".into(), json!(id));
        base.insert("schema".into(), json!(sname));
        base.insert("json_size".into(), json!(size * n));
        if arr {
            base.insert("count".into(), json!(n));
        }
        if let Some(l) = &nd.label {
            base.insert("type".into(), json!(l));
        }
        base.extend(rec);
        self.put_bytes(&mut base, &b, h)?;
        if let Some(k) = self.kids.get(&nd.start).filter(|k| !k.is_empty()) {
            base.insert("children".into(), Value::Object(k.clone()));
        }
        if base.contains_key("bin") {
            base.insert("bin".into(), json!(format!("{stem}.base.bin")));
            let from = self.out.join(format!("{}.bin", file_stem(&id_path(&id))));
            let to = self.out.join(format!("{stem}.base.bin"));
            std::fs::rename(&from, &to).map_err(|e| Error::io(format!("{}: {e}", from.display())))?;
        }
        self.write_doc(
            &format!("{stem}.base.jsonc"),
            &Value::Object(base),
            &[format!("base of {id}: the bytes and links the {sname} JSON does not carry (hidden fields, padding, tail)")],
            None,
        )
    }

    fn write_geometry(&mut self, h: usize) -> Result<()> {
        let none = HashSet::new();
        let (rec, b) = self.raw_record(h, &none)?;
        let nd = &self.nodes[h];
        let stem = nd.file.clone().unwrap();
        let mut side = Map::new();
        side.insert("format_version".into(), json!(FORMAT));
        side.insert("id".into(), json!(nd.id()));
        side.insert("type".into(), json!("Geometry"));
        side.extend(rec);
        let mut an: Vec<(&String, &u32)> = nd.anchors.iter().collect();
        an.sort_by_key(|x| *x.1);
        side.insert("anchors".into(), Value::Object(an.into_iter().map(|(k, v)| (k.clone(), json!(hx(*v)))).collect()));
        side.insert("bin".into(), json!(format!("{stem}.orig.bin")));
        side.shift_remove("bytes");
        if let Some(k) = self.kids.get(&nd.start).filter(|k| !k.is_empty()) {
            side.insert("children".into(), Value::Object(k.clone()));
        }
        write_bytes(&self.out.join(format!("{stem}.orig.bin")), &b)?;
        let head = format!(
            "geometry {} ({}): its original encoding. Built from these bytes while the glTF's content hash is unchanged; an edited glTF is encoded afresh (tools/xdl_gltf.py)",
            nd.id(),
            self.src.release
        );
        self.write_doc(&format!("{stem}.xgeo.jsonc"), &Value::Object(side), &[head], None)
    }

    fn write_pack(&self, layout: Vec<Value>) -> Result<()> {
        let src = &self.src;
        let mut pj = src.packjson.clone();
        if let Some(h) = pj.get_mut("header").and_then(Value::as_object_mut) {
            for (key, cnt) in HEADER_OFFSETS {
                let v = h.get(key).and_then(Value::as_u64);
                let c = h.get(cnt).map(|c| !matches!(c, Value::Null | Value::Bool(false)) && c != &json!(0)).unwrap_or(false);
                if let Some(v) = v
                    && v != 0
                    && c
                    && let Some(&i) = self.by_start.get(&(v as u32))
                    && v <= u32::MAX as u64
                {
                    h.insert(key.into(), json!(self.nodes[i].id()));
                }
            }
        }
        let mut recsize = Map::new();
        if let Some(secs) = pj.get_mut("sections").and_then(Value::as_array_mut) {
            for s in secs.iter() {
                let recs = s["records"].as_u64().unwrap_or(0);
                let v = if recs != 0 { s["bytes"].as_u64().unwrap_or(0) / recs } else { 1 };
                recsize.insert(py::str_of(&s["file"]), json!(v));
            }
            for s in secs.iter_mut() {
                if let Some(m) = s.as_object_mut() {
                    m.shift_remove("bytes");
                    m.shift_remove("records");
                }
            }
        }
        let idx: HashMap<u32, usize> = self.nodes.iter().enumerate().map(|(i, n)| (n.start, i)).collect();
        let slot = |o: u32| -> Result<Value> {
            let h = self.node_at(o).ok_or_else(|| Error::uncaught("'NoneType' object has no attribute 'start'"))?;
            let nd = &self.nodes[h];
            Ok(json!([idx[&nd.start], o - nd.start]))
        };
        let mut order = Map::new();
        order.insert("pointers".into(), Value::Array(src.pointers.iter().map(|&o| slot(o)).collect::<Result<_>>()?));
        for k in ["texture", "sound", "ref", "shader"] {
            order.insert(k.into(), Value::Array(src.tables[k].iter().map(|&(o, _)| slot(o)).collect::<Result<_>>()?));
        }
        let mut sounds = Map::new();
        let sd = self.out.join("sounds");
        for f in ["sound_data.bin", "sound_pointers.bin"] {
            if src.dir.join(f).exists() {
                std::fs::create_dir_all(&sd).map_err(|e| Error::io(e.to_string()))?;
                std::fs::copy(src.dir.join(f), sd.join(f)).map_err(|e| Error::io(format!("{f}: {e}")))?;
                sounds.insert(f.into(), json!(format!("sounds/{f}")));
            }
        }
        let mut files: Vec<String> = std::fs::read_dir(&src.dir)
            .map_err(|e| Error::io(format!("{}: {e}", src.dir.display())))?
            .flatten()
            .map(|e| e.file_name().to_string_lossy().into_owned())
            .collect();
        files.sort();
        let full = src.base("pack/0");
        let doc = json!({
            "format_version": FORMAT,
            "id": full[..full.len() - "/pack/0".len()],
            "release": src.release,
            "archive": src.archive,
            "entry": src.entry,
            "pack": pj,
            "record_sizes": recsize,
            "files": files,
            "sounds": sounds,
            "nodes": self.nodes.len(),
            "layout": layout,
        });
        self.write_doc(
            "pack.jsonc",
            &doc,
            &[format!("{}/{} ({}): pack header, TOC entry and the order of its nodes (tools/xdl_graph.py)", src.archive, src.name, src.release)],
            None,
        )?;
        let mut t = String::new();
        dump_tight(&Value::Object(order), &mut t);
        t.push('\n');
        write_text(&self.out.join("pack.order.json"), &t)
    }
}

/// json.dumps(v, separators=(",", ":"))
fn dump_tight(v: &Value, s: &mut String) {
    match v {
        Value::Array(a) => {
            s.push('[');
            for (i, x) in a.iter().enumerate() {
                if i > 0 {
                    s.push(',');
                }
                dump_tight(x, s);
            }
            s.push(']');
        }
        Value::Object(m) => {
            s.push('{');
            for (i, (k, x)) in m.iter().enumerate() {
                if i > 0 {
                    s.push(',');
                }
                s.push_str(&py::json_str(k));
                s.push(':');
                dump_tight(x, s);
            }
            s.push('}');
        }
        _ => s.push_str(&py::dumps(v)),
    }
}

/// GraphDecoder: links named by the graph's canonical ids.
struct Namer<'a, 'c> {
    ex: &'a Extractor<'c>,
}

impl LinkNamer for Namer<'_, '_> {
    fn pointer(&self, _pk: &Pack, _off: u32, raw: u32, _path: &str) -> Result<Value> {
        self.ex.link_to(raw, false).map(Value::String)
    }
    fn texture(&self, _pk: &Pack, value: u32, _path: &str) -> Result<Value> {
        let s = &self.ex.src;
        asset_link(self.ex.ctx, &s.release, &s.archive, &s.name, "texture", value).map(Value::String)
    }
    fn geometry(&self, _pk: &Pack, _off: u32, _value: u32, raw: u32, _path: &str) -> Result<Value> {
        self.ex.link_to(raw, false).map(Value::String)
    }
}

impl TacHost for Extractor<'_> {
    fn blob(&self) -> &[u8] {
        &self.src.blob
    }
    fn big(&self) -> bool {
        self.src.big
    }
    fn ptr(&self) -> &BTreeMap<u32, u32> {
        &self.src.ptr
    }
    fn rel(&self, kind: &str) -> Option<&BTreeMap<u32, u32>> {
        self.src.rel.get(kind)
    }
    fn link_to_elem(&self, t: u32) -> Result<String> {
        self.link_to(t, true)
    }
    fn by_start(&self, t: u32) -> Option<usize> {
        self.by_start.get(&t).copied()
    }
    fn node(&self, h: usize) -> &TacNode {
        &self.tacnodes[h]
    }
    fn is_kind(&self, label: &str) -> bool {
        self.kinds.contains(label)
    }
}

// ------------------------------------------------------------------------------------------
// building

/// A node ready to place: bytes, and its slots.
#[derive(Debug, Clone, Default)]
pub struct Built {
    pub id: String,
    pub data: Vec<u8>,
    pub align: u32,
    pub links: IndexMap<u32, String>,
    pub refs: IndexMap<u32, String>,
    pub tex: IndexMap<u32, String>,
    pub snd: IndexMap<u32, String>,
    pub shd: IndexMap<u32, u32>,
    pub anchors: IndexMap<String, u32>,
    pub start: u32,
    pub geometry: bool,
}

impl Built {
    fn new(id: &str, data: Vec<u8>, align: u32) -> Built {
        Built { id: id.into(), data, align, ..Default::default() }
    }
}

fn rec_slots(b: &mut Built, rec: &Value) -> Result<()> {
    let each = |k: &str| -> Vec<(String, Value)> {
        obj_get(rec, k).and_then(Value::as_object).map(|m| m.iter().map(|(a, b)| (a.clone(), b.clone())).collect()).unwrap_or_default()
    };
    for (k, v) in each("links") {
        b.links.insert(int_key(&k)?, py::str_of(&v));
    }
    for (k, v) in each("refs") {
        b.refs.insert(int_key(&k)?, py::str_of(&v));
    }
    for (k, v) in each("textures") {
        b.tex.insert(int_key(&k)?, py::str_of(&v));
    }
    for (k, v) in each("sounds") {
        b.snd.insert(int_key(&k)?, py::str_of(&v));
    }
    for (k, v) in each("shaders") {
        b.shd.insert(int_key(&k)?, v.as_u64().unwrap_or(0) as u32);
    }
    for (k, v) in each("anchors") {
        b.anchors.insert(k, int_key(&py::str_of(&v))?);
    }
    Ok(())
}

fn rec_bytes(folder: &Path, rec: &Value) -> Result<Vec<u8>> {
    if let Some(b) = obj_get(rec, "bin") {
        return error::read(&folder.join(py::str_of(b)));
    }
    unhex(obj_get(rec, "bytes").unwrap_or(&json!([])))
}

fn is_trigger_doc(d: &Value) -> bool {
    obj_get(d, "$schema").map(py::str_of).is_some_and(|s| s.ends_with("Trigger.schema.json"))
}

type TriggerParts = IndexMap<String, (Vec<u8>, BTreeMap<u32, Value>)>;

/// Schema validation as xdl_struct.validator gives it with the jsonschema package installed
/// (Draft 2020-12): every keyword the schemas use.
pub struct Validators {
    cache: HashMap<String, Rc<jsonschema::Validator>>,
}

impl Validators {
    pub fn new() -> Self {
        Validators { cache: HashMap::new() }
    }

    /// The messages of `inst` against `sch` (cached under `key`).
    pub fn errors(&mut self, key: &str, sch: &Value, inst: &Value) -> Result<Vec<String>> {
        let v = match self.cache.get(key) {
            Some(v) => v.clone(),
            None => {
                let v = Rc::new(jsonschema::draft202012::new(sch).map_err(|e| Error::uncaught(format!("schema {key}: {e}")))?);
                self.cache.insert(key.into(), v.clone());
                v
            }
        };
        Ok(v.iter_errors(inst).map(|e| e.to_string()).collect())
    }
}

impl Default for Validators {
    fn default() -> Self {
        Self::new()
    }
}

pub const VALIDATOR_KIND: &str = "jsonschema";

pub struct Builder<'c> {
    pub ctx: &'c Ctx,
    pub dir: PathBuf,
    pub pack: Value,
    pub release: String,
    pub big: bool,
    pub archive: String,
    pub name: String,
    /// stem + ext -> document (the pack build edits them in memory)
    pub docs: HashMap<String, Value>,
    pub edited: Vec<String>,
    instances: IndexMap<String, TriggerParts>,
    tac: Option<Tac>,
    val: Validators,
    xp: Pack,
}

impl<'c> Builder<'c> {
    pub fn new(ctx: &'c Ctx, proj: &Path) -> Result<Builder<'c>> {
        let pack = read_jsonc(&proj.join("pack.jsonc"))?;
        let release = py::str_of(&pack["release"]);
        let archive = py::str_of(&pack["archive"]);
        let name = pack_name(&pack["entry"]);
        let xp = Pack::from_parts(&release, &archive, &name, vec![], BTreeSet::new(), BTreeMap::new(), BTreeMap::new(), BTreeMap::new());
        Ok(Builder {
            ctx,
            dir: proj.to_path_buf(),
            big: release == "gc",
            pack,
            release,
            archive,
            name,
            docs: HashMap::new(),
            edited: vec![],
            instances: IndexMap::new(),
            tac: None,
            val: Validators::new(),
            xp,
        })
    }

    /// The document stem+ext (read once).
    pub fn doc(&mut self, stem: &str, ext: &str) -> Result<&mut Value> {
        let key = format!("{stem}{ext}");
        if !self.docs.contains_key(&key) {
            let d = read_jsonc(&self.dir.join(&key))?;
            self.docs.insert(key.clone(), d);
        }
        Ok(self.docs.get_mut(&key).unwrap())
    }

    fn node(&mut self, ent: &Value) -> Result<Option<Built>> {
        let nid = py::str_of(&ent["id"]);
        let stem = py::str_of(&ent["file"]);
        let align = ent["align"].as_u64().unwrap_or(4) as u32;
        let mut d: Option<Value> = None;
        if self.dir.join(format!("{stem}.xgeo.jsonc")).exists() {
            let x = self.doc(&stem, ".xgeo.jsonc")?.clone();
            if x["id"].as_str() == Some(nid.as_str()) {
                return self.geometry(&nid, align, &x).map(Some);
            }
            d = Some(x);
        } else if self.dir.join(format!("{stem}.base.jsonc")).exists() {
            let x = self.doc(&stem, ".base.jsonc")?.clone();
            if x["id"].as_str() == Some(nid.as_str()) {
                let doc = self.doc(&stem, ".jsonc")?.clone();
                return self.typed_rec(&nid, align, &stem, &py::str_of(&x["schema"]), &doc, &x).map(Some);
            }
            d = Some(x);
        }
        let d = match d {
            Some(d) => d,
            None => self.doc(&stem, ".jsonc")?.clone(),
        };
        if is_trigger_doc(&d) {
            let parts = self.trigger_parts(&stem, &d)?;
            if let Some((data, links)) = parts.get(&nid) {
                let mut data = data.clone();
                data.extend(std::iter::repeat_n(0u8, ent.get("pad").and_then(Value::as_u64).unwrap_or(0) as usize));
                let mut b = Built::new(&nid, data, align);
                for (o, l) in links {
                    b.links.insert(*o, py::str_of(l));
                }
                return Ok(Some(b));
            }
            let Some(rec) = obj_get(&d, "children").and_then(|c| obj_get(c, &nid)) else { return Ok(None) };
            let mut b = Built::new(&nid, rec_bytes(&self.dir, rec)?, align);
            rec_slots(&mut b, rec)?;
            return Ok(Some(b));
        }
        if obj_get(&d, "component").is_some() {
            return Err(graph_err(format!("{stem}.jsonc: a component instance (tools/xdl_components.py): not supported by xdlrs yet")));
        }
        let rec = if d.get("id").and_then(Value::as_str) == Some(nid.as_str()) {
            Some(&d)
        } else {
            obj_get(&d, "children").and_then(|c| obj_get(c, &nid))
        };
        let Some(rec) = rec else { return Err(graph_err(format!("{nid}: not in {stem}.jsonc"))) };
        let b_ = rec_bytes(&self.dir, rec)?;
        let size = obj_get(rec, "size").and_then(Value::as_u64).unwrap_or(0) as usize;
        if b_.len() != size {
            return Err(graph_err(format!("{nid}: {} bytes, size says {size}", b_.len())));
        }
        let mut b = Built::new(&nid, b_, align);
        rec_slots(&mut b, rec)?;
        Ok(Some(b))
    }

    fn trigger_parts(&mut self, stem: &str, d: &Value) -> Result<TriggerParts> {
        if let Some(p) = self.instances.get(stem) {
            return Ok(p.clone());
        }
        if self.tac.is_none() {
            self.tac = Some(Tac::from_schemas(&self.ctx.s)?);
        }
        let sch = self.ctx.s.top("Trigger").ok_or_else(|| Error::uncaught("KeyError: 'Trigger'"))?;
        let errs = self.val.errors("Trigger", sch, d)?;
        if !errs.is_empty() {
            return Err(graph_err(format!("{stem}.jsonc: schema ({VALIDATOR_KIND}): {}", errs[..errs.len().min(3)].join("; "))));
        }
        let id = d.get("id").map(py::str_of).ok_or_else(|| Error::uncaught("KeyError: 'id'"))?;
        let parts = match tac::encode_trigger(self.tac.as_ref().unwrap(), d, &id, self.big, "") {
            Ok(p) => p,
            Err(e) if e.kind == Kind::Tac => return Err(graph_err(format!("{stem}.jsonc: {e}"))),
            Err(e) => return Err(e),
        };
        let mut m = TriggerParts::new();
        for (pid, data, links) in parts {
            m.insert(pid, (data, links));
        }
        self.instances.insert(stem.into(), m.clone());
        Ok(m)
    }

    fn typed_rec(&mut self, nid: &str, align: u32, stem: &str, sname: &str, doc: &Value, base: &Value) -> Result<Built> {
        let data = rec_bytes(&self.dir, base)?;
        if base.get("count").is_some() {
            return self.typed_array(nid, align, stem, sname, doc, base, data);
        }
        let s = &self.ctx.s;
        let top = s.top(sname).ok_or_else(|| Error::uncaught(format!("KeyError: {}", py::str_repr(sname))))?;
        let errs = self.val.errors(sname, top, doc)?;
        if !errs.is_empty() {
            return Err(graph_err(format!("{stem}.jsonc: schema ({VALIDATOR_KIND}): {}", errs[..errs.len().min(3)].join("; "))));
        }
        let mut enc = Encoder::symbolic(s, &self.xp, Some(&self.ctx.pairing), sname, data)?;
        let ext = doc.get("extends").filter(|v| !v.is_null()).map(py::str_of);
        match enc.encode(doc, ext.as_deref()) {
            Ok(_) => {}
            Err(e) if e.kind == Kind::Xdl => return Err(graph_err(format!("{stem}.jsonc: {e}"))),
            Err(e) => return Err(e),
        }
        let mut b = Built::new(nid, enc.out.clone(), align);
        rec_slots(&mut b, base)?;
        put_links(&mut b, &enc.links, 0);
        Ok(b)
    }

    #[allow(clippy::too_many_arguments)]
    fn typed_array(&mut self, nid: &str, align: u32, stem: &str, sname: &str, doc: &Value, base: &Value, data: Vec<u8>) -> Result<Built> {
        let Some(items) = obj_get(doc, "items").and_then(Value::as_array) else {
            return Err(graph_err(format!("{stem}.jsonc: `items` (a list) is missing")));
        };
        let mut out: Vec<u8> = vec![];
        let mut b = Built::new(nid, vec![], align);
        rec_slots(&mut b, base)?;
        let ptrs = sname.starts_with("PTR32(");
        let s = &self.ctx.s;
        if ptrs {
            for (i, v) in items.iter().enumerate() {
                if !v.is_null() && !v.is_string() {
                    return Err(graph_err(format!("{stem}.jsonc: items/{i}: a link or null")));
                }
                out.extend([0u8; 4]);
                if let Some(l) = v.as_str() {
                    b.links.insert(4 * i as u32, l.into());
                }
            }
        } else {
            let sch = s.struct_(sname).ok_or_else(|| Error::uncaught("'NoneType' object is not subscriptable"))?;
            let size = schema::size(sch)?;
            let mut top = s.top(sname).ok_or_else(|| Error::uncaught(format!("KeyError: {}", py::str_repr(sname))))?.clone();
            top["required"] = json!([]);
            for (i, item) in items.iter().enumerate() {
                let Some(m) = item.as_object() else {
                    return Err(Error::uncaught(format!("{stem}.jsonc: items/{i}: not an object")));
                };
                let mut it = m.clone();
                it.insert("format_version".into(), json!(1));
                let errs = self.val.errors(&format!("{sname}[]"), &top, &Value::Object(it))?;
                if !errs.is_empty() {
                    return Err(graph_err(format!("{stem}.jsonc: items/{i}: schema ({VALIDATOR_KIND}): {}", errs[..errs.len().min(3)].join("; "))));
                }
                let chunk = if (i + 1) * size <= data.len() { data[i * size..(i + 1) * size].to_vec() } else { vec![0; size] };
                let mut enc = Encoder::symbolic(s, &self.xp, Some(&self.ctx.pairing), sname, chunk)?;
                match enc.encode(item, None) {
                    Ok(_) => {}
                    Err(e) if e.kind == Kind::Xdl => return Err(graph_err(format!("{stem}.jsonc: items/{i}: {e}"))),
                    Err(e) => return Err(e),
                }
                put_links(&mut b, &enc.links, (i * size) as u32);
                out.extend(&enc.out);
            }
            if items.len() * size < data.len() {
                out.extend(&data[items.len() * size..]);
            }
        }
        let n = items.len();
        if Some(n as u64) != base.get("count").and_then(Value::as_u64) {
            let lim = out.len() as u32;
            b.links.retain(|&o, _| o < lim);
            b.refs.retain(|&o, _| o < lim);
            b.tex.retain(|&o, _| o < lim);
            b.snd.retain(|&o, _| o < lim);
            b.shd.retain(|&o, _| o < lim);
        }
        b.data = out;
        let sz = if ptrs { 4 } else { s.size_of(sname)? as u32 };
        b.anchors = (0..n as u32).map(|i| (i.to_string(), i * sz)).collect();
        Ok(b)
    }

    fn geometry(&mut self, nid: &str, align: u32, side: &Value) -> Result<Built> {
        if side.get("gltf").is_some() {
            // the glTF may have been edited: only xdl_graph.py re-encodes geometry
            return Err(graph_err(format!(
                "{nid}: a project with glTF geometry (xdl_graph.py's): the level tools' base projects keep the original encoding only"
            )));
        }
        let orig = error::read(&self.dir.join(py::str_of(&side["bin"])))?;
        let mut b = Built::new(nid, orig, align);
        b.geometry = true;
        rec_slots(&mut b, side)?;
        Ok(b)
    }

    /// The pack files in `out` (data.bin, the relocation tables, pack.json, the sounds); returns
    /// them by name.
    pub fn build(&mut self, out: &Path) -> Result<BTreeMap<String, Vec<u8>>> {
        let layout: Vec<Value> = self.pack["layout"].as_array().cloned().unwrap_or_default();
        if self.pack.get("append").and_then(Value::as_array).is_some_and(|a| !a.is_empty()) {
            return Err(graph_err("pack.jsonc: appended files (tools/xdl_components.py): not supported by xdlrs yet"));
        }
        let mut built: Vec<Built> = vec![];
        for ent in &layout {
            if let Some(b) = self.node(ent)? {
                built.push(b);
            }
        }
        // parts a trigger file gained: after its record
        let have: HashSet<String> = built.iter().map(|b| b.id.clone()).collect();
        let stems: Vec<String> = self.instances.keys().cloned().collect();
        for stem in stems {
            let parts = self.instances[&stem].clone();
            let new: Vec<(&String, &(Vec<u8>, BTreeMap<u32, Value>))> = parts.iter().filter(|(k, _)| !have.contains(*k)).collect();
            if new.is_empty() {
                continue;
            }
            let mut at = built.iter().enumerate().filter(|(_, b)| parts.contains_key(&b.id)).map(|(i, _)| i).max().unwrap_or(0) + 1;
            for (pid, (data, links)) in new {
                let mut nb = Built::new(pid, data.clone(), 4);
                for (o, l) in links {
                    nb.links.insert(*o, py::str_of(l));
                }
                built.insert(at, nb);
                at += 1;
            }
        }
        // place
        let mut pos: u64 = 0;
        let mut index: HashMap<String, usize> = HashMap::new();
        for (i, b) in built.iter_mut().enumerate() {
            let a = b.align.max(1) as u64;
            if pos % a != 0 {
                pos += a - pos % a;
            }
            b.start = pos as u32;
            pos += b.data.len() as u64;
            index.insert(b.id.clone(), i);
        }
        let mut blob = vec![0u8; pos as usize];
        for b in &built {
            blob[b.start as usize..b.start as usize + b.data.len()].copy_from_slice(&b.data);
        }
        let geoms: Vec<usize> = built.iter().enumerate().filter(|(_, b)| b.geometry).map(|(i, _)| i).collect();
        let gidx: HashMap<String, usize> = geoms.iter().enumerate().map(|(k, &i)| (built[i].id.clone(), k)).collect();
        let resolve = |link: &str| -> Result<(u32, String)> {
            if let Some(&i) = index.get(link) {
                return Ok((built[i].start, link.to_string()));
            }
            let parts: Vec<&str> = link.split('/').collect();
            let mut k = parts.len().saturating_sub(1);
            while k > 2 {
                let head = parts[..k].join("/");
                if let Some(&i) = index.get(&head) {
                    let nb = &built[i];
                    let rest = parts[k..].join("/");
                    if let Some(hx_) = rest.strip_prefix('o')
                        && !hx_.is_empty()
                        && hx_.bytes().all(|c| c.is_ascii_digit() || (b'a'..=b'f').contains(&c))
                    {
                        let d = u64::from_str_radix(hx_, 16).unwrap_or(u64::MAX);
                        if d >= nb.data.len() as u64 {
                            return Err(graph_err(format!("{link}: offset 0x{d:X} past the end of {head} (0x{:X} bytes)", nb.data.len())));
                        }
                        return Ok((nb.start + d as u32, head));
                    }
                    if let Some(&a) = nb.anchors.get(&rest) {
                        return Ok((nb.start + a, head));
                    }
                    return Err(graph_err(format!("{link}: no {rest} in {head}")));
                }
                k -= 1;
            }
            Err(graph_err(format!("{link}: no such object in this pack")))
        };
        let mut slots: HashMap<&str, BTreeMap<(usize, u32), Option<u32>>> = SECTIONS.iter().map(|k| (*k, BTreeMap::new())).collect();
        let own = (self.archive.clone(), self.name.clone());
        for (li, b) in built.iter().enumerate() {
            for (&off, link) in &b.links {
                let (t, head) = resolve(link)?;
                put_u32(&mut blob, (b.start + off) as usize, t, self.big)?;
                if b.refs.get(&off).map(String::as_str) == Some("local") {
                    if !gidx.contains_key(&head) || t != built[index[&head]].start {
                        return Err(graph_err(format!("{}+0x{off:X}: a ref slot must point to a geometry", b.id)));
                    }
                    slots.get_mut("ref").unwrap().insert((li, off), Some(gidx[&head] as u32));
                } else {
                    slots.get_mut("pointers").unwrap().insert((li, off), None);
                }
            }
            for (&off, v) in &b.refs {
                if v != "local" {
                    slots.get_mut("ref").unwrap().insert((li, off), Some(export_value(v)?));
                }
            }
            for (&off, v) in &b.tex {
                let x = asset_value(self.ctx, &self.release, v, "texture", (&own.0, &own.1))?;
                slots.get_mut("texture").unwrap().insert((li, off), Some(x));
            }
            for (&off, v) in &b.snd {
                let x = asset_value(self.ctx, &self.release, v, "sound", (&own.0, &own.1))?;
                slots.get_mut("sound").unwrap().insert((li, off), Some(x));
            }
            for (&off, &v) in &b.shd {
                slots.get_mut("shader").unwrap().insert((li, off), Some(v));
            }
        }
        let orderf = self.dir.join("pack.order.json");
        let order = if orderf.exists() { jsonc::parse(&error::read_text(&orderf)?)? } else { json!({}) };
        std::fs::create_dir_all(out).map_err(|e| Error::io(format!("{}: {e}", out.display())))?;
        let mut files: BTreeMap<String, Vec<u8>> = BTreeMap::new();
        files.insert("data.bin".into(), blob);
        for k in SECTIONS {
            let have = &slots[k];
            let mut seq: Vec<(usize, u32)> = vec![];
            let mut seen = HashSet::new();
            for x in order.get(k).and_then(Value::as_array).cloned().unwrap_or_default() {
                let t = (x[0].as_u64().unwrap_or(u64::MAX) as usize, x[1].as_u64().unwrap_or(u64::MAX) as u32);
                if have.contains_key(&t) {
                    seq.push(t);
                    seen.insert(t);
                }
            }
            seq.extend(have.keys().filter(|x| !seen.contains(*x)).copied());
            let mut raw = vec![];
            for (li, off) in seq {
                let a = built[li].start + off;
                let w = |v: u32| if self.big { v.to_be_bytes() } else { v.to_le_bytes() };
                raw.extend(w(a));
                if k != "pointers" {
                    raw.extend(w(have[&(li, off)].unwrap_or(0)));
                }
            }
            files.insert(section_file(k).into(), raw);
        }
        if let Some(snd) = self.pack.get("sounds").and_then(Value::as_object) {
            for (f, rel) in snd {
                files.insert(f.clone(), error::read(&self.dir.join(py::str_of(rel)))?);
            }
        }
        let mut pj = self.pack["pack"].clone();
        if let Some(h) = pj.get_mut("header").and_then(Value::as_object_mut) {
            for (key, _) in HEADER_OFFSETS {
                if let Some(Value::String(v)) = h.get(key).cloned() {
                    h.insert(key.into(), json!(resolve(&v)?.0));
                }
            }
        }
        let mut present: BTreeSet<String> = self.pack.get("files").and_then(Value::as_array).map(|a| a.iter().map(py::str_of).collect()).unwrap_or_default();
        let secs = pj.get_mut("sections").and_then(Value::as_array_mut);
        let mut empty = vec![];
        let secs = secs.unwrap_or(&mut empty);
        for k in ["pointers", "texture", "sound", "ref", "shader"] {
            let f = section_file(k);
            if present.contains(f) || files.get(f).is_none_or(|d| d.is_empty()) {
                continue;
            }
            present.insert(f.into());
            if !secs.iter().any(|s| s["file"].as_str() == Some(f)) {
                let rank = SECTION_ORDER.iter().position(|x| *x == f).unwrap();
                let at = secs
                    .iter()
                    .filter(|s| s["file"].as_str().and_then(|sf| SECTION_ORDER.iter().position(|x| *x == sf)).is_some_and(|r| r < rank))
                    .count();
                let about = SECTION_ABOUT.iter().find(|x| x.0 == f).unwrap().1;
                secs.insert(at, json!({"file": f, "about": about}));
            }
        }
        for s in secs.iter_mut() {
            let f = py::str_of(&s["file"]);
            let data_len = files.get(&f).map(|d| d.len()).unwrap_or(0) as u64;
            let rs = self.pack["record_sizes"].get(&f).and_then(Value::as_u64).unwrap_or(if f == "pointers.bin" {
                4
            } else if SECTION_ABOUT.iter().any(|x| x.0 == f) {
                8
            } else {
                1
            });
            let mut nw = Map::new();
            nw.insert("file".into(), json!(f));
            nw.insert("bytes".into(), json!(data_len));
            nw.insert("records".into(), json!(data_len.checked_div(rs).unwrap_or(0)));
            if let Some(m) = s.as_object() {
                for (k, v) in m {
                    if k != "file" {
                        nw.insert(k.clone(), v.clone());
                    }
                }
            }
            *s = Value::Object(nw);
        }
        files.insert("pack.json".into(), format!("{}\n", py::dumps_indent(&pj, 2, false)).into_bytes());
        for (f, data) in &files {
            if present.contains(f) || f == "data.bin" || f == "pack.json" {
                std::fs::write(out.join(f), data).map_err(|e| Error::io(format!("{}: {e}", out.join(f).display())))?;
            }
        }
        Ok(files)
    }
}

fn put_links(b: &mut Built, links: &BTreeMap<u32, (String, String)>, at: u32) {
    for (off, (kind, v)) in links {
        match kind.as_str() {
            "object" => {
                b.links.insert(at + off, v.clone());
            }
            "texture" => {
                b.tex.insert(at + off, v.clone());
            }
            _ => {
                b.links.insert(at + off, v.clone());
                b.refs.insert(at + off, "local".into());
            }
        }
    }
}

/// Differences between two pack folders: [(file, what)].
pub fn compare(a: &Path, b: &Path) -> Vec<(String, String)> {
    let list = |d: &Path| -> Vec<String> {
        let mut v: Vec<String> = std::fs::read_dir(d)
            .map(|r| r.flatten().filter(|e| e.path().is_file()).map(|e| e.file_name().to_string_lossy().into_owned()).collect())
            .unwrap_or_default();
        v.sort();
        v
    };
    let (fa, fb) = (list(a), list(b));
    let mut out = vec![];
    if fa != fb {
        out.push(("files".into(), format!("{fa:?} != {fb:?}")));
    }
    for f in &fa {
        if b.join(f).exists() {
            let (x, y) = (std::fs::read(a.join(f)).unwrap_or_default(), std::fs::read(b.join(f)).unwrap_or_default());
            if x != y {
                let i = x.iter().zip(&y).position(|(p, q)| p != q).unwrap_or(x.len().min(y.len()));
                out.push((f.clone(), format!("differs at 0x{i:X} ({} / {} bytes)", x.len(), y.len())));
            }
        }
    }
    out
}
