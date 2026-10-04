r"""inline_scan.py <src> <unitfilter> <addr> [modes]      (module: --module d3dren or DECOMP_MODULE=d3dren)
Run from anywhere, e.g.: python tools/inline_scan.py --module d3dren src/d3dren/unk/100098d0.cpp 100098d0 1000ccd9 p1,p2,b8,b16

For a STUB, insert extra inline call sites after each statement of the function body and report which
insertions give a MATCH (or reduce the diff). Modes: p1,p2 (1 or 2 free pending calls after the line),
b8,b16,b32 (ballast of ~N cost units of code-free IL before the line).

Every variant is compiled and checked on a PRIVATE copy (build/<module>/scratch/check/): the source file and the unit's
object are never touched, so nothing can be left behind in src/ if a compile fails, the tool is interrupted, or someone else
edits the unit meanwhile.  <unitfilter> is accepted for compatibility and ignored."""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import toolenv  # noqa: E402  (module selection, MSYS argument repair, private compiles)
import modcfg  # noqa: E402  (module tag LITHTECH or D3DREN)

HELPERS = '''
static int g_scan_bal[1000];
inline void __scan_pend() {}
inline void __scan_b8() { %s }
inline void __scan_b16() { %s }
inline void __scan_b32() { %s }
''' % tuple(''.join('if(0) g_scan_bal[%d]=%d; ' % (i, i) for i in range(n)) for n in (6, 12, 24))


def metric(ev, va):
    """(status, differing bytes, row text) of the function at va in a private evaluation."""
    if ev.error:
        return 'FAIL', 99999, ' '.join(ev.error)[:200]
    r = ev.rows.get(va)
    if r is None:
        return 'ERR', 99999, 'no row for %08x' % va
    return r.status, (0 if r.status == 'MATCH' else r.diffs if r.diffs else 9999), ev.line(va)


def check(path, text, va, slot='0'):
    return metric(toolenv.evaluate(path, text, slot=slot, tool='inline_scan'), va)


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    src, filt, addr = sys.argv[1:4]
    va = int(addr, 16)
    modes = (sys.argv[4] if len(sys.argv) > 4 else 'p1,p2').split(',')
    path = os.path.abspath(src)
    text = open(path, newline='', encoding='latin-1').read()
    lines = text.split('\n')
    # find annotation line
    ai = next(i for i, l in enumerate(lines) if re.match(r'// (STUB|FUNCTION): ' + modcfg.TAG + r' 0x0*' + addr.lstrip('0'), l))
    bi = next(i for i in range(ai, len(lines)) if lines[i].startswith('{'))
    ei = next(i for i in range(bi + 1, len(lines)) if lines[i].startswith('}'))
    # insert helpers before the annotation's preceding comment block
    hi = ai
    while hi > 0 and lines[hi - 1].startswith('//'):
        hi -= 1
    base = check(path, text, va)
    print('base', base[0], base[1], base[2][:100], flush=True)
    if base[0] in ('FAIL', 'ERR'):
        print('the unmodified unit does not compile / the function has no row: nothing to scan')
        return 1
    for li in range(bi + 1, ei):
        l = lines[li]
        s = l.strip()
        if not s.endswith(';') or s.startswith('//') or s.startswith('return') or s.startswith('case') or 'RETURN_ERROR' in s:
            continue
        indent = l[:len(l) - len(l.lstrip())]
        for mode in modes:
            new = list(lines)
            if mode.startswith('p'):
                new[li] = l + '\n' + indent + '__scan_pend();' * int(mode[1:])
            else:
                new[li] = indent + '__scan_b%s();\n' % mode[1:] + l
            new[hi] = HELPERS + '\n' + new[hi]
            st, n, info = check(path, '\n'.join(new), va)
            flag = ' <==' if st == 'MATCH' else (' (better)' if n < base[1] else '')
            if st == 'MATCH' or n < base[1]:
                print('%4d %-3s %-5s %5d  %s%s' % (li + 1, mode, st, n, s[:70], flag), flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(main())
