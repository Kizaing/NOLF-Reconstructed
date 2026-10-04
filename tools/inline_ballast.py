r"""inline_ballast.py <src> <unitfilter> <addr> <anchor-text> K1,K2,... [pending]      (module: --module d3dren / DECOMP_MODULE)
Inserts an inline function with K stores (inline cost ~K) right before the first occurrence of <anchor-text> and reports the
checker status for each K: finds how much budget a STUB's original had left at that point. With 'pending', inserts K free
inline calls AFTER the anchor line instead (the calls that share the remaining budget).

Every K is compiled and checked on a PRIVATE copy (build/<module>/scratch/check/): the source file and the unit's object are
never touched.  <unitfilter> is accepted for compatibility and ignored."""
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import toolenv  # noqa: E402  (module selection, MSYS argument repair, private compiles)


def run(srcpath, addr, repls):
    s = open(srcpath, newline='', encoding='latin-1').read()
    for a, b in repls:
        assert a in s, a
        s = s.replace(a, b, 1)
    ev = toolenv.evaluate(srcpath, s, tool='inline_ballast')
    if ev.error:
        return 'COMPILE FAILED ' + ' | '.join(ev.error)[:300]
    va = int(addr, 16)
    return ev.line(va) if va in ev.rows else 'no row for %08x' % va


def main():
    if len(sys.argv) < 6:
        print(__doc__)
        return 2
    src, filt, addr, anchor, ks = sys.argv[1:6]
    mode = sys.argv[6] if len(sys.argv) > 6 else 'cost'
    path = os.path.abspath(src)
    for K in [int(k) for k in ks.split(',')]:
        if mode == 'cost':
            fn = 'static int g_bal[1000];\ninline void __ballast()\n{\n'
            fn += ''.join('\tif(0) g_bal[%d]=%d;\n' % (i, i) for i in range(K)) + '}\n'
            repl = [(anchor, '__ballast();\n\t' + anchor)]
        else:
            fn = 'inline void __pend() {}\n'
            repl = [(anchor, anchor + '\n\t' + '__pend();' * K)]
        # the helper goes before the first annotation comment
        r = run(path, addr, [('\n// ', '\n' + fn + '\n// ')] + repl if K else [])
        print(K, r[:150], flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
