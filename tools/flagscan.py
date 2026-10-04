r"""Which compiler flag set reproduces the matched functions?  (evidence for the module's default flags)

  python tools/flagscan.py [--module d3dren] <unit filter> [--variants "/O2" "/Ox" ...]

Works from Git Bash too (the MSYS-converted `/O1` is repaired); needs no extra environment (DX8INC and the compiler wrapper come from the
module).  Compiles every unit matching the filter once per flag variant into build/<module>/flagscan/<variant>/ (never into the
real base objects), checks all FUNCTION annotations against the image and prints, per variant, how many functions are
MATCH / DIFF / SIZE ...  A unit's own `// FLAGS:` line is replaced by the variant (the variant is the whole optimisation
part; the common flags /c /nologo /MT /W3 /DWIN32 /DNDEBUG /I include stay); the variant `native` keeps each unit's own flags.  Safe to run while others work.
"""
import contextlib
import collections
import io
import os
import sys

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
import toolenv  # noqa: E402  (module selection, MSYS argument repair: `/O1` from Git Bash arrives as a path)
import build  # noqa: E402
import modcfg  # noqa: E402

DEFAULT_VARIANTS = ['native', '/O2', '/Ox', '/O2 /Gy-', '/O1', '/Od', '/O2 /Ob2', '/O2 /Ob0', '/O2 /G6', '/O2 /GX', '/Ox /Gy',
                    '/Og /Oi /Ot /Oy /Ob1 /Gs', '/Ot /Og /Oy /Oi /Ob2']


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    filt = args[0]
    variants = args[args.index('--variants') + 1:] if '--variants' in args else DEFAULT_VARIANTS
    units = [u for u in build.find_units() if filt in u.name]
    if not units:
        print('no unit matches', filt)
        return 1
    exe, symtab = build.Exe(build.EXE), build.SymTab()
    libs = build.Libraries()
    print('%-34s %s' % ('variant', ' '.join('%-9s' % s for s in ('MATCH', 'DIFF', 'SIZE', 'RELOC', 'ERROR'))))
    for v in variants:
        tag = ''.join(c if c.isalnum() else '_' for c in v)
        for u in units:
            if v != 'native':           # 'native' = each unit's own `// FLAGS:` line (or the module default)
                u.flags = v.split()
            u.base_obj = os.path.join(build.BUILD, 'flagscan', tag, u.name + '.obj')
            os.makedirs(os.path.dirname(u.base_obj), exist_ok=True)
            with contextlib.redirect_stdout(io.StringIO()):
                build.compile_unit(u, force=True)
        with contextlib.redirect_stdout(io.StringIO()):
            results, _ = build.run_check(units, exe, symtab, False, None, libs)
        c = collections.Counter(r.status for r in results if r.a.kind in ('FUNCTION', 'STUB'))
        print('%-34s %s' % (v, ' '.join('%-9d' % c.get(k, 0) for k in ('MATCH', 'DIFF', 'SIZE', 'RELOC', 'ERROR'))))
        sys.stdout.flush()
    return 0


if __name__ == '__main__':
    sys.exit(main())
