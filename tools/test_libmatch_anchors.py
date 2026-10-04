"""Focused tests for independently anchored BSS symbol propagation in libmatch."""
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest

TOOLS = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS))

from coffobj import Section, Symbol, REL_DIR32  # noqa: E402
from libmatch import DSec, Sec, attribute, classify, resolve  # noqa: E402


class FakeImage:
    def __init__(self, lo=0x5000, data=bytes(0x100)):
        self.lo, self.data = lo, data

    def in_image(self, va):
        return self.lo <= va < self.lo + len(self.data)

    def read(self, va, n):
        if va < self.lo or va + n > self.lo + len(self.data):
            raise ValueError('outside fake image')
        return self.data[va - self.lo:va - self.lo + n]


class FakeExe:
    def __init__(self, data=bytes(0x100)):
        self.img = FakeImage(data=data)

    def read(self, va, n):
        return self.img.read(va, n)


class FakeObj:
    def __init__(self, oid, sec):
        self.id = oid
        self.obj = SimpleNamespace(sections=[sec], symbols={})
        self.dsecs = []
        self.secs = []
        self.lib = 'test'

    def key_of(self, sym):
        return ('E', sym.name), 0


class FakeCodeSection:
    next_va = 0x10001000

    def __init__(self, oid, relocations=(), definitions=()):
        self.ob = SimpleNamespace(id=oid, lib='test')
        self.secno, self.va = 1, FakeCodeSection.next_va
        FakeCodeSection.next_va += 0x100
        self.relocs = list(relocations) or list(definitions) or [object()]
        self.syms = [SimpleNamespace(name=n, value=v, cls=2) for n, v in definitions]
        self._relocations = list(relocations)
        self.data = b'code'

    def implied(self, exe, va, raw=None):
        out = [(('S', self.ob.id, self.secno), va)]
        out.extend((('E', n), va + v) for n, v in ((x.name, x.value) for x in self.syms))
        out.extend(self._relocations)
        return out


def make_bss(symbols=(('alpha', 4), ('beta', 12)), data=None):
    data = bytes(24) if data is None else data
    sec = Section(1, '.bss', data, [], 0x80)
    sec.syms = [Symbol(i, name, off, 1, 0, 2, 0) for i, (name, off) in enumerate(symbols)]
    ob = FakeObj('CRT/testdat', sec)
    d = DSec(ob, sec)
    ob.dsecs = [d]
    return ob, d


class LibmatchBssAnchorTests(unittest.TestCase):
    def test_attribution_keeps_stronger_local_owner_after_filtering(self):
        right = SimpleNamespace(id='test/right', lib='test', secs=[])
        wrong = SimpleNamespace(id='test/wrong', lib='test', secs=[])
        caller = FakeCodeSection(right.id, [(('S', right.id, 2), 0x2000)])
        caller.ob, caller.va, caller.cands = right, 0x1000, {0x1000: 'unique'}
        caller.status = 'unique'
        alternative = FakeCodeSection(right.id)
        alternative.ob, alternative.secno = right, 2
        alternative.va, alternative.cands, alternative.status = None, {0x2000: 'name'}, None
        initial = FakeCodeSection(wrong.id)
        initial.ob, initial.secno = wrong, 2
        initial.va, initial.cands, initial.status = 0x2000, {0x2000: 'name'}, 'name'
        right.secs, wrong.secs = [caller, alternative], [initial]

        accepted, _ = attribute([right, wrong], [caller, initial], FakeExe(), {})

        self.assertEqual(accepted, [caller, alternative])
        self.assertEqual((alternative.va, alternative.status), (0x2000, 'name'))
        self.assertEqual((initial.va, initial.status), (None, 'dup'))

    def test_candidate_resolution_cannot_learn_bss_fields_from_one_reference(self):
        ob, d = make_bss()
        ob.secs = []
        raw = bytes(32)
        section = Section(1, '.text', raw, [(1, 0, REL_DIR32)], 0x20)
        section.syms = []
        caller = FakeObj('test/caller', section)
        caller.objname = 'caller.obj'
        caller.obj.symbols = {0: Symbol(0, 'alpha', 0, 0, 0, 2, 0)}
        code = Sec(caller, section)
        caller.secs = [code]
        exe = FakeExe()
        exe.lo = 0x10001000
        exe.text = raw[:1] + (0x5004).to_bytes(4, 'little') + raw[5:] + bytes(32)
        exe.hi = exe.lo + len(exe.text)
        read_data = exe.read
        exe.read = lambda va, n: (exe.text[va - exe.lo:va - exe.lo + n]
                                  if exe.lo <= va and va + n <= exe.hi else read_data(va, n))
        code.cands = {exe.lo: 'name'}

        accepted, known, _ = resolve([caller, ob], exe, SimpleNamespace(funcs={exe.lo: None}), [])

        self.assertEqual(accepted, [code])
        self.assertEqual(known[('E', 'alpha')], 0x5004)
        self.assertNotIn(('E', 'beta'), known)
        self.assertIsNone(d.va)

    def test_agreeing_independent_anchors_publish_relative_definitions(self):
        ob, d = make_bss()
        code = [
            FakeCodeSection('code/a', [(('E', 'alpha'), 0x5004)]),
            FakeCodeSection('code/b', [(('E', 'alpha'), 0x5004)]),
            FakeCodeSection('code/c', [(('E', 'beta'), 0x500c)]),
        ]

        conflicts, placed, vals = classify([ob], code, FakeExe())

        self.assertEqual(conflicts, [])
        self.assertIn(d, placed)
        self.assertEqual(d.va, 0x5000)
        # beta is independently derived at section offset 12. Its one code use remains a
        # single observation in vals, so verification must come from the placed definition.
        self.assertEqual(vals[('E', 'beta')][0x500c], 1)
        self.assertEqual(code[2].vclass, 'verified')

    def test_repeated_references_in_one_code_section_are_not_independent_anchors(self):
        ob, d = make_bss(symbols=(('alpha', 4),))
        code = [FakeCodeSection('code/only', [
            (('E', 'alpha'), 0x5004), (('E', 'alpha'), 0x5004)])]

        _, placed, _ = classify([ob], code, FakeExe())

        self.assertNotIn(d, placed)
        self.assertIsNone(d.va)

    def test_folded_object_copies_at_one_address_are_one_anchor(self):
        ob, d = make_bss(symbols=(('alpha', 4), ('beta', 12)))
        a = FakeCodeSection('variant/a', [(('E', 'alpha'), 0x5004)])
        b = FakeCodeSection('variant/b', [(('E', 'alpha'), 0x5004)])
        b.va = a.va

        _, placed, _ = classify([ob], [a, b], FakeExe())

        self.assertNotIn(d, placed)
        self.assertIsNone(d.va)

    def test_different_fields_can_corroborate_one_section_base(self):
        ob, d = make_bss(symbols=(('alpha', 4), ('beta', 12), ('gamma', 16)))
        code = [FakeCodeSection('code/a', [(('E', 'alpha'), 0x5004)]),
                FakeCodeSection('code/b', [(('E', 'beta'), 0x500c)])]

        conflicts, placed, _ = classify([ob], code, FakeExe())

        self.assertEqual(conflicts, [])
        self.assertIn(d, placed)
        self.assertEqual(d.va, 0x5000)
        self.assertTrue(all(s.vclass == 'verified' for s in code))

    def test_unused_code_bearing_variant_cannot_supply_a_bss_layout(self):
        ob, d = make_bss()
        ob.secs = [object()]
        code = [FakeCodeSection('code/a', [(('E', 'alpha'), 0x5004)]),
                FakeCodeSection('code/b', [(('E', 'alpha'), 0x5004)])]

        _, placed, _ = classify([ob], code, FakeExe())

        self.assertNotIn(d, placed)
        self.assertIsNone(d.va)

    def test_data_only_object_must_come_from_a_selected_library_variant(self):
        ob, d = make_bss()
        ob.lib = 'unused_variant'
        code = [FakeCodeSection('code/a', [(('E', 'alpha'), 0x5004)]),
                FakeCodeSection('code/b', [(('E', 'alpha'), 0x5004)])]

        _, placed, _ = classify([ob], code, FakeExe())

        self.assertNotIn(d, placed)
        self.assertIsNone(d.va)

    def test_conflicting_symbol_bases_reject_the_section_without_majority_vote(self):
        ob, d = make_bss()
        code = [
            FakeCodeSection('code/a', [(('E', 'alpha'), 0x5004)]),
            FakeCodeSection('code/b', [(('E', 'alpha'), 0x5004)]),
            # This singleton is still available conflicting evidence: beta implies base 0x6000.
            FakeCodeSection('code/c', [(('E', 'beta'), 0x600c)]),
        ]

        _, placed, _ = classify([ob], code, FakeExe())

        self.assertNotIn(d, placed)
        self.assertIsNone(d.va)
        self.assertEqual(d.status, 'conflict')

    def test_single_anchor_does_not_promote_weak_key(self):
        ob, d = make_bss(symbols=(('alpha', 4),))
        code = [FakeCodeSection('code/only', [(('E', 'alpha'), 0x5004)])]

        _, placed, vals = classify([ob], code, FakeExe())

        self.assertNotIn(d, placed)
        self.assertIsNone(d.va)
        self.assertEqual(code[0].vclass, 'unverified')
        self.assertEqual(vals[('E', 'alpha')][0x5004], 1)

    def test_unrelated_singleton_key_stays_weak(self):
        ob, _ = make_bss(symbols=(('alpha', 4),))
        code = [FakeCodeSection('code/only', [(('E', 'unrelated'), 0x5008)])]

        conflicts, _, _ = classify([ob], code, FakeExe())

        self.assertEqual(conflicts, [])
        self.assertEqual(code[0].vclass, 'unverified')

    def test_out_of_image_section_extent_is_rejected(self):
        ob, d = make_bss(symbols=(('alpha', 4),))
        code = [
            FakeCodeSection('code/a', [(('E', 'alpha'), 0x7004)]),
            FakeCodeSection('code/b', [(('E', 'alpha'), 0x7004)]),
        ]

        _, placed, _ = classify([ob], code, FakeExe())

        self.assertNotIn(d, placed)
        self.assertEqual(d.status, 'mismatch')

    def test_nonmatching_bss_bytes_are_rejected(self):
        ob, d = make_bss(data=bytes([1]) + bytes(23))
        code = [
            FakeCodeSection('code/a', [(('E', 'alpha'), 0x5004)]),
            FakeCodeSection('code/b', [(('E', 'alpha'), 0x5004)]),
        ]

        _, placed, _ = classify([ob], code, FakeExe())

        self.assertNotIn(d, placed)
        self.assertEqual(d.status, 'mismatch')

    def test_accepted_code_definition_can_anchor_matching_bss_symbol(self):
        ob, d = make_bss(symbols=(('alpha', 4),))
        definition = FakeCodeSection('code/definition', definitions=(('alpha', 4),))
        definition.va = 0x5000
        code = [definition]

        _, placed, _ = classify([ob], code, FakeExe())

        self.assertIn(d, placed)
        self.assertEqual(d.va, 0x5000)


if __name__ == '__main__':
    unittest.main()
