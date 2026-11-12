#!/usr/bin/python3
"""tools/stream_check.py DUMP.f32 [--from S] [--to S] [--win S] [--freqs 300,440,554.37,1000]

The level of each test tone (tools/make_test_music.sh) per window of an
HWVX_AUDIODUMP (raw float32 stereo, 48 kHz), and the RMS of the mix, to check
streamed music: the 300 Hz intro once, the 440/554 Hz loop after it, never
the 1000 Hz tail past the loop end. --summary prints only the totals."""
import argparse
import numpy as np

RATE = 48000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("dump")
    ap.add_argument("--from", dest="t0", type=float, default=0.0)
    ap.add_argument("--to", dest="t1", type=float, default=None)
    ap.add_argument("--win", type=float, default=0.5)
    ap.add_argument("--freqs", default="300,440,554.37,1000")
    ap.add_argument("--summary", action="store_true")
    a = ap.parse_args()
    x = np.fromfile(a.dump, dtype=np.float32).reshape(-1, 2)
    x = x[int(a.t0 * RATE):int(a.t1 * RATE) if a.t1 else None]
    freqs = [float(f) for f in a.freqs.split(",")]
    n = int(a.win * RATE)
    w = np.hanning(n)
    tot = {f: [] for f in freqs}
    print(f"{len(x) / RATE:.1f} s of mix")
    if not a.summary:
        print("    t    rms  " + "  ".join(f"{f:>7g}" for f in freqs))
    for i in range(0, len(x) - n + 1, n):
        m = x[i:i + n].mean(axis=1)
        spec = np.abs(np.fft.rfft(m * w)) * 2 / w.sum()
        bins = np.fft.rfftfreq(n, 1 / RATE)
        amps = [spec[np.abs(bins - f) <= 3].max() for f in freqs]
        for f, v in zip(freqs, amps):
            tot[f].append(v)
        if not a.summary:
            rms = np.sqrt((m * m).mean())
            print(f"{a.t0 + i / RATE:5.1f} {rms:6.3f}  " + "  ".join(f"{v:7.3f}" for v in amps))
    for f in freqs:
        v = np.array(tot[f])
        print(f"{f:>7g} Hz: max {v.max():.3f}, windows above 0.02: {(v > 0.02).sum()}/{len(v)}")


if __name__ == "__main__":
    main()
