r"""Constrained source search for one STUB: move independent statements and commute operands, keep what improves.

  python tools/hillclimb.py <src file> <unit filter> <hex address> [--rounds N] [--ops move,commute] [--apply]

Candidates:
  move     move one simple statement to another position in its run of simple statements (same block, same
           indentation, no braces, comments, labels or control flow in between), only past statements it is
           independent of: no shared identifiers, and not past another call or a store through a pointer if it
           contains a call
  commute  swap the operands of a + b, a * b, a.Dot(b), and flip a < b to b > a (operands without calls only)
Score: MATCH first, then `build.py diff`'s ALIGNED line (instruction mismatches, ignoring stack offsets, then
exact), then differing bytes. Each candidate is compiled with `build.py check <unit filter>`.

Every candidate is compiled and checked on a PRIVATE copy (build/<module>/scratch/check/): the source file and the unit's
object are never touched while it runs, so other people may edit the unit meanwhile.  The best version is written to
<address>.best.cpp in the current directory; --apply copies it over the source at the end, but only when the file is unchanged
since the search began (otherwise it stays in <address>.best.cpp).  Works with --module d3dren / DECOMP_MODULE=d3dren.  Re-read
the result before using it: the dependency test is textual (it doesn't know about aliasing through two different pointers, or
macros with side effects).  <unit filter> is accepted for compatibility and ignored.
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import toolenv  # noqa: E402  (module selection, MSYS argument repair, private compiles)
import modcfg  # noqa: E402  (module tag LITHTECH or D3DREN; --module is consumed here)
KW = set('''int float double char short long unsigned signed bool void const static struct class enum union
    uint32 uint16 uint8 int32 int16 int8 LTBOOL DBOOL LTRESULT DRESULT LTFLOAT DFLOAT LTVector DVector LTRotation
    DRotation LTMatrix DMatrix if else for while do return break continue goto sizeof NULL LTNULL TRUE FALSE
    LTTRUE LTFALSE true false this new delete'''.split())
IDENT = re.compile(r'\b[A-Za-z_]\w*\b')
CALL = re.compile(r'\b\w+\s*\(')
CTRL = re.compile(r'^(return|break|continue|goto|for|if|while|else|do|case|default|switch|try|catch)\b')
STORE_THROUGH_PTR = re.compile(r'(->|\]|^\s*\*)[^=;]*[^=!<>]=[^=]')


def body_range(text, addr):
    m = re.search(r'// (STUB|FUNCTION): ' + modcfg.TAG + r' 0x0*%s\b' % addr, text)
    if not m:
        sys.exit('no annotation for %s' % addr)
    a = text.index('\n{', m.end()) + 2
    b = text.index('\n}\n', a) + 1
    return a, b


def simple(line):
    t = line.strip()
    return (t.endswith(';') and '{' not in t and '}' not in t and '//' not in t and '/*' not in t
            and not CTRL.match(t) and not t.startswith('#') and not re.match(r'^\w+:', t))


def idents(t):
    return set(IDENT.findall(t)) - KW


def has_call(t):
    return any(m.group(0).split('(')[0].strip() not in KW for m in CALL.finditer(t))


def independent(s, t):
    if idents(s) & idents(t):
        return False
    if has_call(s) and (has_call(t) or STORE_THROUGH_PTR.search(t)):
        return False
    if has_call(t) and STORE_THROUGH_PTR.search(s):
        return False
    return True


def move_candidates(lines):
    out = []
    i = 0
    while i < len(lines):
        if not simple(lines[i]):
            i += 1
            continue
        ind = lines[i][:len(lines[i]) - len(lines[i].lstrip())]
        j = i
        while j < len(lines) and simple(lines[j]) and lines[j].startswith(ind) and not lines[j][len(ind)].isspace():
            j += 1
        run = list(range(i, j))
        for k in run:
            for dst in run:
                if dst == k:
                    continue
                crossed = range(dst, k) if dst < k else range(k + 1, dst + 1)
                if all(independent(lines[k], lines[c]) for c in crossed):
                    nl = lines[:]
                    x = nl.pop(k)
                    nl.insert(dst, x)
                    out.append(('move line %d to %d' % (k + 1, dst + 1), nl))
        i = max(j, i + 1)
    return out


def _balanced(s, i, oc, cc):
    d = 0
    for j in range(i, len(s)):
        if s[j] == oc:
            d += 1
        elif s[j] == cc:
            d -= 1
            if d == 0:
                return j + 1
    return -1


def _operand_left(s, end):
    i = end
    while i > 0:
        c = s[i - 1]
        if c in ')]':
            j = i - 1
            d = 0
            oc = '(' if c == ')' else '['
            while j >= 0:
                if s[j] == c:
                    d += 1
                elif s[j] == oc:
                    d -= 1
                    if d == 0:
                        break
                j -= 1
            if j < 0:
                return -1
            i = j
        elif c.isalnum() or c in '_.':
            i -= 1
        elif c == '>' and i >= 2 and s[i - 2] == '-':
            i -= 2
        else:
            break
    return i if i < end else -1


def _operand_right(s, start):
    i, n = start, len(s)
    while i < n:
        c = s[i]
        if c.isalnum() or c in '_.':
            i += 1
        elif c == '-' and i + 1 < n and s[i + 1] == '>':
            i += 2
        elif c in '([':
            e = _balanced(s, i, c, ')' if c == '(' else ']')
            if e < 0:
                return -1
            i = e
        else:
            break
    return i if i > start else -1


def commute_candidates(lines):
    out = []
    for n, line in enumerate(lines):
        if line.strip().startswith(('//', '#')):
            continue
        for m in re.finditer(r'\.Dot\(', line):
            p = m.end() - 1
            e = _balanced(line, p, '(', ')')
            ls = _operand_left(line, m.start())
            if e < 0 or ls < 0:
                continue
            recv, arg = line[ls:m.start()], line[p + 1:e - 1]
            if has_call(recv) or has_call(arg) or ',' in arg:
                continue
            out.append(('line %d: %s.Dot(%s) -> %s.Dot(%s)' % (n + 1, recv, arg, arg.strip(), recv),
                        lines[:n] + [line[:ls] + '%s.Dot(%s)' % (arg.strip(), recv) + line[e:]] + lines[n + 1:]))
        for m in re.finditer(r' (<=|>=|<|>|\+|\*) ', line):
            op = m.group(1)
            ls, re_ = _operand_left(line, m.start()), _operand_right(line, m.end())
            if ls < 0 or re_ < 0:
                continue
            L, R = line[ls:m.start()], line[m.end():re_]
            if has_call(L) or has_call(R):
                continue
            before, after = line[max(ls - 3, 0):ls], line[re_:re_ + 3]
            if re.search(r'[\w\)\]]\s*[-*/+%<>]\s*$', before) or re.match(r'\s*[-*/+%]', after):
                continue    # part of a longer expression: precedence would change
            new = {'<': '>', '>': '<', '<=': '>=', '>=': '<='}.get(op, op)
            out.append(('line %d: %s %s %s -> %s %s %s' % (n + 1, L, op, R, R, new, L),
                        lines[:n] + [line[:ls] + '%s %s %s' % (R, new, L) + line[re_:]] + lines[n + 1:]))
    return out


def score(path, text, addr):
    """(score tuple, status line) of the function at `addr` in a private compile of `text` as the unit at `path`."""
    va = int(addr, 16)
    ev = toolenv.evaluate(path, text, tool='hillclimb')
    if ev.error:
        return (10 ** 9,), 'COMPILE FAILED'
    r = ev.rows.get(va)
    if r is None:
        return (10 ** 9,), 'no status line'
    line = ev.line(va)
    if r.status in ('MATCH', 'RELOC'):
        return (0, 0, 0), line
    if r.a.symbol is None:
        return (10 ** 8,), line
    n, ns, nl, nr = ev.aligned(va)
    return (ns, n, r.diffs), line


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    src, unit, addr = os.path.abspath(argv[0]), argv[1], argv[2].lower().replace('0x', '').lstrip('0')
    rounds = int(argv[argv.index('--rounds') + 1]) if '--rounds' in argv else 3
    ops = argv[argv.index('--ops') + 1].split(',') if '--ops' in argv else ['move', 'commute']
    apply = '--apply' in argv
    orig = open(src, newline='').read()
    a, b = body_range(orig, addr)
    head, tail = orig[:a], orig[b:]
    lines = orig[a:b].split('\n')[:-1]
    cur = lines

    def text_of(ls):
        return head + '\n'.join(ls) + '\n' + tail

    tried = set()
    best, bl = score(src, orig, addr)
    print('start', best, bl.strip()[:110], flush=True)
    if best == (10 ** 9,):
        print('the unmodified unit does not compile')
        return 1
    for rnd in range(rounds):
        if best[0] == 0:
            break
        cands = (move_candidates(cur) if 'move' in ops else []) + \
                (commute_candidates(cur) if 'commute' in ops else [])
        print('round %d: %d candidates' % (rnd + 1, len(cands)), flush=True)
        improved = False
        for desc, nl in cands:
            key = '\n'.join(nl)
            if key in tried:
                continue
            tried.add(key)
            sc, l = score(src, text_of(nl), addr)
            if sc < best:
                best, bl, cur, improved = sc, l, nl, True
                print('  improved', best, desc, flush=True)
                if best[0] == 0:
                    break
        if not improved:
            break
    out = '%s.best.cpp' % addr
    open(out, 'w', newline='').write(text_of(cur))
    print('best version written to', os.path.abspath(out))
    if apply:
        if open(src, newline='').read() == orig:
            open(src, 'w', newline='').write(text_of(cur))
            print('applied to', src)
        else:
            print('NOT applied: %s was changed by someone else during the search (the best version is in %s)' % (src, out))
    print('final', best, bl.strip()[:110])
    return 0


class ExternalEdit(Exception):
    pass


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
