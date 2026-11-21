//! ISO 9660 (the PS2 DVDs, the PC CD), as src/port/pcdisc.c reads it: the primary volume
//! descriptor at sector 16, the root directory record at 156 in it; directory records with their
//! extent (sector, size) little-endian at 2 and 10, flags at 25 (2: a directory), the name at 33
//! (";1" cut off; 0 and 1 are "." and ".."). A zero length byte pads to the next sector. Names are
//! matched without case.
//!
//! The source is the 2048-byte sector view (a raw .bin goes through io::RawSectors first).

use super::gcm::safe_name;
use super::io::{ReadAt, le32};

/// Directories are small; anything bigger is a damaged or crafted image (pcdisc.c MAX_DIR_SIZE).
const MAX_DIR_SIZE: u32 = 1 << 20;

#[derive(Clone, Debug)]
pub struct Rec {
    pub name: String,
    pub lba: u32,
    pub size: u32,
    pub dir: bool,
}

impl Rec {
    pub fn offset(&self) -> u64 {
        self.lba as u64 * 2048
    }
}

pub fn is_iso(src: &dyn ReadAt) -> bool {
    let mut pvd = [0u8; 6];
    src.read_at(16 * 2048, &mut pvd).is_ok() && pvd[0] == 1 && &pvd[1..6] == b"CD001"
}

/// The root directory's record.
pub fn root(src: &dyn ReadAt) -> Result<Rec, String> {
    let mut pvd = vec![0u8; 2048];
    src.read_at(16 * 2048, &mut pvd).map_err(|_| "not an ISO 9660 disc image".to_string())?;
    if pvd[0] != 1 || &pvd[1..6] != b"CD001" {
        return Err("not an ISO 9660 disc image".into());
    }
    Ok(Rec { name: String::new(), lba: le32(&pvd, 156 + 2), size: le32(&pvd, 156 + 10), dir: true })
}

/// The records of a directory (not "." and "..").
pub fn list(src: &dyn ReadAt, dir: &Rec) -> Result<Vec<Rec>, String> {
    if dir.size == 0 || dir.size > MAX_DIR_SIZE {
        return Err("could not read a folder of the disc".into());
    }
    let mut d = vec![0u8; dir.size as usize];
    src.read_at(dir.offset(), &mut d).map_err(|_| "could not read a folder of the disc".to_string())?;
    let size = d.len();
    let mut out = vec![];
    let mut at = 0usize;
    while at < size {
        if d[at] == 0 {
            at = (at / 2048 + 1) * 2048;
            continue;
        }
        let len = d[at] as usize;
        let nlen = d.get(at + 32).copied().unwrap_or(0) as usize;
        if len < 34 || len > size - at || nlen == 0 || 33 + nlen > len {
            break; // the end, or a damaged record: as pcdisc.c, stop there
        }
        let raw = &d[at + 33..at + 33 + nlen];
        if !(nlen == 1 && (raw[0] == 0 || raw[0] == 1)) {
            let mut name = String::from_utf8_lossy(raw).into_owned();
            if let Some(i) = name.find(';') {
                name.truncate(i);
            }
            // "NAME.;1" is a file without an extension (Windows would drop the dot)
            if name.len() > 1 && name.ends_with('.') {
                name.pop();
            }
            out.push(Rec { name, lba: le32(&d, at + 2), size: le32(&d, at + 10), dir: d[at + 25] & 2 != 0 });
        }
        at += len;
    }
    Ok(out)
}

/// A record by name in `dir` (without case).
pub fn find(src: &dyn ReadAt, dir: &Rec, name: &str) -> Option<Rec> {
    list(src, dir).ok()?.into_iter().find(|r| r.name.eq_ignore_ascii_case(name))
}

/// A record by path ('/'-separated, without case) from `from`.
pub fn find_path(src: &dyn ReadAt, from: &Rec, path: &str) -> Option<Rec> {
    let mut cur = from.clone();
    for part in path.split('/').filter(|p| !p.is_empty()) {
        if !cur.dir {
            return None;
        }
        cur = find(src, &cur, part)?;
    }
    Some(cur)
}

/// Every file under `dir` as (path under `prefix`, record), depth first in disc order. Folders
/// that point back at one already walked are skipped (pcdisc.c Visited), at most 256 of them.
pub fn walk(src: &dyn ReadAt, dir: &Rec, prefix: &str) -> Result<Vec<(String, Rec)>, String> {
    fn go(src: &dyn ReadAt, dir: &Rec, prefix: &str, seen: &mut Vec<u32>, out: &mut Vec<(String, Rec)>) -> Result<(), String> {
        if seen.contains(&dir.lba) {
            return Ok(());
        }
        if seen.len() == 256 {
            return Err("could not read a folder of the disc (too many)".into());
        }
        seen.push(dir.lba);
        for r in list(src, dir)? {
            if !safe_name(&r.name) {
                return Err("a damaged name on the disc".into());
            }
            let p = if prefix.is_empty() { r.name.clone() } else { format!("{prefix}/{}", r.name) };
            if r.dir {
                go(src, &r, &p, seen, out)?;
            } else {
                out.push((p, r));
            }
        }
        Ok(())
    }
    let mut out = vec![];
    go(src, dir, prefix, &mut vec![], &mut out)?;
    Ok(out)
}

#[cfg(any(test, feature = "test-utils"))]
#[allow(dead_code, unused_imports)]
pub(crate) mod tests {
    use super::*;
    use crate::disc::io::{Mem, RawSectors};
    use std::sync::Arc;

    pub enum Node<'a> {
        File(&'a str, &'a [u8]),
        Dir(&'a str, Vec<Node<'a>>),
    }

    fn record(name: &[u8], lba: u32, size: u32, dir: bool) -> Vec<u8> {
        let mut r = vec![0u8; 33];
        r[2..6].copy_from_slice(&lba.to_le_bytes());
        r[6..10].copy_from_slice(&lba.to_be_bytes());
        r[10..14].copy_from_slice(&size.to_le_bytes());
        r[14..18].copy_from_slice(&size.to_be_bytes());
        r[25] = if dir { 2 } else { 0 };
        r[32] = name.len() as u8;
        r.extend_from_slice(name);
        if r.len() % 2 == 1 {
            r.push(0);
        }
        r[0] = r.len() as u8;
        r
    }

    /// A tiny ISO 9660 image of `root` (file names get ";1"), 2048-byte sectors.
    pub fn make(root: &[Node]) -> Vec<u8> {
        let mut img = vec![0u8; 18 * 2048];
        // lay out: a directory takes one sector (records padded into it), files after
        fn place(img: &mut Vec<u8>, nodes: &[Node], parent: u32) -> (u32, u32) {
            let me = (img.len() / 2048) as u32;
            img.extend_from_slice(&[0u8; 2048]);
            let mut recs = vec![record(&[0], me, 2048, true), record(&[1], parent, 2048, true)];
            for n in nodes {
                match n {
                    Node::File(name, data) => {
                        let lba = (img.len() / 2048) as u32;
                        img.extend_from_slice(data);
                        while img.len() % 2048 != 0 {
                            img.push(0);
                        }
                        recs.push(record(format!("{name};1").as_bytes(), lba, data.len() as u32, false));
                    }
                    Node::Dir(name, kids) => {
                        let (lba, size) = place(img, kids, me);
                        recs.push(record(name.as_bytes(), lba, size, true));
                    }
                }
            }
            let mut at = me as usize * 2048;
            for r in recs {
                img[at..at + r.len()].copy_from_slice(&r);
                at += r.len();
            }
            assert!(at <= me as usize * 2048 + 2048);
            (me, 2048)
        }
        let (lba, size) = place(&mut img, root, 18);
        let pvd = 16 * 2048;
        img[pvd] = 1;
        img[pvd + 1..pvd + 6].copy_from_slice(b"CD001");
        let r = record(&[0], lba, size, true);
        img[pvd + 156..pvd + 156 + r.len()].copy_from_slice(&r);
        img[17 * 2048] = 255; // the set terminator
        img[17 * 2048 + 1..17 * 2048 + 6].copy_from_slice(b"CD001");
        img
    }

    /// The same image in raw MODE1/2352 sectors.
    pub fn to_raw(img: &[u8]) -> Vec<u8> {
        let mut out = vec![];
        for s in img.chunks(2048) {
            let mut h = [0u8; 16];
            h[1..11].fill(0xFF);
            h[15] = 1;
            out.extend_from_slice(&h);
            out.extend_from_slice(s);
            out.extend_from_slice(&[0u8; 288]);
        }
        out
    }

    fn sample() -> Vec<u8> {
        make(&[
            Node::Dir("XPS", vec![Node::Dir("ENGLISH", vec![Node::File("HW_SHARE.XPS", b"abc"), Node::Dir("AUDIO_", vec![Node::File("A.VAG", &[7u8; 5000])])])]),
            Node::File("SLUS_204.12", b"elf"),
        ])
    }

    #[test]
    fn walks_and_finds() {
        let src = Mem(sample());
        let root = root(&src).unwrap();
        let eng = find_path(&src, &root, "xps/english").unwrap();
        let files = walk(&src, &eng, "XPS/ENGLISH").unwrap();
        let paths: Vec<&str> = files.iter().map(|f| f.0.as_str()).collect();
        assert_eq!(paths, ["XPS/ENGLISH/HW_SHARE.XPS", "XPS/ENGLISH/AUDIO_/A.VAG"]);
        let mut b = vec![0u8; 5000];
        src.read_at(files[1].1.offset(), &mut b).unwrap();
        assert!(b.iter().all(|&x| x == 7));
        assert!(find_path(&src, &root, "XPS/ENGLISH/hw_share.xps").is_some_and(|r| r.size == 3));
    }

    #[test]
    fn raw_sectors_read_the_same() {
        let img = sample();
        let raw: Arc<dyn ReadAt> = Arc::new(Mem(to_raw(&img)));
        assert!(crate::disc::io::is_raw_cd(&*raw));
        let src = RawSectors { inner: raw };
        assert_eq!(src.len(), img.len() as u64);
        let r = find_path(&src, &root(&src).unwrap(), "XPS/ENGLISH/AUDIO_/A.VAG").unwrap();
        let mut b = vec![0u8; 5000];
        src.read_at(r.offset(), &mut b).unwrap();
        assert_eq!(&b[..], &img[r.offset() as usize..r.offset() as usize + 5000]);
    }

    #[test]
    fn a_loop_does_not_go_round() {
        // a folder whose record points at the root again
        let mut img = make(&[Node::Dir("LOOP", vec![]), Node::File("F", b"x")]);
        let src = Mem(img.clone());
        let root = root(&src).unwrap();
        let lp = find(&src, &root, "LOOP").unwrap();
        // the LOOP record in the root: patch its extent to the root's
        let rd = root.offset() as usize;
        let i = img[rd..rd + 2048].windows(4).position(|w| w == b"LOOP").unwrap() + rd - 33;
        img[i + 2..i + 6].copy_from_slice(&root.lba.to_le_bytes());
        let _ = lp;
        let files = walk(&Mem(img), &root, "").unwrap();
        assert_eq!(files.len(), 1);
    }
}
