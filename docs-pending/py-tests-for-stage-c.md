# Three unreferenced Python tests: notes for stage C (port to cargo tests, then delete)

Nothing runs these (not CMake/ctest, not any tools/*.sh). They stay in tools/ until stage C ports
them, since their modules (viewer_client, xdl_gltf, xdl_graph, xdl_struct) are still Python.
All need game data (a survey root or GC data); scratch output must stay in a build dir.

## tools/test_viewer_safety.py (183 lines; one game run, viewer protocol, src/port/viewer*.c)
Needs `--build` and `--data GC_DATA`; drives the game through viewer_client.Viewer. Checks:
- raycast -1e9..1e9 answers within 10 s; far diagonal answers; a ray with inf refused;
  down at the start hits; a clipped ray hits where the inside one does.
- startcheck with a far point answers (2 starts, first not clear); lanecheck 1e12 long
  answers; quadat far point answers.
- spawnbox path policy: non-.json refused; non-JSON .json refused; new .json written;
  overwriting its own JSON allowed.
- level with appends (tile cars grown by one): record World/0/tileCars/0 still works; record of
  the appended car says "appends"; pick a moved tile car returns its World/0/tileCars/ path with
  an offset inside the pack.
- count fields: set numTileCars (World/0 @8:u32) above the data refused ("count"); lower ok;
  back ok; above still refused.
- connections: a silent newcomer doesn't stop the client being served and is dropped after
  ~3 s; a wrong token doesn't either; a newcomer with the token is served and the old client
  is dropped.
Port as: a cargo integration test in tools/xdlrs (or the viewer client crate) gated on a data
path env var, skipped when unset.

## tools/test_xdl_gltf.py (277 lines; glTF output of tools/xdl_gltf.py)
Arg: SURVEY_ROOT (default HWVX-SDL/build-survey); scratch HWVX-SDL/build-gltf/test. Uses the
Khronos `gltf_validator` if on PATH/~/.local/bin, else a structural check. Checks per release:
- one car's glTF files: 0 validator errors; read back to the decoded triangles; one skinned
  geometry becomes a glTF skin; shadow volume is a LINES primitive.
- skinned car_29: weights (1,0,0,0); joints {0,1,3} (24/12/12 vertices); 8 joints, the level's
  bone counts in extras.
- re-encoded geometry decodes to the same triangles; PS2 refuses encoding edited geometry with
  a reason naming PS2.
- COLOR_0 per component type/normalised/vec3-vec4 encodes to the expected bytes; a primitive
  without extras.group is a GeomError naming it.
- PC geometry 12 level 1 has no colour array; an unlit group given colours gets one.
- content hash stable across write/load, changes when a vertex moves.
- edited glTF built back into a pack (xdl_graph): only base://HW_Cars/car_29/geometry/1
  re-encoded; same geometry count; LOD 0 group 0 lifted by 0.25 with the same triangle count;
  other groups and group textures unchanged; every other object extracts the same; geometry
  refs point at their geometry; the result still validates.

## tools/test_xdl_graph.py (288 lines; tools/xdl_graph.py round trips and edits)
Arg: SURVEY_ROOT; scratch HWVX-SDL/build-gltf/test-graph. Checks:
- per release and a pack of each kind: extract + build is byte-identical (node/file counts).
- ids: CarAInfo.engine is base://HW_Cars/car_5/CarAInfo/0/engine; one object reached by two
  paths has one id (aiScripts 0..2); geometry/texture keep pairing ids; ids unique and match
  the link grammar; repeats counted, longest id < 200 chars; typed files validate against
  their schema.
- edits: maxRpm changes only CarAInfo+0x1C..0x20, relocation tables unchanged; rollSound null
  -> one pointer slot fewer; reflectionGeom 14 -> 13 changes one ref.
- grow: data.bin 16+ bytes longer, built pack extracts to the same objects, same pointer count.
- errors: schema error names the file (engine.jsonc); link to nothing refused ("nowhere");
  field left out of a typed file refused (maxRpm), not built as 0.
- car_5 has no shader_relocs.bin; a new table is written with its pack.json section.
- w3_geo q5 links into geometry/1 by offset; an offset past the end of its object refused;
  offset links into a re-encoded geometry listed ("re-encoded").
- triggers: kind and parameters by name (airbox_entered); world volumes typed, enums by name;
  on -> toggle changes one word, relocations unchanged, extracts again as written; a point
  added to a list gives +1 pointer and a word (or alignment) longer; an unknown kind is named.
- extract: a non-empty folder without pack.jsonc refused (mentions --force); --force replaces
  it; a previous extract is replaced.
The whole-release round trip is `xdl_graph.py roundtrip`; this file is the quick set + edits.
