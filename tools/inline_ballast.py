"""inline_ballast.py <src> <unitfilter> <addr> <anchor-text> K1,K2,... [pending]
Run from the repo root. Inserts an inline function with K stores (inline cost ~K) right before the first
occurrence of <anchor-text> and reports the checker status for each K: finds how much budget a STUB's
original had left at that point. With 'pending', inserts K free inline calls AFTER the anchor line instead
(the calls that share the remaining budget). The source file is restored after every run."""
import os
import subprocess
import sys

W = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def run(srcrel, filt, addr, repls):
    p = os.path.join(W, srcrel)
    orig = open(p, newline='', encoding='latin-1').read()
    s = orig
    for a, b in repls:
        assert a in s, a
        s = s.replace(a, b, 1)
    open(p, 'w', newline='', encoding='latin-1').write(s)
    try:
        r = subprocess.run(['python', 'tools/build.py', 'check', filt], cwd=W, capture_output=True, text=True).stdout
    finally:
        open(p, 'w', newline='', encoding='latin-1').write(orig)
    if 'COMPILE FAILED' in r:
        return 'COMPILE FAILED ' + ' | '.join(l for l in r.splitlines() if 'error' in l)[:300]
    return ' '.join(l for l in r.splitlines() if addr in l)


def main():
    src, filt, addr, anchor, ks = sys.argv[1:6]
    mode = sys.argv[6] if len(sys.argv) > 6 else 'cost'
    for K in [int(k) for k in ks.split(',')]:
        if mode == 'cost':
            fn = 'static int g_bal[1000];\ninline void __ballast()\n{\n'
            fn += ''.join('\tif(0) g_bal[%d]=%d;\n' % (i, i) for i in range(K)) + '}\n'
            repl = [(anchor, '__ballast();\n\t' + anchor)]
        else:
            fn = 'inline void __pend() {}\n'
            repl = [(anchor, anchor + '\n\t' + '__pend();' * K)]
        # the helper goes before the first annotation comment
        r = run(src, filt, addr, [('\n// ', '\n' + fn + '\n// ')] + repl if K else [])
        print(K, r[:150], flush=True)


if __name__ == '__main__':
    main()
