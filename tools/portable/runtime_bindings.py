#!/usr/bin/env python3
"""Recover local animation/garage bindings from the verified user disc.

Semantic slot choices reproduce native controller branches. Their corresponding
local clip numbers, frame counts and marker table contents come only from the
supplied executable/banks, never from a development asset or fallback table.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import struct

from source_data_tables import EXECUTABLE_SHA256, IMAGE_BASE

METADATA = Path(__file__).with_name('runtime_bindings.json')
ENEMY_FAMILIES = ('keikan', 'kidoutai', 'inu', 'killer', 'bomb', 'kairiki', 'fire')
EXTRA_ENEMY_FAMILIES = ('para', 'patcar', 'benz', 'heri', 'bheri')


def extract(tree, metadata=None):
    tree = Path(tree)
    metadata = metadata if metadata is not None else json.loads(METADATA.read_text())
    if metadata.get('schema') != 1:
        raise ValueError('Unknown runtime binding metadata schema')
    image = (tree/'decompile/extracted/1ST_READ.BIN').read_bytes()
    if hashlib.sha256(image).hexdigest() != EXECUTABLE_SHA256:
        raise ValueError('Unsupported executable for runtime bindings')
    helper = tree/'decompile/tools/enemy_motion_map.py'
    spec = importlib.util.spec_from_file_location('portable_enemy_motion_map', helper)
    enemy = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(enemy)
    maps, frames = {}, {}
    for family, source in metadata['banks'].items():
        if not re.fullmatch(r'[0-9]{3}_[a-z0-9_]+\.bin', source['path']):
            raise ValueError('Unsafe original enemy bank path')
        data = (tree/'decompile/afs/ENEMY'/source['path']).read_bytes()
        if hashlib.sha256(data).hexdigest() != source['sha256']:
            raise ValueError('Unsupported original enemy bank: ' + family)
        address, count, aliases, root_index = enemy.KNOWN_TABLES[family]
        slots = enemy.source_slots(image, address, count)
        for destination, source_slot in aliases.items():
            slots[destination] = slots[source_slot]
        roots = enemy.object_roots(data, enemy.ENEMY_BASE)
        if root_index >= len(roots):
            raise ValueError('Missing original actor root: ' + family)
        headers = enemy.motion_headers(data, roots[root_index][0], enemy.ENEMY_BASE)
        indices = {offset: index for index, offset in enumerate(headers)}
        maps[family] = [indices.get(offset, -1) for offset in slots]
        frames[family] = [struct.unpack_from('<H', data, offset+2)[0] for offset in headers]
        if any(value > 127 for value in maps[family]) or any(not 0 < value <= 127 for value in frames[family]):
            raise ValueError('Original animation exceeds signed-byte runtime ABI')

    def array(name, values, ctype='signed char', dims=None):
        def literal(item):
            return '{' + ','.join(literal(value) for value in item) + '}' if isinstance(item, (list, tuple)) else str(item)
        dims = dims or '[' + str(len(values)) + ']'
        return 'static const ' + ctype + ' ' + name + dims + '=' + literal(values) + ';'

    outputs = {}
    outputs['main_enemy_maps.inc'] = '\n'.join(array(name, maps[name]) for name in ENEMY_FAMILIES)
    # Keep controller state choices in native code. Only pointer-derived raw
    # JGC slots are supplied by these per-family tables.
    outputs['main_remaining_enemy_maps.inc'] = '\n'.join(
        array('binding_'+name,maps[name]) for name in EXTRA_ENEMY_FAMILIES)
    outputs['main_remaining_enemy_maps.inc'] += '\n'+array(
        'binding_rivals',[maps['rival'+str(i)] for i in range(1,4)],dims='[3][3]')
    squads = []
    # 1ba190 controller substates select idle then wind-up/attack/recover.
    for family in ('keikan', 'kidoutai', 'para', 'killer'):
        clips = [maps[family][slot] for slot in (9, 26, 27, 28)]
        if min(clips) < 0:
            raise ValueError('Missing original formation-member motion')
        squads.append([clips, [frames[family][clip] for clip in clips]])
    outputs['npc_squad_anim.inc'] = array('SQUAD_ANIMS', squads, 'SquadAnim')
    # menu_effects.py packs these original U_GARAGE descriptor slots in this
    # order. Only pointer relocation metadata is held in the converter.
    marker_slots = (0x140, 0x144, 0x148, 0x14c, 0x150, 0x158, 0x15c, 0x160, 0x164)
    marker_index = {0x8c800000 + offset: index for index, offset in enumerate(marker_slots)}
    pointers = struct.unpack_from('<24I', image, 0x8c10dd64-IMAGE_BASE)
    if any(pointer not in marker_index for pointer in pointers):
        raise ValueError('Unknown original garage marker binding')
    outputs['main_mark_model.inc'] = array('MARK_MODEL', [marker_index[p] for p in pointers], 'unsigned char')
    sniper = maps['sniper']
    outputs['sniper_motion.inc'] = array('primary', [[sniper[s] for s in slots] for slots in ((0, 1, 2, 6), (3, 4, 5, 6))], dims='[2][4]')
    outputs['sniper_motion.inc'] += '\n' + array('turn', [sniper[s] for s in (9, 10)])
    # Source 1b77e0 handlers; slot21 serves both grab substates, with the
    # front-contact override left in its native controller branch.
    semantic = (11, 23, 8, 20, 12, 13, 14, 15, 9, 0, 18, 19, 5, 16, 17, 4, 21, 21, 19, 16)
    outputs['police_clips.inc'] = array('clips', [maps['keikan'][s] for s in semantic], 'unsigned char')
    # Source 1b92c8 handler selection; -1 preserves an existing motion.
    semantic = (16, 23, 9, 22, -1, 16, 16, 16, 3, 16, 17, 20, 21, 6, 18, 19, 5, 3)
    outputs['onishima_primary.inc'] = array('raw', [maps['onisima'][s] if s >= 0 else -1 for s in semantic])
    outputs['onishima_overlay.inc'] = array('overlays', [[maps['onisima'][s] for s in slots] for slots in ((13, 14, 15), (10, 11, 12))], dims='[2][3]')
    # Command 0x36's source audio-task records. Native 0e7558 dispatch selects
    # a logical18 program by its five-way case rotation; negative logical
    # records already encode the program in their low word.
    program=[]
    for i in range(8):
        logical,reference=struct.unpack_from('<2i',image,0x8c1ce468-IMAGE_BASE+i*8)
        if logical<0:value=reference&0xffff
        elif logical==18:
            if not 0<=reference<5:raise ValueError('Unknown logical18 scene program')
            value=(reference+3)%5
        else:value=reference
        program.append(value)
    outputs['scene_command_program.inc']=array('program',program,'int')
    # Original animated texture pointers order a compact PSP bank. The
    # referenced source files are identity-checked before invoking the same
    # layout reader used by the mesh/texture exporter.
    sources=metadata.get('sources',{})
    required_sources={'decompile/afs/STAGELAST/000_lasttxr.bin',
                      'decompile/afs/STAGELAST/001_lastdat.bin'}
    if set(sources)!=required_sources:
        raise ValueError('Original emitter source inventory differs from review')
    for relative,expected_sha in sources.items():
        if not re.fullmatch(r'decompile/afs/STAGELAST/[0-9]{3}_[a-z0-9_]+\.bin',relative):
            raise ValueError('Unsafe original emitter source path')
        if hashlib.sha256((tree/relative).read_bytes()).hexdigest()!=expected_sha:
            raise ValueError('Unsupported original emitter source: '+relative)
    emitter_spec=importlib.util.spec_from_file_location('portable_final_emitter_assets',tree/'decompile/tools/final_emitter_assets.py')
    emitter=importlib.util.module_from_spec(emitter_spec);emitter_spec.loader.exec_module(emitter)
    outputs['final_emitter_texmap.inc']=array('texmap',emitter.manifest()['compact_map'],'unsigned')
    result = {name: text.encode('ascii') for name, text in outputs.items()}
    expected = metadata.get('outputs', {})
    if set(result) != set(expected):
        raise ValueError('Runtime binding output inventory differs from review')
    for name, data in result.items():
        if hashlib.sha256(data).hexdigest() != expected[name]:
            raise ValueError('Runtime binding differs from reviewed declaration: ' + name)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tree', type=Path, required=True)
    args = parser.parse_args()
    try:
        outputs = extract(args.tree)
        target = args.tree/'psp/src/generated/source_data'
        target.mkdir(parents=True, exist_ok=True)
        for name, data in outputs.items():
            (target/name).write_bytes(data)
        print('Recovered %d runtime binding groups from verified user-disc inputs' % len(outputs))
    except (OSError, ValueError, KeyError, struct.error) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
