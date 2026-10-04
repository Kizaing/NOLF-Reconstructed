r"""Check cross-binary claims of rows (read-only): rows whose evidence cites an lithtech.exe address (0x004xxxxx) are
compared with the d3d.ren function: size ratio and mnemonic-sequence similarity (difflib ratio).

  python tools/d3dren_names_verify.py [csv]      # prints one line per checked row; summary at the end
Imported by d3dren_names_build.py (verify_rows) to downgrade high -> medium when the similarity is poor.
"""
import csv
import difflib
import os
import re
import sys

import capstone
import pefile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import d3dren_names_scan as S  # noqa: E402
import d3dren_names_xmatch as X  # noqa: E402

_cache = {}


def mnems(img, funcs, va):
    key = (img, va)
    if key in _cache:
        return _cache[key]
    pe = _cache.get(('pe', img))
    if pe is None:
        pe = pefile.PE(img, fast_load=True)
        _cache[('pe', img)] = pe
    base = pe.OPTIONAL_HEADER.ImageBase
    e = funcs[va][0]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    m = [i.mnemonic for i in md.disasm(pe.get_data(va - base, e - va), va) if i.mnemonic != 'nop']
    _cache[key] = (m, e - va)
    return _cache[key]


def check(rows):
    ren_funcs = X.load_funcs(os.path.join(S.ROOT, 'config', 'd3dren', 'symbols.csv'))
    exe_funcs = X.load_funcs(os.path.join(S.ROOT, 'config', 'symbols.csv'))
    out = {}
    for r in rows:
        if r['kind'] != 'func':
            continue
        a = int(r['address'], 16)
        if a not in ren_funcs:
            continue
        cited = [int(x, 16) for x in re.findall(r'0x0?0?(4[0-9a-f]{5})\b', r['evidence'])]
        cited = [c for c in cited if c in exe_funcs]
        if not cited:
            continue
        m1, s1 = mnems(S.IMAGE, ren_funcs, a)
        best = (-1.0, None, 0)
        for c in cited:
            m2, s2 = mnems(r'E:\AVP2Source\bin\lithtech.exe', exe_funcs, c)
            ratio = difflib.SequenceMatcher(None, m1, m2).ratio()
            if ratio > best[0]:
                best = (ratio, c, s2)
        out[r['address']] = (best[0], best[1], s1, best[2])
    return out


if __name__ == '__main__':
    path = sys.argv[1] if len(sys.argv) > 1 else os.path.join(S.ROOT, 'config', 'd3dren', 'names_proposal.csv')
    rows = list(csv.DictReader(open(path, encoding='utf-8')))
    res = check(rows)
    byaddr = {r['address']: r for r in rows}
    bad = 0
    for a, (ratio, c, s1, s2) in sorted(res.items()):
        r = byaddr[a]
        flag = ''
        if ratio < 0.75 and r['confidence'] == 'high':
            flag = '  <-- weak'
            bad += 1
        print('%s %-44s %-6s ratio=%.2f sizes %d/%d eng=%08x%s' % (a, r['name'][:44], r['confidence'], ratio, s1, s2, c, flag))
    print('checked', len(res), 'weak high', bad)
