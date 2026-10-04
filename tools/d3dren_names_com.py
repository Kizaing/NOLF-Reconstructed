r"""COM interface vtable layouts from the DX8 SDK headers (ddraw.h, d3d.h, ...), and a scan that matches
global interface pointers in d3d.ren to interfaces by the vtable slots called through them (read-only).

  python tools/d3dren_names_com.py                 # global pointer -> candidate interfaces
  python tools/d3dren_names_com.py IDirectDraw7    # slot table of an interface
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

INC = r'E:\AVP2Source\directx8-msdx8\include'
FILES = ['ddraw.h', 'd3d.h']


def parse_interfaces():
    ifs = {}
    for f in FILES:
        t = open(os.path.join(INC, f), encoding='latin1').read()
        # strip comments
        t = re.sub(r'/\*.*?\*/', '', t, flags=re.S)
        t = re.sub(r'//[^\n]*', '', t)
        for m in re.finditer(r'DECLARE_INTERFACE_\(\s*(\w+)\s*,\s*(\w+)\s*\)\s*\{(.*?)\n\};', t, flags=re.S):
            name, base, body = m.group(1), m.group(2), m.group(3)
            meths = [a or b for a, b in re.findall(
                r'STDMETHOD_\(\s*[\w ]+\s*,\s*(\w+)\s*\)|STDMETHOD\(\s*(\w+)\s*\)', body)]
            ifs[name] = (base, meths)
    return ifs


def layout(ifs, name):
    if name == 'IUnknown':
        return ['QueryInterface', 'AddRef', 'Release']
    base, meths = ifs[name]
    return meths      # the SDK bodies list the IUnknown methods explicitly


def all_layouts():
    ifs = parse_interfaces()
    return {n: layout(ifs, n) for n in ifs}


if __name__ == '__main__':
    L = all_layouts()
    if len(sys.argv) > 1:
        for i, m in enumerate(L[sys.argv[1]]):
            print('0x%02x %s' % (i * 4, m))
        sys.exit()
    for n in ('IDirectDraw7', 'IDirectDrawSurface7', 'IDirect3D7', 'IDirect3DDevice7', 'IDirectDrawClipper',
              'IDirectDrawPalette', 'IDirectDrawGammaControl', 'IDirect3DVertexBuffer7', 'IDirectDraw4'):
        print(n, len(L.get(n, [])))
