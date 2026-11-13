//! `xdlrs audio level DUMP [--window S] [--from S]`: the loudness of an
//! HWVX_AUDIODUMP file (raw float32 stereo, 48000 Hz) per window of S seconds
//! (default 5): RMS of the mix in dBFS, and of three bands (below 250 Hz,
//! 250 Hz - 4 kHz, above 4 kHz) from 4096-point FFTs of the mono mix. Two
//! runs (music on / muted) compare band for band.

use rustfft::{FftPlanner, num_complex::Complex};

pub const RATE: usize = 48000;
const FFT: usize = 4096;

/// Per window: (start s, total dB, [low, mid, high] dB).
pub fn levels(samples: &[f32], window_s: f64, from_s: f64) -> Vec<(f64, f64, [f64; 3])> {
    let mono: Vec<f32> = samples.chunks_exact(2).map(|c| 0.5 * (c[0] + c[1])).collect();
    let win = ((window_s * RATE as f64) as usize).max(FFT);
    let start = (from_s * RATE as f64) as usize;
    let fft = FftPlanner::<f32>::new().plan_fft_forward(FFT);
    // Hann window (power gain 0.375, undone below)
    let hann: Vec<f32> = (0..FFT).map(|i| 0.5 - 0.5 * (2.0 * std::f32::consts::PI * i as f32 / FFT as f32).cos()).collect();
    let db = |p: f64| if p > 0.0 { 10.0 * p.log10() } else { -200.0 };
    let mut out = vec![];
    let mut pos = start;
    while pos + win <= mono.len() {
        let w = &mono[pos..pos + win];
        let total = w.iter().map(|&x| (x as f64) * (x as f64)).sum::<f64>() / win as f64;
        let mut band = [0.0f64; 3];
        let mut frames = 0;
        for blk in w.chunks_exact(FFT) {
            let mut buf: Vec<Complex<f32>> = blk.iter().zip(&hann).map(|(&x, &h)| Complex::new(x * h, 0.0)).collect();
            fft.process(&mut buf);
            for (k, c) in buf.iter().enumerate().take(FFT / 2).skip(1) {
                let hz = k * RATE / FFT;
                let p = c.norm_sqr() as f64 * 2.0 / (0.375 * FFT as f64 * FFT as f64);
                band[if hz < 250 { 0 } else if hz < 4000 { 1 } else { 2 }] += p;
            }
            frames += 1;
        }
        let n = frames.max(1) as f64;
        out.push((pos as f64 / RATE as f64, db(total), [db(band[0] / n), db(band[1] / n), db(band[2] / n)]));
        pos += win;
    }
    out
}

pub fn read_dump(path: &std::path::Path) -> std::io::Result<Vec<f32>> {
    let b = std::fs::read(path)?;
    Ok(b.chunks_exact(4).map(|c| f32::from_le_bytes([c[0], c[1], c[2], c[3]])).collect())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn tone_lands_in_its_band() {
        let n = RATE * 2;
        let mut s = Vec::with_capacity(n * 2);
        for i in 0..n {
            let x = 0.5 * (2.0 * std::f32::consts::PI * 1000.0 * i as f32 / RATE as f32).sin();
            s.push(x);
            s.push(x);
        }
        let l = levels(&s, 1.0, 0.0);
        assert_eq!(l.len(), 2);
        let (_, total, b) = l[0];
        assert!((total - (-9.03)).abs() < 0.1, "{total}");
        assert!(b[1] > b[0] + 40.0 && b[1] > b[2] + 40.0, "{b:?}");
    }
}
