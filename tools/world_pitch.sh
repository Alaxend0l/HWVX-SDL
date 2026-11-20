#!/bin/bash
# tools/world_pitch.sh BUILD DATADIR OUTDIR NAME WORLD_JSONC [SECONDS] [VAR=value ...]
# tools/world_pitch.sh --report OUTDIR/NAME.pitch     (the numbers of an earlier run again)
#
# How smoothly a car drives a custom world (game/Port_World.c): tools/world_drive.sh with
# HWVX_CARLOG=3, which logs the human car's pitch and the terrain contacts every frame
# (CarA.c Port_CarLog, CollisionTerrain.c OBoxHitsBSP; HWVX_CARLOG=4 also logs each box-corner
# contact: the corner, its motion, the plane it met, the cell). Prints, while the car moves (more
# than 0.5 units a frame) until it first leaves the world (y below FLOOR, default -1000): the
# distance driven, the pitch spikes (the pitch changing by more than SPIKE degrees, default 1.5,
# from one frame to the next; on smooth ground there should be none, landings and real bumps make
# some), and the edge and box-corner contacts, each per 100 units. An edge contact is the body box
# meeting a crease edge of the collision pack, a corner contact one of its corners meeting a face
# of the cell's BSP; on smooth ground every one is a phantom hit. A car standing still is not
# counted (it rests on its box on a deck, say).
report() {
    awk -v floor="${FLOOR:--1000}" -v spike="${SPIKE:-1.5}" '
        { p = $2; x = $4; y = $5; z = $6; e = $8; c = $10
          if (y < floor) exit
          if (n++) { d = sqrt((x - px)^2 + (z - pz)^2)
                     if (d < 50 && d > 0.5) { dist += d; dp = p - pp; if (dp < 0) dp = -dp; if (dp > spike) spikes++; edges += e; corners += c } }
          px = x; pz = z; pp = p }
        END { if (dist < 1) dist = 1
              printf "%s: %d frames, %.0f units driven, pitch spikes %d (%.2f / 100 units), edge contacts %d (%.2f / 100), corner contacts %d (%.2f / 100)\n",
                     FILENAME, n, dist, spikes, 100 * spikes / dist, edges, 100 * edges / dist, corners, 100 * corners / dist }' "$1"
}
if [ "$1" = "--report" ]; then report "$2"; exit; fi
B="$1"; D="$2"; O="$3"; N="$4"; W="$5"; T="${6:-30}"; shift 5; shift 2>/dev/null
here=$(cd "$(dirname "$0")"; pwd)
KEEP= FLOOR="${FLOOR:--1000}" "$here/world_drive.sh" "$B" "$D" "$O" "$N" "$W" "$T" HWVX_CARLOG=3 "$@" > /dev/null
grep -o "carpitch: .*" "$O/$N.log" > "$O/$N.pitch"
report "$O/$N.pitch"
