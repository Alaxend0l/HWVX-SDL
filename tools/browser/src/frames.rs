//! The viewer's frames in shared memory (src/port/viewer_frames.c; the layout is the protocol
//! doc's "Embedded frames"). The `frames` request turns them on and its reply names the shared
//! memory (`name`) and its `key`; `FrameReader::open` maps it, checks the key and unlinks the name
//! (POSIX: so it doesn't outlive a crash), and `take` copies out the newest frame when there is a
//! new one, telling the game it was taken (the game reads back no more than one frame ahead).

use crate::shm::Mapping;
use eframe::egui::Color32;
use std::sync::atomic::{AtomicU32, Ordering, fence};

const MAGIC: u32 = 0x4656_5748; // "HWVF"
const VERSION: u32 = 1;
const HEADER_MIN: usize = 64 + 3 * 32;

/// One frame: RGBA (alpha forced opaque, so premultiplied as it is), top row first. As egui's
/// pixels: the texture takes them as they are, without another copy of the frame.
#[derive(Clone)]
pub struct Frame {
    /// The viewer's frame number.
    pub frame: u32,
    /// How many frames the viewer had published with this one.
    pub published: u32,
    pub width: u32,
    pub height: u32,
    pub pixels: Vec<Color32>,
}

impl Frame {
    /// The pixels as bytes, `width * 4` a row.
    pub fn rgba(&self) -> &[u8] {
        // Color32 is four bytes, RGBA (repr(C))
        unsafe { std::slice::from_raw_parts(self.pixels.as_ptr() as *const u8, self.pixels.len() * 4) }
    }
}

pub struct FrameReader {
    base: *mut u8,
    size: usize,
    pub name: String,
    last: u32,
    _map: Mapping,
}

// the mapping is only touched through atomics and copies
unsafe impl Send for FrameReader {}

impl FrameReader {
    /// The shared memory a `frames` reply names (`name`, `key`: 16 hex digits, `size`).
    pub fn open(name: &str, key: &str, size: usize) -> Result<FrameReader, String> {
        let key = u64::from_str_radix(key, 16).map_err(|_| format!("bad frames key {key}"))?;
        if size < 4096 {
            return Err(format!("frames: a size of {size}"));
        }
        let r = Self::map(name, size)?;
        if r.u32_at(0) != MAGIC || r.u32_at(4) != VERSION {
            return Err(format!("{name}: not the viewer's frames (version {})", r.u32_at(4)));
        }
        let got = unsafe { std::ptr::read_volatile(r.base.add(8) as *const u64) };
        if got != key {
            return Err(format!("{name}: the key doesn't match"));
        }
        Ok(r)
    }

    fn map(name: &str, size: usize) -> Result<FrameReader, String> {
        let m = Mapping::open(name, size)?;
        Ok(FrameReader { base: m.base, size, name: name.to_string(), last: 0, _map: m })
    }

    fn atomic(&self, off: usize) -> &AtomicU32 {
        assert!(off + 4 <= HEADER_MIN.max(self.size.min(4096)));
        unsafe { &*(self.base.add(off) as *const AtomicU32) }
    }

    fn u32_at(&self, off: usize) -> u32 {
        self.atomic(off).load(Ordering::Acquire)
    }

    /// Frames the viewer has published so far.
    pub fn published(&self) -> u32 {
        self.u32_at(28)
    }

    /// The newest frame, if the viewer published one since the last `take` (None otherwise).
    pub fn take(&mut self) -> Option<Frame> {
        let header = self.u32_at(16) as usize;
        let slots = self.u32_at(20) as usize;
        let capacity = self.u32_at(24) as usize;
        if slots == 0 || slots > 3 || header + slots * capacity > self.size {
            return None;
        }
        for _ in 0..8 {
            let published = self.published();
            if published == 0 || published == self.last {
                return None;
            }
            let latest = self.u32_at(32) as usize;
            if latest >= slots {
                return None;
            }
            let s = 64 + latest * 32;
            let seq = self.u32_at(s);
            if seq & 1 != 0 {
                std::thread::yield_now();
                continue;
            }
            let (frame, width, height, stride, slot_pub) =
                (self.u32_at(s + 4), self.u32_at(s + 8), self.u32_at(s + 12), self.u32_at(s + 16), self.u32_at(s + 20));
            let bytes = stride as usize * height as usize;
            if width == 0 || stride < width * 4 || bytes > capacity {
                return None;
            }
            let n = width as usize * height as usize;
            let mut pixels: Vec<Color32> = Vec::with_capacity(n);
            unsafe {
                let src = self.base.add(header + latest * capacity);
                let dst = pixels.as_mut_ptr() as *mut u8;
                if stride == width * 4 {
                    std::ptr::copy_nonoverlapping(src, dst, n * 4);
                } else {
                    let row = width as usize * 4;
                    for y in 0..height as usize {
                        std::ptr::copy_nonoverlapping(src.add(y * stride as usize), dst.add(y * row), row);
                    }
                }
                pixels.set_len(n);
            }
            fence(Ordering::Acquire);
            if self.u32_at(s) != seq {
                continue; // rewritten while it was copied: again
            }
            // the EFB's alpha is the game's scratch, not opacity
            for p in &mut pixels {
                *p = Color32::from_rgba_premultiplied(p.r(), p.g(), p.b(), 255);
            }
            self.last = slot_pub.max(1);
            // taken: the game may read back the next one
            self.atomic(36).store(published.max(slot_pub), Ordering::Release);
            return Some(Frame { frame, published: slot_pub, width, height, pixels });
        }
        None
    }
}
