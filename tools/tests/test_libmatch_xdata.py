import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import libmatch


class FakeSection:
    def __init__(self, ob, secno, secname, va=None, status=None, relocs=()):
        self.ob = ob
        self.secno = secno
        self.secname = secname
        self.va = va
        self.status = status
        self.relocs = list(relocs)
        self.cands = {va: 'bytes'} if va is not None else {}

    def name(self):
        return 'fixture'


class ContradictedXdataTests(unittest.TestCase):
    def make_obj(self, name, xdata_status=None, xdata_secno=7):
        ob = SimpleNamespace(id=name, dsecs=[])
        if xdata_status is not None:
            ob.dsecs.append(FakeSection(ob, xdata_secno, '.xdata$x', status=xdata_status))
        return ob

    def candidate(self, ob, key, va=0x4C3700, secname='.text$x'):
        return FakeSection(ob, 3, secname, va=va, relocs=[(0, 20, key, 0, 'unwind')])

    def test_same_object_mismatched_xdata_rejects_textx_va(self):
        ob = self.make_obj('owner', 'mismatch')
        text = self.candidate(ob, ('S', 'owner', 7))
        log = []

        rejected = libmatch.reject_contradicted_xdata([text], [ob], log)

        self.assertEqual([text], rejected)
        self.assertNotIn(0x4C3700, text.cands)
        self.assertIn('xdata-mismatch:', log[0])

    def test_unknown_xdata_does_not_reject_textx(self):
        ob = self.make_obj('owner', None)
        text = self.candidate(ob, ('S', 'owner', 7))

        self.assertEqual([], libmatch.reject_contradicted_xdata([text], [ob], []))
        self.assertIn(0x4C3700, text.cands)

    def test_valid_placed_xdata_does_not_reject_textx(self):
        ob = self.make_obj('owner', 'placed')
        text = self.candidate(ob, ('S', 'owner', 7))

        self.assertEqual([], libmatch.reject_contradicted_xdata([text], [ob], []))
        self.assertIn(0x4C3700, text.cands)

    def test_other_object_and_other_relocation_key_are_ignored(self):
        owner = self.make_obj('owner', None)
        other = self.make_obj('other', 'mismatch')
        same_section_number_only = self.candidate(owner, ('S', 'owner', 7), va=0x4C3700)
        other_object_key = self.candidate(owner, ('S', 'other', 7), va=0x4C3800)

        rejected = libmatch.reject_contradicted_xdata(
            [same_section_number_only, other_object_key], [owner, other], [])

        self.assertEqual([], rejected)
        self.assertEqual({0x4C3700: 'bytes'}, same_section_number_only.cands)
        self.assertEqual({0x4C3800: 'bytes'}, other_object_key.cands)

    def test_only_textx_sections_are_considered(self):
        ob = self.make_obj('owner', 'mismatch')
        text = self.candidate(ob, ('S', 'owner', 7), secname='.text')

        self.assertEqual([], libmatch.reject_contradicted_xdata([text], [ob], []))
        self.assertIn(0x4C3700, text.cands)


class StableResolutionTests(unittest.TestCase):
    def test_retries_past_twelve_candidate_rejections(self):
        ob = SimpleNamespace(id='owner', lib='fixture', dsecs=[], secs=[])
        for i in range(13):
            ob.dsecs.append(FakeSection(ob, 100 + i, '.xdata$x'))
            ob.secs.append(FakeSection(ob, i + 1, '.text$x', va=0x4C3700 + i * 0x10,
                                       relocs=[(0, 20, ('S', 'owner', 100 + i), 0, 'unwind')]))
        objs = [ob]
        fam_secs = {'fixture': ob.secs}
        classify_round = {'n': 0}

        def fake_resolve(objs, exe, gh, log):
            accepted = []
            for sec in ob.secs:
                if sec.cands:
                    sec.va = next(iter(sec.cands))
                    sec.status = 'unique'
                    accepted.append(sec)
            return accepted, {}, {}

        def fake_classify(objs, accepted, exe):
            classify_round['n'] += 1
            for d in ob.dsecs:
                d.status = None
            if classify_round['n'] <= len(ob.dsecs):
                ob.dsecs[classify_round['n'] - 1].status = 'mismatch'

        with patch.object(libmatch, 'resolve', side_effect=fake_resolve), \
                patch.object(libmatch, 'attribute', side_effect=lambda objs, accepted, *args: (accepted, {})), \
                patch.object(libmatch, 'classify', side_effect=fake_classify), \
                patch.object(libmatch, 'isolated', return_value=[]):
            log = []
            accepted, _, _, _ = libmatch.resolve_stable(objs, object(), object(), fam_secs, log)

        self.assertEqual([], accepted)
        self.assertEqual(14, classify_round['n'])
        self.assertEqual(13, sum(line.startswith('xdata-mismatch:') for line in log))
        self.assertTrue(all(not sec.cands for sec in ob.secs))

    def test_rejection_without_candidate_progress_fails_closed(self):
        ob = SimpleNamespace(id='owner', lib='fixture', dsecs=[], secs=[])
        text = FakeSection(ob, 1, '.text$x', va=0x4C3700)
        ob.secs.append(text)

        def fake_resolve(objs, exe, gh, log):
            text.va = 0x4C3700
            return [text], {}, {}

        with patch.object(libmatch, 'resolve', side_effect=fake_resolve), \
                patch.object(libmatch, 'attribute', side_effect=lambda objs, accepted, *args: (accepted, {})), \
                patch.object(libmatch, 'classify'), \
                patch.object(libmatch, 'reject_contradicted_xdata', return_value=[text]), \
                patch.object(libmatch, 'isolated', return_value=[]):
            with self.assertRaisesRegex(RuntimeError, 'made no candidate progress.*refusing stale results'):
                libmatch.resolve_stable([ob], object(), object(), {'fixture': [text]}, [])


if __name__ == '__main__':
    unittest.main()
