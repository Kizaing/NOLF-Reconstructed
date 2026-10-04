import hashlib
import json
import tempfile
import unittest
from pathlib import Path

from tools.library_data import AR_MAGIC, _extract_member, verify_library_data


ROOT = Path(__file__).resolve().parents[2]
CONFIG = ROOT / "config" / "library_data.json"


class LibraryDataRejectTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        self.config = json.loads(CONFIG.read_text(encoding="utf-8"))
        required = [Path(self.config["original_image"])] + [Path(u["archive"]) for u in self.config["units"]]
        missing = [str(path) for path in required if not path.is_file()]
        if missing:
            self.tmp.cleanup()
            self.skipTest("installed executable/library inputs unavailable: " + ", ".join(missing))

    def tearDown(self):
        self.tmp.cleanup()

    def save_config(self):
        p = self.root / "config.json"
        p.write_text(json.dumps(self.config), encoding="utf-8")
        return p

    def rejected(self, pattern):
        with self.assertRaisesRegex(ValueError, pattern):
            verify_library_data(self.save_config(), cache_dir=self.root / "cache")

    def test_valid_config(self):
        result = verify_library_data(CONFIG, cache_dir=self.root / "valid-cache")
        self.assertEqual((len(result.units), len(result.sections()), result.payload_bytes, result.padding_bytes),
                         (6, 38, 1304, 4))
        self.assertEqual(sum(len(s.metadata["native_relocations"]) for s in result.sections()), 13)

    def test_wrong_archive_hash(self):
        self.config["units"][0]["archive_sha256"] = "0" * 64
        self.rejected("archive SHA256 mismatch")

    def test_wrong_payload_against_image(self):
        source = Path(self.config["original_image"])
        data = bytearray(source.read_bytes())
        section = self.config["units"][0]["sections"][1]
        va = int(section["original_va"], 16)
        # Locate the VA in the PE raw section using the verifier's mapper.
        from tools.library_data import _pe_sections, _read_va
        secs = _pe_sections(data)
        old = _read_va(data, secs, va, section["size"])
        for _, base, span, raw, rawsize in secs:
            if base <= va and va + len(old) <= base + span:
                data[raw + va - base] ^= 1
                break
        image = self.root / "modified.exe"
        image.write_bytes(data)
        self.config["original_image"] = str(image)
        self.config["original_image_sha256"] = hashlib.sha256(data).hexdigest()
        self.rejected("resolved selected section does not match original image")

    def test_wrong_relocation_target(self):
        rel = self.config["units"][0]["sections"][0]["native_relocations"][0]
        rel["target_va"] = "004c9c84"
        self.rejected("target does not match a selected symbol VA")

    def test_mismatched_symbol_range(self):
        self.config["units"][0]["sections"][0]["symbols"][0]["offset"] = 1
        self.rejected("symbol _c_dfDIJoystick range/metadata mismatch")

    def test_overlapping_selected_ranges(self):
        first = self.config["units"][0]["sections"][0]
        second = self.config["units"][1]["sections"][0]
        second["original_va"] = first["original_va"]
        second["original_output_group"] = ".text"
        self.rejected("intervals overlap")

    def test_wrong_output_group(self):
        self.config["units"][0]["sections"][0]["original_output_group"] = ".text"
        self.rejected("declared output group does not match PE section")

    def test_padding_overlaps_selected_range(self):
        self.config["padding"][0]["va"] = self.config["units"][1]["sections"][0]["original_va"]
        self.rejected("padding overlaps a selected section")

    def test_duplicate_padding_interval_even_with_adjusted_total(self):
        self.config["padding"].append(dict(self.config["padding"][0]))
        self.config["alignment_padding_bytes"][".rdata"] = 8
        self.rejected("alignment padding intervals overlap")

    def test_wrong_padding_kind(self):
        self.config["padding"][0]["kind"] = "data"
        self.rejected("invalid alignment padding kind")

    def test_unsupported_relocation(self):
        rel = self.config["units"][0]["sections"][0]["native_relocations"][0]
        rel["type"] = 7
        self.rejected("unsupported native relocation type")

    def test_truncated_archive(self):
        unit = self.config["units"][0]
        original = Path(unit["archive"]).read_bytes()
        truncated = self.root / "truncated.lib"
        truncated.write_bytes(original[:-7])
        unit["archive"] = str(truncated)
        unit["archive_sha256"] = hashlib.sha256(truncated.read_bytes()).hexdigest()
        self.rejected("archive")

    def test_unresolved_target(self):
        rel = self.config["units"][0]["sections"][0]["native_relocations"][0]
        rel["target_symbol"] = "_MISSING_NATIVE_TARGET"
        self.rejected("unresolved or ambiguous relocation target")


class ArchiveParserTests(unittest.TestCase):
    @staticmethod
    def archive(payload):
        name = b"tiny.obj/".ljust(16)
        header = name + b"0".ljust(12) + b"0".ljust(6) + b"0".ljust(6) + b"100644".ljust(8) + str(len(payload)).encode().ljust(10) + b"`\n"
        return AR_MAGIC + header + payload + (b"\n" if len(payload) & 1 else b"")

    def test_extracts_exact_member_and_rejects_truncation(self):
        data = self.archive(b"COFF")
        self.assertEqual(_extract_member(data, "tiny.obj"), b"COFF")
        with self.assertRaisesRegex(ValueError, "truncated archive"):
            _extract_member(data[:70], "tiny.obj")


if __name__ == "__main__":
    unittest.main()
