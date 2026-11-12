#!/bin/bash
# tools/mod_race_test.sh BUILD DATADIR OUTDIR PACKAGE.xmod [CAR] [SECONDS]
#
# A mod in a race, end to end: a fresh preferences folder (OUTDIR/pref-modrace),
# `hwvx --install-mod PACKAGE`, then a deterministic race with car CAR (default
# 33: the sample's) and HWVX_MODS_LOG=1, force-killed after SECONDS (default
# 25). Prints the install result and the log's "mods:" lines.
B="$1"; D="$2"; O="$3"; P="$4"; C="${5:-33}"; T="${6:-25}"
here=$(cd "$(dirname "$0")"; pwd)
mkdir -p "$O"; O=$(cd "$O"; pwd)
rm -rf "$O/pref-modrace"; mkdir -p "$O/pref-modrace"
HWVX_PREF_DIR="$O/pref-modrace" "$B/hwvx" --data "$D" --install-mod "$P" 2>&1 | grep -i "mod" | tail -3
FAST=1 FROMPREF=modrace "$here/run_test.sh" "$B" "$D" "$O" modrace2 "$T" HWVX_MODS_LOG=1 \
    "HWVX_START=race:car=$C" >/dev/null
grep "mods:" "$O/modrace2.log"
