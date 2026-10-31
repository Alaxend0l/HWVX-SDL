#!/usr/bin/env python3
"""List constant byte offsets added to typed (non-byte) pointers in the game
sources: `GETP(x->field) + 0x24` is a byte offset only while `field` points
to u8; once the field is given a struct type it steps whole structs. Run it
after retyping pointer fields (clang, needs BUILD/compile_commands.json):

  tools/check_ptr_offsets.py [BUILD_DIR]

Prints file:line, the pointer type and the constant. Deliberate strides
(e.g. `matrices + i * 16` on f32 *) show up too; check each new one.
"""
import json, subprocess, shlex, sys, re
sys.setrecursionlimit(100000)
BYTE = re.compile(r'^(const )?(volatile )?(unsigned char|signed char|char|void|u8|s8|vu8)( const)? \*( const)?$')
st = {'f': None, 'l': None}
def upd(loc):
    if not loc: return
    if 'expansionLoc' in loc: upd(loc['expansionLoc']); return
    if 'file' in loc: st['f'] = loc['file']
    if 'line' in loc: st['l'] = loc['line']
def walk(n, out):
    if isinstance(n, dict):
        upd(n.get('loc')); r = n.get('range')
        if r: upd(r.get('begin'))
        if n.get('kind') == 'BinaryOperator' and n.get('opcode') in ('+', '-') and len(n.get('inner', [])) == 2:
            ty = n['inner'][0].get('type', {})
            q = ty.get('qualType', ''); d = ty.get('desugaredQualType', q)
            rr = n['inner'][1]
            while rr.get('kind') in ('ImplicitCastExpr', 'ParenExpr') and rr.get('inner'): rr = rr['inner'][0]
            if d.endswith('*') and not BYTE.match(d) and not BYTE.match(q):
                if rr.get('kind') == 'IntegerLiteral' and int(rr.get('value', '0')) >= 4:
                    out.append((st['f'], st['l'], d, int(rr['value'])))
                elif rr.get('kind') == 'BinaryOperator' and rr.get('opcode') == '*':
                    for c in rr.get('inner', []):
                        while c.get('kind') in ('ImplicitCastExpr', 'ParenExpr') and c.get('inner'): c = c['inner'][0]
                        if c.get('kind') == 'IntegerLiteral' and int(c.get('value', '0')) >= 4:
                            out.append((st['f'], st['l'], d, int(c['value'])))
        for c in n.get('inner', []): walk(c, out)
build = sys.argv[1] if len(sys.argv) > 1 else 'build'
db = json.load(open(build + '/compile_commands.json'))
for e in db:
    f = e['file']
    if '/src/game/' not in f: continue
    args = shlex.split(e['command']); i = args.index('-o'); del args[i:i+2]
    args = [a for a in args if a != '-c'] + ['-fsyntax-only', '-w', '-Xclang', '-ast-dump=json']
    try:
        ast = json.loads(subprocess.run(args, cwd=e['directory'], capture_output=True, text=True).stdout)
    except Exception:
        print("PARSE FAIL", f, file=sys.stderr); continue
    st['f'] = None; st['l'] = None
    out = []; walk(ast, out)
    lines = open(f).read().split('\n')
    for file, line, t, v in out:
        if file == f and line:
            print(f"{f.split('/src/game/')[-1]}:{line}: [{t}] {v:#x}: {lines[line-1].strip()[:150]}")
