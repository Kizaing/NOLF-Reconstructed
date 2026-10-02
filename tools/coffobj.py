"""Minimal COFF (i386 .obj) reader for VC6 objects: sections, symbols, relocations, function extents."""
import ctypes
import struct

REL_DIR32, REL_DIR32NB, REL_SECTION, REL_SECREL, REL_REL32 = 0x06, 0x07, 0x0A, 0x0B, 0x14
REL_SIZES = {REL_DIR32: 4, REL_DIR32NB: 4, REL_REL32: 4, REL_SECREL: 4, REL_SECTION: 2}
SCN_LNK_COMDAT = 0x1000


class Section:
    def __init__(self, index, name, data, relocs, flags):
        self.index, self.name, self.data, self.relocs, self.flags = index, name, data, relocs, flags
        self.syms = []      # symbols defined in this section, sorted by value


class Symbol:
    def __init__(self, index, name, value, secno, typ, cls, naux):
        self.index, self.name, self.value, self.secno, self.typ, self.cls, self.naux = index, name, value, secno, typ, cls, naux

    @property
    def is_function(self):
        return self.secno > 0 and self.typ == 0x20

    @property
    def is_section_symbol(self):
        return self.cls == 3 and self.naux > 0 and self.value == 0


class CoffObj:
    def __init__(self, path):
        self.path = path
        d = open(path, 'rb').read()
        machine, nsec, _, symoff, nsym, opthdr, _ = struct.unpack_from('<HHIIIHH', d, 0)
        if machine != 0x14c:
            raise ValueError('%s: not an i386 COFF object' % path)
        strtab = symoff + nsym * 18
        self.sections = []
        for i in range(nsec):
            o = 20 + opthdr + i * 40
            name = d[o:o + 8].rstrip(b'\0').decode('latin1')
            size, raw, rel, _, nrel, _, flags = struct.unpack_from('<IIIIHHI', d, o + 16)
            relocs = [struct.unpack_from('<IIH', d, rel + j * 10) for j in range(nrel)]  # (offset, symindex, type)
            self.sections.append(Section(i + 1, name, d[raw:raw + size] if raw else bytes(size), relocs, flags))
        self.symbols = {}   # symbol table index -> Symbol (aux entries skipped)
        i = 0
        while i < nsym:
            o = symoff + i * 18
            nm = d[o:o + 8]
            if nm[:4] == b'\0\0\0\0':
                so = strtab + struct.unpack_from('<I', nm, 4)[0]
                nm = d[so:d.index(b'\0', so)]
            else:
                nm = nm.rstrip(b'\0')
            val, secno, typ, cls, naux = struct.unpack_from('<IhHBB', d, o + 8)
            s = Symbol(i, nm.decode('latin1'), val, secno, typ, cls, naux)
            self.symbols[i] = s
            if 0 < secno <= nsec and not s.is_section_symbol:
                self.sections[secno - 1].syms.append(s)
            i += 1 + naux
        for sec in self.sections:
            sec.syms.sort(key=lambda x: x.value)

    def functions(self):
        return [s for s in self.symbols.values() if s.is_function]

    def section_of(self, sym):
        return self.sections[sym.secno - 1]

    def extent(self, sym):
        """(section, start, end): a function runs to the next function symbol in its section or the section end."""
        sec = self.section_of(sym)
        later = [s.value for s in sec.syms if s.value > sym.value and s.typ == 0x20]
        return sec, sym.value, (min(later) if later else len(sec.data))

    def relocs_in(self, sec, start, end):
        """Relocations inside [start, end) as (offset-from-start, Symbol, type, addend-in-field)."""
        out = []
        for off, si, typ in sec.relocs:
            if start <= off < end:
                n = REL_SIZES.get(typ, 4)
                addend = int.from_bytes(sec.data[off:off + n], 'little', signed=(typ == REL_REL32))
                out.append((off - start, self.symbols[si], typ, addend))
        return out


_undname_buf = ctypes.create_string_buffer(2048)


def undecorate(name, flags=0x1000):
    """MSVC undecorated name (default UNDNAME_NAME_ONLY), or the name itself for C symbols."""
    if not name.startswith('?'):
        return name[1:] if name.startswith('_') else name
    n = ctypes.windll.dbghelp.UnDecorateSymbolName(name.encode('latin1'), _undname_buf, len(_undname_buf), flags)
    return _undname_buf.value.decode('latin1') if n else name
