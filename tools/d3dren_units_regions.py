r"""d3d.ren unit recovery, step 2: hard object boundaries from section alignment and padding.

Evidence (see config/d3dren/units_proposal.md, "Method"):

  * The renderer's objects are of two kinds.  Objects compiled with /Gy put every function in its own COMDAT
    (16-byte aligned, linker pads with 0x90): every function starts AND ends 16-aligned ("A" functions).  Objects
    compiled without /Gy have one packed .text section: functions follow each other without padding ("P"
    functions), the section starts 16-aligned and the linker pads its end with 0xCC up to the next 16 boundary.
  * Therefore a switch between an A run and a P region is an object boundary, a 0xCC-padded function end is an
    object boundary, and inside a P region a boundary can only be at a 16-aligned function start (none observed:
    a P region is one object).  Inside an A run the boundaries are invisible to padding evidence (an A run is
    one or more /Gy objects).
  * Import-library thunks (`jmp [iat]`, 6 bytes, DDRAW) sit between objects and are not renderer source.

Library ranges (config/d3dren/libraries.json) are excluded; the StdLith objects found at the end of the renderer
region (struct_bank, l_allocator, dynarray: byte-identical to libs_objs/LT_StdLith, see units_proposal.md) are
added as "prebuilt library" units by tools/d3dren_units_recover.py.

This module only computes the regions; it is imported by d3dren_units_recover.py and can be run standalone to list
them:   python tools/d3dren_units_regions.py
"""
import bisect
import json
import os
import sys

import pefile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCAN = os.path.join(REPO, 'build', 'd3dren', 'units', 'scan.json')
IMAGE = r'E:\AVP2Source\bin\talon\d3d.ren'

REN_LO = 0x10001000            # first renderer function
REN_HI = 0x1003b710            # first CRT library object (lib/VC6_LIBCMT_1998/delete)


def load_scan():
    return json.load(open(SCAN))


def text_bytes():
    pe = pefile.PE(IMAGE, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    t = [s for s in pe.sections if s.Name.startswith(b'.text')][0]
    return t.get_data(), base + t.VirtualAddress


def renderer_functions(scan):
    libs = [tuple(x) for x in scan['meta']['libs']]
    starts = [l[0] for l in libs]

    def inlib(a):
        i = bisect.bisect_right(starts, a) - 1
        return i >= 0 and libs[i][0] <= a < libs[i][1]
    return [f for f in scan['funcs'] if REN_LO <= f['addr'] < REN_HI and not inlib(f['addr'])]


def pad_info(f, tb, tlo):
    """(type, count) of the trailing padding of a function extent: 'c' 0xCC, '9' 0x90, 'x' none"""
    b = tb[f['addr'] - tlo:f['end'] - tlo]
    n = 0
    while n < len(b) and b[len(b) - 1 - n] in (0x90, 0xcc):
        n += 1
    if n == 0:
        return 'x', 0
    pb = set(b[len(b) - n:])
    return ('c' if pb == {0xcc} else '9' if pb == {0x90} else 'm'), n


def compute_regions(funcs, tb, tlo, thunks=()):
    """-> list of dict(kind 'A'|'P', lo, hi, first, last (function indices into funcs), nf).
    thunks: [(lo, hi)] ranges that are not functions (import thunks) and terminate a region."""
    N = len(funcs)
    thunk_at = {lo: hi for lo, hi in thunks}
    regs = []
    i = 0
    while i < N:
        f = funcs[i]
        sa, ea = f['addr'] % 16 == 0, f['end'] % 16 == 0
        pt, _ = pad_info(f, tb, tlo)

        def is_A(k):
            g = funcs[k]
            if not (g['addr'] % 16 == 0 and g['end'] % 16 == 0):
                return False
            t, _ = pad_info(g, tb, tlo)
            if t == 'c':
                return False
            if t == 'x':
                # exactly aligned, no pad: A only if the next function is also aligned-end (else it is the first
                # function of a packed object whose size happens to be a multiple of 16)
                if k + 1 < N and funcs[k + 1]['end'] % 16 != 0 and funcs[k + 1]['addr'] == g['end']:
                    return False
            return True
        if is_A(i):
            j = i
            while j + 1 < N and funcs[j + 1]['addr'] == funcs[j]['end'] and is_A(j + 1):
                j += 1
            regs.append(dict(kind='A', first=i, last=j))
            i = j + 1
        else:
            j = i
            while True:
                e = funcs[j]['end']
                pt, _ = pad_info(funcs[j], tb, tlo)
                if e % 16 == 0 and pt in ('c', '9'):
                    break
                if e in thunk_at or j + 1 >= N or funcs[j + 1]['addr'] != e:
                    break
                if e % 16 == 0 and pt == 'x' and funcs[j + 1]['end'] % 16 == 0:
                    break
                j += 1
            regs.append(dict(kind='P', first=i, last=j))
            i = j + 1
    for r in regs:
        r['lo'] = funcs[r['first']]['addr']
        r['hi'] = funcs[r['last']]['end']
        r['nf'] = r['last'] - r['first'] + 1
    return regs


def main():
    scan = load_scan()
    funcs = renderer_functions(scan)
    tb, tlo = text_bytes()
    # the three DDRAW import thunks are labels, not functions: bytes between function extents
    thunks = []
    for a, b in zip(funcs, funcs[1:]):
        if b['addr'] > a['end']:
            thunks.append((a['end'], b['addr']))
    regs = compute_regions(funcs, tb, tlo, thunks)
    print('functions', len(funcs), 'gaps between extents:', ['%x-%x' % t for t in thunks])
    for r in regs:
        print('%s %08x-%08x size=%5x nf=%3d' % (r['kind'], r['lo'], r['hi'], r['hi'] - r['lo'], r['nf']))
    tot = {'A': 0, 'P': 0}
    for r in regs:
        tot[r['kind']] += r['hi'] - r['lo']
    print(tot, len([r for r in regs if r['kind'] == 'A']), len([r for r in regs if r['kind'] == 'P']))


if __name__ == '__main__':
    main()
