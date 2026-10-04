r"""Console-variable recovery for d3d.ren (read-only).

The DLL registers every renderer console variable through a global object of a 0x20-byte struct built by static
initialisers listed in the .CRT$XCU-style table at 0x10048004 (218 entries):

    +0x00 int   m_Val        (rounded value)       +0x14 const char* m_pName
    +0x04 float m_fVal                              +0x18 HLTPARAM m_hParam (filled by GetParameter)
    +0x08 float m_Default                           +0x1c ConVar* m_pNext   (list head DAT_100584f4 = g_pConVars)
    +0x0c int*   m_pIntMirror (optional)
    +0x10 float* m_pFloatMirror (optional)

  python tools/d3dren_names_convars.py            # prints addr name default init-func style
"""
import os
import struct
import sys

import capstone
import pefile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import d3dren_names_scan as S  # noqa: E402

CTOR = 0x100111b5
TABLE = 0x10048004
NTAB = 218


def make_lookup(strings):
    import bisect
    keys = sorted(strings)

    def get(v):
        if v is None:
            return None
        if v in strings:
            return strings[v]
        k = bisect.bisect_right(keys, v) - 1
        if k >= 0:
            a = keys[k]
            t = strings[a]
            if a < v < a + len(t) + 1:       # tail-merged string (suffix of a longer literal)
                return t[v - a:]
        return None
    return get


def f32(bits):
    return struct.unpack('<f', struct.pack('<I', bits))[0]


def extract():
    o = S.load()
    funcs, strings = o['funcs'], o['strings']
    gs = make_lookup(strings)
    pe = pefile.PE(S.IMAGE, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    tab = [struct.unpack_from('<I', pe.get_data(TABLE - base + 4 * i, 4))[0] for i in range(NTAB)]
    out = []
    for idx, a in enumerate(tab):
        fe = funcs[a][0]
        code = pe.get_data(a - base, fe - a)
        thunk = None
        ins = list(md.disasm(code, a))
        body = a
        if ins and ins[0].mnemonic == 'jmp' and len(ins[0].bytes) == 5 and ins[0].size == 5 and fe - a <= 8:
            thunk = a
            body = int(ins[0].op_str, 16)
            fe2 = funcs[body][0]
            code = pe.get_data(body - base, fe2 - body)
            ins = list(md.disasm(code, body))
        name = None
        obj = None
        dflt = None
        style = None
        pushes = []
        for i in ins:
            if i.mnemonic == 'push':
                try:
                    v = int(i.op_str, 0)
                except ValueError:
                    v = None          # register / memory push (the float default slot)
                pushes.append(v)
                if gs(v) and name is None:
                    name = gs(v)
            if i.mnemonic == 'mov' and i.op_str.startswith('ecx, 0x'):
                obj = int(i.op_str.split(',')[1], 16)
            if i.mnemonic == 'call' and i.op_str == hex(CTOR):
                style = 'call'
        if style == 'call' and name:
            # arguments right-to-left: p4, p3, default (imm push, or `push ecx` + fld1/fldz + fstp [esp]), name
            ni = [k for k, v in enumerate(pushes) if gs(v)][0]
            if ni >= 1 and pushes[ni - 1] is not None:
                dflt = f32(pushes[ni - 1] & 0xffffffff)
            elif any(i.mnemonic == 'fld1' for i in ins):
                dflt = 1.0
            elif any(i.mnemonic == 'fldz' for i in ins):
                dflt = 0.0
            else:
                for i in ins:       # fld dword ptr [rdata float constant]
                    if i.mnemonic == 'fld' and 'dword ptr [0x1004' in i.op_str:
                        va = int(i.op_str.split('0x')[1].split(']')[0], 16)
                        dflt = f32(struct.unpack('<I', pe.get_data(va - base, 4))[0])
        elif name is None:
            # inline style: mov [X], string ; X = obj + 0x14
            for i in ins:
                if i.mnemonic == 'mov' and 'dword ptr [0x' in i.op_str and i.op_str.endswith(tuple('0123456789abcdef')):
                    parts = i.op_str.split(',')
                    try:
                        v = int(parts[1], 16)
                    except Exception:
                        continue
                    if gs(v):
                        name = gs(v)
                        obj = int(parts[0].split('0x')[1].split(']')[0], 16) - 0x14
                        style = 'inline'
                        dflt = 0.0
                        for j in ins:
                            if j.mnemonic == 'fld1':
                                dflt = 1.0
                        break
        ip = fp = None
        if obj:
            for i in ins:
                if i.mnemonic == 'mov' and i.op_str.startswith('dword ptr [0x'):
                    parts = i.op_str.split(',')
                    try:
                        dst = int(parts[0].split('0x')[1].split(']')[0], 16)
                        val = int(parts[1], 0)
                    except Exception:
                        continue
                    if dst == obj + 0xc and val:
                        ip = val
                    if dst == obj + 0x10 and val:
                        fp = val
            if style == 'call':
                # push p4 (float mirror), push p3 (int mirror) come first
                nz = [v for v in pushes[:2] if v]
                if len(pushes) >= 2:
                    fp = pushes[0] or fp
                    ip = pushes[1] or ip
        out.append(dict(idx=idx, intptr=ip, floatptr=fp, entry=a, thunk=thunk, body=body, name=name, obj=obj, default=dflt, style=style,
                        size=fe - a))
    return out


if __name__ == '__main__':
    for r in extract():
        print('%3d %08x %08x %-22s %s %s %s ip=%s fp=%s' % (
            r['idx'], r['entry'], r['body'], r['name'], ('%08x' % r['obj']) if r['obj'] else '-', r['default'],
            r['style'], ('%08x' % r['intptr']) if r['intptr'] else '-', ('%08x' % r['floatptr']) if r['floatptr'] else '-'))
