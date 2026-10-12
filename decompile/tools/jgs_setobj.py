#!/usr/bin/env python3
"""Set-placed objects (JMSc +0x34, FUN_8c09ff64): per zone, 0-terminated lists of 0x28-byte records {type, x, y, z, u32, s32 yaw BAM, .., task}
spawned by the stage module on zone entry (Shibuya FUN_8c030af8 -> FUN_8c090ff0 task 0x8a9 with per-type model / collider tables).
Output 'SOB1': u32 nsets; per chained set: u32 n, n x {u32 zone, u32 type, float x, y, z, float yaw_rad}."""
import argparse
import math
from pathlib import Path
import struct

BASE = 0x8cde6000
# Original sector tables, also retained in zonesN.bin/JRS1.
SECTORS = {1: 63, 2: 114, 3: 46, 5: 71, 6: 81}


def convert(data, sectors):
    if not 1 <= sectors <= 128:
        raise ValueError('invalid sector count')

    def offset(address, size):
        at = address - BASE
        if at < 0 or at + size > len(data):
            raise ValueError(f'object pointer outside bank: {address:#x}')
        return at

    def word(address):
        return struct.unpack_from('<I', data, offset(address, 4))[0]

    sets, seen, address = [], set(), BASE
    while address:
        if address in seen or len(sets) >= 8:
            raise ValueError('cyclic or oversized mission-set chain')
        seen.add(address)
        at = offset(address, 0x54)
        if data[at:at + 4] != b'JMSc':
            raise ValueError('missing JMSc header')
        records = []
        table = word(address + 0x34)
        if table:
            offset(table, sectors * 4)
            for zone in range(sectors):
                pointer = word(table + zone * 4)
                if not pointer:
                    continue
                for _ in range(512):
                    record = word(pointer)
                    pointer += 4
                    if not record:
                        break
                    at = offset(record, 0x28)
                    kind, x, y, z, _, yaw = struct.unpack_from('<IfffIi', data, at)
                    if kind > 0x40 or not all(math.isfinite(v) for v in (x, y, z)):
                        raise ValueError(f'invalid placement at {record:#x}')
                    records.append((zone, kind, x, y, z, yaw * 2 * math.pi / 65536))
                else:
                    raise ValueError('unterminated sector object list')
        sets.append(records)
        address = word(address + 0x50)
    out = bytearray(struct.pack('<4sI', b'SOB1', len(sets)))
    for records in sets:
        out.extend(struct.pack('<I', len(records)))
        for record in records:
            out.extend(struct.pack('<IIffff', *record))
    return bytes(out)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    group = parser.add_mutually_exclusive_group(required=True)
    group.add_argument('--stage', type=int, choices=SECTORS)
    group.add_argument('--sectors', type=int, help='explicit count for another source bank')
    args = parser.parse_args()
    out = convert(args.source.read_bytes(), SECTORS[args.stage] if args.stage else args.sectors)
    args.output.write_bytes(out)
    print(f'{args.source}: {len(out)} bytes -> {args.output}')


if __name__ == '__main__':
    main()
