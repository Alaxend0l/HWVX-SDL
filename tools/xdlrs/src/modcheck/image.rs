//! Pixels: a PNG read as xdl_media.read_png reads it with Pillow (every colour type and depth,
//! tRNS as Pillow applies it), the exact-pixel hashes, and the 256-bit gradient hash (Pillow's
//! BOX resize of a float image, numpy's float32 std).

use super::hash::h64;

pub struct Rgba {
    pub w: usize,
    pub h: usize,
    pub px: Vec<u8>,
}

/// pixel_hashes: (with alpha, without)
pub fn pixel_hashes(img: &Rgba) -> (u64, u64) {
    let mut head = Vec::with_capacity(8 + img.px.len());
    head.extend_from_slice(&(img.w as u32).to_le_bytes());
    head.extend_from_slice(&(img.h as u32).to_le_bytes());
    let mut a = head.clone();
    a.extend_from_slice(&img.px);
    let mut b = head;
    for p in img.px.chunks_exact(4) {
        b.extend_from_slice(&p[..3]);
    }
    (h64(&a), h64(&b))
}

const DH_W: usize = 17;
const DH_H: usize = 16;
const DH_MIN_STD: f32 = 6.0;

/// Pillow's precompute_coeffs for the BOX filter: per output pixel (first input, weights).
fn box_coeffs(in_size: usize, out_size: usize) -> Vec<(usize, Vec<f64>)> {
    let scale = in_size as f64 / out_size as f64;
    let filterscale = scale.max(1.0);
    let support = 0.5 * filterscale;
    let ss = 1.0 / filterscale;
    let mut out = vec![];
    for xx in 0..out_size {
        let center = (xx as f64 + 0.5) * scale;
        let xmin = ((center - support + 0.5) as i64).max(0) as usize;
        let xmax = ((center + support + 0.5) as i64).min(in_size as i64) as usize;
        let n = xmax.saturating_sub(xmin);
        let mut k = Vec::with_capacity(n);
        let mut ww = 0.0;
        for x in 0..n {
            let t = (x as f64 + xmin as f64 - center + 0.5) * ss;
            let w = if t > -0.5 && t <= 0.5 { 1.0 } else { 0.0 };
            k.push(w);
            ww += w;
        }
        if ww != 0.0 {
            for w in k.iter_mut() {
                *w /= ww;
            }
        }
        out.push((xmin, k));
    }
    out
}

/// Image.fromarray(float32).resize((17, 16), Image.BOX)
fn resize_box(g: &[f32], w: usize, h: usize) -> Vec<f32> {
    let kx = box_coeffs(w, DH_W);
    let ky = box_coeffs(h, DH_H);
    let need_h = w != DH_W;
    let need_v = h != DH_H;
    // the rows the vertical pass uses
    let y_first = ky[0].0;
    let y_last = ky[DH_H - 1].0 + ky[DH_H - 1].1.len();
    let (mut cur, mut cw, mut ch, mut yoff) = (g.to_vec(), w, h, 0usize);
    if need_h {
        let rows = y_last - y_first;
        let mut t = vec![0f32; DH_W * rows];
        for yy in 0..rows {
            for (xx, (xmin, k)) in kx.iter().enumerate() {
                let mut ss = 0.0f64;
                for (x, &kw) in k.iter().enumerate() {
                    ss += g[(yy + y_first) * w + x + xmin] as f64 * kw;
                }
                t[yy * DH_W + xx] = ss as f32;
            }
        }
        cur = t;
        cw = DH_W;
        ch = rows;
        yoff = y_first;
    }
    if need_v {
        let mut o = vec![0f32; cw * DH_H];
        for (yy, (ymin, k)) in ky.iter().enumerate() {
            for xx in 0..cw {
                let mut ss = 0.0f64;
                for (y, &kw) in k.iter().enumerate() {
                    ss += cur[(y + ymin - yoff) * cw + xx] as f64 * kw;
                }
                o[yy * cw + xx] = ss as f32;
            }
        }
        cur = o;
        ch = DH_H;
    }
    let _ = ch;
    cur
}

/// numpy's pairwise sum of float32
fn pairwise(a: &[f32]) -> f32 {
    let n = a.len();
    if n < 8 {
        let mut r = 0.0f32;
        for &x in a {
            r += x;
        }
        r
    } else if n <= 128 {
        let mut r = [0f32; 8];
        r.copy_from_slice(&a[..8]);
        let mut i = 8;
        while i < n - n % 8 {
            for j in 0..8 {
                r[j] += a[i + j];
            }
            i += 8;
        }
        let mut res = ((r[0] + r[1]) + (r[2] + r[3])) + ((r[4] + r[5]) + (r[6] + r[7]));
        while i < n {
            res += a[i];
            i += 1;
        }
        res
    } else {
        let mut n2 = n / 2;
        n2 -= n2 % 8;
        pairwise(&a[..n2]) + pairwise(&a[n2..])
    }
}

/// np.std of a float32 array (float32 throughout)
fn std_f32(a: &[f32]) -> f32 {
    let n = a.len() as f32;
    let mean = pairwise(a) / n;
    let d: Vec<f32> = a.iter().map(|&x| (x - mean) * (x - mean)).collect();
    (pairwise(&d) / n).sqrt()
}

/// dhash: 256-bit gradient hash of the mean of R, G, B (32 bytes, numpy's packbits order), or
/// None for a small or flat image.
pub fn dhash(img: &Rgba) -> Option<[u8; 32]> {
    let (w, h) = (img.w, img.h);
    if w < 16 || h < 16 {
        return None;
    }
    let g: Vec<f32> = img.px.chunks_exact(4).map(|p| (p[0] as f32 + p[1] as f32 + p[2] as f32) / 3.0f32).collect();
    let s = resize_box(&g, w, h);
    if std_f32(&s) < DH_MIN_STD {
        return None;
    }
    let mut out = [0u8; 32];
    let mut k = 0;
    for y in 0..DH_H {
        for x in 0..DH_W - 1 {
            if s[y * DH_W + x + 1] > s[y * DH_W + x] {
                out[k / 8] |= 0x80 >> (k % 8);
            }
            k += 1;
        }
    }
    Some(out)
}

/// The most pixels a mod's PNG may have: 16 MP, four times the largest texture the game
/// loads (PORT_MAX_TEX_SIZE 2048 x 2048).
pub const MAX_PIXELS: u64 = 4096 * 4096;

/// read_png: RGBA as Pillow gives it (`Image.open(p).convert("RGBA")`, palette PNGs through
/// their palette and tRNS). Only PNG: Pillow would open other formats too (a JPEG named .png).
pub fn read_png(data: &[u8]) -> Result<Rgba, String> {
    // refuse a huge image from its header, before the decoder allocates anything: a tiny 1-bit
    // PNG can claim 16384 x 16384 pixels (the png crate's limit caps only the packed rows)
    if data.len() >= 24 && &data[..8] == b"\x89PNG\r\n\x1a\n" && &data[12..16] == b"IHDR" {
        let w = u32::from_be_bytes(data[16..20].try_into().unwrap()) as u64;
        let h = u32::from_be_bytes(data[20..24].try_into().unwrap()) as u64;
        if w * h > MAX_PIXELS {
            return Err(format!("image file is too large ({w} x {h} pixels; the limit is {MAX_PIXELS})"));
        }
    }
    let mut dec = png::Decoder::new(std::io::Cursor::new(data));
    dec.set_transformations(png::Transformations::IDENTITY);
    dec.ignore_checksums(false);
    let mut r = dec.read_info().map_err(|e| format!("cannot identify image file ({e})"))?;
    let size = r.output_buffer_size().ok_or("image too large")?;
    let mut buf = vec![0u8; size];
    let fi = r.next_frame(&mut buf).map_err(|e| format!("image file is truncated or broken ({e})"))?;
    let info = r.info();
    let (w, h) = (fi.width as usize, fi.height as usize);
    let depth = fi.bit_depth as u8 as usize;
    let line = fi.line_size;
    let trns = info.trns.as_ref().map(|t| t.to_vec());
    let plte = info.palette.as_ref().map(|p| p.to_vec());
    let mut px = vec![0u8; w * h * 4];
    // the sample (raw value) at index i of a row
    let sample = |row: &[u8], i: usize| -> u32 {
        match depth {
            16 => u16::from_be_bytes([row[2 * i], row[2 * i + 1]]) as u32,
            8 => row[i] as u32,
            d => {
                let bit = i * d;
                ((row[bit / 8] >> (8 - d - bit % 8)) as u32) & ((1 << d) - 1)
            }
        }
    };
    // Pillow compares the tRNS value's low byte (a C cast) with the 8-bit pixel; the png crate
    // keeps only that byte below 16 bits
    let t16 = |k: usize| {
        trns.as_ref().and_then(|t| if depth == 16 { t.get(2 * k + 1).copied() } else { t.get(k).copied() }).map(|b| b as u32)
    };
    use png::ColorType as C;
    for y in 0..h {
        let row = &buf[y * line..(y + 1) * line];
        for x in 0..w {
            let o = 4 * (y * w + x);
            let hi = |v: u32| if depth == 16 { (v >> 8) as u8 } else { v as u8 };
            let p: [u8; 4] = match fi.color_type {
                C::Grayscale => {
                    let v = sample(row, x);
                    let l = match depth {
                        1 => if v != 0 { 255 } else { 0 },
                        2 => v * 85,
                        4 => v * 17,
                        8 => v,
                        _ => v.min(255),
                    } as u8;
                    let a = if trns.is_some() && t16(0) == Some(l as u32) { 0 } else { 255 };
                    [l, l, l, a]
                }
                C::Rgb => {
                    let c = [hi(sample(row, 3 * x)), hi(sample(row, 3 * x + 1)), hi(sample(row, 3 * x + 2))];
                    let m = (0..3).all(|k| t16(k) == Some(c[k] as u32));
                    [c[0], c[1], c[2], if trns.is_some() && m { 0 } else { 255 }]
                }
                C::GrayscaleAlpha => {
                    let l = hi(sample(row, 2 * x));
                    [l, l, l, hi(sample(row, 2 * x + 1))]
                }
                C::Rgba => [hi(sample(row, 4 * x)), hi(sample(row, 4 * x + 1)), hi(sample(row, 4 * x + 2)), hi(sample(row, 4 * x + 3))],
                C::Indexed => {
                    let i = sample(row, x) as usize;
                    let pl = plte.as_deref().unwrap_or(&[]);
                    let rgb = if 3 * i + 3 <= pl.len() { [pl[3 * i], pl[3 * i + 1], pl[3 * i + 2]] } else { [0, 0, 0] };
                    let a = trns.as_ref().and_then(|t| t.get(i).copied()).unwrap_or(255);
                    [rgb[0], rgb[1], rgb[2], a]
                }
            };
            px[o..o + 4].copy_from_slice(&p);
        }
    }
    Ok(Rgba { w, h, px })
}
