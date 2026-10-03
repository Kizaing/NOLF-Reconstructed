r"""Editable i386 COFF object model (load / modify / save) plus a from-scratch object writer.

Used by tools/relink.py to patch target objects (make duplicate names static, rename import references,
add alias symbols) and to write the data stand-in object. Symbol indices are preserved on a load/save
round trip; new symbols are appended.
"""
import struct

CLS_EXTERNAL, CLS_STATIC, CLS_LABEL = 2, 3, 6
SCN_CNT_CODE, SCN_CNT_INIT, SCN_CNT_UNINIT, SCN_LNK_COMDAT = 0x20, 0x40, 0x80, 0x1000
SCN_ALIGN = {1: 0x100000, 2: 0x200000, 4: 0x300000, 8: 0x400000, 16: 0x500000, 32: 0x600000, 64: 0x700000}
SCN_MEM_EXECUTE, SCN_MEM_READ, SCN_MEM_WRITE = 0x20000000, 0x40000000, 0x80000000


class Sec:
    def __init__(self, name, data, relocs, flags, nlines=0):
        self.name, self.data, self.relocs, self.flags, self.nlines = name, bytes(data), list(relocs), flags, nlines
        self.size = len(self.data)       # for sections without raw data (bss) set explicitly


class Sym:
    def __init__(self, name, value, sec, typ, cls, aux=b''):
        self.name, self.value, self.sec, self.typ, self.cls, self.aux = name, value, sec, typ, cls, aux

    @property
    def naux(self):
        return len(self.aux) // 18


class Coff:
    def __init__(self):
        self.sections = []      # [Sec]
        self.syms = []          # [Sym or None]: None marks an aux slot (index kept)
        self.timestamp = 0

    # ---- load
    @classmethod
    def load(cls, path):
        d = open(path, 'rb').read()
        c = cls()
        machine, nsec, c.timestamp, symoff, nsym, opthdr, _ = struct.unpack_from('<HHIIIHH', d, 0)
        assert machine == 0x14c, path
        strtab = symoff + nsym * 18
        for i in range(nsec):
            o = 20 + opthdr + i * 40
            nm = d[o:o + 8].rstrip(b'\0').decode('latin1')
            if nm.startswith('/'):
                so = strtab + int(nm[1:])
                nm = d[so:d.index(b'\0', so)].decode('latin1')
            size, raw, rel, _, nrel, nlines, flags = struct.unpack_from('<IIIIHHI', d, o + 16)
            relocs = [list(struct.unpack_from('<IIH', d, rel + j * 10)) for j in range(nrel)]
            s = Sec(nm, d[raw:raw + size] if raw else b'', relocs, flags, nlines)
            s.size = size
            c.sections.append(s)
        i = 0
        while i < nsym:
            o = symoff + i * 18
            nm = d[o:o + 8]
            if nm[:4] == b'\0\0\0\0':
                so = strtab + struct.unpack_from('<I', nm, 4)[0]
                nm = d[so:d.index(b'\0', so)]
            else:
                nm = nm.rstrip(b'\0')
            val, secno, typ, cls_, naux = struct.unpack_from('<IhHBB', d, o + 8)
            c.syms.append(Sym(nm.decode('latin1'), val, secno, typ, cls_, d[o + 18:o + 18 + 18 * naux]))
            c.syms.extend([None] * naux)
            i += 1 + naux
        return c

    # ---- edit
    def add_symbol(self, name, value, secno, typ=0, cls=CLS_EXTERNAL):
        self.syms.append(Sym(name, value, secno, typ, cls))
        return len(self.syms) - 1

    def find(self, name):
        for i, s in enumerate(self.syms):
            if s is not None and s.name == name:
                return i
        return None

    # ---- save
    def save(self, path):
        strtab = bytearray()

        def namefield(nm):
            b = nm.encode('latin1')
            if len(b) <= 8:
                return b.ljust(8, b'\0')
            off = 4 + len(strtab)
            strtab.extend(b + b'\0')
            return struct.pack('<II', 0, off)

        nsec = len(self.sections)
        off = 20 + 40 * nsec
        layout = []
        for s in self.sections:
            raw = off if s.flags & SCN_CNT_UNINIT == 0 and s.data else 0
            if raw:
                off += len(s.data)
            rel = off if s.relocs else 0
            off += 10 * len(s.relocs)
            layout.append((raw, rel))
        symoff = off
        out = bytearray(struct.pack('<HHIIIHH', 0x14c, nsec, self.timestamp, symoff, len(self.syms), 0, 0))
        for s, (raw, rel) in zip(self.sections, layout):
            out += namefield(s.name) + struct.pack('<IIIIIIHHI', 0, 0, s.size, raw, rel, 0, len(s.relocs), s.nlines, s.flags)
        for s in self.sections:
            if s.flags & SCN_CNT_UNINIT == 0 and s.data:
                out += s.data
            for o, si, t in s.relocs:
                out += struct.pack('<IIH', o, si, t)
        assert len(out) == symoff
        for s in self.syms:
            if s is None:
                continue
            out += namefield(s.name) + struct.pack('<IhHBB', s.value, s.sec, s.typ, s.cls, s.naux) + s.aux
        out += struct.pack('<I', 4 + len(strtab)) + strtab
        with open(path, 'wb') as f:
            f.write(out)


def section_aux(length, nrel, selection=0, checksum=0, number=0):
    return struct.pack('<IHHIHB3x', length, nrel, 0, checksum, number, selection)
