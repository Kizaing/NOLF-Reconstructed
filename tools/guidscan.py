r"""Find DirectX SDK GUIDs (DEFINE_GUID in the SDK headers) in an image's data.

  python tools/guidscan.py [--module d3dren] [--sdk DIR] [--csv out.csv]

Parses every DEFINE_GUID(name, l, w1, w2, b1..b8) of the headers in the DX SDK include dir (read-only reference tree),
searches the module image for the 16 bytes, and prints/writes `address,name,header` for each hit: the address is a
data symbol named by the SDK (provenance `dx-sdk`).  The .data hits are what dxguid.lib's per-GUID objects (Rich
header comp ids 19/37) contributed to the link.
"""
import csv
import os
import re
import struct
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
import modcfg  # noqa: E402
import pefile  # noqa: E402

DEF = re.compile(r'DEFINE_GUID\s*\(\s*(\w+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,\s*(0x[0-9a-fA-F]+)\s*,'
                 r'((?:\s*0x[0-9a-fA-F]+\s*,?){8})\s*\)', re.S)


def guids(sdk):
    out = {}
    for f in sorted(os.listdir(sdk)):
        if not f.lower().endswith('.h'):
            continue
        txt = open(os.path.join(sdk, f), encoding='latin1').read()
        for m in DEF.finditer(txt):
            b = [int(x, 16) for x in re.findall(r'0x[0-9a-fA-F]+', m.group(5))]
            raw = struct.pack('<IHH', int(m.group(2), 16), int(m.group(3), 16), int(m.group(4), 16)) + bytes(b)
            out.setdefault(raw, []).append((m.group(1), f))
    return out


def main():
    sdk = modcfg.DX8_INCLUDE
    args = sys.argv[1:]
    if '--sdk' in args:
        sdk = args[args.index('--sdk') + 1]
    pe = pefile.PE(modcfg.IMAGE, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    rows = []
    for raw, names in guids(sdk).items():
        for s in pe.sections:
            data = s.get_data()
            p = data.find(raw)
            while p >= 0:
                rows.append((base + s.VirtualAddress + p, names[0][0], names[0][1],
                             s.Name.rstrip(b'\0').decode(), ','.join(n for n, _ in names[1:])))
                p = data.find(raw, p + 1)
    rows.sort()
    for r in rows:
        print('%08x %-32s %-12s %-6s %s' % r)
    if '--csv' in args:
        with open(args[args.index('--csv') + 1], 'w', newline='') as f:
            w = csv.writer(f)
            w.writerow(['address', 'name', 'header', 'section', 'aliases'])
            for r in rows:
                w.writerow(['%08x' % r[0]] + list(r[1:]))
    print('%d GUID hits in %s' % (len(rows), os.path.basename(modcfg.IMAGE)))


if __name__ == '__main__':
    main()
