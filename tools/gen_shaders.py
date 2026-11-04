#!/usr/bin/env python3
"""Regenerate src/port/gx_shaders.h from src/port/shaders/.

The header holds every shader in every form a renderer needs, so building the
game needs no shader tools:

- GLSL source for OpenGL (gx.vert/gx.frag, includes inlined, no #version);
- SPIR-V (Vulkan), MSL (Metal) and DXIL (D3D12) for SDL_GPU, made with
  glslang -> spirv-cross -> dxc;
- for SDL_GPU, also a pixel shader specialised to each TEV setup in
  tools/tevkeys.txt (src/port/gx_tevgen.h; the OpenGL backend makes its own
  at run time). Setups not in the list use gx.frag. To add the ones a part of
  the game uses, play it with HWVX_TEVKEYS=tools/tevkeys.txt and rerun this.
  The GLSL comes from gx_tevgen.c through tools/tevgen_glsl.c, built here
  with cc and SDL3 (pkg-config).

Tools are looked up on PATH, then in ~/.local/opt/shadertools/{bin,dxc/bin}
(override with --tools DIR). dxc needs its libdxil.so next to it (lib/) to
sign the DXIL; unsigned DXIL is rejected by D3D12.

Run after editing anything in src/port/shaders/.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src", "port", "shaders")
OUT = os.path.join(ROOT, "src", "port", "gx_shaders.h")
TEVKEYS = os.path.join(ROOT, "tools", "tevkeys.txt")
# gx_gl.c FragDecl: specialised shaders reuse gx.frag up to here
DECL_END = "\nvec4 tex(int map"

# (file, stage, also emitted as GLSL for OpenGL)
SHADERS = [
    ("gx.vert", "vert", True),
    ("gx.frag", "frag", True),
    ("present.vert", "vert", False),
    ("present.frag", "frag", False),
]


def find_tool(name, extra):
    for d in extra:
        p = os.path.join(d, name)
        if os.path.isfile(p) and os.access(p, os.X_OK):
            return p
    p = shutil.which(name)
    if p is None:
        sys.exit(f"gen_shaders: {name} not found (see --tools)")
    return p


def inline_includes(path):
    out = []
    with open(path) as f:
        for line in f:
            m = re.match(r'\s*#include\s+"([^"]+)"', line)
            if m:
                out.append(inline_includes(os.path.join(os.path.dirname(path), m.group(1))))
            else:
                out.append(line)
    return "".join(out)


def run(cmd, env=None):
    r = subprocess.run(cmd, capture_output=True, text=True, env=env)
    if r.returncode != 0:
        sys.exit(f"gen_shaders: {' '.join(cmd)} failed:\n{r.stdout}{r.stderr}")


def c_name(file, kind):
    return file.replace(".", "_") + "_" + kind


def c_bytes(name, data):
    lines = [f"static const unsigned char {name}[{len(data)}] = {{"]
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join(f"0x{b:02X}" for b in data[i:i + 16]) + ",")
    lines.append("};")
    return "\n".join(lines)


def c_string(name, text):
    lines = [f"static const char {name}[] ="]
    for line in text.splitlines():
        lines.append('    "' + line.replace("\\", "\\\\").replace('"', '\\"') + '\\n"')
    lines[-1] += ";"
    return "\n".join(lines)


def tev_variants(tmp, frag_src, compile_sdlgpu):
    """gx_tev_variants[]: SDL_GPU's specialised pixel shaders (gx_sdlgpu.c)."""
    end = frag_src.find(DECL_END)
    if end < 0:
        sys.exit(f"gen_shaders: gx.frag has no {DECL_END.strip()!r} (see gx_gl.c FragDecl)")
    decl = os.path.join(tmp, "decl.glsl")
    with open(decl, "w") as f:
        f.write(frag_src[:end + 1])
    tool = os.path.join(tmp, "tevgen_glsl")
    flags = subprocess.run(["pkg-config", "--cflags", "--libs", "sdl3"], capture_output=True, text=True)
    if flags.returncode != 0:
        sys.exit("gen_shaders: pkg-config can't find sdl3 (needed for tools/tevgen_glsl.c)")
    port = os.path.join(ROOT, "src", "port")
    run([os.environ.get("CC", "cc"), "-std=gnu11", "-O1", "-I" + port, "-I" + os.path.join(ROOT, "src", "game", "include"),
         os.path.join(ROOT, "tools", "tevgen_glsl.c"), os.path.join(port, "gx_tevgen.c"),
         *flags.stdout.split(), "-o", tool])
    out = os.path.join(tmp, "tev")
    os.mkdir(out)
    keys = TEVKEYS if os.path.exists(TEVKEYS) else os.devnull
    run([tool, keys, decl, out])
    with open(os.path.join(out, "keys")) as f:
        keylines = [l.split() for l in f if l.strip()]
    parts, rows = [], []
    for i, k in enumerate(keylines):
        with open(os.path.join(out, f"tev{i}.frag")) as f:
            src = f.read()
        spv, msl, dxil = compile_sdlgpu(tmp, f"tev{i}.frag", "frag", src)
        parts.append(c_bytes(f"gx_tev{i}_spv", spv))
        parts.append(c_string(f"gx_tev{i}_msl", msl))
        parts.append(c_bytes(f"gx_tev{i}_dxil", dxil))
        stages = ", ".join(f"0x{x}ull" for x in k[1:])
        rows.append(f"    {{ 0x{k[0]}u, {{ {stages} }},\n"
                    f"      gx_tev{i}_spv, sizeof(gx_tev{i}_spv), gx_tev{i}_msl, gx_tev{i}_dxil, sizeof(gx_tev{i}_dxil) }},")
    print(f"{len(keylines)} specialised pixel shaders from {os.path.relpath(keys, ROOT)}")
    table = [
        "/* Specialised pixel shaders: GXTevKey misc and stages (gx_tevgen.h), then the code. */",
        "typedef struct GXTevVariant {",
        "    unsigned int misc;",
        "    unsigned long long stage[16];",
        "    const unsigned char *spv;",
        "    unsigned int spvSize;",
        "    const char *msl;",
        "    const unsigned char *dxil;",
        "    unsigned int dxilSize;",
        "} GXTevVariant;",
        "",
        f"#define GX_NUM_TEV_VARIANTS {len(keylines)}",
    ]
    if keylines:
        table.append(f"static const GXTevVariant gx_tev_variants[{len(keylines)}] = {{")
        table += rows
        table.append("};")
    return "\n\n".join(parts + ["\n".join(table)])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    home = os.path.expanduser("~/.local/opt/shadertools")
    ap.add_argument("--tools", default=home, help="shader tool prefix (default: %(default)s)")
    args = ap.parse_args()
    extra = [os.path.join(args.tools, "bin"), os.path.join(args.tools, "dxc", "bin")]
    glslang = find_tool("glslang", extra)
    spirv_cross = find_tool("spirv-cross", extra)
    dxc = find_tool("dxc", extra)
    env = dict(os.environ)
    env["LD_LIBRARY_PATH"] = os.path.join(os.path.dirname(os.path.dirname(dxc)), "lib") + \
        (":" + env["LD_LIBRARY_PATH"] if env.get("LD_LIBRARY_PATH") else "")

    def compile_sdlgpu(tmp, name, stage, src):
        """SPIR-V, MSL and DXIL for one shader (GLSL without #version)."""
        vk = os.path.join(tmp, name)
        with open(vk, "w") as f:
            f.write("#version 450\n" + src)
        spv = vk + ".spv"
        run([glslang, "-V", "-S", stage, "-o", spv, vk])
        msl, hlsl = vk + ".msl", vk + ".hlsl"
        # --msl-decoration-binding: SPIR-V bindings become the MSL indices
        # SDL expects ([[texture(n)]]/[[sampler(n)]], [[buffer(0)]])
        run([spirv_cross, spv, "--msl", "--msl-version", "20100", "--msl-decoration-binding",
             "--output", msl])
        run([spirv_cross, spv, "--hlsl", "--shader-model", "60", "--hlsl-enable-compat", "--output", hlsl])
        dxil = vk + ".dxil"
        run([dxc, "-T", ("vs" if stage == "vert" else "ps") + "_6_0", "-E", "main", "-Fo", dxil, hlsl], env)
        with open(spv, "rb") as f:
            spv_data = f.read()
        with open(msl) as f:
            msl_data = f.read()
        with open(dxil, "rb") as f:
            dxil_data = f.read()
        return spv_data, msl_data, dxil_data

    parts = []
    with tempfile.TemporaryDirectory() as tmp:
        frag_src = None
        for file, stage, gl in SHADERS:
            src = inline_includes(os.path.join(SRC, file))
            if file == "gx.frag":
                frag_src = src
            if gl:
                parts.append(c_string(c_name(file, "glsl"), src))
            spv, msl, dxil = compile_sdlgpu(tmp, file, stage, src)
            parts.append(c_bytes(c_name(file, "spv"), spv))
            parts.append(c_string(c_name(file, "msl"), msl))
            parts.append(c_bytes(c_name(file, "dxil"), dxil))
        parts.append(tev_variants(tmp, frag_src, compile_sdlgpu))

    with open(OUT, "w") as f:
        f.write("/* Generated by tools/gen_shaders.py from src/port/shaders/; do not edit. */\n")
        f.write("#ifndef GX_SHADERS_H\n#define GX_SHADERS_H\n\n")
        f.write("\n\n".join(parts))
        f.write("\n\n#endif\n")
    print(f"wrote {os.path.relpath(OUT, ROOT)}")


if __name__ == "__main__":
    main()
