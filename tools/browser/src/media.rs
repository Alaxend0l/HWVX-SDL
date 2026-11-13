//! Textures and sounds as the game decodes them (the viewer's media commands, src/port/viewer_media.c;
//! the protocol doc's "Media"). The pixels and samples come through a shared memory block, the
//! "media blob", not the socket: a `texture` or `sound` reply names it (`blob`: name, key, size,
//! serial, offset, length) and `MediaReader::read` copies the payload out, checking that the
//! serial is still the reply's (a later request rewrites it).
//!
//! Also the files the browser saves: PNG (a texture level) and WAV (PCM16 mono).

use crate::shm::Mapping;
use serde_json::Value;
use std::sync::atomic::{AtomicU32, Ordering, fence};

const MAGIC: u32 = 0x4D56_5748; // "HWVM"
const VERSION: u32 = 1;
const HEADER: usize = 64;

/// The media blob, mapped once and again when the viewer replaces it (a bigger one: a new name).
#[derive(Default)]
pub struct MediaReader {
    map: Option<Mapping>,
}

impl MediaReader {
    pub fn new() -> MediaReader {
        MediaReader::default()
    }

    fn u32_at(&self, off: usize) -> u32 {
        let m = self.map.as_ref().unwrap();
        unsafe { (*(m.base.add(off) as *const AtomicU32)).load(Ordering::Acquire) }
    }

    /// The payload a reply's `blob` describes.
    pub fn read(&mut self, blob: &Value) -> Result<Vec<u8>, String> {
        let name = blob["name"].as_str().ok_or("no blob in the reply")?;
        let size = blob["size"].as_u64().unwrap_or(0) as usize;
        let key = u64::from_str_radix(blob["key"].as_str().unwrap_or(""), 16).map_err(|_| "bad blob key".to_string())?;
        let serial = blob["serial"].as_u64().unwrap_or(0) as u32;
        let offset = blob["offset"].as_u64().unwrap_or(HEADER as u64) as usize;
        let length = blob["length"].as_u64().unwrap_or(0) as usize;
        if self.map.as_ref().is_none_or(|m| m.name != name) {
            if size < HEADER {
                return Err(format!("media blob: a size of {size}"));
            }
            let m = Mapping::open(name, size)?;
            self.map = Some(m);
            if self.u32_at(0) != MAGIC || self.u32_at(4) != VERSION {
                self.map = None;
                return Err(format!("{name}: not the viewer's media blob"));
            }
            let got = unsafe { std::ptr::read_volatile(self.map.as_ref().unwrap().base.add(8) as *const u64) };
            if got != key {
                self.map = None;
                return Err(format!("{name}: the key doesn't match"));
            }
        }
        let m = self.map.as_ref().unwrap();
        if offset < HEADER || offset + length > m.size {
            return Err(format!("media blob: {length} bytes at {offset} don't fit in {}", m.size));
        }
        if self.u32_at(16) != serial || (self.u32_at(20) as usize) < length {
            return Err("media blob: rewritten by a later request (ask again)".into());
        }
        let data = unsafe { std::slice::from_raw_parts(m.base.add(offset), length).to_vec() };
        fence(Ordering::Acquire);
        if self.u32_at(16) != serial {
            return Err("media blob: rewritten while it was read (ask again)".into());
        }
        Ok(data)
    }
}

/// A decoded texture: every level the reply carries (RGBA8, top row first).
#[derive(Clone)]
pub struct Texture {
    pub info: Value,
    pub levels: Vec<Level>,
}

#[derive(Clone)]
pub struct Level {
    pub level: u32,
    pub width: u32,
    pub height: u32,
    pub rgba: Vec<u8>,
}

impl Texture {
    /// A `texture` reply and its blob's bytes.
    pub fn from_reply(reply: &Value, data: &[u8]) -> Result<Texture, String> {
        let mut levels = Vec::new();
        for l in reply["levelData"].as_array().ok_or("no levelData")? {
            let (w, h) = (l["width"].as_u64().unwrap_or(0) as u32, l["height"].as_u64().unwrap_or(0) as u32);
            let off = l["offset"].as_u64().unwrap_or(0) as usize;
            let n = (w * h * 4) as usize;
            if off + n > data.len() {
                return Err(format!("level {} past the blob", l["level"]));
            }
            levels.push(Level { level: l["level"].as_u64().unwrap_or(0) as u32, width: w, height: h, rgba: data[off..off + n].to_vec() });
        }
        let mut info = reply.clone();
        if let Some(m) = info.as_object_mut() {
            for k in ["seq", "ok", "blob", "levelData"] {
                m.remove(k);
            }
        }
        Ok(Texture { info, levels })
    }
}

/// A decoded sample: PCM16 mono and its rate and loop start (in samples).
#[derive(Clone)]
pub struct Sound {
    pub info: Value,
    pub rate: u32,
    pub loop_start: Option<u32>,
    pub pcm: Vec<i16>,
}

impl Sound {
    pub fn from_reply(reply: &Value, data: &[u8]) -> Sound {
        let pcm = data.chunks_exact(2).map(|b| i16::from_le_bytes([b[0], b[1]])).collect();
        let mut info = reply.clone();
        if let Some(m) = info.as_object_mut() {
            for k in ["seq", "ok", "blob"] {
                m.remove(k);
            }
        }
        Sound { rate: reply["rate"].as_u64().unwrap_or(22050) as u32, loop_start: reply["loop"].as_u64().map(|l| l as u32), pcm, info }
    }

    /// Min and max of each of `columns` equal spans of [from, to) (samples), for a waveform.
    pub fn peaks(&self, from: usize, to: usize, columns: usize) -> Vec<(i16, i16)> {
        let to = to.min(self.pcm.len());
        if columns == 0 || from >= to {
            return Vec::new();
        }
        let span = (to - from) as f64 / columns as f64;
        (0..columns)
            .map(|c| {
                let a = from + (c as f64 * span) as usize;
                let b = (from + ((c + 1) as f64 * span) as usize).max(a + 1).min(to);
                let s = &self.pcm[a.min(to - 1)..b.max(a.min(to - 1) + 1)];
                (*s.iter().min().unwrap_or(&0), *s.iter().max().unwrap_or(&0))
            })
            .collect()
    }
}

/// A level as a PNG (RGBA8).
pub fn write_png(path: &std::path::Path, width: u32, height: u32, rgba: &[u8]) -> Result<(), String> {
    let f = std::fs::File::create(path).map_err(|e| format!("{}: {e}", path.display()))?;
    let mut enc = png::Encoder::new(std::io::BufWriter::new(f), width, height);
    enc.set_color(png::ColorType::Rgba);
    enc.set_depth(png::BitDepth::Eight);
    let mut w = enc.write_header().map_err(|e| e.to_string())?;
    w.write_image_data(rgba).map_err(|e| e.to_string())
}

/// A PNG as RGBA8 (palettes, grey and RGB expanded): width, height, pixels.
pub fn read_png(path: &std::path::Path) -> Result<(u32, u32, Vec<u8>), String> {
    let f = std::fs::File::open(path).map_err(|e| format!("{}: {e}", path.display()))?;
    let mut dec = png::Decoder::new(std::io::BufReader::new(f));
    dec.set_transformations(png::Transformations::EXPAND | png::Transformations::STRIP_16);
    let mut r = dec.read_info().map_err(|e| e.to_string())?;
    let mut buf = vec![0; r.output_buffer_size().ok_or("png: too big")?];
    let info = r.next_frame(&mut buf).map_err(|e| e.to_string())?;
    let (w, h) = (info.width, info.height);
    let px = &buf[..info.buffer_size()];
    let rgba = match info.color_type {
        png::ColorType::Rgba => px.to_vec(),
        png::ColorType::Rgb => px.chunks_exact(3).flat_map(|p| [p[0], p[1], p[2], 255]).collect(),
        png::ColorType::GrayscaleAlpha => px.chunks_exact(2).flat_map(|p| [p[0], p[0], p[0], p[1]]).collect(),
        png::ColorType::Grayscale => px.iter().flat_map(|&g| [g, g, g, 255]).collect(),
        png::ColorType::Indexed => return Err("png: a palette left unexpanded".into()),
    };
    Ok((w, h, rgba))
}

/// PCM16 mono as a WAV file's bytes.
pub fn wav_bytes(rate: u32, pcm: &[i16]) -> Vec<u8> {
    let data = (pcm.len() * 2) as u32;
    let mut out = Vec::with_capacity(44 + data as usize);
    out.extend_from_slice(b"RIFF");
    out.extend_from_slice(&(36 + data).to_le_bytes());
    out.extend_from_slice(b"WAVEfmt ");
    out.extend_from_slice(&16u32.to_le_bytes());
    out.extend_from_slice(&1u16.to_le_bytes()); // PCM
    out.extend_from_slice(&1u16.to_le_bytes()); // mono
    out.extend_from_slice(&rate.to_le_bytes());
    out.extend_from_slice(&(rate * 2).to_le_bytes());
    out.extend_from_slice(&2u16.to_le_bytes());
    out.extend_from_slice(&16u16.to_le_bytes());
    out.extend_from_slice(b"data");
    out.extend_from_slice(&data.to_le_bytes());
    for s in pcm {
        out.extend_from_slice(&s.to_le_bytes());
    }
    out
}

/// A PCM16 WAV file's rate and samples (the first channel).
pub fn read_wav(bytes: &[u8]) -> Result<(u32, Vec<i16>), String> {
    if bytes.len() < 12 || &bytes[0..4] != b"RIFF" || &bytes[8..12] != b"WAVE" {
        return Err("not a WAV file".into());
    }
    let (mut p, mut rate, mut channels, mut bits) = (12, 0u32, 1u16, 16u16);
    while p + 8 <= bytes.len() {
        let id = &bytes[p..p + 4];
        let n = u32::from_le_bytes(bytes[p + 4..p + 8].try_into().unwrap()) as usize;
        let body = &bytes[p + 8..(p + 8 + n).min(bytes.len())];
        if id == b"fmt " && body.len() >= 16 {
            channels = u16::from_le_bytes([body[2], body[3]]);
            rate = u32::from_le_bytes(body[4..8].try_into().unwrap());
            bits = u16::from_le_bytes([body[14], body[15]]);
        } else if id == b"data" {
            if bits != 16 {
                return Err(format!("WAV: {bits}-bit"));
            }
            let step = 2 * channels.max(1) as usize;
            return Ok((rate, body.chunks_exact(step).map(|b| i16::from_le_bytes([b[0], b[1]])).collect()));
        }
        p += 8 + n + (n & 1);
    }
    Err("WAV: no data chunk".into())
}

/// Plays a WAV file with the system's player (macOS afplay, Linux aplay/paplay, Windows
/// PowerShell's SoundPlayer): the browser's own playback, for when the game's audio is off.
pub fn play_file(path: &std::path::Path) -> Result<std::process::Child, String> {
    use std::process::{Command, Stdio};
    let mut cmd = if cfg!(target_os = "macos") {
        let mut c = Command::new("afplay");
        c.arg(path);
        c
    } else if cfg!(windows) {
        // the path as an environment variable, never inside the command's text
        let mut c = xdlrs::command("powershell");
        c.env("HWVX_PLAY_FILE", path).args(["-NoProfile", "-Command", "(New-Object Media.SoundPlayer $env:HWVX_PLAY_FILE).PlaySync()"]);
        c
    } else {
        let mut c = Command::new("aplay");
        c.arg("-q").arg(path);
        c
    };
    cmd.stdout(Stdio::null()).stderr(Stdio::null()).spawn().map_err(|e| format!("no system player: {e}"))
}
