r"""List every string in d3d.ren with the functions that reference it (read-only).

  python tools/d3dren_names_strings.py [substr]
"""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import d3dren_names_scan as S  # noqa: E402

o = S.load()
funcs, strings, refs = o['funcs'], o['strings'], o['refs']
users = {}
for f, r in refs.items():
    for t in r:
        users.setdefault(t, set()).add(f)
sub = sys.argv[1].lower() if len(sys.argv) > 1 else None
for a in sorted(strings):
    s = strings[a]
    if sub and sub not in s.lower():
        continue
    u = ' '.join('%08x' % x for x in sorted(users.get(a, [])))
    print('%08x %-60r %s' % (a, s[:100], u))
