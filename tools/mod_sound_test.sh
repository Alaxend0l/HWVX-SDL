#!/bin/bash
# tools/mod_sound_test.sh BUILD DATADIR OUTDIR [SECONDS] [CAR]
#
# A mod's compressed sound in a race, end to end: a generated two-tone loop
# (700 + 1050 Hz, 44.1 kHz stereo, LOOPSTART/LOOPLENGTH tags) as Ogg Vorbis
# (ffmpeg makes the test file; MP3 when it has no Vorbis encoder), a mod
# whose "sounds" put it over car CAR's (default 7) engine sounds, packed by
# tools/xdl_mod.py (a partial index of HW_Cars in OUTDIR), installed with
# `hwvx --install-mod`, then a race with car CAR, HWVX_MODS_LOG=1 and
# HWVX_AUDIODUMP, force-killed after SECONDS (default 20). Passes when the log
# shows the sounds decoded and the mix holds the two tones in a 3:2 ratio
# (the engine's pitch moves both alike).
set -e
B="$1"; D="$2"; O="$3"; T="${4:-20}"; C="${5:-7}"
here=$(cd "$(dirname "$0")"; pwd)
mkdir -p "$O"; O=$(cd "$O"; pwd)
M="$O/tonemod"
rm -rf "$M" "$O/pref-soundmod"; mkdir -p "$M/sound" "$O/pref-soundmod"
python3 - "$O/tone.wav" <<'EOF'
import sys, wave, numpy as np
r = 44100; t = np.arange(2 * r) / r
x = 7000 * np.sin(2 * np.pi * 700 * t) + 5000 * np.sin(2 * np.pi * 1050 * t)
s = np.stack([x, x * 0.8], 1).astype("<i2")
with wave.open(sys.argv[1], "wb") as w:
    w.setnchannels(2); w.setsampwidth(2); w.setframerate(r); w.writeframes(s.tobytes())
EOF
ext=ogg; enc=(-c:a vorbis -strict -2)
ffmpeg -hide_banner -encoders 2>/dev/null | grep -q " vorbis " || { ext=mp3; enc=(-c:a libmp3lame -id3v2_version 3); }
ffmpeg -loglevel error -y -i "$O/tone.wav" "${enc[@]}" -metadata LOOPSTART=0 -metadata LOOPLENGTH=88200 "$M/sound/tone.$ext"
cat > "$M/mod.jsonc" <<EOF
{ "\$schema": "../../../schemas/Mod.schema.json", "format_version": 1, "id": "uid://tonetest",
  "name": "Tone test", "version": "1", "releases": ["gc"],
  "sounds": { "base://HW_Cars/car_$C/sound/0": "sound/tone.$ext",
              "base://HW_Cars/car_$C/sound/1": { "file": "sound/tone.$ext" } } }
EOF
[ -f "$O/index-cars.npz" ] || python3 "$here/xdl_mod.py" index "$D" -o "$O/index-cars.npz" --archives HW_Cars --no-geometry >/dev/null
python3 "$here/xdl_mod.py" pack "$M" -o "$O/tonemod.hwmod" --index "$O/index-cars.npz" --allow-partial-index | tail -1
HWVX_PREF_DIR="$O/pref-soundmod" "$B/hwvx" --data "$D" --install-mod "$O/tonemod.hwmod"
rm -f "$O/soundmod.f32"
FROMPREF=soundmod "$here/run_test.sh" "$B" "$D" "$O" soundmod2 "$T" HWVX_MODS_LOG=1 \
    "HWVX_START=race:car=$C" "HWVX_AUDIODUMP=$O/soundmod.f32" >/dev/null
grep "mods: Tone test" "$O/soundmod2.log" | sort -u
python3 - "$O/soundmod.f32" <<'EOF'
import sys, numpy as np
x = np.fromfile(sys.argv[1], "<f4").reshape(-1, 2).mean(axis=1)
r, n = 48000, 1 << 15
best = (0, 0, 0)
for a in range(0, len(x) - n, n // 2):         # the window where the 3:2 pair stands out most
    s = np.abs(np.fft.rfft(x[a:a + n] * np.hanning(n)))
    f = np.fft.rfftfreq(n, 1 / r)
    k = int(np.argmax(np.where((f > 80) & (f < 4000), s, 0)))
    j = int(round(k * 1.5))
    lo = np.median(s[max(1, k - 200):k + 200]) + 1e-9
    peak = 20 * np.log10(min(s[k], s[j - 2:j + 3].max()) / lo)
    if peak > best[0]:
        best = (peak, f[k], a / r)
print(f"strongest 3:2 pair: {best[1]:.0f} + {best[1] * 1.5:.0f} Hz, {best[0]:.0f} dB over the band's median, "
      f"at {best[2]:.1f} s")
sys.exit(0 if best[0] > 32 else 1)   # the race without the mod: 22 dB; with it: 41
EOF
