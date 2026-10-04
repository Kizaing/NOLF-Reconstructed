"""Stale pointers in a mixed relink: dwords that still hold the ORIGINAL address of a function the relink moved.

  python tools/relink_stale.py [build/relink-run]

Reads the relink's lithtech.map (function names carry their original address as '@<va>' or are known from
build/namemap.json) and lists every place in the new exe (code and data) that holds an old address of a moved
function, with what the original binary has there. Any hit means a reference the relink did not relocate.
"""
import json, os, re, struct, sys

import pefile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def main(argv):
    d = argv[0] if argv else os.path.join(ROOT, 'build', 'relink-run')
    exe = os.path.join(d, 'lithtech.exe')
    byname = {v: int(k, 16) for k, v in json.load(open(os.path.join(ROOT, 'build', 'namemap.json'))).items()}
    moved = {}                          # old va -> (new va, name)
    for l in open(os.path.join(d, 'lithtech.map'), encoding='latin1'):
        m = re.match(r'\s*0001:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{8})\s+f?\s', l)
        if not m:
            continue
        nm, new = m.group(1), int(m.group(2), 16)
        at = re.search(r'@([0-9a-f]{8})$', nm)
        old = int(at.group(1), 16) if at else byname.get(nm) or byname.get(nm.lstrip('_'))
        if old is not None and old != new:
            moved[old] = (new, nm)
    print('%d functions moved' % len(moved))
    pe = pefile.PE(exe)
    img = pe.get_memory_mapped_image()
    orig = pefile.PE(r'E:\AVP2Source\bin\lithtech.exe').get_memory_mapped_image()
    # moved code: a location inside a moved function corresponds to old + (k - new) in the original
    import bisect
    starts = sorted((new, old) for old, (new, nm) in moved.items())
    news = [s[0] for s in starts]

    def orig_at(k):
        va = 0x400000 + k
        j = bisect.bisect_right(news, va) - 1
        if j >= 0 and va - starts[j][0] < 0x4000:
            new, old = starts[j]
            nxt = news[j + 1] if j + 1 < len(news) else None
            if nxt is None or va < nxt:
                return old - 0x400000 + (va - new)
        return k

    hits = 0
    for s in pe.sections:
        name = s.Name.rstrip(b'\0').decode()
        lo, n = s.VirtualAddress, min(s.Misc_VirtualSize, s.SizeOfRawData)
        for k in range(lo, lo + n - 3):
            v = struct.unpack_from('<I', img, k)[0]
            if v in moved:
                new, nm = moved[v]
                ok = orig_at(k) if name == '.text' else k
                o = struct.unpack_from('<I', orig, ok)[0] if ok + 4 <= len(orig) else None
                if o != v:          # the relink changed it: a relocated pointer that equals another function's old address
                    continue
                print('  %-6s %08x holds %08x (%s, now at %08x); original there: %s' % (
                    name, 0x400000 + k, v, nm[:60], new, '%08x' % o if o is not None else '-'))
                hits += 1
    print('%d stale references' % hits)
    return 1 if hits else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
