r"""Run tools/permute.py over the parked STUBs listed in PARKED.md, several at a time.

  python tools/permute_queue.py [--first N] [--last M] [--par P] [--jobs J] [--minutes T] [--seed S] [--only addr,addr]

Rows are taken in PARKED.md order (closest first). Each function gets T minutes. Results are left in
build/permute/<address>/ (log.txt, best.cpp, match.cpp); a summary line per function is printed as it finishes.
"""
import argparse, os, re, subprocess, sys, time
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def rows():
    out = []
    for l in open(os.path.join(ROOT, 'PARKED.md'), encoding='utf-8'):
        m = re.match(r'^\| (\d+) \| `([0-9a-f]{8})` \| (.+?) \| \[([^\]:]+):(\d+)\]', l)
        if m:
            out.append((int(m.group(1)), m.group(2), m.group(3), 'src/' + m.group(4)))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--first', type=int, default=1)
    ap.add_argument('--last', type=int, default=999)
    ap.add_argument('--par', type=int, default=5)
    ap.add_argument('--jobs', type=int, default=4)
    ap.add_argument('--minutes', type=float, default=5)
    ap.add_argument('--seed', type=int, default=1)
    ap.add_argument('--only', default='')
    a = ap.parse_args()
    only = set(a.only.split(',')) if a.only else None
    todo = [r for r in rows() if a.first <= r[0] <= a.last and (not only or r[1] in only)]

    def one(r):
        n, addr, name, src = r
        if os.path.exists(os.path.join(ROOT, 'build', 'permute', addr, 'match.cpp')):
            return '%2d %s %-40s already has match.cpp' % (n, addr, name[:40])
        p = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'permute.py'), src, addr, '--iters', '1000000',
                            '--jobs', str(a.jobs), '--minutes', str(a.minutes), '--seed', str(a.seed), '--resume'],
                           capture_output=True, text=True, cwd=ROOT)
        lines = [l for l in p.stdout.splitlines() if l.strip()]
        tailmsg = lines[-1] if lines else (p.stderr.strip().splitlines() or ['?'])[-1]
        return '%2d %s %-40s %s' % (n, addr, name[:40], tailmsg[:200])

    with ThreadPoolExecutor(a.par) as ex:
        for s in ex.map(one, todo):
            print(time.strftime('%H:%M:%S'), s, flush=True)


if __name__ == '__main__':
    main()
