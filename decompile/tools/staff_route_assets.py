#!/usr/bin/env python3
"""Bake each selected skater's completed original STAFF performance.

TAC2 actor/camera/CNK samples become a streamed TRT2 route. SRT1 contains
nine explicit replay-clock segments and sparse spray/wall-paint state cues.
Missing CNK channels resolve against the captured normal/spray bind tree;
this is a fully resolved pose, never a Beat path reused for another skater.
"""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
MODELS = (4, 6, 3, 2, 1, 7, 10, 0, 15, 8, 13, 11, 12, 5, 9, 14)
TAC_HEADER = struct.pack('<4s3I', b'TAC2', 0x900, 0x290, 0xd0)
TAC_STRIDE = 4192
META_LIMIT = 128 * 1024
SEGMENT = struct.Struct('<6I')
SPRAY = struct.Struct('<4If3f')
GRAFFITI = struct.Struct('<8I')


def require(value, message):
    if not value:
        raise ValueError(message)


def integer(value):
    return int(value, 0) if isinstance(value, str) and value.lower().startswith('0x') else int(value)


def rows(path):
    with path.open(newline='') as stream:
        return list(csv.DictReader(stream))


def bind_pose(bank, model):
    require(bank[:4] == b'JGC2', 'Expected JGC2 bind bank')
    nchars = struct.unpack_from('<I', bank, 4)[0]
    char_off, bone_off = struct.unpack_from('<2I', bank, 32)
    require(model < nchars, 'Bind model outside bank')
    bone0, count = struct.unpack_from('<2I', bank, char_off + model * 48)
    require(count == 20, 'STAFF requires the complete 20-joint skater')
    return [list((*struct.unpack_from('<3f3i3f', bank, bone_off + (bone0+j)*48 + 4),
                  struct.unpack_from('<H', bank, bone_off + (bone0+j)*48 + 2)[0]))
            for j in range(count)]


def decode_pose(frame, bind):
    pointer = struct.unpack_from('<I', frame, 0x594)[0]
    pose = frame[0xc60:]
    address = struct.unpack_from('<I', pose, 4)[0] - pointer
    require(0 <= address <= len(pose)-20*16, 'CNK table outside atomic capture')
    joints = []
    for j in range(20):
        flags, pos, ang, scale = struct.unpack_from('<4I', pose, address+j*16)
        local = list(bind[j])
        for flag, at, size, fmt, start, ev in (
                (1, pos, 12, '3f', 0, 1), (16, ang, 6, '3h', 3, 2),
                (4, scale, 12, '3f', 6, 4)):
            if flags & flag:
                offset = at-pointer
                require(0 <= offset <= len(pose)-size, 'CNK channel outside atomic capture')
                local[start:start+3] = struct.unpack_from('<'+fmt, pose, offset)
                local[9] &= ~ev
        require(all(math.isfinite(v) for v in local[:9]), 'Nonfinite local pose')
        joints.append(local)
    return joints


def signed_angle(value):
    return (value+32768) % 65536-32768


def effects(frame):
    u = lambda off: struct.unpack_from('<I', frame, off)[0]
    f = lambda off: struct.unpack_from('<f', frame, off)[0]
    result = struct.pack('<7i41f3i',
        *[signed32(u(off)) for off in (0x5cc, 0x78, 0x1dc, 0x1e4, 0x108, 0x10c, 0x548)],
        f(0x5ac), f(0xdc), *[f(0x820+k*4) for k in range(3)],
        *[f(0x684+j*12+k*4) for j in (*range(8), 19, 20, 21, 22) for k in range(3)],
        *[signed32(u(off)) for off in (0x16c, 0x168, 0x170)])
    require(all(math.isfinite(v) for v in struct.unpack_from('<41f', result, 28)),
            'Nonfinite skate effect input')
    require(u(0x78) <= 255 and u(0x1e4) < 6, 'Invalid skate effect selector')
    return result


def signed32(value):
    return value if value < 0x80000000 else value-0x100000000


def spray_state(frame, index, variant):
    u = lambda off: struct.unpack_from('<I', frame, off)[0]
    f = lambda off: struct.unpack_from('<f', frame, off)[0]
    active = int(u(0x294) != 0)
    # Inactive overlay fields are semantically unused and may retain stale
    # references. Keep a deterministic reset cue at each row boundary.
    ref, pose_frame, target = (u(0x2b8), f(0x29c), tuple(f(0x2a0+k*4) for k in range(3))) if active else (0, 0., (0., 0., 0.))
    require(ref <= 65535 and all(math.isfinite(v) for v in (pose_frame, *target)),
            'Invalid spray overlay state')
    return (index, active, variant, ref, pose_frame, *target)



def validate_controller_prefix(frame, row, staff_row, player_tick, staff_image):
    """Prove the first captured pose belongs to the consumed header tick."""
    base = 0x8cfe0000
    stream = struct.unpack_from('<I', staff_image, staff_row*20)[0]
    offset = stream-base+32
    require(0 <= offset <= len(staff_image)-4, 'Invalid original STAFF stream')
    record = offset
    repeat = staff_image[record] & 127
    for _ in range(player_tick):
        if repeat:
            repeat -= 1
        else:
            record += 4
            require(record <= len(staff_image)-4, 'Missing creation controller record')
            repeat = staff_image[record] & 127
        require(not staff_image[record] & 128, 'Capture begins after controller completion')
    expected_input = struct.unpack_from('<I', staff_image, record)[0]
    expected = (base+record, repeat, expected_input)
    actual = tuple(struct.unpack_from('<I', frame, 0xb90+off)[0] for off in (0x70, 0x78, 0x6c))
    require(actual == expected, 'Creation-pass controller does not match original STAFF stream')
    require(tuple(integer(row[k]) for k in ('controller_record', 'controller_repeat', 'controller_input')) == actual,
            'Controller CSV/atomic image mismatch')

def decode_capture(source, normal_bank=None, spray_bank=None, require_complete=True):
    data = (source/'title_actor_camera.bin').read_bytes()
    require(data[:16] == TAC_HEADER and (len(data)-16) % TAC_STRIDE == 0, 'Malformed TAC2 capture')
    table = rows(source/'title.csv')
    require(len(table) == (len(data)-16)//TAC_STRIDE and table, 'Capture/CSV sample count mismatch')
    selected = {integer(r.get('selected_character', r['character'])) for r in table}
    require(len(selected) == 1, 'Capture mixes selected characters')
    character = selected.pop()
    require(0 <= character < 16, 'Selected character is not explicit 0..15')
    summary = json.loads((source/'staff_route_metadata.json').read_text())
    require(summary['character'] == character and summary['samples'] == len(table), 'Capture summary mismatch')
    if require_complete:
        require(summary['complete'] and summary['parts'] == 9, 'Refusing incomplete STAFF performance')
    normal = bind_pose(normal_bank if normal_bank is not None else (ROOT/'psp/chars.jgc').read_bytes(), MODELS[character])
    variant_bind = bind_pose(spray_bank if spray_bank is not None else (ROOT/f'psp/spraymesh{MODELS[character]:02d}.jgc').read_bytes(), 0)
    frames, segments, sprays = [], [], []
    previous_part = None
    last_camera = None
    last_spray = None
    staff_image = (ROOT/'psp/staffroll.bin').read_bytes()
    for i, row in enumerate(table):
        frame = data[16+i*TAC_STRIDE:16+(i+1)*TAC_STRIDE]
        u = lambda off: struct.unpack_from('<I', frame, off)[0]
        f = lambda off: struct.unpack_from('<f', frame, off)[0]
        require(integer(row['sample']) == i+1, 'Nonconsecutive capture sample')
        require(integer(row.get('character', character)) == character, 'Selected-character alias mismatch')
        require(u(0x7c) in (16, character), 'Wrong actor character/sentinel')
        if 'actor_character' in row:
            require(integer(row['actor_character']) == u(0x7c), 'Actor-character CSV mismatch')
        part, next_row = integer(row['part']), integer(row['staff_row'])
        # Original STAFF+6c advances after spawning; it points to the NEXT
        # row while the port keeps the currently active replay row.
        staff_row = next_row-1
        require(0 <= part < 9 and staff_row == part+2, 'Wrong STAFF row/part mapping')
        tick = u(0x98)
        require(integer(row.get('player_tick', row['player_clock'])) == tick, 'Player clock CSV mismatch')
        # StaffReplay.tick0 is the creation-pass update; the source camera
        # increments its clock to1 during that same completed callback.
        source_replay_tick = integer(row['replay_tick'])
        require(source_replay_tick == tick, 'Source camera/actor tick mismatch')
        replay_tick = source_replay_tick-1
        variant = int(u(0x560) == u(0x580))
        require(u(0x560) != 0 and integer(row['actor_variant']) == variant, 'Variant tree CSV mismatch')
        if part != previous_part:
            require(part == len(segments), 'Missing, repeated or reordered STAFF part')
            require(0 <= replay_tick <= 8 and 1 <= tick <= 8, 'Uncaptured creation-pass prefix')
            validate_controller_prefix(frame, row, staff_row, tick, staff_image)
            segments.append([staff_row, i, 0, replay_tick, tick, 0])
        segment = segments[-1]
        local = i-segment[1]
        require(tick == segment[4]+local and replay_tick == segment[3]+local,
                'Skipped or duplicate source tick')
        segment[2] += 1
        camera = u(0xb74)
        cut = part != previous_part or (last_camera is not None and camera != last_camera)
        values = [f(0xb8+k*4) for k in range(3)] + [f(0xa90+k*4) for k in range(6)] + [-f(0xab0), -f(0xab4)]
        require(all(math.isfinite(v) for v in values), 'Nonfinite actor/camera')
        require(signed_angle(u(0xaac)) > 0 and 0 < -f(0xab0) < -f(0xab4), 'Invalid camera projection')
        header = struct.pack('<3f3hH6fhh2fIHH', *values[:3],
            *[signed_angle(u(0x678+k*4)) for k in range(3)], u(0x70), *values[3:9],
            signed_angle(u(0xaa8)), signed_angle(u(0xaac)), *values[9:], tick, part, int(cut))
        frames.append((header, decode_pose(frame, variant_bind if variant else normal), effects(frame)))
        spray = spray_state(frame, i, variant)
        expected = None
        if last_spray:
            expected = (i, *last_spray[1:4], last_spray[4]+(i-last_spray[0] if last_spray[1] else 0), *last_spray[5:])
        if part != previous_part or SPRAY.pack(*spray) != (SPRAY.pack(*expected) if expected else None):
            sprays.append(spray)
            last_spray = spray
        previous_part, last_camera = part, camera
    require(len(segments) == summary['parts'], 'Capture part summary mismatch')
    if require_complete:
        require(len(segments) == 9, 'Expected all nine STAFF parts')
    return character, frames, segments, sprays


def pack_route(character, frames):
    require(frames and len(frames) <= 30000, 'Unsupported route length')
    channels, base = [], bytearray()
    components = (0, 1, 2, 6, 7, 8)
    for joint in range(20):
        for component, source_component in enumerate(components):
            values = [struct.pack('<f', pose[joint][source_component]) for _, pose, _ in frames]
            if any(v != values[0] for v in values[1:]):
                channels.append(joint*6+component)
            base += values[0]
    stride = 428+4*len(channels)
    out = bytearray(struct.pack('<4s7I', b'TRT2', len(frames), character, 20, stride, len(channels), 0, 0))
    out += base + struct.pack('<'+'I'*len(channels), *channels)
    for header, pose, fx in frames:
        out += header
        for p in pose:
            out += struct.pack('<3hH', *[signed_angle(int(v)) for v in p[3:6]], p[9])
        for channel in channels:
            joint, component = divmod(channel, 6)
            out += struct.pack('<f', pose[joint][components[component]])
        out += fx
    return bytes(out)


def graffiti_cues(source, frames):
    path = source/'staff_graffiti.csv'
    require(path.exists(), 'Missing source wall-paint cues')
    cues, previous, state = [], None, {}
    table = rows(path)
    frame_of = lambda row: integer(row['frame']) if 'frame' in row else integer(row['sample'])-1
    require(all(frame_of(a) <= frame_of(b) for a, b in zip(table, table[1:])),
            'GF capture moves backward in time')
    # Lua task enumeration order is immaterial for independent targets;
    # the runtime format requires canonical frame/area/index order.
    table.sort(key=lambda row: (frame_of(row), integer(row['area']), integer(row['index'])))
    for row in table:
        frame = integer(row['frame']) if 'frame' in row else integer(row['sample'])-1
        cue = tuple([frame]+[integer(row[k]) for k in ('area', 'index', 'stroke', 'reveal', 'complete', 'started', 'revealing')])
        require(0 <= frame < len(frames) and 0 <= cue[1] <= 65535 and 0 <= cue[2] <= 65535,
                'GF cue outside route or target range')
        require(cue[5] in (0, 1) and cue[6] in (0, 1) and cue[7] in (0, 1, 2), 'Invalid GF flags')
        key = cue[:3]
        require(previous is None or key > previous, 'Unsorted or duplicate GF cue')
        previous = key
        require(all(0 <= x <= 0xffffffff for x in cue), 'Invalid GF unsigned state')
        # Cues already contain absolute source state; do not simulate reveal.
        part = struct.unpack_from('<H', frames[frame][0], 60)[0]
        target = (part, cue[1], cue[2])
        if state.get(target) != cue[3:]:
            cues.append(cue)
            state[target] = cue[3:]
    return cues


def pack_metadata(character, count, segments, sprays, graffiti):
    out = bytearray(struct.pack('<4s7I', b'SRT1', 1, character, count, len(segments), len(sprays), len(graffiti), 0))
    out += b''.join(SEGMENT.pack(*row) for row in segments)
    out += b''.join(SPRAY.pack(*row) for row in sprays)
    out += b''.join(GRAFFITI.pack(*row) for row in graffiti)
    require(len(out) <= META_LIMIT, 'Sparse STAFF metadata exceeds 128 KiB budget')
    return bytes(out)


def bake(source):
    character, frames, segments, sprays = decode_capture(source)
    graffiti = graffiti_cues(source, frames)
    route = pack_route(character, frames)
    metadata = pack_metadata(character, len(frames), segments, sprays, graffiti)
    sha = lambda b: hashlib.sha256(b).hexdigest()
    report = dict(character=character, model=MODELS[character], frames=len(frames), segments=segments,
                  route_bytes=len(route), metadata_bytes=len(metadata), spray_cues=len(sprays),
                  graffiti_cues=len(graffiti), route_sha256=sha(route), metadata_sha256=sha(metadata),
                  sources={p.name:sha(p.read_bytes()) for p in sorted(source.iterdir())
                           if p.name in ('title_actor_camera.bin', 'title.csv', 'staff_route_metadata.json',
                                         'staff_graffiti.csv', 'staff_graffiti.bin', 'staff_spray.csv')},
                  creation_controller_prefixes_verified=len(segments),
                  original_staff_sha256=sha((ROOT/'psp/staffroll.bin').read_bytes()),
                  normal_bind_sha256=sha((ROOT/'psp/chars.jgc').read_bytes()),
                  spray_bind_sha256=sha((ROOT/f'psp/spraymesh{MODELS[character]:02d}.jgc').read_bytes()))
    return route, metadata, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sources', type=Path, nargs='+')
    parser.add_argument('--out', type=Path, default=ROOT/'psp')
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    seen = set()
    for source in args.sources:
        route, metadata, report = bake(source)
        character = report['character']
        require(character not in seen, 'Repeated selected character input')
        seen.add(character)
        for suffix, content in (('.bin', route), ('_meta.bin', metadata),
                                ('.json', (json.dumps(report, indent=2)+'\n').encode())):
            path = args.out/f'staffroute{character:02d}{suffix}'
            if args.check:
                require(path.read_bytes() == content, f'Generated asset differs: {path}')
            else:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(content)
        print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
