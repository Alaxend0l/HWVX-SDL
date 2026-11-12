#!/bin/bash
# tools/stream_fuzz.sh OUTDIR: the stream player on damaged OGG / MP3 / FLAC /
# WAV files under AddressSanitizer and UBSan (tools/streamfuzz.c). Makes the
# test tracks (tools/make_test_music.sh), then from each: truncated copies,
# random bytes with and without the format's magic, and bit-flipped copies
# (seeded). Exits non-zero if the harness crashes or a sanitizer reports.
set -e
here=$(cd "$(dirname "$0")"; pwd)
O=$(mkdir -p "$1"; cd "$1"; pwd)
"$here/make_test_music.sh" "$O/good" >/dev/null
mkdir -p "$O/bad"
/usr/bin/python3 -I - "$O/good" "$O/bad" <<'EOF'
import os, random, sys
good, bad = sys.argv[1], sys.argv[2]
magic = {"ogg": b"OggS", "mp3": b"ID3\x04\x00\x00\x00\x00\x00\x00", "flac": b"fLaC", "wav": b"RIFF\x24\x00\x00\x00WAVEfmt "}
for ext in ("ogg", "mp3", "flac", "wav"):
    data = open(os.path.join(good, "track." + ext), "rb").read()
    rnd = random.Random(ext)
    out = {"empty": b"", "head100": data[:100]}
    for pct in (5, 30, 60, 95):
        out[f"trunc{pct}"] = data[:len(data) * pct // 100]
    out["garbage"] = bytes(rnd.getrandbits(8) for _ in range(200000))
    out["magic+garbage"] = magic[ext] + bytes(rnd.getrandbits(8) for _ in range(200000))
    for flips in (1, 10, 100, 1000):
        for seed in range(5):
            r = random.Random(f"{ext}{flips}{seed}")
            b = bytearray(data)
            for _ in range(flips):
                i = r.randrange(len(b))
                b[i] ^= 1 << r.randrange(8)
            out[f"flip{flips}-{seed}"] = bytes(b)
    # damage only the headers' region, where the parsers read sizes
    for seed in range(5):
        r = random.Random(f"{ext}head{seed}")
        b = bytearray(data)
        for _ in range(20):
            i = r.randrange(min(len(b), 8192))
            b[i] = r.getrandbits(8)
        out[f"headflip-{seed}"] = bytes(b)
    for name, b in out.items():
        open(os.path.join(bad, f"{name}.{ext}"), "wb").write(b)
EOF
SDLP=$(pkg-config --cflags --libs sdl3 2>/dev/null || echo "-I/opt/homebrew/include -L/opt/homebrew/lib -lSDL3")
cc -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer \
   -I"$here/../src/port" -I"$here/../src/game/include" -Wno-unused-function -Wno-tautological-compare \
   "$here/streamfuzz.c" $SDLP -lm -o "$O/streamfuzz"
"$O/streamfuzz" "$O"/good/track.* "$O"/bad/* > "$O/streamfuzz.log" 2>&1
echo "$(grep -c ': played' "$O/streamfuzz.log") played, $(grep -c ': fallback' "$O/streamfuzz.log") fell back, no sanitizer reports"
