r"""Match global interface pointers (DAT_xxxxxxxx) in the Ghidra C dump to the DX7 COM interfaces by the vtable
slots called through them (read-only).   python tools/d3dren_names_gptr.py
"""
import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import d3dren_names_com as C  # noqa: E402
import d3dren_names_fn as F  # noqa: E402

CAND = ['IDirectDraw7', 'IDirectDrawSurface7', 'IDirect3D7', 'IDirect3DDevice7', 'IDirectDrawClipper',
        'IDirectDrawPalette', 'IDirectDrawGammaControl', 'IDirect3DVertexBuffer7', 'IDirectDraw4']
L = {k: v for k, v in C.all_layouts().items() if k in CAND}
sec = F.sections()
use = collections.defaultdict(lambda: collections.defaultdict(set))
rx1 = re.compile(r'\(\*\*\(code \*\*\)\(\*\(?(?:\(int \*\))?(DAT_[0-9a-f]{8}) \+ (0x[0-9a-f]+|\d+)\)\)')
rx0 = re.compile(r'\(\*\*\(code \*\*\)\*\(?(?:\(int \*\*\))?(DAT_[0-9a-f]{8})\)')
for a, (n, body) in sec.items():
    for m in rx1.finditer(body):
        use[m.group(1)][int(m.group(2), 0)].add(a)
    for m in rx0.finditer(body):
        use[m.group(1)][0].add(a)
for g, offs in sorted(use.items()):
    print(g, ' nfuncs=%d' % len({f for s in offs.values() for f in s}))
    for iname, lay in L.items():
        if all(o % 4 == 0 and o // 4 < len(lay) for o in offs):
            print('    %-24s %s' % (iname, ' '.join('%x:%s' % (o, lay[o // 4]) for o in sorted(offs))))
