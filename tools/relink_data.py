r"""Per-unit data layout for tools/relink.py: where each object's .rdata/.data/.bss contributions sit in the exe, whether a
fully matched unit's own data sections reproduce the exe's bytes, and the per-unit data stand-in pieces.

  python tools/relink.py --mode mixed --data-units [--write-data-units]     ranges + per-unit data status
  python tools/relink.py --mode mixed --split-standin                       one stand-in piece per unit, in link order
  python tools/relink.py --mode mixed --split-standin --own-data            fully matched units whose data matches supply
                                                                            their own .data/.rdata sections

How the exe's data is laid out (LINK 6, see the README "Data layout"):
  .rdata  0x4c6000 IAT (.idata$5) | 0x4c6480 '.rdata' contributions in link order | '.rdata$r' (RTTI) | '.xdata$x' (EH)
          | import descriptors and names (.idata$2-$6)
  .data   0x4cf000 the merged .CRT$X?? tables (initialiser pointers, link order inside $XCU) | 0x4cf150 '.data'
          contributions in link order (VC6 /Gf string literals are '.data' COMDATs here) | 0x4de2b0 '.bss' in link order
Inside one object the sections follow the object's section table order. A COMDAT (string literal, __real@ constant,
vtable, $T table) is placed with the first object in link order that defines it; later copies are discarded.

Locating: a section's address follows from any relocation that refers to it: the exe's bytes at the relocated field
give target + addend. Code sections are placed by their annotated functions (only functions that MATCH), data sections
by code relocations and then, for byte-identical objects, by data-to-data relocations.
"""
import os
import struct

import coffedit as F

RDATA_LO, RDATA_HI = 0x4C6480, 0x4CD37C
CRT_LO, CRT_HI = 0x4CF000, 0x4CF150          # merged .CRT$X?? tables at the start of .data
DATA_LO, DATA_HI = 0x4CF150, 0x4DE2AC        # '.data' contributions (the last one is LIBCMT initcon's __confh)
BSS_LO, BSS_HI = 0x4DE2B0, 0x4CF000 + 0x19198
GROUPS = ('.rdata', '.data', '.bss')


def u32(b):
    return struct.unpack('<I', b)[0]


def s32(v):
    return struct.unpack('<i', struct.pack('<I', v & 0xffffffff))[0]


def is_code(s):
    return bool(s.flags & F.SCN_CNT_CODE)


def is_data(s):
    return not is_code(s) and s.name != '.drectve' and not s.name.startswith('.debug')


def is_section_sym(s):
    return s.cls == F.CLS_STATIC and s.naux and s.name.startswith('.')


def sec_align(s):
    a = (s.flags >> 20) & 0xf
    return 1 << (a - 1) if a else 16


def align(v, a):
    return (v + a - 1) & ~(a - 1)


def group_of(name):
    """Output group of an object section name (None: not handled here)."""
    if name in GROUPS:
        return name
    if name.startswith('.CRT$'):
        return '.CRT'
    if name in ('.rdata$r', '.xdata$x'):
        return '.rtail'
    return None


def region_of(va):
    if RDATA_LO <= va < RDATA_HI:
        return '.rdata'
    if CRT_LO <= va < CRT_HI:
        return '.CRT'
    if DATA_LO <= va < DATA_HI:
        return '.data'
    if BSS_LO <= va < BSS_HI:
        return '.bss'
    return None


def comdat_sym(o, k):
    for s in o.syms:
        if s is not None and s.sec == k and not is_section_sym(s):
            return s.name
    return None


def reloc_target(img, va_field, data, off, t):
    """Target address that the exe's bytes imply for a relocation (DIR32 or REL32) at va_field."""
    field = u32(img.read(va_field, 4))
    addend = u32(data[off:off + 4])
    if t == 6:
        return (field - addend) & 0xffffffff
    return (va_field + 4 + s32(field) - s32(addend)) & 0xffffffff


class Obj:
    """One object's data sections with the addresses the exe gives them."""

    def __init__(self, unit, coff, fva, kind):
        self.unit, self.o, self.fva, self.kind = unit, coff, fva, kind
        self.secva = {}
        self.conflicts = []         # (from section, offset, section, va so far, va implied, symbol)
        self.points = []            # (secno, offset, va, symbol name) from code relocations: where the exe has each symbol
        self.named = []             # (secno, offset, va, symbol name) from the name's address (build/symva.json, namemap)
        self.chain = {}             # group -> Chain

    def locate(self, img, va_of_name=None):
        o = self.o
        secva = self.secva
        for s in o.syms:
            if s is not None and s.sec > 0 and s.name in self.fva and is_code(o.sections[s.sec - 1]):
                secva.setdefault(s.sec, self.fva[s.name] - s.value)
        code_known = set(secva)
        propagate = self.kind != 'base'         # a partly matched unit's data layout may differ: only code says where
        if propagate and va_of_name:
            # globals that only other units' code refers to (console variables): the name's address
            for s in o.syms:
                if s is not None and s.sec > 0 and s.cls == F.CLS_EXTERNAL and not is_section_sym(s) \
                        and is_data(o.sections[s.sec - 1]) and not o.sections[s.sec - 1].flags & F.SCN_LNK_COMDAT:
                    va = va_of_name(s.name)
                    if va is not None and region_of(va):
                        self.named.append((s.sec, s.value, va, s.name))
        for k, v, va, n in self.named:
            if k in secva:
                if secva[k] != va - v:
                    self.conflicts.append((0, 0, k, secva[k], va - v, n))
            else:
                secva[k] = va - v
        work, seen = list(secva), set()
        while work:
            k = work.pop()
            if k in seen:
                continue
            seen.add(k)
            sec = o.sections[k - 1]
            if sec.flags & F.SCN_CNT_UNINIT or (not propagate and k not in code_known):
                continue
            for off, si, t in sec.relocs:
                S = o.syms[si]
                if S.sec <= 0 or t not in (6, 20) or is_code(o.sections[S.sec - 1]):
                    continue
                try:
                    tva = reloc_target(img, secva[k] + off, sec.data, off, t)
                except ValueError:
                    continue
                if k in code_known:
                    self.points.append((S.sec, S.value, tva, S.name))
                sva = tva - S.value
                if S.sec in secva:
                    if secva[S.sec] != sva:
                        self.conflicts.append((k, off, S.sec, secva[S.sec], sva, S.name))
                    continue
                secva[S.sec] = sva
                work.append(S.sec)
        # .CRT$XCU: pointers to the object's initialisers; find them in the exe's CRT table
        crt = img.read(CRT_LO, CRT_HI - CRT_LO)
        for k, sec in enumerate(o.sections, 1):
            if sec.name.startswith('.CRT$') and sec.relocs and k not in secva:
                off, si, t = sec.relocs[0]
                S = o.syms[si]
                if S.sec in code_known:
                    want = struct.pack('<I', secva[S.sec] + S.value + u32(sec.data[off:off + 4]))
                    p = crt.find(want)
                    while p >= 0 and (p - off) % 4:
                        p = crt.find(want, p + 1)
                    if p >= 0:
                        secva[k] = CRT_LO + p - off

    def data_sections(self, group):
        return [(k, s) for k, s in enumerate(self.o.sections, 1) if is_data(s) and group_of(s.name) == group]


class Chain:
    """The sections an object contributes to one output group, placed the way LINK would: in section-table order, each
    at the next address its alignment allows. Located COMDATs below the chain's start are pooled into an earlier
    object; issues are anything the exe's layout contradicts."""

    def __init__(self):
        self.owned = []         # (secno, va)
        self.pooled = []        # secno
        self.dropped = []       # secno: unlocated and not at the next address (unreferenced COMDAT, or pooled)
        self.issues = []        # (kind, text)
        self.lo = self.hi = None


def masked_equal(img, sec, va):
    if not sec.size:
        return True
    try:
        got = img.read(va, sec.size)
    except ValueError:
        return False
    masked = set()
    for off, si, t in sec.relocs:
        masked.update(range(off, off + 4))
    return all(got[i] == sec.data[i] for i in range(sec.size) if i not in masked)


def first_diff(img, sec, va):
    got = img.read(va, sec.size)
    masked = set()
    for off, si, t in sec.relocs:
        masked.update(range(off, off + 4))
    for i in range(sec.size):
        if i not in masked and got[i] != sec.data[i]:
            return i
    return None


def reloc_want(obj, S, va_of_name):
    """Address our object says relocation target S is at (None if unknown)."""
    if S.sec > 0 and S.sec in obj.secva:
        return obj.secva[S.sec] + S.value
    if S.sec == 0 and va_of_name:
        return va_of_name(S.name)
    return None


def fits_at(img, obj, sec, va, va_of_name):
    """Could unlocated section `sec` be at `va`: equal bytes, and every relocation we can check agrees (a section that is
    all relocations, e.g. a vtable, needs at least one checked relocation)."""
    if not masked_equal(img, sec, va):
        return False
    checked = 0
    for off, si, t in sec.relocs:
        want = reloc_want(obj, obj.o.syms[si], va_of_name)
        if want is None:
            continue
        if reloc_target(img, va + off, sec.data, off, t) != want:
            return False
        checked += 1
    return checked > 0 or not sec.relocs


def build_chain(img, obj, group, va_of_name=None, min_start=None):
    """min_start: the end of the previous object's contribution; anything located below it is pooled (COMDAT) or out of
    order (plain)."""
    secs = obj.data_sections(group)
    if group == '.bss':
        secs = [(k, s) for k, s in secs if s.size]
    floor = min_start or 0
    # a start candidate: located in this group's region, and (COMDATs) not below the previous object's contribution
    loc = [(k, s) for k, s in secs if k in obj.secva and region_of(obj.secva[k]) == group
           and (obj.secva[k] >= floor or not s.flags & F.SCN_LNK_COMDAT)]
    best = None
    for c, _ in loc or [(None, None)]:
        ch = Chain()
        if c is None:
            ch.dropped = [k for k, s in secs if k not in obj.secva]
            ch.pooled = [k for k, s in secs if k in obj.secva]
            return ch
        start = obj.secva[c]
        lowest = max(floor, start) if min_start is not None else start
        cursor = None
        bad = 0
        for k, s in secs:
            va = obj.secva.get(k)
            comdat = bool(s.flags & F.SCN_LNK_COMDAT)
            if va is not None and region_of(va) != group:
                bad += 1
                ch.issues.append(('section', 'sec %d %s %s is %s in our object, at %08x (%s) in the exe' % (
                    k, s.name, comdat_sym(obj.o, k), group, va, region_of(va))))
                continue
            if cursor is None:
                if k != c:
                    if va is None:
                        ch.dropped.append(k)
                    elif comdat and va < lowest:
                        ch.pooled.append(k)
                    else:
                        bad += 1
                        ch.issues.append(('order', 'sec %d %s %s at %08x precedes the chain start %08x' % (
                            k, s.name, comdat_sym(obj.o, k), va, start)))
                    continue
                cursor = start
            a = align(cursor, sec_align(s))
            if va is None:
                if group != '.bss' and s.size and comdat and region_of(a) == group and fits_at(img, obj, s, a, va_of_name):
                    ch.owned.append((k, a))
                    cursor = a + s.size
                elif group == '.bss' and not comdat:
                    ch.owned.append((k, a))
                    cursor = a + s.size
                else:
                    ch.dropped.append(k)
            elif va == a or (k == c):
                ch.owned.append((k, va))
                cursor = va + s.size
            elif comdat and va < lowest:
                ch.pooled.append(k)
            elif va > a and va - a < 0x1000:
                bad += 1
                ch.issues.append(('gap', '%d bytes at %08x before sec %d %s %s (missing item, or an item in the wrong place)' % (
                    va - a, a, k, s.name, comdat_sym(obj.o, k))))
                ch.owned.append((k, va))
                cursor = va + s.size
            else:
                bad += 1
                kind = 'later' if comdat and va > a else 'order'
                ch.issues.append((kind, 'sec %d %s %s at %08x, expected at %08x' % (k, s.name, comdat_sym(obj.o, k), va, a)))
        score = sum(obj.o.sections[k - 1].size for k, _ in ch.owned) - 1000 * bad
        if best is None or score > best[0]:
            best = (score, ch)
    ch = best[1]
    if ch.owned:
        ch.lo = ch.owned[0][1]
        last = ch.owned[-1]
        ch.hi = last[1] + obj.o.sections[last[0] - 1].size
    return ch


def check_contents(img, obj, ch, group, va_of_name):
    """Content and relocation-target issues of the owned sections."""
    o = obj.o
    for k, va in ch.owned:
        s = o.sections[k - 1]
        if s.flags & F.SCN_CNT_UNINIT or not s.size:
            continue
        reg = region_of(va)
        if reg != group:
            ch.issues.append(('section', 'sec %d %s %s is %s in our object, at %08x (%s) in the exe' % (
                k, s.name, comdat_sym(o, k), group, va, reg)))
            continue
        d = first_diff(img, s, va)
        if d is not None:
            nm = [x.name for x in o.syms if x is not None and x.sec == k and not is_section_sym(x) and x.value <= d]
            ch.issues.append(('content', 'sec %d %s %s differs at +%x (%08x)%s' % (
                k, s.name, comdat_sym(o, k), d, va + d, ' in ' + nm[-1] if nm else '')))
        for off, si, t in s.relocs:
            S = o.syms[si]
            tva = reloc_target(img, va + off, s.data, off, t)
            want = None
            if S.sec > 0 and S.sec in obj.secva and not is_code(o.sections[S.sec - 1]):
                want = obj.secva[S.sec] + S.value
            elif S.sec > 0 and is_code(o.sections[S.sec - 1]) and S.sec in obj.secva:
                want = obj.secva[S.sec] + S.value
            elif S.sec == 0:
                want = va_of_name(S.name)
            if want is not None and want != tva:
                ch.issues.append(('reloc', 'sec %d %s +%x -> %s: exe %08x, ours %08x' % (k, comdat_sym(o, k), off, S.name, tva, want)))


def bss_order(obj):
    """(ok, detail): do the .bss symbols sit in the exe in the same relative order and spacing as in our object?"""
    o = obj.o
    out = []
    for k, s in obj.data_sections('.bss'):
        pts = sorted({(v, va, n) for sk, v, va, n in obj.points if sk == k})
        if len(pts) < 2:
            continue
        base = {va - v for v, va, n in pts}
        if len(base) > 1:
            ours = [n for v, va, n in sorted(pts)]
            exe = [n for v, va, n in sorted(pts, key=lambda x: x[1])]
            out.append('sec %d: %d symbols, our offsets vs exe addresses disagree (%s order: %s)' % (
                k, len(pts), 'same' if ours == exe else 'different', ' '.join(x[:30] for x in exe[:8])))
    return out


# ------------------------------------------------------------------------------------------ layout of every object

class Layout:
    def __init__(self, img, objs, link_of):
        """objs: [Obj]; link_of: unit name -> link position (the unit's lowest .text address)."""
        self.img, self.objs, self.link_of = img, objs, link_of
        self.by_unit = {x.unit: x for x in objs}

    def run(self, va_of_name):
        self.va_of_name = va_of_name
        for x in self.objs:
            x.locate(self.img, va_of_name)
            if x.kind in ('full', 'lib'):
                for g in GROUPS:
                    self.chain(x, g)
        self.rtail = min([va for x in self.objs if x.kind == 'lib' for k, s in x.data_sections('.rtail')
                          for va in [x.secva.get(k)] if va is not None and RDATA_LO <= va < RDATA_HI] or [RDATA_HI])
        self.all_ranges()
        # an object whose chain starts below the previous object's contribution: everything located there is pooled
        # into an earlier object (COMDATs); rebuild its chain above that point
        for g in GROUPS:
            for lo, hi, u, w in self.rejected[g]:
                x = self.by_unit.get(u)
                if x is None or w < 1000:
                    continue
                L = self.link_of.get(u, 1 << 40)
                below = [r[1] for v, r in self.range[g].items() if self.link_of.get(v, 1 << 40) < L]
                self.chain(x, g, max(below) if below else None)
        self.all_ranges()
        self.edges()

    def chain(self, x, g, min_start=None):
        x.chain[g] = build_chain(self.img, x, g, self.va_of_name, min_start)
        if x.kind == 'full' and g in ('.rdata', '.data'):
            check_contents(self.img, x, x.chain[g], g, self.va_of_name)

    @staticmethod
    def bss_shuffled(x):
        for k, s in x.data_sections('.bss'):
            if len({va - v for sk, v, va, n in x.points if sk == k}) > 1:
                return True
        return False

    def status(self, x):
        """(status, [issues]) for a fully matched unit's .rdata/.data."""
        iss, edge = [], []
        for g in ('.rdata', '.data'):
            iss += [(g, k, t) for k, t in x.chain[g].issues]
            edge += [(g, k, t) for k, t in self.edge_issues.get((x.unit, g), [])]
        # 'edge': the object's own sections match, but bytes next to it belong to nobody known (an item missing here
        # or in the neighbour); the unit can still supply its own data, the stand-in fills the gap
        return ('differs' if iss else 'edge' if edge else 'match'), iss + edge

    def edges(self):
        """Bytes between two exactly known neighbours (in link order, with only objects known to contribute nothing
        in between) that neither explains: an item missing at the end of the first or the start of the second."""
        self.edge_issues = {}
        order = sorted(self.link_of, key=lambda u: self.link_of[u])
        for g in ('.rdata', '.data'):
            for i, u in enumerate(order):
                x = self.by_unit.get(u)
                if x is None or x.kind not in ('full', 'lib') or not x.chain[g].owned or u not in self.range[g]:
                    continue
                for v in order[i + 1:]:
                    y = self.by_unit.get(v)
                    if y is None or y.kind not in ('full', 'lib'):
                        break
                    if not y.chain[g].owned:
                        continue
                    if v not in self.range[g]:
                        break
                    k0 = y.chain[g].owned[0][0]
                    pad = align(x.chain[g].hi, sec_align(y.o.sections[k0 - 1])) - x.chain[g].hi
                    gap = y.chain[g].lo - x.chain[g].hi
                    if gap > pad:
                        t = '%d bytes at %08x-%08x between %s and %s that neither object has' % (
                            gap - pad, x.chain[g].hi, y.chain[g].lo, u, v)
                        self.edge_issues.setdefault((u, g), []).append(('edge', t))
                        self.edge_issues.setdefault((v, g), []).append(('edge', t))
                    break

    # ---------------------------------------------------------------------------------- ranges
    def anchors(self, group):
        """[(lo, hi, unit, weight)]: chains of byte-identical objects (strong), symbol addresses of partly matched units
        (weak points)."""
        out = []
        for x in self.objs:
            ch = x.chain.get(group)
            if x.kind in ('full', 'lib') and group == '.bss' and self.bss_shuffled(x):
                # our .bss order is not the exe's (names): only the referenced symbols' addresses are known
                vas = sorted(va for k, v, va, n in x.points if region_of(va) == group)
                if vas:
                    out.append((vas[0], vas[-1] + 1, x.unit, 500))
            elif x.kind in ('full', 'lib'):
                if ch and ch.owned:
                    out.append((ch.lo, ch.hi, x.unit, 1000 + len(ch.owned)))
            else:
                plain = {k for k, s in x.data_sections(group) if not s.flags & F.SCN_LNK_COMDAT}
                vas = sorted(va for k, v, va, n in x.points if k in plain and region_of(va) == group)
                if vas:
                    out.append((vas[0], vas[0] + 1, x.unit, 1))
                    if vas[-1] != vas[0]:
                        out.append((vas[-1], vas[-1] + 1, x.unit, 1))
        return sorted(out)

    def ranges(self, group):
        """unit -> [lo, hi, strength, n anchors]: the heaviest set of anchors whose addresses ascend with the link order
        (an object's contributions are contiguous), and the anchors that contradict it."""
        an = self.anchors(group)
        n = len(an)
        L = [self.link_of.get(a[2], 1 << 40) for a in an]
        best = [0] * n
        prev = [-1] * n
        for j in range(n):
            best[j] = an[j][3]
            for i in range(j):
                ok = (an[i][2] == an[j][2]) or (L[i] < L[j] and an[i][1] <= an[j][0])
                if ok and best[i] + an[j][3] > best[j]:
                    best[j], prev[j] = best[i] + an[j][3], i
        keep = set()
        if n:
            j = max(range(n), key=lambda k: best[k])
            while j >= 0:
                keep.add(j)
                j = prev[j]
        res, rejected = {}, []
        for i, (lo, hi, u, w) in enumerate(an):
            if i not in keep:
                rejected.append((lo, hi, u, w))
                continue
            r = res.setdefault(u, [lo, hi, 'exact' if w >= 1000 else 'points', 0])
            r[0], r[1], r[3] = min(r[0], lo), max(r[1], hi), r[3] + 1
            if w < 1000 and r[2] == 'exact':
                r[2] = 'exact+points'
        return res, rejected

    def all_ranges(self):
        self.range, self.rejected = {}, {}
        for g in GROUPS:
            self.range[g], self.rejected[g] = self.ranges(g)
        return self.range

    def crt_entries(self):
        """unit -> [va of each .CRT$ section]."""
        out = {}
        for x in self.objs:
            for k, s in x.data_sections('.CRT'):
                if k in x.secva:
                    out.setdefault(x.unit, []).append((x.secva[k], s.size, s.name))
        return out


GROUP_SPAN = {'.rdata': (RDATA_LO, None), '.data': (DATA_LO, DATA_HI), '.bss': (BSS_LO, BSS_HI)}


def pieces(layout, own=()):
    """[(key, group, lo, hi, unit)]: the per-unit stand-in pieces. `own`: units whose .rdata/.data come from their base
    object (no piece for them in those groups). Every byte of the three groups is in exactly one piece or own range."""
    out = []
    for g in GROUPS:
        lo_g, hi_g = GROUP_SPAN[g]
        if hi_g is None:
            hi_g = layout.rtail
        rs = sorted((r[0], r[1], u) for u, r in layout.range[g].items())
        pos = lo_g
        prev_unit = None
        for lo, hi, u in rs:
            if lo > pos:
                # bytes no anchor explains: give them to the unit before (or the next one at the start)
                out.append(((layout.link_of.get(prev_unit, 0), 3, pos), g, pos, lo, prev_unit or u))
            pos = max(pos, lo)
            if not (u in own and g != '.bss'):
                out.append(((layout.link_of.get(u, 0), 0, lo), g, lo, hi, u))
            pos = hi
            prev_unit = u
        if pos < hi_g:
            out.append(((layout.link_of.get(prev_unit, 0), 3, pos), g, pos, hi_g, prev_unit))
    return sorted(out)


def piece_alignment(va):
    al = 16
    while va % al:
        al //= 2
    return al


def make_piece_obj(img, name, lo, hi, names):
    """One stand-in piece: section `name` with the exe's bytes [lo, hi) and a public symbol for every (va, name)."""
    c = F.Coff()
    if name == '.bss':
        sec = F.Sec('.bss', b'', [], F.SCN_CNT_UNINIT | F.SCN_ALIGN[piece_alignment(lo)] | F.SCN_MEM_READ | F.SCN_MEM_WRITE)
        sec.size = hi - lo
    else:
        fl = F.SCN_CNT_INIT | F.SCN_ALIGN[piece_alignment(lo)] | F.SCN_MEM_READ
        if not name.startswith('.rdata'):
            fl |= F.SCN_MEM_WRITE
        sec = F.Sec(name, img.read(lo, hi - lo), [], fl)
    c.sections.append(sec)
    c.syms.append(F.Sym(sec.name, 0, 1, 0, F.CLS_STATIC, F.section_aux(sec.size, 0)))
    c.syms.append(None)
    for va, nm in names:
        c.add_symbol(nm, va - lo, 1)
    return c
