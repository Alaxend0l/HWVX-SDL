#!/usr/bin/env python3
"""Sums up a run's HWVX_STATS / HWVX_FPSLOG log (and the NAME.mem samples
tools/wine_run.sh writes beside it), by the minute:

  tools/stats_summary.py OUT/NAME.log

Per minute of frames (3600): the frame rate and work per frame (FPSLOG),
the process's memory (HWVX_STATS 'process:' lines), the textures resident,
the staging buffers kept, and the outside resident size (.mem), last value
in the minute. Lines a build doesn't log show as '-'.
"""
import os
import re
import sys

log = sys.argv[1]
rows = {}
minute = 0
for line in open(log, errors="replace"):
    m = re.search(r"frame (\d+): .*\(\S+, ([\d.]+) ms/frame\)", line)
    if m:
        minute = int(m.group(1)) // 3600
        continue
    r = rows.setdefault(minute, {})
    m = re.search(r"^FPS ([\d.]+) .*, ([\d.]+) ms/frame of work", line)
    if m:
        r.setdefault("fps", []).append(float(m.group(1)))
        r.setdefault("work", []).append(float(m.group(2)))
    m = re.search(r"process: ([\d.]+) MB committed, ([\d.]+) MB resident", line)
    if m:
        r["commit"], r["res"] = float(m.group(1)), float(m.group(2))
    m = re.search(r"textures: (\d+) resident, ([\d.]+) MB", line)
    if m:
        r["tex"] = f"{m.group(1)}/{float(m.group(2)):.0f}MB"
    m = re.search(r"staging: ([\d.]+) copy passes/frame; ([\d.]+) MB", line)
    if m:
        r["passes"], r["staging"] = float(m.group(1)), float(m.group(2))

mem = {}
memf = os.path.splitext(log)[0] + ".mem"
if os.path.exists(memf):
    for line in open(memf):
        t, v = line.split()
        mem[int(t) // 60] = float(v)

print(f"{'min':>3} {'fps':>6} {'work ms':>8} {'commit MB':>10} {'resident MB':>11} {'outside MB':>10} "
      f"{'textures':>11} {'passes/f':>8} {'staging MB':>10}")
for k in sorted(set(rows) | set(mem)):
    r = rows.get(k, {})
    def f(v, fmt):
        return format(v, fmt) if v is not None else "-"
    fps = sum(r["fps"]) / len(r["fps"]) if "fps" in r else None
    work = sum(r["work"]) / len(r["work"]) if "work" in r else None
    commit = r.get("commit")
    if commit is not None and commit > 1e7:
        commit = None   # Wine's counter wraps now and then
    print(f"{k:>3} {f(fps, '6.2f'):>6} {f(work, '8.2f'):>8} {f(commit, '10.1f'):>10} {f(r.get('res'), '11.1f'):>11} "
          f"{f(mem.get(k), '10.1f'):>10} {r.get('tex', '-'):>11} {f(r.get('passes'), '8.2f'):>8} "
          f"{f(r.get('staging'), '10.1f'):>10}")
