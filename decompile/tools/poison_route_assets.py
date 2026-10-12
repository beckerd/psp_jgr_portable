#!/usr/bin/env python3
"""Lossless original S2M03 escape paths, poses and skate-effect inputs.

Only active controller sections are stored. Live mode-3 controllers retain
approach waits, tagging and section transitions; samples cannot drift when the
PSP's collision reconstruction differs from the original executable.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import struct
import tempfile
from title_route_assets import ROOT, bake
from analyze_poison_routes import layout

SOURCE = ROOT / 'decompile/campaign_captures/poison_routes_20260918/dreamcast_poses'


def source_sections(folder, slot, routes=None):
    data = (folder / f'rival{slot}.bin').read_bytes()
    assert data[:8] == struct.pack('<4sI', b'PRT2', 4192)
    assert (len(data) - 8) % 4192 == 0
    if routes is None: routes = layout()
    sections = [[], [], []]
    for at in range(8, len(data), 4192):
        frame = data[at:at + 4192]
        word = lambda off: struct.unpack_from('<I', frame, off)[0]
        if not word(0xc48):
            continue
        seg = word(0xc50)
        assert seg < 3
        assert word(0x98) == len(sections[seg]) + 1
        route = routes[slot, seg]
        index = (word(0xc00) - route['stream'] - 32) // 4
        assert 0 <= index < len(route['records'])
        assert frame[0xbfc:0xc00] == bytes(route['records'][index])
        sections[seg].append(frame)
    for seg, frames in enumerate(sections):
        assert len(frames) == routes[slot, seg]['active_ticks']
        expected = routes[slot, seg]['start']
        assert list(struct.unpack_from('<3f', frames[0], 0xb8)) == expected
    return sections


def pack_sections(sections, character):
    counts = [len(section) for section in sections]
    frames = [f for section in sections for f in section]
    with tempfile.TemporaryDirectory(prefix='jgr-poison-bake-') as tmp:
        path = Path(tmp)
        (path / 'title_actor_camera.bin').write_bytes(
            struct.pack('<4s3I', b'TAC2', 0x900, 0x290, 0xd0) + b''.join(frames))
        with (path / 'title.csv').open('w', newline='') as stream:
            writer = csv.DictWriter(stream, fieldnames=['character'])
            writer.writeheader()
            writer.writerows({'character': character} for _ in frames)
        data, report = bake(path)
    old_stride = struct.unpack_from('<I', data, 16)[0]
    header_size = 512 + struct.unpack_from('<I', data, 20)[0] * 4
    packed = bytearray(data[:header_size])
    packed[:4] = b'TRT3'
    struct.pack_into('<I', packed, 16, old_stride + 12)
    for i, source in enumerate(frames):
        frame = bytearray(data[header_size+i*old_stride:header_size+(i+1)*old_stride])
        pointer = struct.unpack_from('<I', source, 0x594)[0]
        pose = source[0xc60:]
        table = struct.unpack_from('<I', pose, 4)[0] - pointer
        # Retain channel presence for GF's own bind tree (019cfc).
        # The resolved PLAY values alone cannot recover absent channels.
        for joint in range(20):
            flags = struct.unpack_from('<I', pose, table + joint*16)[0]
            keyed = (flags & 1) | ((flags & 16) >> 3) | (flags & 4)
            ev = struct.unpack_from('<H', frame, 70 + joint*8)[0]
            assert ev < 8192
            struct.pack_into('<H', frame, 70 + joint*8, ev | keyed << 13)
        packed += frame
        packed += source[0x54c:0x558] if struct.unpack_from('<I', source, 0x548)[0] else bytes(12)
    data = packed
    starts = [0, counts[0], counts[0] + counts[1]] if len(counts)==3 else [0,counts[0],counts[0]]
    struct.pack_into('<2I', data, 24, *starts[1:])
    return data, report, starts, old_stride


def build(folder=SOURCE, check=False, *, prefix="poison", character=10, bank=12, subset=0):
    assert 'POISON_ROUTES complete all nine segments and return-to-start' in (folder / 'flycast.log').read_text()
    routes = layout(ROOT / f"decompile/afs/MISSIONSET/{bank:03d}_mission_set_{bank:02d}.bin", subset)
    reports = []
    for slot in range(3):
        sections = source_sections(folder, slot, routes)
        counts = [len(frames) for frames in sections]
        frames = [f for section in sections for f in section]
        data, report, starts, old_stride = pack_sections(sections, character)
        target = ROOT / f'psp/{prefix}_rival{slot}.bin'
        if check:
            assert target.read_bytes() == data, target
        else:
            target.write_bytes(data)
        report.update(member=slot, section_counts=counts, section_starts=starts,
                      stride=old_stride+12, bytes=len(data),
                      controller_sha256=hashlib.sha256((ROOT / f'psp/set{bank}_ctrl.bin').read_bytes()).hexdigest(),
                      source=str((folder / f'rival{slot}.bin').relative_to(ROOT)),
                      capture_sha256=hashlib.sha256((folder / f'rival{slot}.bin').read_bytes()).hexdigest())
        if subset:report['controller_subset']=subset
        reports.append(report)
    manifest = json.dumps(reports, indent=2) + '\n'
    target = ROOT / f'psp/{prefix}_routes.json'
    if check:
        assert target.read_text() == manifest
    else:
        target.write_text(manifest)
    print(f'Original {prefix} routes:', sum(r['frames'] for r in reports), 'ticks,',
          sum(r['bytes'] for r in reports), 'bytes; all nine sections')
    return reports


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=SOURCE)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    build(args.source, args.check)
