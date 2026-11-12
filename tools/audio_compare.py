#!/usr/bin/python3
"""tools/audio_compare.py REF.f32 NEW.f32 [REF2.f32]

Compares two HWVX_AUDIODUMP mixes (float32 stereo, 48 kHz). The dumps are only
bit-identical until the AX thread's timing against the game's first differs
(a few seconds in, between two runs of one build), so after the identical
prefix it compares per-half-second RMS and the averaged spectrum; with REF2 (a
second run of the reference) it prints the run-to-run jitter to set them
against."""
import sys
import numpy as np


def load(p):
    return np.fromfile(p, np.float32).reshape(-1, 2).mean(1)


def stats(a, b, n):
    w = 24000
    ra = np.sqrt((a[:n // w * w].reshape(-1, w) ** 2).mean(1))
    rb = np.sqrt((b[:n // w * w].reshape(-1, w) ** 2).mean(1))
    k = (n // 2 + 1) // 1000 * 1000
    sa = np.log10(np.abs(np.fft.rfft(a[:n]))[:k].reshape(-1, 1000).mean(1) + 1e-9)
    sb = np.log10(np.abs(np.fft.rfft(b[:n]))[:k].reshape(-1, 1000).mean(1) + 1e-9)
    return np.abs(ra - rb).max(), np.abs(sa - sb).max()


def main():
    dumps = [load(p) for p in sys.argv[1:]]
    n = min(len(d) for d in dumps)
    ref, new = dumps[0], dumps[1]
    diff = np.nonzero(ref[:n] != new[:n])[0]
    print(f"{n / 48000:.1f} s; identical for the first {(diff[0] if len(diff) else n) / 48000:.2f} s")
    r, s = stats(ref, new, n)
    print(f"ref-new:  max RMS difference {r:.4f}, max spectrum difference {s:.4f} (log10)")
    if len(dumps) > 2:
        r, s = stats(ref, dumps[2], n)
        print(f"ref-ref2: max RMS difference {r:.4f}, max spectrum difference {s:.4f} (log10)")


if __name__ == "__main__":
    main()
