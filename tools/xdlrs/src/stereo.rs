//! Stereo pan laws of the three releases, and the left/right balance of an
//! HWVX_AUDIODUMP (`xdlrs audio pan`, `xdlrs audio level DUMP --lr`).
//!
//! All three compute the same pan from a sound's position: SFX_ProcessSource
//! sets 26213.6 * x (x: the source's direction in the listener's frame,
//! normalized; ±0x6666 at the side), the IT player ±0x7FFF from the channel
//! pan. They differ in how the pan becomes speaker levels:
//!   GameCube (Voice_.c SetVoiceVol_, 0x800BD1D4) and PS2 (AUDIO.IRX
//!   SetVoiceVol_, 0x5458): a 7-bit index ((pan >> 9) & 0x7F) ^ 0x40 into
//!   g_panTab, 32767 * sqrt(i / 127) (equal power): L = tab[127 - i], R = tab[i].
//!   PC (0x45DC20): IDirectSoundBuffer::SetPan(pan / 4), clamped to ±10000:
//!   the far speaker is attenuated by |pan / 4| hundredths of a dB, the near
//!   one stays at full level (centre: both full).

/// g_panTab (GameCube .rodata, the PS2 IOP module's .data: the same values).
pub const PAN_TAB: [u16; 128] = [
    0x0, 0xB5B, 0x100F, 0x13AC, 0x16B7, 0x1965, 0x1BD2, 0x1E0C, 0x201F, 0x2212, 0x23EA, 0x25AB, 0x2758, 0x28F3, 0x2A7F,
    0x2BFD, 0x2D6E, 0x2ED4, 0x302F, 0x3181, 0x32CB, 0x340C, 0x3545, 0x3678, 0x37A4, 0x38CA, 0x39E9, 0x3B04, 0x3C19,
    0x3D29, 0x3E35, 0x3F3C, 0x403F, 0x413E, 0x423A, 0x4331, 0x4425, 0x4516, 0x4603, 0x46EE, 0x47D5, 0x48B9, 0x499B,
    0x4A7A, 0x4B56, 0x4C30, 0x4D08, 0x4DDD, 0x4EB0, 0x4F81, 0x504F, 0x511C, 0x51E7, 0x52AF, 0x5376, 0x543B, 0x54FE,
    0x55BF, 0x567F, 0x573D, 0x57FA, 0x58B5, 0x596E, 0x5A26, 0x5ADC, 0x5B91, 0x5C45, 0x5CF7, 0x5DA8, 0x5E58, 0x5F06,
    0x5FB3, 0x605F, 0x610A, 0x61B4, 0x625C, 0x6303, 0x63AA, 0x644F, 0x64F3, 0x6596, 0x6638, 0x66D9, 0x6779, 0x6818,
    0x68B6, 0x6954, 0x69F0, 0x6A8B, 0x6B26, 0x6BBF, 0x6C58, 0x6CF0, 0x6D87, 0x6E1E, 0x6EB3, 0x6F48, 0x6FDC, 0x706F,
    0x7102, 0x7194, 0x7225, 0x72B5, 0x7344, 0x73D3, 0x7462, 0x74EF, 0x757C, 0x7608, 0x7694, 0x771F, 0x77A9, 0x7833,
    0x78BC, 0x7944, 0x79CC, 0x7A53, 0x7ADA, 0x7B60, 0x7BE6, 0x7C6B, 0x7CEF, 0x7D73, 0x7DF6, 0x7E79, 0x7EFC, 0x7F7D,
    0x7FFF,
];

/// SFX_ProcessSource's pan scale (0x46CCCB33 on all three).
pub const SFX_PAN_SCALE: f32 = 26213.6;

/// The GameCube/PS2 speaker gains (0..1) for a voice pan.
pub fn gc_gains(pan: i32) -> (f64, f64) {
    let i = (((pan as u32) >> 9) & 0x7F) ^ 0x40;
    (PAN_TAB[127 - i as usize] as f64 / 32767.0, PAN_TAB[i as usize] as f64 / 32767.0)
}

/// The PC's DirectSound gains (0..1, centre both 1) for a voice pan.
pub fn pc_gains(pan: i32) -> (f64, f64) {
    let ds = (pan / 4).clamp(-10000, 10000); // 0x45D610: x / 4, truncated
    let far = 10f64.powf(-(ds.abs() as f64) / 2000.0);
    if ds >= 0 { (far, 1.0) } else { (1.0, far) }
}

fn db(x: f64) -> f64 {
    if x > 0.0 { 20.0 * x.log10() } else { f64::NEG_INFINITY }
}

/// `xdlrs audio pan`: R - L in dB per pan, for both laws.
pub fn pan_table() -> String {
    let mut s = String::from("source x  angle   pan      GC/PS2: L      R   R-L dB      PC: L      R   R-L dB\n");
    for k in 0..=10 {
        let x = k as f32 / 10.0;
        let pan = (SFX_PAN_SCALE * x) as i32 as i16 as i32;
        let (gl, gr) = gc_gains(pan);
        let (pl, pr) = pc_gains(pan);
        s += &format!(
            "  {x:5.2}  {:5.1}° {pan:6}   {:7.1} {:6.1} {:7.1}   {:8.1} {:6.1} {:7.1}\n",
            (x as f64).asin().to_degrees(),
            db(gl),
            db(gr),
            db(gr) - db(gl),
            db(pl),
            db(pr),
            db(pr) - db(pl)
        );
    }
    s += "music (IT channel pan 0..64 -> (p - 32) << 10):\n";
    for p in [32, 40, 48, 56, 64] {
        let pan = ((p - 32) << 10) - (((p - 32) << 10) >> 15);
        let (gl, gr) = gc_gains(pan);
        let (pl, pr) = pc_gains(pan);
        s += &format!("  chn {p:2}         {pan:6}   {:7.1} {:6.1} {:7.1}   {:8.1} {:6.1} {:7.1}\n", db(gl), db(gr), db(gr) - db(gl), db(pl), db(pr), db(pr) - db(pl));
    }
    s
}

/// Per window: (start s, left dB, right dB) RMS of an interleaved stereo dump.
pub fn lr_levels(samples: &[f32], rate: usize, window_s: f64, from_s: f64) -> Vec<(f64, f64, f64)> {
    let win = ((window_s * rate as f64) as usize).max(1);
    let start = (from_s * rate as f64) as usize;
    let frames = samples.len() / 2;
    let p = |x: f64| if x > 0.0 { 10.0 * x.log10() } else { -200.0 };
    let mut out = vec![];
    let mut pos = start;
    while pos + win <= frames {
        let (mut l, mut r) = (0.0f64, 0.0f64);
        for f in &samples[pos * 2..(pos + win) * 2].chunks_exact(2).collect::<Vec<_>>() {
            l += (f[0] as f64).powi(2);
            r += (f[1] as f64).powi(2);
        }
        out.push((pos as f64 / rate as f64, p(l / win as f64), p(r / win as f64)));
        pos += win;
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn table_is_equal_power() {
        for (i, &v) in PAN_TAB.iter().enumerate() {
            let want = 32767.0 * (i as f64 / 127.0).sqrt();
            assert!((v as f64 - want).abs() < 1.0, "{i}: {v} vs {want}");
        }
    }

    #[test]
    fn laws() {
        // centre
        let (l, r) = gc_gains(0);
        assert!((db(r) - db(l)).abs() < 0.1);
        assert_eq!(pc_gains(0), (1.0, 1.0));
        // a source at the right side: GC about 9.8 dB, PC 65.5 dB
        let pan = (SFX_PAN_SCALE * 1.0) as i32;
        let (l, r) = gc_gains(pan);
        assert!((db(r) - db(l) - 9.85).abs() < 0.1, "{}", db(r) - db(l));
        let (l, r) = pc_gains(pan);
        assert!((db(r) - db(l) - 65.53).abs() < 0.01, "{}", db(r) - db(l));
        // left is the mirror
        let (l, r) = pc_gains(-pan);
        assert!(l == 1.0 && r < 0.001);
    }

    #[test]
    fn lr() {
        let s: Vec<f32> = (0..4800).flat_map(|_| [0.5f32, 0.05]).collect();
        let v = lr_levels(&s, 48000, 0.05, 0.0);
        assert_eq!(v.len(), 2);
        assert!((v[0].1 - v[0].2 - 20.0).abs() < 0.01);
    }
}
