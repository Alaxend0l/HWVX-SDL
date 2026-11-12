# Vendored libraries

Decoders for streamed music (`src/port/musicstream.c`, docs-pending/streamed-music.md). Both
are unmodified official files, under their own public-domain / MIT(-0) notices (at the end of
each file). The configuration lives in musicstream.c, not in these files.

| file | version | source | sha256 |
|---|---|---|---|
| `miniaudio.h` | miniaudio 0.11.26 (2026-10-31) | https://github.com/mackron/miniaudio, tag `0.11.26` (commit 877ea4a7c2601c7b0f2a44e83dfac42e6470fea6) | 6b376c7fbb1352ba1e0e432ab66069b335ea9405e45cb6c30b97ef0f3c9c3e0e |
| `stb_vorbis.c` | stb_vorbis 1.23 (2026-11-08) | https://github.com/nothings/stb, commit 6d17754ecc01e3772fd70ef4de7541277c4bf095 (2026-11-10, the file's latest on master on 2026-11-12) | 7399fe71d37b84148b6a6742f4116313bd47492824e949ce7f3cdcc8cee13693 |

Configuration (musicstream.c): miniaudio with only its decoders (`MA_NO_DEVICE_IO`,
`MA_NO_ENGINE`, `MA_NO_NODE_GRAPH`, `MA_NO_RESOURCE_MANAGER`, `MA_NO_GENERATION`,
`MA_NO_ENCODING`, `MA_NO_THREADING`; its functions `static`), which use its built-in dr_wav,
dr_flac and dr_mp3, and stb_vorbis for OGG. stb_vorbis keeps its stdio file API: miniaudio
opens a file with it in pull mode, the one mode with real seeking; through read callbacks it
would use push mode, where every seek to a loop point decodes the file again from the start.

Update by replacing the files from the same sources and changing this table.
