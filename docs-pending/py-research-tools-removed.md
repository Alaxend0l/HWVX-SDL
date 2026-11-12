# Research scripts removed (stage A of "tools are Rust")

HWVX-SDL branch `pyA` deletes these one-off research scripts from tools/ (git history keeps
them): asset_stats, audio_compare, tac_survey, tac_layouts, world_fields, geom_limits_probe,
stats_summary, xgc_inspect, net_hostile, net_tunnel_check, level_summary, typed_bytes,
xdl_cache_diff, stream_check, proto_pack, make_gc_image, gen_net_roots (all `.py`).

Superproject doc edits (exact; old -> new):

## docs/xdl-format/asset-limits.md

- line 12: "Release numbers come from `tools/asset_stats.py` (all three releases, counts and
  extremes only) and from runs with `HWVX_HEAPLOG=1`." ->
  "Release numbers come from `tools/asset_stats.py` (all three releases, counts and extremes
  only; the script was removed, git history keeps it) and from runs with `HWVX_HEAPLOG=1`."
- line 52: "`tools/asset_stats.py`)." -> "measured with `tools/asset_stats.py`, since removed)."
- line 95: "**Test:** `tools/geom_limits_probe.py` puts a generated sphere in place of a car
  group, encodes it ..." -> "**Test:** `tools/geom_limits_probe.py` (removed; git history keeps
  it) put a generated sphere in place of a car group, encoded it for each release and decoded
  it back."

## docs/xdl-format/README.md

- line 648 (streamed music test): "measured with `tools/stream_check.py` (each tone's level per
  window):" -> "measured with `tools/stream_check.py` (each tone's level per window; the script
  was removed, git history keeps it):"
- line 670: "so `tools/audio_compare.py` sets the difference against pc-data run twice" ->
  "so the difference was set against pc-data run twice (`tools/audio_compare.py`, since removed)"
- line 682: "(`tools/audio_compare.py`: RMS 0.0008)" -> "(RMS 0.0008, measured with the since
  removed `tools/audio_compare.py`)"
- line 1024 (prototype tools list): drop the bullet "`tools/proto_pack.py`: one of its
  archives, TOC and pack header; `--song` writes ..." or replace it with "`tools/proto_pack.py`
  (an archive's TOC and pack header, `--song` for a pack's song and bank) was removed; git
  history keeps it."
- line 1742: "tools/level_summary.py WORLD lists a retail level's records and triggers, one line
  each, so you can see how a mode's levels are made. For example, ..." -> "A retail level's
  records and triggers, listed one line each (by `tools/level_summary.py`, since removed; git
  history keeps it), show how a mode's levels are made. For example, ..." (rest unchanged)

## docs/fields/objects.md

- line 7: "(`tools/world_fields.py`, `tools/xdl_tac.py SURVEY_ROOT`)" -> "(`tools/xdl_tac.py
  SURVEY_ROOT`; the per-field counts came from `tools/world_fields.py`, since removed)"
- § Tools (lines 167-171): delete the bullets for `tools/world_fields.py`,
  `tools/typed_bytes.py` and `tools/tac_survey.py`; add one line under the list: "The research
  scripts these tables came from (`world_fields.py`: per-field zeros, distinct values and
  pointer slots; `typed_bytes.py`: bytes by kind; `tac_survey.py` with `tac_layouts.py`: the
  parameter layouts) were removed; git history keeps them."

## docs/windows/README.md

- line 15: "`tools/wine_run.sh` runs it, `tools/stats_summary.py` sums the log by the minute." ->
  "`tools/wine_run.sh` runs it; the table's per-minute sums came from `tools/stats_summary.py`
  (removed; git history keeps it)."
- line 69: "`tools/make_gc_image.py DISC OUT [--nkit]` builds a plain image or one with NKit's
  layout from an extracted disc (sys/ + files/). Its output is the player's data: never commit
  it." -> "The test images were built from an extracted disc (sys/ + files/), plain and with
  NKit's layout, by `tools/make_gc_image.py` (removed; git history keeps it). Such images are the
  player's data: never commit them."

Not removed (still needed; see the stage A report): world_stats.py (docs/worlds/README.md
lines 47-51 and 333 stay as they are).
