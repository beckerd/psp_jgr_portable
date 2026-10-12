#!/usr/bin/env python3
"""JMSc +38 paint (09ff70) and +3c health (09ff7c) -> CAN3 bank.

09fec8 follows +50 from 8cde6000. 04d8a0 selects one of FIVE lists;
04d9d8 destroys that area's instances on departure. Aliased area lists
must not be flattened into simultaneously collectible copies. Repeated
records *within* an authored list remain separate original objects.
CAN3: magic, row count, sub-set count; rows {set,area,source,x,y,z,kind}.
Kinds 0/1 are paint, 2/3 are health. The health record's +10 collected
word is runtime state, reset by 0da2a4 and retained across area reloads.
"""
import argparse
from pathlib import Path
import struct
from mission_chain import chain_offsets

BASE = 0x8cde6000


def convert(data):
    if data[:4] == b'JMSb':
        return struct.pack('<4sII', b'CAN3', 0, 0)
    sets = chain_offsets(data, b'JMSc')

    def word(address):
        if address % 4 or not BASE <= address <= BASE + len(data) - 4:
            raise ValueError(f'out-of-image can pointer {address:08x}')
        return struct.unpack_from('<I', data, address - BASE)[0]

    rows = []
    for si, off in enumerate(sets):
        for field, bias in ((0x38, 0), (0x3c, 2)):
            table = word(BASE + off + field)
            if not table:
                continue
            for area in range(5):
                p = word(table + 4 * area)
                if not p:
                    continue
                count = 0
                while (record := word(p)):
                    kind = word(record + 12)
                    if kind not in (0, 1):
                        raise ValueError(f'unknown pickup type {kind} at {record:08x}')
                    if bias:
                        word(record + 16)  # validate the source collected word
                    rows.append(struct.pack('<III', si, area, record) +
                                data[record-BASE:record-BASE+12] + struct.pack('<I',kind+bias))
                    p += 4
                    count += 1
                    if count > 4096:
                        raise ValueError('unterminated pickup list')
    return struct.pack('<4sII', b'CAN3', len(rows), len(sets)) + b''.join(rows)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source', type=Path)
    ap.add_argument('output', type=Path)
    ap.add_argument('--all', action='store_true', help='convert every mission-set image into output directory')
    args = ap.parse_args()
    pairs = [(args.source, args.output)]
    if args.all:
        pairs = [(p, args.output / f'set{int(p.name[:3])}_cans.bin')
                 for p in sorted(args.source.glob('*_mission_set_*.bin'))]
    for src, dst in pairs:
        out = convert(src.read_bytes())
        dst.write_bytes(out)
        n, ns = struct.unpack_from('<II', out, 4)
        print(f'{src.name}: {ns} sub-sets, {n} area-scoped records -> {dst}')


if __name__ == '__main__':
    main()
