#!/usr/bin/env python3
"""Package project-built modules and notices for EBOOT's DATA.PSAR section.

The two helper PRXs, runtime notice, installer recipe and temporary converter PRX
are required. The recipe must be payload-free metadata produced by its reviewed
generator; this container does not establish authorship or audit recipe content.
No disc bytes, artwork, converted game assets, or replay captures belong here.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

MAGIC = b'JGRBND2\0'
HEADER = struct.Struct('<8s4I')
ENTRY = struct.Struct('<4I')
LIMITS = (256 * 1024, 256 * 1024, 128 * 1024, 4 * 1024 * 1024, 8 * 1024 * 1024)
NAMES = ('jgr_me.prx', 'jgr_crash.prx', 'crash_handler_license.txt', 'install.recipe', 'setup.prx')


def module(data):
    if (len(data) < 52 or data[:7] != b'\x7fELF\x01\x01\x01' or
            struct.unpack_from('<HH', data, 16) != (0xffa0, 8) or
            struct.unpack_from('<H', data, 40)[0] != 52):
        raise ValueError('Expected a built little-endian MIPS PSP PRX')


def validate(data):
    if len(data) < HEADER.size:
        raise ValueError('Truncated runtime bundle')
    magic, version, count, total, crc = HEADER.unpack_from(data)
    if magic != MAGIC or version != 2 or count != 5 or total != len(data):
        raise ValueError('Invalid runtime bundle header')
    cursor = HEADER.size + count * ENTRY.size
    table = data[HEADER.size:cursor]
    if len(table) != count * ENTRY.size or zlib.crc32(table) & 0xffffffff != crc:
        raise ValueError('Runtime bundle index checksum mismatch')
    rows = []
    for index in range(count):
        role, offset, size, checksum = ENTRY.unpack_from(table, index * ENTRY.size)
        if role != index + 1 or offset != cursor or not 0 < size <= LIMITS[index] or size > len(data) - cursor:
            raise ValueError('Invalid runtime bundle entry')
        payload = data[cursor:cursor + size]
        if zlib.crc32(payload) & 0xffffffff != checksum:
            raise ValueError('Runtime bundle payload checksum mismatch')
        if index < 2 or index == 4:
            module(payload)
        rows.append({'role': role, 'name': NAMES[index], 'bytes': size,
                     'sha256': hashlib.sha256(payload).hexdigest(), 'crc32': '%08x' % checksum})
        cursor += size
    if cursor != len(data):
        raise ValueError('Runtime bundle trailing content')
    return {'schema': 1, 'index_crc32': '%08x' % crc, 'bytes': len(data),
            'sha256': hashlib.sha256(data).hexdigest(), 'entries': rows,
            'scope': 'Container integrity and module format; recipe content/provenance audited separately'}


def build(me, crash, notice, recipe=None, converter=None):
    if recipe is None or converter is None:
        raise ValueError('Runtime bundle requires installer instructions and converter PRX')
    paths = [Path(me), Path(crash), Path(notice), Path(recipe), Path(converter)]
    payloads = []
    for index, path in enumerate(paths):
        if not 0 < path.stat().st_size <= LIMITS[index]:
            raise ValueError('Runtime bundle input exceeds its limit: ' + path.name)
        data = path.read_bytes()
        if index < 2 or index == 4:
            module(data)
        elif index == 2:
            text = data.decode('utf-8')
            if not text.strip() or '\0' in text:
                raise ValueError('Required runtime notice must be nonempty UTF-8 text')
        payloads.append(data)
    offset = HEADER.size + len(paths) * ENTRY.size
    entries = []
    for index, payload in enumerate(payloads):
        entries.append(ENTRY.pack(index + 1, offset, len(payload), zlib.crc32(payload) & 0xffffffff))
        offset += len(payload)
    table = b''.join(entries)
    data = HEADER.pack(MAGIC, 2, len(paths), offset, zlib.crc32(table) & 0xffffffff) + table + b''.join(payloads)
    return data, validate(data)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--me', type=Path, required=True)
    parser.add_argument('--crash', type=Path, required=True)
    parser.add_argument('--notice', type=Path, required=True)
    parser.add_argument('--recipe', type=Path, required=True)
    parser.add_argument('--converter', type=Path, required=True, help='Project-built converter PRX, never original game code')
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--receipt', type=Path)
    args = parser.parse_args()
    try:
        data, receipt = build(args.me, args.crash, args.notice, args.recipe, args.converter)
        args.out.parent.mkdir(parents=True, exist_ok=True)
        temporary = args.out.with_name(args.out.name + '.tmp')
        temporary.write_bytes(data)
        temporary.replace(args.out)
        if args.receipt:
            args.receipt.write_text(json.dumps(receipt, indent=2) + '\n')
        print(json.dumps(receipt, indent=2))
    except (ValueError, OSError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
