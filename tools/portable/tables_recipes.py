#!/usr/bin/env python3
"""Emit source-coordinate and ABI recipes for native mixed-table conversion.

Reads only reviewed source metadata and payload-free storage declarations.
Never reads a disc, generated initializer, converted asset or capture. Numeric
recipe fields are source coordinates, formats, output offsets and operations.
"""
import argparse
import json
from pathlib import Path
import re
import runpy

from source_data_sections import arrays

ROOT = Path(__file__).resolve().parents[2]
TYPES = {'float': ('f', 4), 'unsigned': ('I', 4), 'uint32_t': ('I', 4),
         'int': ('i', 4), 'unsigned char': ('B', 1), 'signed char': ('b', 1)}
WIDTH = {'b': 1, 'B': 1, 'h': 2, 'H': 2, 'i': 4, 'I': 4, 'f': 4}
FLAG_POINTER, FLAG_ZERO, FLAG_NEGATE, FLAG_UNSIGNED, FLAG_COLOR = 1, 2, 4, 8, 16


def initializer(text):
    tokens = re.findall(r'\{|\}|[0-9]+', text)
    position = 0
    def sequence():
        nonlocal position
        if tokens[position] != '{':
            raise ValueError('Expected aggregate')
        position += 1
        result = []
        while tokens[position] != '}':
            if tokens[position] == '{':
                result.append(sequence())
            else:
                result.append(int(tokens[position]))
                position += 1
        position += 1
        return result
    value = sequence()
    if position != len(tokens):
        raise ValueError('Unexpected initializer tokens')
    return value


def positions(values, dimensions):
    """C aggregate placement, retaining implicit zero padding as empty spans."""
    inner = 1
    for dim in dimensions[1:]:
        inner *= dim
    result = {}
    cursor = 0
    for value in values:
        if isinstance(value, list):
            cursor = ((cursor + inner - 1) // inner) * inner
            child = positions(value, dimensions[1:])
            result.update((cursor + at, item) for at, item in child.items())
            cursor += inner
        else:
            result[cursor] = value
            cursor += 1
    if dimensions and dimensions[0] and cursor > dimensions[0] * inner:
        raise ValueError('Initializer exceeds ABI dimensions')
    return result


def layout(repo, group):
    text = (repo / 'psp/src' / ('source_data_' + group['name'] + '.c')).read_text()
    rows = re.findall(r'\{(\d+),(\d+),(\d+),(\d+),&?([A-Za-z_]\w*)\}', text)
    if not rows:
        raise ValueError('Missing bounded ABI metadata: ' + group['name'])
    entries = [dict(identity=int(a), offset=int(b), size=int(c), alignment=int(d), name=e)
               for a, b, c, d, e in rows]
    return entries


def fields(recipe):
    source = recipe['source']
    mode = source.get('mode', 'contiguous')
    kind = source['format']
    result = []
    count = len(recipe['formats'])
    if mode == 'formation_rows':
        for row in range(source['rows']):
            result.extend(dict(address=int(source['table'], 0) + row * 4, add=i * 4,
                               kind=kind, flags=FLAG_POINTER) for i in range((row + 1) * 3))
    else:
        for i in range(count):
            field = dict(address=0, add=0, kind=kind, flags=0)
            if mode == 'contiguous':
                field['address'] = int(source['address'], 0) + i * source.get('stride', WIDTH[kind])
            elif mode == 'addresses':
                field['address'] = int(source['addresses'][i], 0)
            elif mode == 'pointer_field':
                field.update(address=int(source['table'], 0) + i * 4, add=source['offset'], flags=FLAG_POINTER)
            elif mode == 'mixed_record':
                field.update(address=int(source['address'], 0) + i * 4, kind=source['storage'][i])
            elif mode == 'fields':
                value = source['fields'][i]
                if value.get('unused_slot_zero'):
                    field['flags'] = FLAG_ZERO
                else:
                    field.update(address=int(value['address'], 0), kind=value.get('format', kind),
                                 flags=FLAG_UNSIGNED if value.get('unsigned32') else 0)
            else:
                raise ValueError('Unsupported native table mode ' + mode)
            result.append(field)
    if len(result) != count:
        raise ValueError('Source field count mismatch')
    for i, field in enumerate(result):
        if i in source.get('negate_indices', []):
            field['flags'] |= FLAG_NEGATE
        if source.get('swap_red_blue'):
            field['flags'] |= FLAG_COLOR
        fmt = recipe['formats'][i]
        if fmt['style'] not in ('integer', 'hex', 'decimal'):
            raise ValueError('Non-numeric recipe style')
        field['precision'] = fmt['precision'] if fmt['style'] == 'decimal' else -1
    return result


def recipes(repo=ROOT):
    mixed = {v['output']: v for v in json.loads((repo / 'tools/portable/source_data_mixed.json').read_text())['declarations']}
    groups = json.loads((repo / 'tools/portable/source_data_numeric.json').read_text())['groups']
    result = []
    for group in groups:
        if not group['name'].startswith('mixed_'):
            continue
        recipe = mixed[Path(group['input']).name]
        source = recipe['source']
        entries = layout(repo, group)
        data = dict(output='source_data_' + group['name'] + '.jsd', sections=entries,
                    source='decompile/afs/' + source['file'] if 'file' in source else 'decompile/extracted/1ST_READ.BIN',
                    base=int(source.get('base', '0x8c010000'), 0), fields=[],
                    sort=int(bool(source.get('sort'))), bgm=0, bgm_count=0, bgm_names=0, special=0,
                    version=1, fixups=0)
        if source.get('mode') == 'bgm_entries':
            if len(entries) != 1 or entries[0]['size'] != source['count'] * 44:
                raise ValueError('BGM ABI changed')
            data.update(bgm=int(source['address'], 0), bgm_count=source['count'], bgm_names=source['names'])
        else:
            source_fields = fields(recipe)
            serial = iter(range(len(source_fields)))
            skeleton = re.sub(r'@@', lambda _: str(1000000 + next(serial)), recipe['syntax'])
            declarations = arrays(skeleton)
            if len(declarations) != len(entries):
                raise ValueError('Array count disagrees with native ABI')
            assigned = set()
            for declaration, entry in zip(declarations, entries):
                kind, width = TYPES[declaration['type']]
                if entry['name'] != declaration['name'] or entry['size'] % width:
                    raise ValueError('Scalar ABI mismatch')
                dims = [int(v) if v else 0 for v in declaration['dims']]
                for offset, value in positions(initializer(declaration['initializer']), dims).items():
                    if not 1000000 <= value < 1000000 + len(source_fields):
                        if value != 0:
                            raise ValueError('Nonzero literal payload is not a recipe operation')
                        continue
                    index = value - 1000000
                    if index in assigned or (offset + 1) * width > entry['size']:
                        raise ValueError('Invalid output field placement')
                    assigned.add(index)
                    source_fields[index].update(destination=entry['offset'] + offset * width, output_kind=kind)
            if len(assigned) != len(source_fields):
                raise ValueError('Some source fields have no output')
            data['fields'] = source_fields
        result.append(data)
    from tables_sources import extra_recipes
    pointer_groups = json.loads((repo / 'tools/portable/source_data_pointers.json').read_text())['groups']
    pointer_groups = [dict(row, name='ptr_' + row['name']) for row in pointer_groups]
    result.extend(extra_recipes(groups + pointer_groups, lambda group: layout(repo, group), repo))
    return result


def emit(repo=ROOT):
    rows = recipes(repo)
    lines = ['/* Generated by tables_recipes.py. Coordinates, operations and ABI only;',
             ' * original values are read from the user disc during setup. */']
    player_specs = (repo / 'decompile/chars_spec.txt').read_text().splitlines()[:16]
    player_sources = [line.split(':')[1] for line in player_specs]
    if len(player_sources) != 16 or len(set(player_sources)) != 16 or any(
            not re.fullmatch(r'afs/PLAYER/[0-9]{3}_[a-z0-9_]+\.bin', path) for path in player_sources):
        raise ValueError('Expected sixteen original PLAY sources in model packing order')
    lines.append('static const char *const table_player_sources[]={')
    lines += ['    ' + json.dumps('decompile/' + path) + ',' for path in player_sources]
    lines.append('};')
    dialogue_text = (repo / 'decompile/tools/dialogue_assets.py').read_text()
    dialogue_ids = [int(value) for value in re.findall(r'\d+', re.search(r'DIALOGUE_IDS=\((.*?)\)', dialogue_text, re.S)[1])]
    if len(dialogue_ids) != len(set(dialogue_ids)) or any(not 0 <= value < 112 for value in dialogue_ids):
        raise ValueError('Invalid native enabled dialogue route set')
    lines.append('/* Existing native dialogue route selection, not original selector values. */')
    lines.append('static const unsigned char table_dialogue_routes[]={' + ','.join(map(str, dialogue_ids)) + '};')
    banks = json.loads((repo / 'tools/portable/runtime_bindings.json').read_text())['banks']
    banks['taban'] = {'path': '035_tabandat.bin'}
    known = runpy.run_path(str(repo / 'decompile/tools/enemy_motion_map.py'))['KNOWN_TABLES']
    for i, family in enumerate(banks):
        lines.append('#define TABLE_ENEMY_' + family.upper() + ' ' + str(i))
    lines.append('static const EnemyRecipe enemy_recipes[]={')
    for family, bank in banks.items():
        address, count, aliases, root_index = known[family]
        if len(aliases) > 1:
            raise ValueError('Multiple alias rules need an explicit native operator')
        destination, source = next(iter(aliases.items())) if aliases else (-1, -1)
        lines.append('    {%s,0x%08xu,%du,%du,%d,%d},' %
                     (json.dumps('decompile/afs/ENEMY/' + bank['path']), address, count, root_index, destination, source))
    lines.append('};')
    for i, row in enumerate(rows):
        lines.append(f'static const TableSection table_sections_{i}[]={{')
        lines += ['    {%du,%du,%du,%du},' % (v['identity'], v['offset'], v['size'], v['alignment']) for v in row['sections']]
        lines.append('};')
        if row['fields']:
            lines.append(f'static const TableField table_fields_{i}[]={{')
            for f in row['fields']:
                lines.append("    {0x%08xu,%du,%du,'%s','%s',%d,%d}," %
                             (f['address'], f['add'], f['destination'], f['kind'], f['output_kind'], f['flags'], f['precision']))
            lines.append('};')
    lines.append('static const TableRecipe table_recipes[]={')
    for i, row in enumerate(rows):
        lines.append('    {%s,%s,0x%08xu,table_sections_%d,%du,%s,%du,%du,0x%08xu,%du,%du,%du,%du,%du},' %
                     (json.dumps(row['output']), json.dumps(row['source']), row['base'], i, len(row['sections']),
                      f'table_fields_{i}' if row['fields'] else 'NULL', len(row['fields']), row['sort'],
                      row['bgm'], row['bgm_count'], row['bgm_names'], row['special'], row['version'], row['fixups']))
    lines.extend(['};', ''])
    return '\n'.join(lines)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=ROOT)
    parser.add_argument('--out', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    output = args.out or args.repo / 'psp/src/portable_tables_data.inc'
    generated = emit(args.repo)
    if args.check:
        if output.read_text() != generated:
            parser.exit(1, 'Native table recipe metadata is stale\n')
    else:
        output.write_text(generated)
    print(f'{len(recipes(args.repo))} native numeric table recipes')
