#!/bin/bash
# tools/drive_test.sh BUILD DATADIR OUTDIR NAME SECONDS [VAR=value ...]
#
# A short scripted drive for physics comparisons: HWVX_START=race (joyride
# by default; pass HWVX_START=... to change it), the mission window
# dismissed, then the gas held with a turn partway (HWVX_AUTOINPUT, which a
# DRIVE=... variable replaces), the human's car logged every 30 frames
# (HWVX_CARLOG). Prints the carlog lines: handling, gear, rpm, wheel speed,
# speed, steering, turbo, flags, position. Deterministic: run it twice with
# one thing changed (HWVX_HANDLING=2) and compare.
B="$1"; D="$2"; O="$3"; N="$4"; T="$5"; shift 5
here=$(cd "$(dirname "$0")"; pwd)
FAST=1 "$here/run_test.sh" "$B" "$D" "$O" "$N" "$T" HWVX_START=race HWVX_CARLOG=1 \
    "HWVX_AUTOINPUT=${DRIVE:-308:a,377:a,446:a,515:a*1500,1100:right*90,1400:left*60}" "$@" >/dev/null
grep -o "carlog: .*" "$O/$N.log"
