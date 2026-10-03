"""Behaviour audit (build.py audit [-v] [F]): for every annotated function that doesn't match, compare what ours
*does* with what the exe does, ignoring layout and register allocation:

  calls   the call sequence (direct, import and vtable calls, tail jumps out of the function), aligned with difflib
  imm     immediates (multiset; 0 and stack adjustments ignored: VC6 reuses zeroed registers)
  float   float constants loaded by x87 instructions (__real@ literals vs the exe's .rdata)
  str     string literals referenced
  data    other globals referenced (by address; a string read through a global's name is reconciled by content)
  jcc     conditional jump classes (je/jne, jl/jge, jle/jg, jb/jae, jbe/ja, ...): an inverted branch keeps its class,
          a swapped or off-by-one comparison doesn't
  order   (a hint, not a difference) callees defined earlier in the same file whose exe address is higher than
          the caller's: the original compiled them after the caller, so VC6 knew nothing about them when compiling
          it (README "Order and definitions"). Matching functions have such callees too (templates, inlines).

A difference in calls/str/data/float is usually a semantic bug or a different inlining decision; imm/jcc
differences are often semantic too (swapped comparison, wrong constant). A function with no differences only
differs in layout or register allocation.

Hints (shown after the differences; they don't count against "behaviour matches"):
  mset      the two listings as multisets of register-abstracted instructions (registers -> r32/r16/r8, stack and
            ebp-frame slots -> [STK], addresses -> SYM; calls, jumps and x87 left out). Zero for a pure register
            permutation or frame layout difference. An exe-only store, a re-read member or a copy of another operand
            means the source differs (ReadNewObjectInfo's redundant m_FileType store). -v lists them, x87 separately.
  zero-reg  one side keeps 0 in a callee-saved register for the whole function and the other doesn't: count the
            integer-0 stores (4+ -> register); a float member zeroed as an int, or the reverse, is the usual cause.
  lazy-push one side pushes a callee-saved register after an early `ret`: early `return` inside a nested block vs
            the wrapped `if (x != y) { ... }` form.
"""
import collections, difflib, re, struct

from coffobj import CoffObj, REL_DIR32, REL_REL32

JCC = {'je': 'e', 'jne': 'e', 'jl': 'l', 'jge': 'l', 'jle': 'le', 'jg': 'le', 'jb': 'b', 'jae': 'b',
       'jbe': 'be', 'ja': 'be', 'js': 's', 'jns': 's', 'jp': 'p', 'jnp': 'p', 'jo': 'o', 'jno': 'o',
       'jecxz': 'cxz'}
CATS = ('calls', 'imm', 'float', 'str', 'data', 'jcc', 'order')


class Side:
    def __init__(self):
        self.calls, self.imm, self.float, self.str, self.data, self.jcc = [], [], [], [], [], []
        self.shape = []         # register-abstracted instructions (mset)
        self.insns = []         # the instructions up to the switch tables (zero-reg, lazy-push)


GP32 = ('eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp')
SAVED = ('ebx', 'esi', 'edi', 'ebp')


def _cstring(exe, va, min_len=1):
    try:
        b = exe.read(va, 256)
    except Exception:
        return None
    n = b.find(b'\0')
    if n < min_len:
        return None
    s = b[:n]
    if not all(32 <= c < 127 or c in (9, 10, 13) for c in s):
        return None
    return s.decode('latin1')


def _float(b):
    return struct.unpack('<f', b)[0] if len(b) == 4 else struct.unpack('<d', b)[0] if len(b) == 8 else None


def _fmt_float(v):
    return ('%.9g' % v) if v is not None else '?'


class Auditor:
    def __init__(self, exe, namemap, all_names=None):
        import capstone
        self.cs = capstone
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = True
        self.exe = exe
        self.va2name = namemap
        self.name2va = {}
        for va, n in namemap.items():
            self.name2va.setdefault(n, va)
        self.name2va.update(all_names or {})
        pe = exe.pe
        self.text, self.rdata = [], []
        self.image = (exe.base, exe.base + pe.OPTIONAL_HEADER.SizeOfImage)
        for s in pe.sections:
            lo = exe.base + s.VirtualAddress
            if s.Characteristics & 0x20000000:      # executable
                self.text.append((lo, lo + s.Misc_VirtualSize))
            elif s.Characteristics & 0x40 and not s.Characteristics & 0x80000000 and s.Name.startswith(b'.rdata'):
                self.rdata.append((lo, lo + s.Misc_VirtualSize))

    def is_literal(self, va):
        """A float constant: named __real@ by a match, or an unnamed address in a read-only data section."""
        n = self.va2name.get(va)
        if n is not None:
            return n.startswith('__real@')
        return any(lo <= va < hi for lo, hi in self.rdata)

    def is_rdata(self, va):
        return any(lo <= va < hi for lo, hi in self.rdata)

    def in_text(self, va):
        return any(lo <= va < hi for lo, hi in self.text)

    def in_image(self, va):
        return self.image[0] <= va < self.image[1]

    def key(self, va):
        return self.va2name.get(va) or ('%08x' % va)

    def sym_key(self, name):
        va = self.name2va.get(name)
        return self.key(va) if va is not None else name

    def _common(self, side, ins, skip=(), skip_all_imm=False):
        """Immediates and jcc classes, shared by both sides (relocated fields already removed by the caller)."""
        m = ins.mnemonic
        if m in JCC:
            side.jcc.append(JCC[m])
            return
        if m.startswith(('ret', 'j', 'call', 'loop', 'enter', 'leave')):
            return
        regs = ins.op_str
        if m in ('add', 'sub', 'and') and regs.startswith('esp'):
            return
        for op in ins.operands:
            if op.type == self.cs.x86.X86_OP_IMM and not skip_all_imm:
                v = op.imm & 0xffffffff
                if v == 0 or v in skip:
                    continue
                side.imm.append(v)

    def classify(self, side, ins, op_size, va=None, sym=None, lit=None):
        """One address-valued field: a call target, float constant, string literal or other global."""
        m = ins.mnemonic
        name = sym if sym is not None else self.va2name.get(va)
        if va is None and sym is not None:
            va = self.name2va.get(sym)
        key = self.key(va) if va is not None else sym
        if m == 'call' or m == 'jmp':
            side.calls.append((ins.address, ('jmp ' if m == 'jmp' else '') + key))
        elif name is not None and name.startswith('__real@') or (sym is None and op_size in (4, 8) and
                                                                 self.is_literal(va)):
            b = lit if lit is not None else self.exe.read(va, op_size)
            side.float.append(_fmt_float(_float(b)))
        elif name is not None and name.startswith('??_C@') or (sym is None and name is None and va is not None
                                                               and op_size in (0, 1) and not self.in_text(va)):
            st = lit.split(b'\0')[0].decode('latin1') if lit is not None else _cstring(self.exe, va)
            if st is None:
                side.data.append(key)
            else:
                side.str.append(st)
        else:
            side.data.append(key)

    def _content(self, key):
        va = self.name2va.get(key)
        if va is None and re.match(r'^[0-9a-f]{8}$', key):
            va = int(key, 16)
        return _cstring(self.exe, va, 0) if va is not None and not self.in_text(va) else None

    def reconcile(self, diffs):
        """A string literal one side reads through another name (merged literals, a dword copy of a short
        string, g_EmptyString) is the same reference: cancel string/global pairs with equal contents."""
        so, se = diffs['str']
        do, de = diffs['data']
        for strs, other in ((so, de), (se, do)):
            for st in list(strs):
                for k in other:
                    if self._content(k) == st:
                        strs.remove(st)
                        other.remove(k)
                        break

    def shape(self, ins, addr_disp, addr_imm, frame):
        """One instruction with registers reduced to their size class, stack slots to [STK] and addresses to SYM:
        a register permutation or a different frame layout leaves the multiset of shapes unchanged."""
        X86 = self.cs.x86
        m = ins.mnemonic
        if m == 'nop' or m == 'call' or m.startswith(('j', 'ret', 'int3')):
            return None
        ops = []
        for op in ins.operands:
            if op.type == X86.X86_OP_REG:
                r = ins.reg_name(op.reg)
                if r in GP32:
                    r = 'r32'
                elif r in ('ax', 'bx', 'cx', 'dx', 'si', 'di', 'bp'):
                    r = 'r16'
                elif r in ('al', 'ah', 'bl', 'bh', 'cl', 'ch', 'dl', 'dh'):
                    r = 'r8'
                ops.append(r)
            elif op.type == X86.X86_OP_IMM:
                ops.append('SYM' if addr_imm else '0x%x' % (op.imm & 0xffffffff))
            elif op.type == X86.X86_OP_MEM:
                base = ins.reg_name(op.mem.base) if op.mem.base else ''
                if base == 'esp' or (frame and base == 'ebp'):
                    ops.append('%d[STK]' % op.size)
                    continue
                t = []
                if op.mem.segment:
                    ops.append('%d[%s:]' % (op.size, ins.reg_name(op.mem.segment)))   # fs:[0] (__except_list)
                    continue
                if base:
                    t.append('r32')
                if op.mem.index:
                    t.append('r32*%d' % op.mem.scale)
                if addr_disp:
                    t.append('SYM')
                elif op.mem.disp:
                    t.append('0x%x' % (op.mem.disp & 0xffffffff))
                ops.append('%d[%s]' % (op.size, '+'.join(t)))
        return m + ' ' + ','.join(ops)

    @staticmethod
    def _table_cut(insns, lo, hi, field):
        """Switch tables follow the code: stop at the first table a memory operand indexes into."""
        cut = hi
        for ins in insns:
            t = field(ins)
            if t is not None and lo < t < cut and t > ins.address:
                cut = t
        return cut

    def ours(self, o, sec, start, end):
        X86 = self.cs.x86
        base = bytes(sec.data[start:end])
        relocs = {}
        for off, s, typ, addend in o.relocs_in(sec, start, end):
            relocs[off] = (s, typ, addend)
        insns = list(self.md.disasm(base, 0))

        def own(ins):
            for k in range(ins.address, ins.address + ins.size):
                if k in relocs:
                    s, typ, addend = relocs[k]
                    if s.secno > 0 and o.sections[s.secno - 1] is sec and typ == REL_DIR32:
                        return addend + (0 if s.is_section_symbol else s.value) - start
            return None
        cut = self._table_cut(insns, 0, len(base), own)
        side = Side()
        frame = _ebp_frame(insns)
        for ins in insns:
            if ins.address >= cut:
                break
            m = ins.mnemonic
            rel = [relocs[k] for k in range(ins.address, ins.address + ins.size) if k in relocs]
            side.insns.append(ins)
            d = ins.disp_size and any(ins.address + ins.disp_offset + k in relocs for k in range(ins.disp_size))
            i = ins.imm_size and any(ins.address + ins.imm_offset + k in relocs for k in range(ins.imm_size))
            sh = self.shape(ins, d, i, frame)
            if sh:
                side.shape.append(sh)
            if not rel:
                if m == 'call':
                    side.calls.append((ins.address, self._indirect(ins.operands[0], ins)))
                elif not (m == 'jmp' or m.startswith('loop')) or m in JCC:
                    self._common(side, ins)
                continue
            size = max([op.size for op in ins.operands if op.type == X86.X86_OP_MEM] or [0])
            for s, typ, addend in rel:
                if s.secno > 0 and o.sections[s.secno - 1] is sec and not (m == 'call' and typ == REL_REL32):
                    continue                        # switch tables / local labels (a call here is recursion)
                if s.name == '__except_list':       # fs:[0]
                    continue
                if s.name.startswith(('__real@', '??_C@')):
                    self.classify(side, ins, size, sym=s.name, lit=_literal(o, s))
                    continue
                va = self.name2va.get(s.name)
                if va is not None and typ == REL_DIR32 and addend:
                    self.classify(side, ins, size, va=va + addend)
                else:
                    self.classify(side, ins, size, sym=s.name)
            if m not in ('call', 'jmp'):
                # a relocated immediate is an address; the others (mov byte ptr [g+2], 2) are constants
                imm_rel = ins.imm_offset and any(ins.address + ins.imm_offset + k in relocs for k in range(ins.imm_size))
                self._common(side, ins, skip_all_imm=imm_rel)
        return side, len(base)

    def _indirect(self, op, ins):
        if op.type == self.cs.x86.X86_OP_MEM and op.mem.base != 0:
            return 'vcall+%d' % op.mem.disp
        if op.type == self.cs.x86.X86_OP_REG:
            return 'call reg'
        return ins.op_str

    def theirs(self, va, n):
        X86 = self.cs.x86
        data = self.exe.read(va, n)
        insns = list(self.md.disasm(data, va))

        def table(ins):
            for op in ins.operands:
                if op.type == X86.X86_OP_MEM and (op.mem.base or op.mem.index):
                    t = op.mem.disp & 0xffffffff
                    if va <= t < va + n:
                        return t
            return None
        cut = self._table_cut(insns, va, va + n, table)
        side = Side()
        frame = _ebp_frame(insns)
        for ins in insns:
            if ins.address >= cut:
                break
            m = ins.mnemonic
            side.insns.append(ins)
            d = i = False
            for op in ins.operands:
                if op.type == X86.X86_OP_MEM and ins.disp_size == 4 and self.in_image(op.mem.disp & 0xffffffff):
                    d = True
                elif op.type == X86.X86_OP_IMM and ins.imm_size == 4 and self.in_image(op.imm & 0xffffffff) \
                        and (op.imm & 0xffffffff) >= 0x401000:
                    i = True
            sh = self.shape(ins, d, i, frame)
            if sh:
                side.shape.append(sh)
            if m in JCC or m.startswith('loop'):
                self._common(side, ins)
                continue
            if m == 'call' or m == 'jmp':
                op = ins.operands[0]
                if op.type == X86.X86_OP_IMM:
                    t = op.imm & 0xffffffff
                    if m == 'call' or not (va <= t < va + n):
                        self.classify(side, ins, 0, va=t)
                    continue
                if op.type == X86.X86_OP_MEM and op.mem.base == 0 and self.in_image(op.mem.disp & 0xffffffff):
                    t = op.mem.disp & 0xffffffff
                    if m == 'call' or not (va <= t < va + n):
                        self.classify(side, ins, 4, va=t)   # import slot, function table; not a switch jump
                    continue
                if m == 'call':
                    side.calls.append((ins.address, self._indirect(op, ins)))
                continue
            skip = set()
            for op in ins.operands:
                if op.type == X86.X86_OP_MEM and self.in_image(op.mem.disp & 0xffffffff):
                    t = op.mem.disp & 0xffffffff
                    skip.add(t)
                    if not (va <= t < va + n):
                        self.classify(side, ins, op.size, va=t)
                elif op.type == X86.X86_OP_IMM and self.in_image(op.imm & 0xffffffff) and                         (op.imm & 0xffffffff) >= 0x401000:
                    t = op.imm & 0xffffffff
                    skip.add(t)
                    if not (va <= t < va + n):
                        self.classify(side, ins, 0, va=t)
            self._common(side, ins, skip)
        return side


def _literal(o, s):
    if s.secno <= 0:
        return None
    sec = o.sections[s.secno - 1]
    if s.name.startswith('??_C@'):
        return bytes(sec.data[s.value:s.value + 256])
    return bytes(sec.data[s.value:s.value + len(s.name.split('@')[1]) // 2])     # __real@3f000000 (float), 16 digits: double


def _ebp_frame(insns):
    """push ebp; mov ebp, esp: ebp-based operands are stack slots."""
    return len(insns) > 1 and insns[0].mnemonic == 'push' and insns[0].op_str == 'ebp' and \
        insns[1].mnemonic == 'mov' and insns[1].op_str == 'ebp, esp'


_ALIASES = {'ebx': ('ebx', 'bx', 'bl', 'bh'), 'esi': ('esi', 'si'), 'edi': ('edi', 'di'), 'ebp': ('ebp', 'bp')}


def zero_regs(insns):
    """Callee-saved registers that hold 0 for the whole function (one `xor r, r`, no other write): {reg: uses}.
    VC6 does this once a function stores the integer constant 0 four or more times (README, hand pass)."""
    out = {}
    for r in SAVED:
        names = _ALIASES[r]
        zeroed, other, uses = False, False, 0
        for ins in insns:
            ops = ins.operands
            if not ops:
                continue
            m = ins.mnemonic
            dst = ops[0].type == 1 and ins.reg_name(ops[0].reg) in names        # X86_OP_REG
            if m == 'xor' and ins.op_str == '%s, %s' % (r, r):
                zeroed = True
            elif dst and m not in ('push', 'pop', 'cmp', 'test'):
                other = True
                break
            elif m in ('mov', 'push', 'cmp') and any(o.type == 1 and ins.reg_name(o.reg) == r for o in ops):
                uses += 1
        if zeroed and not other and uses >= 2:
            out[r] = uses
    return out


def int0_stores(insns, zr):
    """Stores of the integer constant 0 (an immediate 0 or a register known to hold 0) to memory."""
    n = 0
    for ins in insns:
        ops = ins.operands
        if ins.mnemonic != 'mov' or len(ops) != 2 or ops[0].type != 3 or ops[0].size != 4:     # X86_OP_MEM
            continue
        if ops[1].type == 2 and ops[1].imm == 0 or ops[1].type == 1 and ins.reg_name(ops[1].reg) in zr:
            n += 1
    return n


def lazy_push(insns):
    """A callee-saved push after the first `ret` (the register is saved after an early exit), within the first
    40 instructions."""
    seen_ret = False
    for k, ins in enumerate(insns[:40]):
        if ins.mnemonic == 'ret':
            seen_ret = True
        elif seen_ret and ins.mnemonic == 'push' and ins.op_str in SAVED:
            return True
        elif seen_ret and ins.mnemonic == 'call':
            return False
    return False


def _multiset_diff(a, b):
    ca, cb = collections.Counter(a), collections.Counter(b)
    return sorted((ca - cb).elements(), key=str), sorted((cb - ca).elements(), key=str)


def order_problems(results, a, called):
    """Callees in the same unit, defined earlier in the file, at a higher exe address than the caller."""
    out = []
    for r in results:
        b = r.a
        if b.unit is a.unit and b.kind in ('FUNCTION', 'STUB') and b.line < a.line and b.va > a.va:
            if b.symbol is not None and b.symbol.name in called:
                out.append('%s@%08x' % (b.name or b.symbol.name, b.va))
    return out


def run(results, namemap, exe, symtab, objs_by_unit, filt=None, verbose=False, matched=False, all_names=None,
        out=None):
    """Print the audit; with out={} fill out[va] = one-line summary instead of printing."""
    aud = Auditor(exe, namemap, all_names)
    rows = []
    for r in results:
        a = r.a
        if a.kind == 'GLOBAL' or (r.status == 'MATCH') != matched or a.symbol is None:
            continue
        if filt and filt not in a.unit.name and filt not in (a.name or '') and filt not in '%08x' % a.va:
            continue
        o = objs_by_unit.get(a.unit.name)
        if o is None:
            continue
        sec, start, end = o.extent(a.symbol)
        ours, n = aud.ours(o, sec, start, end)
        ext = symtab.funcs.get(a.va)
        theirs = aud.theirs(a.va, (ext[0] - a.va) if ext else n)
        called = set()
        for off, s, typ, _ in o.relocs_in(sec, start, end):
            called.add(s.name)
        diffs = {}
        A, B = [t for _, t in ours.calls], [t for _, t in theirs.calls]
        sm = difflib.SequenceMatcher(None, A, B, autojunk=False)
        hunks = [op for op in sm.get_opcodes() if op[0] != 'equal']
        diffs['calls'] = sum(max(i2 - i1, j2 - j1) for _, i1, i2, j1, j2 in hunks)
        for cat in ('imm', 'float', 'str', 'data', 'jcc'):
            x, y = _multiset_diff(getattr(ours, cat), getattr(theirs, cat))
            diffs[cat] = (x, y)
        aud.reconcile(diffs)
        order = order_problems(results, a, called)
        x, y = _multiset_diff(ours.shape, theirs.shape)
        diffs['mset'] = ([s for s in x if not s.startswith('f')], [s for s in y if not s.startswith('f')])
        diffs['x87'] = ([s for s in x if s.startswith('f')], [s for s in y if s.startswith('f')])
        zo, ze = zero_regs(ours.insns), zero_regs(theirs.insns)
        diffs['zero'] = (zo, ze, int0_stores(ours.insns, zo), int0_stores(theirs.insns, ze))
        diffs['lazy'] = (lazy_push(ours.insns), lazy_push(theirs.insns))
        rows.append((a, r, diffs, hunks, ours, theirs, order))
    clean = 0
    for a, r, diffs, hunks, ours, theirs, order in rows:
        parts = []
        if diffs['calls']:
            parts.append('calls %d' % diffs['calls'])
        for cat in ('imm', 'float', 'str', 'data', 'jcc'):
            x, y = diffs[cat]
            if x or y:
                parts.append('%s -%d +%d' % (cat, len(x), len(y)))
        if not parts:
            clean += 1
        hints = []
        if order:
            hints.append('order %d' % len(order))
        x, y = diffs['mset']
        if x or y:
            hints.append('mset -%d +%d' % (len(x), len(y)))
        zo, ze, no, ne = diffs['zero']
        if bool(zo) != bool(ze):
            hints.append('zero-reg %s' % ('ours' if zo else 'exe'))
        lo, le = diffs['lazy']
        if lo != le:
            hints.append('lazy-push %s' % ('exe' if le else 'ours'))
        kind = 'STUB' if a.kind == 'STUB' else r.status
        shown = ', '.join(parts) if parts else 'behaviour matches'
        if hints:
            shown += (', hint: %s' if parts else ' (hint: %s)') % ', '.join(hints)
        if out is not None:
            out[a.va] = shown
            continue
        print('%-5s %08x %-40s %s' % (kind, a.va, (a.name or a.symbol.name)[:40], shown))
        if not verbose or not (parts or hints):
            continue
        for tag, i1, i2, j1, j2 in hunks:
            for k in range(max(i2 - i1, j2 - j1)):
                x = '%04x %s' % ours.calls[i1 + k] if i1 + k < i2 else ''
                y = '%04x %s' % theirs.calls[j1 + k] if j1 + k < j2 else ''
                print('      calls  %-58s | %s' % (x[:58], y[:70]))
        for cat in ('imm', 'float', 'str', 'data', 'jcc'):
            x, y = diffs[cat]
            if x or y:
                print('      %-5s  ours only: %s' % (cat, ', '.join(_show(v) for v in x)[:300]))
                print('      %-5s  exe only:  %s' % ('', ', '.join(_show(v) for v in y)[:300]))
        if order:
            print('      order  defined earlier but later in the exe: %s' % ', '.join(order))
        for cat in ('mset', 'x87'):
            x, y = diffs[cat]
            if x and len(x) + len(y) <= 40 or y and len(x) + len(y) <= 40:
                for k, v in sorted(collections.Counter(x).items()):
                    print('      %-5s  ours only x%d  %s' % (cat, v, k))
                for k, v in sorted(collections.Counter(y).items()):
                    print('      %-5s  exe only  x%d  %s' % (cat, v, k))
            elif x or y:
                print('      %-5s  ours only %d, exe only %d (too many to list)' % (cat, len(x), len(y)))
        zo, ze, no, ne = diffs['zero']
        if zo or ze:
            print('      zero   zero kept in %s (ours) / %s (exe); integer-0 dword stores: ours %d, exe %d' % (
                ','.join('%s:%d' % kv for kv in zo.items()) or '-', ','.join('%s:%d' % kv for kv in ze.items()) or '-',
                no, ne))
        lo, le = diffs['lazy']
        if lo != le:
            print('      lazy   %s pushes a callee-saved register after an early ret; %s saves it in the prologue '
                  '(early return in a nested block vs the wrapped if-form)' % (('exe', 'ours') if le else ('ours', 'exe')))
    if out is not None:
        return 0
    print('audit: %d functions, %d with no behaviour difference (layout/register allocation only)' % (len(rows), clean))
    return 0


def _show(v):
    if isinstance(v, int):
        return '0x%x' % v if v > 9 else str(v)
    if isinstance(v, str) and not v.startswith(('?', '_')) and (' ' in v or not v.isidentifier()) and not re.match(r'^[0-9a-f]{8}$|^-?[0-9.e+-]+$|^jmp |^vcall|^real ', v):
        return '"%s"' % v.replace('\n', '\\n')[:60]
    return str(v)[:70]
