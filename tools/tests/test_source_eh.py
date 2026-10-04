import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import coffedit as F
import source_eh


class Image:
    def __init__(self):
        self.mem = {}

    def put(self, va, data):
        for i, b in enumerate(data):
            self.mem[va + i] = b

    def read(self, va, size):
        try:
            return bytes(self.mem[va + i] for i in range(size))
        except KeyError as exc:
            raise ValueError('unmapped fixture address %x' % exc.args[0])


def coff_with_root(helper_data=b'HELP', helper_relocs=(), xdata_data=b'XDAT', xdata_relocs=()):
    c = F.Coff()
    # Root .text: a DIR32 relocation to the helper section.
    c.sections.append(F.Sec('.text', b'\0\0\0\0', [[0, 0, source_eh.RELOC_DIR32]], F.SCN_CNT_CODE))
    c.sections.append(F.Sec('.text$x', helper_data, list(helper_relocs), F.SCN_CNT_CODE | F.SCN_LNK_COMDAT))
    c.sections.append(F.Sec('.xdata$x', xdata_data, list(xdata_relocs), F.SCN_CNT_INIT | F.SCN_LNK_COMDAT))
    c.syms.extend([
        F.Sym('helper', 0, 2, 0, F.CLS_EXTERNAL),  # relocation 0 target
        F.Sym('parent', 0, 1, 0x20, F.CLS_EXTERNAL),
        F.Sym('xdata', 0, 3, 0, F.CLS_STATIC),
    ])
    # COFF relocation symbol indices above: root's index 0 is helper.
    return c


def root():
    return [{'symbol': 'parent', 'va': 0x1000, 'size': 4}]


class SourceEHTests(unittest.TestCase):
    def test_root_anchors_identical_helpers_without_byte_search(self):
        c = coff_with_root()
        c.sections[0].data = b'\0' * 8
        c.sections[0].relocs.append([4, 3, source_eh.RELOC_DIR32])
        c.syms.append(F.Sym('ordinary_unknown', 0, 0, 0, F.CLS_EXTERNAL))
        image = Image()
        image.put(0x1000, (0x2000).to_bytes(4, 'little') + (0x5555).to_bytes(4, 'little'))
        image.put(0x2000, b'HELP')
        image.put(0x3000, b'HELP')

        result = source_eh.verify_unit(c, [{'symbol': 'parent', 'va': 0x1000, 'size': 8}], image, {})

        self.assertEqual(0x2000, result['sections']['2']['anchor'])
        self.assertEqual('MATCH', result['sections']['2']['status'])
        self.assertEqual('UNPLACED', result['sections']['3']['status'])
        self.assertEqual('MATCH', result['roots'][0]['status'])
        self.assertEqual(0, result['counts']['unverified_relocations'])
        self.assertEqual([], result['unverified'])

    def test_nonrelocation_mismatch_blocks_local_xdata_propagation(self):
        # Helper relocation at +1 points to xdata; first byte intentionally differs.
        helper = b'H\0\0\0\0'
        c = coff_with_root(helper, [[1, 2, source_eh.RELOC_DIR32]])
        image = Image()
        image.put(0x1000, (0x2000).to_bytes(4, 'little'))
        image.put(0x2000, b'X' + (0x3000).to_bytes(4, 'little'))
        image.put(0x3000, b'XDAT')

        result = source_eh.verify_unit(c, root(), image, {})

        self.assertEqual('MISMATCH', result['sections']['2']['status'])
        self.assertIsNone(result['sections']['3']['anchor'])
        self.assertEqual('UNPLACED', result['sections']['3']['status'])
        self.assertEqual('MISMATCH', result['roots'][0]['status'])

    def test_wrong_named_external_relocation_fails_payload(self):
        c = coff_with_root(b'\0\0\0\0', [[0, 3, source_eh.RELOC_DIR32]])
        c.syms.append(F.Sym('named_api', 0, 0, 0, F.CLS_EXTERNAL))
        c.sections[1].relocs[0][1] = 3
        image = Image()
        image.put(0x1000, (0x2000).to_bytes(4, 'little'))
        image.put(0x2000, (0x4444).to_bytes(4, 'little'))

        result = source_eh.verify_unit(c, root(), image, {'named_api': 0x5555})

        self.assertEqual('MISMATCH', result['sections']['2']['status'])
        self.assertEqual('MISMATCH', result['roots'][0]['status'])
        self.assertTrue(any(e['kind'] == 'relocation_target' for e in result['errors']))

    def test_unknown_parent_upgrades_to_mismatch_when_dependency_fails_later(self):
        c = coff_with_root(b'\0' * 8, [[0, 2, source_eh.RELOC_DIR32], [4, 3, source_eh.RELOC_DIR32]],
                           b'\0' * 4, [[0, 4, source_eh.RELOC_DIR32]])
        c.syms.extend([F.Sym('unknown', 0, 0, 0, F.CLS_EXTERNAL),
                       F.Sym('known_but_wrong', 0, 0, 0, F.CLS_EXTERNAL)])
        c.sections[1].relocs[1][1] = 3
        c.sections[2].relocs[0][1] = 4
        image = Image()
        image.put(0x1000, (0x2000).to_bytes(4, 'little'))
        image.put(0x2000, (0x4444).to_bytes(4, 'little') + (0x3000).to_bytes(4, 'little'))
        image.put(0x3000, (0x5555).to_bytes(4, 'little'))

        result = source_eh.verify_unit(c, root(), image, {'known_but_wrong': 0x6666})

        self.assertEqual('MISMATCH', result['sections']['2']['status'])
        self.assertEqual('MISMATCH', result['sections']['3']['status'])
        self.assertEqual('MISMATCH', result['roots'][0]['status'])

    def test_valid_child_of_bad_helper_is_not_counted_as_reachable(self):
        c = coff_with_root(b'\0' * 8, [[0, 2, source_eh.RELOC_DIR32], [4, 3, source_eh.RELOC_DIR32]],
                           b'\0' * 4, [[0, 4, source_eh.RELOC_DIR32]])
        c.syms.extend([F.Sym('xdata_api', 0, 0, 0, F.CLS_EXTERNAL),
                       F.Sym('text_api', 0, 0, 0, F.CLS_EXTERNAL)])
        c.sections[1].relocs[1][1] = 4
        c.sections[2].relocs[0][1] = 3
        image = Image()
        image.put(0x1000, (0x2000).to_bytes(4, 'little'))
        image.put(0x2000, (0x3000).to_bytes(4, 'little') + (0x7777).to_bytes(4, 'little'))
        image.put(0x3000, (0x4444).to_bytes(4, 'little'))

        result = source_eh.verify_unit(c, root(), image, {'xdata_api': 0x4444, 'text_api': 0x8888})

        self.assertEqual('MISMATCH', result['sections']['2']['status'])
        self.assertEqual('MATCH', result['sections']['3']['status'])
        self.assertFalse(result['sections']['3']['reachable_from_match_root'])
        self.assertEqual(0, result['counts']['xdata_sections'])

    def test_malformed_relocation_fails_closed_and_plain_root_unknown_is_informational(self):
        c = coff_with_root(b'ABCD', [[3, 2, source_eh.RELOC_DIR32]])
        image = Image()
        image.put(0x1000, (0x2000).to_bytes(4, 'little'))

        result = source_eh.verify_unit(c, root(), image, {})

        self.assertEqual('UNVERIFIED', result['sections']['2']['status'])
        self.assertEqual('UNVERIFIED', result['roots'][0]['status'])
        self.assertTrue(any(e['kind'] == 'malformed_relocation' for e in result['errors']))

        plain = F.Coff()
        plain.sections.append(F.Sec('.text', b'\0' * 4, [[0, 0, source_eh.RELOC_DIR32]], F.SCN_CNT_CODE))
        plain.sections.append(F.Sec('.text$x', b'HELP', [], F.SCN_CNT_CODE | F.SCN_LNK_COMDAT))
        plain.syms.extend([F.Sym('unknown', 0, 0, 0, F.CLS_EXTERNAL),
                           F.Sym('plain_root', 0, 1, 0x20, F.CLS_EXTERNAL)])
        plain_image = Image()
        plain_image.put(0x1000, (0x5555).to_bytes(4, 'little'))
        plain_result = source_eh.verify_unit(plain,
            [{'symbol': 'plain_root', 'va': 0x1000, 'size': 4}], plain_image, {})
        self.assertEqual('NO_REACHED_EH', plain_result['status'])
        self.assertEqual([], plain_result['roots'])
        self.assertEqual(0, plain_result['counts']['unverified_relocations'])
        self.assertEqual([], plain_result['unverified'])


class FunctionAliasTests(unittest.TestCase):
    def setUp(self):
        self.unit = F.Coff()
        self.unit.sections.extend([
            F.Sec('.text$x', b'HELP', [], F.SCN_CNT_CODE | F.SCN_LNK_COMDAT),
            F.Sec('.text', b'FUNC', [], F.SCN_CNT_CODE),
        ])
        self.unit.syms.append(F.Sym('??_GAlias@@UAE@XZ', 0, 2, 0x20, F.CLS_EXTERNAL))
        self.obj_symbol = SimpleNamespace(name='??_GAlias@@UAE@XZ', secno=2, is_section_symbol=False,
                                          is_function=True)
        self.obj = SimpleNamespace(symbols={0: self.obj_symbol})
        self.record = {'section': 1, 'offset': 3, 'symbol': self.unit.syms[0].name,
                       'symbol_index': 0, 'reason': 'unmapped_defined_symbol',
                       'actual_va': 0x3000, 'payload_passed': True}
        self.canonical = {0x3000: [{'unit': 'canonical/unit', 'function': 'AliasCanonical',
                                    'symbol': '?AliasCanonical@@'}]}

    def test_alias_requires_match_root_and_strict_fully_named_validation(self):
        calls = []

        def validate(sym, va, roots):
            calls.append((sym.name, va, roots))
            return SimpleNamespace(status='MATCH', unverified=[])

        aliases, evidence, conflicts = source_eh.resolve_function_aliases(
            self.unit, {'unverified': [self.record]}, self.obj, self.canonical, validate)

        self.assertEqual({'??_GAlias@@UAE@XZ': 0x3000}, aliases)
        self.assertEqual(1, len(calls))
        self.assertEqual('MATCH', evidence[0]['status'])
        self.assertEqual([], conflicts)

        calls.clear()
        no_root, _, _ = source_eh.resolve_function_aliases(
            self.unit, {'unverified': [self.record]}, self.obj, {}, validate)
        self.assertEqual({}, no_root)
        self.assertEqual([], calls)

        unknown, failed_evidence, _ = source_eh.resolve_function_aliases(
            self.unit, {'unverified': [self.record]}, self.obj, self.canonical,
            lambda *_: SimpleNamespace(status='MATCH', unverified=[(0x4000, 'unknown')]))
        self.assertEqual({}, unknown)
        self.assertEqual('REJECTED', failed_evidence[0]['status'])

    def test_contradictory_candidate_vas_fail_closed(self):
        other = dict(self.record, offset=8, actual_va=0x4000)
        canonical = {**self.canonical, 0x4000: [{'unit': 'another/unit', 'function': 'Other',
                                                 'symbol': '?Other@@'}]}
        calls = []
        aliases, evidence, conflicts = source_eh.resolve_function_aliases(
            self.unit, {'unverified': [self.record, other]}, self.obj, canonical,
            lambda *args: calls.append(args) or SimpleNamespace(status='MATCH', unverified=[]))

        self.assertEqual({}, aliases)
        self.assertEqual([], calls)
        self.assertEqual('CONFLICT', conflicts[0]['status'])
        self.assertEqual({0x3000, 0x4000}, set(conflicts[0]['candidate_vas']))

    def test_contradictory_root_anchors_are_rejected(self):
        c = F.Coff()
        c.sections.extend([
            F.Sec('.text', b'\0' * 8, [[0, 0, source_eh.RELOC_DIR32], [4, 1, source_eh.RELOC_DIR32]], F.SCN_CNT_CODE),
            F.Sec('.text$x', b'ABCDEFGH', [], F.SCN_CNT_CODE | F.SCN_LNK_COMDAT),
        ])
        c.syms.extend([F.Sym('h0', 0, 2, 0, F.CLS_EXTERNAL),
                       F.Sym('h4', 4, 2, 0, F.CLS_EXTERNAL),
                       F.Sym('parent', 0, 1, 0x20, F.CLS_EXTERNAL)])
        image = Image()
        image.put(0x1000, (0x2000).to_bytes(4, 'little') + (0x3004).to_bytes(4, 'little'))
        image.put(0x2000, b'ABCDEFGH')

        result = source_eh.verify_unit(c, [{'symbol': 'parent', 'va': 0x1000, 'size': 8}], image, {})

        self.assertEqual('CONFLICT', result['sections']['2']['status'])
        self.assertEqual('MISMATCH', result['roots'][0]['status'])
        self.assertTrue(any(e['kind'] == 'anchor_conflict' for e in result['errors']))

    def test_unrooted_eh_sections_are_ignored(self):
        c = coff_with_root()
        image = Image()
        image.put(0x1000, (0x2000).to_bytes(4, 'little'))
        image.put(0x2000, b'HELP')

        result = source_eh.verify_unit(c, [], image, {})

        self.assertIsNone(result['sections']['2']['anchor'])
        self.assertEqual('UNPLACED', result['sections']['2']['status'])
        self.assertEqual(0, result['counts']['anchored_sections'])

    def test_unknown_external_taints_reached_helper_and_root(self):
        c = coff_with_root(b'\0\0\0\0', [[0, 2, source_eh.RELOC_DIR32]], b'\0\0\0\0')
        c.syms.append(F.Sym('mystery', 0, 0, 0, F.CLS_EXTERNAL))
        c.sections[2].relocs.append([0, 3, source_eh.RELOC_DIR32])
        image = Image()
        image.put(0x1000, (0x2000).to_bytes(4, 'little'))
        image.put(0x2000, (0x3000).to_bytes(4, 'little'))
        image.put(0x3000, (0x4444).to_bytes(4, 'little'))

        result = source_eh.verify_unit(c, root(), image, {})

        self.assertEqual('UNVERIFIED', result['sections']['2']['status'])
        self.assertEqual('UNVERIFIED', result['sections']['3']['status'])
        self.assertEqual('UNVERIFIED', result['roots'][0]['status'])
        self.assertEqual(1, result['counts']['unverified_relocations'])


if __name__ == '__main__':
    unittest.main()
