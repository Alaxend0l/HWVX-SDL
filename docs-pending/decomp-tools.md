# The decomp tools are Rust (stage D): superproject doc edits

The decomp workflow scripts moved into one Rust program, `hwvx-decomp` (HWVX-SDL `tools/decomp`,
`cargo build --release --manifest-path tools/decomp/Cargo.toml`); the Python versions are deleted.
HWVX-SDL/README.md is already updated. Edits for docs/xdl-format/README.md:

1. Line 887 (§ on the prototype's executable):
   `` `tools/mipsdis.py ELF FUNC...` disassembles functions by name ``
   -> `` `hwvx-decomp mipsdis ELF FUNC...` (HWVX-SDL `tools/decomp`) disassembles functions by name ``
2. Line 1021 (tool list):
   `` * `tools/mipsdis.py`: disassembles the prototype's executable. ``
   -> `` * `hwvx-decomp mipsdis` (HWVX-SDL `tools/decomp`): disassembles the prototype's executable. ``

Optional, docs/xdl-format/xdlrs.md: a line saying the decomp tools are a separate crate
(`tools/decomp`, `hwvx-decomp`: import, strip-sdk-decls, gen-protos, slotify, fix-casts, fieldify,
view-externs, check-ptr-offsets, offsets-to-fields, gen-data, asm-args, mipsdis), not part of xdlrs.
