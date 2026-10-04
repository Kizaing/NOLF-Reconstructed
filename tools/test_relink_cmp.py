"""Whole-file relink comparison tests; the PE diagnostic path uses a tiny in-memory-built PE fixture."""
import contextlib
import io
import struct
import tempfile
from pathlib import Path
import unittest
from unittest.mock import patch

import relink_cmp


def minimal_pe(section_count=1):
    """Create a small PE32 image with one raw .text section."""
    data = bytearray(0x400)
    data[:2] = b'MZ'
    struct.pack_into('<I', data, 0x3c, 0x80)
    data[0x80:0x84] = b'PE\0\0'
    struct.pack_into('<HHIIIHH', data, 0x84, 0x14c, section_count, 0, 0, 0, 224, 0x010f)

    optional = 0x98
    struct.pack_into('<H', data, optional, 0x10b)
    struct.pack_into('<I', data, optional + 4, 0x200)       # SizeOfCode
    struct.pack_into('<I', data, optional + 16, 0x1000)    # AddressOfEntryPoint
    struct.pack_into('<I', data, optional + 20, 0x1000)    # BaseOfCode
    struct.pack_into('<I', data, optional + 28, 0x400000)  # ImageBase
    struct.pack_into('<II', data, optional + 32, 0x1000, 0x200)
    struct.pack_into('<HH', data, optional + 40, 4, 0)     # OS version
    struct.pack_into('<HH', data, optional + 48, 4, 0)     # subsystem version
    struct.pack_into('<I', data, optional + 56, 0x2000)    # SizeOfImage
    struct.pack_into('<I', data, optional + 60, 0x200)     # SizeOfHeaders
    struct.pack_into('<H', data, optional + 68, 2)         # Windows GUI subsystem
    struct.pack_into('<I', data, optional + 72, 0x100000)  # Stack reserve
    struct.pack_into('<I', data, optional + 76, 0x1000)    # Stack commit
    struct.pack_into('<I', data, optional + 80, 0x100000)  # Heap reserve
    struct.pack_into('<I', data, optional + 84, 0x1000)    # Heap commit
    struct.pack_into('<I', data, optional + 92, 16)        # NumberOfRvaAndSizes

    if section_count:
        section = 0x178
        data[section:section + 8] = b'.text\0\0\0'
        struct.pack_into('<IIIIIIHHI', data, section + 8,
                         0x10, 0x1000, 0x200, 0x200, 0, 0, 0, 0, 0x60000020)
    data[0x200:0x400] = bytes(range(256)) * 2
    return bytes(data)


class CompareFilesTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.original = self.root / 'original.bin'
        self.new = self.root / 'new.bin'

    def test_identical_files(self):
        self.original.write_bytes(b'PE header + section bytes')
        self.new.write_bytes(self.original.read_bytes())
        result = relink_cmp.compare_files(self.original, self.new)
        self.assertTrue(result['equal'])
        self.assertEqual(result['total_differences'], 0)
        self.assertEqual(result['original_size'], result['new_size'])

    def test_single_header_or_tail_byte_difference(self):
        source = bytearray(minimal_pe())
        self.original.write_bytes(source)

        source[0] ^= 0x01
        self.new.write_bytes(source)
        header = relink_cmp.compare_files(self.original, self.new)
        self.assertFalse(header['equal'])
        self.assertEqual(header['total_differences'], 1)
        self.assertEqual(header['runs'][0][0], 0)

        source = bytearray(self.original.read_bytes())
        source[-1] ^= 0x80
        self.new.write_bytes(source)
        tail = relink_cmp.compare_files(self.original, self.new)
        self.assertFalse(tail['equal'])
        self.assertEqual(tail['total_differences'], 1)
        self.assertEqual(tail['runs'][0][0], len(source) - 1)

    def test_length_difference_counts_unmatched_tail(self):
        self.original.write_bytes(minimal_pe())
        self.new.write_bytes(self.original.read_bytes() + b'overlay')
        result = relink_cmp.compare_files(self.original, self.new)
        self.assertFalse(result['equal'])
        self.assertEqual(result['in_range_differences'], 0)
        self.assertEqual(result['new_extra_bytes'], 7)
        self.assertEqual(result['total_differences'], 7)

    def test_check_cli_reports_whole_file_and_missing_sections(self):
        original = self.root / 'original.exe'
        rebuilt = self.root / 'rebuilt.exe'
        original.write_bytes(minimal_pe())
        rebuilt.write_bytes(original.read_bytes())

        with patch.object(relink_cmp.modcfg, 'IMAGE', str(original)):
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(relink_cmp.main([str(rebuilt), '--check']), 0)
            self.assertIn('sections: orig', output.getvalue())
            self.assertIn('whole-file byte comparison: identical', output.getvalue())

            data = bytearray(original.read_bytes())
            data[0xd8] ^= 1  # PE checksum header byte
            rebuilt.write_bytes(data)
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(relink_cmp.main([str(rebuilt), '--check']), 1)
            self.assertIn('1 differing byte positions', output.getvalue())

            data = bytearray(original.read_bytes())
            data[-1] ^= 0x80
            rebuilt.write_bytes(data)
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(relink_cmp.main([str(rebuilt), '--check']), 1)
            self.assertIn('1 differing byte positions', output.getvalue())

            rebuilt.write_bytes(original.read_bytes() + b'overlay')
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(relink_cmp.main([str(rebuilt), '--check']), 1)
            self.assertIn('7 unmatched new bytes', output.getvalue())
            self.assertIn('trailing data: 0 unmatched original bytes, 7 unmatched new bytes', output.getvalue())

            rebuilt.write_bytes(minimal_pe(section_count=0))
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(relink_cmp.main([str(rebuilt), '--check']), 1)
            self.assertIn('whole-file byte comparison: DIFFERENT', output.getvalue())
            self.assertIn('sections missing from new: .text', output.getvalue())

    def test_default_legacy_report_does_not_claim_file_equality(self):
        original = self.root / 'original.exe'
        rebuilt = self.root / 'rebuilt.exe'
        original.write_bytes(minimal_pe())
        rebuilt.write_bytes(original.read_bytes())
        with patch.object(relink_cmp.modcfg, 'IMAGE', str(original)):
            output = io.StringIO()
            with contextlib.redirect_stdout(output):
                self.assertEqual(relink_cmp.main([str(rebuilt), '--max', '2']), 0)
        self.assertIn('orig size', output.getvalue())
        self.assertIn('.text: 0 of 512 bytes differ', output.getvalue())
        self.assertNotIn('whole-file byte comparison', output.getvalue())


if __name__ == '__main__':
    unittest.main()
