# For docs/xdl-format/README.md §3 (the container) and §5 (loading)

Appending a pack: its sections, the new TOC and the new section table are
written after the end of the file, synced (fsync), then the header pointing
at the new table, synced. The old TOC and table stay behind unused (a few KB
per file: 0.1% of a full GameCube conversion). Until the header is written
the file reads as before, so a write cut short (killed, full disk) loses
only that pack.

A cache file is not read (the source is loaded, as for an out-of-date file)
when its section table or any section runs past the end of the file. A pack
is damaged (loaded from its source, the file's other packs still read) when
a fixup's slot holds an offset past the end of its target segment, or its
song bank reference names no segment or lies past the end of it; a pack's
segments are read whole when it is found, before anything is placed.

A pack written this session is added to the open file's index: later loads
read it from the cache (reading) or skip recording it. Writing only, the
file's index is read once when it is opened.
