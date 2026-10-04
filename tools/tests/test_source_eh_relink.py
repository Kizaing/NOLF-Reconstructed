import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import coffedit as F
from relink import subset_sections
import source_eh_relink as native


class SparseImage:
    def __init__(self):
        self.mem = {}

    def put(self, va, data):
        for offset, byte in enumerate(data):
            self.mem[va + offset] = byte

    def read(self, va, size):
        try:
            return bytes(self.mem[va + offset] for offset in range(size))
        except KeyError as exc:
            raise ValueError('unmapped fixture address %08x' % exc.args[0])


class SourceEHRelinkTests(unittest.TestCase):
    unit = 'lithshared/rezmgr/rezmgr'

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.map_path = Path(self.temp.name) / 'fixture.map'

    def make_output_fixture(self):
        text_anchor = native.TEXT_START
        xdata_anchor = native.XDATA_START
        text_alias = '__source_eh_text_fixture_1'
        xdata_alias = '__source_eh_xdata_fixture_2'
        text_target = 'callee_alias'
        data_target = 'table_alias'
        text_sec = F.Sec('.text$xfixture', b'\0' * 4 + b'CODE',
                         [[0, 0, 0x14]], F.SCN_CNT_CODE)
        xdata_sec = F.Sec('.xdata$xfixture', b'\0' * 4 + b'META',
                          [[0, 1, 0x06]], F.SCN_CNT_INIT)
        text_ent = {
            'unit': self.unit, 'secno': 2, 'anchor': text_anchor,
            'size': len(text_sec.data), 'start_alias': text_alias,
            'section': text_sec, 'name': text_sec.name, 'parent': 1,
            'relocations': {0: {'type': 0x14, 'target_name': text_target,
                                'target_va': 0x004D2000, 'addend': 0}},
        }
        xdata_ent = {
            'unit': self.unit, 'secno': 3, 'anchor': xdata_anchor,
            'size': len(xdata_sec.data), 'start_alias': xdata_alias,
            'section': xdata_sec, 'name': xdata_sec.name, 'parent': 1,
            'relocations': {0: {'type': 0x06, 'target_name': data_target,
                                'target_va': 0x004D3000, 'addend': 4}},
        }

        plan = native.SourceEHPlan.__new__(native.SourceEHPlan)
        plan.text_sections = [text_ent]
        plan.xdata_sections = [xdata_ent]
        plan.text_gaps = []
        plan.units = {self.unit: {
            'sections': {2: text_ent, 3: xdata_ent},
            'output_section_numbers': {1: 1, 2: 2, 3: 3},
            'root_relocations': [],
        }}

        linked = F.Coff()
        linked.sections = [
            F.Sec('.text.parent', b'PARENT', [], F.SCN_CNT_CODE),
            F.Sec(text_sec.name, text_sec.data, [[0, 0, 0x14]], text_sec.flags),
            F.Sec(xdata_sec.name, xdata_sec.data, [[0, 1, 0x06]], xdata_sec.flags),
        ]
        linked.syms = [
            F.Sym('.text$xfixture', 0, 2, 0, F.CLS_STATIC,
                  F.section_aux(len(text_sec.data), 1, selection=5, number=1)),
            F.Sym('.xdata$xfixture', 0, 3, 0, F.CLS_STATIC,
                  F.section_aux(len(xdata_sec.data), 1, selection=5, number=1)),
            F.Sym(text_target, 0, 0, 0, F.CLS_EXTERNAL),
            F.Sym(data_target, 0, 0, 0, F.CLS_EXTERNAL),
        ]
        linked.sections[1].relocs[0][1] = 2
        linked.sections[2].relocs[0][1] = 3
        prep = type('Prep', (), {'base': {self.unit: (linked, {})}})()

        # These map addresses intentionally differ from any source-image target
        # address. LINK may move an ordinary target while retaining its alias.
        actual_text_target = 0x00512347
        actual_data_target = 0x00623458
        entries = [
            (text_alias, text_anchor),
            (xdata_alias, xdata_anchor),
            (text_target, actual_text_target),
            (data_target, actual_data_target),
        ]
        self.map_path.write_text(
            ''.join(' 0001:%08X %s %08X\n' % (va, name, va) for name, va in entries),
            encoding='latin1')

        image = SparseImage()
        text_disp = (actual_text_target - (text_anchor + 4)) & 0xFFFFFFFF
        data_value = (actual_data_target + 4) & 0xFFFFFFFF
        image.put(text_anchor, text_disp.to_bytes(4, 'little') + b'CODE')
        image.put(xdata_anchor, data_value.to_bytes(4, 'little') + b'META')
        return plan, image, prep, linked, text_ent, xdata_ent

    def verify(self, plan, image, prep):
        with patch('mktarget._image', return_value=image):
            plan.verify_linked_output('fixture.exe', str(self.map_path), prep)

    def test_enabled_uses_only_default_mixed_own_data_gate(self):
        self.assertTrue(native.enabled('mixed', '', False, None))
        self.assertFalse(native.enabled('targets', '', False, None))
        self.assertFalse(native.enabled('mixed', 'client/cnet', False, None))
        self.assertFalse(native.enabled('mixed', '', True, None))
        self.assertFalse(native.enabled('mixed', '', False, 'client/cnet'))

    def test_linked_name_resolution_requires_one_exact_map_address(self):
        plan = native.SourceEHPlan.__new__(native.SourceEHPlan)
        self.assertEqual(plan._require_linked_names({'alias': {0x1234}}, ['alias']),
                         {'alias': 0x1234})
        with self.assertRaisesRegex(ValueError, 'resolves to 2 addresses'):
            plan._require_linked_names({'alias': {0x1234, 0x5678}}, ['alias'])
        with self.assertRaisesRegex(ValueError, 'resolves to 0 addresses'):
            plan._require_linked_names({}, ['alias'])

    def test_output_checks_helper_and_metadata_anchors_and_actual_map_targets(self):
        plan, image, prep, _, _, _ = self.make_output_fixture()

        self.verify(plan, image, prep)

    def test_output_rejects_helper_or_metadata_start_alias_at_wrong_anchor(self):
        plan, image, prep, _, _, _ = self.make_output_fixture()
        self.map_path.write_text(
            ' 0001:%08X %s %08X\n 0001:%08X %s %08X\n'
            ' 0001:00512347 callee_alias 00512347\n'
            ' 0001:00623458 table_alias 00623458\n' % (
                native.TEXT_START + 1, plan.text_sections[0]['start_alias'], native.TEXT_START + 1,
                native.XDATA_START, plan.xdata_sections[0]['start_alias'], native.XDATA_START),
            encoding='latin1')

        with self.assertRaisesRegex(ValueError, 'resolves to 1 addresses|expected'):
            self.verify(plan, image, prep)

    def test_output_rejects_dropped_relocation(self):
        plan, image, prep, linked, _, _ = self.make_output_fixture()
        linked.sections[1].relocs.clear()

        with self.assertRaisesRegex(ValueError, 'relocation'):
            self.verify(plan, image, prep)

    def test_output_rejects_changed_relocation_offset_or_type(self):
        plan, image, prep, linked, _, _ = self.make_output_fixture()
        linked.sections[1].relocs[:] = [[1, 2, 0x14]]
        with self.assertRaisesRegex(ValueError, 'relocation'):
            self.verify(plan, image, prep)

    def test_output_rejects_corrupted_helper_and_root_operands(self):
        with self.subTest(relocation='helper'):
            plan, image, prep, _, _, _ = self.make_output_fixture()
            original = int.from_bytes(image.read(native.TEXT_START, 4), 'little')
            image.put(native.TEXT_START, ((original + 1) & 0xFFFFFFFF).to_bytes(4, 'little'))

            with self.assertRaisesRegex(ValueError, 'relocation.*differs'):
                self.verify(plan, image, prep)

        with self.subTest(relocation='root'):
            plan, image, prep, _, _, _ = self.make_output_fixture()
            unit = plan.units[self.unit]
            linked = prep.base[self.unit][0]
            linked.sections.append(F.Sec('.text', b'\0' * 4, [[0, 0, 0x06]], F.SCN_CNT_CODE))
            unit['output_section_numbers'][4] = 4
            unit['root_relocations'] = [{
                'symbol': 'root_fn', 'va': 0x00401000, 'section': 4,
                'symbol_value': 0, 'size': 4, 'offset': 0, 'type': 0x06,
                'target_name': 'root_helper_target', 'target_sec': 2, 'addend': 0,
            }]
            function_va = 0x00402010
            target_va = native.TEXT_START
            unit['root_link_names'] = {('root_fn', 0x00401000): 'root_fn_map'}
            with self.map_path.open('a', encoding='latin1') as map_file:
                map_file.write(' 0001:00402010 root_fn_map 00402010\n')
                map_file.write(' 0001:%08X root_helper_target %08X\n' %
                               (target_va, target_va))
            image.put(function_va, target_va.to_bytes(4, 'little'))
            self.verify(plan, image, prep)

            image.put(function_va, (target_va + 1).to_bytes(4, 'little'))
            with self.assertRaisesRegex(ValueError, 'root_fn relocation.*differs'):
                self.verify(plan, image, prep)

    def test_output_rejects_absolute_symbol_value_changed_after_verification(self):
        plan, image, prep, linked, text_ent, _ = self.make_output_fixture()
        absolute_va = 0x00512347
        target = linked.syms[2]
        target.sec = -1
        target.value = absolute_va
        text_ent['relocations'][0].update({
            'symbol_sec': -1, 'symbol_value': absolute_va,
            'target_va': absolute_va, 'target_name': None,
        })
        self.verify(plan, image, prep)

        target.value += 1
        with self.assertRaisesRegex(ValueError, 'absolute relocation.*changed its COFF symbol value'):
            self.verify(plan, image, prep)

        plan, image, prep, linked, _, _ = self.make_output_fixture()
        linked.sections[1].relocs[:] = [[0, 2, 0x06]]
        with self.assertRaisesRegex(ValueError, 'relocation'):
            self.verify(plan, image, prep)

    def test_output_rejects_non_int3_padding(self):
        plan, image, prep, _, _, _ = self.make_output_fixture()
        gap = (native.TEXT_START + 8, native.TEXT_START + 10)
        image.put(gap[0], b'\xCC\xCC')
        plan.text_gaps = [gap]
        self.verify(plan, image, prep)

        image.put(gap[0], b'\xCC\x00')
        with self.assertRaisesRegex(ValueError, 'padding'):
            self.verify(plan, image, prep)

    def test_root_aliases_must_resolve_and_function_names_cannot_conflict(self):
        plan, image, prep, _, _, _ = self.make_output_fixture()
        unit = plan.units[self.unit]
        linked = prep.base[self.unit][0]
        linked.sections.append(F.Sec('.text', b'\0' * 4, [[0, 0, 0x06]], F.SCN_CNT_CODE))
        unit['output_section_numbers'][4] = 4
        unit['root_relocations'] = [{
            'symbol': 'root_fn', 'va': 0x00401000, 'section': 4,
            'symbol_value': 0, 'size': 4, 'offset': 0, 'type': 0x06,
            'target_name': 'root_helper_target', 'target_sec': 2, 'addend': 0,
        }]
        unit['root_link_names'] = {('root_fn', 0x00401000): 'root_fn_map'}
        root_function_va = 0x00402010
        root_target_va = native.TEXT_START
        with self.map_path.open('a', encoding='latin1') as map_file:
            map_file.write(' 0001:00402010 root_fn_map 00402010\n')
        image.put(root_function_va, root_target_va.to_bytes(4, 'little'))
        with self.assertRaisesRegex(ValueError, 'root_helper_target'):
            self.verify(plan, image, prep)

        with self.map_path.open('a', encoding='latin1') as map_file:
            map_file.write(' 0001:%08X root_helper_target %08X\n' %
                           (root_target_va, root_target_va))
        self.verify(plan, image, prep)

        unit['roots'] = [{'symbol': 'root_fn', 'va': 0x00401000}]
        plan.record_function_symbol(self.unit, 'root_fn', 0x00401000, 'root_fn_map')
        self.assertEqual(unit['root_link_names'][('root_fn', 0x00401000)], 'root_fn_map')
        with self.assertRaisesRegex(ValueError, 'conflicting linked names'):
            plan.record_function_symbol(self.unit, 'root_fn', 0x00401000, 'other_linked_root')

    def test_remove_tail_masks_linked_relocation_bytes_and_rebinds_each_object(self):
        tail_start, tail_end = 0x1000, 0x1008
        original = SparseImage()
        # The first word is the linked relocation result. The COFF inputs below
        # retain different addends there, while the nonrelocation bytes agree.
        original.put(tail_start, b'\x00\x50\x00\x00TAIL')

        def target_object(raw_addend, tail_va, with_decoy=False):
            coff = F.Coff()
            coff.sections = [
                F.Sec('.text.caller', b'\0' * 4, [[0, 0, 0x14]], F.SCN_CNT_CODE),
                F.Sec('.text$xhelper', raw_addend.to_bytes(4, 'little'),
                      [[0, 1, 0x06]], F.SCN_CNT_CODE),
                F.Sec('.text.keep', b'KEEP', [], F.SCN_CNT_CODE),
            ]
            coff.syms = [
                F.Sym('tail_function', 1, 2, 0, F.CLS_EXTERNAL),
                F.Sym('tail_destination', 0, 3, 0, F.CLS_EXTERNAL),
                F.Sym('kept_function', 0, 3, 0, F.CLS_EXTERNAL),
            ]
            if with_decoy:
                # The earlier object's newly appended symbol index collides
                # with this unrelated symbol unless indices are object-local.
                coff.syms.append(F.Sym('wrong_external', 0, 0, 0, F.CLS_EXTERNAL))
            return coff

        objs = {
            'target_a': target_object(0x11111111, tail_start),
            'target_b': target_object(0x22222222, tail_start + 4, with_decoy=True),
        }
        vas = {
            'target_a': [0x800, tail_start, 0x5000],
            'target_b': [0x900, tail_start + 4, 0x6000],
        }
        plan = native.SourceEHPlan.__new__(native.SourceEHPlan)
        plan.text_start, plan.text_end = tail_start, tail_end
        plan.text_sections = [
            {'anchor': tail_start, 'size': 4, 'alignment': 1},
            {'anchor': tail_start + 4, 'size': 4, 'alignment': 1},
        ]
        plan.xdata_sections = []

        prep = SimpleNamespace(
            orig=SimpleNamespace(img=original, text_lo=tail_start, text_hi=tail_end),
            objs=objs, objvas=vas,
            objsym={name: {} for name in objs},
            unit_of={name: 'fixture' for name in objs},
            orig_vas={name: list(vas[name]) for name in objs},
            order=[(0x800, 'target_a'), (0x800, 'target_b')],
            leader={(name, secno): '%s-section-%d' % (name, secno)
                    for name in objs for secno in (1, 2, 3)},
            text_name={tail_start + 1: 'tail_a_interior_alias',
                       tail_start + 5: 'tail_b_interior_alias'},
            data_name={}, used_names=set(),
        )
        prep.sections = lambda name: [
            (va, secno, len(objs[name].sections[secno - 1].data))
            for secno, va in enumerate(vas[name], 1)
        ]

        plan.remove_target_tail(prep, subset_sections)

        self.assertEqual(prep.source_eh_text_gaps, [])
        for name in objs:
            retained = prep.objs[name]
            self.assertEqual([sec.name for sec in retained.sections],
                             ['.text.caller', '.text.keep'])
            expected_vas = [0x800, 0x5000] if name == 'target_a' else [0x900, 0x6000]
            self.assertEqual(prep.objvas[name], expected_vas)
            self.assertEqual(prep.leader[(name, 2)], '%s-section-3' % name)
            self.assertNotIn((name, 3), prep.leader)

            relocs = retained.sections[0].relocs
            self.assertEqual([(off, typ) for off, _si, typ in relocs], [(0, 0x14)])
            target = retained.syms[relocs[0][1]]
            expected_alias = ('tail_a_interior_alias' if name == 'target_a'
                              else 'tail_b_interior_alias')
            self.assertEqual((target.name, target.sec, target.cls),
                             (expected_alias, 0, F.CLS_EXTERNAL))
            kept = next(s for s in retained.syms if s is not None and s.name == 'kept_function')
            self.assertEqual(kept.sec, 2)


if __name__ == '__main__':
    unittest.main()
