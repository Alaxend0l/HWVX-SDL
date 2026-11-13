//! Button prompts' art (docs/plans/v0.4.1.md A2): the glyphs the port draws in place of a
//! release's button atlas cells (src/game/Port_Prompts.c), from "The Meritite Union's Input
//! Prompts" 2.0.0 (CC0 1.0, https://github.com/meritite-union/input-prompts).
//!
//! - `sheet PACK OUT.png`: a contact sheet of every style's glyphs in several of the pack's looks
//!   (theme, colour or monochrome, fill), one row per look (`LOOKS`), to choose from.
//! - `build SRC OUT.bin`: assets/prompts/src's PNGs (the chosen look, copied from the pack as
//!   they are) into prompts.bin, the table the game embeds: "HWVXPRM1", u32 count, u32 cell,
//!   then per glyph a 16-byte name and cell x cell RGBA8, top row first.
//! - `check SRC BIN`: BIN is what `build` makes of SRC, and every PNG's sha256 is the one
//!   SRC/../README.md gives (the provenance table).

use sha2::{Digest, Sha256};
use std::path::Path;

pub type R<T> = std::result::Result<T, String>;

/// A glyph cell's size in prompts.bin: the 128 px atlas's 32 px cell times 4 (the port's
/// replacement atlases are 512 x 512).
pub const CELL: usize = 128;

/// The glyphs: name in prompts.bin (and SRC's file name, .png), path in the pack's png_256
/// with {t} the theme, {c} `color` or `_` (monochrome), {f} the fill.
pub const GLYPHS: [(&str, &str); 15] = [
    ("xbox_a", "xbox/{t}/button/primary/{c}/{f}/a.png"),
    ("xbox_b", "xbox/{t}/button/primary/{c}/{f}/b.png"),
    ("xbox_x", "xbox/{t}/button/primary/{c}/{f}/x.png"),
    ("xbox_y", "xbox/{t}/button/primary/{c}/{f}/y.png"),
    ("ps_cross", "ps/{t}/button/primary/{c}/{f}/cross.png"),
    ("ps_circle", "ps/{t}/button/primary/{c}/{f}/circle.png"),
    ("ps_square", "ps/{t}/button/primary/{c}/{f}/square.png"),
    ("ps_triangle", "ps/{t}/button/primary/{c}/{f}/triangle.png"),
    ("switch_a", "ns/{t}/button/primary/_/{f}/a.png"),
    ("switch_b", "ns/{t}/button/primary/_/{f}/b.png"),
    ("switch_x", "ns/{t}/button/primary/_/{f}/x.png"),
    ("switch_y", "ns/{t}/button/primary/_/{f}/y.png"),
    ("key_enter", "pc/{t}/key/special/_/{f}/enter.png"),
    ("key_escape", "pc/{t}/key/special/_/{f}/escape.png"),
    ("key_x", "pc/{t}/key/alphanum/letter/{f}/x.png"),
];

/// The contact sheet's rows: (theme, colour, fill). The first is the default look.
pub const LOOKS: [(&str, bool, &str); 7] = [
    ("dark", true, "solid"),
    ("dark", true, "border"),
    ("dark", true, "flat"),
    ("dark", false, "solid"),
    ("dark", false, "flat"),
    ("dark", false, "none"),
    ("light", true, "solid"),
];

pub fn glyph_path(pattern: &str, theme: &str, color: bool, fill: &str) -> String {
    pattern.replace("{t}", theme).replace("{c}", if color { "color" } else { "_" }).replace("{f}", fill)
}

/// RGBA8 pixels.
pub struct Img {
    pub w: usize,
    pub h: usize,
    pub px: Vec<u8>,
}

pub fn read_png(path: &Path) -> R<Img> {
    let f = std::fs::File::open(path).map_err(|e| format!("{}: {e}", path.display()))?;
    let mut dec = png::Decoder::new(std::io::BufReader::new(f));
    dec.set_transformations(png::Transformations::EXPAND | png::Transformations::STRIP_16);
    let mut rd = dec.read_info().map_err(|e| format!("{}: {e}", path.display()))?;
    let mut buf = vec![0; rd.output_buffer_size().ok_or("png too large")?];
    let info = rd.next_frame(&mut buf).map_err(|e| format!("{}: {e}", path.display()))?;
    let (w, h) = (info.width as usize, info.height as usize);
    let px = match info.color_type {
        png::ColorType::Rgba => buf[..w * h * 4].to_vec(),
        png::ColorType::Rgb => buf[..w * h * 3].chunks(3).flat_map(|p| [p[0], p[1], p[2], 255]).collect(),
        png::ColorType::GrayscaleAlpha => buf[..w * h * 2].chunks(2).flat_map(|p| [p[0], p[0], p[0], p[1]]).collect(),
        png::ColorType::Grayscale => buf[..w * h].iter().flat_map(|&g| [g, g, g, 255]).collect(),
        _ => return Err(format!("{}: unexpected colour type", path.display())),
    };
    Ok(Img { w, h, px })
}

pub fn write_png(path: &Path, img: &Img) -> R<()> {
    let f = std::fs::File::create(path).map_err(|e| format!("{}: {e}", path.display()))?;
    let mut enc = png::Encoder::new(std::io::BufWriter::new(f), img.w as u32, img.h as u32);
    enc.set_color(png::ColorType::Rgba);
    enc.set_depth(png::BitDepth::Eight);
    let mut wr = enc.write_header().map_err(|e| format!("{}: {e}", path.display()))?;
    wr.write_image_data(&img.px).map_err(|e| format!("{}: {e}", path.display()))
}

/// `img` box-filtered to n x n (a whole fraction of its size), colours weighted by alpha.
pub fn shrink(img: &Img, n: usize) -> R<Img> {
    if img.w != img.h || img.w % n != 0 {
        return Err(format!("{}x{} does not shrink to {n}x{n}", img.w, img.h));
    }
    let k = img.w / n;
    let mut px = vec![0u8; n * n * 4];
    for y in 0..n {
        for x in 0..n {
            let mut s = [0u64; 4];
            for yy in 0..k {
                for xx in 0..k {
                    let p = &img.px[((y * k + yy) * img.w + x * k + xx) * 4..][..4];
                    let a = p[3] as u64;
                    for c in 0..3 {
                        s[c] += p[c] as u64 * a;
                    }
                    s[3] += a;
                }
            }
            let o = &mut px[(y * n + x) * 4..][..4];
            if s[3] > 0 {
                for c in 0..3 {
                    o[c] = ((s[c] + s[3] / 2) / s[3]) as u8;
                }
            }
            o[3] = ((s[3] + (k * k / 2) as u64) / (k * k) as u64) as u8;
        }
    }
    Ok(Img { w: n, h: n, px })
}

/// `src` over `dst` at (x, y).
fn blit(dst: &mut Img, src: &Img, x: usize, y: usize) {
    for j in 0..src.h {
        for i in 0..src.w {
            let s = &src.px[(j * src.w + i) * 4..][..4];
            let o = ((y + j) * dst.w + x + i) * 4;
            let a = s[3] as u32;
            for c in 0..3 {
                dst.px[o + c] = ((s[c] as u32 * a + dst.px[o + c] as u32 * (255 - a) + 127) / 255) as u8;
            }
        }
    }
}

/// The contact sheet: one row per look (LOOKS), the glyphs at 64 px in GLYPHS' order, each row
/// on the menus' dark blue on the left half of each glyph's tile and a mid grey on the right.
pub fn sheet(pack: &Path, out: &Path) -> R<()> {
    const G: usize = 64;
    const TILE: usize = 80;
    let (w, h) = (TILE * GLYPHS.len() + 16, TILE * LOOKS.len() + 16);
    let mut img = Img { w, h, px: vec![255; w * h * 4] };
    for (r, (theme, color, fill)) in LOOKS.iter().enumerate() {
        for (c, (_, pat)) in GLYPHS.iter().enumerate() {
            let (x0, y0) = (8 + c * TILE, 8 + r * TILE);
            for y in y0..y0 + TILE - 4 {
                for x in x0..x0 + TILE - 4 {
                    let bg: [u8; 3] = if x < x0 + (TILE - 4) / 2 { [16, 24, 64] } else { [110, 110, 120] };
                    img.px[(y * w + x) * 4..][..3].copy_from_slice(&bg);
                }
            }
            let g = shrink(&read_png(&pack.join(glyph_path(pat, theme, *color, fill)))?, G)?;
            blit(&mut img, &g, x0 + (TILE - 4 - G) / 2, y0 + (TILE - 4 - G) / 2);
        }
    }
    write_png(out, &img)
}

/// prompts.bin from SRC's PNGs.
pub fn build(src: &Path) -> R<Vec<u8>> {
    let mut out = b"HWVXPRM1".to_vec();
    out.extend((GLYPHS.len() as u32).to_le_bytes());
    out.extend((CELL as u32).to_le_bytes());
    for (name, _) in GLYPHS {
        let g = shrink(&read_png(&src.join(format!("{name}.png")))?, CELL)?;
        let mut n = [0u8; 16];
        n[..name.len()].copy_from_slice(name.as_bytes());
        out.extend(n);
        out.extend(&g.px);
    }
    Ok(out)
}

/// What is wrong with BIN against SRC and README.md's sha256 column (empty: nothing).
pub fn check(src: &Path, bin: &Path) -> R<Vec<String>> {
    let mut bad = vec![];
    let readme_path = src.parent().unwrap_or(src).join("README.md");
    let readme = std::fs::read_to_string(&readme_path).map_err(|e| format!("{}: {e}", readme_path.display()))?;
    for (name, _) in GLYPHS {
        let p = src.join(format!("{name}.png"));
        let bytes = std::fs::read(&p).map_err(|e| format!("{}: {e}", p.display()))?;
        let sha: String = Sha256::digest(&bytes).iter().map(|b| format!("{b:02x}")).collect();
        let row = readme.lines().find(|l| l.contains(&format!("`{name}.png`")));
        match row {
            None => bad.push(format!("README.md: no row for {name}.png")),
            Some(l) if !l.contains(&sha) => bad.push(format!("{name}.png: sha256 {sha} is not README.md's")),
            _ => {}
        }
    }
    let want = build(src)?;
    let have = std::fs::read(bin).map_err(|e| format!("{}: {e}", bin.display()))?;
    if want != have {
        bad.push(format!("{}: not what `xdlrs prompts build` makes of {}", bin.display(), src.display()));
    }
    Ok(bad)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn shrink_weights_by_alpha() {
        // a red opaque pixel and three transparent ones become a quarter-alpha red
        let mut px = vec![0u8; 16];
        px[..4].copy_from_slice(&[255, 0, 0, 255]);
        px[4..8].copy_from_slice(&[0, 255, 0, 0]);
        let s = shrink(&Img { w: 2, h: 2, px }, 1).unwrap();
        assert_eq!(s.px, vec![255, 0, 0, 64]);
    }

    #[test]
    fn assets_match_bin() {
        let root = Path::new(env!("CARGO_MANIFEST_DIR")).join("../../assets/prompts");
        let bad = check(&root.join("src"), &root.join("prompts.bin")).unwrap();
        assert!(bad.is_empty(), "{bad:?}");
    }
}

/// Screenshots side by side for a look (`xdlrs prompts montage OUT.png COLS PNG...`): each
/// scaled to 320 px wide (nearest), COLS a row, on black.
pub fn montage(out: &Path, cols: usize, files: &[std::path::PathBuf]) -> R<()> {
    const TW: usize = 320;
    let imgs: Vec<Img> = files.iter().map(|f| read_png(f)).collect::<R<_>>()?;
    let th = imgs.iter().map(|i| i.h * TW / i.w.max(1)).max().unwrap_or(0);
    let cols = cols.max(1);
    let rows = imgs.len().div_ceil(cols);
    let (w, h) = (cols * (TW + 4), rows * (th + 4));
    let mut o = Img { w, h, px: vec![0; w * h * 4] };
    for p in o.px.chunks_exact_mut(4) {
        p[3] = 255;
    }
    for (n, im) in imgs.iter().enumerate() {
        let (x0, y0) = ((n % cols) * (TW + 4), (n / cols) * (th + 4));
        let ih = im.h * TW / im.w.max(1);
        for y in 0..ih {
            for x in 0..TW {
                let s = &im.px[((y * im.h / ih) * im.w + x * im.w / TW) * 4..][..4];
                let d = ((y0 + y) * w + x0 + x) * 4;
                o.px[d..d + 3].copy_from_slice(&s[..3]);
            }
        }
    }
    write_png(out, &o)
}
