r"""Compact one-line-per-function map of d3d.ren for the naming pass (read-only).

  python tools/d3dren_names_map.py [lo hi]       # hex address range (default: all)

Columns: addr size ncallers ncallees | CRT/lib name | strings (first 2) | globals touched (first 6) | callees (first 5)
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import d3dren_names_scan as S  # noqa: E402

o = S.load()
funcs, strings, refs, calls, callers = o['funcs'], o['strings'], o['refs'], o['calls'], o['callers']
lib = json.load(open(os.path.join(S.ROOT, 'config', 'd3dren', 'libraries.json')))
libname = {int(a, 16): n for a, n in lib['names'].items() if int(a, 16)}
dnames = {}
for a, e, k, n, x in o['syms']:
    if k in ('data', 'import', 'label') and not n.startswith('DAT_') and not n.startswith('s_'):
        dnames.setdefault(a, n)

lo = int(sys.argv[1], 16) if len(sys.argv) > 1 else 0
hi = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0xffffffff
for fa in sorted(funcs):
    if not lo <= fa <= hi:
        continue
    fe, fn = funcs[fa]
    r = refs.get(fa, {})
    ss = []
    gl = []
    for t in sorted(r):
        if t in strings:
            ss.append(strings[t][:38].replace('\n', '\\n'))
        elif t >= 0x10046000 and t not in funcs:
            gl.append('%x' % (t & 0xfffff))
    cs = []
    for t in calls.get(fa, []):
        nm = libname.get(t) or (funcs[t][1] if t in funcs else '?')
        cs.append(nm if not nm.startswith('FUN_') else nm[4:])
    seen = []
    for c in cs:
        if c not in seen:
            seen.append(c)
    print('%08x %5d c%-3d f%-3d %s | %s | %s | %s' % (
        fa, fe - fa, len(callers.get(fa, ())), len(set(calls.get(fa, []))),
        libname.get(fa, ''), ' ; '.join(ss[:2]), ' '.join(gl[:6]), ' '.join(seen[:5])))
