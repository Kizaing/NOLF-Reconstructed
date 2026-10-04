r"""Write config/d3dren/names_crt.csv from config/d3dren/libraries.json: the ORIGINAL names of the CRT functions and data
that tools/libmatch.py proved byte-identical (provenance crt-lib, confidence high).

  python tools/d3dren_crt_names.py --module d3dren

Columns match names_proposal.csv: address,name,kind,source_file,evidence,confidence,provenance.  C++ symbols are
written undecorated (`operator delete`), C symbols as the object file spells them (`_strcpy`, `__CIpow`).
"""
import csv
import json
import os
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
import modcfg  # noqa: E402
from coffobj import undecorate  # noqa: E402


def main():
    j = json.load(open(modcfg.LIBRARIES_JSON))
    syms = {int(r['addr'], 16): r for r in csv.DictReader(open(modcfg.SYMBOLS_CSV, encoding='utf-8'))
            if r['kind'] in ('func', 'data', 'label')}
    funcs = {int(r['addr'], 16) for r in csv.DictReader(open(modcfg.SYMBOLS_CSV, encoding='utf-8')) if r['kind'] == 'func'}
    rows = {}
    for u in j['units']:
        src = u['name'].split('/')[-1]
        lib = u['lib']
        for va, name in u['functions'].items():
            rows[int(va, 16)] = (name, 'func', src, lib, u['name'])
        for va, name in u['data'].items():
            va = int(va, 16)
            if va not in rows and not name.startswith(('.', '$')):
                rows[va] = (name, 'data', src, lib, u['name'])
    out = os.path.join(modcfg.CONFIG, 'names_crt.csv')
    n = {'func': 0, 'data': 0}
    with open(out, 'w', newline='') as f:
        w = csv.writer(f)
        w.writerow(['address', 'name', 'kind', 'source_file', 'evidence', 'confidence', 'provenance'])
        for va in sorted(rows):
            name, kind, src, lib, unit = rows[va]
            if kind == 'func' and va not in funcs:
                continue            # an entry inside a larger Ghidra function (asm label): not a function to rename
            nm = undecorate(name).replace(' ', '_') if name.startswith('?') else name   # Ghidra labels cannot contain spaces
            nm = nm.replace('`', '').replace("'", '')                                   # `vftable', `scalar deleting destructor'
            if nm.startswith('_$E'):                                                    # per-object static initialisers: keep them apart
                nm = '%s_%s' % (src, nm[1:])
            w.writerow(['%08x' % va, nm, kind, src, 'byte-identical code of %s from %s (tools/libmatch.py)' % (unit, lib),
                        'high', 'crt-lib'])
            n[kind] += 1
    print('%s: %d functions, %d data symbols' % (out, n['func'], n['data']))


if __name__ == '__main__':
    main()
