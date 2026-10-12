#!/usr/bin/env python3
"""Compare completed S2M03 actor trajectories, not just controller exports.

Read the original actor/camera/controller snapshots made by
flycast_poison_routes.lua and the port's --rival-route-test runtime log.
Align by (member, section, record, repeat) so a controller clock offset cannot
hide movement errors. This reports measured differences; completion alone is
never treated as route parity.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'decompile/afs/MISSIONSET/012_mission_set_12.bin'
BASE = 0x8cde6000
STRIDE = 0xc60


def layout(source=SOURCE, subset=0):
    from mission_chain import chain_offsets
    data = source.read_bytes()
    def word(address):
        return struct.unpack_from('<I', data, address - BASE)[0]
    tables = word(BASE + chain_offsets(data, b'JMSc')[subset] + 0x44)
    assert word(tables + 12) == 0
    result = {}
    for slot in range(3):
        table = word(tables + slot * 4)
        for section in range(3):
            stream = word(table + section * 12)
            header = struct.unpack_from('<I4f3i', data, stream - BASE)
            records = []
            offset = stream - BASE + 32
            while True:
                record = tuple(data[offset:offset + 4])
                assert len(record) == 4
                if record[0] & 0x80:
                    break
                records.append(record)
                offset += 4
            result[slot, section] = dict(
                stream=stream, character=header[0], speed=header[1],
                start=list(header[2:5]), angles=list(header[5:8]),
                flags=word(table + section * 12 + 8),
                activation_distance=struct.unpack_from('<f', data, table - BASE + 4)[0],
                records=records,
                # The parent controller advances BEFORE the first child tick.
                active_ticks=sum(r[0] + 1 for r in records) - 1)
        assert [result[slot, s]['flags'] for s in range(3)] == [1, 1, 3]
    return result


def original_rows(folder, routes):
    assert 'POISON_ROUTES complete all nine segments and return-to-start' in (folder / 'flycast.log').read_text()
    rows = []
    for slot in range(3):
        data = (folder / f'rival{slot}.bin').read_bytes()
        stride = struct.unpack_from('<I', data, 4)[0]
        assert (data[:4], stride) in ((b'PRT1', STRIDE), (b'PRT2', STRIDE + 1024))
        assert (len(data) - 8) % stride == 0
        for sample, offset in enumerate(range(8, len(data), stride), 1):
            def u(at): return struct.unpack_from('<I', data, offset + at)[0]
            def f(at): return struct.unpack_from('<f', data, offset + at)[0]
            section = u(0xc50)
            route = routes[slot, section]
            pointer = u(0xc00) - route['stream'] - 32
            assert pointer % 4 == 0 and 0 <= pointer // 4 < len(route['records'])
            row = dict(slot=slot, sample=sample, segment=section,
                       started=u(0xc48), record=pointer // 4, repeat=u(0xc08),
                       tick=u(0x98), x=f(0xb8), y=f(0xbc), z=f(0xc0),
                       state=u(0x70), motion=u(0x78), frame=f(0x5ac),
                       yaw=u(0x168) & 65535, speed=f(0xdc),
                       camera_yaw=u(0x9c4) & 65535,
                       boost=u(0x10c), boost_cooldown=u(0x110))
            rows.append(row)
        last=rows[-1]
        # The completion marker checks all three live parent controllers.
        # A vblank may observe the final parent returning to section zero
        # before its child's next camera snapshot; accept its last completed
        # active tick too. compare() still requires every active input/tick.
        returned=last['segment']==0 and not last['started']
        final_tick=last['segment']==2 and last['started'] and last['tick']==routes[slot,2]['active_ticks']
        assert returned or final_tick, (slot,last)
    return rows


def port_rows(path):
    text = path.read_text()
    assert 'POISON_ROUTE complete all nine segments and return-to-start writable=0' in text
    rows = []
    floats = {'x', 'y', 'z', 'frame', 'speed'}
    for line in text.splitlines():
        if not line.startswith('ROUTE_SAMPLE '):
            continue
        values = dict(field.split('=', 1) for field in line.split()[1:])
        values.update(zip(('x', 'y', 'z'), values.pop('xyz').split(',')))
        row = {k: float(v) if k in floats else int(v) for k, v in values.items()}
        row['camera_yaw'] &= 65535
        rows.append(row)
    return rows


def compare(dc, port, routes):
    results = []
    def pos(r): return [r[k] for k in ('x', 'y', 'z')]
    for key, route in routes.items():
        def active(rows):
            return [r for r in rows if (r['slot'], r['segment']) == key and r['started']]
        a, b = active(dc), active(port)
        assert len(a) == route['active_ticks'], (key, len(a), route['active_ticks'])
        assert [r['tick'] for r in a] == list(range(1, len(a) + 1)), key
        assert [r['tick'] for r in b] == list(range(1, len(b) + 1)), key
        inputs = {(r['record'], r['repeat']): r for r in b}
        assert len(inputs) == len(b), 'duplicate port input/tick samples'
        pairs = [(r, inputs[r['record'], r['repeat']]) for r in a]
        errors = [math.dist(pos(r), pos(s)) for r, s in pairs]
        over_one = next((r['tick'] for (r, _), e in zip(pairs, errors) if e > 1), None)
        exact = lambda r, s: all(struct.pack('<f', r[k]) == struct.pack('<f', s[k]) for k in ('x', 'y', 'z', 'frame', 'speed')) and all(r[k] == s[k] for k in ('state', 'motion', 'tick'))
        results.append(dict(
            exact_source_samples=sum(exact(r,s) for r,s in pairs),
            member=key[0] + 1, section=key[1] + 1,
            source_stream=f"{route['stream']:08x}", source_start=route['start'],
            source_angles_rx_yaw_rz=route['angles'],
            source_active_ticks=len(a), port_active_ticks=len(b),
            matched_input_ticks=len(pairs),
            input_tick_offsets=sorted({s['tick'] - r['tick'] for r, s in pairs}),
            first_source_tick_over_one_unit=over_one,
            max_position_error=max(errors),
            rms_position_error=math.sqrt(sum(e * e for e in errors) / len(errors)),
            end_position_error=errors[-1], source_end=pos(a[-1]), port_end=pos(b[-1]),
            source_end_state=a[-1]['state'], port_end_state=b[-1]['state']))
    return results


def write_csv(path, rows):
    with path.open('w', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--dreamcast', type=Path, required=True)
    parser.add_argument('--port-log', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--mission',choices=('S1M03','S2M03','S3M03','S3M05'),default='S2M03')
    args = parser.parse_args()
    bank = {'S1M03':3,'S2M03':12,'S3M03':22,'S3M05':24}[args.mission]
    source = ROOT/f'decompile/afs/MISSIONSET/{bank:03d}_mission_set_{bank:02d}.bin'
    routes = layout(source, 1 if args.mission=='S3M05' else 0)
    dc, port = original_rows(args.dreamcast, routes), port_rows(args.port_log)
    sections = compare(dc, port, routes)
    args.out.mkdir(parents=True, exist_ok=True)
    write_csv(args.out / 'dreamcast_completed.csv', dc)
    write_csv(args.out / 'port_completed.csv', port)
    files = [source, args.port_log, *(args.dreamcast / f'rival{i}.bin' for i in range(3))]
    report = dict(
        result='EXACT_FLOAT32_SAMPLES' if all(r['exact_source_samples']==r['source_active_ticks']==r['port_active_ticks'] for r in sections) else 'MISMATCH' if any(r['max_position_error'] > 1 for r in sections) else 'WITHIN_ONE_WORLD_UNIT',
        alignment='same source input record and repeat countdown; Euclidean distance in game world units',
        captures='completed original actor/camera callbacks and completed port player updates; supplied player approaches, no tags',
        hashes={str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in files},
        sections=sections)
    (args.out / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n')
    print('member section DC_ticks PSP_ticks offset max_error end_error')
    for row in sections:
        print(row['member'], row['section'], row['source_active_ticks'], row['port_active_ticks'],
              row['input_tick_offsets'], round(row['max_position_error'], 3), round(row['end_position_error'], 3))
    print(report['result'])


if __name__ == '__main__':
    main()
