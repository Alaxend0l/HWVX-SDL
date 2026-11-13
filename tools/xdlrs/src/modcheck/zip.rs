//! Zip files as Python's zipfile writes them (entries stored, fixed dates: the same files give
//! the same bytes), a reader for stored entries (zip64 sizes too), and numpy's .npy / .npz so
//! the index is the same file format the Python reads and writes.

use super::hash::crc32;
use std::collections::BTreeMap;

/// zipfile.ZipFile(path, "w", ZIP_STORED) with ZipInfo(name, (1980, 1, 1, 0, 0, 0)),
/// external_attr 0o644 << 16, written in the given order. Past what fits without zip64 (where
/// Python's zipfile switches to zip64: an entry over ZIP64_LIMIT / 1.05, an offset or the
/// central directory over ZIP64_LIMIT, 0xFFFF entries or more) it is an error, so every file
/// written is the bytes Python writes.
pub fn write_stored(entries: &[(String, &[u8])]) -> Result<Vec<u8>, String> {
    if entries.len() >= 0xFFFF {
        return Err(format!("{} files: a package holds at most {}", entries.len(), 0xFFFF - 1));
    }
    let mut out = vec![];
    let mut central = vec![];
    let (dosdate, dostime) = ((1u16 << 5) | 1, 0u16); // 1980-01-01 00:00:00
    for (name, data) in entries {
        let flag: u16 = if name.is_ascii() { 0 } else { 0x800 };
        if zip64_size(data.len() as u64) || out.len() as u64 > ZIP64_LIMIT || name.len() > 0xFFFF {
            return Err(format!("{name}: too large for a package ({} bytes at offset {})", data.len(), out.len()));
        }
        let crc = crc32(data);
        let off = out.len() as u32;
        let n = name.as_bytes();
        out.extend_from_slice(b"PK\x03\x04");
        out.extend_from_slice(&[20, 0]);
        for v in [flag, 0, dostime, dosdate] {
            out.extend_from_slice(&v.to_le_bytes());
        }
        for v in [crc, data.len() as u32, data.len() as u32] {
            out.extend_from_slice(&v.to_le_bytes());
        }
        out.extend_from_slice(&(n.len() as u16).to_le_bytes());
        out.extend_from_slice(&0u16.to_le_bytes());
        out.extend_from_slice(n);
        out.extend_from_slice(data);
        central.extend_from_slice(b"PK\x01\x02");
        central.extend_from_slice(&[20, 3, 20, 0]);
        for v in [flag, 0, dostime, dosdate] {
            central.extend_from_slice(&v.to_le_bytes());
        }
        for v in [crc, data.len() as u32, data.len() as u32] {
            central.extend_from_slice(&v.to_le_bytes());
        }
        for v in [n.len() as u16, 0, 0, 0, 0] {
            central.extend_from_slice(&v.to_le_bytes());
        }
        central.extend_from_slice(&(0o644u32 << 16).to_le_bytes());
        central.extend_from_slice(&off.to_le_bytes());
        central.extend_from_slice(n);
    }
    if out.len() as u64 > ZIP64_LIMIT || central.len() as u64 > ZIP64_LIMIT {
        return Err(format!("a package of {} bytes is too large", out.len() + central.len()));
    }
    let cd_off = out.len() as u32;
    out.extend_from_slice(&central);
    out.extend_from_slice(b"PK\x05\x06");
    let cnt = entries.len() as u16;
    for v in [0u16, 0, cnt, cnt] {
        out.extend_from_slice(&v.to_le_bytes());
    }
    out.extend_from_slice(&(central.len() as u32).to_le_bytes());
    out.extend_from_slice(&cd_off.to_le_bytes());
    out.extend_from_slice(&0u16.to_le_bytes());
    Ok(out)
}

/// zipfile.ZIP64_LIMIT
const ZIP64_LIMIT: u64 = (1 << 31) - 1;

/// where Python's zipfile writes an entry with zip64 extras: its size (x 1.05, as
/// ZipFile._open_to_write guesses) past ZIP64_LIMIT
fn zip64_size(n: u64) -> bool {
    n as f64 * 1.05 > ZIP64_LIMIT as f64
}

fn le16(b: &[u8], o: usize) -> Result<usize, String> {
    b.get(o..o + 2).map(|x| u16::from_le_bytes([x[0], x[1]]) as usize).ok_or_else(|| "truncated".to_string())
}
fn le32(b: &[u8], o: usize) -> Result<u64, String> {
    b.get(o..o + 4).map(|x| u32::from_le_bytes(x.try_into().unwrap()) as u64).ok_or_else(|| "truncated".to_string())
}
fn le64(b: &[u8], o: usize) -> Result<u64, String> {
    b.get(o..o + 8).map(|x| u64::from_le_bytes(x.try_into().unwrap())).ok_or_else(|| "truncated".to_string())
}

/// The stored entries of a zip: {name: (data offset, size)}. Compressed entries are an error.
pub fn read_stored(z: &[u8]) -> Result<BTreeMap<String, (usize, usize)>, String> {
    let eocd = (0..z.len().saturating_sub(21)).rev().find(|&i| &z[i..i + 4] == b"PK\x05\x06").ok_or("File is not a zip file")?;
    let mut count = le16(z, eocd + 10)? as u64;
    let mut cd = le32(z, eocd + 16)?;
    if (count == 0xFFFF || cd == 0xFFFF_FFFF) && eocd >= 20 && &z[eocd - 20..eocd - 16] == b"PK\x06\x07" {
        let r = le64(z, eocd - 12)? as usize;
        if z.get(r..r + 4) == Some(b"PK\x06\x06") {
            count = le64(z, r + 32)?;
            cd = le64(z, r + 48)?;
        }
    }
    let mut out = BTreeMap::new();
    let mut p = cd as usize;
    for _ in 0..count {
        if z.get(p..p + 4) != Some(b"PK\x01\x02") {
            return Err("bad central directory".into());
        }
        let method = le16(z, p + 10)?;
        let mut csize = le32(z, p + 20)?;
        let mut usize_ = le32(z, p + 24)?;
        let (nl, xl, cl) = (le16(z, p + 28)?, le16(z, p + 30)?, le16(z, p + 32)?);
        let mut off = le32(z, p + 42)?;
        let name = String::from_utf8_lossy(z.get(p + 46..p + 46 + nl).ok_or("truncated")?).into_owned();
        // zip64 extra: the fields that are 0xFFFFFFFF, in order
        let mut x = p + 46 + nl;
        let xe = x + xl;
        while x + 4 <= xe {
            let (id, len) = (le16(z, x)?, le16(z, x + 2)?);
            if id == 1 {
                let mut q = x + 4;
                if usize_ == 0xFFFF_FFFF {
                    usize_ = le64(z, q)?;
                    q += 8;
                }
                if csize == 0xFFFF_FFFF {
                    csize = le64(z, q)?;
                    q += 8;
                }
                if off == 0xFFFF_FFFF {
                    off = le64(z, q)?;
                }
            }
            x += 4 + len;
        }
        if method != 0 {
            return Err(format!("{name}: compressed (a package's entries are stored)"));
        }
        let lo = off as usize;
        if z.get(lo..lo + 4) != Some(b"PK\x03\x04") {
            return Err("bad local header".into());
        }
        let data = lo + 30 + le16(z, lo + 26)? + le16(z, lo + 28)?;
        if data + usize_ as usize > z.len() {
            return Err("truncated".into());
        }
        let _ = csize;
        out.insert(name, (data, usize_ as usize));
        p += 46 + nl + xl + cl;
    }
    Ok(out)
}

/// One .npy array: dtype ('<u8', '<u4', '|u1', '<u2'), shape, raw little-endian bytes.
pub struct Npy {
    pub descr: String,
    pub shape: Vec<usize>,
    pub data: Vec<u8>,
}

pub fn npy_bytes(descr: &str, shape: &[usize], data: &[u8]) -> Vec<u8> {
    let sh = match shape.len() {
        1 => format!("({},)", shape[0]),
        _ => format!("({})", shape.iter().map(|s| s.to_string()).collect::<Vec<_>>().join(", ")),
    };
    let mut h = format!("{{'descr': '{descr}', 'fortran_order': False, 'shape': {sh}, }}");
    // magic (6) + version (2) + length (2) + header + '\n', padded to 64
    let total = 10 + h.len() + 1;
    h.push_str(&" ".repeat((64 - total % 64) % 64));
    h.push('\n');
    let mut out = b"\x93NUMPY\x01\x00".to_vec();
    out.extend_from_slice(&(h.len() as u16).to_le_bytes());
    out.extend_from_slice(h.as_bytes());
    out.extend_from_slice(data);
    out
}

pub fn npy_parse(b: &[u8]) -> Result<Npy, String> {
    if b.len() < 10 || &b[..6] != b"\x93NUMPY" {
        return Err("not a .npy array".into());
    }
    let (hl, start) = if b[6] == 1 { (le16(b, 8)?, 10) } else { (le32(b, 8)? as usize, 12) };
    let h = std::str::from_utf8(b.get(start..start + hl).ok_or("truncated")?).map_err(|e| e.to_string())?;
    let descr = h.split("'descr':").nth(1).and_then(|s| s.split('\'').nth(1)).ok_or("no descr")?.to_string();
    if h.contains("'fortran_order': True") {
        return Err("Fortran-ordered array".into());
    }
    let sh = h.split("'shape':").nth(1).and_then(|s| s.split('(').nth(1)).and_then(|s| s.split(')').next()).ok_or("no shape")?;
    let shape: Vec<usize> = sh.split(',').map(str::trim).filter(|s| !s.is_empty()).map(|s| s.parse().map_err(|_| "bad shape".to_string())).collect::<Result<_, _>>()?;
    Ok(Npy { descr, shape, data: b[start + hl..].to_vec() })
}

pub fn u64s(n: &Npy) -> Result<Vec<u64>, String> {
    match n.descr.as_str() {
        "<u8" => Ok(n.data.chunks_exact(8).map(|c| u64::from_le_bytes(c.try_into().unwrap())).collect()),
        d => Err(format!("dtype {d}, not uint64")),
    }
}

pub fn u32s(n: &Npy) -> Result<Vec<u32>, String> {
    match n.descr.as_str() {
        "<u4" => Ok(n.data.chunks_exact(4).map(|c| u32::from_le_bytes(c.try_into().unwrap())).collect()),
        "<u8" | "<i8" => Ok(n.data.chunks_exact(8).map(|c| u64::from_le_bytes(c.try_into().unwrap()) as u32).collect()),
        d => Err(format!("dtype {d}, not uint32")),
    }
}

pub fn u16s(n: &Npy) -> Result<Vec<u16>, String> {
    match n.descr.as_str() {
        "<u2" => Ok(n.data.chunks_exact(2).map(|c| u16::from_le_bytes(c.try_into().unwrap())).collect()),
        d => Err(format!("dtype {d}, not uint16")),
    }
}

#[cfg(test)]
mod tests {
    #[test]
    fn npy_roundtrip() {
        let b = super::npy_bytes("<u8", &[2], &[1, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0]);
        assert_eq!(b.len() % 64, 16);
        let n = super::npy_parse(&b).unwrap();
        assert_eq!(super::u64s(&n).unwrap(), vec![1, 2]);
        let z = super::write_stored(&[("a.npy".into(), &b[..])]).unwrap();
        let e = super::read_stored(&z).unwrap();
        assert_eq!(&z[e["a.npy"].0..e["a.npy"].0 + e["a.npy"].1], &b[..]);
    }
}

#[cfg(test)]
mod limits {
    use super::*;

    #[test]
    fn past_the_zip_limits_is_an_error() {
        // 0xFFFE entries fit; 0xFFFF would need zip64's end record (the count was cut to u16)
        let names: Vec<String> = (0..0xFFFF).map(|i| format!("{i}")).collect();
        let e: Vec<(String, &[u8])> = names.iter().map(|n| (n.clone(), &b""[..])).collect();
        let z = write_stored(&e[..0xFFFE]).unwrap();
        assert_eq!(read_stored(&z).unwrap().len(), 0xFFFE);
        assert!(write_stored(&e).unwrap_err().contains("at most"));
        // sizes: where Python's zipfile goes zip64
        assert!(!zip64_size(2_000_000_000));
        assert!(zip64_size(2_100_000_000));
        assert!(zip64_size(u32::MAX as u64 + 1));
        let long = "n".repeat(0x10000);
        assert!(write_stored(&[(long, &b""[..])]).unwrap_err().contains("too large"));
    }
}
