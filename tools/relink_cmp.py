r"""Compare build/relink/lithtech.exe with the original: headers, sections, first differing bytes.

  python tools/relink_cmp.py [new.exe] [--max N]
"""
import os
import sys

import pefile

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
ORIG = r'E:\AVP2Source\bin\lithtech.exe'


def sec_table(pe):
    return {s.Name.rstrip(b'\0').decode('latin1'): s for s in pe.sections}


def diff_runs(a, b, maxruns):
    """[(start, length)] of maximal differing runs between two equal-length byte strings (merging gaps < 8)."""
    runs, i, n = [], 0, min(len(a), len(b))
    while i < n and len(runs) < maxruns:
        if a[i] != b[i]:
            j = i
            last = i
            while j < n and j - last < 8:
                if a[j] != b[j]:
                    last = j
                j += 1
            runs.append((i, last - i + 1))
            i = last + 1
        else:
            i += 1
    return runs


def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    maxruns = 10
    if '--max' in sys.argv:
        maxruns = int(sys.argv[sys.argv.index('--max') + 1])
        args = [a for a in args if a != str(maxruns)]
    new = args[0] if args else os.path.join(ROOT, 'build', 'relink', 'lithtech.exe')
    a, b = pefile.PE(ORIG), pefile.PE(new)
    print('orig size %d  new size %d' % (os.path.getsize(ORIG), os.path.getsize(new)))
    for fld in ('AddressOfEntryPoint', 'ImageBase', 'SectionAlignment', 'FileAlignment', 'SizeOfImage', 'SizeOfHeaders',
                'SizeOfCode', 'SizeOfInitializedData', 'SizeOfUninitializedData', 'BaseOfCode', 'BaseOfData',
                'Subsystem', 'MajorOperatingSystemVersion', 'MajorSubsystemVersion', 'SizeOfStackReserve',
                'SizeOfStackCommit', 'SizeOfHeapReserve', 'SizeOfHeapCommit', 'DllCharacteristics', 'CheckSum'):
        va, vb = getattr(a.OPTIONAL_HEADER, fld), getattr(b.OPTIONAL_HEADER, fld)
        print('  %-28s %8x %8x %s' % (fld, va, vb, '' if va == vb else '  <-- differs'))
    print('  characteristics %x %x' % (a.FILE_HEADER.Characteristics, b.FILE_HEADER.Characteristics))
    sa, sb = sec_table(a), sec_table(b)
    print('sections: orig %s | new %s' % (list(sa), list(sb)))
    for n in sa:
        x = sa[n]
        y = sb.get(n)
        if not y:
            print('  %-6s missing in new' % n)
            continue
        print('  %-6s va %x/%x vsize %x/%x raw %x/%x flags %x/%x' % (
            n, x.VirtualAddress, y.VirtualAddress, x.Misc_VirtualSize, y.Misc_VirtualSize,
            x.SizeOfRawData, y.SizeOfRawData, x.Characteristics, y.Characteristics))
    for n in sa:
        if n not in sb:
            continue
        x, y = sa[n], sb[n]
        da, db = x.get_data(), y.get_data()
        n_ = min(len(da), len(db))
        runs = diff_runs(da[:n_], db[:n_], maxruns)
        total = sum(1 for i in range(n_) if da[i] != db[i]) if n_ < 200000 else sum(1 for p, q in zip(da[:n_], db[:n_]) if p != q)
        print('%s: %d of %d bytes differ' % (n, total, n_))
        for off, ln in runs:
            va = a.OPTIONAL_HEADER.ImageBase + x.VirtualAddress + off
            print('   +%06x (va %08x) len %d: orig %s | new %s' % (
                off, va, ln, da[off:off + min(ln, 12)].hex(), db[off:off + min(ln, 12)].hex()))


if __name__ == '__main__':
    main()
