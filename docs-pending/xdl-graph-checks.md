# xdl_graph build checks (for docs/xdl-format/README.md §10.8)

After the paragraph "A link into a node is `<id>/o<hex offset>`. Inside a geometry it is a
structural name ...", add:

Links that reach into a geometry by offset (`geometry/1/o3b04`: world quads' orphans point into
vertex and display-list data, about 18,000 such links in the GameCube world geometry packs)
can't follow a re-encoding. Building a project whose edited glTF re-encodes such a geometry is
refused, with the links listed. Every `o<hex>` offset must lie inside its object.

Under **Files**, typed objects, add:

A typed file lists every field. Its base (`.base.jsonc`) has the JSON's fields zeroed, so a
field left out is an error naming it, not a 0 (top-level texture links excepted: the base keeps
their bytes).

In the command list: `extract` / `extract-archive` replace the output folder only when it is
empty or a previous extract (has `pack.jsonc`); `--force` replaces any folder.

Building writes a relocation table the source pack lacked (car_5 has no `shader_relocs.bin`)
once an edit gives it entries, and adds its `pack.json` section.
