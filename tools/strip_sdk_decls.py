#!/usr/bin/env python3
"""Remove ad-hoc SDK/libc prototypes and duplicate SDK typedefs from game files.

The decomp declares the functions it calls locally, often with signatures that
differ from file to file. The port declares them once (src/port/sdk.h, gx.h and
the C library headers pulled in by common.h), so local copies are dropped.

  tools/strip_sdk_decls.py src/game/*.c src/game/include/*.h
"""
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
HEADERS = [ROOT / "src/port/sdk.h", ROOT / "src/port/gx.h", ROOT / "src/port/lgwheels.h"]

LIBC = """memset memcpy memmove memcmp strlen strcpy strncpy strcat strcmp strncmp strchr
strrchr strstr sprintf snprintf vsprintf printf tolower toupper rand srand atan atan2 sin
cos tan acos asin sqrt sqrtf pow fabs fabsf floor ceil fmod time exit free malloc abs
atoi""".split()

KEYWORDS = set("void char short int long float double signed unsigned const struct union enum typedef return else case goto do sizeof if while for switch".split())

SDK_TYPES = ["GXTexObj", "GXColor", "GXColorS10", "GXLightObj"]


def sdk_names():
    names = set()
    for h in HEADERS:
        for m in re.finditer(r"^[ \t]*(?:const[ \t]+)?(?:struct[ \t]+)?\w+[ \t\*]+([A-Za-z_]\w*)[ \t]*\(", h.read_text(), re.M):
            names.add(m.group(1))
    names -= KEYWORDS
    return names | set(LIBC)


def main():
    names = sdk_names()
    decl = re.compile(
        r"^[ \t]*(?:extern[ \t]+)?(?:static[ \t]+)?(?!(?:return|else|case|goto|do|typedef)\b)(?:const[ \t]+)?(?:struct[ \t]+)?\w+[\w \t\*]*?\b(" +
        "|".join(sorted(names)) + r")[ \t]*\([^;{}=()]*(?:\([^;{}=()]*\)[^;{}=()]*)*\)[ \t]*;[ \t]*(?://.*)?\n", re.M)
    tdef = re.compile(r"^typedef struct (" + "|".join(SDK_TYPES) + r")\s*\{[^}]*\}\s*\1\s*;\n", re.M)
    for path in sys.argv[1:]:
        p = pathlib.Path(path)
        src = p.read_text()
        out = decl.sub("", src)
        out = tdef.sub("", out)
        if out != src:
            p.write_text(out)
            print(f"{p.name}: -{src.count(chr(10)) - out.count(chr(10))} lines")


if __name__ == "__main__":
    main()
