//! xdl_mod's block hashes: a polynomial over the bytes mod 2^64, so every window of a mod file
//! can be hashed and looked up among the base's aligned blocks.

pub const FINE: usize = 32;
pub const COARSE: usize = 256;
const Q: u64 = 0x9E3779B97F4A7C15;

fn qinv() -> u64 {
    // Newton's iteration for the inverse mod 2^64 (Q is odd)
    let mut x: u64 = Q;
    for _ in 0..6 {
        x = x.wrapping_mul(2u64.wrapping_sub(Q.wrapping_mul(x)));
    }
    x
}

fn min_distinct(b: usize) -> usize {
    if b == FINE { 12 } else { 24 }
}

fn powers(b: usize) -> Vec<u64> {
    let mut p = vec![1u64; b.max(1)];
    for i in 1..b {
        p[i] = p[i - 1].wrapping_mul(Q);
    }
    p
}

/// _trivial for one row: few distinct bytes, or an arithmetic progression of 1, 2 or 4-byte
/// values (either byte order).
fn trivial(row: &[u8], md: usize) -> bool {
    let mut seen = [0u64; 4];
    let mut n = 0;
    for &x in row {
        let (w, bit) = ((x >> 6) as usize, 1u64 << (x & 63));
        if seen[w] & bit == 0 {
            seen[w] |= bit;
            n += 1;
        }
    }
    if n < md {
        return true;
    }
    fn prog(v: impl Iterator<Item = i64>) -> bool {
        let v: Vec<i64> = v.collect();
        v.windows(3).all(|w| (w[2] - w[1]) - (w[1] - w[0]) == 0)
    }
    prog(row.iter().map(|&x| x as i64))
        || prog(row.chunks(2).map(|c| u16::from_be_bytes([c[0], c[1]]) as i64))
        || prog(row.chunks(2).map(|c| u16::from_le_bytes([c[0], c[1]]) as i64))
        || prog(row.chunks(4).map(|c| u32::from_be_bytes([c[0], c[1], c[2], c[3]]) as i64))
        || prog(row.chunks(4).map(|c| u32::from_le_bytes([c[0], c[1], c[2], c[3]]) as i64))
}

/// block_hashes: hashes of the aligned B-byte blocks of buf worth indexing.
pub fn block_hashes(buf: &[u8], b: usize) -> Vec<u64> {
    let pw = powers(b);
    let md = min_distinct(b);
    buf.chunks_exact(b)
        .filter(|c| !trivial(c, md))
        .map(|c| c.iter().zip(&pw).fold(0u64, |h, (&x, &p)| h.wrapping_add((x as u64).wrapping_mul(p))))
        .collect()
}

/// window_hashes: the hash of buf[i..i+B] for every i.
pub fn window_hashes(buf: &[u8], b: usize) -> Vec<u64> {
    let n = buf.len();
    if n < b {
        return vec![];
    }
    let pw = powers(b);
    let top = pw[b - 1];
    let qi = qinv();
    let mut h = buf[..b].iter().zip(&pw).fold(0u64, |h, (&x, &p)| h.wrapping_add((x as u64).wrapping_mul(p)));
    let mut out = Vec::with_capacity(n - b + 1);
    out.push(h);
    for i in 0..n - b {
        h = h.wrapping_sub(buf[i] as u64).wrapping_mul(qi).wrapping_add((buf[i + b] as u64).wrapping_mul(top));
        out.push(h);
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn windows_are_blocks() {
        let buf: Vec<u8> = (0..1000u32).map(|i| (i.wrapping_mul(2654435761) >> 13) as u8).collect();
        let w = window_hashes(&buf, FINE);
        let pw = powers(FINE);
        for i in [0usize, 1, 7, 500, 968] {
            let h = buf[i..i + FINE].iter().zip(&pw).fold(0u64, |h, (&x, &p)| h.wrapping_add((x as u64).wrapping_mul(p)));
            assert_eq!(w[i], h);
        }
        assert_eq!(Q.wrapping_mul(qinv()), 1);
        assert!(trivial(&[5u8; 32], 12));
        let ramp: Vec<u8> = (0..32).collect();
        assert!(trivial(&ramp, 12));
    }
}
