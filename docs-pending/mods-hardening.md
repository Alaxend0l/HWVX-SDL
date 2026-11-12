# Mods: what the loader and installer now enforce (fix-mods)

For docs/xdl-format/README.md §8 (mods) and §8.2 (Tested).

**Patches.** Patches apply only to car packs (type 0, root struct `CarAInfo`) and item packs
(type 2, root `ItemRecord`), the same `ROOT_KIND` as tools/xdl_mod.py. Every object a patch reads
or writes must lie inside the pack's data blob, along with the root table and each link target.
An index in an object path is allowed only into an array whose length the schema gives (a `num*`
or `count*` field, or a `[numX]` in the link's description), and it is checked against that
count. Anywhere else the only index allowed is 0. Fields that count an array's entries can't be
patched (the port treats them as `MF_OTHER`, and the packer refuses them with "counts an array's
entries"). A value outside its field type's range is clamped and reported. Report lines from
loading packs appear only once.

**Load order.** `after` is applied as a stable topological sort, so mods.jsonc's order is kept
wherever no `after` says otherwise. Mods in a cycle keep their list order and are reported.
When two mods bring the same entry, the later mod's entry is written to ids.lock once, so the
file is no longer rewritten on every start. `HWVX_MODS_LOG=1` also logs the load order.

**Packages (check.json, tool_version 2).** `files` is `{name: {"size": n, "sha256": hex}}`.
`Port_ModsInstall` and `xdl_mod.py install` both refuse a package when:
- a file has a kind the check doesn't read, or a name with a hidden part;
- an entry is compressed, or a name appears twice;
- check.json isn't from tools/xdl_mod.py with format_version 1 and tool_version 2 or later;
- check.json names a different mod, or its result isn't "no game data found";
- the files it lists aren't exactly the package's files, with matching sizes and sha256.

Older packages have to be re-packed. A mod folder counts as "checked" only when its check.json
passes the same test against the files on disk.

**Install.** The package is unpacked to `<id>.part`. If an old copy exists, it is renamed to
`<id>.part-old`, the new copy is renamed into place, and then the old copy is deleted; if
something fails, the old copy is put back. A folder named `<id>` that holds some other mod (or
no mod) blocks the install. Once the new copy is in place, a copy of the same mod in any other
folder is removed. Removing a folder never follows links: a working folder symlinked into mods/
loses only the link.

**Packer.** Hex is now decoded from .txt and .md files as well as JSON strings, including hex
with `0x` prefixes or separators (whitespace, commas, semicolons, colons) and 2- or 4-byte words
(also read little-endian). Base64 runs are decoded the same way, across line breaks. Arrays of
numbers in JSON, nested arrays flattened, are checked as u8 (and s8), f32 LE and f32 BE. When a
mod's `releases` names a release the index has no data for, it is refused unless
`--allow-partial-index` is passed; that case is recorded in check.json as `unindexed_releases`.

**Tested:** tools/test_xdl_mod.py. With HWVX_BIN (default build/hwvx), it also runs the port's
`--install-mod` against crafted packages and symlinked folders. tools/mod_race_test.sh installs
a package and runs a race with HWVX_MODS_LOG=1, which shows the sample's 5 car values.
