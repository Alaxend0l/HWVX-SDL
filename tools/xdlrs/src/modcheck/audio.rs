//! Sound for the mod check: a mod's sound file decoded as xdl_media.decode_audio decodes it (a
//! PCM16 WAV as Python's `wave` reads it, everything else through the port's own decoders,
//! src/audiodec, linked here), the channels' mix, and the spectral fingerprint (sound_fp).

use rustfft::{FftPlanner, num_complex::Complex};
use std::ffi::{CStr, CString, c_char};
use std::path::Path;

#[repr(C)]
struct AudioDecoded {
    pcm: *mut i16,
    frames: u32,
    channels: u32,
    rate: u32,
    loop_start: i64,
    loop_end: i64,
    format: *const c_char,
}

unsafe extern "C" {
    fn AudioDec_File(path: *const c_char, out: *mut AudioDecoded, err: *mut c_char, err_size: usize) -> i32;
    fn AudioDec_Free(a: *mut AudioDecoded);
}

/// Decoded audio: interleaved PCM16, channels, rate.
pub struct Pcm {
    pub data: Vec<i16>,
    pub channels: usize,
    pub rate: u32,
}

impl Pcm {
    pub fn frames(&self) -> usize {
        self.data.len() / self.channels.max(1)
    }
    pub fn channel(&self, c: usize) -> Vec<i16> {
        self.data.iter().skip(c).step_by(self.channels).copied().collect()
    }
    /// to_mono: the channels' mean, rounded half away from zero
    pub fn mono(&self) -> Vec<i16> {
        let c = self.channels as i64;
        if c == 1 {
            return self.data.clone();
        }
        self.data
            .chunks_exact(self.channels)
            .map(|f| {
                let s: i64 = f.iter().map(|&x| x as i64).sum();
                (if s >= 0 { (s + c / 2) / c } else { -((-s + c / 2) / c) }) as i16
            })
            .collect()
    }
}

/// src/audiodec's AUDIODEC_MAX_FRAMES: ten minutes at 48 kHz
pub const MAX_FRAMES: usize = 48000 * 60 * 10;

/// Python 3.9's `wave` on a RIFF WAVE: Ok(None) where wave.open fails (wave.Error, EOFError) or
/// the samples aren't 16-bit (decode_audio then runs the decoder), Err for what numpy refuses.
fn py_wave(data: &[u8]) -> Result<Option<Pcm>, String> {
    let rd32 = |o: usize| u32::from_le_bytes(data[o..o + 4].try_into().unwrap()) as usize;
    let riff_size = rd32(4);
    // the RIFF chunk: reads are bounded by its size and the file
    let base = 8usize;
    let riff_end = base + riff_size;
    let avail = |pos: usize, n: usize| -> usize { n.min(riff_end.saturating_sub(pos)).min(data.len().saturating_sub(pos)) };
    let mut pos = base + 4; // after "WAVE"
    if riff_size < 4 {
        return Ok(None);
    }
    let (mut fmt, mut chans, mut rate, mut width) = (false, 0usize, 0u32, 0usize);
    let mut data_chunk = None;
    loop {
        if avail(pos, 4) < 4 {
            break;
        }
        let name = &data[pos..pos + 4];
        if avail(pos + 4, 4) < 4 {
            break;
        }
        let size = rd32(pos + 4);
        let start = pos + 8;
        let rd = |o: usize, n: usize| -> usize { avail(start + o, n.min(size.saturating_sub(o))) };
        if name == b"fmt " {
            if rd(0, 14) < 14 {
                return Ok(None);
            }
            let tag = u16::from_le_bytes([data[start], data[start + 1]]);
            if tag != 1 {
                return Ok(None);
            }
            chans = u16::from_le_bytes([data[start + 2], data[start + 3]]) as usize;
            rate = rd32(start + 4) as u32;
            if rd(14, 2) < 2 {
                return Ok(None);
            }
            width = (u16::from_le_bytes([data[start + 14], data[start + 15]]) as usize).div_ceil(8);
            if width == 0 || chans == 0 {
                return Ok(None);
            }
            fmt = true;
        } else if name == b"data" {
            if !fmt {
                return Ok(None);
            }
            data_chunk = Some((start, size));
            break;
        }
        let skip = size + (size & 1);
        let np = start + skip;
        if np > riff_end {
            return Err("RuntimeError (a chunk runs past the RIFF chunk)".into());
        }
        pos = np;
    }
    let Some((start, size)) = data_chunk else { return Ok(None) };
    if !fmt || width != 2 {
        return Ok(None);
    }
    let framesize = chans * width;
    let nframes = size / framesize;
    let n = avail(start, nframes * framesize);
    // longer than the decoder takes (AUDIODEC_MAX_FRAMES, ten minutes at 48 kHz): the decoder
    // then refuses it as it refuses any other format
    if n / framesize > MAX_FRAMES {
        return Ok(None);
    }
    if n % 2 != 0 {
        return Err("buffer size must be a multiple of element size".into());
    }
    if (n / 2) % chans != 0 {
        return Err(format!("cannot reshape array of size {} into shape ({chans})", n / 2));
    }
    let pcm = data[start..start + n].chunks_exact(2).map(|c| i16::from_le_bytes([c[0], c[1]])).collect();
    Ok(Some(Pcm { data: pcm, channels: chans, rate }))
}

/// decode_audio: a sound file as the game's sounds are (all channels, the file's rate).
/// `shown` is the path as the Python prints it.
pub fn decode_audio(path: &Path, shown: &str) -> Result<Pcm, String> {
    let data = std::fs::read(path).map_err(|e| format!("[Errno] {e}: '{shown}'"))?;
    if data.len() >= 12 && &data[..4] == b"RIFF" && &data[8..12] == b"WAVE"
        && let Some(p) = py_wave(&data)?
    {
        return Ok(p);
    }
    let c = CString::new(path.as_os_str().to_string_lossy().as_bytes()).map_err(|e| e.to_string())?;
    let mut a = AudioDecoded {
        pcm: std::ptr::null_mut(),
        frames: 0,
        channels: 0,
        rate: 0,
        loop_start: -1,
        loop_end: -1,
        format: std::ptr::null(),
    };
    let mut err = [0 as c_char; 256];
    // SAFETY: the decoder fills `a` (pcm malloc'd, frames * channels samples) or writes a message
    unsafe {
        if AudioDec_File(c.as_ptr(), &mut a, err.as_mut_ptr(), err.len()) == 0 {
            let m = CStr::from_ptr(err.as_ptr()).to_string_lossy().into_owned();
            return Err(format!("{shown}: {m}"));
        }
        if a.pcm.is_null() {
            AudioDec_Free(&mut a);
            return Err("-: can't be written".into());
        }
        let n = a.frames as usize * a.channels as usize;
        let v = std::slice::from_raw_parts(a.pcm, n).to_vec();
        let out = Pcm { data: v, channels: a.channels as usize, rate: a.rate };
        AudioDec_Free(&mut a);
        Ok(out)
    }
}

// ---- the fingerprint ------------------------------------------------------------------------

fn pairwise(a: &[f64]) -> f64 {
    let n = a.len();
    if n < 8 {
        let mut r = 0.0;
        for &x in a {
            r += x;
        }
        r
    } else if n <= 128 {
        let mut r = [0f64; 8];
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

/// Python's round() of a float to an int (half to even)
fn py_round(x: f64) -> i64 {
    let r = x.round();
    if (x - x.trunc()).abs() == 0.5 {
        let f = x.floor();
        if (f as i64) % 2 == 0 { f as i64 } else { f as i64 + 1 }
    } else {
        r as i64
    }
}

/// sound_fp: (keys, frame numbers) of the frames louder than an rms of 200.
pub fn sound_fp(pcm: &[i16], rate: u32) -> (Vec<u16>, Vec<u32>) {
    let x: Vec<f64> = pcm.iter().map(|&v| v as f64).collect();
    let rate_f = rate as f64;
    if rate < 4000 || (x.len() as i64) < (rate / 8) as i64 {
        return (vec![], vec![]);
    }
    let win = py_round(rate_f * 0.064) as usize;
    let hop = (win / 4).max(1);
    let n = 1 + (x.len() as i64 - win as i64).div_euclid(hop as i64);
    if n < 3 {
        return (vec![], vec![]);
    }
    let n = n as usize;
    // np.hanning
    let hann: Vec<f64> = if win == 1 {
        vec![1.0]
    } else {
        (0..win)
            .map(|i| {
                let k = (1 - win as i64 + 2 * i as i64) as f64;
                0.5 + 0.5 * (std::f64::consts::PI * k / (win - 1) as f64).cos()
            })
            .collect()
    };
    // np.fft.rfftfreq(win, 1 / rate)
    let val = 1.0 / (win as f64 * (1.0 / rate_f));
    let nb = win / 2 + 1;
    let hz: Vec<f64> = (0..nb).map(|k| k as f64 * val).collect();
    // np.geomspace(300, min(3000, 0.45 * rate), 17)
    let stop = 3000f64.min(0.45 * rate_f);
    let (ls, le) = (300f64.log10(), stop.log10());
    let step = (le - ls) / 16.0;
    let mut edges: Vec<f64> = (0..17).map(|k| 10f64.powf(k as f64 * step + ls)).collect();
    edges[16] = 10f64.powf(le);
    edges[0] = 300.0;
    edges[16] = stop;
    let bands: Vec<Vec<usize>> =
        (0..16).map(|j| (0..nb).filter(|&k| hz[k] >= edges[j] && hz[k] < edges[j + 1]).collect()).collect();
    let mut planner = FftPlanner::<f64>::new();
    let fft = planner.plan_fft_forward(win);
    let mut d_prev: Option<[f64; 15]> = None;
    let mut keys = vec![];
    let mut frames = vec![];
    let mut buf = vec![Complex::new(0.0, 0.0); win];
    let mut spec = vec![0f64; nb];
    let mut tmp = vec![];
    for i in 0..n {
        let fr: Vec<f64> = (0..win).map(|k| x[i * hop + k] * hann[k]).collect();
        for k in 0..win {
            buf[k] = Complex::new(fr[k], 0.0);
        }
        fft.process(&mut buf);
        for k in 0..nb {
            let a = buf[k].re.hypot(buf[k].im);
            spec[k] = a * a;
        }
        let mut e = [0f64; 16];
        for j in 0..16 {
            tmp.clear();
            tmp.extend(bands[j].iter().map(|&k| spec[k]));
            e[j] = (pairwise(&tmp) + 1e-3).ln();
        }
        let mut d = [0f64; 15];
        for j in 0..15 {
            d[j] = e[j] - e[j + 1];
        }
        if let Some(p) = d_prev {
            let mut key = 0u16;
            for j in 0..15 {
                if d[j] - p[j] > 0.0 {
                    key |= 1 << j;
                }
            }
            let sq: Vec<f64> = fr.iter().map(|v| v * v).collect();
            let rms = (pairwise(&sq) / win as f64).sqrt();
            if rms > 200.0 {
                keys.push(key);
                frames.push(i as u32);
            }
        }
        d_prev = Some(d);
    }
    (keys, frames)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn wav(frames: usize) -> Vec<u8> {
        let n = frames * 2;
        let mut w = b"RIFF".to_vec();
        w.extend_from_slice(&((36 + n) as u32).to_le_bytes());
        w.extend_from_slice(b"WAVEfmt ");
        w.extend_from_slice(&16u32.to_le_bytes());
        w.extend_from_slice(&[1, 0, 1, 0]); // PCM, mono
        w.extend_from_slice(&48000u32.to_le_bytes());
        w.extend_from_slice(&96000u32.to_le_bytes());
        w.extend_from_slice(&[2, 0, 16, 0]);
        w.extend_from_slice(b"data");
        w.extend_from_slice(&(n as u32).to_le_bytes());
        w.resize(44 + n, 0);
        w
    }

    #[test]
    fn wave_over_ten_minutes_goes_to_the_decoder() {
        assert_eq!(py_wave(&wav(100)).unwrap().unwrap().data.len(), 100);
        assert_eq!(py_wave(&wav(MAX_FRAMES)).unwrap().unwrap().data.len(), MAX_FRAMES);
        // one frame more: not read here (the decoder says "is longer than ten minutes")
        assert!(py_wave(&wav(MAX_FRAMES + 1)).unwrap().is_none());
    }
}
