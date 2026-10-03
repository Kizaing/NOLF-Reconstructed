r"""Try hand-written source variants of one or more functions and print each one's score.

  python tools/vtry.py <src file> <unit filter> <func[,func2..]> <variants.py> [--calls]

variants.py defines
  VARIANTS = [(label, old, new), ...]     old/new: a string, or lists of strings for several replacements
  PRE = [(old, new), ...]                 optional, applied to the base and every variant
Each old string must occur exactly once in the file. For every variant: `build.py check <unit filter>`, then
`build.py diff <func>` for each function: the status line and the ALIGNED score. Functions of the unit that
matched before and stop matching are listed as BROKEN. --calls also lists our call sequence for each function.

The source file is always restored. If the file is edited by someone else during the run, it stops and leaves
the file alone (the variant being tried is lost, the others are already printed).
"""
import os, re, runpy, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class ExternalEdit(Exception):
    pass


def run(*a):
    r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'build.py')] + list(a), capture_output=True,
                       text=True, cwd=ROOT)
    return r.stdout + r.stderr


def failing(chk):
    return {l.split()[1] for l in chk.splitlines()
            if re.match(r'^(DIFF|SIZE|ERROR|RELOC)\s', l) and '(stub)' not in l}


def score(unit, funcs, showcalls, before):
    chk = run('check', unit)
    if 'COMPILE FAILED' in chk:
        return 'COMPILE FAILED ' + ' '.join(l for l in chk.splitlines() if 'error' in l)[:300]
    res = []
    for func in funcs:
        out = run('diff', func)
        lines = out.splitlines()
        st = [l for l in lines if re.match(r'^(MATCH|DIFF|SIZE|RELOC|ERROR)\s', l)]
        m = re.findall(r'ALIGNED.*', out)
        r = (st[0][:62] if st else '?') + ' | ' + (m[0][8:] if m else '')
        if showcalls:
            calls = [l.split('|')[0].split('call', 1)[1].strip()[:28] for l in lines
                     if '|' in l and ' call ' in l.split('|')[0]]
            r += '\n        ' + ', '.join(calls)
        res.append(r)
    broken = failing(chk) - before
    if broken:
        res.append('BROKEN: ' + ' '.join(sorted(broken)))
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
    funcs = funcs.split(',')
    D = runpy.run_path(vfile)
    V, PRE = D['VARIANTS'], D.get('PRE', [])
    orig = open(src, newline='').read()
    last = [orig]

    def write(s):
        if open(src, newline='').read() != last[0]:
            raise ExternalEdit()
        last[0] = s
        open(src, 'w', newline='').write(s)

    before = failing(run('check', unit))
    try:
        base, err = apply(orig, [p[0] for p in PRE], [p[1] for p in PRE])
        if err:
            print('PRE:', err)
            return 1
        write(base)
        print('base:\n   ', score(unit, funcs, showcalls, before), flush=True)
        for label, olds, news in V:
            if isinstance(olds, str):
                olds, news = [olds], [news]
            s, err = apply(base, olds, news)
            if err:
                print(label, ':', err)
                continue
            write(s)
            print(label, ':\n   ', score(unit, funcs, showcalls, before), flush=True)
    except ExternalEdit:
        print('ABORTED: %s was changed by someone else during the run; left as it is now (not restored)' % src)
        return 2
    finally:
        if open(src, newline='').read() == last[0]:
            open(src, 'w', newline='').write(orig)
            run('check', unit)      # leave the object consistent with the file
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
