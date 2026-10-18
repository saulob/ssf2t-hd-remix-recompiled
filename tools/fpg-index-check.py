"""Checks the flog_x360.fpg index the way the guest does, without launching the game.

  python -I tools\fpg-index-check.py

The index starts at 0x800 with `count` 16-byte little-endian entries (key, offset, size, flags). The key is
CRC-32 of the upper-cased asset path with '/' separators, without a device prefix. Entries hold zlib streams.
Exits 1 when a representative path does not resolve the way it should.
"""
import os
import struct
import sys
import zlib

ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..'))
ARCHIVE = os.path.join(ROOT, 'game', 'extracted', 'flog_x360.fpg')
INDEX_OFFSET = 0x800
ENTRY_SIZE = 16

# Paths that must resolve, and paths that must not (the content is not in this package).
PRESENT = [
    'textures/replacements.tsv',
    'textures/backgrounds/Fei_Long/Fei_Long.tsv',
    'fonts/Helvetica55RomanXbox38.rf',
]
ABSENT = [
    'particles/TEX_FX_MMGHOST.model.x360',
    'ui/UIDefaults.xml',
]


def asset_key(path):
    return zlib.crc32(path.replace('\\', '/').upper().encode('latin-1'))


def read_index(data):
    if data[:4] != b'30GF':
        raise ValueError('not an FPG archive')
    count = struct.unpack_from('<I', data, 4)[0]
    if INDEX_OFFSET + count * ENTRY_SIZE > len(data):
        raise ValueError('index runs past end of file')
    entries = {}
    for i in range(count):
        key, offset, size, flags = struct.unpack_from('<IIII', data, INDEX_OFFSET + i * ENTRY_SIZE)
        if offset + size > len(data):
            raise ValueError(f'entry {i} points past end of file')
        entries.setdefault(key, (offset, size, flags))
    return count, entries


def main():
    with open(ARCHIVE, 'rb') as f:
        data = f.read()
    count, entries = read_index(data)
    print(f'slots {count}, unique keys {len(entries)}')

    failures = 0
    for path in PRESENT:
        entry = entries.get(asset_key(path))
        if entry is None:
            print(f'FAIL present path missing from index: {path}')
            failures += 1
            continue
        offset, size, _ = entry
        text = zlib.decompress(data[offset:offset + size])
        print(f'hit   {path} -> {len(text)} bytes inflated')

    for path in ABSENT:
        if asset_key(path) in entries:
            print(f'FAIL absent path resolves unexpectedly: {path}')
            failures += 1
        else:
            print(f'miss  {path}')

    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
