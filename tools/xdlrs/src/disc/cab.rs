//! A Microsoft cabinet (the PC CD's HWVX.cab), as src/port/pcdisc.c ExtractCab reads it: one
//! cabinet (not part of a set), its folders compressed with MSZIP (each data block "CK" and a
//! deflate stream, the blocks of a folder sharing their 32 KB history) or stored. LZX and Quantum
//! folders are refused, as the C code does.
//!
//! Streamed: a folder is decompressed a block (32 KB at most) at a time, from the image, and each
//! block handed to the caller with its place in the folder; nothing holds the whole cabinet.
//!
//! Header (little-endian): "MSCF", the files' table offset at 16, folder and file counts at 26/28,
//! flags at 30 (1|2: one of a set; 4: reserved space, sizes at 36/38/39), folders from 36 (or 40 +
//! the header reserve): data offset u32, block count u16, compression u16. A file: size, offset in
//! its folder's data, folder u16, date, time, attributes, then its name (NUL-ended). A block:
//! checksum u32, compressed size u16, uncompressed size u16, the block reserve, the data.

use super::gcm::safe_name;
use super::io::{Src, le16, le32};
use miniz_oxide::inflate::TINFLStatus;
use miniz_oxide::inflate::core::{DecompressorOxide, decompress, inflate_flags};

/// What [`Cab::stream`] hands each block to: its offset in the folder, its bytes; false stops.
pub type Sink<'a> = dyn FnMut(u64, &[u8]) -> Result<bool, String> + 'a;

pub struct Folder {
    pub data: u32,
    pub blocks: u16,
    pub kind: u16,
}

#[derive(Clone, Debug)]
pub struct CabFile {
    pub name: String,
    pub size: u32,
    pub offset: u32,
    pub folder: u16,
}

pub struct Cab {
    src: Src,
    pub folders: Vec<Folder>,
    pub files: Vec<CabFile>,
    data_reserve: u32,
}

fn read(src: &Src, off: u64, n: usize) -> Result<Vec<u8>, String> {
    let mut b = vec![0u8; n];
    src.read_at(off, &mut b).map_err(|_| "HWVX.cab is damaged".to_string())?;
    Ok(b)
}

impl Cab {
    pub fn open(src: Src) -> Result<Cab, String> {
        let size = src.len();
        if size < 36 {
            return Err("HWVX.cab is not a cabinet".into());
        }
        let h = read(&src, 0, (size.min(40 + 255 + 65535)) as usize)?;
        if &h[0..4] != b"MSCF" {
            return Err("HWVX.cab is not a cabinet".into());
        }
        let (coff_files, n_folders, n_files, flags) = (le32(&h, 16) as u64, le16(&h, 26), le16(&h, 28), le16(&h, 30));
        if flags & 3 != 0 {
            return Err("HWVX.cab is one part of a set: not the disc's".into());
        }
        let (mut first, mut folder_size, mut data_reserve) = (36usize, 8usize, 0u32);
        if flags & 4 != 0 {
            if h.len() < 40 {
                return Err("HWVX.cab is damaged".into());
            }
            folder_size += h[38] as usize;
            data_reserve = h[39] as u32;
            first = 40 + le16(&h, 36) as usize;
        }
        if (first + n_folders as usize * folder_size) as u64 > size {
            return Err("HWVX.cab is damaged".into());
        }
        let fb = read(&src, first as u64, n_folders as usize * folder_size)?;
        let folders = (0..n_folders as usize)
            .map(|i| {
                let a = i * folder_size;
                Folder { data: le32(&fb, a), blocks: le16(&fb, a + 4), kind: le16(&fb, a + 6) & 0xF }
            })
            .collect();
        // the files' table: 16 bytes and a name of up to 256 each
        if coff_files > size {
            return Err("HWVX.cab is damaged".into());
        }
        let t = read(&src, coff_files, ((n_files as u64) * (16 + 257)).min(size - coff_files) as usize)?;
        let mut files = vec![];
        let mut p = 0usize;
        for _ in 0..n_files {
            if t.len() < p + 17 {
                return Err("HWVX.cab is damaged".into());
            }
            let end = t[p + 16..].iter().position(|&c| c == 0).ok_or("HWVX.cab is damaged")?;
            let name = String::from_utf8_lossy(&t[p + 16..p + 16 + end]).into_owned();
            files.push(CabFile { name, size: le32(&t, p), offset: le32(&t, p + 4), folder: le16(&t, p + 8) });
            p += 16 + end + 1;
        }
        for f in &files {
            if (f.folder as usize) < n_folders as usize && !safe_name(&f.name) {
                return Err("HWVX.cab: a damaged file entry".into());
            }
        }
        Ok(Cab { src, folders, files, data_reserve })
    }

    /// Decompress folder `i`, handing each block to `sink` with its offset in the folder's data;
    /// `sink` returns false to stop early.
    pub fn stream(&self, i: usize, sink: &mut Sink) -> Result<(), String> {
        let f = &self.folders[i];
        if f.kind > 1 {
            return Err(format!("HWVX.cab: compression type {} (only MSZIP is read)", f.kind));
        }
        let damaged = || format!("HWVX.cab: a damaged block in folder {i}");
        let mut hist: Vec<u8> = vec![];
        let mut p = f.data as u64;
        let mut pos = 0u64;
        for _ in 0..f.blocks {
            let head = read(&self.src, p, 8).map_err(|_| damaged())?;
            let (cb, unc) = (le16(&head, 4) as usize, le16(&head, 6) as usize);
            let payload = p + 8 + self.data_reserve as u64;
            if unc > 32768 || (f.kind == 1 && cb < 2) {
                return Err(damaged());
            }
            let data = read(&self.src, payload, cb).map_err(|_| damaged())?;
            let out: Vec<u8> = if f.kind == 0 {
                if cb != unc {
                    return Err(damaged());
                }
                data
            } else {
                if &data[0..2] != b"CK" {
                    return Err(damaged());
                }
                // the history before the block, then room for it
                let start = hist.len();
                let mut buf = std::mem::take(&mut hist);
                buf.resize(start + unc, 0);
                let mut r = DecompressorOxide::new();
                let (st, _, n) = decompress(&mut r, &data[2..], &mut buf, start, inflate_flags::TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
                if st != TINFLStatus::Done || n != unc {
                    return Err(damaged());
                }
                let keep = buf.len().saturating_sub(32768);
                hist = buf[keep..].to_vec();
                buf[start..].to_vec()
            };
            if !sink(pos, &out)? {
                return Ok(());
            }
            pos += out.len() as u64;
            p = payload + cb as u64;
        }
        Ok(())
    }

    /// One file's bytes (its folder decompressed up to the file's end).
    pub fn read_file(&self, f: &CabFile) -> Result<Vec<u8>, String> {
        // a folder holds at most 32 KB a block: a larger size is a damaged table, not 4 GB to allocate
        let room = self.folders.get(f.folder as usize).map_or(0, |d| d.blocks as u64 * 32768);
        if f.size > 0 && f.offset as u64 + f.size as u64 > room {
            return Err(format!("HWVX.cab: {} runs past its folder's data", f.name));
        }
        // and the blocks may still be missing: the buffer grows as they arrive
        let mut out = Vec::with_capacity((f.size as usize).min(16 << 20));
        let mut w = Writer { file: f, out: &mut out };
        self.stream_one(f, &mut w)?;
        Ok(out)
    }

    fn stream_one(&self, f: &CabFile, w: &mut Writer) -> Result<(), String> {
        if f.folder as usize >= self.folders.len() {
            return Err(format!("HWVX.cab: {} is in no folder of this cabinet", f.name));
        }
        if f.size == 0 {
            return Ok(());
        }
        self.stream(f.folder as usize, &mut |pos, b| {
            w.take(pos, b);
            Ok(w.out.len() < f.size as usize)
        })?;
        if w.out.len() != f.size as usize {
            return Err(format!("HWVX.cab: {} runs past its folder's data", f.name));
        }
        Ok(())
    }
}

struct Writer<'a> {
    file: &'a CabFile,
    out: &'a mut Vec<u8>,
}

impl Writer<'_> {
    fn take(&mut self, pos: u64, b: &[u8]) {
        if let Some(s) = overlap(self.file, pos, b.len()) {
            self.out.extend_from_slice(&b[s]);
        }
    }
}

/// The part of a block (at `pos`, `n` bytes) that belongs to `f`, as a range of the block.
pub fn overlap(f: &CabFile, pos: u64, n: usize) -> Option<std::ops::Range<usize>> {
    let (a, b) = (f.offset as u64, f.offset as u64 + f.size as u64);
    let (s, e) = (a.max(pos), b.min(pos + n as u64));
    (s < e).then(|| (s - pos) as usize..(e - pos) as usize)
}

#[cfg(any(test, feature = "test-utils"))]
#[allow(dead_code, unused_imports)]
pub(crate) mod tests {
    use super::*;
    use crate::disc::io::Mem;
    use std::sync::Arc;

    /// Bits written LSB first (deflate's order).
    struct Bits {
        out: Vec<u8>,
        acc: u32,
        n: u32,
    }
    impl Bits {
        fn put(&mut self, v: u32, n: u32) {
            for k in 0..n {
                self.acc |= ((v >> k) & 1) << self.n;
                self.n += 1;
                if self.n == 8 {
                    self.out.push(self.acc as u8);
                    self.acc = 0;
                    self.n = 0;
                }
            }
        }
        /// a Huffman code: most significant bit first
        fn code(&mut self, c: u32, n: u32) {
            for k in (0..n).rev() {
                self.put((c >> k) & 1, 1);
            }
        }
        fn finish(mut self) -> Vec<u8> {
            if self.n > 0 {
                self.out.push(self.acc as u8);
            }
            self.out
        }
    }

    /// A final fixed-Huffman deflate block: `lits` as literals, then one match of `len` bytes
    /// `dist` back (len 3..=10: no extra bits; dist 1..=4, or 9..=12: code 6 and two extra bits), then end of block.
    fn fixed_block(lits: &[u8], m: Option<(u32, u32)>) -> Vec<u8> {
        let mut b = Bits { out: vec![], acc: 0, n: 0 };
        b.put(1, 1); // final
        b.put(1, 2); // fixed codes
        for &l in lits {
            if l < 144 { b.code(0x30 + l as u32, 8) } else { b.code(0x190 + (l as u32 - 144), 9) }
        }
        if let Some((len, dist)) = m {
            b.code(len - 3 + 1, 7); // length symbols 257..: 7-bit codes 1..
            if dist <= 4 { b.code(dist - 1, 5) } else { b.code(6, 5); b.put(dist - 9, 2) }
        }
        b.code(0, 7); // 256: end of block
        b.finish()
    }

    /// A cabinet of `files` (name, folder, bytes) in `folders` (kind, blocks of deflate or raw:
    /// (stored bytes, uncompressed size)).
    pub fn make(folders: &[(u16, Vec<(Vec<u8>, usize)>)], files: &[(&str, u16, u32, u32)]) -> Vec<u8> {
        let mut head = vec![0u8; 36];
        head[0..4].copy_from_slice(b"MSCF");
        let n_folders = folders.len();
        let folder_tab = 36 + 8 * n_folders;
        let mut ftab = vec![];
        for (name, folder, off, size) in files {
            let mut e = vec![0u8; 16];
            e[0..4].copy_from_slice(&size.to_le_bytes());
            e[4..8].copy_from_slice(&off.to_le_bytes());
            e[8..10].copy_from_slice(&folder.to_le_bytes());
            e.extend_from_slice(name.as_bytes());
            e.push(0);
            ftab.extend_from_slice(&e);
        }
        let mut data = vec![];
        let data_start = folder_tab + ftab.len();
        let mut fol = vec![];
        for (kind, blocks) in folders {
            fol.extend_from_slice(&((data_start + data.len()) as u32).to_le_bytes());
            fol.extend_from_slice(&(blocks.len() as u16).to_le_bytes());
            fol.extend_from_slice(&kind.to_le_bytes());
            for (stored, unc) in blocks {
                let mut payload = if *kind == 1 { b"CK".to_vec() } else { vec![] };
                payload.extend_from_slice(stored);
                data.extend_from_slice(&0u32.to_le_bytes());
                data.extend_from_slice(&(payload.len() as u16).to_le_bytes());
                data.extend_from_slice(&(*unc as u16).to_le_bytes());
                data.extend_from_slice(&payload);
            }
        }
        head[16..20].copy_from_slice(&(folder_tab as u32).to_le_bytes());
        head[26..28].copy_from_slice(&(n_folders as u16).to_le_bytes());
        head[28..30].copy_from_slice(&(files.len() as u16).to_le_bytes());
        let mut out = head;
        out.extend_from_slice(&fol);
        out.extend_from_slice(&ftab);
        out.extend_from_slice(&data);
        let total = out.len() as u32;
        out[8..12].copy_from_slice(&total.to_le_bytes());
        out
    }

    /// The sample: folder 0 MSZIP in two blocks, the second's match reaching into the first
    /// (shared history); folder 1 stored. Files: "a.xpc" spans both blocks, "b.xpc" the second,
    /// "c.bin" folder 1, "e" empty.
    pub fn sample() -> (Vec<u8>, Vec<(&'static str, Vec<u8>)>) {
        let block1 = miniz_oxide::deflate::compress_to_vec(b"HELLO WORLD ", 6);
        // "abc", then 6 bytes from 12 back: reaching into the first block
        let block2 = fixed_block(b"abc", Some((6, 12)));
        let mut folder0 = b"HELLO WORLD ".to_vec();
        folder0.extend_from_slice(b"abc");
        let l = folder0.len();
        for k in 0..6 {
            let c = folder0[l - 12 + k];
            folder0.push(c);
        }
        let cab = make(
            &[(1, vec![(block1, 12), (block2, 9)]), (0, vec![(b"stored!".to_vec(), 7)])],
            &[("a.xpc", 0, 6, 10), ("b.xpc", 0, 16, 5), ("c.bin", 1, 0, 7), ("e", 0, 21, 0)],
        );
        let want = vec![
            ("a.xpc", folder0[6..16].to_vec()),
            ("b.xpc", folder0[16..21].to_vec()),
            ("c.bin", b"stored!".to_vec()),
            ("e", vec![]),
        ];
        (cab, want)
    }

    #[test]
    fn reads_mszip_with_shared_history() {
        let (img, want) = sample();
        let cab = Cab::open(Arc::new(Mem(img))).unwrap();
        assert_eq!(cab.files.len(), 4);
        for (f, (name, bytes)) in cab.files.iter().zip(&want) {
            assert_eq!(&f.name, name);
            assert_eq!(&cab.read_file(f).unwrap(), bytes, "{name}");
        }
    }

    #[test]
    fn refuses_lzx_and_sets() {
        let mut img = make(&[(3, vec![(vec![0; 4], 4)])], &[("x", 0, 0, 4)]);
        let cab = Cab::open(Arc::new(Mem(img.clone()))).unwrap();
        assert!(cab.read_file(&cab.files[0]).unwrap_err().contains("compression type 3"));
        img[30] = 1;
        assert!(Cab::open(Arc::new(Mem(img))).is_err());
    }

    #[test]
    fn a_damaged_block_is_an_error() {
        let (mut img, _) = sample();
        let at = img.windows(2).rposition(|w| w == b"CK").unwrap();
        img[at] = b'X';
        let cab = Cab::open(Arc::new(Mem(img))).unwrap();
        assert!(cab.read_file(&cab.files[1]).is_err());
    }
}
