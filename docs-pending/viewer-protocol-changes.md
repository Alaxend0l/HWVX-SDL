# viewer-protocol.md changes (branch fix-viewer)

For the superproject's docs/viewer-protocol.md; each item says where.

## What it is (add after "It listens on 127.0.0.1 only")

- **A token.** A connection's first request has to be `{"cmd": "hello", "token": T}` with T the
  viewer's token, or the viewer answers `{"ok": false, "error": ...}` and closes it. The token is
  `HWVX_VIEWER_TOKEN`; unset, the viewer makes one up and logs `viewer: token T`. A first line that
  looks like HTTP (`POST / HTTP/1.1`, any method, or `HTTP/` in it) is closed without an answer:
  a web page can reach 127.0.0.1 and send a body of JSON lines, and that is not a client.
  `tools/browser` and `viewer_client.py` make up a token when they start the game and pass it;
  connecting to a running viewer they take `--token T` (default `HWVX_VIEWER_TOKEN`).
  Still one client at a time: a new connection replaces the old one even before its hello
  (a page could knock the browser off, not drive the viewer).

## Framing (add)

A request line is at most 1 MB; a longer one is answered `{"ok": false}` and the connection closed.

## Commands table

- `hello`: takes `token` (required on the first request; later hellos may leave it out).
- `load`: a pack that isn't in the file's table of contents is an error **before** anything is
  unloaded: the pack shown (and a loaded copy of the one asked for) stays.
- `set`: add argument `force` (bool, default false). An object link takes a node of the
  schema's `x-link` target type (a target `void` or none: any node), at a 4-byte aligned offset;
  another type is refused (`CarAInfo/0 is a CarAInfo, not a CarAEngine (force: true ...)`), and a
  raw `@hex` target needs `force: true`. Raw `@HEX:TYPE` offsets are hex that fits in 32 bits and
  in the node; array indices are 0 .. n-1 (decimal, nothing else). A store that touches any
  byte of a pointer slot is refused (a u16 at +2 of one, an f64 over one).
- `background`: `color` is three numbers, each clamped to 0 .. 255; anything else is an error.
- `screenshot`: `path` ends in `.bmp` or `.png`; an existing file there is replaced only when it
  is a BMP or PNG (`... is there and isn't an image: not overwritten`).

## Tests and tools (add)

`tools/browser/tests/protocol.rs` also checks: HTTP-looking and token-less connections dropped,
wrapped/oversized `@hex` offsets and bad indices refused, pointer-slot overlap refused, link type
and force, a missing pack leaving the shown one loaded, background and screenshot path checks.
`hwvx-browser --app-shot` exits 1 on an error reply, a lost viewer, or after 120 s.
