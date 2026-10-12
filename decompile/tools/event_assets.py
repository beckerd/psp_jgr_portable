#!/usr/bin/env python3
"""Import every EVENT object's original mesh/CNK bindings and shared audio.

The catalog preserves typed actors, world tracks, explicit sound commands
and unresolved runtime opcodes. It does NOT enable unaudited campaign scenes.
CAS1 is a seekable source dependency archive, not a resident sound buffer:
header {magic,count}, records {char folder[16],index,offset,length,reserved}.
"""
import argparse
import json
import re
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from cutscene_sources import (ROOT, DEC, EVENT_BASE, Image, executable,
    afs_file, fingerprint, event_descriptors, texture_lists, texture_image, check_untextured)

PSP = ROOT / 'psp'


def typed_binding(actor_type):
    ex = executable()
    assert 0 <= actor_type < 26
    # Source 06f630 forwards attachments to their owning loader; type 4
    # shares type 1's bank but has a DIFFERENT root in 06fa40's table.
    owner = {23: 5, 24: 8, 25: 9}.get(actor_type, actor_type)
    record = ex.u(ex.u(0x8c05be50) + owner * 4)
    assert ex.u(record) == ex.u(record + 32) == 0x19
    assert ex.u(record + 20) == 0 and ex.u(record + 52) == 2
    txr = afs_file('ENEMY', ex.u(record + 4))
    dat = afs_file('ENEMY', ex.u(record + 36))
    descriptor = ex.u(ex.u(0x8c06fb04) + actor_type * 4)
    root = ex.u(ex.u(descriptor))
    lists, offsets = texture_lists(ex, ex.u(record + 12), txr.read_bytes(), 0x8ca00000)
    texlist = ex.u(ex.u(descriptor + 4))
    return dict(type=actor_type, root=root, texlist=texlist, flags=2 if actor_type in (10, 13) else 1,
        source=str(dat.relative_to(DEC)), textures=str(txr.relative_to(DEC)),
        source_base=0x8ca00000, texture_map=lists[texlist], texture_offsets=offsets,
        # 0cc958 / 0cc930 use a PLAY variant in a live mission. Keep this
        # branch explicit; the standalone enemy hierarchy is not a substitute.
        gameplay_character={18: 11, 19: 10, 20: 12}.get(actor_type),
        gameplay_variant='source 013e94(type,3)' if 18 <= actor_type <= 20 else None)


def catalog():
    source = (PSP / 'src/scene.c').read_text().split('static int command(')[1].split('static int commands(')[0]
    handled = {int(v, 0) for v in re.findall(r'case\s+(0x[0-9a-fA-F]+|\d+)\s*:', source)} | {0, 1, 2, 99, 100}
    result, seen = [], {}
    for entry in event_descriptors():
        row = dict(entry)
        if entry['source_index'] in seen:
            row['asset_descriptor'] = seen[entry['source_index']]
            result.append(row)
            continue
        seen[entry['source_index']] = entry['id']
        row['asset_descriptor'] = entry['id']
        im = Image((DEC / entry['source']).read_bytes(), EVENT_BASE)
        commands = [(phase, pc, w) for phase in range(3) for pc, w in im.commands(im.u(EVENT_BASE + 4 * phase))]
        table = next((w[1] for _, _, w in commands if w[0] == 0x56), 0)
        texture_data = texture_image(DEC / entry['textures']) if entry['id'] == 91 else (DEC / entry['textures']).read_bytes()
        lists, offsets = texture_lists(im, table, texture_data, 0x8cda0000) if table else ({}, [])
        models, typed_slots, object_slots = [], {}, {}
        bindings = {}
        audio, ops = [], set()
        for phase, pc, w in commands:
            code = w[0]; ops.add(code)
            if code == 0x21:
                kind, slot = w[1:3]
                key = ('typed', kind)
                if key not in bindings:
                    bindings[key] = len(models)
                    models.append(dict(typed_binding(kind), role='typed', motions=[], njs_motions=[]))
                typed_slots[slot] = bindings[key]
            elif code == 0x2d:
                slot, root, tl, flags = w[1:]
                key = ('object', root, tl, flags)
                if key not in bindings:
                    if not tl:
                        check_untextured(im, root)
                    bindings[key] = len(models)
                    models.append(dict(role='object', root=root, texlist=tl, flags=flags,
                        source=entry['source'], textures=entry['textures'], source_base=EVENT_BASE,
                        texture_map=lists[tl] if tl else [], texture_offsets=offsets,
                        motions=[], njs_motions=[]))
                    if entry['id'] == 90 and slot == 2:
                        models[-1]['cull'] = 'none'  # Existing source-verified beam draw path.
                object_slots[slot] = bindings[key]
            elif code in (0x25, 0x26, 0x27, 0x28, 0x30, 0x31, 0x32, 0x33, 0x34):
                slots = typed_slots if code < 0x2d else object_slots
                model = models[slots[w[1]]]
                dest = model['njs_motions' if code == 0x32 else 'motions']
                if w[2] not in dest:
                    im.read(w[2], 12 if code == 0x32 else 8)
                    dest.append(w[2])
            if code in (0x29, 0x2a, 0x35, 0x36, 0x43, 0x4d, 0x59, 0x5a):
                audio.append(dict(phase=phase, pc=pc, opcode=code, arguments=list(w[1:])))
        for i, model in enumerate(models):
            model['asset'] = f'event{entry["id"]:03d}_m{i:02d}.jgc'
            model['motion_source'] = entry['source']
            if entry['id'] == 91 and model['role'] == 'object':
                model['texture_sector_tail'] = True
        row.update(models=models, opcodes=sorted(ops), unsupported_vm_opcodes=sorted(ops - handled),
            audio_commands=audio, script_sha256=fingerprint(DEC / entry['source'])['sha256'],
            texture_sha256=fingerprint(DEC / entry['textures'])['sha256'],
            runtime_status='Runtime routes and callback evidence are tracked in dialogue_audit_20260912/coverage.json; import alone does not enable a descriptor')
        result.append(row)
    return result


def dependency_archive():
    # Full source banks avoid dropping implicit effect voices, radio calls,
    # short CV variants or Japanese commands that static EVENT scanning
    # alone cannot enumerate. These are ORIGINAL bytes, never pre-mixed audio.
    folders = ('EVENT', 'ENEMY', 'E_SND_SE', 'SND_SE', 'E_SND_MUSEN', 'SND_MUSEN')
    files = [p for folder in folders for p in sorted((DEC / 'afs' / folder).iterdir()) if p.is_file()]
    table = bytearray(32 * len(files));payload = bytearray();rows = [];stored = {}
    for slot, path in enumerate(files):
        row = fingerprint(path);index = int(path.name.split('_')[0]);folder = path.parent.name
        data = path.read_bytes();key = (row['bytes'], row['sha256'])
        if key not in stored:
            stored[key] = 8 + len(table) + len(payload);payload.extend(data)
        off = stored[key]
        struct.pack_into('<16s4I', table, 32 * slot, folder.encode(), index, off, len(data), 0)
        rows.append(dict(row, folder=folder, index=index, offset=off))
    return b'CAS1' + struct.pack('<I', len(files)) + table + payload, rows


def bake(model, output):
    args = [sys.executable, str(DEC / 'tools/jgc_convert.py'), str(output),
        f'--motions={len(model["motions"])}', '--source-material=1', '--source-environment=1',
        '--texture-offsets=' + ','.join(map(str, model['texture_offsets']))]
    if model.get('texture_sector_tail'):
        args.append('--texture-sector-tail=1')
    spec = (f"Event_{model['root']:x}:{model['source']}:{model['textures']}:0,0,0,0:"
            f"rawat:{model['source_base']:x}:{model['root']:x}:" +
            (','.join(f'{p:x}' for p in model['motions']) or '0'))
    if model['texture_map']:
        spec += ':texmap=' + ','.join(map(str, model['texture_map']))
    spec += ':lighting=' + ('2' if model['flags'] & 2 else '1')
    spec += f':motionimage={model["motion_source"]}@{EVENT_BASE:x}'
    if 'cull' in model:
        spec += ':cull=' + model['cull']
    subprocess.run(args + [spec], cwd=DEC, check=True, stdout=subprocess.DEVNULL)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--bake', action='store_true')
    args = ap.parse_args()
    events = catalog();archive, files = dependency_archive()
    ex = executable()
    info = dict(format=1, events=events, dependencies=files,
        object_sound_table=[struct.unpack('<iI', ex.read(0x8c1ce468 + i * 8, 8)) for i in range(8)],
        note='Source import and converted assets only. Typed PLAY variant selection, remaining '
             'VM/effect/audio handlers and campaign routing still require playback recovery.')
    outputs = {PSP / 'cutscene_sources.bin': archive,
               PSP / 'events.json': (json.dumps(info, indent=2) + '\n').encode()}
    for path, data in outputs.items():
        if args.check:
            assert path.read_bytes() == data, path
        else:
            path.write_bytes(data)
    for event in events:
        for model in event.get('models', []):
            path = PSP / model['asset']
            if args.bake:
                if args.check:
                    with tempfile.TemporaryDirectory(prefix='jgr-event-import-') as tmp:
                        rebuilt = Path(tmp) / path.name;bake(model, rebuilt)
                        assert rebuilt.read_bytes() == path.read_bytes(), path
                else:
                    bake(model, path)
            if args.check or args.bake:
                data = path.read_bytes();assert data[:4] in (b'JGC2', b'JGC3')
                h = struct.unpack_from('<16I', data, 4)
                assert h[0] == 1 and h[6] == len(model['motions']), path
                assert data[:4] == b'JGC3' or h[2] == h[3] == 0, path
        if event.get('models'):
            print(f'EVENT {event["id"]}: {len(event["models"])} imported hierarchies; '
                  f'unimplemented VM opcodes {event["unsupported_vm_opcodes"]}', flush=True)
    print(f'{len(events)} descriptors, {sum(len(e.get("models", [])) for e in events)} model banks, '
          f'{len(files)} source dependencies, {len(archive):,} archive bytes.')


if __name__ == '__main__':
    main()
