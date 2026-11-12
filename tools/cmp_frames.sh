#!/bin/bash
# tools/cmp_frames.sh DIR_A DIR_B
#
# Compares two frame dumps (HWVX_DUMP folders, tools/run_test.sh's dump-NAME):
# the frames both have, by name, byte for byte. Prints the first few that
# differ and a count; exit status 1 if any differ or none are common.
a=0; d=0
for f in "$1"/*; do
    n=$(basename "$f"); [ -f "$2/$n" ] || continue
    a=$((a+1))
    cmp -s "$f" "$2/$n" || { d=$((d+1)); [ $d -le 5 ] && echo "differs: $n"; }
done
echo "$a common frames, $d differ"
[ $a -gt 0 ] && [ $d -eq 0 ]
