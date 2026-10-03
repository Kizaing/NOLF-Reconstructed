r"""Randomised source permuter for one STUB (in the spirit of decomp-permuter, for VC6 C++).

  python tools/permute.py <src file> <hex address> [--iters N] [--jobs J] [--seed S] [--temp T] [--ops a,b,..]

Applies random source mutations to the function's body, compiles every candidate privately (a copy of the unit
under build/permute/<address>/, never the unit's own object, so it is safe next to other checks) and scores the
function against the exe: instruction mismatches after alignment (ignoring stack offsets, then exact), then
size and differing bytes. It walks by simulated annealing from the current source.

The mutations are NOT all semantics-preserving (type changes, operand swaps, statement moves without a dependency
test). That is deliberate: the only result it reports as solved is a byte-identical function, and identical code
is identical behaviour. An improved-but-not-matching candidate must be read before it is adopted.

Output (build/permute/<address>/):
  best.cpp     the best-scoring version of the whole source file so far
  match.cpp    the first byte-identical version (the search stops); verify with `build.py check <unit>` after
               copying it over the source (relocation targets and the unit's other functions are checked there)
  log.txt      every improvement: score and the mutations that led to it

  python tools/permute.py --minimize <src file> <hex address>
reverts every hunk of match.cpp that the match does not need (writes min.cpp, prints the remaining diff).
"""
import hashlib, math, os, random, re, shutil, subprocess, sys, time

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)

# ---------------------------------------------------------------- mutations

INTS = ['int', 'uint32', 'int32', 'uint16', 'uint8', 'long', 'short', 'unsigned short', 'unsigned long', 'LTBOOL',
        'int16', 'unsigned int', 'unsigned char', 'char']
OPND = r'(?:[A-Za-z_]\w*(?:(?:->|\.)\w+|\[\w+\])*|\d[\w.]*)'
DECL = re.compile(r'^(\t+)((?:const |static |unsigned |struct |class )*[A-Za-z_][\w:]*(?:<[^;=()]*>)?)\s+([*&\s]*[A-Za-z_]\w*(?:\[\w*\])*'
                  r'(?:\s*,\s*[*&\s]*[A-Za-z_]\w*(?:\[\w*\])*)*)\s*;\s*$')
DECL_INIT = re.compile(r'^(\t+)((?:const |unsigned |struct |class )*[A-Za-z_][\w:]*(?:<[^;=()]*>)?)\s+([*&]*\s*)([A-Za-z_]\w*)\s*=\s*(.+);\s*$')
NOT_TYPE = {'return', 'delete', 'goto', 'else', 'case', 'new', 'throw', 'break', 'continue', 'if', 'while', 'for', 'do'}
CTRL = re.compile(r'^(return|break|continue|goto|for|if|while|else|do|case|default|switch)\b')


def indent(l):
    return len(l) - len(l.lstrip('\t'))


def is_simple(l):
    t = l.strip()
    return bool(t) and t.endswith(';') and '{' not in t and '}' not in t and not t.startswith(('//', '#')) \
        and not CTRL.match(t) and not re.match(r'^\w+:$', t)


def is_decl(l):
    m = DECL.match(l)
    return bool(m) and m.group(2).split()[-1] not in NOT_TYPE and '(' not in l


def block_end(L, i):
    """L[i] is a line that is just `{`: index of its closing `}` line (same indentation)."""
    ind = indent(L[i])
    for j in range(i + 1, len(L)):
        if L[j].strip() in ('}', '};') and indent(L[j]) == ind:
            return j
        if L[j].strip().startswith('}') and indent(L[j]) == ind:
            return j
    return None


def stmt_span(L, i):
    """The statement starting at line i: (i, j) with j exclusive. A braced block, a control statement with its
    body, or one simple line. None if it can't be delimited."""
    if i >= len(L):
        return None
    t = L[i].strip()
    if t == '{':
        e = block_end(L, i)
        return (i, e + 1) if e is not None else None
    if re.match(r'^(if|for|while|else)\b', t) and not t.endswith(';'):
        s = stmt_span(L, i + 1)
        if not s:
            return None
        j = s[1]
        if t.startswith('if') and j < len(L) and L[j].strip().startswith('else') and indent(L[j]) == indent(L[i]):
            if L[j].strip() == 'else':
                s2 = stmt_span(L, j + 1)
            elif not L[j].strip().endswith(';'):
                s2 = stmt_span(L, j)
            else:
                s2 = (j, j + 1)
            if not s2:
                return None
            j = s2[1]
        return (i, j)
    if t.endswith(';') and not t.startswith('}'):
        return (i, i + 1)
    return None


def runs(L):
    """Maximal runs of consecutive simple lines with the same indentation: list of (a, b)."""
    out, i = [], 0
    while i < len(L):
        if is_simple(L[i]):
            j = i
            while j < len(L) and is_simple(L[j]) and indent(L[j]) == indent(L[i]):
                j += 1
            out.append((i, j))
            i = j
        else:
            i += 1
    return out


def m_move_stmt(L, rng, ctx):
    rs = [r for r in runs(L) if r[1] - r[0] >= 2]
    if not rs:
        return None
    a, b = rng.choice(rs)
    i = rng.randrange(a, b)
    j = rng.randrange(a, b)
    if i == j:
        return None
    if rng.random() < 0.6:      # mostly neighbours
        j = i + rng.choice((-1, 1))
        if not (a <= j < b):
            return None
    x = L.pop(i)
    L.insert(j, x)
    return 'move %r %+d' % (x.strip()[:40], j - i)


def m_swap_decl(L, rng, ctx):
    ds = [i for i, l in enumerate(L) if is_decl(l) and indent(l) == 1]
    if len(ds) < 2:
        return None
    i, j = rng.sample(ds, 2)
    L[i], L[j] = L[j], L[i]
    return 'swap decl %r / %r' % (L[i].strip()[:30], L[j].strip()[:30])


def m_split_multi_decl(L, rng, ctx):
    ds = [i for i, l in enumerate(L) if is_decl(l) and ',' in DECL.match(l).group(3)]
    if not ds:
        return None
    i = rng.choice(ds)
    m = DECL.match(L[i])
    parts = [p.strip() for p in m.group(3).split(',')]
    if rng.random() < 0.5:
        rng.shuffle(parts)
        L[i] = '%s%s %s;' % (m.group(1), m.group(2), ', '.join(parts))
        return 'shuffle decl list %r' % L[i].strip()[:40]
    L[i:i + 1] = ['%s%s %s;' % (m.group(1), m.group(2), p) for p in parts]
    return 'split decl %r' % m.group(3)[:40]


def m_split_init(L, rng, ctx):
    ds = [i for i, l in enumerate(L) if DECL_INIT.match(l) and DECL_INIT.match(l).group(2).split()[-1] not in NOT_TYPE]
    if not ds:
        return None
    i = rng.choice(ds)
    m = DECL_INIT.match(L[i])
    ind, typ, ptr, name, expr = m.groups()
    if typ.startswith('const') or '&' in ptr:
        return None
    L[i:i + 1] = ['%s%s %s%s;' % (ind, typ, ptr, name), '%s%s = %s;' % (ind, name, expr)]
    if rng.random() < 0.5:      # hoist the bare declaration to the top of the function
        d = L.pop(i)
        L.insert(0, '\t' + d.strip())
    return 'split init %s' % name


def m_decl_to_use(L, rng, ctx):
    ds = [i for i, l in enumerate(L) if is_decl(l) and ',' not in l and '[' not in l]
    if not ds:
        return None
    i = rng.choice(ds)
    m = DECL.match(L[i])
    name = re.sub(r'[*&\s]', '', m.group(3))
    pat = re.compile(r'\b%s\b' % re.escape(name))
    for j in range(i + 1, len(L)):
        if pat.search(L[j]):
            break
    else:
        return None
    ma = re.match(r'^(\t+)%s\s*=\s*([^=].*);\s*$' % re.escape(name), L[j])
    d = L[i]
    if ma and rng.random() < 0.7:
        L[j] = '%s%s %s = %s;' % (ma.group(1), m.group(2), m.group(3).strip(), ma.group(2))
        del L[i]
        return 'merge decl+init %s' % name
    if j == i + 1:
        return None
    del L[i]
    L.insert(j - 1, '\t' * indent(L[j - 1]) + d.strip())
    return 'decl to first use %s' % name


def m_int_type(L, rng, ctx):
    ds = [i for i, l in enumerate(L) if (DECL.match(l) and DECL.match(l).group(2) in INTS) or
          (DECL_INIT.match(l) and DECL_INIT.match(l).group(2) in INTS)]
    if not ds:
        return None
    i = rng.choice(ds)
    m = DECL.match(L[i]) or DECL_INIT.match(L[i])
    new = rng.choice([t for t in INTS if t != m.group(2)])
    L[i] = L[i].replace(m.group(2), new, 1)
    return 'type %s -> %s: %s' % (m.group(2), new, L[i].strip()[:30])


COMM = re.compile(r'(?<=[(=,!&|?:]\s)(%s) (\+|\*|\||&|\^|==|!=|<|>|<=|>=) (%s)(?=\s*[);,?:]|\s(?:&&|\|\|))' % (OPND, OPND))
FLIP = {'<': '>', '>': '<', '<=': '>=', '>=': '<='}


def m_commute(L, rng, ctx):
    c = [(i, m) for i, l in enumerate(L) for m in COMM.finditer(l) if not l.strip().startswith('//')]
    if not c:
        return None
    i, m = rng.choice(c)
    a, op, b = m.groups()
    L[i] = L[i][:m.start()] + '%s %s %s' % (b, FLIP.get(op, op), a) + L[i][m.end():]
    return 'commute %s %s %s' % (a, op, b)


ANYBIN = re.compile(r'(%s) (\+|\*) (%s)' % (OPND, OPND))


def m_commute_any(L, rng, ctx):
    c = [(i, m) for i, l in enumerate(L) for m in ANYBIN.finditer(l) if not l.strip().startswith('//')]
    if not c:
        return None
    i, m = rng.choice(c)
    a, op, b = m.groups()
    L[i] = L[i][:m.start()] + '%s %s %s' % (b, op, a) + L[i][m.end():]
    return 'commute(any) %s %s %s' % (a, op, b)


def m_incdec(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        for m in re.finditer(r'(?<![\w+\-])(%s)(\+\+|--)(?=\s*[;)])' % OPND, l):
            c.append((i, m, 'post'))
        for m in re.finditer(r'(?<![\w+\-])(\+\+|--)(%s)(?=\s*[;)])' % OPND, l):
            c.append((i, m, 'pre'))
    if not c:
        return None
    i, m, k = rng.choice(c)
    new = (m.group(2) + m.group(1))
    L[i] = L[i][:m.start()] + new + L[i][m.end():]
    return 'incdec %s' % new


def m_compound(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        m = re.match(r'^(\t+)(%s) = \2 ([+\-*/|&]) (.+);$' % OPND, l)
        if m:
            c.append((i, '%s%s %s= %s;' % (m.group(1), m.group(2), m.group(3), m.group(4))))
        m = re.match(r'^(\t+)(%s) ([+\-*/|&])= (.+);$' % OPND, l)
        if m:
            e = m.group(4) if re.match(r'^%s$' % OPND, m.group(4)) else '(%s)' % m.group(4)
            c.append((i, '%s%s = %s %s %s;' % (m.group(1), m.group(2), m.group(2), m.group(3), e)))
            if m.group(3) in '+*|&':
                c.append((i, '%s%s = %s %s %s;' % (m.group(1), m.group(2), e, m.group(3), m.group(2))))
    if not c:
        return None
    i, new = rng.choice(c)
    L[i] = new
    return 'compound %r' % new.strip()[:40]


def m_if_invert(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        t = l.strip()
        m = re.match(r'^if\s*\((.*)\)$', t)
        if not m:
            continue
        s1 = stmt_span(L, i + 1)
        if not s1 or s1[1] >= len(L) or L[s1[1]].strip() != 'else' or indent(L[s1[1]]) != indent(l):
            continue
        s2 = stmt_span(L, s1[1] + 1)
        if s2:
            c.append((i, m.group(1), s1, s2))
    if not c:
        return None
    i, cond, s1, s2 = rng.choice(c)
    ind = '\t' * indent(L[i])
    if cond.startswith('!(') and cond.endswith(')') and cond.count('(') == 1:
        ncond = cond[2:-1]
    elif re.match(r'^!%s$' % OPND, cond):
        ncond = cond[1:]
    elif re.match(r'^%s$' % OPND, cond):
        ncond = '!' + cond
    else:
        ncond = '!(%s)' % cond
    new = [ind + 'if (%s)' % ncond] + L[s2[0]:s2[1]] + [ind + 'else'] + L[s1[0]:s1[1]]
    L[i:s2[1]] = new
    return 'invert if (%s)' % cond[:30]


CMPNEG = {'<': '>=', '>': '<=', '<=': '>', '>=': '<', '==': '!=', '!=': '=='}


def m_cmp_form(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        if not re.match(r'^\s*(if|while|else if)\b', l.strip()) and '?' not in l:
            continue
        for m in re.finditer(r'(?<=[(|&]\s|.\()(%s) (<=|>=|<|>|==|!=) (%s)(?=\)|\s(?:&&|\|\|))' % (OPND, OPND), l):
            c.append((i, m))
        for m in re.finditer(r'!\((%s) (<=|>=|<|>|==|!=) (%s)\)' % (OPND, OPND), l):
            c.append((i, m))
    if not c:
        return None
    i, m = rng.choice(c)
    a, op, b = m.groups()
    if m.group(0).startswith('!('):
        new = '%s %s %s' % (a, CMPNEG[op], b)
    else:
        new = '!(%s %s %s)' % (a, CMPNEG[op], b)
    L[i] = L[i][:m.start()] + new + L[i][m.end():]
    return 'cmp form %s' % new


def m_truth_form(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        if not re.match(r'^(if|while|else if)\b', l.strip()):
            continue
        for m in re.finditer(r'(?<=[(|&]\s|.\()(!?)(%s)(?=\)|\s(?:&&|\|\|))' % OPND, l):
            if m.group(2) not in ('LTTRUE', 'LTFALSE', 'TRUE', 'FALSE') and not m.group(2)[0].isdigit():
                c.append((i, m, 'bare'))
        for m in re.finditer(r'(%s) (==|!=) (0|LTNULL|NULL|LTFALSE|FALSE)\b' % OPND, l):
            c.append((i, m, 'cmp'))
    if not c:
        return None
    i, m, k = rng.choice(c)
    if k == 'bare':
        z = rng.choice(['0', 'LTNULL'])
        new = '%s %s %s' % (m.group(2), '==' if m.group(1) else '!=', z)
    else:
        new = ('!' if m.group(2) == '==' else '') + m.group(1)
    L[i] = L[i][:m.start()] + new + L[i][m.end():]
    return 'truth form %s' % new


def m_ternary(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        m = re.match(r'^(\t+)(.+?) = (.+?) \? (.+?) : (.+);$', l)
        if m and '?' not in m.group(4) + m.group(5):
            c.append(('t', i, m))
        m = re.match(r'^(\t+)if\s*\((.*)\)$', l)
        if m and i + 3 < len(L) and L[i + 2].strip() == 'else':
            a = re.match(r'^\t+(.+?) = (.+);$', L[i + 1])
            b = re.match(r'^\t+(.+?) = (.+);$', L[i + 3])
            if a and b and a.group(1) == b.group(1):
                c.append(('i', i, (m, a, b)))
    if not c:
        return None
    k, i, m = rng.choice(c)
    if k == 't':
        ind, lhs, cond, a, b = m.groups()
        if rng.random() < 0.3:
            L[i:i + 1] = [ind + '%s = %s;' % (lhs, b), ind + 'if (%s)' % cond, ind + '\t%s = %s;' % (lhs, a)]
        else:
            L[i:i + 1] = [ind + 'if (%s)' % cond, ind + '\t%s = %s;' % (lhs, a), ind + 'else', ind + '\t%s = %s;' % (lhs, b)]
        return 'ternary -> if: %s' % lhs[:30]
    mi, a, b = m
    L[i:i + 4] = ['%s%s = (%s) ? %s : %s;' % (mi.group(1), a.group(1), mi.group(2), a.group(2), b.group(2))]
    return 'if -> ternary: %s' % a.group(1)[:30]


def m_block_wrap(L, rng, ctx):
    rs = runs(L)
    if not rs:
        return None
    a, b = rng.choice(rs)
    i = rng.randrange(a, b)
    j = min(b, i + rng.randint(1, 4))
    ind = '\t' * indent(L[i])
    L[i:j] = [ind + '{'] + ['\t' + x for x in L[i:j]] + [ind + '}']
    return 'wrap block %d lines at %r' % (j - i, L[i + 1].strip()[:30])


def m_block_unwrap(L, rng, ctx):
    c = [i for i, l in enumerate(L) if l.strip() == '{' and i > 0 and
         (L[i - 1].strip().endswith((';', '}', '{')) or not L[i - 1].strip())]
    if not c:
        return None
    i = rng.choice(c)
    e = block_end(L, i)
    if e is None:
        return None
    L[i:e + 1] = [x[1:] if x.startswith('\t') else x for x in L[i + 1:e]]
    return 'unwrap block'


def m_param_copy(L, rng, ctx):
    ps = ctx['params']
    if not ps:
        return None
    typ, name = rng.choice(ps)
    new = rng.choice(['p' + name[0].upper() + name[1:] + '2', name + 'Local', 'tmp' + name[0].upper() + name[1:]])
    pat = re.compile(r'\b%s\b' % re.escape(name))
    if not any(pat.search(l) for l in L):
        return None
    start = 0
    while start < len(L) and (is_decl(L[start]) or not L[start].strip() or L[start].strip().startswith('//')):
        start += 1
    if rng.random() < 0.4:      # copy later: from a random use on
        uses = [i for i in range(start, len(L)) if pat.search(L[i]) and indent(L[i]) == 1]
        if uses:
            start = rng.choice(uses)
    for i in range(start, len(L)):
        L[i] = pat.sub(new, L[i])
    L.insert(start, '\t%s %s = %s;' % (typ, new, name))
    return 'copy param %s' % name


def m_local_split(L, rng, ctx):
    ds = [i for i, l in enumerate(L) if is_decl(l) and ',' not in l and '[' not in l and indent(l) == 1]
    if not ds:
        return None
    i = rng.choice(ds)
    m = DECL.match(L[i])
    name = re.sub(r'[*&\s]', '', m.group(3))
    ptr = m.group(3).replace(name, '').strip()
    pat = re.compile(r'\b%s\b' % re.escape(name))
    uses = [j for j in range(i + 1, len(L)) if pat.search(L[j]) and indent(L[j]) == 1]
    if len(uses) < 2:
        return None
    j = rng.choice(uses[1:])
    new = name + '2'
    for k in range(j, len(L)):
        L[k] = pat.sub(new, L[k])
    L.insert(j, '\t%s %s%s = %s;' % (m.group(2), ptr, new, name))
    return 'split local %s at %r' % (name, L[j + 1].strip()[:30])


def m_loop_form(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        t = l.strip()
        if re.match(r'^for\s*\((.*);(.*);(.*)\)$', t) or re.match(r'^while\s*\((.*)\)$', t):
            if i + 1 < len(L) and L[i + 1].strip() == '{' and block_end(L, i + 1) is not None:
                c.append(i)
    if not c:
        return None
    i = rng.choice(c)
    e = block_end(L, i + 1)
    ind = '\t' * indent(L[i])
    t = L[i].strip()
    body = L[i + 2:e]
    has_continue = any(re.search(r'\bcontinue\b', x) for x in body)
    m = re.match(r'^for\s*\((.*);(.*);(.*)\)$', t)
    if m:
        init, cond, inc = (x.strip() for x in m.groups())
        if has_continue or not cond:
            return None
        k = rng.randrange(3)
        pre = [ind + init + ';'] if init else []
        tail = [ind + '\t' + inc + ';'] if inc else []
        if k == 0:
            new = pre + [ind + 'while (%s)' % cond, ind + '{'] + body + tail + [ind + '}']
        elif k == 1:
            new = pre + [ind + 'for (;;)', ind + '{', ind + '\tif (!(%s))' % cond, ind + '\t\tbreak;'] + body + tail + [ind + '}']
        else:
            new = pre + [ind + 'if (%s)' % cond, ind + '{', ind + '\tdo', ind + '\t{'] + ['\t' + x for x in body + tail] + \
                [ind + '\t} while (%s);' % cond, ind + '}']
        L[i:e + 1] = new
        return 'for -> form %d' % k
    m = re.match(r'^while\s*\((.*)\)$', t)
    cond = m.group(1)
    k = rng.randrange(3)
    if k == 0:
        new = [ind + 'for (;;)', ind + '{', ind + '\tif (!(%s))' % cond, ind + '\t\tbreak;'] + body + [ind + '}']
    elif k == 1:
        new = [ind + 'do', ind + '{', ind + '\tif (!(%s))' % cond, ind + '\t\tbreak;'] + body + [ind + '} while (1);']
    else:
        if has_continue:
            return None
        new = [ind + 'if (%s)' % cond, ind + '{', ind + '\tdo', ind + '\t{'] + ['\t' + x for x in body] + \
            [ind + '\t} while (%s);' % cond, ind + '}']
    L[i:e + 1] = new
    return 'while -> form %d' % k


def m_index_form(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        for m in re.finditer(r'&(%s)\[(\w+)\]' % r'[A-Za-z_]\w*(?:(?:->|\.)\w+)*', l):
            c.append((i, m, '(%s + %s)' % (m.group(1), m.group(2))))
        for m in re.finditer(r'\((%s) \+ (\w+)\)' % r'[A-Za-z_]\w*(?:(?:->|\.)\w+)*', l):
            c.append((i, m, '&%s[%s]' % (m.group(1), m.group(2))))
    if not c:
        return None
    i, m, new = rng.choice(c)
    L[i] = L[i][:m.start()] + new + L[i][m.end():]
    return 'index form %s' % new


def m_chain_assign(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        m = re.match(r'^(\t+)(%s) = (%s) = ([^=;]+);$' % (OPND, OPND), l)
        if m:
            c.append((i, [m.group(1) + '%s = %s;' % (m.group(3), m.group(4)), m.group(1) + '%s = %s;' % (m.group(2), m.group(4))], 1))
        m = re.match(r'^(\t+)(%s) = ([^=;]+);$' % OPND, l)
        if m and i + 1 < len(L):
            m2 = re.match(r'^(\t+)(%s) = ([^=;]+);$' % OPND, L[i + 1])
            if m2 and m2.group(3) == m.group(3) and m2.group(1) == m.group(1):
                c.append((i, [m.group(1) + '%s = %s = %s;' % (m2.group(2), m.group(2), m.group(3))], 2))
    if not c:
        return None
    i, new, n = rng.choice(c)
    L[i:i + n] = new
    return 'chain assign %r' % new[0].strip()[:40]


def m_swap_stmts_any(L, rng, ctx):
    """Swap two adjacent whole statements (control statements with their bodies included)."""
    c = []
    for i in range(len(L)):
        s1 = stmt_span(L, i)
        if not s1 or (i > 0 and re.match(r'^(if|for|while|else|do)\b', L[i - 1].strip()) and not L[i - 1].strip().endswith(';')):
            continue
        j = s1[1]
        while j < len(L) and not L[j].strip():
            j += 1
        s2 = stmt_span(L, j)
        if s2 and j < len(L) and indent(L[j]) == indent(L[i]) and not L[j].strip().startswith('else') \
                and not is_decl(L[i]) and not is_decl(L[j]):
            c.append((s1, (j, s2[1])))
    if not c:
        return None
    s1, s2 = rng.choice(c)
    L[s1[0]:s2[1]] = L[s2[0]:s2[1]] + L[s1[1]:s2[0]] + L[s1[0]:s1[1]]
    return 'swap statements at %r' % L[s1[0]].strip()[:30]


def m_early_return(L, rng, ctx):
    """`if (c) { A } [rest]` <-> guard forms are too varied to do textually; this one only toggles
    `if (c) return X;` between one line and a braced body (affects nothing in VC6) - kept as a no-op slot."""
    return None


def m_assoc(L, rng, ctx):
    c = [(i, m) for i, l in enumerate(L) for m in re.finditer(r'(%s) ([+*]) (%s) \2 (%s)' % (OPND, OPND, OPND), l)]
    if not c:
        return None
    i, m = rng.choice(c)
    a, op, b, d = m.groups()
    new = rng.choice(['%s %s (%s %s %s)' % (a, op, b, op, d), '(%s %s %s) %s %s' % (a, op, d, op, b),
                      '%s %s %s %s %s' % (b, op, a, op, d), '%s %s %s %s %s' % (a, op, d, op, b)])
    L[i] = L[i][:m.start()] + new + L[i][m.end():]
    return 'assoc %s' % new


def m_decl_scope(L, rng, ctx):
    """Move a top-level declaration into the innermost block that holds all its uses."""
    ds = [i for i, l in enumerate(L) if is_decl(l) and ',' not in l and indent(l) == 1]
    if not ds:
        return None
    i = rng.choice(ds)
    m = DECL.match(L[i])
    name = re.sub(r'[*&\s\[\]\w]*?(\w+)(\[\w*\])*$', r'\1', m.group(3).strip())
    pat = re.compile(r'\b%s\b' % re.escape(name))
    uses = [j for j in range(len(L)) if j != i and pat.search(L[j])]
    if not uses or min(indent(L[j]) for j in uses) < 2:
        return None
    first = uses[0]
    k = first
    while k > 0 and not (L[k].strip() == '{' and indent(L[k]) < indent(L[first]) and (block_end(L, k) or 0) >= uses[-1]):
        k -= 1
    if k <= 0:
        return None
    d = L[i].strip()
    L.insert(k + 1, '\t' * (indent(L[k]) + 1) + d)
    del L[i if i < k else i + 1]
    return 'decl into block: %s' % name


MEMB = r'[A-Za-z_]\w*(?:(?:->|\.)\w+(?:\(\))?|\[\w+\])*'
# project idioms that are the same operation written two ways (README: accessors vs members, SDK macros vs operators)
IDIOMS = [
    (r'&(%s)->m_Message\b' % MEMB, r'\1->GetMessageImpl()'), (r'(%s)->GetMessageImpl\(\)' % MEMB, r'&\1->m_Message'),
    (r'VEC_INIT\((%s)\);' % MEMB, r'\1.Init();'), (r'(%s)\.Init\(\);' % MEMB, r'VEC_INIT(\1);'),
    (r'VEC_COPY\((%s), (%s)\);' % (MEMB, MEMB), r'\1 = \2;'),
    (r'^(\t+)(%s) = (%s);$' % (MEMB, MEMB), r'\1VEC_COPY(\2, \3);'),
    (r'(%s)\.Dot\((%s)\)' % (MEMB, MEMB), r'VEC_DOT(\1, \2)'), (r'VEC_DOT\((%s), (%s)\)' % (MEMB, MEMB), r'\1.Dot(\2)'),
    (r'(%s)\.Dot\((%s)\)' % (MEMB, MEMB), r'\2.Dot(\1)'),
    (r'VEC_SUB\((%s), (%s), (%s)\);' % (MEMB, MEMB, MEMB), r'\1 = \2 - \3;'),
    (r'VEC_ADD\((%s), (%s), (%s)\);' % (MEMB, MEMB, MEMB), r'\1 = \2 + \3;'),
    (r'^(\t+)(%s) = (%s) - (%s);$' % (MEMB, MEMB, MEMB), r'\1VEC_SUB(\2, \3, \4);'),
    (r'^(\t+)(%s) = (%s) \+ (%s);$' % (MEMB, MEMB, MEMB), r'\1VEC_ADD(\2, \3, \4);'),
    (r'->GetPos\(\)', r'->m_Pos'), (r'->m_Pos\b', r'->GetPos()'),
    (r'->GetDims\(\)', r'->m_Dims'), (r'->m_Dims\b', r'->GetDims()'),
    (r'\.GetSize\(\)', r'.m_nElements'), (r'\.m_nElements\b', r'.GetSize()'),
    (r'&(%s)\[0\]' % MEMB, r'\1.GetArray()'), (r'(%s)\.GetArray\(\)' % MEMB, r'&\1[0]'),
    (r'\(LTBOOL\)', ''), (r'\b0\.0f\b', '0'), (r'\bLTNULL\b', '0'),
    (r'(%s)\.MagSqr\(\)' % MEMB, r'VEC_MAGSQR(\1)'), (r'VEC_MAGSQR\((%s)\)' % MEMB, r'\1.MagSqr()'),
    (r'(%s)\.Mag\(\)' % MEMB, r'VEC_MAG(\1)'), (r'VEC_MAG\((%s)\)' % MEMB, r'\1.Mag()'),
    (r'(%s) \*= (%s);' % (MEMB, MEMB), r'VEC_MULSCALAR(\1, \1, \2);'),
    (r'LTMIN\(([^(),]+), ([^(),]+)\)', r'LTMIN(\2, \1)'), (r'LTMAX\(([^(),]+), ([^(),]+)\)', r'LTMAX(\2, \1)'),
]
IDIOMS = [(re.compile(a, re.M), b) for a, b in IDIOMS]


def m_idiom(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        if l.strip().startswith('//'):
            continue
        for k, (pat, rep) in enumerate(IDIOMS):
            for m in pat.finditer(l):
                c.append((i, k, m))
    if not c:
        return None
    i, k, m = rng.choice(c)
    pat, rep = IDIOMS[k]
    new = m.expand(rep)
    L[i] = L[i][:m.start()] + new + L[i][m.end():]
    return 'idiom %r -> %r' % (m.group(0)[:30], new.strip()[:30])


FLOATISH = re.compile(r'(?<![\w.>])((?:[A-Za-z_]\w*(?:->|\.))+(?:x|y|z|m_f\w+|m_Dist|m_Radius)|[A-Za-z_]\w*\[\w+\]\.(?:x|y|z))\b(?!\s*(?:=[^=]|\+=|-=|\*=|/=|\())')
VECISH = re.compile(r'(?<![\w.>&])((?:[A-Za-z_]\w*(?:->|\.))+(?:m_Normal|m_Pos|m_Dims|m_v[A-Z]\w*|m_Velocity|m_Scale)|[A-Za-z_]\w*->GetPos\(\))(?![\w.(]|\s*(?:=[^=]|\+=|-=|\*=|/=))')


def enclosing_block_start(L, i):
    """Index of the first line of the innermost block containing line i (0 for the function body)."""
    ind = indent(L[i])
    for k in range(i - 1, -1, -1):
        if L[k].strip() == '{' and indent(L[k]) < ind:
            return k + 1
    return 0


def m_named_temp(L, rng, ctx):
    """Name a float / vector operand as a local declared earlier in the same block (first-reference order,
    register choice) and use it from there on in that block."""
    c = []
    for i, l in enumerate(L):
        if l.strip().startswith('//') or is_decl(l):
            continue
        for m in FLOATISH.finditer(l):
            c.append((i, m.group(1), 'float'))
        for m in VECISH.finditer(l):
            c.append((i, m.group(1), 'vec'))
    if not c:
        return None
    i, expr, kind = rng.choice(c)
    b0 = enclosing_block_start(L, i)
    # candidate insertion points: statement starts in the same block, at the same indentation, before line i
    pts = [k for k in range(b0, i + 1) if indent(L[k]) == indent(L[i]) and L[k].strip() and
           not L[k].strip().startswith(('else', '{', '}', '//')) and
           not (k > 0 and re.match(r'^(if|for|while|else|do)\b', L[k - 1].strip()) and not L[k - 1].strip().endswith(';'))]
    if not pts:
        return None
    at = rng.choice(pts) if rng.random() < 0.5 else pts[-1]
    n = ctx.setdefault('ntemp', 0)
    ctx['ntemp'] = n + 1
    ind = '\t' * indent(L[i])
    if kind == 'float':
        name = 'fTmp%d' % n
        decl = '%sfloat %s = %s;' % (ind, name, expr)
        use = name
    else:
        form = rng.randrange(3)
        name = ('vTmp%d' if form == 0 else 'pVec%d' if form == 1 else 'vRef%d') % n
        decl = ind + ('LTVector %s = %s;' if form == 0 else 'LTVector *%s = &%s;' if form == 1 else 'LTVector &%s = %s;') % (name, expr)
        use = name if form != 1 else '(*%s)' % name
        if form == 1 and expr.endswith('()'):
            return None
    pat = re.compile(r'(?<![\w.>])' + re.escape(expr) + r'(?![\w(])')
    end = len(L)
    for k in range(i, len(L)):
        if indent(L[k]) < indent(L[i]) and L[k].strip().startswith('}'):
            end = k
            break
    stop = i + 1 if rng.random() < 0.3 else end
    for k in range(at, stop):
        if not re.search(re.escape(expr) + r'\s*(=[^=]|\+=|-=|\*=|/=|\+\+|--)', L[k]):
            L[k] = pat.sub(use, L[k])
    L.insert(at, decl)
    return 'named temp %s = %s' % (name, expr)


def m_dead_local(L, rng, ctx):
    """Add or remove an unused local (frame size / slot layout: README wave 5, `float[4]`)."""
    dead = [i for i, l in enumerate(L) if re.match(r'^\t(float|uint32|LTVector) (unused\w*)(\[\d+\])?;$', l)]
    if dead and rng.random() < 0.5:
        del L[rng.choice(dead)]
        return 'remove dead local'
    n = ctx.setdefault('ndead', 0)
    ctx['ndead'] = n + 1
    d = rng.choice(['float unused%d;', 'uint32 unused%d;', 'float unused%d[2];', 'float unused%d[3];',
                    'float unused%d[4];', 'LTVector unused%d;']) % n
    ds = [i for i, l in enumerate(L) if is_decl(l) and indent(l) == 1]
    L.insert(rng.choice(ds + [0]) if ds else 0, '\t' + d)
    return 'dead local %s' % d


def m_decl_hoist(L, rng, ctx):
    """Move a declaration from an inner block to the top of the function (splitting off its initialiser)."""
    c = [i for i, l in enumerate(L) if indent(l) >= 2 and (is_decl(l) or (DECL_INIT.match(l) and
         DECL_INIT.match(l).group(2).split()[-1] not in NOT_TYPE and '&' not in DECL_INIT.match(l).group(3)))]
    if not c:
        return None
    i = rng.choice(c)
    m = DECL_INIT.match(L[i])
    if m and not is_decl(L[i]):
        ind, typ, ptr, name, expr = m.groups()
        L[i] = '%s%s = %s;' % (ind, name, expr)
        L.insert(0, '\t%s %s%s;' % (typ, ptr, name))
        return 'hoist decl %s (init stays)' % name
    d = L.pop(i)
    L.insert(0, '\t' + d.strip())
    return 'hoist decl %r' % d.strip()[:30]


def m_else_form(L, rng, ctx):
    """`if (c) { ...; return/break/continue; } else { B }` <-> the same without the else."""
    c = []
    for i, l in enumerate(L):
        if not re.match(r'^if\s*\(.*\)$', l.strip()):
            continue
        s1 = stmt_span(L, i + 1)
        if not s1:
            continue
        last = L[s1[1] - 2].strip() if L[s1[1] - 1].strip() == '}' else L[s1[1] - 1].strip()
        if not re.match(r'^(return\b.*|break|continue|goto \w+);$', last) and not last.startswith(('RETURN_ERROR', 'ERR(')):
            continue
        j = s1[1]
        if j < len(L) and L[j].strip() == 'else' and indent(L[j]) == indent(l):
            s2 = stmt_span(L, j + 1)
            if s2:
                c.append(('drop', i, j, s2))
        else:
            # the rest of the enclosing block becomes the else body
            e = j
            while e < len(L) and not (L[e].strip().startswith('}') and indent(L[e]) < indent(l)):
                e += 1
            if e > j and any(x.strip() for x in L[j:e]) and not any(is_decl(x) or DECL_INIT.match(x) for x in L[j:e] if indent(x) == indent(l)):
                c.append(('add', i, j, (j, e)))
    if not c:
        return None
    k, i, j, s2 = rng.choice(c)
    ind = '\t' * indent(L[i])
    if k == 'drop':
        body = L[s2[0]:s2[1]]
        if body and body[0].strip() == '{':
            body = [x[1:] if x.startswith('\t') else x for x in body[1:-1]]
        else:
            body = [x[1:] if x.startswith('\t') else x for x in body]
        L[j:s2[1]] = body
        return 'drop else after terminating if'
    body = [x for x in L[s2[0]:s2[1]]]
    while body and not body[0].strip():
        body.pop(0)
    while body and not body[-1].strip():
        body.pop()
    L[s2[0]:s2[1]] = [ind + 'else', ind + '{'] + ['\t' + x for x in body] + [ind + '}']
    return 'add else after terminating if'


def m_tail_dup(L, rng, ctx):
    """Copy the statement after an if/else into both branches, or pull a common last statement out."""
    c = []
    for i, l in enumerate(L):
        if not re.match(r'^if\s*\(.*\)$', l.strip()) or i + 1 >= len(L) or L[i + 1].strip() != '{':
            continue
        e1 = block_end(L, i + 1)
        if e1 is None or e1 + 2 >= len(L) or L[e1 + 1].strip() != 'else' or L[e1 + 2].strip() != '{':
            continue
        e2 = block_end(L, e1 + 2)
        if e2 is None:
            continue
        if e2 + 1 < len(L) and is_simple(L[e2 + 1]) and indent(L[e2 + 1]) == indent(l) and not is_decl(L[e2 + 1]):
            c.append(('dup', i, e1, e2))
        if is_simple(L[e1 - 1]) and L[e1 - 1].strip() == L[e2 - 1].strip() and not L[e1 - 1].strip().startswith(('return', 'break', 'continue')):
            c.append(('merge', i, e1, e2))
    if not c:
        return None
    k, i, e1, e2 = rng.choice(c)
    if k == 'dup':
        s = L.pop(e2 + 1)
        L.insert(e2, '\t' + s)
        L.insert(e1, '\t' + s)
        return 'tail duplicate %r' % s.strip()[:30]
    s = L[e1 - 1]
    del L[e2 - 1]
    del L[e1 - 1]
    L.insert(e2 - 1, s[1:])
    return 'tail merge %r' % s.strip()[:30]


def m_cast(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        if l.strip().startswith('//'):
            continue
        for m in re.finditer(r'\((float|int|uint32|uint8|uint16|char|LTBOOL|int32|short|long)\)(?=[\w(])', l):
            c.append((i, m, 'del'))
        for m in re.finditer(r'(?<== )(%s)(?=;| [+\-*/])' % MEMB, l):
            c.append((i, m, 'add'))
    if not c:
        return None
    i, m, k = rng.choice(c)
    if k == 'del':
        if rng.random() < 0.5:
            L[i] = L[i][:m.start()] + L[i][m.end():]
            return 'drop cast %s' % m.group(0)
        new = '(%s)' % rng.choice(['float', 'int', 'uint32', 'uint8', 'uint16', 'char', 'long'])
        L[i] = L[i][:m.start()] + new + L[i][m.end():]
        return 'cast %s -> %s' % (m.group(0), new)
    new = '(%s)%s' % (rng.choice(['float', 'int', 'uint32', 'uint8', 'uint16', 'char']), m.group(1))
    L[i] = L[i][:m.start()] + new + L[i][m.end():]
    return 'add cast %s' % new[:30]


def m_logic_swap(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        m = re.match(r'^(\t*(?:else )?(?:if|while)\s*\()([^&|]+?) (&&|\|\|) ([^&|]+)(\))$', l)
        if m and m.group(2).count('(') == m.group(2).count(')') and m.group(4).count('(') == m.group(4).count(')'):
            c.append((i, m))
    if not c:
        return None
    i, m = rng.choice(c)
    if rng.random() < 0.5 and m.group(3) == '&&' and m.group(1).strip().startswith('if') and L[i + 1].strip() != '{':
        return None
    L[i] = m.group(1) + m.group(4) + ' ' + m.group(3) + ' ' + m.group(2) + m.group(5)
    return 'swap %s operands' % m.group(3)


def m_nest_and(L, rng, ctx):
    """`if (a && b) S` <-> `if (a) if (b) S` (only without an else)."""
    c = []
    for i, l in enumerate(L):
        m = re.match(r'^(\t*)if\s*\(([^&|]+?) && ([^&|]+)\)$', l)
        if m and m.group(2).count('(') == m.group(2).count(')'):
            s = stmt_span(L, i)
            s1 = stmt_span(L, i + 1)
            if s and s1 and s[1] == s1[1]:      # no else
                c.append((i, m, s1))
    if not c:
        return None
    i, m, s1 = rng.choice(c)
    ind = m.group(1)
    body = L[s1[0]:s1[1]]
    L[i:s1[1]] = [ind + 'if (%s)' % m.group(2), ind + '{', ind + '\tif (%s)' % m.group(3)] + ['\t' + x for x in body] + [ind + '}']
    return 'nest && into two ifs'


def m_return_form(L, rng, ctx):
    c = []
    for i, l in enumerate(L):
        m = re.match(r'^(\t+)return (.+?) \? (.+?) : (.+);$', l)
        if m:
            c.append(('t', i, m))
        m = re.match(r'^(\t+)if\s*\((.*)\)$', l)
        if m and i + 2 < len(L):
            a = re.match(r'^\t+return (.+);$', L[i + 1])
            b = re.match(r'^\t+return (.+);$', L[i + 2]) if indent(L[i + 2]) == indent(l) else None
            if a and b:
                c.append(('i', i, (m, a, b)))
    if not c:
        return None
    k, i, m = rng.choice(c)
    if k == 't':
        ind, cond, a, b = m.groups()
        L[i:i + 1] = [ind + 'if (%s)' % cond, ind + '\treturn %s;' % a, ind + 'return %s;' % b]
        return 'return ternary -> if'
    mi, a, b = m
    L[i:i + 3] = ['%sreturn (%s) ? %s : %s;' % (mi.group(1), mi.group(2), a.group(1), b.group(1))]
    return 'return if -> ternary'


MUTATORS = {
    'idiom': (m_idiom, 4), 'temp': (m_named_temp, 4), 'dead': (m_dead_local, 1), 'hoist': (m_decl_hoist, 2),
    'elseform': (m_else_form, 3), 'taildup': (m_tail_dup, 2), 'cast': (m_cast, 1), 'logic': (m_logic_swap, 1),
    'nestand': (m_nest_and, 1), 'retform': (m_return_form, 1),
    'move': (m_move_stmt, 6), 'swapstmt': (m_swap_stmts_any, 3), 'swapdecl': (m_swap_decl, 5),
    'splitdecl': (m_split_multi_decl, 2), 'splitinit': (m_split_init, 3), 'decluse': (m_decl_to_use, 3),
    'declscope': (m_decl_scope, 2), 'inttype': (m_int_type, 2), 'commute': (m_commute, 4), 'commany': (m_commute_any, 2),
    'incdec': (m_incdec, 1), 'compound': (m_compound, 2), 'ifinv': (m_if_invert, 3), 'cmpform': (m_cmp_form, 2),
    'truth': (m_truth_form, 1), 'ternary': (m_ternary, 2), 'wrap': (m_block_wrap, 2), 'unwrap': (m_block_unwrap, 1),
    'paramcopy': (m_param_copy, 2), 'localsplit': (m_local_split, 2), 'loop': (m_loop_form, 2),
    'index': (m_index_form, 1), 'chain': (m_chain_assign, 2), 'assoc': (m_assoc, 1),
}


def mutate(body, rng, ctx, ops):
    L = body.split('\n')
    names = [n for n in ops for _ in range(MUTATORS[n][1])]
    done = []
    want = rng.choice((1, 1, 1, 1, 2, 2, 3))
    tries = 0
    while len(done) < want and tries < 40:
        tries += 1
        n = rng.choice(names)
        try:
            r = MUTATORS[n][0](L, rng, ctx)
        except Exception as e:      # a mutator tripped over unusual text: skip it
            r = None
        if r:
            done.append(r)
    return ('\n'.join(L), done) if done else (None, None)


# ---------------------------------------------------------------- compile + score

class Target:
    def __init__(self, src, addr):
        import build
        self.build = build
        self.src = os.path.abspath(src)
        self.va = int(addr, 16)
        self.unit = build.Unit(self.src)
        self.annot = [a for a in self.unit.annots if a.va == self.va and a.kind != 'GLOBAL']
        if not self.annot:
            sys.exit('no FUNCTION/STUB annotation for %08x in %s' % (self.va, src))
        self.annot = self.annot[0]
        self.exe = build.Exe(build.EXE)
        ext = build.SymTab().funcs.get(self.va)
        self.exe_len = ext[0] - self.va
        self.target = self.exe.read(self.va, self.exe_len + 64)
        self.dir = os.path.join(ROOT, 'build', 'permute', '%08x' % self.va)
        os.makedirs(self.dir, exist_ok=True)
        self.symname = None
        # known names -> va (build/namemap.json): relocation targets are checked against them, so a candidate that
        # swaps two calls or two globals is not a match
        import json
        nm = os.path.join(ROOT, 'build', 'namemap.json')
        self.name2va = {}
        if os.path.exists(nm):
            for va, n in json.load(open(nm)).items():
                self.name2va.setdefault(n, set()).add(int(va, 16))
        import capstone
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.right = [(i.address, i.size, '%s %s' % (i.mnemonic, i.op_str))
                      for i in self.md.disasm(self.target[:self.exe_len], self.va)]
        while self.right and self.right[-1][2].split()[0] in ('int3', 'nop'):
            self.right.pop()

    def compile(self, text, slot):
        b = self.build
        d = os.path.join(self.dir, 'w%d' % slot)
        os.makedirs(d, exist_ok=True)
        cpp = os.path.join(d, os.path.basename(self.src))
        obj = cpp[:-4] + '.obj'
        open(cpp, 'w', encoding='latin1', newline='').write(text)
        if os.path.exists(obj):
            os.remove(obj)
        env = dict(os.environ, TMP=d, TEMP=d)
        args = [b.VC6CL] + b.COMMON_FLAGS + self.unit.flags + ['/I' + os.path.dirname(self.src), '/Fo' + obj, cpp]
        r = subprocess.run(['cmd', '/c'] + args, capture_output=True, text=True, cwd=os.path.dirname(self.src), env=env)
        if r.returncode != 0 or not os.path.exists(obj):
            return None
        return obj

    def score(self, obj):
        """(scalar, ns, n, size delta, differing bytes, code hash) or None."""
        b = self.build
        try:
            o = b.CoffObj(obj)
        except Exception:
            return None
        if self.symname is None:
            self.unit.annots = [a for a in self.unit.annots]
            b.bind_symbols([self.unit], {self.unit.name: o})
            if not self.annot.symbol:
                sys.exit('cannot bind the function symbol: %s' % self.annot.error)
            self.symname = self.annot.symbol.name
        syms = [s for s in o.functions() if s.name == self.symname]
        if len(syms) != 1:
            return None
        sec, start, end = o.extent(syms[0])
        base = sec.data[start:end]
        relocs = {off: s.name for off, s, _, _ in o.relocs_in(sec, start, end)}
        mask = bytearray(len(base))
        for off in relocs:
            for k in range(4):
                if off + k < len(mask):
                    mask[off + k] = 1
        tgt = self.target
        nd = sum(1 for i in range(min(len(base), len(tgt))) if not mask[i] and base[i] != tgt[i])
        for off, s_, typ, addend in o.relocs_in(sec, start, end):
            known = self.name2va.get(s_.name)
            if not known or off + 4 > len(tgt) or typ not in (b.REL_DIR32, b.REL_REL32):
                continue
            f = int.from_bytes(tgt[off:off + 4], 'little', signed=(typ == b.REL_REL32))
            tva = (f - addend) if typ == b.REL_DIR32 else (self.va + off + 4 + f - addend)
            if (tva & 0xffffffff) not in known:
                nd += 4
        pad_ok = len(base) <= self.exe_len and all(x in (0xCC, 0x90) for x in tgt[len(base):self.exe_len])
        if nd == 0 and pad_ok:
            return (0.0, 0, 0, 0, 0, hashlib.md5(bytes(base)).hexdigest())
        left = [(i.address, i.size, '%s %s' % (i.mnemonic, i.op_str)) for i in self.md.disasm(base, 0)]
        while left and left[-1][2].split()[0] in ('int3', 'nop'):
            left.pop()
        n = b.aligned_score(left, self.right, relocs, False)[0]
        ns = b.aligned_score(left, self.right, relocs, True)[0]
        dsz = abs(len(left) - len(self.right))
        scalar = ns * 3.0 + n + min(nd, 400) / 400.0 + 0.5
        # masked bytes differ only through relocations here, which the final build.py check verifies
        h = bytearray(base)
        for i in range(len(h)):
            if mask[i]:
                h[i] = 0
        return (scalar, ns, n, dsz, nd, hashlib.md5(bytes(h)).hexdigest())


def split_source(text, va):
    m = re.search(r'// (STUB|FUNCTION): LITHTECH 0x0*%x\b[^\n]*\n' % va, text)
    if not m:
        sys.exit('annotation not found')
    a = text.index('\n{\n', m.end()) + 3
    b = text.index('\n}\n', a)
    sig = text[m.end():a - 3]
    return text[:a], text[a:b], text[b:], sig


def parse_params(sig):
    sig = ' '.join(l.split('//')[0] for l in sig.split('\n'))
    if '(' not in sig:
        return []
    inner = sig[sig.index('(') + 1:sig.rindex(')')]
    out = []
    for p in inner.split(','):
        m = re.match(r'^\s*((?:const\s+)?[\w:]+(?:\s*[*&])*)\s*(\w+)\s*$', p.strip())
        if m and '&' not in m.group(1) and m.group(1) not in ('void',):
            out.append((m.group(1).strip(), m.group(2)))
    return out


def evaluate(job):
    slot, text = job
    obj = T.compile(text, slot)
    return T.score(obj) if obj else None


T = None


def minimize(src, addr):
    """Reduce build/permute/<addr>/match.cpp to the fewest changed hunks (against the current source) that still
    give a byte-identical function; writes min.cpp and prints the remaining diff."""
    global T
    import difflib
    T = Target(src, addr)
    orig = open(T.src, encoding='latin1', newline='').read()
    head, body, tail, _ = split_source(orig, T.va)
    mt = open(os.path.join(T.dir, 'match.cpp'), encoding='latin1', newline='').read()
    _, mbody, _, _ = split_source(mt, T.va)
    A, B = body.split('\n'), mbody.split('\n')
    r = evaluate((0, head + '\n'.join(B) + tail))
    if not r or r[0] != 0:
        sys.exit('match.cpp does not match on top of the current source (score %s)' % (r,))
    changed = True
    while changed:
        changed = False
        ops = [o for o in difflib.SequenceMatcher(None, A, B, autojunk=False).get_opcodes() if o[0] != 'equal']
        for tag, i1, i2, j1, j2 in ops:
            C = B[:j1] + A[i1:i2] + B[j2:]
            r = evaluate((0, head + '\n'.join(C) + tail))
            if r and r[0] == 0:
                B, changed = C, True
                break
    open(os.path.join(T.dir, 'min.cpp'), 'w', encoding='latin1', newline='').write(head + '\n'.join(B) + tail)
    for l in difflib.unified_diff(A, B, 'current', 'minimal match', n=1, lineterm=''):
        print(l)
    return 0


def main(argv):
    global T
    if argv and argv[0] == '--minimize':
        return minimize(argv[1], argv[2])
    import argparse
    from concurrent.futures import ThreadPoolExecutor
    ap = argparse.ArgumentParser()
    ap.add_argument('src')
    ap.add_argument('addr')
    ap.add_argument('--iters', type=int, default=3000)
    ap.add_argument('--jobs', type=int, default=6)
    ap.add_argument('--seed', type=int, default=None)
    ap.add_argument('--temp', type=float, default=1.5)
    ap.add_argument('--ops', default=','.join(MUTATORS))
    ap.add_argument('--minutes', type=float, default=0)
    ap.add_argument('--resume', action='store_true', help='start from best.cpp of an earlier run')
    a = ap.parse_args(argv)
    T = Target(a.src, a.addr)
    rng = random.Random(a.seed)
    ops = [o for o in a.ops.split(',') if o in MUTATORS]
    orig = open(T.src, encoding='latin1', newline='').read()
    head, body, tail, sig = split_source(orig, T.va)
    ctx = {'params': parse_params(sig)}
    log = open(os.path.join(T.dir, 'log.txt'), 'a', encoding='utf-8')

    def say(s):
        print(s, flush=True)
        log.write(s + '\n')
        log.flush()

    s0 = evaluate((0, orig))
    if s0 is None:
        sys.exit('the unmodified source does not compile / the function was not found')
    say('== %s %08x %s  start: ns=%d n=%d bytes=%d  (seed %s)' % (time.strftime('%H:%M:%S'), T.va, T.symname, s0[1], s0[2], s0[4], a.seed))
    if s0[0] == 0:
        say('already matches')
        return 0
    cur_body, cur, cur_hist = body, s0, []
    best_body, best, best_hist = body, s0, []
    bp = os.path.join(T.dir, 'best.cpp')
    if a.resume and os.path.exists(bp):
        _, rb, _, _ = split_source(open(bp, encoding='latin1', newline='').read(), T.va)
        r = evaluate((0, head + rb + tail))
        if r and r[0] == 0:
            open(os.path.join(T.dir, 'match.cpp'), 'w', encoding='latin1', newline='').write(head + rb + tail)
            say('*** MATCH: best.cpp already matches')
            return 0
        if r and r[0] < s0[0]:
            cur_body, cur, cur_hist = rb, r, ['(resumed)']
            best_body, best, best_hist = rb, r, ['(resumed)']
            say('resumed from best.cpp: ns=%d n=%d bytes=%d' % (r[1], r[2], r[4]))
    seen = {hashlib.md5(body.encode('latin1')).hexdigest()}
    seen_code = {s0[5]: 1}
    stale, done, t0, fails = 0, 0, time.time(), 0
    with ThreadPoolExecutor(a.jobs) as ex:
        while done < a.iters and (not a.minutes or time.time() - t0 < a.minutes * 60):
            cands = []
            guard = 0
            while len(cands) < a.jobs and guard < a.jobs * 30:
                guard += 1
                nb, what = mutate(cur_body, rng, ctx, ops)
                if nb is None:
                    continue
                h = hashlib.md5(nb.encode('latin1')).hexdigest()
                if h in seen:
                    continue
                seen.add(h)
                cands.append((nb, what))
            if not cands:
                cur_body, cur, cur_hist = best_body, best, list(best_hist)
                stale += 1
                if stale > 50:
                    say('no new candidates: stopping')
                    break
                continue
            res = list(ex.map(evaluate, [(k, head + nb + tail) for k, (nb, _) in enumerate(cands)]))
            done += len(cands)
            fails += sum(1 for r in res if r is None)
            scored = [(r, nb, what) for r, (nb, what) in zip(res, cands) if r is not None]
            if not scored:
                continue
            scored.sort(key=lambda x: x[0][0])
            r, nb, what = scored[0]
            if r[0] == 0:
                open(os.path.join(T.dir, 'match.cpp'), 'w', encoding='latin1', newline='').write(head + nb + tail)
                say('*** MATCH after %d candidates (%.0fs): %s' % (done, time.time() - t0, ' ; '.join(cur_hist + what)))
                return 0
            # anneal: take the best of the batch if it is better, equal (drift), or with a temperature-driven chance
            pick = None
            if r[0] < cur[0]:
                pick = scored[0]
            else:
                rng.shuffle(scored)
                for cand in scored:
                    d = cand[0][0] - cur[0]
                    novel = seen_code.get(cand[0][5], 0) == 0
                    if (d <= 0 and (novel or rng.random() < 0.3)) or (d > 0 and rng.random() < math.exp(-d / a.temp) * (0.5 if novel else 0.1)):
                        pick = cand
                        break
            for c in scored:
                seen_code[c[0][5]] = seen_code.get(c[0][5], 0) + 1
            if pick:
                r, nb, what = pick
                cur_body, cur, cur_hist = nb, r, cur_hist + what
                if r[0] < best[0]:
                    best_body, best, best_hist = nb, r, list(cur_hist)
                    stale = 0
                    open(os.path.join(T.dir, 'best.cpp'), 'w', encoding='latin1', newline='').write(head + nb + tail)
                    say('%6d %5.0fs  best ns=%d n=%d bytes=%d  <- %s' % (done, time.time() - t0, r[1], r[2], r[4], ' ; '.join(what)))
                    continue
            stale += 1
            if stale and stale % 60 == 0:       # wandered off without profit: restart from the best
                cur_body, cur, cur_hist = best_body, best, list(best_hist)
    say('== done: %d candidates (%d failed to compile), best ns=%d n=%d bytes=%d in %.0fs' % (
        done, fails, best[1], best[2], best[4], time.time() - t0))
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
