r"""Compare a relinked image with the selected module's original image.

  python tools/relink_cmp.py [new.exe] [--max N] [--module lithtech|d3dren]
  python tools/relink_cmp.py [new.exe] --check [--module lithtech|d3dren]

The default mode prints PE headers, sections and byte differences for diagnosis.  ``--check`` also reports a
whole-file byte comparison and exits nonzero unless every byte and the file length match.
"""
import argparse
import os
import sys

import pefile

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(TOOLS)
sys.path.insert(0, TOOLS)
import modcfg  # noqa: E402


def sec_table(pe):
    return {s.Name.rstrip(b'\0').decode('latin1'): s for s in pe.sections}


def diff_runs(a, b, maxruns):
    """Return maximal differing byte runs, merging gaps shorter than eight bytes."""
    runs, i, n = [], 0, min(len(a), len(b))
    while i < n and len(runs) < maxruns:
        if a[i] != b[i]:
            j = i
            last = i
            while j < n and j - last < 8:
                if a[j] != b[j]:
                    last = j
                j += 1
            runs.append((i, last - i + 1))
            i = last + 1
        else:
            i += 1
    return runs


def compare_bytes(original, new, maxruns=10):
    """Return complete byte and length counts for two byte strings."""
    common = min(len(original), len(new))
    in_range = sum(a != b for a, b in zip(original[:common], new[:common]))
    original_extra = max(0, len(original) - len(new))
    new_extra = max(0, len(new) - len(original))
    return {
        'equal': len(original) == len(new) and in_range == 0,
        'original_size': len(original),
        'new_size': len(new),
        'common_size': common,
        'in_range_differences': in_range,
        'original_extra_bytes': original_extra,
        'new_extra_bytes': new_extra,
        # A byte with no counterpart counts as one differing file position.
        'total_differences': in_range + original_extra + new_extra,
        'runs': diff_runs(original, new, maxruns),
    }


def compare_files(original_path, new_path, maxruns=10):
    """Compare files byte-for-byte and return complete counts plus a bounded set of differing runs."""
    with open(original_path, 'rb') as f:
        original = f.read()
    with open(new_path, 'rb') as f:
        new = f.read()
    return compare_bytes(original, new, maxruns)


def print_pe_diagnostics(original_path, new_path, maxruns):
    with open(original_path, 'rb') as f:
        original = pefile.PE(data=f.read())
    with open(new_path, 'rb') as f:
        new = pefile.PE(data=f.read())
    print('orig size %d  new size %d' % (os.path.getsize(original_path), os.path.getsize(new_path)))
    fields = ('AddressOfEntryPoint', 'ImageBase', 'SectionAlignment', 'FileAlignment', 'SizeOfImage', 'SizeOfHeaders',
              'SizeOfCode', 'SizeOfInitializedData', 'SizeOfUninitializedData', 'BaseOfCode', 'BaseOfData',
              'Subsystem', 'MajorOperatingSystemVersion', 'MajorSubsystemVersion', 'SizeOfStackReserve',
              'SizeOfStackCommit', 'SizeOfHeapReserve', 'SizeOfHeapCommit', 'DllCharacteristics', 'CheckSum')
    for field in fields:
        va = getattr(original.OPTIONAL_HEADER, field)
        vb = getattr(new.OPTIONAL_HEADER, field)
        print('  %-28s %8x %8x %s' % (field, va, vb, '' if va == vb else '  <-- differs'))
    print('  characteristics %x %x' % (original.FILE_HEADER.Characteristics, new.FILE_HEADER.Characteristics))
    sections_original, sections_new = sec_table(original), sec_table(new)
    print('sections: orig %s | new %s' % (list(sections_original), list(sections_new)))
    for name in sections_original:
        x = sections_original[name]
        y = sections_new.get(name)
        if not y:
            print('  %-6s missing in new' % name)
            continue
        print('  %-6s va %x/%x vsize %x/%x raw %x/%x flags %x/%x' % (
            name, x.VirtualAddress, y.VirtualAddress, x.Misc_VirtualSize, y.Misc_VirtualSize,
            x.SizeOfRawData, y.SizeOfRawData, x.Characteristics, y.Characteristics))
    for name in sections_original:
        if name not in sections_new:
            continue
        x, y = sections_original[name], sections_new[name]
        a, b = x.get_data(), y.get_data()
        common = min(len(a), len(b))
        differences = sum(x != y for x, y in zip(a[:common], b[:common]))
        print('%s: %d of %d bytes differ' % (name, differences, common))
        for off, length in diff_runs(a[:common], b[:common], maxruns):
            va = original.OPTIONAL_HEADER.ImageBase + x.VirtualAddress + off
            count = min(length, 12)
            print('   +%06x (va %08x) len %d: orig %s | new %s' % (
                off, va, length, a[off:off + count].hex(), b[off:off + count].hex()))
    return set(sections_new) - set(sections_original), set(sections_original) - set(sections_new)


def print_whole_file_result(result, original_path, new_path, missing_original, missing_new):
    if result['equal']:
        print('whole-file byte comparison: identical (%d bytes)' % result['original_size'])
        return
    print('whole-file byte comparison: DIFFERENT; %d differing byte positions (%d within the common range, '
          '%d unmatched original bytes, %d unmatched new bytes); sizes original=%d new=%d' % (
              result['total_differences'], result['in_range_differences'], result['original_extra_bytes'],
              result['new_extra_bytes'], result['original_size'], result['new_size']))
    if missing_original:
        print('  sections missing from original: %s' % ', '.join(sorted(missing_original)))
    if missing_new:
        print('  sections missing from new: %s' % ', '.join(sorted(missing_new)))
    for offset, length in result['runs']:
        count = min(length, 12)
        print('  file +%08x len %d: original %s | new %s' % (
            offset, length,
            _hex_slice(original_path, offset, count),
            _hex_slice(new_path, offset, count)))
    if result['original_extra_bytes'] or result['new_extra_bytes']:
        print('  trailing data: %d unmatched original bytes, %d unmatched new bytes' % (
            result['original_extra_bytes'], result['new_extra_bytes']))


def _hex_slice(path, offset, count):
    with open(path, 'rb') as f:
        f.seek(offset)
        return f.read(count).hex()


def main(argv=None):
    parser = argparse.ArgumentParser(description='Compare a relinked image with the selected module original.')
    parser.add_argument('new', nargs='?', help='new image (defaults to build/relink/<module image>)')
    parser.add_argument('--max', type=int, default=10, dest='maxruns', help='maximum differing runs per section (default: 10)')
    parser.add_argument('--check', action='store_true', help='exit 0 only when the complete files are byte-identical')
    args = parser.parse_args(argv)

    original_path = modcfg.IMAGE
    new_path = args.new or os.path.join(modcfg.BUILD, 'relink', modcfg.OUT_IMAGE_NAME)
    missing_original, missing_new = print_pe_diagnostics(original_path, new_path, args.maxruns)
    if not args.check:
        return 0

    result = compare_files(original_path, new_path, args.maxruns)
    print_whole_file_result(result, original_path, new_path, missing_original, missing_new)
    return 0 if result['equal'] else 1


if __name__ == '__main__':
    sys.exit(main())
