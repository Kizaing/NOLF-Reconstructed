r"""Try hand-written source variants of one or more functions and print each one's score.   (--module d3dren / DECOMP_MODULE)

  python tools/vtry.py <src file> <unit filter> <func[,func2..]> <variants.py> [--calls]

variants.py defines
  VARIANTS = [(label, old, new), ...]     old/new: a string, or lists of strings for several replacements
  PRE = [(old, new), ...]                 optional, applied to the base and every variant
Each old string must occur exactly once in the file. For every variant the whole unit is compiled and checked on a PRIVATE
copy (build/<module>/scratch/check/): the status line and the ALIGNED score of each function (name or hex address).
Functions of the unit that matched before and stop matching are listed as BROKEN. --calls also lists our call sequence for
each function.

The source file and the unit's object are never touched (older versions edited the file in place and restored it), so other
people may edit the unit, or run `build.py check`, while this runs.  <unit filter> is accepted for compatibility and ignored.
"""
import io
import os
import re
import runpy
import sys
import contextlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import toolenv  # noqa: E402  (module selection, MSYS argument repair, private compiles)


def failing(ev):
    return {va for va, r in ev.rows.items() if r.a.kind == 'FUNCTION' and r.status in ('DIFF', 'SIZE', 'ERROR', 'RELOC')}


def pick(ev, func):
    """The first annotated function row whose name or address contains `func` (the filter semantics of build.py diff)."""
    f = func.lower().replace('0x', '')
    for va, r in ev.rows.items():
        if func in (r.a.name or '') or f in '%08x' % va:
            return va, r
    return None, None


def score(src, funcs, showcalls, before):
    ev = toolenv.evaluate(src, score.text, tool='vtry')
    if ev.error:
        return 'COMPILE FAILED ' + ' '.join(ev.error)[:300]
    import build
    res = []
    for func in funcs:
        va, r = pick(ev, func)
        if r is None:
            res.append('? | ')
            continue
        al = ''
        if r.status != 'MATCH' and r.a.symbol is not None:
            n, ns, nl, nr = ev.aligned(va)
            al = '%d instruction mismatches, %d ignoring stack offsets (%d vs %d instructions)' % (n, ns, nl, nr)
        line = (ev.line(va))[:62]
        out = line + ' | ' + al
        if showcalls and r.a.symbol is not None:
            buf = io.StringIO()
            build.ALIGNED_ONLY = False
            with contextlib.redirect_stdout(buf):
                build.print_diff(r, build.LAST_OBJS[ev.unit.name], toolenv._context()['exe'])
            calls = [l.split('|')[0].split('call', 1)[1].strip()[:28] for l in buf.getvalue().splitlines()
                     if '|' in l and ' call ' in l.split('|')[0]]
            out += '\n        ' + ', '.join(calls)
        res.append(out)
    broken = failing(ev) - before
    if broken:
        res.append('BROKEN: ' + ' '.join(sorted('%08x' % va for va in broken)))
    return '\n    '.join(res)


def apply(s, olds, news):
    for o, n in zip(olds, news):
        if s.count(o) != 1:
            return None, 'old string occurs %d times: %r' % (s.count(o), o[:60])
        s = s.replace(o, n)
    return s, None


def main(argv):
    showcalls = '--calls' in argv or 'calls' in argv[4:]
    argv = [a for a in argv if a not in ('--calls', 'calls')]
    if len(argv) != 4:
        print(__doc__)
        return 1
    src, unit, funcs, vfile = argv
    src = os.path.abspath(src)
    funcs = funcs.split(',')
    D = runpy.run_path(vfile)
    V, PRE = D['VARIANTS'], D.get('PRE', [])
    orig = open(src, newline='', encoding='latin-1').read()
    score.text = orig
    ev0 = toolenv.evaluate(src, orig, tool='vtry')
    if ev0.error:
        print('the unmodified unit does not compile:', ev0.error)
        return 1
    before = failing(ev0)
    base, err = apply(orig, [p[0] for p in PRE], [p[1] for p in PRE])
    if err:
        print('PRE:', err)
        return 1
    score.text = base
    print('base:\n   ', score(src, funcs, showcalls, before), flush=True)
    for label, olds, news in V:
        if isinstance(olds, str):
            olds, news = [olds], [news]
        s, err = apply(base, olds, news)
        if err:
            print(label, ':', err)
            continue
        score.text = s
        print(label, ':\n   ', score(src, funcs, showcalls, before), flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
