r"""Run tools/permute.py over a list of STUBs, several at a time, and write build/permute/summary.md.

  python tools/permute_queue.py [which] [--minutes T] [--par P] [--jobs J] [--seed S] [--ops a,b] [--fresh]

which (default: every row of PARKED.md):
  --addrs 0048bfe0,41d820       these addresses
  --list FILE                   addresses from a file (one per line; `addr [src]`, # comments)
  --stubs                       every `// STUB:` annotation under src/
  --first N --last M            PARKED.md rows N..M (the closest first)

Each function gets T minutes with J compile threads (P functions at once: P*J threads in all). A function resumes
from its build/permute/<addr>/best.cpp unless --fresh; one that already has match.cpp is skipped (--rerun runs it
again from the current source). Results stay in build/permute/<addr>/ (log.txt, best.cpp, match.cpp, result.json,
hints.txt); build/permute/summary.md is rewritten after every function with one row each: address, name, ALIGNED
of the current source, ALIGNED of the best candidate, matched, the mutations that make up the best candidate,
and the ftype hints. A match is not final: `permute.py --minimize`, permute_apply.py, `build.py check <unit>`.
"""
import argparse, glob, json, os, re, subprocess, sys, threading, time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PERM = os.path.join(ROOT, 'build', 'permute')
ANNOT = re.compile(r'^// (STUB|FUNCTION): LITHTECH 0x([0-9a-fA-F]+)')


def parked_rows():
    out = []
    for l in open(os.path.join(ROOT, 'PARKED.md'), encoding='utf-8'):
        m = re.match(r'^\| (\d+) \| `([0-9a-f]{8})` \| (.+?) \| \[([^\]:]+):(\d+)\]', l)
        if m:
            out.append((int(m.group(1)), m.group(2), m.group(3), 'src/' + m.group(4)))
    return out


_index = None


def annotation_index():
    """{addr: (kind, src)} for every annotation under src/."""
    global _index
    if _index is None:
        _index = {}
        for p in glob.glob(os.path.join(ROOT, 'src', '**', '*.cpp'), recursive=True):
            rel = os.path.relpath(p, ROOT).replace('\\', '/')
            for l in open(p, encoding='latin1'):
                m = ANNOT.match(l)
                if m:
                    _index['%08x' % int(m.group(2), 16)] = (m.group(1), rel)
    return _index


def norm_addr(a):
    return '%08x' % int(a, 16)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--addrs', default='')
    ap.add_argument('--only', default='', help=argparse.SUPPRESS)       # old name of --addrs
    ap.add_argument('--list', default='')
    ap.add_argument('--stubs', action='store_true')
    ap.add_argument('--first', type=int, default=1)
    ap.add_argument('--last', type=int, default=999)
    ap.add_argument('--par', type=int, default=3)
    ap.add_argument('--jobs', type=int, default=6)
    ap.add_argument('--minutes', type=float, default=10)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--ops', default='', help='passed to permute.py (default: all mutations and the ftype probe)')
    ap.add_argument('--fresh', action='store_true', help='start from the current source, not best.cpp')
    ap.add_argument('--rerun', action='store_true', help='run functions that already have match.cpp too')
    ap.add_argument('--summary-only', action='store_true', help='just rewrite summary.md from result.json files')
    a = ap.parse_args()

    if a.addrs or a.only or a.list:
        addrs = [x for x in (a.addrs or a.only).split(',') if x.strip()]
        srcs = {}
        if a.list:
            for l in open(a.list, encoding='utf-8'):
                l = l.split('#')[0].split()
                if l:
                    addrs.append(l[0])
                    if len(l) > 1:
                        srcs[norm_addr(l[0])] = l[1]
        idx = annotation_index()
        todo = []
        for n, x in enumerate(addrs, 1):
            ad = norm_addr(x)
            src = srcs.get(ad) or (idx.get(ad) or (None, None))[1]
            if not src:
                print('%s: no annotation found under src/, skipped' % ad)
                continue
            todo.append((n, ad, '', src))
    elif a.stubs:
        todo = [(n, ad, '', src) for n, (ad, (k, src)) in enumerate(sorted(annotation_index().items()), 1) if k == 'STUB']
    else:
        todo = [r for r in parked_rows() if a.first <= r[0] <= a.last]

    lock = threading.Lock()

    def write_summary():
        rows = []
        for n, ad, name, src in todo:
            rj = os.path.join(PERM, ad, 'result.json')
            r = json.load(open(rj)) if os.path.exists(rj) else {}
            matched = r.get('matched') or os.path.exists(os.path.join(PERM, ad, 'match.cpp'))
            hints = os.path.join(PERM, ad, 'hints.txt')
            nh = sum(1 for _ in open(hints, encoding='utf-8')) if os.path.exists(hints) else 0
            ops = ', '.join('%s %d' % (o, c) if c > 1 else o for o, c in sorted(r.get('ops', {}).items(), key=lambda x: -x[1]))
            st, be = r.get('start'), r.get('best')
            rows.append('| %s | `%s` | %s | %s | %s | %s | %s | %s | %s |' % (
                n, ad, (r.get('name') or name or '?')[:60].replace('|', '/'), src,
                st[1] if st else '', be[1] if be else '', 'MATCH' if matched else '', ops,
                ('%d (hints.txt)' % nh) if nh else ''))
        with open(os.path.join(PERM, 'summary.md'), 'w', encoding='utf-8', newline='\n') as f:
            f.write('# Permuter queue summary (%s)\n\n' % time.strftime('%Y-%m-%d %H:%M'))
            f.write('ALIGNED = instruction mismatches after alignment (`build.py diff`). "before": the current source when '
                    'the function was last run; "best": build/permute/<addr>/best.cpp. Mutations: the ones in the best '
                    'candidate\'s history (NOTES.md "The permuter"). A MATCH still needs --minimize, permute_apply.py and '
                    '`build.py check`; ftype hints point at a header member type, never at a source change.\n\n')
            f.write('| # | addr | function | source | before | best | matched | mutations in the best | ftype hints |\n')
            f.write('|---|---|---|---|---|---|---|---|---|\n')
            f.write('\n'.join(rows) + '\n')

    if a.summary_only:
        write_summary()
        print(os.path.join(PERM, 'summary.md'))
        return

    def one(r):
        n, addr, name, src = r
        if os.path.exists(os.path.join(PERM, addr, 'match.cpp')) and not a.rerun:
            msg = '%3d %s %-40s already has match.cpp' % (n, addr, name[:40])
        else:
            cmd = [sys.executable, os.path.join(ROOT, 'tools', 'permute.py'), src, addr, '--iters', '100000000',
                   '--jobs', str(a.jobs), '--minutes', str(a.minutes), '--seed', str(a.seed)]
            if not a.fresh:
                cmd.append('--resume')
            if a.ops:
                cmd += ['--ops', a.ops]
            p = subprocess.run(cmd, capture_output=True, text=True, cwd=ROOT)
            lines = [l for l in p.stdout.splitlines() if l.strip() and not l.startswith('   compile failures')]
            tailmsg = lines[-1] if lines else (p.stderr.strip().splitlines() or ['?'])[-1]
            msg = '%3d %s %-40s %s' % (n, addr, name[:40], tailmsg[:200])
        with lock:
            write_summary()
        return msg

    print('%d functions, %.0f minutes each, %d at a time x %d jobs: about %.0f minutes' % (
        len(todo), a.minutes, a.par, a.jobs, len(todo) * a.minutes / max(1, a.par)), flush=True)
    with ThreadPoolExecutor(a.par) as ex:
        for s in ex.map(one, todo):
            print(time.strftime('%H:%M:%S'), s, flush=True)
    write_summary()
    print('summary: %s' % os.path.join(PERM, 'summary.md'))


if __name__ == '__main__':
    main()
