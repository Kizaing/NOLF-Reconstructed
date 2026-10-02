"""inline_scan.py <src> <unitfilter> <addr> [modes]
Run from the repo root, e.g.: python tools/inline_scan.py src/server/s_client.cpp s_client 00470e10 p1,p2,b8,b16

For a STUB, insert extra inline call sites after each statement of the function body and report which
insertions give a MATCH (or reduce the diff). Modes: p1,p2 (1 or 2 free pending calls after the line),
b8,b16,b32 (ballast of ~N cost units of code-free IL before the line)."""
import sys, re, subprocess
import os
W = os.path.dirname(os.path.dirname(os.path.abspath(__file__))).replace(os.sep, '/')

HELPERS = '''
static int g_scan_bal[1000];
inline void __scan_pend() {}
inline void __scan_b8() { %s }
inline void __scan_b16() { %s }
inline void __scan_b32() { %s }
''' % tuple(''.join('if(0) g_scan_bal[%d]=%d; ' % (i, i) for i in range(n)) for n in (6, 12, 24))


def metric(out, addr):
    for l in out.splitlines():
        if addr in l and l[:6].strip() in ('MATCH', 'DIFF', 'SIZE', 'RELOC'):
            st = l.split()[0]
            m = re.search(r'(\d+)(?:/\d+)? bytes differ', l)
            return st, (0 if st == 'MATCH' else int(m.group(1)) if m else 9999), l
    return 'ERR', 99999, out[-300:]


def check(path, text, filt, addr):
    orig = open(path, newline='', encoding='latin-1').read()
    open(path, 'w', newline='', encoding='latin-1').write(text)
    try:
        out = subprocess.run(['python', 'tools/build.py', 'check', filt], cwd=W, capture_output=True, text=True).stdout
    finally:
        open(path, 'w', newline='', encoding='latin-1').write(orig)
    if 'COMPILE FAILED' in out:
        return 'FAIL', 99999, ''
    return metric(out, addr)


def main():
    src, filt, addr = sys.argv[1:4]
    modes = (sys.argv[4] if len(sys.argv) > 4 else 'p1,p2').split(',')
    path = W + '/' + src.replace(os.sep, '/')
    text = open(path, newline='', encoding='latin-1').read()
    lines = text.split('\n')
    # find annotation line
    ai = next(i for i, l in enumerate(lines) if re.match(r'// (STUB|FUNCTION): LITHTECH 0x0*' + addr.lstrip('0'), l))
    bi = next(i for i in range(ai, len(lines)) if lines[i].startswith('{'))
    ei = next(i for i in range(bi + 1, len(lines)) if lines[i].startswith('}'))
    # insert helpers before the annotation's preceding comment block
    hi = ai
    while hi > 0 and lines[hi - 1].startswith('//'):
        hi -= 1
    base = check(path, text, filt, addr)
    print('base', base[0], base[1], flush=True)
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
            st, n, info = check(path, '\n'.join(new), filt, addr)
            flag = ' <==' if st == 'MATCH' else (' (better)' if n < base[1] else '')
            if st == 'MATCH' or n < base[1]:
                print('%4d %-3s %-5s %5d  %s%s' % (li + 1, mode, st, n, s[:70], flag), flush=True)


if __name__ == '__main__':
    main()
