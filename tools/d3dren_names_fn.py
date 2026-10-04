r"""Print Ghidra C for functions from out/decomp/d3d_ren_v2.c (read-only).

  python tools/d3dren_names_fn.py <hexaddr> [<hexaddr> ...]       # C of those functions
  python tools/d3dren_names_fn.py -g <regex>                      # which functions' C match the regex (addr list)
Also shows callers/callees and string refs from the scan cache.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import d3dren_names_scan as S  # noqa: E402

DUMP = r'E:\AVP2Source\out\decomp\d3d_ren_v2.c'


def sections():
    txt = open(DUMP, encoding='latin1').read()
    parts = re.split(r'(?m)^// ==== ([0-9a-f]{8}) (\S+)\s*$', txt)
    out = {}
    for i in range(1, len(parts), 3):
        out[int(parts[i], 16)] = (parts[i + 1], parts[i + 2])
    return out


def main():
    args = sys.argv[1:]
    sec = sections()
    o = S.load()
    if args and args[0] == '-g':
        rx = re.compile(args[1])
        for a in sorted(sec):
            if rx.search(sec[a][1]):
                print('%08x %s' % (a, sec[a][0]))
        return
    for x in args:
        a = int(x, 16)
        if a not in sec:
            print('no section for %08x' % a)
            continue
        print('// ==== %08x %s  callers=%s' % (
            a, sec[a][0], ' '.join('%08x' % c for c in sorted(o['callers'].get(a, ())))))
        print(sec[a][1].rstrip())
        print()


if __name__ == '__main__':
    main()
