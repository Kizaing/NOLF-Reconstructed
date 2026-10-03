r"""Find prebuilt library code (CRT, STL, WONAPI, StdLith, ...) in lithtech.exe and verify it.

  python tools/libmatch.py [-v] [--libs DIR] [--report FILE]

Every code section of every library object under E:\AVP2Source\libs_objs\<lib>\*.obj is matched
against the exe's .text:
  * candidates come from names (undecorated obj symbol == Ghidra name) and from a search for the
    section's longest relocation-free byte run; a candidate must equal the section byte for byte
    outside relocation fields;
  * relocations are then checked for consistency: each target is a key (external name, or
    obj-local section) and every matched section implies an address for it.  Sections are accepted
    from the most certain ones outwards (unique long matches first, then candidates that agree with
    what is already known), and a candidate that contradicts a known address is rejected;
  * the same object often exists in several library variants (VC6 RTM/SP3/SP5/SP6 LIBCMT, ...):
    identical sections are attributed to the first library in LIB_PRIORITY; a variant only wins
    where its bytes are the ones that match.

Only objects of PREBUILT libraries become units.  The lithshared libraries in libs_objs (RezMgr,
StdLith, lith, controlfilemgr, ButeMgr) are game-side builds: the engine's copies differ, so their
partial matches are only reported (and listed under "source_built_matches").  Matches below
REGION_LO are engine COMDAT copies of the same inline/template code and are ignored; .text$x
sections (EH unwind funclets) start at TEXTX_LO.

Writes config/libraries.json:
  {"units": [{"name": "lib/<lib>/<obj>", "obj": <abs path>, "lib": .., "comp_id": "prod/build",
              "functions": {"<hexVA>": "<obj symbol>"},          # .text symbols
              "sections": {"<hexVA>": [secno, len, vclass, how, secname, [[off, sym, cls, type]..]]},
              "data": {"<hexVA>": "<symbol>"}}, ...],            # the object's placed data
   "names": {"<hexVA>": "<symbol>"},     # externals: definitions + relocation targets (global namemap)
   "source_built_matches": {...}}
and build/libmatch_report.txt (per-library counts, Ghidra extent differences, data-only objects
for the Rich header, unresolved sections).
"""
import argparse
import bisect
import collections
import csv
import json
import os
import struct
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
from coffobj import CoffObj, undecorate, REL_DIR32, REL_REL32, REL_SIZES  # noqa: E402
import mktarget  # noqa: E402

EXE = r'E:\AVP2Source\bin\lithtech.exe'
LIBS = r'E:\AVP2Source\libs_objs'
SYMBOLS_CSV = os.path.join(ROOT, 'config', 'symbols.csv')
OUT_JSON = os.path.join(ROOT, 'config', 'libraries.json')
SCN_CNT_CODE = 0x20
SCN_LNK_COMDAT = 0x1000
MIN_ANCHOR = 6          # shortest relocation-free run used for a byte search
MAX_HITS = 4000
# preferred attribution when identical code exists in several libraries (earlier wins)
LIB_PRIORITY = ['VC6SP5_LIBCMT', 'VC6SP5_LIBCPMT', 'LT_WONAPI', 'LT_StdLith', 'LT_lith', 'LT_Zlib',
                'LT_RezMgr', 'LT_ButeMgr', 'LT_CryptMgr', 'LT_dibmgr',
                'VC6_LIBCMT_1999', 'VC6_LIBCMT_1998', 'VC6SP6_LIBCMT', 'VC6_LIBC',
                'VC6_LIBCPMT', 'VC6_LIBCP', 'LT_StdLith_alloc', 'LT_Rezmgrfull']


# libraries that are linked as prebuilt objects (everything else is source we compile ourselves:
# the lithshared libs in libs_objs are game-side builds whose code differs from the engine's copy)
PREBUILT = {'VC6SP5_LIBCMT', 'VC6SP6_LIBCMT', 'VC6_LIBCMT_1998', 'VC6_LIBCMT_1999', 'VC6_LIBC',
            'VC6SP5_LIBCPMT', 'VC6_LIBCPMT', 'VC6_LIBCP', 'LT_WONAPI'}
FAMILY = {'VC6SP5_LIBCMT': 'LIBCMT', 'VC6SP6_LIBCMT': 'LIBCMT', 'VC6_LIBCMT_1998': 'LIBCMT',
          'VC6_LIBCMT_1999': 'LIBCMT', 'VC6_LIBC': 'LIBCMT', 'VC6SP5_LIBCPMT': 'LIBCPMT',
          'VC6_LIBCPMT': 'LIBCPMT', 'VC6_LIBCP': 'LIBCPMT', 'LT_StdLith_alloc': 'LT_StdLith',
          'LT_Rezmgrfull': 'LT_RezMgr', 'LT_LithTrackMgrClient': 'LT_lithtrackmgr'}
# Library code is linked after every engine object: matches below this address are engine COMDAT
# copies of the same inline/template code (found by this script: first WONAPI object; the engine's
# last function is 004a04d0).
REGION_LO = 0x4a0500
TEXTX_LO = 0x4c3680         # .text$x (EH unwind funclets) of the library objects starts here


def lib_rank(lib):
    return LIB_PRIORITY.index(lib) if lib in LIB_PRIORITY else len(LIB_PRIORITY)


# ---------------------------------------------------------------- inputs

class Exe:
    def __init__(self):
        self.img = mktarget._image(EXE)
        lo, hi, _, data = [s for s in self.img.sections if s[2] == '.text'][0]
        self.lo, self.hi, self.text = lo, hi, data

    def read(self, va, n):
        return self.img.read(va, n)


IMPORTS = {}        # IAT slot -> imported function name (None for ordinals)


def imp_check(key, addr):
    """None: not an import key; True/False: an __imp_ key that does / doesn't hit its IAT slot."""
    if key[0] != 'E' or not key[1].startswith('__imp_'):
        return None
    if addr not in IMPORTS:
        return False
    want = IMPORTS[addr]
    if want is None:
        return None
    n = key[1][6:]
    n = n[1:] if n.startswith('_') else n
    return n.split('@')[0] == want


class Ghidra:
    def __init__(self):
        self.funcs = {}         # va -> (end, name)
        self.by_name = collections.defaultdict(list)
        for r in csv.DictReader(open(SYMBOLS_CSV, encoding='utf-8')):
            if r['kind'] == 'import':
                f = r['name'].split('!', 1)[-1]
                IMPORTS[int(r['addr'], 16)] = None if f.startswith('Ordinal_') else f
            if r['kind'] == 'func':
                va = int(r['addr'], 16)
                self.funcs[va] = (int(r['end'], 16), r['name'])
                self.by_name[r['name']].append(va)
        self.entries = sorted(self.funcs)

    def containing(self, va):
        i = bisect.bisect_right(self.entries, va) - 1
        if i >= 0 and va < self.funcs[self.entries[i]][0]:
            return self.entries[i]
        return None


class Sec:
    """One code section of a library object, plus its candidate placements."""

    def __init__(self, ob, s):
        self.ob, self.secno, self.data = ob, s.index, s.data
        self.comdat = bool(s.flags & SCN_LNK_COMDAT)
        self.secname = s.name
        self.mask = bytearray(len(s.data))
        self.relocs = []        # (off, type, key, delta)  key address = implied target - delta
        for off, si, typ in s.relocs:
            n = REL_SIZES.get(typ, 4)
            for k in range(n):
                if off + k < len(self.mask):
                    self.mask[off + k] = 1
            if typ not in (REL_DIR32, REL_REL32):
                continue
            sym = ob.obj.symbols[si]
            addend = int.from_bytes(s.data[off:off + 4], 'little', signed=(typ == REL_REL32))
            key, base = ob.key_of(sym)
            self.relocs.append((off, typ, key, addend + base, sym.name))
        # function-ish symbols: typ 0x20, or (MASM) any external/static non-label symbol
        self.syms = [x for x in s.syms if not x.name.startswith(('$L', '$SG', '$T')) and x.cls in (2, 3)]
        self.cands = {}         # va -> how found ('name'/'bytes')
        self.va = None
        self.status = None

    @property
    def id(self):
        return (self.ob.id, self.secno)

    def name(self):
        f = [x for x in self.syms if x.value == 0]
        return (f or self.syms or [None])[0].name if (f or self.syms) else '<sec%d>' % self.secno

    def anchor(self):
        """(offset, bytes) of the longest relocation-free run."""
        best, cur = (0, 0), None
        for i in range(len(self.data) + 1):
            if i < len(self.data) and not self.mask[i]:
                if cur is None:
                    cur = i
            else:
                if cur is not None and i - cur > best[1] - best[0]:
                    best = (cur, i)
                cur = None
        return best[0], self.data[best[0]:best[1]]

    def matches_at(self, exe, va):
        if va < exe.lo or va + len(self.data) > exe.hi:
            return False
        t = exe.text[va - exe.lo:va - exe.lo + len(self.data)]
        d, m = self.data, self.mask
        if not any(m):
            return t == d
        return all(m[i] or d[i] == t[i] for i in range(len(d)))

    def implied(self, exe, va, raw=None):
        """[(key, address)] from definitions and relocations if the section sits at va."""
        out = [(('S', self.ob.id, self.secno), va)]
        for x in self.syms:
            if x.cls == 2:
                out.append((('E', x.name), va + x.value))
        for off, typ, key, delta, _ in self.relocs:
            if raw is not None:
                field = struct.unpack_from('<I', raw, off)[0]
            else:
                field = struct.unpack_from('<I', exe.text, va - exe.lo + off)[0]
            if typ == REL_DIR32:
                tgt = (field - delta) & 0xffffffff
            else:
                tgt = (va + off + 4 + field - delta) & 0xffffffff
            out.append((key, tgt))
        return out


class DSec(Sec):
    """Initialised data section (vtables, EH tables, strings, constants): placed through the
    address of its own symbol once a matched code section references it."""

    def __init__(self, ob, s):
        Sec.__init__(self, ob, s)
        self.name_ = s.name
        self.syms = [x for x in s.syms if x.cls in (2, 3)]
        self.done = False

    def raw_at(self, exe, va):
        try:
            return exe.read(va, len(self.data))
        except ValueError:
            return None

    def matches_at(self, exe, va):
        t = self.raw_at(exe, va)
        if t is None:
            return False
        d, m = self.data, self.mask
        return all(m[i] or d[i] == t[i] for i in range(len(d)))

    def base_from(self, known):
        k = ('S', self.ob.id, self.secno)
        if k in known:
            return known[k]
        for x in self.syms:
            if x.cls == 2 and ('E', x.name) in known:
                return known[('E', x.name)] - x.value
        return None


class Obj:
    def __init__(self, lib, path):
        self.lib, self.path = lib, path
        self.objname = os.path.basename(path)
        self.id = '%s/%s' % (lib, os.path.splitext(self.objname)[0])
        self.obj = CoffObj(path)
        cid = [s.value for s in self.obj.symbols.values() if s.name == '@comp.id']
        self.comp_id = '%d/%d' % (cid[0] >> 16, cid[0] & 0xffff) if cid else None
        self.secs = [Sec(self, s) for s in self.obj.sections if s.flags & SCN_CNT_CODE and s.data]
        self.dsecs = [DSec(self, s) for s in self.obj.sections
                      if not s.flags & SCN_CNT_CODE and s.data and s.flags & 0x40
                      and not s.name.startswith(('.debug', '.drectve'))]

    def key_of(self, sym):
        """Relocation target key and the symbol's offset inside it.  Externals resolve by name
        (a COMDAT may come from another object); statics and section symbols by obj section."""
        if sym.cls == 2 or sym.secno <= 0:
            return ('E', sym.name), 0
        return ('S', self.id, sym.secno), (0 if sym.is_section_symbol else sym.value)


def load_objs(libs_dir, only=None):
    objs = []
    for lib in sorted(os.listdir(libs_dir), key=lambda l: (lib_rank(l), l)):
        d = os.path.join(libs_dir, lib)
        if not os.path.isdir(d) or (only and lib not in only):
            continue
        seen = set()
        for f in sorted(os.listdir(d)):
            if not f.lower().endswith('.obj') or f.lower() in seen:
                continue
            seen.add(f.lower())
            try:
                objs.append(Obj(lib, os.path.join(d, f)))
            except (ValueError, struct.error):
                pass        # machine-0 alias objects etc.
    return objs


# ---------------------------------------------------------------- candidate search

def ghidra_names_for(sym):
    n = sym.name
    out = {n, undecorate(n)}
    if n.startswith('?'):
        q = undecorate(n)           # Class::Method
        out.add(q)
        out.add(q.replace(' ', '_'))
    if n.startswith('_') and not n.startswith('__'):
        out.add(n[1:])
    if '@' in n and not n.startswith('?'):
        out.add(n.lstrip('_').split('@')[0])
    return out


def find_candidates(objs, exe, gh):
    hit_cache = {}
    for ob in objs:
        for s in ob.secs:
            for x in s.syms:
                for g in ghidra_names_for(x):
                    for va in gh.by_name.get(g, ()):
                        base = va - x.value
                        if base >= REGION_LO and s.matches_at(exe, base):
                            s.cands[base] = 'name'
            off, anc = s.anchor()
            if len(anc) < MIN_ANCHOR and len(anc) < len(s.data):
                # short sections: try every Ghidra entry (cheap for tiny bodies)
                if len(s.data) <= 64:
                    for va in gh.entries:
                        if va >= REGION_LO and s.matches_at(exe, va):
                            s.cands.setdefault(va, 'entry')
                continue
            key = bytes(anc)
            if key not in hit_cache:
                hits, p = [], exe.text.find(key)
                while p >= 0 and len(hits) < MAX_HITS:
                    hits.append(p)
                    p = exe.text.find(key, p + 1)
                hit_cache[key] = hits
            for p in hit_cache[key]:
                va = exe.lo + p - off
                if va >= REGION_LO and va not in s.cands and s.matches_at(exe, va):
                    s.cands[va] = 'bytes'


# ---------------------------------------------------------------- resolution

def resolve(objs, exe, gh, log):
    secs = [s for ob in objs for s in ob.secs if s.cands]
    known = {}              # key -> address
    evidence = collections.Counter()
    claimed = {}            # va -> Sec
    accepted = []
    # identical code under other names (template instances, strcat/mbscat): a placement can't
    # tell which name it is, so it doesn't define its names; callers' references do
    bucket = collections.defaultdict(set)
    for x in secs:
        defs = frozenset(y.name for y in x.syms if y.cls == 2)
        for va in x.cands:
            bucket[(va, x.data)].add(defs)
    twin = lambda x, va: len(bucket[(va, x.data)]) > 1

    def contradictions(s, va, strong=False):
        """(bad, good[, strong]): strong agreement = somebody else placed this very section or
        symbol, or it points into its own object's already placed sections"""
        bad, good, st = 0, 0, 0
        imp = s.implied(exe, va)
        nself = 1 + sum(1 for x in s.syms if x.cls == 2)
        for i, (k, a) in enumerate(imp):
            ic = imp_check(k, a)
            if ic is not None:
                if ic:
                    good += 1
                    st += 1
                else:
                    bad += 1
                continue
            if k in known:
                if known[k] == a:
                    good += 1
                    if i < nself or (k[0] == 'S' and k[1] == s.ob.id):
                        st += 1
                else:
                    bad += 1
        return (bad, good, st) if strong else (bad, good)

    def overlaps(s, va):
        """another accepted section already covers [va, va+len)"""
        for v2 in range(va, va + len(s.data)):
            o = claimed.get(v2)
            if o is not None:
                return o
        return None

    def accept(s, va, how):
        s.va, s.status = va, how
        for v in range(va, va + len(s.data)):
            claimed[v] = s
        imp = s.implied(exe, va)
        if twin(s, va):
            imp = [ka for ka in imp if not (ka[0][0] == 'E' and ka[0][1] in {y.name for y in s.syms})]
        for k, a in imp:
            if k not in known:
                known[k] = a
            evidence[(k, a)] += 1
        accepted.append(s)

    def rank(s):     # name matches first: identical code under several names (strcat/mbscat)
        return ('name' not in s.cands.values(), lib_rank(s.ob.lib), s.ob.lib, s.ob.objname, s.secno)

    dsecs = [d for ob in objs for d in ob.dsecs]

    def propagate():
        """data sections whose address became known: check their bytes, learn their pointers"""
        n = 0
        while True:
            new = 0
            for d in dsecs:
                if d.done:
                    continue
                base = d.base_from(known)
                if base is None:
                    continue
                d.done = True
                if not d.matches_at(exe, base):
                    d.status = 'mismatch'
                    continue
                raw = d.raw_at(exe, base)
                imp = d.implied(exe, base, raw)
                if any(not exe.img.in_image(a) for k, a in imp[1:]):
                    d.status = 'mismatch'
                    continue
                d.va, d.status = base, 'placed'
                if any(k in known and known[k] != a for k, a in imp):
                    d.status = 'conflict'
                    continue
                for k, a in imp:
                    if k not in known:
                        known[k] = a
                        new += 1
                    evidence[(k, a)] += 1
            n += new
            if not new:
                return n

    def is_confident(s, va):
        how = s.cands[va]
        on_entry = va in gh.funcs
        _, anc = s.anchor()
        if how == 'name' and on_entry:
            return True
        return on_entry and len(s.cands) == 1 and len(anc) >= 24

    # identical sections from variant libraries compete for one VA: process in priority order
    for rnd in range(50):
        progress = False
        pending = sorted((s for s in secs if s.va is None and s.status is None), key=rank)
        for s in pending:
            ok = []
            for va in sorted(s.cands):
                if overlaps(s, va):
                    continue
                bad, good, st = contradictions(s, va, True)
                if bad:
                    continue
                ok.append((va, good, st))
            if not ok:
                if all(overlaps(s, va) for va in s.cands):
                    s.status = 'dup'        # same bytes already attributed (variant / COMDAT copy)
                continue
            if rnd == 0:
                conf = [(va, g) for va, g, _ in ok if is_confident(s, va)]
                if len(conf) == 1 and len(ok) == 1:
                    accept(s, conf[0][0], 'unique')
                    progress = True
                continue
            # later rounds: a candidate is backed by known addresses (callers, own obj's sections)
            best = max(st for _, _, st in ok)
            top = [va for va, _, st in ok if st == best]
            bestg = max(g for _, g, _ in ok)
            topg = [va for va, g, _ in ok if g == bestg]
            if best > 0 and len(top) == 1:
                accept(s, top[0], 'constrained')
                progress = True
            elif bestg > 0 and len(topg) == 1 and len(s.data) >= 24:
                accept(s, topg[0], 'constrained-weak')
                progress = True
            elif len(ok) == 1 and is_confident(s, ok[0][0]):
                accept(s, ok[0][0], 'unique')
                progress = True
        if propagate():
            progress = True
        if not progress and rnd > 0:
            break
    # final passes: lone candidates that abut accepted code (the linker packs sections), or sit on
    # a Ghidra entry, with no contradictions
    def abuts(va, n):
        for p in range(va - 1, va - 17, -1):
            o = claimed.get(p)
            if o is not None:
                return all(b in (0xcc, 0x90) for b in exe.read(p + 1, va - p - 1))
        for p in range(va + n, va + n + 16):
            o = claimed.get(p)
            if o is not None:
                return p == o.va and all(b in (0xcc, 0x90) for b in exe.read(va + n, p - va - n))
        return False

    for how in ('adjacent', 'lone'):
        while True:
            progress = False
            for s in sorted((s for s in secs if s.va is None and s.status is None), key=rank):
                ok = [va for va in s.cands if not overlaps(s, va) and contradictions(s, va)[0] == 0]
                if len(ok) != 1:
                    continue
                if (how == 'adjacent' and abuts(ok[0], len(s.data))) or                         (how == 'lone' and ok[0] in gh.funcs and len(s.data) >= 8):
                    accept(s, ok[0], how)
                    progress = True
            if propagate() == 0 and not progress:
                break
    # gap filling: sections too short to search for (or hidden inside a Ghidra function), placed
    # right after accepted code of the same object, else of the same library family
    by_obj, by_fam = collections.defaultdict(list), collections.defaultdict(list)
    for ob in objs:
        for x in ob.secs:
            by_obj[ob.id].append(x)
            by_fam[FAMILY.get(ob.lib, ob.lib)].append(x)
    tried = set()      # fits only shrink as placements accumulate: a failed spot never succeeds
    while True:
        progress = False
        for a in sorted(accepted, key=lambda x: x.va):
            end = a.va + len(a.data)
            if end in tried:
                continue
            spots = [end]
            g = end
            while g - end < 15 and g not in claimed and exe.read(g, 1)[0] in (0xcc, 0x90):
                g += 1
                if g % 4 == 0:
                    spots.append(g)
            for g in spots:
                if g in claimed or g >= exe.hi or g in tried:
                    continue
                pool = by_obj[a.ob.id] + by_fam[FAMILY.get(a.ob.lib, a.ob.lib)]
                fits = []
                for x in pool:
                    # a tiny section (a 1-byte ret) can be marked 'dup' because every byte-search hit overlaps other
                    # code; right after accepted code of its own object it is still placeable (fpinit's __fpclear)
                    if x.va is None and (x.status is None or (x.status == 'dup' and x.ob.id == a.ob.id)) \
                            and len(x.data) <= 4096 and x.matches_at(exe, g)                             and not overlaps(x, g):
                        bad, good, st = contradictions(x, g, True)
                        if not bad:
                            fits.append((-st, x.ob.id != a.ob.id, -len(x.data), rank(x), id(x), x))
                if not fits:
                    tried.add(g)
                    continue
                fits.sort()
                accept(fits[0][-1], g, 'gapfill')
                progress = True
                break
        if propagate() == 0 and not progress:
            break
    propagate()
    return accepted, known, evidence


def attribute(objs, accepted, exe, known):
    """The same bytes often exist in several objects (variant libraries, COMDAT template copies in
    every WONAPI object).  Give each placement to the object that owns its neighbourhood."""
    by_va = collections.defaultdict(list)
    for ob in objs:
        for s in ob.secs:
            for va in s.cands:
                by_va[(va, len(s.data))].append(s)
    acc = sorted(accepted, key=lambda s: s.va)
    alts = {s.va: by_va[(s.va, len(s.data))] for s in acc}
    uniq = {}
    for s in acc:
        ids = {a.ob.id for a in alts[s.va]}
        if len(ids) == 1:
            uniq[s.va] = s.ob
    evid = collections.Counter(o.id for o in uniq.values())
    uvas = sorted(uniq)
    out = []
    fv = {}         # obj-local section keys implied by sections attributed so far

    def note(s):
        for k, v in s.implied(exe, s.va):
            if k[0] == 'S':
                fv.setdefault(k, v)

    for s in acc:
        cands = []
        for a in alts[s.va]:
            if a is not s and a.va is not None:
                continue
            bad = good = 0
            for k, v in a.implied(exe, s.va):
                if imp_check(k, v) is False:
                    bad += 1
                elif k[0] == 'E' and k in known:
                    if known[k] == v:
                        good += 1
                    else:
                        bad += 1
            sgood = 0
            for k, v in a.implied(exe, s.va):
                if k[0] == 'S' and k in fv:
                    if fv[k] != v:
                        bad += 1
                    else:
                        sgood += 1
            if not bad:
                cands.append((a, (sgood, good)))
        if not cands:
            out.append(s)
            note(s)
            continue
        top = max(g for _, g in cands)
        cands = [a for a, g in cands if g == top]
        if s.va in uniq or len({a.ob.id for a in cands}) == 1:
            out.append(s)
            note(s)
            continue
        i = bisect.bisect_left(uvas, s.va)
        prev = uniq[uvas[i - 1]] if i > 0 else None
        nxt = uniq[uvas[i]] if i < len(uvas) else None

        def score(a):
            return (prev is not None and a.ob.id == prev.id, nxt is not None and a.ob.id == nxt.id,
                    evid[a.ob.id] > 0, prev is not None and a.ob.lib == prev.lib, -lib_rank(a.ob.lib), -len(a.ob.id))
        best = max(cands, key=score)
        if best is not s:
            best.va, best.status = s.va, s.status
            s.va, s.status = None, 'dup'
        out.append(best)
        note(best)
    return out, evid


def obj_proximity_check(accepted, log):
    """Sections of one object are laid out in object order by the linker; report outliers."""
    by_obj = collections.defaultdict(list)
    for s in accepted:
        by_obj[s.ob.id].append(s)
    odd = []
    for oid, ss in by_obj.items():
        if len(ss) < 2:
            continue
        vas = [s.va for s in sorted(ss, key=lambda s: s.secno)]
        span = max(vas) - min(vas)
        if span > 0x20000:
            odd.append((oid, span))
    return odd


# ---------------------------------------------------------------- verification classes

def classify(objs, accepted, exe):
    """Re-derive every relocation target from the final placement alone (code + the data
    sections it reaches) and grade each section: exact (no relocations), verified (every target
    is defined by a placed section or implied identically by two independent places),
    unverified (bytes match, some target seen once), conflict (a target implied differently)."""
    vals = collections.defaultdict(collections.Counter)
    for s in accepted:
        for k, a in s.implied(exe, s.va):
            vals[k][a] += 1
    defined = {}
    for s in accepted:
        defined[('S', s.ob.id, s.secno)] = s.va
        for x in s.syms:
            if x.cls == 2:
                defined[('E', x.name)] = s.va + x.value
    for ob in objs:
        for d in ob.dsecs:
            d.done, d.va = False, None
    dsecs = [d for ob in objs for d in ob.dsecs]
    while True:     # data sections (vtables, EH tables, strings) placed by the final code
        new = 0
        known = {k: next(iter(c)) for k, c in vals.items() if len(c) == 1}
        for d in dsecs:
            if d.done:
                continue
            base = d.base_from(known)
            if base is None:
                continue
            d.done = True
            if not d.matches_at(exe, base):
                d.status = 'mismatch'
                continue
            imp = d.implied(exe, base, d.raw_at(exe, base))[1:]
            if any(not exe.img.in_image(a) for k, a in imp if k[0] == 'E' or k[1] != d.ob.id):
                d.status = 'mismatch'
                continue
            d.va, d.status = base, 'placed'
            for k, a in imp:
                vals[k][a] += 1
            vals[('S', d.ob.id, d.secno)][base] += 1
            new += 1
        if not new:
            break
    conflicts = []
    for s in accepted:
        if not s.relocs:
            s.vclass = 'exact'
            continue
        weak = bad = 0
        for k, a in s.implied(exe, s.va)[1:]:
            c = vals[k]
            ic = imp_check(k, a)
            if ic is not None:
                if not ic:
                    conflicts.append((s, k, sorted(c), a))
                    bad += 1
                continue
            if len(c) > 1 or (k in defined and defined[k] != a):
                conflicts.append((s, k, sorted(c), a))
                bad += 1
            elif not (k in defined or c[a] >= 2):
                weak += 1
        s.vclass = 'conflict' if bad else 'verified' if not weak else 'unverified'
    dplaced = [d for d in dsecs if d.va is not None]
    return conflicts, dplaced, vals


WEAK = ('lone', 'adjacent', 'constrained-weak', 'gapfill')


def isolated(accepted, textx_lo):
    """Placements far from any other code of the same library family: inline/template copies that
    really belong to neighbouring (engine-built) code.  Also weak placements (short sections placed
    only by elimination) of an object that has no strong placement and whose neighbours belong to
    another library."""
    acc = sorted(accepted, key=lambda s: s.va)
    strong = {s.ob.id for s in acc if s.status not in WEAK}
    out = []
    for i, s in enumerate(acc):
        if s.status in WEAK and s.ob.id not in strong:
            nb = [acc[j] for j in (i - 1, i + 1) if 0 <= j < len(acc)]
            fam = FAMILY.get(s.ob.lib, s.ob.lib)
            if not any(FAMILY.get(n.ob.lib, n.ob.lib) == fam for n in nb):
                out.append(s)
    if out:
        return out
    fam = collections.defaultdict(list)
    for s in accepted:
        if s.va < textx_lo:
            fam[FAMILY.get(s.ob.lib, s.ob.lib)].append(s.va)
    for v in fam.values():
        v.sort()
    out = []
    for s in accepted:
        if s.va >= textx_lo or len(s.data) >= 256:
            continue
        v = fam[FAMILY.get(s.ob.lib, s.ob.lib)]
        i = bisect.bisect_left(v, s.va)
        near = [x for x in v[max(0, i - 1):i + 2] if x != s.va and abs(x - s.va) < 0x400]
        if not near:
            out.append(s)
    return out


# ---------------------------------------------------------------- output

def build_units(objs, accepted, vals):
    """Library units (prebuilt objects only) + names learned for the global namemap."""
    known = {k: next(iter(c)) for k, c in vals.items() if len(c) == 1}
    units, names = {}, {}
    prebuilt = [s for s in accepted if s.ob.lib in PREBUILT]
    for s in sorted(prebuilt, key=lambda s: s.va):
        u = units.setdefault(s.ob.id, {'name': 'lib/' + s.ob.id, 'obj': s.ob.path, 'lib': s.ob.lib,
                                       'comp_id': s.ob.comp_id, 'functions': {}, 'sections': {}, 'data': {}})
        sec = s.ob.obj.sections[s.secno - 1]
        syms = [[x.value, x.name, x.cls, x.typ] for x in sec.syms if not x.name.startswith('$L')]
        u['sections']['%08x' % s.va] = [s.secno, len(s.data), s.vclass, s.status, s.secname, syms]
        for x in s.syms:
            va = s.va + x.value
            if s.secname == '.text':
                u['functions'].setdefault('%08x' % va, x.name)
            if x.cls == 2:
                names.setdefault(va, x.name)
    for oid, u in units.items():
        ob = next(o for o in objs if o.id == oid)
        for sec in ob.obj.sections:
            a = known.get(('S', oid, sec.index))
            if a is None or sec.flags & SCN_CNT_CODE:
                continue
            if not sec.syms or sec.syms[0].value != 0:
                u['data'].setdefault('%08x' % a, sec.name)
            for x in sec.syms:
                u['data']['%08x' % (a + x.value)] = x.name
                if x.cls == 2:
                    names.setdefault(a + x.value, x.name)
    # externals referenced by prebuilt code (CRT data, imports __imp__X@n, other library functions)
    for s in prebuilt:
        for off, typ, key, delta, symname in s.relocs:
            if key[0] == 'E' and key in known:
                names.setdefault(known[key], key[1])
    return units, names


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('-v', action='store_true')
    ap.add_argument('--libs', default=LIBS)
    ap.add_argument('--only', nargs='*')
    ap.add_argument('--report', default=os.path.join(ROOT, 'build', 'libmatch_report.txt'))
    ap.add_argument('--out', default=OUT_JSON)
    a = ap.parse_args()
    exe, gh = Exe(), Ghidra()
    objs = load_objs(a.libs, a.only)
    print('%d library objects, %d code sections' % (len(objs), sum(len(o.secs) for o in objs)))
    find_candidates(objs, exe, gh)
    log = []
    fam_secs = collections.defaultdict(list)
    for ob in objs:
        for sec in ob.secs:
            fam_secs[FAMILY.get(ob.lib, ob.lib)].append(sec)
    for it in range(12):
        for ob in objs:
            for sec in ob.secs + ob.dsecs:
                sec.va, sec.status = None, None
                if hasattr(sec, 'done'):
                    sec.done = False
        accepted, known, evidence = resolve(objs, exe, gh, log)
        accepted, evid = attribute(objs, accepted, exe, known)
        iso = isolated(accepted, TEXTX_LO)
        if not iso:
            break
        for sec in iso:
            log.append('isolated: %08x %s %s (dropped)' % (sec.va, sec.ob.id, sec.name()))
            for other in fam_secs[FAMILY.get(sec.ob.lib, sec.ob.lib)]:
                other.cands.pop(sec.va, None)
    conflicts, dplaced, vals = classify(objs, accepted, exe)
    units, names = build_units(objs, accepted, vals)
    srcbuilt = collections.defaultdict(dict)
    for sec in accepted:
        if sec.ob.lib not in PREBUILT:
            for x in sec.syms:
                srcbuilt[sec.ob.id]['%08x' % (sec.va + x.value)] = [x.name, sec.vclass]
    rep = report(objs, accepted, exe, gh, conflicts, units) + rich_report(objs, units, dplaced, [k for k, c in vals.items() if len(c) == 1]) + log
    os.makedirs(os.path.dirname(a.report), exist_ok=True)
    open(a.report, 'w').write('\n'.join(rep) + '\n')
    out = {'units': sorted(units.values(), key=lambda u: u['name']),
           'names': {'%08x' % k: v for k, v in sorted(names.items())},
           'source_built_matches': {k: dict(sorted(v.items())) for k, v in sorted(srcbuilt.items())}}
    json.dump(out, open(a.out, 'w', newline=''), indent=1)
    print('\n'.join(rep[:60]))
    print('... full report: %s' % a.report)


def rich_report(objs, units, dplaced, known_keys=()):
    """Objects linked without matched code (data-only: tables, init segments, globals), found
    through placed non-COMDAT data sections, for the Rich header accounting."""
    libs = {u['lib'] for u in units.values()}
    have = {(FAMILY.get(u['lib'], u['lib']), os.path.basename(u['obj']).lower()) for u in units.values()}
    extra = {}
    taken = {d.va for d in dplaced if d.ob.id in units}
    for d in sorted(dplaced, key=lambda d: (lib_rank(d.ob.lib), d.ob.id)):
        ob = d.ob
        if ob.lib not in libs or ob.id in units or d.va in taken:
            continue
        taken.add(d.va)
        sec = ob.obj.sections[d.secno - 1]
        if sec.flags & SCN_LNK_COMDAT:
            continue
        key = (FAMILY.get(ob.lib, ob.lib), ob.objname.lower())
        if key in have:
            continue
        extra.setdefault(key, (ob, []))[1].append('%s@%08x' % (sec.name, d.va))
    rep = ['data-only linked objects (placed non-COMDAT data, no matched code): %d' % len(extra)]
    comp = collections.Counter(u['comp_id'] for u in units.values())
    for (fam, name), (ob, where) in sorted(extra.items()):
        rep.append('  %-28s %-8s %s' % (ob.id, ob.comp_id, ' '.join(where[:4])))
        comp[ob.comp_id] += 1
    rep.append('objects by @comp.id incl. data-only: ' + ', '.join('%s x%d' % kv for kv in sorted(comp.items())))
    # objects whose external symbols are referenced by placed code but that were not placed
    # themselves (code too short / ambiguous to place)
    known = set(known_keys)
    placed = set(units) | {ob.id for (_, _), (ob, _) in extra.items()}
    miss = []
    unit_defs = {x.name for ob in objs if ob.id in units for x in ob.obj.symbols.values() if x.cls == 2 and x.secno > 0}
    for ob in objs:
        if ob.lib not in libs or ob.id in placed:
            continue
        key = (FAMILY.get(ob.lib, ob.lib), ob.objname.lower())
        if key in have:
            continue
        defs = [x.name for x in ob.obj.symbols.values() if x.cls == 2 and x.secno > 0
                and not (ob.obj.sections[x.secno - 1].flags & SCN_LNK_COMDAT)]
        hit = [n for n in defs if ('E', n) in known and n not in unit_defs]
        if hit:
            miss.append((ob, hit))
    rep.append('objects referenced by placed code but not placed themselves (.bss-only data, or code '
               'too short to place): %d' % len(miss))
    for ob, hit in miss:
        rep.append('  %-28s %-8s %s' % (ob.id, ob.comp_id, ' '.join(hit[:4])))
    return rep


def report(objs, accepted, exe, gh, conflicts, units):
    rep = []
    total = sum(e - va for va, (e, _) in gh.funcs.items())
    by_lib = collections.defaultdict(lambda: collections.Counter())
    for s in accepted:
        c = by_lib[s.ob.lib]
        c['sections'] += 1
        c['bytes'] += len(s.data)
        c[s.vclass] += 1
        c[s.vclass + '_bytes'] += len(s.data)
        c['objs_' + s.ob.id] = 1
    rep.append('%-18s %6s %5s %8s  %-22s %s' % ('library', 'secs', 'objs', 'bytes', 'exact/verified/unverif', 'bytes exact/verified/unverif'))
    tb = 0
    for lib, c in sorted(by_lib.items(), key=lambda kv: -kv[1]['bytes']):
        nobj = sum(1 for k in c if k.startswith('objs_'))
        rep.append('%-18s %6d %5d %8d  %-22s %d/%d/%d%s' % (
            lib, c['sections'], nobj, c['bytes'], '%d/%d/%d' % (c['exact'], c['verified'], c['unverified']),
            c['exact_bytes'], c['verified_bytes'], c['unverified_bytes'],
            '' if lib in PREBUILT else '   (source-built copy: partial, not a unit)'))
        if lib in PREBUILT:
            tb += c['bytes']
    rep.append('prebuilt library code bytes %d of %d function bytes (%.2f%%)' % (tb, total, 100.0 * tb / total))
    # entry alignment
    off_entry = [s for s in accepted if s.va not in gh.funcs]
    rep.append('matches not starting at a Ghidra function entry: %d' % len(off_entry))
    for s in off_entry[:40]:
        c = gh.containing(s.va)
        rep.append('  %08x %-40s in %s' % (s.va, s.name()[:40], ('%08x %s' % (c, gh.funcs[c][1])) if c else '-'))
    # extent mismatches
    rep.append('sections vs Ghidra extents (section longer than extent or extent > section + 15 padding):')
    for s in sorted(accepted, key=lambda s: s.va):
        if s.va not in gh.funcs:
            continue
        ext = gh.funcs[s.va][0] - s.va
        if len(s.data) > ext or ext - len(s.data) > 15:
            rep.append('  %08x %-40s sec %d ext %d (%s)' % (s.va, s.name()[:40], len(s.data), ext, gh.funcs[s.va][1][:40]))
    rep.append('relocation conflicts: %d' % len(conflicts))
    for s, k, seen, got in conflicts[:40]:
        rep.append('  %08x %s %s: implied %s, here %08x' % (s.va, s.ob.id, k, ' '.join('%08x' % x for x in seen), got))
    # comp.id of matched objects
    comp = collections.Counter()
    for oid in units:
        comp[units[oid]['comp_id']] += 1
    rep.append('matched objects by @comp.id: ' + ', '.join('%s x%d' % kv for kv in sorted(comp.items())))
    # unmatched sections with candidates (ambiguous)
    amb = [s for ob in objs for s in ob.secs if s.va is None and s.cands and s.status != 'dup']
    rep.append('sections with byte candidates left unresolved: %d' % len(amb))
    for s in amb[:60]:
        rep.append('  %-40s %-30s %d bytes, %d cands %s' % (s.name()[:40], s.ob.id[:30], len(s.data), len(s.cands),
                                                          ' '.join('%08x' % v for v in sorted(s.cands)[:4])))
    return rep


if __name__ == '__main__':
    main()
