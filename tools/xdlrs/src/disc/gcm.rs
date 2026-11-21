//! A GameCube disc (.iso/.gcm, or what a .gcz holds): its header and file system table, as
//! src/port/disc.c reads them.
//!
//! Big-endian: the magic 0xC2339F3D at 0x1C, the game ID at 0, the title at 0x20, the table's
//! offset and size at 0x424/0x428. The table is 12-byte entries, then their names: entry 0 is the
//! root; a directory's entry holds its parent and the index after its last descendant, a file's
//! its offset and length on the disc. An NKit image ("NKIT" at 0x200) packs the files closer and
//! rewrites the table to match, so it reads the same way (docs/windows/README.md "NKit images").

use super::io::{ReadAt, be32};

pub const MAGIC: u32 = 0xC233_9F3D;
pub const WII_MAGIC: u32 = 0x5D1C_9EA3;

#[derive(Debug)]
pub struct Header {
    pub id: String,
    pub title: String,
    pub nkit: bool,
}

#[derive(Debug)]
pub struct FstFile {
    pub path: String,
    pub offset: u64,
    pub size: u64,
}

pub fn is_gc(src: &dyn ReadAt) -> bool {
    let mut h = [0u8; 4];
    src.read_at(0x1C, &mut h).is_ok() && u32::from_be_bytes(h) == MAGIC
}

fn cstr(b: &[u8]) -> String {
    let end = b.iter().position(|&c| c == 0).unwrap_or(b.len());
    String::from_utf8_lossy(&b[..end]).into_owned()
}

/// A name from the disc, safe as one path component, on Windows too (an extraction made
/// elsewhere may be copied there): none of <>:"/\|?* or control characters, no trailing dot or
/// space (Windows drops them), not a device name (CON, NUL.txt, COM1, ...).
pub fn safe_name(n: &str) -> bool {
    if n.is_empty() || n.ends_with(['.', ' ']) || n.chars().any(|c| c < ' ' || "<>:\"/\\|?*".contains(c)) {
        return false;
    }
    let stem = n.split('.').next().unwrap_or(n).trim_end_matches(' ').to_ascii_uppercase();
    let device = matches!(stem.as_str(), "CON" | "PRN" | "AUX" | "NUL")
        || (stem.len() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) && stem.as_bytes()[3].is_ascii_digit());
    !device
}

/// The header and every file of the table, in table order, paths joined with '/'.
pub fn read(src: &dyn ReadAt) -> Result<(Header, Vec<FstFile>), String> {
    let mut h = vec![0u8; 0x440];
    src.read_at(0, &mut h).map_err(|_| "too small to be a GameCube disc image".to_string())?;
    if be32(&h, 0x1C) != MAGIC {
        return Err(if be32(&h, 0x18) == WII_MAGIC { "that is a Wii disc image, not a GameCube one" } else { "not a GameCube disc image" }.into());
    }
    let header = Header { id: cstr(&h[0..6]), title: cstr(&h[0x20..0x20 + 0x3E0]), nkit: &h[0x200..0x204] == b"NKIT" };
    let (off, size) = (be32(&h, 0x424) as u64, be32(&h, 0x428) as usize);
    if !(12..=16 << 20).contains(&size) || off >= src.len() {
        return Err("the disc image has no readable file table (a trimmed or damaged image?)".into());
    }
    let mut fst = vec![0u8; size];
    src.read_at(off, &mut fst).map_err(|_| "the disc image's file table is unreadable (a trimmed or damaged image?)".to_string())?;
    let entries = be32(&fst, 8) as usize;
    if entries == 0 || entries.saturating_mul(12) > size || fst[0] & 1 == 0 {
        return Err("the disc image's file table is damaged".into());
    }
    let name = |i: usize| -> Result<String, String> {
        let at = entries * 12 + (be32(&fst, i * 12) & 0xFF_FFFF) as usize;
        let n = if at < size { cstr(&fst[at..]) } else { String::new() };
        if safe_name(&n) { Ok(n) } else { Err(format!("the disc image's file table is damaged (entry {i}: {n:?})")) }
    };
    let mut out = vec![];
    let mut dirs: Vec<(usize, String)> = vec![]; // (index after the last descendant, name)
    for i in 1..entries {
        while dirs.last().is_some_and(|d| i >= d.0) {
            dirs.pop();
        }
        let n = name(i)?;
        if fst[i * 12] != 0 {
            let end = (be32(&fst, i * 12 + 8) as usize).clamp(i + 1, entries);
            // a crafted table can't nest past its parent's end
            let end = dirs.last().map_or(end, |d| end.min(d.0));
            dirs.push((end, n));
        } else {
            let mut path = String::new();
            for d in &dirs {
                path.push_str(&d.1);
                path.push('/');
            }
            path.push_str(&n);
            out.push(FstFile { path, offset: be32(&fst, i * 12 + 4) as u64, size: be32(&fst, i * 12 + 8) as u64 });
        }
    }
    Ok((header, out))
}

#[cfg(any(test, feature = "test-utils"))]
#[allow(dead_code, unused_imports)]
pub(crate) mod tests {
    use super::*;
    use crate::disc::io::Mem;

    /// A tiny GameCube image: `files` (path, bytes), directories made from the paths ('/').
    pub fn make(id: &str, files: &[(&str, &[u8])]) -> Vec<u8> {
        // the tree: entries in depth-first order (the paths sorted), each directory's end set
        // when it closes
        enum E {
            Dir(String, usize), // name, parent
            File(String, usize), // name, index into files
        }
        let mut ents: Vec<E> = vec![E::Dir(String::new(), 0)];
        let mut ends: Vec<usize> = vec![0];
        let mut sorted: Vec<usize> = (0..files.len()).collect();
        sorted.sort_by(|&a, &b| files[a].0.cmp(files[b].0));
        let mut open: Vec<(String, usize)> = vec![]; // (dir path, entry index)
        for &k in &sorted {
            let parts: Vec<&str> = files[k].0.split('/').collect();
            let dirs = &parts[..parts.len() - 1];
            while let Some((p, i)) = open.last() {
                if open.len() <= dirs.len() && *p == dirs[..open.len()].join("/") {
                    break;
                }
                ends[*i] = ents.len();
                open.pop();
            }
            while open.len() < dirs.len() {
                let parent = open.last().map_or(0, |o| o.1);
                ents.push(E::Dir(dirs[open.len()].to_string(), parent));
                ends.push(0);
                open.push((dirs[..open.len() + 1].join("/"), ents.len() - 1));
            }
            ents.push(E::File(parts[parts.len() - 1].to_string(), k));
            ends.push(0);
        }
        for (_, i) in open {
            ends[i] = ents.len();
        }
        ends[0] = ents.len();
        let mut names = vec![];
        let mut fst = vec![];
        let data_start = 0x10000u32;
        let mut data = vec![];
        for (i, e) in ents.iter().enumerate() {
            let (name, isdir) = match e { E::Dir(nm, _) => (nm.clone(), true), E::File(nm, _) => (nm.clone(), false) };
            let noff = if i == 0 { 0 } else { names.len() as u32 };
            if i > 0 {
                names.extend_from_slice(name.as_bytes());
                names.push(0);
            }
            fst.extend_from_slice(&((isdir as u32) << 24 | noff).to_be_bytes());
            match e {
                E::Dir(_, p) => {
                    fst.extend_from_slice(&(*p as u32).to_be_bytes());
                    fst.extend_from_slice(&(ends[i] as u32).to_be_bytes());
                }
                E::File(_, k) => {
                    while data.len() % 32 != 0 { data.push(0); }
                    fst.extend_from_slice(&(data_start + data.len() as u32).to_be_bytes());
                    fst.extend_from_slice(&(files[*k].1.len() as u32).to_be_bytes());
                    data.extend_from_slice(files[*k].1);
                }
            }
        }
        fst.extend_from_slice(&names);
        let fst_off = 0x2000u32;
        let mut img = vec![0u8; data_start as usize];
        img[..id.len()].copy_from_slice(id.as_bytes());
        img[0x1C..0x20].copy_from_slice(&MAGIC.to_be_bytes());
        img[0x20..0x30].copy_from_slice(b"Hot Wheels Test\0");
        img[0x424..0x428].copy_from_slice(&fst_off.to_be_bytes());
        img[0x428..0x42C].copy_from_slice(&(fst.len() as u32).to_be_bytes());
        img[fst_off as usize..fst_off as usize + fst.len()].copy_from_slice(&fst);
        img.extend_from_slice(&data);
        img
    }

    #[test]
    fn reads_the_table() {
        let img = make("GHWE78", &[("HW_share.xgc", b"share"), ("worlds_/a/w1.xgc", b"one"), ("worlds_/b.xgc", b"bee"), ("zz.bin", b"")]);
        let src = Mem(img);
        let (h, files) = read(&src).unwrap();
        assert_eq!(h.id, "GHWE78");
        assert!(!h.nkit);
        let paths: Vec<&str> = files.iter().map(|f| f.path.as_str()).collect();
        assert_eq!(paths, ["HW_share.xgc", "worlds_/a/w1.xgc", "worlds_/b.xgc", "zz.bin"]);
        let mut b = vec![0u8; 3];
        src.read_at(files[2].offset, &mut b).unwrap();
        assert_eq!(&b, b"bee");
    }

    #[test]
    fn refuses_other_discs() {
        assert!(read(&Mem(vec![0u8; 0x1000])).is_err());
        let mut w = vec![0u8; 0x1000];
        w[0x18..0x1C].copy_from_slice(&WII_MAGIC.to_be_bytes());
        assert!(read(&Mem(w)).unwrap_err().contains("Wii"));
    }

    #[test]
    fn refuses_unsafe_names() {
        let mut img = make("GHWE78", &[("ab", b"x")]);
        // the name "ab" sits right after the two entries
        let at = 0x2000 + 24;
        img[at..at + 2].copy_from_slice(b"..");
        assert!(read(&Mem(img)).is_err());
    }
}
