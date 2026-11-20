//! Dolphin's compressed GameCube image (.gcz, "CompressedBlob"), read as the plain disc it holds.
//!
//! The port itself refuses a .gcz (disc.c: "convert it to .iso first"); the ripper reads one, so
//! a player's .gcz can be extracted without converting it. Layout (little-endian, Dolphin
//! Source/Core/DiscIO/CompressedBlob.h): a 32-byte header (magic 0xB10BC001, sub type, compressed
//! data size, data size, block size, block count), a u64 per block (its offset in the data that
//! follows; the top bit set: stored uncompressed), a u32 per block (Adler-32 of the stored bytes),
//! then the blocks, each one zlib stream (or raw). One block is decompressed at a time and the
//! last one is kept.

use super::io::{ReadAt, Src, le32, le64, past_end};
use std::io;
use std::sync::Mutex;

pub const MAGIC: u32 = 0xB10B_C001;

pub struct Gcz {
    file: Src,
    data_size: u64,
    block_size: u32,
    comp_size: u64,
    data_off: u64,
    ptrs: Vec<u64>,
    hashes: Vec<u32>,
    last: Mutex<Option<(usize, Vec<u8>)>>,
}

fn bad(why: String) -> io::Error {
    io::Error::new(io::ErrorKind::InvalidData, why)
}

pub fn adler32(b: &[u8]) -> u32 {
    let (mut a, mut s) = (1u32, 0u32);
    for chunk in b.chunks(5552) {
        for &x in chunk {
            a += x as u32;
            s += a;
        }
        a %= 65521;
        s %= 65521;
    }
    s << 16 | a
}

impl Gcz {
    pub fn open(file: Src) -> Result<Gcz, String> {
        let mut h = [0u8; 32];
        file.read_at(0, &mut h).map_err(|_| "too small to be a .gcz image".to_string())?;
        if le32(&h, 0) != MAGIC {
            return Err("not a .gcz image".into());
        }
        let (comp_size, data_size, block_size, n) = (le64(&h, 8), le64(&h, 16), le32(&h, 24), le32(&h, 28) as u64);
        if block_size == 0 || block_size > 64 << 20 || n > 1 << 28 || n * (block_size as u64) < data_size || n == 0 {
            return Err("the .gcz header is damaged".into());
        }
        let data_off = 32 + n * 12;
        if data_off > file.len() || comp_size > file.len() - data_off {
            return Err("the .gcz image is cut short".into());
        }
        let mut t = vec![0u8; (n * 12) as usize];
        file.read_at(32, &mut t).map_err(|e| format!("the .gcz block table: {e}"))?;
        let ptrs = (0..n as usize).map(|i| le64(&t, i * 8)).collect();
        let hashes = (0..n as usize).map(|i| le32(&t, (n as usize) * 8 + i * 4)).collect();
        Ok(Gcz { file, data_size, block_size, comp_size, data_off, ptrs, hashes, last: Mutex::new(None) })
    }

    pub fn block_size(&self) -> u32 {
        self.block_size
    }

    fn block(&self, i: usize) -> io::Result<Vec<u8>> {
        const RAW: u64 = 1 << 63;
        let start = self.ptrs[i] & !RAW;
        let end = if i + 1 < self.ptrs.len() { self.ptrs[i + 1] & !RAW } else { self.comp_size };
        if end < start || end > self.comp_size || end - start > self.block_size as u64 + 1024 {
            return Err(bad(format!(".gcz block {i}: a damaged block table")));
        }
        let mut stored = vec![0u8; (end - start) as usize];
        self.file.read_at(self.data_off + start, &mut stored)?;
        if adler32(&stored) != self.hashes[i] {
            return Err(bad(format!(".gcz block {i}: checksum mismatch (a damaged image)")));
        }
        let want = (self.data_size - i as u64 * self.block_size as u64).min(self.block_size as u64) as usize;
        let mut out = if self.ptrs[i] & RAW != 0 {
            stored
        } else {
            miniz_oxide::inflate::decompress_to_vec_zlib_with_limit(&stored, self.block_size as usize)
                .map_err(|e| bad(format!(".gcz block {i}: {e:?}")))?
        };
        if out.len() < want {
            return Err(bad(format!(".gcz block {i}: {} bytes, not {want}", out.len())));
        }
        out.truncate(want);
        Ok(out)
    }
}

impl ReadAt for Gcz {
    fn len(&self) -> u64 {
        self.data_size
    }
    fn read_at(&self, mut off: u64, buf: &mut [u8]) -> io::Result<()> {
        if off > self.data_size || buf.len() as u64 > self.data_size - off {
            return Err(past_end());
        }
        let bs = self.block_size as u64;
        let mut done = 0;
        let mut last = self.last.lock().unwrap_or_else(|e| e.into_inner());
        while done < buf.len() {
            let i = (off / bs) as usize;
            if last.as_ref().map(|l| l.0) != Some(i) {
                *last = Some((i, self.block(i)?));
            }
            let b = &last.as_ref().unwrap().1;
            let within = (off % bs) as usize;
            let n = (b.len() - within).min(buf.len() - done);
            buf[done..done + n].copy_from_slice(&b[within..within + n]);
            done += n;
            off += n as u64;
        }
        Ok(())
    }
}

#[cfg(any(test, feature = "test-utils"))]
#[allow(dead_code, unused_imports)]
pub(crate) mod tests {
    use super::*;
    use crate::disc::io::Mem;
    use std::sync::Arc;

    /// A .gcz of `data` in blocks of `bs`: odd blocks stored raw, the others zlib.
    pub fn make(data: &[u8], bs: usize) -> Vec<u8> {
        let blocks: Vec<&[u8]> = data.chunks(bs).collect();
        let mut stored = vec![];
        let mut ptrs = vec![];
        let mut hashes = vec![];
        for (i, b) in blocks.iter().enumerate() {
            let mut padded = b.to_vec();
            padded.resize(bs, 0);
            let (s, raw) = if i % 2 == 1 { (padded, true) } else { (miniz_oxide::deflate::compress_to_vec_zlib(&padded, 6), false) };
            ptrs.push(stored.len() as u64 | if raw { 1 << 63 } else { 0 });
            hashes.push(adler32(&s));
            stored.extend_from_slice(&s);
        }
        let mut out = vec![];
        out.extend_from_slice(&MAGIC.to_le_bytes());
        out.extend_from_slice(&0u32.to_le_bytes());
        out.extend_from_slice(&(stored.len() as u64).to_le_bytes());
        out.extend_from_slice(&(data.len() as u64).to_le_bytes());
        out.extend_from_slice(&(bs as u32).to_le_bytes());
        out.extend_from_slice(&(blocks.len() as u32).to_le_bytes());
        for p in ptrs {
            out.extend_from_slice(&p.to_le_bytes());
        }
        for h in hashes {
            out.extend_from_slice(&h.to_le_bytes());
        }
        out.extend_from_slice(&stored);
        out
    }

    #[test]
    fn reads_across_blocks() {
        let data: Vec<u8> = (0..10_000u32).map(|i| (i * 7 % 251) as u8).collect();
        let g = Gcz::open(Arc::new(Mem(make(&data, 1024)))).unwrap();
        assert_eq!(g.len(), data.len() as u64);
        let mut buf = vec![0u8; 3000];
        g.read_at(1000, &mut buf).unwrap();
        assert_eq!(buf, &data[1000..4000]);
        let mut tail = vec![0u8; 16];
        g.read_at(data.len() as u64 - 16, &mut tail).unwrap();
        assert_eq!(tail, &data[data.len() - 16..]);
        assert!(g.read_at(data.len() as u64 - 1, &mut [0u8; 2]).is_err());
    }

    #[test]
    fn damaged_block_is_refused() {
        let data = vec![5u8; 4096];
        let mut img = make(&data, 1024);
        let at = img.len() - 3;
        img[at] ^= 0xFF;
        let g = Gcz::open(Arc::new(Mem(img))).unwrap();
        assert!(g.read_at(4000, &mut [0u8; 8]).is_err());
        assert!(g.read_at(0, &mut [0u8; 8]).is_ok());
    }
}
