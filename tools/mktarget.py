r"""Split per-function "target" COFF objects out of lithtech.exe for objdiff.

Each requested function becomes one COMDAT-style .text section (like VC6 /Gy output) holding the
exact bytes of its extent [entry, next entry), with relocations recovered by disassembly:

  * call/jmp/jcc rel32 leaving the function (or `call` to its own entry) -> REL32 to the exact
    target (callees are always entries; unknown ones get a synthetic fn_XXXXXXXX name)
  * 32-bit displacement / immediate whose value lies inside a mapped image section -> DIR32 to the
    containing symbol, addend (value - symbol) left in the field, as COFF does
  * switch jump tables (ptr) -> DIR32 per entry to a local "$L<va>" label (VC6 uses $L<n> labels;
    objdiff treats $L* symbols as labels and compares in-function relocations by address)
  * byte index tables / other in-function data: copied verbatim, no relocations

Relocated fields hold the addend, everything else is the original bytes; verify() re-applies the
relocations against the original addresses and checks the bytes round-trip to the exe.

API:
  st = SymTab.load(csv_or_tsv_path, exe)       # symbols.csv (or the avp2_now.tsv stand-in)
  info = write_target_obj(out_path, func_vas, exe_path, st, namemap, body_ends={va: end_va})
  verify(out_path, exe_path, info['name2va'])  -> list of mismatches ([] == OK)

CLI:
  python mktarget.py --exe bin\lithtech.exe --symbols decomp\config\symbols.csv
         [--namemap map.json] --out t.obj [--range 41ff50-420080] [--verify] [hexVA ...]
  python mktarget.py --exe ... --symbols ... --all <outdir> [--parts 24]   # whole exe + verify
  python mktarget.py --learn out\match\q_O2.obj --out q_target.obj [--save-namemap q.json]
         quat_Slerp=44cf90 quat_GetVectors=44cf30 ...
      (--learn: take function names from the base obj and name every relocation target after the
       base relocation at the same offset - callees, __real@ constants, ??_C@ strings, statics)
namemap json: {"0x41ff50": "?cc_AddCommand@@...", "41ffa0": "...", ...} (hex keys, 0x optional)
--symbols defaults to decomp\config\symbols.csv (falls back to out\stage4\dump\avp2_now.tsv).

objdiff: objdiff-cli.exe diff -1 <target.obj> -2 <base.obj> -o out.json [--format json-pretty]
         (-c functionRelocDiffs=none ignores relocation targets entirely)
"""
import argparse
import bisect
import csv
import json
import os
import struct
import sys
import time

import capstone
import pefile

IMAGE_REL_I386_DIR32 = 0x06
IMAGE_REL_I386_REL32 = 0x14
TEXT_CHARS = 0x60501020   # CNT_CODE | LNK_COMDAT | ALIGN_16BYTES | MEM_EXECUTE | MEM_READ
CLS_EXTERNAL, CLS_STATIC, CLS_LABEL = 2, 3, 6
X86_OP_IMM, X86_OP_MEM = 2, 3


# ----------------------------------------------------------------------------------------------
# Image + symbol table
# ----------------------------------------------------------------------------------------------
class Image:
    def __init__(self, exe_path):
        pe = pefile.PE(exe_path, fast_load=True)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.sections = []   # (lo, hi, name, data)
        for s in pe.sections:
            lo = self.base + s.VirtualAddress
            size = max(s.Misc_VirtualSize, s.SizeOfRawData)
            data = s.get_data()[:size].ljust(size, b'\0')
            self.sections.append((lo, lo + size, s.Name.rstrip(b'\0').decode('latin1'), data))
        self.sections.sort()
        text = [s for s in self.sections if s[2] == '.text'][0]
        self.text_lo, self.text_hi = text[0], text[1]
        self.pe = pe
        # A DLL carries its base relocation table: the exact VAs of every 32-bit absolute address in the image.
        # recover() then knows which displacement/immediate fields really are addresses (an EXE has no table: None).
        self.reloc_vas = None
        d = pe.OPTIONAL_HEADER.DATA_DIRECTORY[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_BASERELOC']]
        if d.VirtualAddress and d.Size:
            pe.parse_data_directories([pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_BASERELOC']])
            self.reloc_vas = {self.base + r.rva for blk in getattr(pe, 'DIRECTORY_ENTRY_BASERELOC', [])
                              for r in blk.entries if r.type == 3}

    def in_image(self, va):
        for lo, hi, _, _ in self.sections:
            if lo <= va < hi:
                return True
        return False

    def read(self, va, n):
        for lo, hi, _, data in self.sections:
            if lo <= va and va + n <= hi:
                return data[va - lo:va - lo + n]
        raise ValueError('read %08x+%x outside sections' % (va, n))


def load_splits(path):
    """config/splits.csv: {addr: name} where a Ghidra function extent must be split (Ghidra merged
    two functions, e.g. a non-contiguous body swallowing its neighbour)."""
    out = {}
    if os.path.exists(path):
        for r in csv.DictReader(l for l in open(path, encoding='utf-8') if not l.startswith('#')):
            out[int(r['addr'], 16)] = r['name']
    return out


def apply_splits(funcs, path):
    """funcs: sorted [(addr, end, name)] -> same, with each split point starting a new function."""
    splits = load_splits(path)
    if not splits:
        return funcs
    out = []
    for a, e, n in funcs:
        cuts = sorted(s for s in splits if a < s < e)
        for s in cuts:
            out.append((a, s, n))
            a, n = s, splits[s]
        out.append((a, e, n))
    return out


class SymTab:
    """funcs: sorted [(addr, end, name)], tables: {addr: (end, 'ptr'|'byte')},
    imports: {slot: 'DLL!name'}, data: sorted [(addr, end, name)] (data + labels outside .text)."""

    def __init__(self):
        self.funcs, self.tables, self.imports, self.data = [], {}, {}, []
        self.source = None

    @classmethod
    def load(cls, path, img):
        st = cls()
        st.source = path
        if path.lower().endswith('.csv'):
            with open(path, newline='') as f:
                for r in csv.DictReader(f):
                    a, e, k = int(r['addr'], 16), int(r['end'], 16), r['kind']
                    if k == 'func':
                        st.funcs.append((a, e, r['name']))
                    elif k == 'jumptable':
                        st.tables[a] = (e, r['extra'] or 'ptr')
                    elif k == 'import':
                        st.imports[a] = r['name']
                    elif k in ('data', 'label'):
                        if not (img.text_lo <= a < img.text_hi):
                            st.data.append((a, e, r['name']))
        else:   # stand-in: Ghidra dump TSV addr name size source thunk; extent = next entry
            rows = []
            with open(path) as f:
                next(f)
                for line in f:
                    p = line.rstrip('\n').split('\t')
                    rows.append((int(p[0], 16), p[1]))
            rows.sort()
            for i, (a, n) in enumerate(rows):
                e = rows[i + 1][0] if i + 1 < len(rows) else img.text_hi
                st.funcs.append((a, min(e, img.text_hi), n))
            if hasattr(img.pe, 'DIRECTORY_ENTRY_IMPORT') or True:
                img.pe.parse_data_directories([pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
                for d in getattr(img.pe, 'DIRECTORY_ENTRY_IMPORT', []):
                    for imp in d.imports:
                        nm = imp.name.decode() if imp.name else 'ord%d' % imp.ordinal
                        st.imports[imp.address] = '%s!%s' % (d.dll.decode(), nm)
        st.funcs.sort()
        st.funcs = apply_splits(st.funcs, os.path.join(os.path.dirname(path), 'splits.csv'))
        # VC6 emits catch blocks inside the parent function's section (and the parent's tail code can
        # follow them), so fold Ghidra's separate Catch@ funclets back into the preceding function.
        folded = []
        for f in st.funcs:
            if folded and f[2].startswith('Catch@') and folded[-1][1] == f[0]:
                folded[-1] = (folded[-1][0], f[1], folded[-1][2])
            else:
                folded.append(f)
        st.funcs = folded
        st.data.sort()
        st._index()
        return st

    def _index(self):
        self.func_addrs = [f[0] for f in self.funcs]
        self.func_by_addr = {f[0]: f for f in self.funcs}
        self.data_addrs = [d[0] for d in self.data]
        self.data_by_addr = {}
        for d in self.data:
            self.data_by_addr.setdefault(d[0], d)

    def ensure_func(self, va):
        """Make va a function entry (splitting the containing extent) and return (addr, end, name)."""
        if va in self.func_by_addr:
            return self.func_by_addr[va]
        i = bisect.bisect_right(self.func_addrs, va) - 1
        if i >= 0 and self.funcs[i][1] > va:
            a, e, n = self.funcs[i]
            self.funcs[i] = (a, va, n)
            self.funcs.insert(i + 1, (va, e, 'fn_%08x' % va))
        else:
            nxt = self.func_addrs[i + 1] if i + 1 < len(self.funcs) else va + 16
            self.funcs.insert(i + 1, (va, nxt, 'fn_%08x' % va))
        self._index()
        return self.func_by_addr[va]

    def set_body(self, va, end, name=None):
        """Make [va, end) exactly one function (a library object's code, without the linker's
        alignment padding): entries strictly inside are dropped (Ghidra splits, e.g. catch blocks).
        Split off any code Ghidra merged past end with split_at() first."""
        self.ensure_func(va)
        i = bisect.bisect_right(self.func_addrs, va)
        old_end = self.funcs[i - 1][1]
        while i < len(self.funcs) and self.funcs[i][0] < end:
            old_end = max(old_end, self.funcs[i][1])
            del self.funcs[i]
        a, _, n = self.funcs[i - 1]
        self.funcs[i - 1] = (a, end, name or n)
        self._index()

    def split_at(self, va):
        if va not in self.func_by_addr and self.containing_func(va):
            self.ensure_func(va)

    def containing_func(self, va):
        i = bisect.bisect_right(self.func_addrs, va) - 1
        if i >= 0 and self.funcs[i][0] <= va < self.funcs[i][1]:
            return self.funcs[i]
        return None

    def containing_data(self, va):
        i = bisect.bisect_right(self.data_addrs, va) - 1
        # several symbols may share a start; walk back over a few in case of nesting
        for j in range(i, max(i - 4, -1), -1):
            a, e, n = self.data[j]
            if a <= va < e:
                return self.data[j]
        return None

    def tables_in(self, lo, hi):
        return {a: v for a, v in self.tables.items() if lo <= a < hi}


# ----------------------------------------------------------------------------------------------
# Relocation recovery
# ----------------------------------------------------------------------------------------------
class Resolver:
    """Maps a referenced address to (symbol name, symbol address) and keeps names unique per VA."""

    def __init__(self, st, img, namemap):
        self.st, self.img, self.namemap = st, img, namemap or {}
        self.name2va = {}
        self.va2name = {}
        self.nm_keys = sorted(k for k in self.namemap if not (img.text_lo <= k < img.text_hi))

    def _namemap_containing(self, va, sym_start):
        """namemap data key k <= va that is at/after the symtab symbol start (or within 64 bytes)."""
        i = bisect.bisect_right(self.nm_keys, va) - 1
        if i < 0:
            return None
        k = self.nm_keys[i]
        if sym_start is not None and k >= sym_start:
            return k
        if sym_start is None and va - k < 64:
            return k
        return None

    def _name(self, va, default):
        if va in self.va2name:
            return self.va2name[va]
        nm = self.namemap.get(va)
        if nm is None:
            nm = default
            if nm in self.name2va and self.name2va[nm] != va:
                nm = '%s_%08x' % (nm, va)
        self.name2va.setdefault(nm, va)
        self.va2name[va] = nm
        return nm

    def code_exact(self, va):
        f = self.st.func_by_addr.get(va)
        return self._name(va, f[2] if f else 'fn_%08x' % va), va

    def import_name(self, va):
        full = self.st.imports[va]
        fn = full.split('!', 1)[-1]
        return self._name(va, '__imp_' + fn), va

    def data_ref(self, va):
        """DIR32 target: containing symbol (namemap/symtab), else synthetic exact symbol."""
        st = self.st
        if va in self.namemap:
            return self._name(va, None), va
        if va in st.imports:
            return self.import_name(va)
        if self.img.text_lo <= va < self.img.text_hi:
            # code pointers (callbacks, SEH handlers) are entries even when the symbol table
            # missed them, so reference the exact address rather than containing-func + addend
            return self.code_exact(va)
        d = st.data_by_addr.get(va)
        if d:
            return self._name(va, d[2]), va
        d = st.containing_data(va)
        k = self._namemap_containing(va, d[0] if d else None)
        if k is not None:
            return self._name(k, None), k
        if d:
            return self._name(d[0], d[2]), d[0]
        # import slot +offset (shouldn't happen) / unknown data: exact synthetic symbol
        return self._name(va, 'dat_%08x' % va), va


def _sweep(code, fva, fend, tables, md):
    """Linear sweep over [fva, fend) skipping tables (dict addr->(end, kind)), discovering inline
    switch tables on the way.  Returns (instructions, tables, issues)."""
    insns, issues = [], []
    off = 0
    n = len(code)
    md.detail = True
    while off < n:
        va = fva + off
        if va in tables:
            off = tables[va][0] - fva
            continue
        # next table boundary limits the decode window
        nxt = min([a for a in tables if a > va] + [fend])
        window = code[off:nxt - fva]
        last = off
        restart = False
        for ins in md.disasm(window, va):
            insns.append(ins)
            last = ins.address + ins.size - fva
            # discover jump tables:  jmp [reg*4 + T] / mov r8,[reg+T] / movzx r,byte [reg+T]
            if ins.disp_size == 4 and ins.id in (capstone.x86.X86_INS_JMP, capstone.x86.X86_INS_MOV,
                                                  capstone.x86.X86_INS_MOVZX):
                for op in ins.operands:
                    if op.type != X86_OP_MEM:
                        continue
                    t = op.mem.disp & 0xffffffff
                    if not (ins.address < t < fend) or any(a <= t < (e or a + 1) for a, (e, _) in tables.items()):
                        continue
                    if ins.id == capstone.x86.X86_INS_JMP and op.mem.scale == 4:
                        e = t
                        while e + 4 <= fend and (e == t or e not in tables):
                            v = struct.unpack_from('<I', code, e - fva)[0]
                            if not (fva <= v < fend):
                                break
                            e += 4
                        if e > t:
                            tables[t] = (e, 'ptr')
                            restart = True
                    elif op.size == 1:
                        tables[t] = (None, 'byte')   # end fixed below
                        restart = True
            if restart or last >= nxt - fva:
                break
        # finalize open-ended byte tables: run to the next table start or the function end
        for a, (e, k) in list(tables.items()):
            if e is None:
                tables[a] = (min([b for b in tables if b > a] + [fend]), k)
        if restart:
            off = last
        elif last < nxt - fva:
            # capstone stopped early: invalid opcode or truncated instruction at a boundary
            rest = code[last:nxt - fva]
            if rest.strip(b'\xcc\x90\x00'):
                issues.append('undecodable at %08x (%s)' % (fva + last, rest[:8].hex()))
            off = last + 1 if rest.strip(b'\xcc\x90\x00') else nxt - fva
        else:
            off = last
        # instructions overlapping a table discovered later -> drop them
    starts = set()
    clean = []
    for ins in insns:
        a = ins.address
        bad = False
        for t, (e, _) in tables.items():
            if a < e and a + ins.size > t:
                bad = True
                break
        if bad:
            issues.append('instruction %08x overlaps table' % a)
            continue
        clean.append(ins)
        starts.add(a)
    return clean, starts, issues


def recover(fva, fend, img, st, res, md, stats):
    """Returns (bytes with addend fields, relocs [(off, type, symname)], labels {va: cls}, issues)."""
    code = bytearray(img.read(fva, fend - fva))
    tables = dict(st.tables_in(fva, fend))
    insns, starts, issues = _sweep(bytes(code), fva, fend, tables, md)
    relocs, labels, values = [], {}, {}
    taken = set()
    selfname = res.code_exact(fva)[0]

    def add(off, typ, sym, symva, value):
        if any(o in taken for o in range(off, off + 4)):
            issues.append('overlapping reloc at %08x' % (fva + off))
            return
        taken.update(range(off, off + 4))
        if typ == IMAGE_REL_I386_REL32:
            field = (value - symva) & 0xffffffff         # value = branch target
        else:
            field = (value - symva) & 0xffffffff
        struct.pack_into('<I', code, off, field)
        relocs.append((off, typ, sym))
        values[off] = value

    def local_label(va, cls):
        if va == fva:
            return selfname, fva
        if cls == CLS_STATIC:
            labels[va] = CLS_STATIC
        else:
            labels.setdefault(va, CLS_LABEL)
        return '$L%x' % va, va

    internal_targets = []
    for ins in insns:
        off = ins.address - fva
        grp = ins.groups
        is_branch = capstone.x86.X86_GRP_JUMP in grp or capstone.x86.X86_GRP_CALL in grp
        rel = is_branch and ins.imm_size and capstone.x86.X86_GRP_BRANCH_RELATIVE in grp
        if rel:
            tgt = ins.operands[0].imm & 0xffffffff
            is_call = ins.id == capstone.x86.X86_INS_CALL
            if fva <= tgt < fend and not (is_call and tgt == fva):
                internal_targets.append(tgt)
                continue
            if ins.imm_size != 4:
                issues.append('short branch out of function at %08x -> %08x' % (ins.address, tgt))
                stats['short_out'] += 1
                continue
            if not (img.text_lo <= tgt < img.text_hi):
                issues.append('rel32 to outside .text at %08x -> %08x' % (ins.address, tgt))
                stats['bad_rel32'] += 1
                continue
            if tgt not in st.func_by_addr:
                stats['rel32_nonentry'] += 1
            sym, sva = res.code_exact(tgt)
            add(off + ins.imm_offset, IMAGE_REL_I386_REL32, sym, sva, tgt)
            stats['rel32'] += 1
            continue
        for fo, fs, kind in ((ins.disp_offset, ins.disp_size, 'disp'), (ins.imm_offset, ins.imm_size, 'imm')):
            if fs != 4 or not fo:
                continue
            v = struct.unpack_from('<I', code, off + fo)[0]
            if not img.in_image(v) or v < img.text_lo:
                continue
            if img.reloc_vas is not None and fva + off + fo not in img.reloc_vas:
                stats['dir32_not_reloc'] += 1      # looks like an address but the base relocation table says it is a constant
                continue
            if fva <= v < fend:
                cls = CLS_STATIC if v in tables else CLS_LABEL
                sym, sva = local_label(v, cls)
                stats['dir32_local'] += 1
            else:
                sym, sva = res.data_ref(v)
                if img.text_lo <= v < img.text_hi and v not in st.func_by_addr:
                    stats['dir32_code_nonentry'] += 1
                if kind == 'imm':
                    stats['dir32_imm'] += 1
                    if not (v in st.func_by_addr or v in st.imports or v in st.data_by_addr or v in res.namemap):
                        stats['dir32_imm_unsym'] += 1   # possible false positive (constant in range)
                else:
                    stats['dir32_disp'] += 1
            add(off + fo, IMAGE_REL_I386_DIR32, sym, sva, v)
    for t, (e, k) in sorted(tables.items()):
        if k != 'ptr':
            stats['byte_tables'] += 1
            continue
        stats['ptr_tables'] += 1
        for a in range(t, e - 3, 4):
            v = struct.unpack_from('<I', code, a - fva)[0]
            if fva <= v < fend:
                sym, sva = local_label(v, CLS_LABEL)
                internal_targets.append(v)
            else:
                sym, sva = res.data_ref(v)
                issues.append('jumptable %08x entry -> %08x outside function' % (a, v))
            add(a - fva, IMAGE_REL_I386_DIR32, sym, sva, v)
            stats['dir32_table'] += 1
    for t in internal_targets:
        if t not in starts and not any(a <= t < e for a, (e, _) in tables.items()):
            issues.append('branch/table target %08x not an instruction start' % t)
    for v in labels:
        if v not in starts and v not in tables:
            issues.append('label %08x not an instruction/table start' % v)
    relocs.sort()
    return bytes(code), relocs, labels, issues, values


# ----------------------------------------------------------------------------------------------
# COFF writer / reader
# ----------------------------------------------------------------------------------------------
class _Strtab:
    def __init__(self):
        self.data = bytearray()
        self.idx = {}

    def name_field(self, name):
        b = name.encode('utf-8')
        if len(b) <= 8:
            return b.ljust(8, b'\0')
        if b not in self.idx:
            self.idx[b] = 4 + len(self.data)
            self.data += b + b'\0'
        return struct.pack('<II', 0, self.idx[b])


def _write_coff(path, sections):
    """sections: list of dict(name, data, relocs[(off,type,symname)], syms[(name,value,cls,type)])
    The first sym of each section is the function (external)."""
    st = _Strtab()
    symrecs = []          # bytes
    symidx = {}           # name -> index (defined first, then undefined)
    nsec = len(sections)
    # pass 1: assign indices for defined symbols
    idx = 0
    for si, s in enumerate(sections):
        idx += 2  # section symbol + aux
        for (nm, val, cls, typ) in s['syms']:
            symidx.setdefault(nm, idx)
            idx += 1
    undef = []
    for s in sections:
        for _, _, nm in s['relocs']:
            if nm not in symidx:
                symidx[nm] = idx
                undef.append(nm)
                idx += 1
    nsym = idx
    # layout
    hdr = 20
    off = hdr + 40 * nsec
    out_sec = bytearray()
    blobs = []
    for s in sections:
        raw = off
        off += len(s['data'])
        rel = off if s['relocs'] else 0
        off += 10 * len(s['relocs'])
        blobs.append((raw, rel))
    symoff = off
    # build
    f = bytearray(struct.pack('<HHIIIHH', 0x14c, nsec, 0, symoff, nsym, 0, 0))
    for s, (raw, rel) in zip(sections, blobs):
        f += st.name_field(s['name'])[:8] if len(s['name']) <= 8 else st.name_field(s['name'])
        f += struct.pack('<IIIIIIHHI', 0, 0, len(s['data']), raw, rel, 0, len(s['relocs']), 0, TEXT_CHARS)
    for s in sections:
        f += s['data']
        for o, t, nm in s['relocs']:
            f += struct.pack('<IIH', o, symidx[nm], t)
    assert len(f) == symoff
    undef_code = set()
    for s in sections:
        for o, t, nm in s['relocs']:
            if t == IMAGE_REL_I386_REL32:
                undef_code.add(nm)
    for si, s in enumerate(sections):
        f += st.name_field('.text') + struct.pack('<IhHBB', 0, si + 1, 0, CLS_STATIC, 1)
        chk = 0
        f += struct.pack('<IHHIHB3x', len(s['data']), len(s['relocs']), 0, chk, 0, 1)
        for (nm, val, cls, typ) in s['syms']:
            f += st.name_field(nm) + struct.pack('<IhHBB', val, si + 1, typ, cls, 0)
    for nm in undef:
        typ = 0x20 if nm in undef_code or nm.startswith('fn_') else 0
        f += st.name_field(nm) + struct.pack('<IhHBB', 0, 0, typ, CLS_EXTERNAL, 0)
    f += struct.pack('<I', 4 + len(st.data)) + st.data
    with open(path, 'wb') as fh:
        fh.write(f)


def read_coff(path):
    d = open(path, 'rb').read()
    _, nsec, _, symoff, nsym, opthdr, _ = struct.unpack_from('<HHIIIHH', d, 0)
    strtab = symoff + nsym * 18
    secs = []
    for i in range(nsec):
        o = 20 + opthdr + i * 40
        size, raw, rel, _, nrel = struct.unpack_from('<IIIIH', d, o + 16)
        secs.append(dict(data=d[raw:raw + size], relocs=[struct.unpack_from('<IIH', d, rel + j * 10) for j in range(nrel)], syms=[]))
    syms, i = [None] * nsym, 0
    while i < nsym:
        o = symoff + i * 18
        nm = d[o:o + 8]
        if nm[:4] == b'\0\0\0\0':
            so = strtab + struct.unpack_from('<I', nm, 4)[0]
            nm = d[so:d.index(b'\0', so)]
        else:
            nm = nm.rstrip(b'\0')
        val, secno, typ, cls, naux = struct.unpack_from('<IhHBB', d, o + 8)
        syms[i] = dict(name=nm.decode('utf-8'), value=val, sec=secno, typ=typ, cls=cls, aux=naux)
        if secno > 0 and not (cls == CLS_STATIC and naux):
            secs[secno - 1]['syms'].append(syms[i])
        i += 1 + naux
    return secs, syms


# ----------------------------------------------------------------------------------------------
# Public API
# ----------------------------------------------------------------------------------------------
_IMG_CACHE = {}


def _image(exe_path):
    if exe_path not in _IMG_CACHE:
        _IMG_CACHE[exe_path] = Image(exe_path)
    return _IMG_CACHE[exe_path]


def new_stats():
    return dict.fromkeys(['funcs', 'bytes', 'rel32', 'rel32_nonentry', 'dir32_disp', 'dir32_imm',
                          'dir32_imm_unsym', 'dir32_code_nonentry', 'dir32_local', 'dir32_table', 'ptr_tables',
                          'byte_tables', 'short_out', 'bad_rel32', 'funcs_with_issues', 'dir32_not_reloc'], 0)


def validate_body_end(start, original_end, body_end, img):
    """Validate an optional body cutoff against an original function extent.

    A shorter body is permitted only when the omitted original bytes are a nonempty
    linker-padding tail.  The symbol table extent itself is left unchanged.
    """
    if not start < body_end <= original_end:
        raise ValueError('body end %08x is outside original extent [%08x, %08x)' %
                         (body_end, start, original_end))
    if body_end < original_end:
        padding = img.read(body_end, original_end - body_end)
        if len(padding) != original_end - body_end or any(b not in (0xCC, 0x90) for b in padding):
            raise ValueError('body end %08x omits non-padding bytes in [%08x, %08x)' %
                             (body_end, body_end, original_end))
    return body_end


def write_target_obj(out_path, func_vas, exe_path, symtab, namemap=None, stats=None, body_ends=None):
    """Write one COFF object with a COMDAT .text section per function.  Returns
    dict(name2va, issues{va: [..]}, stats).  body_ends optionally shortens matched
    bodies whose omitted original tail has been verified as linker padding."""
    img = _image(exe_path)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    res = Resolver(symtab, img, {int(k): v for k, v in (namemap or {}).items()})
    stats = stats if stats is not None else new_stats()
    body_ends = body_ends or {}
    unrequested = set(body_ends) - set(func_vas)
    if unrequested:
        raise ValueError('body ends supplied for unrequested functions: %s' %
                         ', '.join('%08x' % va for va in sorted(unrequested)))
    for va in func_vas:           # entries first so names/extents are settled
        symtab.ensure_func(va)
    sections, issues = [], {}
    for va in sorted(set(func_vas)):
        a, e, _ = symtab.func_by_addr[va]
        e = validate_body_end(a, e, body_ends[va], img) if va in body_ends else e
        name = res.code_exact(a)[0]
        data, relocs, labels, iss, _ = recover(a, e, img, symtab, res, md, stats)
        syms = [(name, 0, CLS_EXTERNAL, 0x20)]
        for lva, cls in sorted(labels.items()):
            nm = '$L%x' % lva
            res.name2va[nm] = lva
            syms.append((nm, lva - a, cls, 0))
        sections.append(dict(name='.text', data=data, relocs=relocs, syms=syms))
        stats['funcs'] += 1
        stats['bytes'] += len(data)
        if iss:
            issues[a] = iss
            stats['funcs_with_issues'] += 1
    _write_coff(out_path, sections)
    return dict(name2va=dict(res.name2va), issues=issues, stats=stats)


def write_target_sections(out_path, sections, exe_path, symtab, namemap=None, stats=None):
    """Like write_target_obj, but each output section is a whole library-object code section:
    sections = [(start_va, end_va, [(offset, name, cls, type), ...])] with the object's own
    symbols, so several functions (MASM files, non-COMDAT C) share one section as in the base
    object and branches between them stay section-internal, without relocations."""
    img = _image(exe_path)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    res = Resolver(symtab, img, {int(k): v for k, v in (namemap or {}).items()})
    stats = stats if stats is not None else new_stats()
    for a, e, syms in (x[:3] for x in sections):     # register the object's names first
        for off, nm, cls, typ in syms:
            res.va2name.setdefault(a + off, nm)
            res.name2va.setdefault(nm, a + off)
    out, issues = [], {}
    for sec in sorted(sections):
        a, e, syms = sec[:3]
        base_offs = sec[3] if len(sec) > 3 else None
        data, relocs, labels, iss, values = recover(a, e, img, symtab, res, md, stats)
        if base_offs is not None:
            # a call to the section's own start: assemblers resolve it, compilers relocate it
            relocs = [r for r in relocs if r[0] in base_offs or not (a <= values[r[0]] < e)]
            data = bytearray(data)
            for off, v in values.items():
                if off not in {r[0] for r in relocs}:
                    data[off:off + 4] = img.read(a + off, 4)
            data = bytes(data)
        own = {a + off for off, _, _, _ in syms}
        osyms = sorted(((nm, off, cls, typ) for off, nm, cls, typ in syms), key=lambda x: (x[1] != 0, x[1]))
        for lva, cls in sorted(labels.items()):
            if lva in own:
                continue
            nm = '$L%x' % lva
            res.name2va[nm] = lva
            osyms.append((nm, lva - a, cls, 0))
        # in-section references to the object's own symbols use those symbols, not $L labels
        mine = {'$L%x' % (a + off): nm for off, nm, _, _ in syms}
        relocs = [(o, t, mine.get(n, n)) for o, t, n in relocs]
        out.append(dict(name='.text', data=data, relocs=relocs, syms=osyms))
        stats['funcs'] += 1
        stats['bytes'] += len(data)
        if iss:
            issues[a] = iss
            stats['funcs_with_issues'] += 1
    _write_coff(out_path, out)
    return dict(name2va=dict(res.name2va), issues=issues, stats=stats)


def verify(out_obj, exe_path, name2va):
    """Re-apply every relocation with the original addresses; compare with the exe bytes.
    Returns a list of mismatch descriptions (empty == perfect round trip)."""
    img = _image(exe_path)
    secs, syms = read_coff(out_obj)
    bad = []
    secva = {}
    for si, s in enumerate(secs):
        fn = [x for x in s['syms'] if x['cls'] == CLS_EXTERNAL and x['name'] in name2va] or              [x for x in s['syms'] if x['name'] in name2va]
        if fn:
            secva[si + 1] = name2va[fn[0]['name']] - fn[0]['value']
    for si, s in enumerate(secs):
        if si + 1 not in secva:
            bad.append('section %d: no symbol with a known address' % (si + 1))
            continue
        fn = [x for x in s['syms'] if x['name'] in name2va]
        sva = secva[si + 1]
        buf = bytearray(s['data'])
        for off, symi, typ in s['relocs']:
            sym = syms[symi]
            S = secva[sym['sec']] + sym['value'] if sym['sec'] > 0 else name2va[sym['name']]
            A = struct.unpack_from('<I', buf, off)[0]
            if typ == IMAGE_REL_I386_DIR32:
                v = (S + A) & 0xffffffff
            elif typ == IMAGE_REL_I386_REL32:
                v = (S + A - (sva + off + 4)) & 0xffffffff
            else:
                bad.append('%s: unknown reloc type %x' % (fn[0]['name'], typ))
                continue
            struct.pack_into('<I', buf, off, v)
        orig = img.read(sva, len(buf))
        if bytes(buf) != orig:
            k = next(i for i in range(len(buf)) if buf[i] != orig[i])
            bad.append('%s @%08x: first diff at +%x' % (fn[0]['name'], sva, k))
    return bad


def namemap_from_base(base_obj, sym2va, exe_path, symtab, namemap=None, log=None):
    """Learn names from a (byte-matching) base object: for every base function symbol in sym2va
    ({base symbol name: VA}) pair each base relocation with the target relocation recovered at the
    same offset and map (target value - base addend) -> base symbol name.  Returns a new namemap
    (input namemap entries kept).  $L labels / section symbols are skipped."""
    img = _image(exe_path)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    nm = dict(namemap or {})
    secs, syms = read_coff(base_obj)
    byname = {}
    for si, s in enumerate(secs):
        for x in s['syms']:
            if x['typ'] == 0x20 and x['cls'] == CLS_EXTERNAL:
                byname[x['name']] = (si, x['value'])
    for name, va in sym2va.items():
        nm[va] = name
        symtab.ensure_func(va)
    for name, va in sym2va.items():
        si, base_off = byname[name]
        a, e, _ = symtab.func_by_addr[va]
        res = Resolver(symtab, img, nm)
        _, _, _, _, values = recover(a, e, img, symtab, res, md, new_stats())
        sec = secs[si]
        for off, symi, typ in sec['relocs']:
            if typ not in (IMAGE_REL_I386_DIR32, IMAGE_REL_I386_REL32):
                continue
            toff = off - base_off
            bsym = syms[symi]
            bname = bsym['name']
            if bname.startswith('$L') or (bsym['cls'] == CLS_STATIC and bsym['aux']):
                continue
            if toff not in values:
                if log is not None:
                    log.append('%s+%x: base reloc to %s has no target reloc' % (name, toff, bname))
                continue
            addend = struct.unpack_from('<i', sec['data'], off)[0]
            tva = (values[toff] - addend) & 0xffffffff
            if nm.get(tva, bname) != bname and log is not None:
                log.append('%08x: %s vs %s (keeping first)' % (tva, nm[tva], bname))
            nm.setdefault(tva, bname)
    return nm


def load_namemap(path):
    if not path:
        return {}
    with open(path) as f:
        m = json.load(f)
    return {int(k, 16): v for k, v in m.items()}


def default_symbols():
    here = os.path.dirname(os.path.abspath(__file__))
    csvp = os.path.join(here, '..', 'config', 'symbols.csv')
    if os.path.exists(csvp):
        return os.path.normpath(csvp)
    return r'E:\AVP2Source\out\stage4\dump\avp2_now.tsv'


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--exe', default=r'E:\AVP2Source\bin\lithtech.exe')
    ap.add_argument('--symbols', default=None, help='symbols.csv (or Ghidra TSV stand-in)')
    ap.add_argument('--namemap', default=None, help='json {hexVA: name}')
    ap.add_argument('--out', help='output .obj')
    ap.add_argument('--range', action='append', default=[], help='lo-hi (hex, hi exclusive): all funcs in range')
    ap.add_argument('--verify', action='store_true', help='round-trip check the written object(s)')
    ap.add_argument('--all', metavar='OUTDIR', help='split every function into --parts objects and verify')
    ap.add_argument('--parts', type=int, default=24)
    ap.add_argument('--learn', metavar='BASE_OBJ', help='derive names from a matching base object; '
                    'positional args then take the form <base-symbol-substring>=<hexVA>')
    ap.add_argument('--save-namemap', metavar='JSON', help='write the (learned) namemap')
    ap.add_argument('-q', '--quiet', action='store_true')
    ap.add_argument('vas', nargs='*', help='hexVA, or symsubstr=hexVA with --learn')
    a = ap.parse_args()
    img = _image(a.exe)
    st = SymTab.load(a.symbols or default_symbols(), img)
    nm = load_namemap(a.namemap)

    if a.all:
        os.makedirs(a.all, exist_ok=True)
        t0 = time.time()
        funcs = [f[0] for f in st.funcs]
        per = (len(funcs) + a.parts - 1) // a.parts
        stats, nbad, allissues = new_stats(), 0, {}
        for p in range(a.parts):
            chunk = funcs[p * per:(p + 1) * per]
            if not chunk:
                continue
            out = os.path.join(a.all, 'part%02d_%08x.obj' % (p, chunk[0]))
            info = write_target_obj(out, chunk, a.exe, st, nm, stats)
            bad = verify(out, a.exe, info['name2va'])
            nbad += len(bad)
            for b in bad[:5]:
                print('VERIFY FAIL', b)
            allissues.update(info['issues'])
        dt = time.time() - t0
        print('symbols: %s' % st.source)
        print('functions %d, bytes %d, objects %d, %.1fs, round-trip failures %d' %
              (stats['funcs'], stats['bytes'], a.parts, dt, nbad))
        print(json.dumps(stats, indent=1))
        print('functions with disassembly issues: %d' % len(allissues))
        if not a.quiet:
            for va, iss in sorted(allissues.items()):
                print('  %08x %-40s %s' % (va, st.func_by_addr[va][2][:40], '; '.join(iss[:3]) + (' ...' if len(iss) > 3 else '')))
        return

    vas, learn = [], {}
    for v in a.vas:
        sub, _, h = v.rpartition('=')
        vas.append(int(h, 16))
        if sub:
            learn[sub] = int(h, 16)
    if a.learn:
        secs, _ = read_coff(a.learn)
        fnames = [x['name'] for s in secs for x in s['syms'] if x['typ'] == 0x20 and x['cls'] == CLS_EXTERNAL]
        sym2va = {}
        for sub, va in learn.items():
            c = [n for n in fnames if sub in n]
            c = [n for n in c if n == sub] or c
            if len(c) != 1:
                ap.error('%s matches %d base symbols: %s' % (sub, len(c), c[:4]))
            sym2va[c[0]] = va
        log = []
        nm = namemap_from_base(a.learn, sym2va, a.exe, st, nm, log)
        for l in log:
            print('  learn:', l)
    if a.save_namemap:
        with open(a.save_namemap, 'w') as f:
            json.dump({'%08x' % k: v for k, v in sorted(nm.items())}, f, indent=1)
    for r in a.range:
        lo, hi = (int(x, 16) for x in r.split('-'))
        vas += [f[0] for f in st.funcs if lo <= f[0] < hi]
    if not vas or not a.out:
        ap.error('need --out and at least one VA or --range')
    info = write_target_obj(a.out, vas, a.exe, st, nm)
    s = info['stats']
    print('%s: %d funcs, %d bytes, rel32 %d, dir32 %d (+%d local, %d table)' % (
        a.out, s['funcs'], s['bytes'], s['rel32'], s['dir32_disp'] + s['dir32_imm'], s['dir32_local'], s['dir32_table']))
    for va, iss in sorted(info['issues'].items()):
        print('  issues %08x: %s' % (va, '; '.join(iss)))
    if a.verify:
        bad = verify(a.out, a.exe, info['name2va'])
        print('verify: %s' % ('OK' if not bad else '%d FAIL\n  ' % len(bad) + '\n  '.join(bad)))
        if bad:
            sys.exit(1)


if __name__ == '__main__':
    main()
