r"""lithtech.exe decomp build driver.

  python tools/build.py                 compile, check, write target objs + objdiff.json, objdiff report
  python tools/build.py check [-v] [F]  compile and check annotated functions (F = substring of unit/name/addr)
  python tools/build.py diff <F>        side-by-side disassembly for matching functions
  python tools/build.py base <obj>      rebuild one base object (objdiff's "custom make" entry point)

Source annotations (reccmp style), on the line(s) directly before a definition:
  // FUNCTION: LITHTECH 0x0044cc80 [?mangled]   function that should match byte for byte
  // STUB: LITHTECH 0x0044cc80 [?mangled]       placeholder: compiled and diffed, not expected to match
  // GLOBAL: LITHTECH 0x004def1c [?mangled]     data symbol (names relocation targets)
A unit may set compiler flags with "// FLAGS: /O2 ..." in its first 30 lines (replaces DEFAULT_OPT).
"""
import json, os, re, subprocess, sys, time

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
from coffobj import CoffObj, undecorate, REL_DIR32, REL_REL32, REL_SIZES  # noqa: E402

EXE = r'E:\AVP2Source\bin\lithtech.exe'
VC6CL = os.environ.get('VC6CL') or r'E:\AVP2Source\scripts\vc6cl.bat'   # worktrees: set VC6CL=tools\vc6cl_wt.bat
OBJDIFF = r'E:\AVP2Source\tools\objdiff\objdiff-cli.exe'
SRC, INC, BUILD = os.path.join(ROOT, 'src'), os.path.join(ROOT, 'include'), os.path.join(ROOT, 'build')
SYMBOLS_CSV = os.path.join(ROOT, 'config', 'symbols.csv')
RENAMES_CSV = os.path.join(ROOT, 'config', 'renames.csv')
SPLITS_CSV = os.path.join(ROOT, 'config', 'splits.csv')
SYMBOLS_FALLBACK =r'E:\AVP2Source\out\stage4\dump\avp2_now.tsv'
NAMEMAP_JSON = os.path.join(BUILD, 'namemap.json')
LIBRARIES_JSON = os.path.join(ROOT, 'config', 'libraries.json')     # tools/libmatch.py output
UNITS_CSV = os.path.join(ROOT, 'config', 'units.csv')   # accepted unit ranges: unit,start,end,confidence


def load_unit_ranges():
    """unit name -> (start, end) from config/units.csv (a unit's functions are [start, end))."""
    import csv
    if not os.path.exists(UNITS_CSV):
        return {}
    return {r['unit']: (int(r['start'], 16), int(r['end'], 16)) for r in csv.DictReader(open(UNITS_CSV))}
COMMON_FLAGS = ['/c', '/nologo', '/MT', '/W3', '/DWIN32', '/DNDEBUG', '/I' + INC]
DEFAULT_OPT = ['/O2']
UNASSIGNED_BLOCK = 0x10000     # target-only units for not-yet-decompiled code, per 64 KB of .text
# Same function under two names (CRT aliases): base undecorated name -> Ghidra name
ALIASES = {'stricmp': '__strcmpi', 'strcmpi': '__strcmpi', 'strnicmp': '__strnicmp', '_chkstk': '__alloca_probe'}


def same_symbol(base, gname):
    """Does base obj symbol `base` plausibly name what Ghidra calls `gname`?"""
    und = undecorate(base)
    if und in (gname, gname.split('::')[-1]) or ALIASES.get(und) == gname:
        return True
    # template spellings: CMoArray<unsigned char,class DefaultCache> vs Ghidra's CMoArray<unsigned_char,DefaultCache>;
    # VC6 mangles template functions without their arguments (BaseDelete vs BaseDelete<CUDPQuery>)
    def norm(x, strip_args):
        x = re.sub(r'\b(class|struct|enum) ', '', x).replace(' ', '').replace('_', '').strip("`'")
        return re.sub(r'<[^<>]*>', '', re.sub(r'<[^<>]*>', '', x)) if strip_args else x
    if norm(und, False) == norm(gname, False) or norm(und, True) == norm(gname, True):
        return True
    if '!' in gname and base.startswith('__imp__'):      # import slot: __imp__LoadStringA@16 vs USER32.DLL!LoadStringA
        imp = gname.split('!', 1)[1]                    # MSS32.DLL!_AIL_lock@0 keeps its decoration
        return base[len('__imp__'):].split('@')[0] == imp or base == '__imp_' + imp
    return False

ANNOT = re.compile(r'^\s*//\s*(FUNCTION|STUB|GLOBAL):\s*LITHTECH\s+0x([0-9a-fA-F]+)(?:\s+(\S+))?')
FLAGS_RE = re.compile(r'^\s*//\s*FLAGS:\s*(.+)$')


# ---------------------------------------------------------------- exe + symbol table

class Exe:
    def __init__(self, path):
        import pefile
        self.pe = pefile.PE(path, fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase

    def read(self, va, n):
        return self.pe.get_data(va - self.base, n)


class SymTab:
    """Function extents and names from Ghidra (config/symbols.csv, or the older DumpFuncs tsv)."""

    def __init__(self):
        self.funcs = {}     # va -> (end, name)
        self.names = {}     # va -> name (all kinds)
        self.by_name = {}   # name -> [va]
        if os.path.exists(SYMBOLS_CSV):
            import csv
            for r in csv.DictReader(open(SYMBOLS_CSV, encoding='utf-8')):
                va, end = int(r['addr'], 16), int(r['end'], 16)
                if r['kind'] == 'func':
                    self.funcs[va] = (end, r['name'])
                if r['kind'] in ('func', 'data', 'label', 'import'):
                    self.names.setdefault(va, r['name'])
            self.source = SYMBOLS_CSV
        else:
            rows = [l.rstrip('\n').split('\t') for l in open(SYMBOLS_FALLBACK, encoding='utf-8')][1:]
            vas = sorted((int(r[0], 16), r[1]) for r in rows if r[4] == 'false')
            for (va, name), nxt in zip(vas, vas[1:] + [(None, None)]):
                self.funcs[va] = ((nxt[0] if nxt[0] else va + 16), name)
                self.names[va] = name
            self.source = SYMBOLS_FALLBACK
        # Function extents Ghidra merged (config/splits.csv), split as mktarget does.
        import mktarget
        fl = mktarget.apply_splits(sorted((va, e, n) for va, (e, n) in self.funcs.items()), SPLITS_CSV)
        self.funcs = {va: (e, n) for va, e, n in fl}
        for va, e, n in fl:
            self.names.setdefault(va, n)
        # Names proven wrong by matched code, until they're synced back into Ghidra.
        if os.path.exists(RENAMES_CSV):
            import csv
            for r in csv.DictReader(l for l in open(RENAMES_CSV, encoding='utf-8') if not l.startswith('#')):
                va = int(r['addr'], 16)
                self.names[va] = r['name']
                if va in self.funcs:
                    self.funcs[va] = (self.funcs[va][0], r['name'])
        for va, n in self.names.items():
            self.by_name.setdefault(n, []).append(va)


# ---------------------------------------------------------------- units and annotations

class Annot:
    def __init__(self, kind, va, mangled, unit, line, name=None):
        self.kind, self.va, self.mangled, self.unit, self.line, self.name = kind, va, mangled, unit, line, name
        self.symbol = None
        self.error = None

    def where(self):
        return '%s:%d' % (self.unit.rel, self.line)


class Unit:
    def __init__(self, path):
        self.path = path
        self.rel = os.path.relpath(path, SRC).replace('\\', '/')
        self.name = os.path.splitext(self.rel)[0]
        self.base_obj = os.path.join(BUILD, 'base', self.name + '.obj')
        self.target_obj = os.path.join(BUILD, 'target', self.name + '.obj')
        self.flags, self.annots = list(DEFAULT_OPT), []
        lines = open(path, encoding='latin1').read().split('\n')
        for i, l in enumerate(lines):
            m = FLAGS_RE.match(l)
            if m and i < 30:
                self.flags = m.group(1).split()
            m = ANNOT.match(l)
            if m:
                a = Annot(m.group(1), int(m.group(2), 16), m.group(3), self, i + 1)
                a.name = _decl_name(lines, i + 1, a.kind == 'GLOBAL')
                self.annots.append(a)


def _decl_name(lines, i, is_data):
    """Qualified name declared by the first code line at/after lines[i]."""
    while i < len(lines):
        l = lines[i].split('//')[0].strip()
        i += 1
        if not l or l.startswith('#') or ANNOT.match(lines[i - 1]) or l.startswith('template'):
            continue
        if is_data:
            if '=' in l:
                l = l[:l.index('=')] + '='      # the initializer can't name it: `const float x = 0.1f;`
            m = re.findall(r'([A-Za-z_][\w:]*)\s*(?:\[[^\]]*\])*\s*(?:=|;|$)', l)      # Cls::s_X too
            m = m or re.findall(r'([A-Za-z_]\w*)\s*\(', l)[:1]      # constructor syntax: LTLink g_X(LTLink_Init);
            return m[-1] if m else None
        if '(' not in l:
            continue
        m = re.findall(r'([A-Za-z_~][\w:~]*)\s*$', l[:l.index('(')])
        return m[-1] if m else None
    return None


def header_globals():
    """(va, mangled, declared name, where) for each // GLOBAL: annotation in include/."""
    out = []
    for d, _, files in os.walk(INC):
        for f in sorted(files):
            if not f.lower().endswith('.h'):
                continue
            path = os.path.join(d, f)
            lines = open(path, encoding='latin1').read().splitlines()
            for i, l in enumerate(lines):
                m = ANNOT.match(l)
                if m and m.group(1) == 'GLOBAL':
                    out.append((int(m.group(2), 16), m.group(3), _decl_name(lines, i + 1, True),
                                '%s:%d' % (os.path.relpath(path, INC).replace(os.sep, '/'), i + 1)))
    return out


def find_units():
    units = []
    for d, _, files in os.walk(SRC):
        units += [Unit(os.path.join(d, f)) for f in sorted(files) if f.lower().endswith(('.cpp', '.c'))]
    return sorted(units, key=lambda u: u.rel)


# ---------------------------------------------------------------- compile

def _newest_header():
    t = 0
    for d, _, files in os.walk(INC):
        t = max([t] + [os.path.getmtime(os.path.join(d, f)) for f in files])
    return t


def compile_unit(u, force=False):
    os.makedirs(os.path.dirname(u.base_obj), exist_ok=True)
    if not force and os.path.exists(u.base_obj):
        t = os.path.getmtime(u.base_obj)
        if t > os.path.getmtime(u.path) and t > _newest_header():
            return True
    args = [VC6CL] + COMMON_FLAGS + u.flags + ['/Fo' + u.base_obj, u.path]
    r = subprocess.run(['cmd', '/c'] + args, capture_output=True, text=True, cwd=os.path.dirname(u.path))
    out = '\n'.join(l for l in r.stdout.splitlines() if l.strip() and l.strip() != os.path.basename(u.path))
    if out:
        print(out)
    if r.returncode != 0 or not os.path.exists(u.base_obj):
        # never check a stale object: its functions report ERROR until the unit compiles again
        if os.path.exists(u.base_obj):
            os.remove(u.base_obj)
        print('COMPILE FAILED: %s' % u.rel)
        COMPILE_FAILED.append(u.rel)
        return False
    return True


COMPILE_FAILED = []


# ---------------------------------------------------------------- check

class Result:
    def __init__(self, a):
        self.a = a
        self.status, self.detail, self.size, self.diffs = 'ERROR', '', 0, 0
        self.unverified = []    # (va, base symbol name) reloc targets not confirmed by name or content
        self.bad_relocs = []    # (offset, base name, expected va, exe va)


def bind_symbols(units, obj):
    """Attach the compiled obj symbol to each annotation (by undecorated name, or the explicit mangled name)."""
    for u in units:
        o = obj.get(u.name)
        if not o:
            continue
        for a in u.annots:
            if a.kind == 'GLOBAL':
                cands = [s for s in o.symbols.values() if not s.is_section_symbol and s.cls in (2, 3)
                         and (s.name == a.mangled if a.mangled else undecorate(s.name) == a.name)]
                names = sorted({s.name for s in cands})
                a.symbol = names[0] if len(names) == 1 else None
                a.error = None if a.symbol else 'GLOBAL %s: %d symbol candidates %s' % (a.name, len(names), names[:4])
                continue
            cands = [s for s in o.functions() if (s.name == a.mangled if a.mangled else undecorate(s.name) == a.name)]
            a.symbol = cands[0] if len(cands) == 1 else None
            a.error = None if a.symbol else '%s: %d function symbols match %r %s (add the mangled name to the annotation)' % (
                a.where(), len(cands), a.name, [c.name for c in cands][:4])


class Libraries:
    """Prebuilt library objects found in the exe by tools/libmatch.py (config/libraries.json)."""

    def __init__(self):
        self.units, self.names = [], {}
        if os.path.exists(LIBRARIES_JSON):
            j = json.load(open(LIBRARIES_JSON))
            self.units = j['units']
            self.names = {int(k, 16): v for k, v in j['names'].items()}
        for u in self.units:
            u['base_obj'] = os.path.join(BUILD, 'base', u['name'] + '.obj')
            u['target_obj'] = os.path.join(BUILD, 'target', u['name'] + '.obj')
            # code sections placed in .text (.text$x unwind funclets have no symbols for objdiff)
            u['text'] = sorted((int(va, 16), n) for va, (_, n, _, _, sec, _) in u['sections'].items() if sec == '.text')
            u['funclets'] = sorted((int(va, 16), n) for va, (_, n, _, _, sec, _) in u['sections'].items() if sec != '.text')

    def code_bytes(self):
        return sum(n for u in self.units for _, n in u['text'] + u['funclets'])


def build_namemap(units, symtab):
    """va -> mangled base name, from annotations; name -> va for external symbols; and per-unit
    name -> va for static (file-local) symbols such as VC6's _$E1 static initialisers."""
    va2name, name2va, problems = {}, {}, []
    local = {}
    for u in units:
        mine = local.setdefault(u.name, {})
        for a in u.annots:
            n = a.symbol.name if hasattr(a.symbol, 'name') else a.symbol
            if not n:
                continue
            is_static = getattr(a.symbol, 'cls', 2) == 3
            names = mine if is_static else name2va
            if va2name.get(a.va, n) != n or names.get(n, a.va) != a.va:
                problems.append('%s: %s @%08x conflicts with %s @%08x' % (a.where(), n, a.va, va2name.get(a.va), names.get(n, 0)))
            va2name[a.va], names[n] = n, a.va
    return va2name, name2va, local, problems


def literal_bytes(o, sym):
    """Data of a COMDAT literal (string / float constant) symbol defined in obj o, else None."""
    if sym.secno <= 0:
        return None
    sec = o.section_of(sym)
    return sec.data[sym.value:]


def check_function(a, o, exe, symtab, name2va, ghidra_conflicts, local=None):
    r = Result(a)
    if not a.symbol:
        r.detail = a.error
        return r
    sec, start, end = o.extent(a.symbol)
    base = sec.data[start:end]
    r.size = len(base)
    ext = symtab.funcs.get(a.va)
    exe_len = (ext[0] - a.va) if ext else None
    target = exe.read(a.va, len(base))
    relocs = o.relocs_in(sec, start, end)
    mask = bytearray(len(base))
    for off, _, typ, _ in relocs:
        for k in range(REL_SIZES.get(typ, 4)):
            if off + k < len(mask):
                mask[off + k] = 1
    diffs = [i for i in range(len(base)) if not mask[i] and base[i] != target[i]]
    r.diffs = len(diffs)
    for off, s, typ, addend in relocs:
        if typ not in (REL_DIR32, REL_REL32) or off + 4 > len(base):
            continue
        field = target[off:off + 4]
        if typ == REL_DIR32:
            tva = int.from_bytes(field, 'little') - addend
        else:
            tva = a.va + off + 4 + int.from_bytes(field, 'little', signed=True) - addend
        if s.is_section_symbol or (s.secno == a.symbol.secno):     # own section: switch tables, local labels
            expect = a.va + (s.value if not s.is_section_symbol else 0) - start
            if tva != expect:
                r.bad_relocs.append((off, s.name, expect, tva))
            continue
        known = (local or {}).get(s.name) if s.cls == 3 else name2va.get(s.name)
        if known is not None:
            if known != tva:
                r.bad_relocs.append((off, s.name, known, tva))
            continue
        lit = literal_bytes(o, s) if s.name.startswith(('??_C@', '__real@')) else None
        if lit is not None:
            try:
                got = exe.read(tva, len(lit))
            except Exception:     # target outside the image: the reloc is simply wrong
                got = None
            if got != lit:
                r.bad_relocs.append((off, s.name, None, tva))
            continue
        gname = symtab.names.get(tva)
        if gname and not gname.startswith(('FUN_', 'DAT_', 'LAB_', 'PTR_', 'switchD', 'caseD', 's_', 'u_')) \
                and not same_symbol(s.name, gname):
            ghidra_conflicts.append((a, s.name, tva, gname))
        r.unverified.append((tva, s.name))
    # the linker pads between COMDATs with int3 (0xCC) when the object's own padding is shorter (e.g. /Od units)
    pad_only = exe_len is not None and exe_len > len(base) and \
        all(b in (0xCC, 0x90) for b in exe.read(a.va + len(base), exe_len - len(base)))
    if len(base) > len(target) or (exe_len is not None and exe_len != len(base) and not pad_only):
        r.status = 'SIZE'
        r.detail = 'base %d bytes, Ghidra extent %s' % (len(base), exe_len)
        if diffs or r.bad_relocs:
            r.detail += ', %d bytes differ' % len(diffs)
    elif diffs:
        r.status, r.detail = 'DIFF', '%d/%d bytes differ (first +0x%x)' % (len(diffs), len(base), diffs[0])
    elif r.bad_relocs:
        r.status = 'RELOC'
        r.detail = ', '.join('+0x%x %s: want %s got %08x' % (off, n, ('%08x' % e) if e else 'literal', g)
                             for off, n, e, g in r.bad_relocs[:3])
    else:
        r.status = 'MATCH'
        if r.unverified:
            r.detail = '%d reloc target(s) named by this match' % len(r.unverified)
    return r


def run_check(units, exe, symtab, verbose=False, filt=None, libs=None):
    objs = {u.name: CoffObj(u.base_obj) for u in units if os.path.exists(u.base_obj)}
    bind_symbols(units, objs)
    va2name, name2va, local, problems = build_namemap(units, symtab)
    # GLOBAL annotations in include/ name the symbol in every object that references it
    for va, mangled, name, where in header_globals():
        syms = {s.name for o in objs.values() for s in o.symbols.values() if not s.is_section_symbol and s.cls == 2
                and (s.name == mangled if mangled else undecorate(s.name) == name)}
        for n in syms:
            if name2va.setdefault(n, va) != va or va2name.setdefault(va, n) != n:
                problems.append('include/%s: %s @%08x conflicts with %s @%08x' % (where, n, va, va2name.get(va), name2va.get(n, 0)))
    # library names verify engine relocations too (CRT calls, __imp__X@n import slots)
    lib_names = libs.names if libs else {}
    for va, n in lib_names.items():
        if name2va.setdefault(n, va) != va:
            problems.append('library name %s @%08x conflicts with annotation @%08x' % (n, va, name2va[n]))
    for p in problems:
        if filt is None or not p.startswith('library name'):
            print('ANNOTATION CONFLICT: ' + p)
    results, ghidra_conflicts = [], []
    for u in units:
        for a in u.annots:
            if a.kind == 'GLOBAL':
                if a.error:
                    print('%s: %s' % (a.where(), a.error))
                continue
            if u.name not in objs:
                r = Result(a)
                r.detail = 'no object: %s did not compile' % u.rel
                results.append(r)
                continue
            r = check_function(a, objs[u.name], exe, symtab, name2va, ghidra_conflicts, local.get(u.name))
            results.append(r)
    # names learned from fully matching functions name the targets of their relocations
    learned, clash, learned_at = {}, [], {}
    for r in results:
        if r.status != 'MATCH':
            continue
        for tva, n in r.unverified:
            if va2name.get(tva, n) != n or learned.get(tva, n) != n:
                clash.append((r.a, tva, n, va2name.get(tva) or learned.get(tva)))
                continue
            learned[tva] = n
            learned_at.setdefault(n, {}).setdefault(tva, r.a)
    for r in results:
        if filt and filt not in r.a.unit.name and filt not in (r.a.name or '') and filt not in '%08x' % r.a.va:
            continue
        flag = '' if r.a.kind == 'FUNCTION' else ' (stub)'
        print('%-6s %08x %5d  %-45s %s%s' % (r.status, r.a.va, r.size, (r.a.mangled or r.a.name or '?')[:45], r.detail, flag))
        if verbose and r.status in ('DIFF', 'SIZE', 'RELOC'):
            print_diff(r, objs[r.a.unit.name], exe)
    # Only byte-identical functions pair relocations reliably, and ICF-folded targets are already CLASHes.
    exact = {id(r.a) for r in results if r.status in ('MATCH', 'RELOC')}
    folded = {tva for _, tva, _, _ in clash}
    seen = set()
    for a, n, tva, g in ghidra_conflicts:
        if id(a) not in exact or tva in folded or (a.where(), n) in seen:
            continue
        seen.add((a.where(), n))
        print('NAME?  %s references %s at %08x, Ghidra calls it %s' % (a.where(), n, tva, g))
    for a, tva, n, other in clash:
        print('CLASH  %s: %08x is %s here but %s elsewhere' % (a.where(), tva, n, other))
    # one symbol learned at several addresses: a member offset or addend is wrong in one of the matches
    for n, at in sorted(learned_at.items()):
        if len(at) > 1:
            print('MULTI  %s learned at %s' % (n, ', '.join('%08x (%s)' % (v, a.where()) for v, a in sorted(at.items()))))
    namemap = dict(va2name)
    namemap.update({k: v for k, v in learned.items() if k not in namemap})
    namemap.update({k: v for k, v in lib_names.items() if k not in namemap})
    return results, namemap


def save_namemap(namemap):
    os.makedirs(BUILD, exist_ok=True)
    tmp = NAMEMAP_JSON + '.%d.tmp' % os.getpid()
    json.dump({'%08x' % k: v for k, v in sorted(namemap.items())}, open(tmp, 'w'), indent=1)
    os.replace(tmp, NAMEMAP_JSON)


def show_todo(filt, units, symtab, results):
    """Functions in the units matching filt (config/units.csv ranges), with their annotation status."""
    status = {r.a.va: r.status for r in results}
    ranges = load_unit_ranges()
    for name, (lo, hi) in sorted(ranges.items(), key=lambda x: x[1]):
        if filt not in name:
            continue
        vas = sorted(va for va in symtab.funcs if lo <= va < hi)
        done = sum(1 for va in vas if status.get(va) == 'MATCH')
        print('%s  %08x-%08x  %d functions, %d bytes, %d matching' % (name, lo, hi, len(vas), hi - lo, done))
        for va in vas:
            end, n = symtab.funcs[va]
            print('  %-6s %08x %6d  %s' % (status.get(va, '-'), va, end - va, n))


def print_diff(r, o, exe):
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    sec, start, end = o.extent(r.a.symbol)
    base = sec.data[start:end]
    ext = r.size
    target = exe.read(r.a.va, max(ext, len(base)))
    relocs = {off: s.name for off, s, _, _ in o.relocs_in(sec, start, end)}
    left = [(i.address, i.size, '%s %s' % (i.mnemonic, i.op_str)) for i in md.disasm(base, 0)]
    right = [(i.address, i.size, '%s %s' % (i.mnemonic, i.op_str)) for i in md.disasm(target, r.a.va)]
    for k in range(max(len(left), len(right))):
        la, ls, lt = left[k] if k < len(left) else (None, 0, '')
        ra, rs, rt = right[k] if k < len(right) else (None, 0, '')
        rel = [relocs[o_] for o_ in range(la, la + ls) if la is not None and o_ in relocs] if la is not None else []
        if rel and lt.split()[0] in ('call', 'jmp') or (rel and lt.startswith('j')):
            lt = lt.split()[0] + ' ' + rel[0][:40]
        elif rel:
            lt = re.sub(r'0x0\b|0x[0-9a-f]+(?=\])', rel[0][:28], lt, count=1) if '0x' in lt else lt + ' ; ' + rel[0][:28]
        same = la is not None and ra is not None and base[la:la + ls] == target[ra - r.a.va:ra - r.a.va + rs]
        mark = ' ' if same or (rel and ls == rs and lt.split()[0] == rt.split()[0]) else '*'
        print('   %s %04x %-50s | %s' % (mark, la if la is not None else 0, lt[:50], ('%04x ' % (ra - r.a.va) if ra else '') + rt[:50]))


# ---------------------------------------------------------------- targets + objdiff

def apply_library_extents(libs, exe_path):
    """Function extents for library code = the library object's function symbols: Ghidra entries
    inside a library function are dropped (catch blocks, split tails), merged ones are split."""
    import mktarget
    st = symtab_for_mktarget()
    img = mktarget._image(exe_path)
    for u in libs.units:
        fstarts = sorted(int(k, 16) for k in u['functions'])
        for sva, n in u['text']:
            inside = [va for va in fstarts if sva <= va < sva + n]
            if not inside or inside[0] != sva:
                inside.insert(0, sva)
            # past the section: alignment padding, then whatever Ghidra merged into the extent
            last = st.containing_func(inside[-1])
            p, end = sva + n, (last[1] if last else sva + n)
            while p < end and img.read(p, 1) in (b'\xcc', b'\x90'):
                p += 1
            if p < end:
                st.split_at(p)
            # each library function is exactly its object bytes (padding is not part of it)
            for va, nxt in zip(inside, inside[1:] + [sva + n]):
                st.set_body(va, nxt)
        for sva, n in u['funclets']:
            # unwind funclets: claimed (verified bytes) but not emitted; split them off their hosts
            st.split_at(sva)
            st.split_at(sva + n)


def library_funclet_vas(libs):
    st = symtab_for_mktarget()
    out = set()
    for u in libs.units:
        for sva, n in u['funclets']:
            out.update(f[0] for f in st.funcs if sva <= f[0] < sva + n)
    return out


def write_library_units(libs, namemap):
    import mktarget
    import shutil
    out_units, claimed = [], set()
    st = symtab_for_mktarget()
    stamp = max(_inputs_mtime(), os.path.getmtime(LIBRARIES_JSON), os.path.getmtime(os.path.abspath(__file__)))
    for u in libs.units:
        vas = sorted({f[0] for sva, n in u['text'] for f in st.funcs if sva <= f[0] < sva + n})
        claimed.update(vas)
        if not vas:
            continue
        os.makedirs(os.path.dirname(u['base_obj']), exist_ok=True)
        if not os.path.exists(u['base_obj']) or os.path.getmtime(u['base_obj']) < os.path.getmtime(u['obj']):
            shutil.copyfile(u['obj'], u['base_obj'])
        nm = dict(namemap)
        nm.update({int(k, 16): v for k, v in u['data'].items()})
        nm.update({int(k, 16): v for k, v in u['functions'].items()})
        os.makedirs(os.path.dirname(u['target_obj']), exist_ok=True)
        if True:     # always rewritten: SYMVA (build/symva.json, used by tools/relink.py) needs every object's names
            base = CoffObj(u['obj'])
            secs = []
            for sva, n in u['text']:
                secno, _, _, _, _, syms = u['sections']['%08x' % sva]
                offs = {off for off, _, _ in base.sections[secno - 1].relocs}
                secs.append((sva, sva + n, [tuple(x) for x in syms], offs))
            info = mktarget.write_target_sections(u['target_obj'], secs, EXE, st, nm)
            _note_names(info['name2va'], SYMVA, u['name'])
            OBJVAS[u['name']] = [sva for sva, n in u['text']]
            for bad in mktarget.verify(u['target_obj'], EXE, info['name2va']):
                print('TARGET ROUND-TRIP FAILED: ' + bad)
        out_units.append({'name': u['name'], 'target_path': rel(u['target_obj']), 'base_path': rel(u['base_obj']),
                          'metadata': {'complete': True}})
    return out_units, claimed


def write_targets(units, symtab, namemap, libs=None):
    try:
        import mktarget
    except ImportError:
        print('tools/mktarget.py not available yet: skipping target objects')
        return None
    claimed = set()
    out_units = []
    lib_units = []
    if libs and libs.units:
        apply_library_extents(libs, EXE)
        lib_units, lib_claimed = write_library_units(libs, namemap)
        claimed |= lib_claimed | library_funclet_vas(libs)
    ranges = load_unit_ranges()
    src_units = {u.name: u for u in units}
    mk_funcs = symtab_for_mktarget().func_addrs
    for name in sorted(set(ranges) | set(src_units)):
        u = src_units.get(name)
        vas = set(a.va for a in (u.annots if u else []) if a.kind in ('FUNCTION', 'STUB') and a.va in symtab.funcs)
        if name in ranges:
            lo, hi = ranges[name]
            vas |= set(va for va in mk_funcs if lo <= va < hi)
        vas -= claimed
        claimed.update(vas)
        if not vas:
            continue
        target = os.path.join(BUILD, 'target', name + '.obj')
        os.makedirs(os.path.dirname(target), exist_ok=True)
        info = mktarget.write_target_obj(target, sorted(vas), EXE, symtab_for_mktarget(), namemap)
        _note_names(info['name2va'], SYMVA, name)
        OBJVAS[name] = sorted(vas)
        for bad in mktarget.verify(target, EXE, info['name2va']):
            print('TARGET ROUND-TRIP FAILED: ' + bad)
        entry = {'name': name, 'target_path': rel(target), 'metadata': {'complete': False}}
        if u and os.path.exists(u.base_obj):
            entry['base_path'] = rel(u.base_obj)
            entry['metadata']['source_path'] = rel(u.path)
        out_units.append(entry)
    out_units += lib_units
    blocks = {}
    for va in symtab_for_mktarget().func_addrs:
        if va not in claimed:
            blocks.setdefault(va & ~(UNASSIGNED_BLOCK - 1), []).append(va)
    for blk, vas in sorted(blocks.items()):
        name = 'unassigned/%08x' % blk
        path = os.path.join(BUILD, 'target', name + '.obj')
        os.makedirs(os.path.dirname(path), exist_ok=True)
        info = mktarget.write_target_obj(path, vas, EXE, symtab_for_mktarget(), namemap)
        _note_names(info['name2va'], SYMVA, name)
        OBJVAS[name] = sorted(vas)
        out_units.append({'name': name, 'target_path': rel(path), 'metadata': {'auto_generated': True}})
    return out_units


def _note_names(name2va, into, obj=None):
    for k, v in name2va.items():
        if k.startswith('$L'):
            continue
        if into.get(k, v) != v:
            SYMCONFLICT.setdefault(k, set()).update((into[k], v))
        into[k] = v
        if obj:
            OBJSYM.setdefault(obj, {})[k] = v


OBJVAS = {}         # target object name (unit / lib/... / unassigned/...) -> section VAs in object order
OBJSYM = {}         # target object name -> {symbol name: VA} (names are unique per object, not across objects)
SYMCONFLICT = {}    # symbol name -> VAs, for names that mean different addresses in different objects
SYMVA = {}          # symbol name -> VA for every name used in a target object (not the '$L' labels)
_mk_symtab = None


def symtab_for_mktarget():
    global _mk_symtab
    if _mk_symtab is None:
        import mktarget
        _mk_symtab = mktarget.SymTab.load(SYMBOLS_CSV if os.path.exists(SYMBOLS_CSV) else SYMBOLS_FALLBACK, mktarget._image(EXE))
    return _mk_symtab


def _inputs_mtime():
    return max(os.path.getmtime(p) for p in [NAMEMAP_JSON, LIBRARIES_JSON, SYMBOLS_CSV if os.path.exists(SYMBOLS_CSV) else SYMBOLS_FALLBACK,
                                             os.path.join(TOOLS, 'mktarget.py')] if os.path.exists(p))


def rel(p):
    return os.path.relpath(p, ROOT).replace('\\', '/')


def write_objdiff_json(units_json):
    cfg = {
        '$schema': 'https://raw.githubusercontent.com/encounter/objdiff/main/config.schema.json',
        'custom_make': 'python',
        'custom_args': ['tools/build.py', 'base'],
        'build_target': False,
        'build_base': True,
        'watch_patterns': ['src/**/*.cpp', 'src/**/*.c', 'include/**/*.h'],
        'units': units_json,
    }
    json.dump(cfg, open(os.path.join(ROOT, 'objdiff.json'), 'w'), indent=2)


def objdiff_report():
    out = os.path.join(BUILD, 'report.json')
    r = subprocess.run([OBJDIFF, 'report', 'generate', '-p', ROOT, '-o', out, '-f', 'json-pretty'],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print('objdiff report failed:\n' + r.stderr[-2000:])
        return None
    rep = json.load(open(out))
    m = rep.get('measures', {})
    print('objdiff: code %s/%s bytes matched (%.2f%%), functions %s/%s matched, fuzzy %.2f%%' % (
        m.get('matched_code', 0), m.get('total_code', 0), m.get('matched_code_percent', 0),
        m.get('matched_functions', 0), m.get('total_functions', 0), m.get('fuzzy_match_percent', 0)))
    return rep


# ---------------------------------------------------------------- lint

PROTO_RE = re.compile(r'^(?:extern\s+)?(?!typedef|return|else|delete|friend|#)([A-Za-z_][\w\s\*&<>,:]*?[\s\*&])'
                      r'([A-Za-z_]\w*)\s*\(([^;{}]*)\)\s*;')
STANDIN_RE = re.compile(r'^\s*//\s*STANDIN:')


def _proto_params(p):
    p = re.sub(r'=[^,]*', '', re.sub(r'/\*.*?\*/', '', p))
    if p.strip() in ('', 'void'):
        return ()
    out = []
    for a in (x.strip() for x in p.split(',')):
        # drop the parameter name: "const LTVector &vPos" -> "const LTVector&"
        a = re.sub(r'^((?:const\s+)?(?:unsigned\s+|signed\s+)?[A-Za-z_][\w:<>]*)\s*([\*&\s]*)\s*([A-Za-z_]\w*)\s*(\[\s*\w*\s*\])?$',
                   lambda m: m.group(0) if m.group(3) in ('int', 'char', 'short', 'long') else
                   m.group(1) + m.group(2).replace(' ', '') + ('*' if m.group(4) else ''), a)
        out.append(re.sub(r'\s+', ' ', a).replace(' *', '*').replace(' &', '&').replace('size_t', 'unsigned int'))
    return tuple(out)


def lint():
    """Warnings for the problems integration kept fixing by hand. Returns the stand-in count."""
    decls, standins = {}, 0
    for root in (INC, SRC):
        for d, _, files in os.walk(root):
            for f in sorted(files):
                if not f.lower().endswith(('.h', '.cpp', '.c')):
                    continue
                path = os.path.join(d, f)
                where = os.path.relpath(path, ROOT).replace(os.sep, '/')
                data = open(path, 'rb').read()
                if b'\r\n' in data:
                    print('WARNING CRLF: %s (write files with newline=\'\' and LF line endings)' % where)
                for i, l in enumerate(data.decode('latin1').split('\n')):
                    if STANDIN_RE.match(l):
                        standins += 1
                    m = PROTO_RE.match(l)
                    if m and 'static' not in m.group(1) and 'inline' not in m.group(1):
                        ret = re.sub(r'\s+', '', m.group(1).replace('extern', ''))
                        decls.setdefault(m.group(2), []).append((ret, _proto_params(m.group(3)), '%s:%d' % (where, i + 1)))
    # a .cpp prototype that disagrees with the header's, or with another unit's (headers may overload)
    for name, ds in sorted(decls.items()):
        hdr = {(r, p) for r, p, w in ds if w.startswith('include/')}
        src = {(r, p) for r, p, w in ds if w.startswith('src/')}
        if (src - hdr) if hdr else len(src) > 1:
            print('WARNING PROTOTYPE %s declared differently: %s' % (
                name, '; '.join('%s %s(%s) at %s' % (r, name, ', '.join(p), w) for r, p, w in ds)))
    return standins


# ---------------------------------------------------------------- main

def main(argv):
    cmd = argv[0] if argv else 'all'
    if cmd == 'base':           # objdiff passes the base object path
        want = os.path.normcase(os.path.abspath(os.path.join(ROOT, argv[1])))
        if want.startswith(os.path.normcase(os.path.join(BUILD, 'base', 'lib') + os.sep)):
            return 0            # prebuilt library object: nothing to build
        for u in find_units():
            if os.path.normcase(os.path.abspath(u.base_obj)) == want:
                return 0 if compile_unit(u, force=True) else 1
        print('no unit builds ' + argv[1])
        return 1
    t0 = time.time()
    units = find_units()
    verbose = '-v' in argv
    rest = [x for x in argv[1:] if x != '-v']
    filt = rest[0] if rest else None
    # a filtered check/diff/todo only compiles the units it names (several agents may run checks at once;
    # other units' existing objects are still read for names)
    mine = [u for u in units if filt and filt in u.name] if cmd in ('check', 'diff', 'todo') else []
    if cmd in ('check', 'diff', 'todo') and filt and not mine:
        print('no unit matches %r: checking the existing objects without compiling' % filt)
    ok = all([compile_unit(u) for u in (mine or units)])
    standins = lint() if cmd in ('all', 'check') else 0
    exe, symtab = Exe(EXE), SymTab()
    libs = Libraries()
    if cmd == 'diff':
        run_check(units, exe, symtab, verbose=True, filt=filt, libs=libs)
        return 0
    results, namemap = run_check(units, exe, symtab, verbose, filt if cmd in ('check', 'todo') else None, libs)
    if cmd == 'todo':
        show_todo(filt or '', units, symtab, results)
        return 0
    n = sum(1 for r in results if r.a.kind == 'FUNCTION')
    m = sum(1 for r in results if r.a.kind == 'FUNCTION' and r.status == 'MATCH')
    mb = sum(r.size for r in results if r.a.kind == 'FUNCTION' and r.status == 'MATCH')
    total = sum(e - va for va, (e, _) in symtab.funcs.items())
    print('%d/%d annotated functions match; %d of %d .text function bytes (%.3f%%)  [symbols: %s]' % (
        m, n, mb, total, 100.0 * mb / max(total, 1), os.path.basename(symtab.source)))
    if libs.units:
        lb = libs.code_bytes()
        print('libraries: %d prebuilt objects, %d functions, %d code bytes (%.3f%%) matched by tools/libmatch.py' % (
            len(libs.units), sum(len(u['functions']) for u in libs.units), lb, 100.0 * lb / max(total, 1)))
    if standins:
        print('%d stand-ins not in lithtech.exe (// STANDIN: lines; a relink cannot contain them)' % standins)
    if cmd == 'all':
        save_namemap(namemap)
        units_json = write_targets(units, symtab, namemap, libs)
        if units_json is not None:
            write_objdiff_json(units_json)
            json.dump({k: v for k, v in sorted(SYMVA.items()) if not k.startswith('$L')},
                      open(os.path.join(BUILD, 'symva.json'), 'w'), indent=0)
            json.dump(OBJVAS, open(os.path.join(BUILD, 'objvas.json'), 'w'), indent=0)
            json.dump(OBJSYM, open(os.path.join(BUILD, 'objsym.json'), 'w'), indent=0)
            json.dump({k: sorted(v) for k, v in SYMCONFLICT.items()}, open(os.path.join(BUILD, 'symconflict.json'), 'w'), indent=0)
            objdiff_report()
    print('done in %.1fs' % (time.time() - t0))
    if COMPILE_FAILED:
        print('*** COMPILE FAILED: %s -- their functions were not checked ***' % ', '.join(COMPILE_FAILED))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
