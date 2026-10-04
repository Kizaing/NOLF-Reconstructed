r"""Find DX SDK GUIDs (DEFINE_GUID lines of the DX8 SDK headers) in d3d.ren images (read-only).

  python tools/d3dren_names_guids.py            # prints "addr name guid  users"
"""
import glob
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import d3dren_names_scan as S  # noqa: E402

INC = r'E:\AVP2Source\directx8-msdx8\include'
rx = re.compile(r'DEFINE_GUID\(\s*(\w+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,'
                r'\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,'
                r'\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*\)')


def all_guids():
    out = {}
    for f in glob.glob(os.path.join(INC, '*.h')):
        try:
            t = open(f, encoding='latin1').read()
        except Exception:
            continue
        for m in rx.finditer(t):
            v = [int(x, 16) for x in m.groups()[1:]]
            b = struct.pack('<IHH8B', v[0], v[1], v[2], *v[3:])
            out.setdefault(b, []).append((m.group(1), os.path.basename(f)))
    return out


def find():
    o = S.load()
    raw = open(S.IMAGE, 'rb').read()
    g = all_guids()
    secs = o['secs']
    res = []
    for b, nm in g.items():
        i = raw.find(b)
        while i >= 0:
            for name, va, vs, po, ps in secs:
                if po <= i < po + ps:
                    res.append((va + i - po, nm[0][0], nm[0][1]))
            i = raw.find(b, i + 1)
    return sorted(res)


if __name__ == '__main__':
    o = S.load()
    users = {}
    for f, r in o['refs'].items():
        for t in r:
            users.setdefault(t, set()).add(f)
    for a, n, h in find():
        print('%08x %-40s %-12s %s' % (a, n, h, ' '.join('%08x' % x for x in sorted(users.get(a, [])))))
