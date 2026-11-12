# For docs/xdl-format/README.md §10.8 (after the xdl_graph / xdl_struct tool list)

### 10.8.x The Rust core: tools/xdlrs (editor without Python, stage 1)

`tools/xdlrs` is a library crate (and a small CLI) that the editor (tools/browser) links, so the
release editor needs no Python. Stage 1 ports `xdl_struct.py` and `xdl_tac.py`; the level
compiler and the mod check (stages 2 and 3) build on it. The Python files stay: they are the
reference, and the research tools import them.

* Same input, same output: struct layouts from schemas/, f32 text (the shortest text that reads
  back as the same f32, `"0"` and `"-0.0"` for zeros, no `.0` on whole numbers), `json.dumps`
  rules (ensure_ascii, separators, `repr(float)`), JSONC as `jsonc_loads` reads it (comments,
  trailing commas, an unterminated string or comment is an error), byte order per release,
  `struct.pack`'s ranges and messages.
* A decoded f32 is held as the JSON value its text reads back as (an integer for `"7400"`),
  which is what the Python tools work on once a file is saved; packed back it is the same f32.
* Errors say which Python exception they are (`Kind::Xdl`, `Tac`, `Json`, `Struct`, `Graph`,
  and `Uncaught` where the Python stops with a traceback), so callers catch what the Python
  catches.
* The browser's JSONC code (`jsonc_edit`, the lenient reader) moved into the crate.
* Validation is `MiniValidator` (the Python's fallback): with the `jsonschema` package installed
  the Python tools check more (anyOf, enum, oneOf ...).

```
xdlrs struct extract|build|roundtrip|observe ...     as xdl_struct.py
xdlrs tac survey SURVEY_ROOT                         as xdl_tac.py SURVEY_ROOT
python3 tools/xdlrs_parity.py [--quick]              the port against the Python, byte for byte
```

The parity test runs both on every schema, the level and mod samples, every car object of the
GC, PC and PS2 surveys (round trip, extract, build with good and bad edits, observe), every
world pack's World record (decode, and the symbolic encoder xdl_graph uses), every retail TAC
parameter list, the samples' triggers with edits, and `xdl_tac.Extract` over xdl_graph's
extraction of every world pack.
