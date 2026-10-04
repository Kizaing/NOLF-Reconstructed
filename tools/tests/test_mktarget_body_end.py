import sys
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import build
import mktarget


class MemoryImage:
    def __init__(self, start, data):
        self.start = start
        self.data = data

    def read(self, va, size):
        lo = va - self.start
        if lo < 0 or lo + size > len(self.data):
            raise ValueError('read outside fixture')
        return self.data[lo:lo + size]


class ShortReadImage:
    def read(self, va, size):
        return b'\xcc'


class FakeBaseObject:
    def __init__(self, start, end):
        self.start = start
        self.end = end

    def extent(self, symbol):
        return object(), self.start, self.end


def result(va, kind='FUNCTION', status='MATCH'):
    annot = SimpleNamespace(kind=kind, va=va, symbol=object(), unit=SimpleNamespace(name='unit'))
    return SimpleNamespace(a=annot, status=status)


class ValidateBodyEndTests(unittest.TestCase):
    def test_accepts_nonempty_linker_padding_tail(self):
        start = 0x1000
        img = MemoryImage(start, b'\x55\x8b\xec\x90\xcc\xcc')
        self.assertEqual(mktarget.validate_body_end(start, start + 6, start + 3, img), start + 3)

    def test_rejects_nonpadding_in_omitted_tail(self):
        start = 0x2000
        img = MemoryImage(start, b'\x55\x8b\xec\xcc\x90\x83')
        with self.assertRaisesRegex(ValueError, 'non-padding'):
            mktarget.validate_body_end(start, start + 6, start + 3, img)

    def test_rejects_short_image_read(self):
        start = 0x2800
        with self.assertRaisesRegex(ValueError, 'non-padding'):
            mktarget.validate_body_end(start, start + 4, start + 1, ShortReadImage())

    def test_rejects_cutoff_outside_original_extent(self):
        start = 0x3000
        img = MemoryImage(start, b'\x55\x8b\xec\xcc\xcc\xcc')
        with self.assertRaisesRegex(ValueError, 'outside original extent'):
            mktarget.validate_body_end(start, start + 6, start + 7, img)


class MatchedBodyEndTests(unittest.TestCase):
    def test_uses_section_relative_base_extent_for_function_match(self):
        va = 0x401000
        symtab = SimpleNamespace(funcs={va: (va + 16, 'func')})
        base_objects = {'unit': FakeBaseObject(0, 12)}
        img = MemoryImage(va, b'\x55' * 12 + b'\xcc' * 4)
        with patch.object(mktarget, '_image', return_value=img):
            ends = build.matched_body_ends([result(va)], symtab, base_objects, 'fixture.exe')
        self.assertEqual({va: va + 12}, ends)

    def test_skips_stub_and_diff_results(self):
        va = 0x402000
        symtab = SimpleNamespace(funcs={va: (va + 16, 'func')})
        base_objects = {'unit': FakeBaseObject(0, 12)}
        img = MemoryImage(va, b'\x55' * 12 + b'\xcc' * 4)
        with patch.object(mktarget, '_image', return_value=img):
            ends = build.matched_body_ends([
                result(va, kind='STUB'),
                result(va, status='DIFF'),
            ], symtab, base_objects, 'fixture.exe')
        self.assertEqual({}, ends)

    def test_matched_nonpadding_tail_is_refused(self):
        va = 0x403000
        symtab = SimpleNamespace(funcs={va: (va + 16, 'func')})
        base_objects = {'unit': FakeBaseObject(0, 12)}
        img = MemoryImage(va, b'\x55' * 12 + b'\xcc\x90\x83\xcc')
        with patch.object(mktarget, '_image', return_value=img):
            with self.assertRaisesRegex(ValueError, 'non-padding'):
                build.matched_body_ends([result(va)], symtab, base_objects, 'fixture.exe')


if __name__ == '__main__':
    unittest.main()
