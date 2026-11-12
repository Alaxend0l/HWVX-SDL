#!/bin/bash
# tools/make_test_music.sh OUTDIR: test tracks for streamed music
# (docs-pending/streamed-music.md), made with the local ffmpeg; nothing from
# the game. 8 s at 44.1 kHz stereo: 0-2 s a 300 Hz tone (the intro), 2-6 s an
# A major dyad (440 + 554.37 Hz, the loop), 6-8 s 1000 Hz (past the loop end:
# never heard once looping). LOOPSTART=88200 LOOPEND=264600 tags (Vorbis
# comments; ID3 TXXX in the MP3). Writes track.ogg, track.mp3, track.flac,
# track.wav (the WAV untagged, for the sidecar / manifest loop points).
set -e
OUT="$1"
mkdir -p "$OUT"
E='if(lt(t,2),0.4*sin(2*PI*300*t),if(lt(t,6),0.25*sin(2*PI*440*t)+0.25*sin(2*PI*554.37*t),0.4*sin(2*PI*1000*t)))'
SRC="aevalsrc=exprs='$E|$E':s=44100:d=8"
TAGS=(-metadata LOOPSTART=88200 -metadata LOOPEND=264600)
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "$SRC" -c:a vorbis -strict -2 -ac 2 "${TAGS[@]}" "$OUT/track.ogg"
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "$SRC" -c:a libmp3lame -b:a 192k "${TAGS[@]}" "$OUT/track.mp3"
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "$SRC" -c:a flac "${TAGS[@]}" "$OUT/track.flac"
ffmpeg -hide_banner -loglevel error -y -f lavfi -i "$SRC" -c:a pcm_s16le "$OUT/track.wav"
ls -l "$OUT"
