r"""Print the facts of a PE image: sections, exports, imports, base relocations, Rich header (decoded).

  python tools/pefacts.py <image> [--md]      # --md: markdown tables (used for config/d3dren/FACTS.md)

The Rich header lists (product id, build, object count) per toolchain component that contributed objects to
the link.  Product ids below are the community-known comp.id table for VC6-era tools; "Utc" is the compiler
back end (C2.DLL), so its build number is the C2.DLL version that compiled the objects.
"""
import collections
import datetime
import struct
import sys

import pefile

PRODIDS = {
    0: 'Unknown', 1: 'Import0 (import thunks)', 2: 'Linker 5.10', 3: 'Cvtomf 5.10', 4: 'Linker 6.00',
    5: 'Cvtomf 6.00', 6: 'Cvtres 5.00', 7: 'Utc11 Basic', 8: 'Utc11 C', 9: 'Utc12 Basic',
    10: 'Utc12 C (VC6 C compiler back end)', 11: 'Utc12 C++ (VC6 C++ compiler back end)', 12: 'AliasObj 6.0',
    13: 'VisualBasic 6.0', 14: 'Masm 6.13', 15: 'Masm 7.10', 16: 'Linker 5.11', 17: 'Cvtomf 5.11',
    18: 'Masm 6.14', 19: 'Linker 5.12 (?)', 20: 'Cvtomf 5.12', 21: 'Utc12 C Std', 22: 'Utc12 C++ Std',
    23: 'Utc12 C Book', 24: 'Utc12 C++ Book', 25: 'Implib 7.00', 26: 'Cvtomf 7.00', 27: 'Utc13 Basic',
    28: 'Utc13 C (VS.NET)', 29: 'Utc13 C++ (VS.NET)', 30: 'Linker 6.10', 31: 'Cvtomf 6.10',
    32: 'Linker 6.01', 33: 'Cvtomf 6.01', 37: 'Implib (?)', 49: 'Utc12.2 (Processor Pack back end)',
}


def rich(pe):
    r = pe.parse_rich_header()
    if not r:
        return []
    v = r['values']
    out = collections.Counter()
    for i in range(0, len(v), 2):
        out[(v[i] >> 16, v[i] & 0xffff)] += v[i + 1]
    return sorted(out.items())


def main():
    path = sys.argv[1]
    md = '--md' in sys.argv
    pe = pefile.PE(path)
    oh, fh = pe.OPTIONAL_HEADER, pe.FILE_HEADER
    data = open(path, 'rb').read()
    out = []
    P = out.append
    P('file: %s (%d bytes)' % (path, len(data)))
    P('machine 0x%x, characteristics 0x%x (%s)' % (fh.Machine, fh.Characteristics,
                                                    'DLL' if fh.Characteristics & 0x2000 else 'EXE'))
    P('timestamp %s (0x%08x)' % (datetime.datetime.fromtimestamp(fh.TimeDateStamp, datetime.timezone.utc), fh.TimeDateStamp))
    P('linker %d.%d, image base 0x%08x, entry point RVA 0x%x (VA 0x%08x)' % (
        oh.MajorLinkerVersion, oh.MinorLinkerVersion, oh.ImageBase, oh.AddressOfEntryPoint,
        oh.ImageBase + oh.AddressOfEntryPoint))
    P('section alignment 0x%x, file alignment 0x%x, size of image 0x%x, subsystem %d, os %d.%d, subsys %d.%d' % (
        oh.SectionAlignment, oh.FileAlignment, oh.SizeOfImage, oh.Subsystem, oh.MajorOperatingSystemVersion,
        oh.MinorOperatingSystemVersion, oh.MajorSubsystemVersion, oh.MinorSubsystemVersion))
    P('stack reserve/commit 0x%x/0x%x, heap reserve/commit 0x%x/0x%x, checksum 0x%x' % (
        oh.SizeOfStackReserve, oh.SizeOfStackCommit, oh.SizeOfHeapReserve, oh.SizeOfHeapCommit, oh.CheckSum))
    P('')
    P('sections:')
    P('  %-8s %10s %10s %10s %10s  %s' % ('name', 'VA', 'vsize', 'raw off', 'raw size', 'flags'))
    for s in pe.sections:
        ch = s.Characteristics
        fl = ''.join(c if ch & b else '-' for c, b in (('R', 0x40000000), ('W', 0x80000000), ('X', 0x20000000),
                                                       ('C', 0x20), ('I', 0x40), ('U', 0x80)))
        P('  %-8s %10x %10x %10x %10x  %s' % (s.Name.rstrip(b'\0').decode('latin1'), oh.ImageBase + s.VirtualAddress,
                                              s.Misc_VirtualSize, s.PointerToRawData, s.SizeOfRawData, fl))
    P('')
    P('data directories:')
    for d in oh.DATA_DIRECTORY:
        if d.VirtualAddress or d.Size:
            P('  %-24s RVA 0x%x size 0x%x' % (d.name, d.VirtualAddress, d.Size))
    P('')
    if hasattr(pe, 'DIRECTORY_ENTRY_EXPORT'):
        e = pe.DIRECTORY_ENTRY_EXPORT
        P('exports (dll name %s):' % (e.name.decode() if e.name else '?'))
        for s in e.symbols:
            P('  ordinal %d  %s  RVA 0x%x (VA 0x%08x)' % (s.ordinal, s.name.decode() if s.name else '-', s.address,
                                                            oh.ImageBase + s.address))
        P('')
    if hasattr(pe, 'DIRECTORY_ENTRY_IMPORT'):
        n = 0
        P('imports:')
        for d in pe.DIRECTORY_ENTRY_IMPORT:
            P('  %s (%d)' % (d.dll.decode(), len(d.imports)))
            names = [(i.name.decode() if i.name else 'ord%d' % i.ordinal) for i in d.imports]
            n += len(names)
            P('    ' + ', '.join(names))
        P('  total %d imported functions' % n)
        P('')
    pe.parse_data_directories([pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_BASERELOC']])
    if hasattr(pe, 'DIRECTORY_ENTRY_BASERELOC'):
        cnt = collections.Counter()
        for blk in pe.DIRECTORY_ENTRY_BASERELOC:
            for r in blk.entries:
                cnt[r.type] += 1
        P('base relocations: %s (HIGHLOW=3, ABSOLUTE=0 padding)' % dict(cnt))
        P('')
    rows = rich(pe)
    P('Rich header (checksum key present: %s):' % bool(pe.RICH_HEADER))
    if pe.RICH_HEADER:
        P('  %-40s %7s %6s' % ('component', 'build', 'objs'))
        for (pid, build), cnt in rows:
            P('  %-40s %7d %6d' % ('%d %s' % (pid, PRODIDS.get(pid, '?')), build, cnt))
        P('  total objects (excluding Import0): %d' % sum(c for (p, b), c in rows if p != 1))
    print('\n'.join(('    ' + l if md and l else l) if False else l for l in out))


if __name__ == '__main__':
    main()
