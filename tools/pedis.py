r"""Disassemble functions of the selected module's image (lithtech.exe or d3d.ren).

  python tools/pedis.py [--module d3dren] <hexVA>[:len] ...      # len defaults to the function extent in symbols.csv
  python tools/pedis.py [--module d3dren] -n <name-substring>     # find functions by (Ghidra/our) name

Call and jump targets are annotated with the symbols.csv / renames.csv names; data references to the image print the
name of the data symbol when there is one.  Read-only, no side effects.
"""
import bisect
import csv
import os
import sys

import capstone
import pefile

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
import modcfg  # noqa: E402


def load():
    pe = pefile.PE(modcfg.IMAGE, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    funcs, names = {}, {}
    for r in csv.DictReader(open(modcfg.SYMBOLS_CSV, encoding='utf-8')):
        a = int(r['addr'], 16)
        if r['kind'] == 'func':
            funcs[a] = (int(r['end'], 16), r['name'])
        if r['kind'] in ('func', 'data', 'import', 'label'):
            names.setdefault(a, r['name'])
    if os.path.exists(modcfg.RENAMES_CSV):
        for r in csv.DictReader(l for l in open(modcfg.RENAMES_CSV, encoding='utf-8') if not l.startswith('#')):
            a = int(r['addr'], 16)
            names[a] = r['name']
            if a in funcs:
                funcs[a] = (funcs[a][0], r['name'])
    return pe, base, funcs, names


def main():
    args = sys.argv[1:]
    pe, base, funcs, names = load()
    if args and args[0] == '-n':
        for a, (e, n) in sorted(funcs.items()):
            if args[1].lower() in n.lower():
                print('%08x %5d %s' % (a, e - a, n))
        return
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    keys = sorted(names)
    for arg in args:
        va, _, n = arg.partition(':')
        va = int(va, 16)
        n = int(n) if n else (funcs[va][0] - va if va in funcs else 64)
        print('== %08x %s (%d bytes)' % (va, names.get(va, ''), n))
        for i in md.disasm(pe.get_data(va - base, n), va):
            note = ''
            for tok in i.op_str.replace('[', ' ').replace(']', ' ').replace(',', ' ').split():
                if tok.startswith('0x') and len(tok) >= 8:
                    t = int(tok, 16)
                    if t in names:
                        note = ' ; ' + names[t]
                    else:
                        k = bisect.bisect_right(keys, t) - 1
                        if k >= 0 and t - keys[k] < 0x40 and t >= base:
                            note = ' ; %s+0x%x' % (names[keys[k]], t - keys[k])
            print('  %08x %-22s %s %s%s' % (i.address, i.bytes.hex(), i.mnemonic, i.op_str, note))


if __name__ == '__main__':
    main()
