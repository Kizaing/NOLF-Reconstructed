r"""Which CLASH / ANNOTATION CONFLICT addresses are linker ICF folds (identical COMDATs merged: several source symbols live at
one address) and which are real inconsistencies?  Read-only; prints candidate rows for config/<module>/icf.csv.

  python tools/icfscan.py [--module d3dren] [--addr <hexva> ...] [--compile]

Runs the checker with an empty ICF list, collects every address that more than one name claims, and for each name finds
its compiled function in the objects (build/<module>/base) and compares the compiled bytes (outside relocations) with the
image bytes at that address.  Verdicts per address:
  ICF        every name's compiled code is byte-identical to the image at the address, and the names are different
             functions (different class/template instance or name): one copy was kept by the linker.  Row for icf.csv.
  PROTOTYPE  the same function name declared with different signatures (e.g. only the return type, or `int` vs `unsigned`
             parameters of a FUN_<addr>): the sources disagree about one function; not an ICF fold.  Fix the sources.
  NAMES      one function under two different names (e.g. FUN_10007e36 and sb_Allocate): the sources disagree on its name.
  DATA       data declared with two types / names.
  UNVERIFIED a name has no compiled definition in any object (or its bytes differ): nothing proves identical code.
--addr adds addresses to examine that currently report no clash (e.g. COMDATs whose other copies only fold in later).
--compile compiles the stale units first (default: reads the existing objects).
Genuine rows are listed as comments: never put them in icf.csv.
"""
import contextlib
import io
import os
import re
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
import build  # noqa: E402
from coffobj import REL_SIZES, undecorate  # noqa: E402


def stem(mangled):
    """Qualified name with template arguments removed ("CMoArray::_DeleteAndDestroyArray")."""
    n = undecorate(mangled)
    for _ in range(3):
        n = re.sub(r'<[^<>]*>', '', n)
    return n.replace(' ', '')


def main(argv):
    extra = [int(argv[i + 1], 16) for i, a in enumerate(argv) if a == '--addr']
    units = build.find_units()
    if '--compile' in argv:
        for u in units:
            build.compile_unit(u)
    exe, symtab, libs = build.Exe(build.EXE), build.SymTab(), build.Libraries()
    build.load_icf = lambda path=None: {}              # report every multi-name address
    buf = io.StringIO()
    with contextlib.redirect_stdout(buf):
        results, _ = build.run_check(units, exe, symtab, False, None, libs)
    names = {}                                          # va -> set of mangled names claiming it
    for l in buf.getvalue().splitlines():
        m = re.match(r'^CLASH\s+\S+: ([0-9a-f]{8}) is (\S+) here but (\S+) elsewhere', l)
        if m:
            names.setdefault(int(m.group(1), 16), set()).update((m.group(2), m.group(3)))
        m = re.match(r'^ANNOTATION CONFLICT: \S+ (\S+) @([0-9a-f]{8}) conflicts with (\S+) @([0-9a-f]{8})', l)
        if m:
            va = int(m.group(2), 16)
            names.setdefault(va, set()).update((m.group(1), m.group(3)))
    objs = build.LAST_OBJS
    defs = {}                                           # mangled -> [(unit, obj, sym)]
    for un, o in objs.items():
        for s in o.functions():
            defs.setdefault(s.name, []).append((un, o, s))
    for va in extra:                                    # all the object symbols that fold with the annotated one
        ann = [r for r in results if r.a.va == va and r.a.symbol is not None]
        cands = {r.a.symbol.name for r in ann}
        for c in list(cands):
            st = stem(c)
            cands |= {n for n in defs if n.startswith('?') and stem(n) == st}
        names.setdefault(va, set()).update(cands)

    def verdict(va, n):
        if n not in defs:
            return 'no definition'
        out = []
        for un, o, s in defs[n]:
            sec, start, end = o.extent(s)
            base = sec.data[start:end]
            mask = bytearray(len(base))
            for off, _, typ, _ in o.relocs_in(sec, start, end):
                for k in range(REL_SIZES.get(typ, 4)):
                    if off + k < len(mask):
                        mask[off + k] = 1
            tgt = exe.read(va, len(base))
            same = all(mask[i] or base[i] == tgt[i] for i in range(len(base)))
            out.append('%s:%d bytes %s' % (un, len(base), 'identical' if same else 'DIFFERENT'))
        return '; '.join(out)

    rows = []
    for va in sorted(names):
        ns = sorted(names[va])
        isfunc = symtab.funcs.get(va) is not None and va in symtab.funcs
        vs = {n: verdict(va, n) for n in ns} if isfunc else {}
        elsewhere = [n for n in ns if 'DIFFERENT' in vs.get(n, '')]          # other code (a different element size ...): lives at another address
        if va in extra and isfunc:
            ns = [n for n in ns if n not in elsewhere]
        stems = {stem(n) for n in ns}
        defined = [n for n in ns if vs.get(n, '').strip() and vs[n] != 'no definition' and 'DIFFERENT' not in vs[n]]
        if not isfunc:
            kind = 'DATA'
        elif len(defined) < len(ns):
            # a name nobody defines is a declaration with another prototype of the function that IS defined
            kind = 'PROTOTYPE' if len(stems) == 1 else 'UNVERIFIED'
        elif len(stems) == 1 and re.match(r'^FUN_[0-9a-f]{8}', next(iter(stems))):
            kind = 'PROTOTYPE'
        elif any(re.match(r'^(FUN_|DAT_)[0-9a-f]{8}', st) for st in stems):
            kind = 'NAMES'               # an invented name next to a real one (or a second invented one) for one function
        elif len(ns) > 1:
            kind = 'ICF'                 # several defined functions, all byte-identical to the one image copy
        else:
            kind = 'UNVERIFIED'
        rows.append((va, kind, ns, vs))
    for va, kind, ns, vs in rows:
        print('%08x  %-10s %s' % (va, kind, symtab.names.get(va, '')))
        for n in ns:
            print('      %-70s %s' % (n[:70], vs.get(n, '')))
        for n in vs:
            if n not in ns:
                print('      (other code, lives elsewhere) %-40s %s' % (n[:40], vs[n]))
    print()
    print('# candidate rows for config/%s/icf.csv (addr,note):' % build.modcfg.NAME)
    for va, kind, ns, vs in rows:
        if kind == 'ICF':
            print('%08x,"%s"' % (va, ' + '.join(sorted({stem(n) for n in ns}))[:150]))
    print('# NOT ICF (reported by the checker on purpose):')
    for va, kind, ns, vs in rows:
        if kind != 'ICF':
            print('#  %08x %s: %s' % (va, kind, ' | '.join(undecorate(n, 0)[:60] for n in ns)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
