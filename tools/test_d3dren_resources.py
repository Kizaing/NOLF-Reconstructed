"""Rebuild and verify d3d.ren's authored resource/export metadata.

Run from any directory with ``python tools/test_d3dren_resources.py``. It uses the
VC6 RTM RC/CVTRES/LINK binaries identified in
build/d3dren/renderer_resource_link_inventory_20261004.md, and writes only to
build/d3dren/scratch/resource_metadata. The linked DLL is a resource-only probe,
not a renderer build.
"""
import hashlib
import os
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
os.environ['DECOMP_MODULE'] = 'd3dren'
sys.path.insert(0, str(ROOT / 'tools'))

import modcfg  # noqa: E402
import build  # noqa: E402
import pefile  # noqa: E402
from coffobj import CoffObj, REL_DIR32NB  # noqa: E402


RC = Path(r'E:\Program Files (x86)\Microsoft Visual Studio\Common\MSDev98\Bin\RC.EXE')
RCDLL = RC.with_name('RCDLL.DLL')
CVTRES = Path(r'E:\Program Files (x86)\Microsoft Visual Studio\VC98\Bin\CVTRES.EXE')
LINK = Path(r'E:\Program Files (x86)\Microsoft Visual Studio\VC98\Bin\LINK.EXE')
EXPECTED_HASHES = {
    RC: '582d0e68739b1128199d0ffc12eb62f48a17a3216a791fd6be948ff9e2eb2ffa',
    RCDLL: '5932342fc326b056988cebc710b414592b81c9ba18b9ae247b838a6021f2e434',
    CVTRES: '5e8377423582f57a94432209405fc3a96615e1799cbfab62787bed6ff40f8fe5',
    LINK: '924714f47a0558c29524012e692a8c0b49d380c05b03bd76953a5b8ba9f88236',
}
OUTPUT = Path(modcfg.BUILD) / 'scratch' / 'resource_metadata'
RC_SOURCE = Path(modcfg.CONFIG) / 'd3dren.rc'
DEF_SOURCE = Path(modcfg.CONFIG) / 'd3dren.def'
EXPECTED_TEXT = 'LithTech Direct3D Renderer'
EXPECTED_PAYLOAD_SHA256 = 'a1e69fb504d9e7f0c17fef9f3eab3320b06107263b8e22e8e49f0fd1d0c329a0'
EXPORTS = [
    ('FreeModeList', '_FreeModeList', 1, 0x10FD3),
    ('GetSupportedModes', '_GetSupportedModes', 2, 0x10F44),
    ('RenderDLLSetup', '_RenderDLLSetup', 3, 0x10FF1),
]


def require(condition, message):
    if not condition:
        raise SystemExit('FAIL: ' + message)


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def check_tools():
    for path, expected in EXPECTED_HASHES.items():
        require(path.is_file(), 'required VC6 tool is missing: %s' % path)
        actual = sha256(path)
        require(actual == expected, '%s hash %s does not match the recorded RTM tool %s' %
                (path, actual, expected))
        print('tool OK: %s (%s)' % (path.name, actual[:16]))


def run_tool(path, args, env):
    command = [str(path)] + [str(arg) for arg in args]
    result = subprocess.run(command, cwd=str(OUTPUT), env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True, errors='replace')
    if result.stdout.strip():
        print(result.stdout.rstrip())
    require(result.returncode == 0, '%s failed with exit code %s' % (path, result.returncode))


def pe_section(pe, name):
    found = [s for s in pe.sections if s.Name.rstrip(b'\0') == name]
    require(len(found) == 1, 'expected exactly one %s section, found %d' % (name.decode('ascii'), len(found)))
    return found[0]


def directory_entries(raw, offset):
    require(offset + 16 <= len(raw), 'truncated IMAGE_RESOURCE_DIRECTORY at %#x' % offset)
    characteristics, timestamp, major, minor, named, ids = struct.unpack_from('<IIHHHH', raw, offset)
    require((characteristics, timestamp, major, minor, named) == (0, 0, 0, 0, 0),
            'resource directory at %#x has unexpected metadata' % offset)
    require(ids == 1, 'resource directory at %#x has %d integer entries, expected one' % (offset, ids))
    entry_offset = offset + 16
    require(entry_offset + ids * 8 <= len(raw), 'truncated resource entries at %#x' % entry_offset)
    return [struct.unpack_from('<II', raw, entry_offset + i * 8) for i in range(ids)]


def resource_leaf(raw):
    """Return the one RT_STRING/1/1033 data-entry location and its decoded fields."""
    root = directory_entries(raw, 0)
    require(root[0][0] == 6 and root[0][1] & 0x80000000, 'resource root is not integer type RT_STRING (6)')
    type_offset = root[0][1] & 0x7FFFFFFF
    type_entries = directory_entries(raw, type_offset)
    require(type_entries[0][0] == 1 and type_entries[0][1] & 0x80000000,
            'RT_STRING resource does not contain string-table block 1')
    name_offset = type_entries[0][1] & 0x7FFFFFFF
    language_entries = directory_entries(raw, name_offset)
    require(language_entries[0][0] == 1033 and not (language_entries[0][1] & 0x80000000),
            'string-table language is not English (US), ID 1033')
    data_offset = language_entries[0][1]
    require(data_offset + 16 <= len(raw), 'truncated IMAGE_RESOURCE_DATA_ENTRY')
    data_rva, size, codepage, reserved = struct.unpack_from('<IIII', raw, data_offset)
    require((size, codepage, reserved) == (84, 0, 0),
            'resource data entry is (%d bytes, codepage %d, reserved %d), expected (84, 0, 0)' %
            (size, codepage, reserved))
    return data_offset, data_rva, size


def expected_string_payload():
    payload = bytearray()
    for slot in range(16):
        encoded = EXPECTED_TEXT.encode('utf-16le') if slot == 5 else b''
        payload += struct.pack('<H', len(encoded) // 2)
        payload += encoded
    require(len(payload) == 84, 'constructed STRINGTABLE payload has unexpected size %d' % len(payload))
    require(hashlib.sha256(payload).hexdigest() == EXPECTED_PAYLOAD_SHA256,
            'internal STRINGTABLE payload fingerprint does not match the inventory')
    return bytes(payload)


def verify_exports():
    text = DEF_SOURCE.read_text(encoding='ascii')
    library = re.search(r'^\s*LIBRARY\s+"([^"]+)"\s*$', text, re.I | re.M)
    require(library is not None and library.group(1) == 'd3d.ren', 'DEF library name must be d3d.ren')
    in_exports = False
    rows = []
    for line in text.splitlines():
        line = line.strip()
        if not line or line.startswith(';'):
            continue
        if line.upper() == 'EXPORTS':
            in_exports = True
            continue
        if in_exports:
            match = re.fullmatch(r'([A-Za-z_][A-Za-z0-9_]*)\s+@(\d+)', line)
            require(match is not None, 'unrecognized DEF export line: %r' % line)
            rows.append((match.group(1), '_' + match.group(1), int(match.group(2))))
    expected_def = [(name, symbol, ordinal) for name, symbol, ordinal, _ in EXPORTS]
    require(rows == expected_def, 'DEF exports differ from required public-name/COFF-symbol/ordinal mapping')

    original = pefile.PE(str(modcfg.IMAGE))
    require(hasattr(original, 'DIRECTORY_ENTRY_EXPORT'), 'original renderer has no export directory')
    export_rows = {}
    for entry in original.DIRECTORY_ENTRY_EXPORT.symbols:
        require(entry.name is not None, 'original renderer has an unnamed export')
        export_rows[entry.name.decode('ascii')] = (entry.ordinal, entry.address)
    expected_exports = {name: (ordinal, rva) for name, _, ordinal, rva in EXPORTS}
    require(export_rows == expected_exports, 'DEF name/ordinal map disagrees with the original renderer exports')

    units = [unit for unit in build.find_units() if unit.name == 'sys/d3d/common_init']
    require(len(units) == 1, 'expected exactly one renderer common_init source unit')
    unit = units[0]
    require(build._up_to_date(unit), 'source common_init.obj is missing or stale; build/check sys/d3d/common_init first')
    for path in [Path(unit.base_obj)]:
        obj = CoffObj(str(path))
        for name, symbol, ordinal, _ in EXPORTS:
            matches = [s for s in obj.symbols.values() if s.name == symbol]
            require(len(matches) == 1, '%s has %d definitions of %s' % (path, len(matches), symbol))
            found = matches[0]
            require(found.is_function and found.cls == 2,
                    '%s is not an externally defined COFF function in %s' % (symbol, path))
        print('real object symbols OK: %s' % path.relative_to(ROOT))
    require(build._up_to_date(unit), 'common_init source/object inputs changed during export verification')
    print('exports OK: original ordinals 1-3 match d3dren.def and common_init.obj')
    return Path(unit.base_obj)


def verify_export_object(source_object, env):
    """Have the RTM librarian resolve DEF names against a real source object.

    LINK applies x86 C decoration itself. Writing an underscored DEF alias can
    double-decorate it; checking source symbol spellings alone cannot catch that.
    This creates only an import library/export object, not fake exported code.
    """
    lib_path = OUTPUT / 'exports_metadata_probe.lib'
    run_tool(LINK, ['/LIB', '/MACHINE:IX86', '/DEF:' + str(DEF_SOURCE),
                    '/OUT:' + str(lib_path), source_object], env)
    exp_path = lib_path.with_suffix('.exp')
    require(exp_path.is_file(), 'RTM librarian did not produce the export object')
    obj = CoffObj(str(exp_path))
    referenced = {obj.symbols[index].name for section in obj.sections
                  for _, index, _ in section.relocs if obj.symbols[index].secno == 0}
    expected = {symbol for _, symbol, _, _ in EXPORTS}
    require(referenced == expected, 'export object targets %r, expected real source symbols %r' %
            (sorted(referenced), sorted(expected)))
    print('RTM export object OK: DEF names bind to the three source-built C functions')


def verify_resource_object(path):
    obj = CoffObj(str(path))
    sections = {s.name: s for s in obj.sections}
    require('.rsrc$01' in sections and '.rsrc$02' in sections,
            'CVTRES object does not contain both .rsrc$01 and .rsrc$02 contributions')
    directory = sections['.rsrc$01']
    payload = sections['.rsrc$02']
    require(len(directory.data) == 0x58 and len(payload.data) == 0x58,
            'CVTRES resource subsection sizes changed (%#x, %#x)' % (len(directory.data), len(payload.data)))
    require(len(directory.relocs) == 1, 'expected one data-RVA relocation in .rsrc$01')
    offset, symbol_index, reloc_type = directory.relocs[0]
    symbol = obj.symbols[symbol_index]
    require(offset == 0x48 and reloc_type == REL_DIR32NB and symbol.name == '$R000000' and
            symbol.secno == payload.index,
            'resource data-entry relocation does not point from .rsrc$01 into .rsrc$02')
    print('CVTRES sections OK: .rsrc$01 directory plus relocated .rsrc$02 payload')


def verify_linked_resource(dll_path, payload):
    original = pefile.PE(str(modcfg.IMAGE))
    linked = pefile.PE(str(dll_path))
    original_section = pe_section(original, b'.rsrc')
    linked_section = pe_section(linked, b'.rsrc')
    original_raw = original_section.get_data()
    linked_raw = linked_section.get_data()
    require(original_section.Misc_VirtualSize == 0xB8 and original_section.SizeOfRawData == 0x1000,
            'original .rsrc layout differs from the inventory')
    require(linked_section.Misc_VirtualSize == original_section.Misc_VirtualSize and
            linked_section.SizeOfRawData == original_section.SizeOfRawData,
            'resource-only link did not retain the original virtual/raw .rsrc extents')
    require(linked_section.Characteristics == original_section.Characteristics == 0x40000040,
            'resource section characteristics differ from the original read-only initialized-data section')
    require(set(original_raw[original_section.Misc_VirtualSize:]) <= {0} and
            set(linked_raw[linked_section.Misc_VirtualSize:]) <= {0},
            '.rsrc raw padding is not zero-filled')

    original_entry_offset, original_data_rva, original_size = resource_leaf(original_raw)
    require(original_data_rva == original_section.VirtualAddress + 0x60,
            'original resource data RVA differs from the recorded section-relative payload offset')
    linked_entry_offset, linked_data_rva, linked_size = resource_leaf(linked_raw)
    require(linked_entry_offset == original_entry_offset, 'linked resource directory layout changed')
    require(linked_size == original_size == len(payload), 'linked resource payload size differs from original')
    linked_payload_offset = linked_data_rva - linked_section.VirtualAddress
    require(linked_data_rva == linked_section.VirtualAddress + 0x60 and
            linked_payload_offset + linked_size <= linked_section.Misc_VirtualSize,
            'LINK did not fix the resource data RVA to the linked .rsrc payload')
    require(linked_raw[linked_payload_offset:linked_payload_offset + linked_size] == payload,
            'linked resource payload is not the expected 16-slot STRINGTABLE')

    normalized = bytearray(linked_raw)
    struct.pack_into('<I', normalized, linked_entry_offset, original_data_rva)
    require(bytes(normalized) == original_raw,
            'linked .rsrc bytes differ from the original after normalizing the link-time data RVA')
    print('linked .rsrc OK: 184 virtual bytes, 84-byte payload, raw bytes equal after RVA normalization')


def main():
    require(RC_SOURCE.is_file() and DEF_SOURCE.is_file(), 'missing config/d3dren resource or export source')
    OUTPUT.mkdir(parents=True, exist_ok=True)
    check_tools()
    source_object = verify_exports()
    payload = expected_string_payload()

    res_path = OUTPUT / 'd3dren_resources.res'
    obj_path = OUTPUT / 'd3dren_resources.obj'
    dll_path = OUTPUT / 'resource_metadata_probe.dll'
    run_tool(RC, ['/r', '/fo', res_path, RC_SOURCE], os.environ.copy())
    tool_env = os.environ.copy()
    link_dirs = [str(LINK.parent), str(RC.parent)]
    tool_env['PATH'] = os.pathsep.join(link_dirs + [tool_env.get('PATH', '')])
    tool_env['LIB'] = str(LINK.parent.parent / 'Lib')
    verify_export_object(source_object, tool_env)
    run_tool(CVTRES, ['/MACHINE:IX86', '/OUT:' + str(obj_path), res_path], tool_env)
    verify_resource_object(obj_path)
    run_tool(LINK, ['/DLL', '/NOENTRY', '/NODEFAULTLIB', '/MACHINE:IX86', '/BASE:0x10000000',
                    '/SUBSYSTEM:WINDOWS,4.0', '/FILEALIGN:4096', '/SECTION:.rsrc,R',
                    '/OUT:' + str(dll_path), obj_path], tool_env)
    verify_linked_resource(dll_path, payload)
    print('PASS: authored renderer resource/export metadata verified; probe is not a renderer DLL')
    print('outputs: %s' % OUTPUT.relative_to(ROOT))


if __name__ == '__main__':
    main()
