r"""Relink spike: link build/target (and base) objects with a data stand-in into build/relink/lithtech.exe.

  python tools/build.py          # first: base + target objects, build/{namemap,symva,objvas,objsym}.json
  python tools/relink.py [--mode targets|mixed] [--stage prep|link|all]

Stages
  prep     copy every target object to build/relink/obj with its symbols made linkable (unique external names,
           references resolved by VA, import references renamed to the import library's names), write the data
           stand-in object (build/relink/standin.obj: .rdata/.data/.bss copied from the original exe, public
           symbols at the original addresses), the import libraries and the /ORDER file.
  link     run LINK.EXE.
Set VC6CL is not needed here; the compiler is only used by build.py.
"""
import argparse
import json
import os
import subprocess
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import coffedit  # noqa: E402
from coffedit import Coff, Sec, Sym  # noqa: E402
import mktarget  # noqa: E402

BUILD = os.path.join(ROOT, 'build')
OUT = os.path.join(BUILD, 'relink')
EXE = r'E:\AVP2Source\bin\lithtech.exe'
MSVC = r'E:\MSVC6\VC98\Bin'
LINK = os.path.join(MSVC, 'LINK.EXE')
LIBX = os.path.join(MSVC, 'LIB.EXE')
IMAGE_BASE = 0x400000

RDATA_LO = 0x4C6480        # first byte after the import address table, 16-aligned
RDATA_HI = 0x4CD37C        # start of the import descriptors (.idata$2)
DATA_LO, DATA_SIZE = 0x4CF000, 0x10000
BSS_SIZE = 0x19198 - 0x10000


EXTRA_LINK_FLAGS = []
USE_ORDER = False


def tool_env():
    env = dict(os.environ)
    env['PATH'] = MSVC + ';E:\\MSVC6\\COMMON\\MSDev98\\Bin;' + env['PATH']
    env['LIB'] = r'E:\MSVC6\VC98\Lib'
    return env


def run(args, **kw):
    r = subprocess.run(args, capture_output=True, text=True, env=tool_env(), **kw)
    return r.returncode, (r.stdout + r.stderr)


def load_json(name):
    with open(os.path.join(BUILD, name)) as f:
        return json.load(f)


# ------------------------------------------------------------------------------------------ original exe

class Orig:
    def __init__(self):
        import pefile
        self.img = mktarget._image(EXE)
        pe = self.img.pe
        pe.parse_data_directories([pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_IMPORT']])
        self.dlls = []          # [(dll name, [(slot va, entry name or None, ordinal, hint)])]
        for d in pe.DIRECTORY_ENTRY_IMPORT:
            self.dlls.append((d.dll.decode(), [(i.address, i.name.decode() if i.name else None, i.ordinal, i.hint)
                                               for i in d.imports]))
        self.slots = {s[0]: (dll, s[1], s[2]) for dll, ss in self.dlls for s in ss}
        self.entry = pe.OPTIONAL_HEADER.AddressOfEntryPoint + IMAGE_BASE
        self.text_lo, self.text_hi = self.img.text_lo, self.img.text_hi
        text = [s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text'][0]
        self.text_end = IMAGE_BASE + text.VirtualAddress + text.Misc_VirtualSize       # real end of code (0x4c54b5)


def imp_symbol(entry):
    return '__imp__' + entry


# ------------------------------------------------------------------------------------------ import libraries

def make_idata_obj(orig):
    """The import tables as one object of .idata$2..$6 sections holding the original's bytes, with an
    __imp__<name> symbol at every IAT slot.

    Why not import libraries (tried first, see the report): LINK lays the IAT out in an order that comes from its
    internal pull order and hash tables, and writes the hint/name pool in pull order too. The thunk objects of a
    LIB /DEF library, LINK's hints (0 instead of the DLL's), and the thunk COMDATs all differ from the original,
    and the IAT order is baked into every `call [slot]` in .text. Taking the tables from the exe makes the
    addresses right; deriving them from real import libraries is the open item."""
    img = orig.img
    F = coffedit
    c = Coff()
    parts = [('.idata$2', 0x4CD37C, 0x4CD41C, 4), ('.idata$3', 0x4CD41C, 0x4CD430, 4),
             ('.idata$4', 0x4CD430, 0x4CD8A4, 4), ('.idata$5', 0x4C6000, 0x4C6474, 4),
             ('.idata$6', 0x4CD8A4, 0x4CEE58, 2)]
    for name, lo, hi, al in parts:
        c.sections.append(Sec(name, img.read(lo, hi - lo), [], F.SCN_CNT_INIT | F.SCN_ALIGN[al] | F.SCN_MEM_READ | F.SCN_MEM_WRITE))
    for i, s in enumerate(c.sections):
        c.syms.append(Sym(s.name, 0, i + 1, 0, F.CLS_STATIC, F.section_aux(s.size, 0)))
        c.syms.append(None)
    for dll, slots in orig.dlls:
        for va, name, ordn, hint in slots:
            c.add_symbol(imp_symbol(name if name else '%s_ord%d' % (dll, ordn)), va - 0x4C6000, 4)
    p = os.path.join(OUT, 'idata.obj')
    c.save(p)
    return p



# ------------------------------------------------------------------------------------------ target objects

def slice_section(o, secno):
    """A new object holding only section `secno` (1-based) of `o`. Symbols defined in other sections that this one
    refers to become undefined externals."""
    sec = o.sections[secno - 1]
    n = Coff()
    n.sections = [Sec(sec.name, sec.data, [], sec.flags, sec.nlines)]
    n.sections[0].size = sec.size
    symmap = {}
    for idx, s in enumerate(o.syms):
        if s is not None and s.sec == secno:
            symmap[idx] = len(n.syms)
            n.syms.append(Sym(s.name, s.value, 1, s.typ, s.cls, s.aux))
            n.syms.extend([None] * s.naux)
    for off, si, t in sec.relocs:
        if si not in symmap:
            s = o.syms[si]
            symmap[si] = len(n.syms)
            if s.sec > 0:                       # defined in another section: now an undefined external
                n.syms.append(Sym(s.name, 0, 0, s.typ, coffedit.CLS_EXTERNAL))
            else:
                n.syms.append(Sym(s.name, s.value, s.sec, s.typ, s.cls, s.aux))
                n.syms.extend([None] * s.naux)
        n.sections[0].relocs.append([off, symmap[si], t])
    return n


def is_section_sym(s):
    return s.cls == coffedit.CLS_STATIC and s.naux and s.name.startswith('.')


class Prepared:
    def __init__(self, orig):
        self.orig = orig
        self.objvas = load_json('objvas.json')
        self.objsym = load_json('objsym.json')
        self.objs = {}
        for name, v in self.objvas.items():
            if v:
                self.objs[name] = Coff.load(os.path.join(BUILD, 'target', name + '.obj'))
        self.add_gaps()
        self.order = sorted((min(v), k) for k, v in self.objvas.items() if v and k in self.objs)

    def add_gaps(self):
        """Text bytes that no target object covers (import-library thunks and data, unwind funclets) become
        pseudo-function sections 'gap/<va>', cut from the exe with recovered relocations. Padding-only gaps are
        left to the linker."""
        import build as B
        st = B.symtab_for_mktarget()
        namemap = {int(k, 16): v for k, v in load_json('namemap.json').items()}
        img = self.orig.img
        iv = sorted((va, va + len(self.objs[n].sections[i].data)) for n, vas in self.objvas.items() if n in self.objs
                    for i, va in enumerate(vas))
        pos, gaps = img.text_lo, []
        end = self.orig.text_end
        for lo, hi in iv + [(end, end)]:
            lo = min(lo, end)
            # INT3 padding is what LINK itself writes between sections; anything else (NOP fill, thunks, data) is kept
            if lo > pos and img.read(pos, lo - pos).strip(b'\xcc'):
                gaps.append((pos, lo))
            pos = max(pos, hi)
        self.gaps = gaps
        d = os.path.join(OUT, 'gap')
        os.makedirs(d, exist_ok=True)
        for lo, hi in gaps:
            name = 'gap/%08x' % lo
            path = os.path.join(d, '%08x.obj' % lo)
            info = mktarget.write_target_sections(path, [(lo, hi, [(0, 'gap_%08x' % lo, 2, 0x20)])], EXE, st, namemap)
            o = Coff.load(path)
            for sec in o.sections:
                sec.flags = (sec.flags & ~0x00F00000) | coffedit.SCN_ALIGN[1]
            self.objs[name] = o
            self.objvas[name] = [lo]
            self.objsym[name] = {k: v for k, v in info['name2va'].items() if not (k.startswith('$L') and len(k) >= 8)}

    def sections(self, name):
        """[(va, section index 1-based, size)] in object order."""
        o = self.objs[name]
        vas = self.objvas[name]
        assert len(vas) == len(o.sections), (name, len(vas), len(o.sections))
        return [(va, i + 1, len(o.sections[i].data)) for i, va in enumerate(vas)]

    def run(self):
        orig = self.orig
        text_lo, text_hi = orig.text_lo, orig.text_hi
        # 1. every defined, non-label symbol -> (obj, sym index, va)
        defs = []
        for name, o in self.objs.items():
            secva = {i + 1: va for i, va in enumerate(self.objvas[name])}
            for idx, s in enumerate(o.syms):
                if s is None or s.sec <= 0 or is_section_sym(s) or s.cls not in (coffedit.CLS_EXTERNAL, coffedit.CLS_STATIC):
                    continue
                if s.name.startswith('$L') and s.typ != 0x20:       # mktarget's local labels ($L<hex va>)
                    continue
                defs.append((name, idx, secva[s.sec] + s.value))
        # 2. unique external names. A name used for several VAs gets '@<va>'.
        by_name, first_sym = {}, {}          # first_sym: the COMDAT symbol = first symbol of the section in table order
        for n, idx, va in sorted(defs, key=lambda x: (x[0], x[1])):
            first_sym.setdefault((n, self.objs[n].syms[idx].sec), idx)
        for n, idx, va in defs:
            by_name.setdefault(self.objs[n].syms[idx].name, set()).add(va)
        self.leader = {}                 # (obj, section no) -> final leader name
        self.text_name = {}              # va -> canonical external name
        renames = 0
        for n, idx, va in sorted(defs, key=lambda x: x[2]):
            s = self.objs[n].syms[idx]
            is_leader = (n, s.sec) not in self.leader and idx == first_sym[(n, s.sec)]
            if len(by_name[s.name]) > 1:
                s.name = '%s@%08x' % (s.name, va)
                renames += 1
            if is_leader:
                s.cls = coffedit.CLS_EXTERNAL
                if not s.name.startswith(('?', '_')):       # /ORDER wants C names undecorated: LINK adds the '_'
                    s.name = '_' + s.name
                self.leader[(n, s.sec)] = s.name
            if s.cls == coffedit.CLS_EXTERNAL or is_leader:
                self.text_name.setdefault(va, s.name)
        self.renamed = renames
        # 3. references
        self.unresolved = {}
        data_names = {}              # va -> {name: count}
        for name, o in self.objs.items():
            osym = self.objsym.get(name, {})
            for idx, s in enumerate(o.syms):
                if s is None or s.sec != 0 or s.cls != coffedit.CLS_EXTERNAL:
                    continue
                va = osym.get(s.name)
                if va is None:
                    self.unresolved.setdefault(s.name, []).append(name)
                    continue
                if va in orig.slots:
                    dll, ent, ordn = orig.slots[va]
                    s.name = imp_symbol(ent if ent else '%s_ord%d' % (dll, ordn))
                elif text_lo <= va < text_hi:
                    if va not in self.text_name:
                        self.add_code_symbol(va)
                    s.name = self.text_name[va]
                else:
                    data_names.setdefault(va, {}).setdefault(s.name, 0)
                    data_names[va][s.name] += 1
                    s.name = '\0D%08x' % va         # placeholder, canonical name chosen below
        # 4. canonical data names
        used = set(self.text_name.values())
        self.data_name = {}
        for va, names in sorted(data_names.items()):
            cands = sorted(names.items(), key=lambda kv: (kv[0].startswith(('dat_', 'DAT_')), -kv[1], kv[0]))
            nm = cands[0][0]
            if nm in used:
                nm = '%s@%08x' % (nm, va)
            used.add(nm)
            self.data_name[va] = nm
        for o in self.objs.values():
            for s in o.syms:
                if s is not None and s.name.startswith('\0D'):
                    s.name = self.data_name[int(s.name[2:], 16)]

    def add_code_symbol(self, va):
        """A reference to code that no object defines at that address (a label inside a function): add a
        symbol to the section that contains it."""
        for name in self.objs:
            for sva, secno, size in self.sections(name):
                if sva <= va < sva + size:
                    nm = 'fn_%08x' % va
                    self.objs[name].add_symbol(nm, va - sva, secno, 0x20, coffedit.CLS_EXTERNAL)
                    self.text_name[va] = nm
                    return
        raise SystemExit('no section contains code address %08x' % va)

    def slice_interleaved(self):
        """Library objects whose sections sit between other objects' sections (COMDATs the original linker placed
        elsewhere) are cut into one object per section, so that link order = address order."""
        spans = sorted((min(v), max(v) + 1, n) for n, v in self.objvas.items() if n in self.objs and v)
        hit, reach = set(), None
        for lo, hi, n in spans:
            if reach and lo < reach[0]:
                hit.update((n, reach[1]))
            if not reach or hi > reach[0]:
                reach = (hi, n)
        hit.discard(None)
        self.sliced = sorted(hit)
        for name in self.sliced:
            o = self.objs.pop(name)
            vas = self.objvas.pop(name)
            # a static symbol that another section of the object refers to must become external
            for idx, s in enumerate(o.syms):
                if s is not None and s.cls == coffedit.CLS_STATIC and s.sec > 0 and not is_section_sym(s) \
                        and not s.name.startswith('$L') and s.typ != 0x20:
                    s.cls = coffedit.CLS_EXTERNAL
                    s.name = '%s@%08x' % (s.name, vas[s.sec - 1] + s.value)
            for i, va in enumerate(vas):
                new = slice_section(o, i + 1)
                key = '%s#%08x' % (name, va)
                self.objs[key] = new
                self.objvas[key] = [va]
                self.leader[(key, 1)] = self.leader[(name, i + 1)]
        self.order = sorted((min(v), k) for k, v in self.objvas.items() if v and k in self.objs)

    def write(self):
        d = os.path.join(OUT, 'obj')
        os.makedirs(d, exist_ok=True)
        self.paths = {}
        for name, o in self.objs.items():
            for va, sec in zip(self.objvas[name], o.sections):
                if va + len(sec.data) > self.orig.text_end:      # the last extent runs to the page end: cut at the real end
                    keep = self.orig.text_end - va
                    assert not sec.data[keep:].strip(b'\0') and all(r[0] < keep for r in sec.relocs), name
                    sec.data, sec.size = sec.data[:keep], keep
                # sections that the original linker placed at an unaligned address (static initialisers, library
                # code) get the alignment their address allows; padding is whatever the extents already contain
                al = 16
                while va % al:
                    al //= 2
                sec.flags = (sec.flags & ~0x00F00000) | coffedit.SCN_ALIGN[al]
            p = os.path.join(d, name.replace('/', '__') + '.obj')
            o.save(p)
            self.paths[name] = p
        order = []
        for name in self.objs:
            for sva, secno, size in self.sections(name):
                order.append((sva, self.leader[(name, secno)]))
        order.sort()
        self.leaders = order
        with open(os.path.join(OUT, 'order.txt'), 'w', newline='') as f:
            for _, n in order:
                f.write((n if n.startswith('?') else n[1:]) + '\n')
        with open(os.path.join(OUT, 'leaders.json'), 'w') as f:
            json.dump(order, f)


# ------------------------------------------------------------------------------------------ data stand-in

def make_rsrc_obj(orig):
    """The resources: the original's .rsrc bytes (resource data entries hold RVAs, which stay valid because the
    section lands at the same address)."""
    sec = [x for x in orig.img.pe.sections if x.Name.rstrip(b'\0') == b'.rsrc'][0]
    F = coffedit
    c = Coff()
    c.sections.append(Sec('.rsrc$01', sec.get_data()[:sec.Misc_VirtualSize], [],
                          F.SCN_CNT_INIT | F.SCN_ALIGN[4] | F.SCN_MEM_READ))
    c.syms.append(Sym('.rsrc$01', 0, 1, 0, F.CLS_STATIC, F.section_aux(c.sections[0].size, 0)))
    c.syms.append(None)
    p = os.path.join(OUT, 'rsrc.obj')
    c.save(p)
    return p


def make_standin(prep):
    img = prep.orig.img
    F = coffedit
    c = Coff()
    c.sections.append(Sec('.rdata', img.read(RDATA_LO, RDATA_HI - RDATA_LO), [],
                          F.SCN_CNT_INIT | F.SCN_ALIGN[16] | F.SCN_MEM_READ))
    c.sections.append(Sec('.data', img.read(DATA_LO, DATA_SIZE), [],
                          F.SCN_CNT_INIT | F.SCN_ALIGN[16] | F.SCN_MEM_READ | F.SCN_MEM_WRITE))
    bss = Sec('.bss', b'', [], F.SCN_CNT_UNINIT | F.SCN_ALIGN[16] | F.SCN_MEM_READ | F.SCN_MEM_WRITE)
    bss.size = BSS_SIZE
    c.sections.append(bss)
    for i, s in enumerate(c.sections):      # section symbols
        c.syms.append(Sym(s.name, 0, i + 1, 0, F.CLS_STATIC, F.section_aux(s.size, 0)))
        c.syms.append(None)
    bad = []
    for va, nm in sorted(prep.data_name.items()):
        if RDATA_LO <= va < RDATA_HI:
            c.add_symbol(nm, va - RDATA_LO, 1)
        elif DATA_LO <= va < DATA_LO + DATA_SIZE:
            c.add_symbol(nm, va - DATA_LO, 2)
        elif DATA_LO + DATA_SIZE <= va < DATA_LO + DATA_SIZE + BSS_SIZE:
            c.add_symbol(nm, va - DATA_LO - DATA_SIZE, 3)
        else:
            bad.append((va, nm))
    p = os.path.join(OUT, 'standin.obj')
    c.save(p)
    return p, bad


# ------------------------------------------------------------------------------------------ link

def do_link(prep, standin, idata, mode):
    objs = [idata, standin, make_rsrc_obj(prep.orig)] + [prep.paths[n] for _, n in prep.order]
    entry = prep.text_name[prep.orig.entry]
    entry = entry[1:] if entry.startswith('_') else entry       # LINK adds the decoration itself
    rsp = ['/NOLOGO', '/NODEFAULTLIB', '/OUT:' + os.path.join(OUT, 'lithtech.exe'),
           '/BASE:0x400000', '/FIXED', '/SUBSYSTEM:WINDOWS,4.0', '/ENTRY:' + entry,
           '/OPT:REF,NOICF', '/MAP:' + os.path.join(OUT, 'lithtech.map')]
    if USE_ORDER:
        rsp.append('/ORDER:@' + os.path.join(OUT, 'order.txt'))
    # /OPT:REF drops the import libraries' unreferenced thunks (the original has few of them); /INCLUDE keeps every
    # target function, whose callers are partly stand-in data (vtables) that carries no relocations
    rsp += EXTRA_LINK_FLAGS
    rsp += ['/INCLUDE:' + n for _, n in prep.leaders]
    rsp += ['"%s"' % o for o in objs]
    with open(os.path.join(OUT, 'link.rsp'), 'w', newline='') as f:
        f.write('\n'.join(rsp) + '\n')
    return run([LINK, '@' + os.path.join(OUT, 'link.rsp')])


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--mode', default='targets')
    ap.add_argument('--stage', default='all')
    ap.add_argument('--link-flag', action='append', default=[], help='extra LINK flag (repeatable)')
    ap.add_argument('--order', action='store_true', help='pass /ORDER (not needed: object order = address order)')
    a = ap.parse_args()
    EXTRA_LINK_FLAGS.extend(a.link_flag)
    global USE_ORDER
    USE_ORDER = a.order
    os.makedirs(OUT, exist_ok=True)
    orig = Orig()
    prep = Prepared(orig)
    print('gap objects %d (%d bytes)' % (len(prep.gaps), sum(h - l for l, h in prep.gaps)))
    prep.run()
    prep.slice_interleaved()
    print("sliced %d interleaved library objects" % len(prep.sliced))
    prep.write()
    print('objects %d, renamed %d, unresolved %d, data symbols %d' % (
        len(prep.objs), prep.renamed, len(prep.unresolved), len(prep.data_name)))
    for n, objs in list(prep.unresolved.items())[:20]:
        print('  unresolved', n, objs[:3])
    standin, bad = make_standin(prep)
    for va, nm in bad[:20]:
        print('  data symbol outside stand-in sections: %08x %s' % (va, nm))
    idata = make_idata_obj(orig)
    if a.stage == 'prep':
        return
    rc, out = do_link(prep, standin, idata, a.mode)
    print(out[-6000:])
    print('link rc', rc)


if __name__ == '__main__':
    main()
