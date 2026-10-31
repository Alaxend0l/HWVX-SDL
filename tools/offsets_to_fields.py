#!/usr/bin/env python3
"""Rewrite byte-offset struct accesses into field accesses, using the struct
layouts clang computes from the headers.

  tools/offsets_to_fields.py LAYOUTS FILE FUNC [VAR=Type ...] [--rename A=B ...]

LAYOUTS: output of `clang -Xclang -fdump-record-layouts-complete
-Xclang -fdump-record-layouts` on a file including the headers.

In function FUNC of FILE the variables given are retyped (their u8 * / void *
declarations, parameters included, become Type *); variables the function
already declares as pointers to a known struct are typed from their
declarations. Then every `BASE + 0xNN`
whose BASE has a known struct-pointer type is rewritten according to what
surrounds it:

    *(T *)(BASE + 0xNN)          -> BASE->field
    SLOT_LOAD(T, (BASE + 0xNN))  -> GETP(BASE->field)       (PTR32 fields)
    SLOT_STORE((BASE + 0xNN), v) -> SETP(BASE->field, v)
    f(..., BASE + 0xNN, ...)     -> &BASE->field            (a field starting there)
    BASE[0xNN]                   -> BASE->field             (byte fields; BASE one
                                                             of the variables given)
    M2C_FIELD(BASE, T *, 0xNN)   -> BASE->field

BASE may carry a (u8 *) / (char *) cast and parentheses. The types of
expressions are worked out from the layouts: variables, GETP(x->f) of PTR32
fields, chains of ->, . and [i]. Rounds repeat until nothing changes, so
nested accesses resolve inside out. A site is rewritten only when a field
starts at the offset with the access's size; the rest are listed.

Afterwards build and run tools/check_ptr_offsets.py: a leftover `BASE + 0xNN`
on a retyped variable now steps whole structs.
"""
import re
import sys

SCALAR = {'u8': 1, 's8': 1, 'char': 1, 'u16': 2, 's16': 2, 'u32': 4, 's32': 4, 'f32': 4,
          'u64': 8, 's64': 8, 'f64': 8, 'int': 4, 'BOOL': 4}
ACCESS = {'u8': 1, 's8': 1, 'char': 1, 'u16': 2, 's16': 2, 'u32': 4, 's32': 4, 'f32': 4,
          'f64': 8, 's64': 8, 'u64': 8, 'GXColor': 4, 'Vector4': 16, 'Vector': 16,
          'Matrix4': 64, 'Quaternion': 16}


# ---- layouts -------------------------------------------------------------

def parse_layouts(path):
    records = {}
    lines = open(path).read().split('\n')
    i = 0
    while i < len(lines):
        m = re.match(r'^\s*0 \| (struct|union) (.+)$', lines[i])
        if m and i > 0 and lines[i - 1].startswith('*** Dumping AST Record Layout'):
            rname = m.group(2).strip()
            root = {'off': 0, 'type': m.group(1) + ' ' + rname, 'name': None, 'kids': [], 'depth': -1}
            stack = [root]
            i += 1
            while i < len(lines) and not lines[i].startswith('*** Dumping'):
                l = lines[i]
                sm = re.search(r'\[sizeof=(\d+)', l)
                if sm:
                    root['size'] = int(sm.group(1))
                    break
                fm = re.match(r'^\s*(\d+)(?::\d+-\d+)? \|( +)(.+)$', l)
                if fm:
                    off = int(fm.group(1))
                    depth = (len(fm.group(2)) - 3) // 2
                    body = fm.group(3).strip()
                    parts = body.rsplit(' ', 1)
                    if len(parts) == 2 and re.match(r'^[A-Za-z_]\w*$', parts[1]) and not parts[0].endswith(('struct', 'union')) \
                            and '(unnamed' not in parts[1] and '(anonymous' not in parts[1]:
                        ftype, fname = parts
                    else:
                        ftype, fname = body, None
                    node = {'off': off, 'type': ftype, 'name': fname, 'kids': [], 'depth': depth}
                    while len(stack) > 1 and stack[-1]['depth'] >= depth:
                        stack.pop()
                    stack[-1]['kids'].append(node)
                    stack.append(node)
                i += 1
            records[rname] = root
            records.setdefault(rname.split('::')[-1], root)
        i += 1
    return records


def ptr32_target(node):
    kids = node['kids']
    if node['type'].startswith('union') and len(kids) == 2 and kids[0]['name'] == 'h' and kids[1]['name'] == '_t':
        m = re.match(r'^(.*?)\s*\*\s*\[0\]$', kids[1]['type'])
        if m:
            return m.group(1).strip()
    return None


def rec_key(t, R):
    t = t.strip()
    for k in (t, t.replace('struct ', '', 1).replace('union ', '', 1).strip()):
        if k in R:
            return k
    return None


def type_size(t, R):
    t = t.strip()
    m = re.match(r'^union .*\(unnamed(?: union)? at [^)]*\)\[(\d+)\]$', t)
    if m:
        return 4 * int(m.group(1))    # an array of PTR32 slots
    m = re.match(r'^(.*)\[(\d+)\]$', t)
    if m:
        s = type_size(m.group(1), R)
        return None if s is None else s * int(m.group(2))
    if t in SCALAR:
        return SCALAR[t]
    k = rec_key(t, R)
    if k and R[k].get('size'):
        return R[k]['size']
    return None


def node_size(n, R):
    if ptr32_target(n) is not None:
        return 4
    s = type_size(n['type'], R)
    if s is None:
        k = rec_key(n['type'], R)
        if k:
            s = R[k].get('size')
    if s is None and n['kids']:
        # an anonymous struct / union: from its members
        ends = []
        for k in n['kids']:
            ks = node_size(k, R)
            if ks is None:
                return None
            ends.append(k['off'] - n['off'] + ks)
        s = max(ends)
    return s


def node_record(n, R):
    """The record a non-PTR32 struct-typed field (or array element) is."""
    t = re.sub(r'\[\d+\]$', '', n['type'])
    return rec_key(t, R)


def find_path(node, base, off, size, mode, R):
    """Field path at offset `off` (absolute in the record whose root offsets start at
    `base`). mode: 'scalar' (size), 'ptr' (a PTR32), 'addr' (any field starting there).
    Returns (path, node) or None."""
    cands = []
    for k in node['kids']:
        ks = node_size(k, R)
        if ks is None:
            continue
        rel = k['off'] - base
        if not (rel <= off < rel + ks):
            continue
        cands.append((k, rel, ks))
    for k, rel, ks in cands:
        name = k['name']
        tgt = ptr32_target(k)
        if tgt is not None:
            if off == rel and mode in ('ptr', 'addr') or (off == rel and mode == 'scalar' and size == 4):
                return ([name], k)
            continue
        if mode == 'addr' and off == rel and name:
            return ([name], k)
        am = re.match(r'^(.*)\[(\d+)\]$', k['type'])
        if am:
            et = am.group(1).strip()
            es = type_size(et, R)
            if not es:
                continue
            idx = (off - rel) // es
            inner = off - rel - idx * es
            if et in SCALAR or et.endswith('*'):
                if inner == 0 and (mode == 'scalar' and es == size or mode == 'addr'):
                    return ([name, '[%d]' % idx], k)
                continue
            ek = rec_key(et, R)
            if ek:
                if inner == 0 and mode == 'addr':
                    return ([name, '[%d]' % idx], k)
                if inner == 0 and mode == 'scalar' and es == size:
                    return ([name, '[%d]' % idx], k)
                r = find_path(R[ek], 0, inner, size, mode, R)
                if r:
                    return ([name, '[%d]' % idx] + r[0], r[1])
            continue
        if k['kids']:
            if mode == 'scalar' and off == rel and ks == size and name and rec_key(k['type'], R) is None and k['type'] in ACCESS:
                return ([name], k)
            if mode == 'scalar' and off == rel and ks == size and name and k['type'].split()[-1] in ('Vector4', 'Quaternion', 'Matrix4', 'GXColor'):
                return ([name], k)
            r = find_path(k, base, off, size, mode, R)
            if r:
                return (([name] if name else []) + r[0], r[1])
            continue
        if mode == 'scalar' and off == rel and ks == size:
            return ([name], k)
    return None


FLOATS = {'f32', 'f64'}
INTS = {'u8', 's8', 'char', 'u16', 's16', 'u32', 's32', 'u64', 's64', 'int', 'BOOL'}


def leaf_type(node):
    if ptr32_target(node) is not None:
        return 'u32'
    t = re.sub(r'\[\d+\]$', '', node['type']).strip()
    return t


def access_text(txt, acc, node, after, before):
    """txt for an access of type acc to field node, or None if the types clash.
    Same-size integers of other signedness: reads get a cast, writes stay."""
    ft = leaf_type(node)
    if acc == ft or acc in ('Vector4', 'Quaternion', 'Matrix4', 'GXColor', 'Vector'):
        return txt
    if (acc in FLOATS) != (ft in FLOATS):
        return None
    if acc in INTS and ft in INTS:
        write = re.match(r'\s*(=(?!=)|\+=|-=|\|=|&=|\^=|<<=|>>=|\+\+|--)', after) or re.search(r'(\+\+|--)\s*$', before) \
            or re.search(r'&\s*$', before)
        if write:
            return txt
        return '((%s)%s)' % (acc, txt)
    return None


def join(base, path, arrow=True):
    out = '(' + base + ')' if re.match(r'^(?:[&*]|\(\w[\w\s]*\*\))', base) else base
    first = True
    for p in path:
        if p.startswith('['):
            out += p
        else:
            out += ('->' if first and arrow else '.') + p
            first = False
    return out


# ---- expression typing ---------------------------------------------------

def strip_parens(e):
    e = e.strip()
    while e.startswith('(') and match(e, 0) == len(e) - 1:
        e = e[1:-1].strip()
    return e


def match(s, i):
    pairs = {'(': ')', '[': ']'}
    close = pairs[s[i]]
    depth = 0
    for j in range(i, len(s)):
        if s[j] in '([':
            depth += 1
        elif s[j] in ')]':
            depth -= 1
            if depth == 0:
                return j
    return -1


CASTS = re.compile(r'^\((?:const\s+)?(?:u8|s8|char|void|struct \w+|\w+)\s*\*\)\s*')


def split_last(e):
    """Split e at its last top-level ->, . or [...]: (prefix, op, rest)."""
    depth = 0
    i = len(e) - 1
    while i >= 0:
        c = e[i]
        if c in ')]':
            if c == ']' and depth == 0:
                # find its [
                d = 0
                j = i
                while j >= 0:
                    if e[j] == ']':
                        d += 1
                    elif e[j] == '[':
                        d -= 1
                        if d == 0:
                            break
                    j -= 1
                if j > 0:
                    return e[:j], '[', e[j + 1:i]
                return None
            depth += 1
        elif c in '([':
            depth -= 1
        elif depth == 0:
            if e[i - 1:i + 1] == '->':
                return e[:i - 1], '->', e[i + 1:]
            if c == '.' and not e[i - 1].isdigit():
                return e[:i], '.', e[i + 1:]
        i -= 1
    return None


class Typer:
    def __init__(self, R, vars_, byte_vars=()):
        self.R = R
        self.vars = vars_
        # variables retyped from u8 * / void *: their `+ N` and `[N]` are bytes;
        # on the others (declared struct pointers) they step whole elements
        self.byte_vars = set(byte_vars)

    def declared(self, e):
        """e is a variable declared with a struct type: arithmetic on it is by element."""
        return e in self.vars and e not in self.byte_vars

    def ptr(self, e):
        """Record R if e has type R *."""
        e = strip_parens(e)
        m = CASTS.match(e)
        if m and m.group(0).replace(' ', '') not in ('(u8*)', '(s8*)', '(char*)', '(void*)'):
            k = rec_key(re.sub(r'[()*]|const', '', m.group(0)).strip(), self.R)
            if k:
                return k
        if e in self.vars:
            return self.vars[e]
        m = re.match(r'^GETP\((.*)\)$', e)
        if m and match(e, 4) == len(e) - 1:
            n = self.lval(m.group(1))
            if n is not None and isinstance(n, dict):
                t = ptr32_target(n)
                if t:
                    return rec_key(t, self.R)
        m = re.match(r'^&(.*)$', e)
        if m:
            n = self.lval(m.group(1))
            if isinstance(n, dict):
                return node_record(n, self.R) if not n['kids'] or rec_key(n['type'], self.R) else None
            if isinstance(n, str):
                return n
        return None

    def lval(self, e):
        """Field node (or record name for a whole element) an lvalue denotes."""
        e = strip_parens(e)
        sp = split_last(e)
        if not sp:
            return None
        pre, op, rest = sp
        if op == '[':
            r = self.ptr(pre)
            if r:
                return r            # an element: record name
            n = self.lval(pre)
            if isinstance(n, dict) and re.search(r'\[\d+\]$', n['type']):
                k = node_record(n, self.R)
                return k
            return None
        if op == '->':
            r = self.ptr(pre)
            if not r:
                return None
            return self.field(self.R[r], rest)
        if op == '.':
            n = self.lval(pre)
            if isinstance(n, str):
                return self.field(self.R[n], rest)
            if isinstance(n, dict):
                k = rec_key(n['type'], self.R)
                if k:
                    return self.field(self.R[k], rest)
                return self.field(n, rest)
        return None

    def field(self, node, name):
        for k in node['kids']:
            if k['name'] == name:
                return k
            if k['name'] is None and k['kids']:
                r = self.field(k, name)
                if r:
                    return r
        return None


# ---- rewriting -------------------------------------------------------------

def left_operand(s, end):
    """Start index of the operand ending at s[end-1] (identifier chain, calls)."""
    i = end
    while i > 0 and s[i - 1] == ' ':
        i -= 1
    j = i
    while j > 0:
        c = s[j - 1]
        if c in ')]' and j < i and (s[j].isalnum() or s[j] == '_'):
            break           # `(T)name`: a cast, not part of the operand
        if c in ')]':
            # balanced back
            d = 0
            k = j - 1
            while k >= 0:
                if s[k] in ')]':
                    d += 1
                elif s[k] in '([':
                    d -= 1
                    if d == 0:
                        break
                k -= 1
            j = k
            continue
        if c.isalnum() or c == '_':
            j -= 1
            continue
        if c == '>' and j >= 2 and s[j - 2] == '-':
            j -= 2
            continue
        if c == '.':
            j -= 1
            continue
        break
    # a cast right before
    m = re.search(r'\((?:u8|s8|char) \*\)\s*$', s[:j])
    if m:
        j = m.start()
    return j


def rewrite_strides(body, typer):
    """BASE + i * sizeof(*BASE) -> (&BASE[i])."""
    changed = False
    out = body
    for m in reversed(list(re.finditer(r'\s*\+\s*([A-Za-z_][\w.\->\[\]]*|\([^()]*\))\s*\*\s*(0x[0-9A-Fa-f]+|\d+)\b', out))):
        st = left_operand(out, m.start())
        base = strip_parens(re.sub(r'^\((?:u8|s8|char) \*\)\s*', '', strip_parens(out[st:m.start()])))
        if not base:
            continue
        rec = typer.ptr(base)
        if not rec or typer.R[rec].get('size') != int(m.group(2), 0):
            continue
        out = out[:st] + '(&' + base + '[' + m.group(1) + '])' + out[m.end():]
        changed = True
    return out, changed


def tidy(body):
    """(&E)->f -> E.f, (&E)[...]-free forms."""
    out = body
    while True:
        i = out.find('(&')
        found = False
        while i >= 0:
            j = match(out, i)
            if j > 0 and out[j + 1:j + 3] == '->':
                inner = out[i + 2:j]
                out = out[:i] + inner + '.' + out[j + 3:]
                found = True
                break
            i = out.find('(&', i + 1)
        if not found:
            return out


def rewrite(body, typer):
    changed = False
    body, changed = rewrite_strides(body, typer)
    OFFRE = re.compile(r'\s*\+\s*(0x[0-9A-Fa-f]+|\d+)\b')
    out = body
    # process from the end so indices stay valid
    sites = list(OFFRE.finditer(out))
    for m in reversed(sites):
        st = left_operand(out, m.start())
        base_txt = out[st:m.start()]
        base = strip_parens(re.sub(r'^\((?:u8|s8|char) \*\)\s*', '', strip_parens(base_txt)))
        if not base or base[0].isdigit():
            continue
        if typer.declared(base) and not re.match(r'^\(*\((?:u8|s8|char) \*\)', base_txt.strip()):
            continue
        rec = typer.ptr(base)
        if not rec:
            continue
        off = int(m.group(1), 0)
        R = typer.R
        before = out[:st]
        after = out[m.end():]
        # *(T *)( [ ( ] BASE + OFF [ ) ] )
        mb = re.search(r'\*\(\s*(\w+)\s*\*\s*\)\s*\(\s*(\(\s*)*$', before)
        if mb:
            nopen = before[mb.start():].count('(') - before[mb.start():].count(')')
            k = closers(after, nopen)
            size = ACCESS.get(mb.group(1))
            r = find_path(R[rec], 0, off, size, 'scalar', R) if size else None
            if r and k is not None:
                txt = join(base, r[0])
                if ptr32_target(r[1]) is not None:
                    txt += '.h'
                txt = access_text(txt, mb.group(1), r[1], after[k:], out[:mb.start()])
                if txt is not None:
                    out = out[:mb.start()] + txt + after[k:]
                    changed = True
                    continue
        # SLOT_LOAD(T, ( BASE + OFF ))
        mb = re.search(r'SLOT_LOAD\(\s*[^,()]+?(?:\([^()]*\))?\s*,\s*(\(\s*)*$', before)
        if mb:
            nopen = before[mb.start():].count('(') - before[mb.start():].count(')')
            r = find_path(R[rec], 0, off, 4, 'ptr', R)
            k = closers(after, nopen)
            if r and ptr32_target(r[1]) is not None and k is not None:
                if True:
                    out = out[:mb.start()] + 'GETP(' + join(base, r[0]) + ')' + after[k:]
                    changed = True
                    continue
        # SLOT_STORE(( BASE + OFF ), v)
        mb = re.search(r'SLOT_STORE\(\s*(\(\s*)*$', before)
        if mb:
            nopen = before[mb.start():].count('(') - before[mb.start():].count(')') - 1
            r = find_path(R[rec], 0, off, 4, 'ptr', R)
            ma = re.match(r'(\s*\))*\s*,', after)
            if r and ptr32_target(r[1]) is not None and ma and ma.group(0).count(')') == nopen:
                out = out[:mb.start()] + 'SETP(' + join(base, r[0]) + ',' + after[ma.end():]
                changed = True
                continue
        # address: an argument / initializer / operand ending there
        mbef = re.search(r'(?:(?<![*\s])\(|,|=|\breturn)\s*$', before)
        if re.search(r'\*\s*\(\s*$', before):
            mbef = None
        maft = re.match(r'\s*(?:,|;|\)(?!\s*(?:\[|\.|->)))', after)
        if mbef and maft:
            r = find_path(R[rec], 0, off, 0, 'addr', R)
            if r:
                out = out[:st] + '&' + join(base, r[0]) + after
                changed = True
                continue
    # BASE[0xNN] and M2C_FIELD(BASE, T *, 0xNN)
    for m in reversed(list(re.finditer(r'\[\s*(0x[0-9A-Fa-f]+|\d+)\s*\]', out))):
        st = left_operand(out, m.start())
        base = strip_parens(out[st:m.start()])
        if base not in typer.byte_vars:
            continue        # indexing a struct pointer picks an element
        rec = typer.ptr(base)
        if not rec:
            continue
        r = find_path(typer.R[rec], 0, int(m.group(1), 0), 1, 'scalar', typer.R)
        if r:
            txt = access_text(join(base, r[0]), 'u8', r[1], out[m.end():], out[:st])
            if txt is not None:
                out = out[:st] + txt + out[m.end():]
                changed = True
    for m in reversed(list(re.finditer(r'M2C_FIELD\(', out))):
        e = match(out, m.end() - 1)
        args = out[m.end():e]
        parts = [p.strip() for p in split_args(args)]
        if len(parts) != 3:
            continue
        base = strip_parens(re.sub(r'^\((?:u8|s8|char) \*\)\s*', '', parts[0]))
        rec = typer.ptr(base)
        tm = re.match(r'^(\w+)\s*\*$', parts[1])
        if not rec or not tm:
            continue
        size = ACCESS.get(tm.group(1))
        r = find_path(typer.R[rec], 0, int(parts[2], 0), size, 'scalar', typer.R) if size else None
        if r:
            txt = join(base, r[0]) + ('.h' if ptr32_target(r[1]) is not None else '')
            txt = access_text(txt, tm.group(1), r[1], out[e + 1:], out[:m.start()])
            if txt is not None:
                out = out[:m.start()] + txt + out[e + 1:]
                changed = True
    return out, changed


def closers(after, n):
    """Index past n closing parens at the start of `after` (spaces allowed), or None."""
    k = 0
    for _ in range(n):
        while k < len(after) and after[k] == ' ':
            k += 1
        if k < len(after) and after[k] == ')':
            k += 1
        else:
            return None
    return k


def split_args(s):
    parts = []
    depth = 0
    cur = ''
    for c in s:
        if c in '([':
            depth += 1
        elif c in ')]':
            depth -= 1
        if c == ',' and depth == 0:
            parts.append(cur)
            cur = ''
        else:
            cur += c
    parts.append(cur)
    return parts


def func_span(src, name):
    m = re.search(r'^(?![ \t])[^\n;{}]*\b' + re.escape(name) + r'\s*\([^;{]*\)\s*\{', src, re.M)
    if not m:
        raise SystemExit('function not found: ' + name)
    depth = 0
    j = m.end() - 1
    while j < len(src):
        if src[j] == '{':
            depth += 1
        elif src[j] == '}':
            depth -= 1
            if depth == 0:
                return m.start(), j + 1
        j += 1
    raise SystemExit('unbalanced: ' + name)


def main():
    args = sys.argv[1:]
    renames = []
    while '--rename' in args:
        i = args.index('--rename')
        renames.append(args[i + 1].split('=', 1))
        del args[i:i + 2]
    layouts, path, fname = args[:3]
    varmap = dict(a.split('=', 1) for a in args[3:])
    R = parse_layouts(layouts)
    src = open(path).read()
    a, b = func_span(src, fname)
    body = src[a:b]
    vars_ = {}
    for v, t in varmap.items():
        k = rec_key(t, R)
        if k is None:
            raise SystemExit('unknown type ' + t)
        vars_[v] = k
        body = re.sub(r'\b(?:u8|s8|void|char)\s*\*\s*' + re.escape(v) + r'\b', t + ' *' + v, body)
    for o, n in renames:
        body = body.replace(o, n)
    # struct pointers the function already declares (parameters and locals)
    for m in re.finditer(r'(?:^\s*|[,(;{]\s*)(?:const\s+)?(?:struct\s+)?([A-Za-z_]\w*)\s*\*\s*([A-Za-z_]\w*)\s*[,;=)]', body, re.M):
        k = rec_key(m.group(1), R)
        if k and m.group(2) not in vars_:
            vars_[m.group(2)] = k
    typer = Typer(R, vars_, varmap.keys())
    rounds = 0
    changed = True
    while changed and rounds < 60:
        body, changed = rewrite(body, typer)
        rounds += 1
    body = tidy(body)
    src = src[:a] + body + src[b:]
    open(path, 'w').write(src)
    left = [l for l in body.split('\n') if re.search(r'\+ 0x[0-9A-Fa-f]+|M2C_FIELD|\[0x[0-9A-Fa-f]+\]', l)]
    print('%s: %d rounds, %d lines with offsets left' % (fname, rounds, len(left)))
    for l in left[:60]:
        print('   ', l.strip()[:160])


if __name__ == '__main__':
    main()
