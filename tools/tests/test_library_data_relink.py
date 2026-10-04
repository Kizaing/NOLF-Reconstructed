import tempfile
import sys
import unittest
from pathlib import Path
from types import SimpleNamespace

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import coffedit
import library_data_relink as native
from relink import subset_sections


def fake_section(unit_name, **metadata):
    return SimpleNamespace(unit_name=unit_name, metadata=metadata)


class LibraryDataRelinkTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.objpath = Path(self.temp.name) / "fixture.obj"

    def save(self, obj):
        obj.save(str(self.objpath))

    def test_native_library_data_mode_gate(self):
        self.assertTrue(native.enabled("mixed", "", False, None))
        self.assertFalse(native.enabled("mixed", "", False, "client/cnet"))
        self.assertFalse(native.enabled("targets", "", False, None))
        self.assertFalse(native.enabled("mixed", None, True, None))
        self.assertFalse(native.enabled("mixed", "client/cnet", False, None))

    def test_copy_selected_preserves_internal_rdata_to_text_relocation(self):
        obj = coffedit.Coff()
        obj.sections = [
            coffedit.Sec(".rdata", b"\0\0\0\0", [[0, 1, 0x06]], coffedit.SCN_CNT_INIT),
            coffedit.Sec(".text", b"TABL", [], coffedit.SCN_CNT_CODE),
        ]
        obj.add_symbol("_data", 0, 1, cls=coffedit.CLS_EXTERNAL)
        obj.add_symbol("_table", 0, 2, cls=coffedit.CLS_STATIC)
        self.save(obj)
        unit = SimpleNamespace(
            metadata={"name": "fixture"}, member_path=self.objpath,
            sections=[
                fake_section("fixture", coff_section_index=1,
                             symbols=[{"name": "_data", "offset": 0, "va": "00001000", "coff_type": 0, "storage_class": 2}],
                             native_relocations=[{"offset": 0, "type": 6, "target_va": "00002000"}]),
                fake_section("fixture", coff_section_index=2,
                             symbols=[{"name": "_table", "offset": 0, "va": "00002000", "coff_type": 0, "storage_class": 3}],
                             native_relocations=[]),
            ],
        )

        copied, new_sections = native.copy_selected(
            unit, coffedit.Coff, coffedit.Sym, subset_sections, lambda va: None)

        self.assertEqual(new_sections, {1: 1, 2: 2})
        reloc = copied.sections[0].relocs[0]
        target = copied.syms[reloc[1]]
        self.assertEqual((reloc[0], reloc[2]), (0, 0x06))
        self.assertEqual((target.name, target.sec, target.value, target.cls),
                         ("_table", 2, 0, coffedit.CLS_STATIC))

    def test_make_table_seed_remaps_relocation_symbol_index(self):
        obj = coffedit.Coff()
        obj.sections = [
            coffedit.Sec(".discard", b"DROP", [], coffedit.SCN_CNT_INIT),
            coffedit.Sec(".text", b"\0\0\0\0", [[0, 2, 0x06]], coffedit.SCN_CNT_CODE),
        ]
        obj.add_symbol("_discard", 0, 1, cls=coffedit.CLS_EXTERNAL)
        obj.add_symbol("_table", 0, 2, cls=coffedit.CLS_STATIC)
        obj.add_symbol("_external", 0, 0, cls=coffedit.CLS_EXTERNAL)
        self.save(obj)
        section = fake_section("fixture", original_output_group=".text",
                               original_va="004af960", size=4, coff_section_index=2,
                               symbols=[{"name": "_table", "offset": 0, "va": "004af960"}])
        unit = SimpleNamespace(metadata={"name": "fixture"}, member_path=self.objpath,
                               sections=[section])
        verified = SimpleNamespace(units=[unit], sections=lambda: [section])

        seed, got_section, got_unit = native.make_table_seed(
            verified, coffedit.Coff, subset_sections)

        self.assertIs(got_section, section)
        self.assertIs(got_unit, unit)
        old_target_index = 2
        new_target_index = seed.sections[0].relocs[0][1]
        self.assertNotEqual(new_target_index, old_target_index)
        self.assertEqual(seed.syms[new_target_index].name, "_external")
        self.assertEqual(seed.syms[new_target_index].sec, 0)

    def test_subtract_native_spans_removes_payload_and_middle_padding(self):
        pieces = [("standin", ".rdata", 100, 125, "owner")]
        payload = [(105, 110, "left native data"), (115, 120, "right native data")]
        padding = ((110, 5, b"\0" * 5),)

        result = native.subtract_native_spans(pieces, payload, padding)

        self.assertEqual(result, [
            ("standin", ".rdata", 100, 105, "owner"),
            ("standin", ".rdata", 120, 125, "owner"),
        ])


if __name__ == "__main__":
    unittest.main()
