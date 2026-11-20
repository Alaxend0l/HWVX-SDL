//! The game's movies, decoded in the editor: the GameCube's THP (here, natively: JPEG pictures
//! through zune-jpeg once their unstuffed entropy data is stuffed, and DSP ADPCM audio, as
//! src/port/thp.c and thp_jpeg.c read them), the PC's Bink and the PS2's MPEG-2 .PSS (through
//! FFmpeg loaded at run time, src/ffmpeg.rs; a PSS's own SShd/SSbd PCM track, which FFmpeg's
//! demuxer passes over, read here as src/port/pss.c reads it).
//!
//! `find_movies` lists a data folder's movies (FMV_ / fmv_, any case; the PS2 prototype's
//! M1INTRO.PSS etc. wherever they are) and other video files (a modder's mp4 and the like, which
//! only the system's video player plays). `probe` describes one; `VideoStream` decodes its
//! pictures on a thread, from any frame, a few ahead; `decode_audio` its whole soundtrack.

use crate::ffmpeg::FfMovie;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom};
use std::path::{Path, PathBuf};
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::mpsc::{Receiver, SyncSender, TrySendError, sync_channel};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    Thp,
    Bink,
    Pss,
    /// a video file of some other kind: the system's player's
    Other,
}

impl Kind {
    pub fn name(self) -> &'static str {
        match self {
            Kind::Thp => "THP",
            Kind::Bink => "Bink",
            Kind::Pss => "PSS (MPEG-2)",
            Kind::Other => "video",
        }
    }
    /// Decoded through FFmpeg (else here, or not at all).
    pub fn needs_ffmpeg(self) -> bool {
        matches!(self, Kind::Bink | Kind::Pss)
    }
}

#[derive(Clone, Debug)]
pub struct MovieFile {
    pub path: PathBuf,
    /// the path under the data folder, as listed
    pub name: String,
    pub kind: Kind,
    pub bytes: u64,
}

const OTHER_EXT: [&str; 9] = ["mp4", "m4v", "webm", "mkv", "avi", "mov", "mpg", "mpeg", "ogv"];

/// What kind of movie a file is (None: not a video).
pub fn kind_of(path: &Path) -> Option<Kind> {
    let ext = path.extension().map(|e| e.to_string_lossy().to_lowercase()).unwrap_or_default();
    match ext.as_str() {
        "thp" => return Some(Kind::Thp),
        "bik" => return Some(Kind::Bink),
        "pss" => return Some(Kind::Pss),
        e if OTHER_EXT.contains(&e) => return Some(Kind::Other),
        _ => {}
    }
    // the GameCube's have no extension (fmv_/bglogo): the magic says
    let mut m = [0u8; 4];
    let ok = File::open(path).and_then(|mut f| f.read_exact(&mut m)).is_ok();
    (ok && &m == b"THP\0").then_some(Kind::Thp)
}

/// The movies (and other videos) under a folder, a few levels down, sorted by name.
pub fn find_movies(dir: &Path) -> Vec<MovieFile> {
    fn walk(root: &Path, d: &Path, depth: u32, out: &mut Vec<MovieFile>) {
        let Ok(rd) = std::fs::read_dir(d) else { return };
        let mut ents: Vec<_> = rd.flatten().collect();
        ents.sort_by_key(|e| e.file_name());
        for e in ents {
            let p = e.path();
            let Ok(md) = e.metadata() else { continue };
            if md.is_dir() {
                if depth < 4 {
                    walk(root, &p, depth + 1, out);
                }
                continue;
            }
            // an extension-less file is only looked into in a movie folder
            let in_fmv = d.file_name().is_some_and(|n| n.to_string_lossy().to_lowercase().starts_with("fmv"));
            if p.extension().is_none() && !in_fmv {
                continue;
            }
            if let Some(kind) = kind_of(&p) {
                let name = p.strip_prefix(root).unwrap_or(&p).to_string_lossy().replace('\\', "/");
                out.push(MovieFile { path: p, name, kind, bytes: md.len() });
            }
        }
    }
    let mut out = Vec::new();
    walk(dir, dir, 0, &mut out);
    out
}

/// What a movie is.
#[derive(Clone, Debug, Default)]
pub struct Info {
    pub format: String,
    pub video_codec: String,
    pub width: u32,
    pub height: u32,
    pub fps: f64,
    pub frames: u32,
    pub seconds: f64,
    pub channels: u32,
    pub rate: u32,
    pub audio_codec: String,
}

impl Info {
    pub fn summary(&self) -> String {
        let audio = if self.channels > 0 {
            format!("{} {} ch {} Hz", self.audio_codec, self.channels, self.rate)
        } else {
            "no audio".into()
        };
        format!(
            "{} {}  ·  {}×{}  ·  {:.2} fps  ·  {} frames  ·  {}  ·  {}",
            self.format,
            self.video_codec,
            self.width,
            self.height,
            self.fps,
            self.frames,
            fmt_time(self.seconds),
            audio
        )
    }
}

pub fn fmt_time(s: f64) -> String {
    let s = s.max(0.0);
    format!("{}:{:05.2}", (s / 60.0) as u32, s % 60.0)
}

// ---- THP ---------------------------------------------------------------------------------------

fn be32(b: &[u8], o: usize) -> u32 {
    u32::from_be_bytes(b[o..o + 4].try_into().unwrap())
}

/// A THP file: a 0x30-byte header, component info, then a chain of frames, each with the next's
/// size, its components' sizes, a JPEG and a block of DSP ADPCM (thp.c).
pub struct Thp {
    f: File,
    pub fps: f64,
    pub frames: u32,
    comps: Vec<u8>,
    pub width: u32,
    pub height: u32,
    pub channels: u32,
    pub rate: u32,
    first_off: u64,
    first_size: u32,
    next_off: u64,
    next_size: u32,
    pub read: u32,
}

impl Thp {
    pub fn open(path: &Path) -> Result<Thp, String> {
        let mut f = File::open(path).map_err(|e| format!("{}: {e}", path.display()))?;
        let mut h = [0u8; 0x30];
        f.read_exact(&mut h).map_err(|e| format!("{}: {e}", path.display()))?;
        if &h[0..4] != b"THP\0" {
            return Err(format!("{}: not a THP file", path.display()));
        }
        let fps = f32::from_bits(be32(&h, 0x10)) as f64;
        let fps = if fps > 1.0 && fps < 120.0 { fps } else { 29.97 };
        let (frames, first_size, comp_off, movie_off) = (be32(&h, 0x14), be32(&h, 0x18), be32(&h, 0x20) as u64, be32(&h, 0x28) as u64);
        let mut c = [0u8; 0x80];
        f.seek(SeekFrom::Start(comp_off)).and_then(|_| f.read_exact(&mut c)).map_err(|e| format!("{}: {e}", path.display()))?;
        let n = (be32(&c, 0) as usize).min(16);
        let comps = c[4..4 + n].to_vec();
        let (mut width, mut height, mut channels, mut rate) = (0, 0, 0, 0);
        let mut o = 0x14;
        for &t in &comps {
            if o + 12 > c.len() {
                break;
            }
            if t == 0 {
                (width, height) = (be32(&c, o), be32(&c, o + 4));
                o += 8;
            } else if t == 1 {
                (channels, rate) = (be32(&c, o), be32(&c, o + 4));
                o += 12;
            }
        }
        if width == 0 || height == 0 || width > 1024 || height > 1024 {
            return Err(format!("{}: a THP of {width}x{height}", path.display()));
        }
        if !(1..=2).contains(&channels) {
            channels = 0;
        }
        Ok(Thp { f, fps, frames, comps, width, height, channels, rate, first_off: movie_off, first_size, next_off: movie_off, next_size: first_size, read: 0 })
    }

    pub fn rewind(&mut self) {
        self.next_off = self.first_off;
        self.next_size = self.first_size;
        self.read = 0;
    }

    /// Passes over a frame without reading it (its first word is the next one's size).
    pub fn skip(&mut self) -> bool {
        if self.read >= self.frames || self.next_size < 8 {
            return false;
        }
        let mut w = [0u8; 4];
        if self.f.seek(SeekFrom::Start(self.next_off)).and_then(|_| self.f.read_exact(&mut w)).is_err() {
            return false;
        }
        self.next_off += self.next_size as u64;
        self.next_size = u32::from_be_bytes(w);
        self.read += 1;
        true
    }

    /// The next frame's components: (its JPEG, its audio block).
    pub fn next_frame(&mut self) -> Option<(Vec<u8>, Option<Vec<u8>>)> {
        let size = self.next_size as usize;
        if self.read >= self.frames || size < 8 + 4 * self.comps.len() {
            return None;
        }
        let mut b = vec![0u8; size];
        self.f.seek(SeekFrom::Start(self.next_off)).ok()?;
        self.f.read_exact(&mut b).ok()?;
        self.next_off += size as u64;
        self.next_size = be32(&b, 0);
        self.read += 1;
        let mut off = 8 + 4 * self.comps.len();
        let (mut video, mut audio) = (Vec::new(), None);
        for (i, &t) in self.comps.iter().enumerate() {
            let cs = be32(&b, 8 + 4 * i) as usize;
            if off + cs > size {
                break;
            }
            match t {
                0 => video = b[off..off + cs].to_vec(),
                1 => audio = Some(b[off..off + cs].to_vec()),
                _ => {}
            }
            off += cs;
        }
        Some((video, audio))
    }
}

/// A THP picture as a standard JPEG: THP's entropy-coded data doesn't stuff a 0x00 after 0xFF
/// (thp_jpeg.c), so it is stuffed here, up to the closing EOI.
pub fn thp_jpeg_standard(j: &[u8]) -> Option<Vec<u8>> {
    let mut p = 2;
    if j.len() < 4 || j[0] != 0xFF || j[1] != 0xD8 {
        return None;
    }
    while p + 4 <= j.len() {
        if j[p] != 0xFF {
            return None;
        }
        let marker = j[p + 1];
        let len = u16::from_be_bytes([j[p + 2], j[p + 3]]) as usize;
        if marker == 0xDA {
            let start = p + 2 + len;
            let mut end = j.len();
            while end >= start + 2 && !(j[end - 2] == 0xFF && j[end - 1] == 0xD9) {
                end -= 1;
            }
            let end = if end >= start + 2 { end - 2 } else { j.len() };
            let mut out = Vec::with_capacity(j.len() + j.len() / 64);
            out.extend_from_slice(&j[..start.min(j.len())]);
            for &b in &j[start.min(end)..end] {
                out.push(b);
                if b == 0xFF {
                    out.push(0);
                }
            }
            out.extend_from_slice(&[0xFF, 0xD9]);
            return Some(out);
        }
        p += 2 + len;
    }
    None
}

pub fn decode_thp_picture(jpeg: &[u8], width: u32, height: u32, out: &mut [u8]) -> Result<(), String> {
    use zune_jpeg::zune_core::bytestream::ZCursor;
    use zune_jpeg::zune_core::colorspace::ColorSpace;
    use zune_jpeg::zune_core::options::DecoderOptions;
    let std = thp_jpeg_standard(jpeg).ok_or("not a THP JPEG")?;
    let opts = DecoderOptions::default().jpeg_set_out_colorspace(ColorSpace::RGBA).set_strict_mode(false);
    let mut d = zune_jpeg::JpegDecoder::new_with_options(ZCursor::new(&std[..]), opts);
    let px = d.decode().map_err(|e| format!("JPEG: {e:?}"))?;
    let info = d.info().ok_or("JPEG: no size")?;
    let (w, h) = (info.width as usize, info.height as usize);
    let (cw, ch) = ((w as u32).min(width) as usize, (h as u32).min(height) as usize);
    for y in 0..ch {
        out[y * width as usize * 4..(y * width as usize + cw) * 4].copy_from_slice(&px[y * w * 4..(y * w + cw) * 4]);
    }
    Ok(())
}

/// DSP ADPCM: 8-byte frames of a header byte (predictor << 4 | scale) and 14 4-bit samples.
fn dsp_adpcm(src: &[u8], n: usize, coefs: &[i16; 16], hist: &mut [i16; 2], out: &mut [i16], stride: usize, ch: usize) {
    let (mut i, mut p) = (0, 0);
    while i < n && p < src.len() {
        let hdr = src[p];
        p += 1;
        let (scale, pred) = (1i32 << (hdr & 0xF), ((hdr >> 4) & 7) as usize);
        let (c1, c2) = (coefs[pred * 2] as i32, coefs[pred * 2 + 1] as i32);
        let mut k = 0;
        while k < 14 && i < n {
            let byte = *src.get(p + k / 2).unwrap_or(&0);
            let mut nib = if k & 1 == 1 { byte & 0xF } else { byte >> 4 } as i32;
            if nib >= 8 {
                nib -= 16;
            }
            let s = (((nib * scale) << 11) + 1024 + c1 * hist[0] as i32 + c2 * hist[1] as i32) >> 11;
            let s = s.clamp(-32768, 32767) as i16;
            hist[1] = hist[0];
            hist[0] = s;
            out[i * stride + ch] = s;
            k += 1;
            i += 1;
        }
        p += 7;
    }
}

/// A THP frame's audio block, appended interleaved.
fn thp_audio(a: &[u8], channels: usize, out: &mut Vec<i16>) {
    if a.len() < 8 || channels == 0 {
        return;
    }
    let (ch_size, n) = (be32(a, 0) as usize, be32(a, 4) as usize);
    let mut p = 8;
    let mut coefs = [[0i16; 16]; 2];
    let mut hist = [[0i16; 2]; 2];
    for c in coefs.iter_mut().take(channels) {
        for k in c.iter_mut() {
            *k = i16::from_be_bytes([a[p], a[p + 1]]);
            p += 2;
        }
    }
    for h in hist.iter_mut().take(channels) {
        *h = [i16::from_be_bytes([a[p], a[p + 1]]), i16::from_be_bytes([a[p + 2], a[p + 3]])];
        p += 4;
    }
    if p + ch_size * channels > a.len() {
        return;
    }
    let at = out.len();
    out.resize(at + n * channels, 0);
    for c in 0..channels {
        let src = &a[p + c * ch_size..p + (c + 1) * ch_size];
        dsp_adpcm(src, n, &coefs[c], &mut hist[c], &mut out[at..], channels, c);
    }
}

// ---- PSS audio ---------------------------------------------------------------------------------

/// A PSS's own audio track (pss.c): private stream 1 (0xBD) payloads with one more 0xFF than
/// their PES header counts, sub-stream 0xA0; Sony's SShd/SSbd headers in the first; PCM16 in
/// blocks of `interleave` bytes per channel.
pub struct PssAudio {
    pub channels: u32,
    pub rate: u32,
    /// the first audio PTS, in seconds (None: none)
    pub start: Option<f64>,
    pub pcm: Vec<i16>,
}

fn pss_payloads(b: &[u8]) -> Vec<(usize, usize, Option<f64>)> {
    let mut out = Vec::new();
    let mut p = 0;
    while p + 4 <= b.len() {
        if b[p] != 0 || b[p + 1] != 0 || b[p + 2] != 1 {
            p += 1;
            continue;
        }
        let id = b[p + 3];
        p += 4;
        if id == 0xB9 {
            break;
        }
        if id == 0xBA {
            if p + 10 <= b.len() && b[p] & 0xC0 == 0x40 {
                p += 10 + (b[p + 9] & 7) as usize;
            } else {
                p += 8;
            }
            continue;
        }
        if id < 0xBB || p + 2 > b.len() {
            continue;
        }
        let len = u16::from_be_bytes([b[p], b[p + 1]]) as usize;
        p += 2;
        let pkt = &b[p..(p + len).min(b.len())];
        p += len;
        if id != 0xBD || pkt.len() < 3 || pkt[0] & 0xC0 != 0x80 {
            continue;
        }
        let mut hl = pkt[2] as usize;
        if 3 + hl > pkt.len() {
            continue;
        }
        let mut pts = None;
        if pkt[1] & 0x80 != 0 && hl >= 5 {
            let q = &pkt[3..];
            let v = ((q[0] as u64 >> 1) & 7) << 30 | ((q[1] as u64) << 7 | q[2] as u64 >> 1) << 15 | ((q[3] as u64) << 7 | q[4] as u64 >> 1);
            pts = Some(v as f64 / 90000.0);
        }
        hl += 3;
        while hl < pkt.len() && pkt[hl] == 0xFF {
            hl += 1;
        }
        if hl + 3 > pkt.len() || pkt[hl] & 0xF0 != 0xA0 {
            continue;
        }
        let at = p - len + hl + 3;
        out.push((at, pkt.len() - hl - 3, pts));
    }
    out
}

impl PssAudio {
    pub fn read(path: &Path) -> Result<PssAudio, String> {
        let b = std::fs::read(path).map_err(|e| format!("{}: {e}", path.display()))?;
        let pays = pss_payloads(&b);
        let &(at, n, start) = pays.first().ok_or("no audio track (private stream 1)")?;
        let d = &b[at..at + n];
        let le = |o: usize| u32::from_le_bytes(d[o..o + 4].try_into().unwrap());
        if n < 16 || &d[0..4] != b"SShd" {
            return Err("the audio track has no SShd header".into());
        }
        let hs = le(4) as usize;
        if hs < 16 || 8 + hs + 8 > n || &d[8 + hs..12 + hs] != b"SSbd" {
            return Err("the audio track has no SSbd header".into());
        }
        let (typ, rate, channels, inter) = (le(8), le(12), le(16), le(20) as usize);
        let mut left = le(8 + hs + 4) as usize;
        if typ != 1 || !(8000..=96000).contains(&rate) || !(1..=2).contains(&channels) || !(2..=0x4000).contains(&inter) || inter & 1 != 0 {
            return Err(format!("audio type {typ}, {rate} Hz, {channels} ch, interleave {inter:#x}: only PCM is read"));
        }
        let mut data = Vec::with_capacity(left);
        let mut keep = |s: &[u8]| {
            let k = s.len().min(left);
            data.extend_from_slice(&s[..k]);
            left -= k;
        };
        keep(&d[8 + hs + 8..]);
        for &(a, len, _) in &pays[1..] {
            keep(&b[a..a + len]);
        }
        let ch = channels as usize;
        let block = inter * ch;
        let per = inter / 2;
        let mut pcm = Vec::with_capacity(data.len() / 2);
        for blk in data.chunks_exact(block) {
            for i in 0..per {
                for c in 0..ch {
                    let s = &blk[c * inter + i * 2..];
                    pcm.push(i16::from_le_bytes([s[0], s[1]]));
                }
            }
        }
        Ok(PssAudio { channels, rate, start, pcm })
    }
}

// ---- the movie ---------------------------------------------------------------------------------

/// Describes a movie (reads its headers; FFmpeg's for Bink and PSS).
pub fn probe(m: &MovieFile) -> Result<Info, String> {
    match m.kind {
        Kind::Thp => {
            let t = Thp::open(&m.path)?;
            Ok(Info {
                format: "THP".into(),
                video_codec: "JPEG".into(),
                width: t.width,
                height: t.height,
                fps: t.fps,
                frames: t.frames,
                seconds: t.frames as f64 / t.fps,
                channels: t.channels,
                rate: t.rate,
                audio_codec: "DSP ADPCM".into(),
            })
        }
        Kind::Bink | Kind::Pss => {
            let f = FfMovie::open(&m.path, false, false)?;
            let mut info = Info {
                format: m.kind.name().into(),
                video_codec: f.video_codec.clone(),
                width: f.width,
                height: f.height,
                fps: f.fps,
                frames: f.frames,
                seconds: f.duration,
                channels: f.channels,
                rate: f.rate,
                audio_codec: f.audio_codec.clone(),
            };
            if m.kind == Kind::Pss && info.channels == 0 {
                // the length from the audio track's (the program stream states none)
                if let Ok(a) = PssAudio::read(&m.path) {
                    info.channels = a.channels;
                    info.rate = a.rate;
                    info.audio_codec = "SShd PCM16".into();
                    let s = a.pcm.len() as f64 / a.channels as f64 / a.rate as f64;
                    if info.seconds <= 0.0 {
                        info.seconds = s;
                        info.frames = (s * info.fps) as u32;
                    }
                }
            }
            Ok(info)
        }
        Kind::Other => Err("not a game movie: open it in your video player".into()),
    }
}

/// A movie's soundtrack, interleaved PCM16, placed against the picture (time 0 is the first
/// frame's).
#[derive(Clone, Debug, Default)]
pub struct Audio {
    pub channels: u32,
    pub rate: u32,
    pub pcm: Vec<i16>,
}

impl Audio {
    pub fn seconds(&self) -> f64 {
        if self.channels == 0 || self.rate == 0 { 0.0 } else { self.pcm.len() as f64 / self.channels as f64 / self.rate as f64 }
    }
    /// Peak |sample| / 32768 over all channels.
    pub fn peak(&self) -> f32 {
        self.pcm.iter().map(|s| s.unsigned_abs()).max().unwrap_or(0) as f32 / 32768.0
    }
    /// From `t` seconds on, as WAV bytes.
    pub fn wav_from(&self, t: f64) -> Vec<u8> {
        let ch = self.channels.max(1) as usize;
        let at = ((t.max(0.0) * self.rate as f64) as usize * ch).min(self.pcm.len());
        crate::media::wav_bytes_channels(self.rate, self.channels.max(1) as u16, &self.pcm[at..])
    }
}

/// The whole soundtrack (Ok(None): the movie has none).
pub fn decode_audio(m: &MovieFile) -> Result<Option<Audio>, String> {
    match m.kind {
        Kind::Thp => {
            let mut t = Thp::open(&m.path)?;
            if t.channels == 0 {
                return Ok(None);
            }
            let mut pcm = Vec::new();
            while let Some((_, a)) = t.next_frame() {
                if let Some(a) = a {
                    thp_audio(&a, t.channels as usize, &mut pcm);
                }
            }
            Ok(Some(Audio { channels: t.channels, rate: t.rate, pcm }))
        }
        Kind::Bink | Kind::Pss => {
            let mut f = FfMovie::open(&m.path, false, true)?;
            if f.channels > 0 {
                let ch = f.channels;
                let rate = f.rate;
                let pcm = f.all_audio().iter().map(|&s| (s.clamp(-1.0, 1.0) * 32767.0).round() as i16).collect();
                return Ok(Some(Audio { channels: ch, rate, pcm }));
            }
            if m.kind != Kind::Pss {
                return Ok(None);
            }
            let video_start = f.start;
            drop(f);
            let a = PssAudio::read(&m.path)?;
            let ch = a.channels as usize;
            let mut pcm = a.pcm;
            // placed by its PTS against the picture's first (pss.c's lead)
            let lead = a.start.map(|s| s - video_start).unwrap_or(0.0);
            let n = (lead.abs() * a.rate as f64) as usize * ch;
            if lead > 0.0 {
                pcm.splice(0..0, std::iter::repeat_n(0, n));
            } else if lead < 0.0 {
                pcm.drain(0..n.min(pcm.len()));
            }
            Ok(Some(Audio { channels: a.channels, rate: a.rate, pcm }))
        }
        Kind::Other => Ok(None),
    }
}

/// Pictures, decoded in turn.
enum Pictures {
    Thp(Thp),
    Ff(Box<FfMovie>),
}

impl Pictures {
    fn open(m: &MovieFile) -> Result<(Pictures, u32, u32), String> {
        match m.kind {
            Kind::Thp => {
                let t = Thp::open(&m.path)?;
                let (w, h) = (t.width, t.height);
                Ok((Pictures::Thp(t), w, h))
            }
            Kind::Bink | Kind::Pss => {
                let f = FfMovie::open(&m.path, true, false)?;
                let (w, h) = (f.width, f.height);
                Ok((Pictures::Ff(Box::new(f)), w, h))
            }
            Kind::Other => Err("not a game movie".into()),
        }
    }

    /// The next picture (into `rgba` if given); false at the end.
    fn next(&mut self, rgba: Option<&mut [u8]>, w: u32, h: u32) -> Result<bool, String> {
        match self {
            Pictures::Thp(t) => match rgba {
                None => Ok(t.skip()),
                Some(out) => match t.next_frame() {
                    None => Ok(false),
                    Some((jpeg, _)) => decode_thp_picture(&jpeg, w, h, out).map(|_| true),
                },
            },
            Pictures::Ff(f) => Ok(f.next_frame(rgba)),
        }
    }
}

/// A decoded picture: its frame number and RGBA pixels.
pub struct Picture {
    pub index: u32,
    pub rgba: Vec<u8>,
}

/// A movie's pictures from a frame on, decoded on a thread a few frames ahead (`ahead`). Dropping
/// it stops the thread.
pub struct VideoStream {
    pub width: u32,
    pub height: u32,
    rx: Receiver<Result<Picture, String>>,
    stop: Arc<AtomicBool>,
}

impl Drop for VideoStream {
    fn drop(&mut self) {
        self.stop.store(true, Ordering::Relaxed);
    }
}

impl VideoStream {
    pub fn start(m: &MovieFile, from: u32, ahead: usize) -> Result<VideoStream, String> {
        let (mut pics, width, height) = Pictures::open(m)?;
        let (tx, rx): (SyncSender<Result<Picture, String>>, _) = sync_channel(ahead.max(1));
        let stop = Arc::new(AtomicBool::new(false));
        let s = stop.clone();
        std::thread::spawn(move || {
            let mut index = 0;
            // up to `from`, unconverted
            while index < from {
                if s.load(Ordering::Relaxed) {
                    return;
                }
                match pics.next(None, width, height) {
                    Ok(true) => index += 1,
                    Ok(false) => return,
                    Err(e) => {
                        let _ = tx.send(Err(e));
                        return;
                    }
                }
            }
            loop {
                let mut rgba = vec![0u8; (width * height * 4) as usize];
                let r = pics.next(Some(&mut rgba), width, height);
                let msg = match r {
                    Ok(true) => Ok(Picture { index, rgba }),
                    Ok(false) => return,
                    Err(e) => Err(format!("frame {index}: {e}")),
                };
                index += 1;
                let mut msg = Some(msg);
                // blocks while `ahead` wait, giving up when stopped
                loop {
                    if s.load(Ordering::Relaxed) {
                        return;
                    }
                    match tx.try_send(msg.take().unwrap()) {
                        Ok(()) => break,
                        Err(TrySendError::Full(m)) => {
                            msg = Some(m);
                            std::thread::sleep(std::time::Duration::from_millis(3));
                        }
                        Err(TrySendError::Disconnected(_)) => return,
                    }
                }
            }
        });
        Ok(VideoStream { width, height, rx, stop })
    }

    /// The next picture if one is decoded (Ok(None): not yet). Err(None): the movie has ended.
    pub fn poll(&self) -> Result<Option<Picture>, Option<String>> {
        match self.rx.try_recv() {
            Ok(Ok(p)) => Ok(Some(p)),
            Ok(Err(e)) => Err(Some(e)),
            Err(std::sync::mpsc::TryRecvError::Empty) => Ok(None),
            Err(std::sync::mpsc::TryRecvError::Disconnected) => Err(None),
        }
    }

    /// Waits for the next picture (None: the end).
    pub fn wait(&self) -> Option<Result<Picture, String>> {
        self.rx.recv().ok()
    }
}
