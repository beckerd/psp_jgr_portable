#!/usr/bin/env python3
"""Regenerate retained collision geometry from disc with explicit legacy math.

The shipping static contacts retain the older XYZ placement convention while
live interactive owners use the corrected source convention. Reproduce that
choice from source records; never read historical converted collision banks.
"""
import argparse
import json
from pathlib import Path
import runpy
import struct
import subprocess
import sys
import tempfile


def emit(tree, argv):
    sys.path.insert(0, str(tree / 'decompile/tools'))
    import ninjalib as nl
    def legacy_matrix(pos, ang):
        rx, ry, rz = [value * nl.A2R for value in ang]
        return nl.mmul(nl.mtrans(*pos), nl.mmul(nl.mrotx(rx), nl.mmul(nl.mroty(ry), nl.mrotz(rz))))
    nl.placement_matrix = legacy_matrix
    sys.argv = [str(tree / 'decompile/tools/jgs_collision.py'), *argv]
    runpy.run_path(sys.argv[0], run_name='__main__')


def run_export(tree, argv):
    subprocess.run([sys.executable, str(Path(__file__).resolve()), '--tree', str(tree),
                    '--emit', *argv], cwd=tree / 'decompile', check=True)


def world_end(data):
    offset = struct.unpack_from('<I', data, 8)[0]
    version, _, patches, entries = struct.unpack_from('<4I', data, offset)
    if version != 1:
        raise ValueError('Unsupported world collision metadata')
    return offset + 16 + 1028 * 4 + patches * 32 + entries * 12


def rebuild(tree, stage, output):
    sys.path.insert(0, str(tree / 'decompile/tools'))
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='legacy-collision-', dir=output.parent) as tmp:
        raw = Path(tmp) / 'unfiltered.col'
        if stage == 2:
            import kogane_collision
            argv = [value for value in kogane_collision.command(raw)[2:] if not value.startswith('--dynexclude=')]
            run_export(tree, argv)
            data = raw.read_bytes()
            # The historical input predates CIP1; rebuild computes fresh bounds
            # around the retained legacy triangles after removing live owners.
            raw.write_bytes(data[:world_end(data)])
            kogane_collision.rebuild(output, raw)
        elif stage == 3:
            from city_opening_assets import CONFIG
            from stage_render_assets import Source
            from street_prop_assets import source_records
            c = CONFIG[3]
            groups = {28:[3,14,*range(25,32),40,41,42,49,*range(51,57)],34:[6],
                      32:[11,16,17],27:[13,21],8:[22],7:[32,33,34],30:[35],
                      42:[37,38,39],33:[47],5:[48],65535:[62,63,64]}
            contacts = 'default:31' + ''.join(',%d:%d' % (kind, sound) for sound, kinds in groups.items() for kind in kinds)
            owners = Path(tmp) / 'owners.json'
            run_export(tree, ['afs/STAGE3/000_st3dat.bin', str(raw), '--grid=8cd62bd0',
                '--sectors=45', '--dyn=8c1ba918', '--dynprotos=8c1bc294,70',
                '--dynrec=52', '--dyncontact=' + contacts, '--owners-json=' + str(owners)])
            data = raw.read_bytes()
            count, offset = struct.unpack_from('<II', data, 4)
            records = Source(3).records(c['table'], c['sectors'], 52, c['count'])
            addresses = {r['address'] for r in records}
            wanted = {r['address'] for r in records if r['pid'] in c['types']}
            _, props, _ = source_records(3)
            wanted.update(r['address'] for r in props if r['address'] in addresses)
            removed, seen = set(), set()
            for address, _, first, n in json.loads(owners.read_text()):
                if address in wanted:
                    removed.update(range(first, first+n)); seen.add(address)
            if seen != wanted:
                raise ValueError('Incomplete collision owner coverage')
            triangles = b''.join(data[16+i*44:16+(i+1)*44] for i in range(count) if i not in removed)
            output.write_bytes(struct.pack('<4sIII', b'COL1', count-len(removed), 16+len(triangles), 0x31535043)
                               + triangles + data[offset:world_end(data)])
        elif stage == 5:
            from city_opening_collision import rebuild as current
            current(5, output)
        else:
            raise ValueError('Legacy collision recipe supports stages 2, 3 and 5')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tree', type=Path, required=True)
    parser.add_argument('--stage', type=int, choices=(2, 3, 5))
    parser.add_argument('--out', type=Path)
    parser.add_argument('--emit', nargs=argparse.REMAINDER)
    args = parser.parse_args()
    if args.emit:
        emit(args.tree.resolve(), args.emit)
    elif args.stage and args.out:
        rebuild(args.tree.resolve(), args.stage, args.out.resolve())
    else:
        parser.error('--stage and --out are required')


if __name__ == '__main__':
    main()
