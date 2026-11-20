//! Random access to a disc image, read only: a file, a part of one, a raw-sector CD's 2048-byte
//! view, a block in memory (tests). Everything is read at an offset as it is needed; nothing reads
//! a whole image.

use std::fs::File;
use std::io::{self, Read, Seek, SeekFrom, Write};
use std::path::Path;
use std::sync::{Arc, Mutex};

/// Bytes read at any offset (shared between readers: one lock per file).
pub trait ReadAt: Send + Sync {
    fn len(&self) -> u64;
    /// Exactly `buf.len()` bytes at `off`; an error past the end.
    fn read_at(&self, off: u64, buf: &mut [u8]) -> io::Result<()>;
}

pub type Src = Arc<dyn ReadAt>;

pub(crate) fn past_end() -> io::Error {
    io::Error::new(io::ErrorKind::UnexpectedEof, "past the end of the image (a trimmed or damaged image?)")
}

fn inside(len: u64, off: u64, n: usize) -> io::Result<()> {
    if off > len || n as u64 > len - off { Err(past_end()) } else { Ok(()) }
}

/// A file on disk.
pub struct FileAt {
    f: Mutex<File>,
    len: u64,
}

impl FileAt {
    pub fn open(p: &Path) -> io::Result<FileAt> {
        let f = File::open(p)?;
        let len = f.metadata()?.len();
        Ok(FileAt { f: Mutex::new(f), len })
    }
}

impl ReadAt for FileAt {
    fn len(&self) -> u64 {
        self.len
    }
    fn read_at(&self, off: u64, buf: &mut [u8]) -> io::Result<()> {
        inside(self.len, off, buf.len())?;
        let mut f = self.f.lock().unwrap_or_else(|e| e.into_inner());
        f.seek(SeekFrom::Start(off))?;
        f.read_exact(buf)
    }
}

/// Bytes in memory (the tests' images).
pub struct Mem(pub Vec<u8>);

impl ReadAt for Mem {
    fn len(&self) -> u64 {
        self.0.len() as u64
    }
    fn read_at(&self, off: u64, buf: &mut [u8]) -> io::Result<()> {
        inside(self.len(), off, buf.len())?;
        buf.copy_from_slice(&self.0[off as usize..off as usize + buf.len()]);
        Ok(())
    }
}

/// `len` bytes of `inner` from `base` (a file inside an image).
pub struct Region {
    pub inner: Src,
    pub base: u64,
    pub len: u64,
}

impl ReadAt for Region {
    fn len(&self) -> u64 {
        self.len
    }
    fn read_at(&self, off: u64, buf: &mut [u8]) -> io::Result<()> {
        inside(self.len, off, buf.len())?;
        self.inner.read_at(self.base + off, buf)
    }
}

/// A CD image of raw sectors (MODE1/2352: 16 bytes of sync and header before each sector's 2048
/// bytes of data) as the plain 2048-byte sectors an .iso holds (pcdisc.c OpenDisc).
pub struct RawSectors {
    pub inner: Src,
}

impl ReadAt for RawSectors {
    fn len(&self) -> u64 {
        self.inner.len() / 2352 * 2048
    }
    fn read_at(&self, mut off: u64, buf: &mut [u8]) -> io::Result<()> {
        inside(self.len(), off, buf.len())?;
        let mut done = 0;
        while done < buf.len() {
            let (sector, within) = (off / 2048, off % 2048);
            let n = ((2048 - within) as usize).min(buf.len() - done);
            self.inner.read_at(sector * 2352 + 16 + within, &mut buf[done..done + n])?;
            done += n;
            off += n as u64;
        }
        Ok(())
    }
}

/// True when the image starts with a raw sector's sync pattern (00, ten FF, 00).
pub fn is_raw_cd(src: &dyn ReadAt) -> bool {
    let mut sync = [0u8; 12];
    src.read_at(0, &mut sync).is_ok() && sync[0] == 0 && sync[1..11].iter().all(|&b| b == 0xFF) && sync[11] == 0
}

/// Copy `len` bytes from `off` to `w`, a megabyte at a time.
pub fn copy_range(src: &dyn ReadAt, off: u64, len: u64, w: &mut dyn Write) -> io::Result<()> {
    let mut buf = vec![0u8; (1 << 20).min(len as usize)];
    let mut done = 0u64;
    while done < len {
        let n = buf.len().min((len - done) as usize);
        src.read_at(off + done, &mut buf[..n])?;
        w.write_all(&buf[..n])?;
        done += n as u64;
    }
    Ok(())
}

pub(crate) fn be32(b: &[u8], at: usize) -> u32 {
    u32::from_be_bytes(b[at..at + 4].try_into().unwrap())
}

pub(crate) fn le32(b: &[u8], at: usize) -> u32 {
    u32::from_le_bytes(b[at..at + 4].try_into().unwrap())
}

pub(crate) fn le16(b: &[u8], at: usize) -> u16 {
    u16::from_le_bytes(b[at..at + 2].try_into().unwrap())
}

pub(crate) fn le64(b: &[u8], at: usize) -> u64 {
    u64::from_le_bytes(b[at..at + 8].try_into().unwrap())
}
