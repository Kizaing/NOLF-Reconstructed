import struct
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import coffedit as F
import relink
import relink_data as RD


class FixtureImage:
    def __init__(self, reads=None):
        self.reads = reads or {}

    def read(self, va, size):
        if va == RD.CRT_LO and size == RD.CRT_HI - RD.CRT_LO:
            return b'\0' * size
        data = self.reads.get(va)
        if data is None or len(data) < size:
            raise ValueError('read outside fixture')
        return data[:size]


def data_flags(comdat=False, section='.data'):
    flags = F.SCN_CNT_INIT | F.SCN_ALIGN[4] | F.SCN_MEM_READ
    if section == '.data':
        flags |= F.SCN_MEM_WRITE
    return flags | (F.SCN_LNK_COMDAT if comdat else 0)


def one_data_symbol(name, cls=F.CLS_STATIC, section='.data', comdat=False):
    coff = F.Coff()
    coff.sections.append(F.Sec(section, b'\0' * 4, [], data_flags(comdat, section)))
    coff.syms.append(F.Sym(name, 0, 1, 0, cls))
    return coff


class ExplicitDataAnchorTests(unittest.TestCase):
    def test_relink_handoff_ignores_undefined_global_externs(self):
        coff = F.Coff()
        coff.sections.append(F.Sec('.data', b'\0' * 4, [], data_flags()))
        coff.syms.extend([
            F.Sym('defined_pointer', 0, 1, 0, F.CLS_STATIC),
            F.Sym('external_only', 0, 0, 0, F.CLS_EXTERNAL),
        ])
        unit = SimpleNamespace(name='unit', annots=[
            SimpleNamespace(kind='GLOBAL', symbol='defined_pointer', va=0x4CF200),
            SimpleNamespace(kind='GLOBAL', symbol='external_only', va=0x4CF204),
        ])

        self.assertEqual({'defined_pointer': 0x4CF200}, relink.unit_data_anchors(unit, coff))

    def test_explicit_static_and_comdat_symbols_are_anchored(self):
        coff = F.Coff()
        coff.sections.extend([
            F.Sec('.data', b'\0' * 4, [], data_flags()),
            F.Sec('.rdata', b'lit\0', [], data_flags(comdat=True, section='.rdata')),
        ])
        coff.syms.extend([
            F.Sym('static_pointer', 0, 1, 0, F.CLS_STATIC),
            F.Sym('literal_comdat', 0, 2, 0, F.CLS_EXTERNAL),
        ])
        obj = RD.Obj('unit', coff, {}, 'full', {
            'static_pointer': 0x4CF200,
            'literal_comdat': 0x4C6500,
        })

        obj.locate(FixtureImage())

        self.assertEqual({1: 0x4CF200, 2: 0x4C6500}, obj.secva)
        self.assertEqual(2, len(obj.named))

    def test_static_names_are_scoped_per_object(self):
        name = '?___bdefs__pFnName@ReadString'
        first = RD.Obj('first', one_data_symbol(name), {}, 'full', {name: 0x4CF200})
        second = RD.Obj('second', one_data_symbol(name), {}, 'full', {name: 0x4CF300})

        first.locate(FixtureImage())
        second.locate(FixtureImage())

        self.assertEqual(0x4CF200, first.secva[1])
        self.assertEqual(0x4CF300, second.secva[1])

    def test_unspecified_static_and_comdat_symbols_are_not_named_by_fallback(self):
        static = one_data_symbol('static_unanchored')
        comdat = one_data_symbol('comdat_unanchored', F.CLS_EXTERNAL, '.rdata', comdat=True)
        img = FixtureImage()

        static_obj = RD.Obj('static', static, {}, 'full')
        comdat_obj = RD.Obj('comdat', comdat, {}, 'full')
        static_obj.locate(img, lambda _: 0x4CF210)
        comdat_obj.locate(img, lambda _: 0x4C6510)
        self.assertEqual({}, static_obj.secva)
        self.assertEqual({}, comdat_obj.secva)

    def test_code_relocation_contradicting_explicit_anchor_is_reported(self):
        coff = F.Coff()
        coff.sections.extend([
            F.Sec('.text', struct.pack('<I', 4), [[0, 1, 6]],
                  F.SCN_CNT_CODE | F.SCN_CNT_INIT | F.SCN_ALIGN[4] | F.SCN_MEM_EXECUTE | F.SCN_MEM_READ),
            F.Sec('.data', b'\0' * 4, [], data_flags()),
        ])
        coff.syms.extend([
            F.Sym('function', 0, 1, 0x20, F.CLS_EXTERNAL),
            F.Sym('static_pointer', 0, 2, 0, F.CLS_STATIC),
        ])
        img = FixtureImage({0x401000: struct.pack('<I', 0x4CF304)})
        obj = RD.Obj('unit', coff, {'function': 0x401000}, 'full', {'static_pointer': 0x4CF200})

        obj.locate(img)

        self.assertEqual(0x4CF200, obj.secva[2])
        self.assertEqual([(1, 0, 2, 0x4CF200, 0x4CF300, 'static_pointer')], obj.conflicts)

    def test_cross_region_anchor_is_kept_but_chain_reports_section_mismatch(self):
        obj = RD.Obj('unit', one_data_symbol('static_pointer'), {}, 'full', {'static_pointer': 0x4C6500})

        obj.locate(FixtureImage())
        chain = RD.build_chain(FixtureImage(), obj, '.data')

        self.assertEqual(0x4C6500, obj.secva[1])
        self.assertTrue(any(kind == 'section' for kind, _ in chain.issues))
        self.assertIn('at 004c6500 (.rdata)', chain.issues[0][1])

    def test_collision_constant_anchor_preserves_address_and_reports_mismatch(self):
        data = bytes.fromhex('cdcccc3d')
        coff = F.Coff()
        coff.sections.append(F.Sec('.rdata', data, [], data_flags(section='.rdata')))
        coff.syms.append(F.Sym('_g_fStairMinNormalY', 0, 1, 0, F.CLS_STATIC))
        va = 0x4D0804
        img = FixtureImage({va: data})
        obj = RD.Obj('shared/collision', coff, {}, 'full', {'_g_fStairMinNormalY': va})

        obj.locate(img)
        chain = RD.build_chain(img, obj, '.rdata')

        self.assertEqual('.data', RD.region_of(va))
        self.assertEqual(va, obj.secva[1])
        self.assertEqual([(1, coff.sections[0])], obj.data_sections('.rdata'))
        self.assertTrue(any(kind == 'section' for kind, _ in chain.issues))
        self.assertIn('at 004d0804 (.data)', chain.issues[0][1])

    def test_explicit_anchor_in_code_section_is_rejected(self):
        coff = F.Coff()
        coff.sections.append(F.Sec('.text', b'\xc3', [],
                                   F.SCN_CNT_CODE | F.SCN_CNT_INIT | F.SCN_ALIGN[1] | F.SCN_MEM_EXECUTE))
        coff.syms.append(F.Sym('function', 0, 1, 0x20, F.CLS_EXTERNAL))
        obj = RD.Obj('unit', coff, {}, 'full', {'function': 0x4D0804})

        with self.assertRaisesRegex(ValueError, 'not defined in a data section'):
            obj.locate(FixtureImage())

    def test_explicit_anchor_outside_output_data_regions_is_rejected(self):
        obj = RD.Obj('unit', one_data_symbol('static_pointer'), {}, 'full', {'static_pointer': 0x500000})

        with self.assertRaisesRegex(ValueError, 'outside output data regions'):
            obj.locate(FixtureImage())


if __name__ == '__main__':
    unittest.main()
