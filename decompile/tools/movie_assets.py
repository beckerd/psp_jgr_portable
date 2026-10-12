#!/usr/bin/env python3
"""Import all 35 original DEMO movies, including their bank-31 narration.

Meshes/materials are converted, sparse actor/camera/light/caption data remain
byte-exact. This is not a claim that every movie's special render flags or
campaign transition has been implemented. --check verifies the installed
import; --bake --check also rebuilds and compares every converted mesh bank.
"""
import argparse
import json
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from cutscene_sources import ROOT, DEC, MOVIE_BASE, afs_file, fingerprint, movie_manifest

PSP = ROOT / 'psp'
# DEMO26 ending, DEMO30 introduction, and DEMO32/33/34 story panels.
# These are full-screen illustrations, not character texture sheets.
PANEL_MOVIES = frozenset((25, 29, 31, 32, 33))


def catalog():
    movies = [movie_manifest(i) for i in range(35)]
    voices = [dict(reference=i, **fingerprint(afs_file('SND_DJTALK', i))) for i in range(184)]
    for movie in movies:
        # Preserve all original 512x1024 panel sheets at 256x512, within the
        # PSP texture limit. The generic actor cap would reduce them to 64x128.
        movie['texture_max'] = 512 if movie['movie'] in PANEL_MOVIES else 128
        # DEMO02 points the camera directly into the Love Shockers' tag.
        # Original GBIX slot 16 is 512x128; the general cap made it 128x32.
        if movie['movie'] == 1:
            movie['texture_max_overrides'] = {'16': 512}
        movie['prior_ending_playback_evidence'] = movie['movie'] in (25, 26)
        movie['visual_parity_verified'] = False
        movie['campaign_route_verified'] = False
        movie['special_render_flags'] = sorted({r['flags'] for s in movie['segments']
            for r in s['actors']} - {1, 2, 0x20, 0x81})
    return dict(format=1, movies=movies, voices=voices,
        note='Import completeness is separate from playback/choreography fidelity. '
             'Campaign routing and visual verification are tracked separately in CUTSCENE_IMPORT.md.')


def tables(info):
    lines = ['/* Generated from 1ST_READ.BIN 1e5284 and all 35 DEMO images. */',
        'static const unsigned char MOVIE_SEGMENTS[35]={' +
        ','.join(str(len(m['segments'])) for m in info['movies']) + '};',
        'static const short MOVIE_VOICES[35][7]={']
    for m in info['movies']:
        values = [s['voice'] for s in m['segments']]
        assert len(values) <= 7
        lines.append('{' + ','.join(map(str, values + [-1] * (7 - len(values)))) + '},')
    lines += ['};', 'static const signed char MOVIE_NEXT[35]={' +
        ','.join(str(m['next_movie']) for m in info['movies']) + '};',
        'static const unsigned char MOVIE_FLAGS[35]={' +
        ','.join(str(m['flags']) for m in info['movies']) + '};']
    lines += ['/* 0de0e0 caption opcode 13, in authored script order. */',
              'static const struct { int movie,segment,tick,voice; } MOVIE_AUDIO_CUES[]={']
    for m in info['movies']:
        for cue in m['audio_cues']:
            lines.append('{%d,%d,%d,%d},' % (m['movie'],cue['segment'],cue['tick'],cue['voice']))
    lines += ['};']
    return '\n'.join(lines + [''])


def bake(movie, path):
    args = [sys.executable, str(DEC / 'tools/jgc_convert.py'), str(path), '--motions=0',
        '--source-material=1', '--source-environment=1',
        '--texmax=' + str(movie['texture_max']),
        '--texture-offsets=' + ','.join(map(str, movie['texture_offsets']))]
    if movie.get('texture_max_overrides'):
        args.append('--texmax-overrides=' + ','.join(f'{i}:{cap}' for i, cap in movie['texture_max_overrides'].items()))
    for segment in movie['segments']:
        for r in segment['actors']:
            spec = (f"Movie_{r['address']:x}:{movie['source']}:{movie['textures']}:0,0,0,0:"
                    f"rawat:{MOVIE_BASE:x}:{r['root']:x}:0")
            if r['texture_map']:
                spec += ':texmap=' + ','.join(map(str, r['texture_map']))
            spec += ':lighting=' + ('2' if r['flags'] & 2 else '1')
            args.append(spec)
    subprocess.run(args, cwd=DEC, check=True, stdout=subprocess.DEVNULL)


def verify_mesh(movie, path):
    data = path.read_bytes()
    assert data[:4] == b'JGC3', path
    h = struct.unpack_from('<16I', data, 4)
    actors = [a for s in movie['segments'] for a in s['actors']]
    assert h[0] == len(actors) and h[6] == 0, path
    assert all(0 < struct.unpack_from('<I', data, h[7] + 48 * i + 4)[0] <= 512
               for i in range(len(actors))), path


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check', action='store_true')
    ap.add_argument('--bake', action='store_true')
    ap.add_argument('--movie', type=int, help='Limit mesh baking only; always import/catalog all movies')
    args = ap.parse_args()
    info = catalog()
    outputs = {PSP / 'movies.json': (json.dumps(info, indent=2) + '\n').encode(),
               PSP / 'src/movie_tables.inc': tables(info).encode()}
    for m in info['movies']:
        outputs[PSP / f'movie{m["movie"]:02d}.bin'] = (DEC / m['source']).read_bytes()
    for v in info['voices']:
        outputs[PSP / f'movievoice{v["reference"]:03d}.adx'] = (DEC / v['source']).read_bytes()
    for path, data in outputs.items():
        if args.check:
            assert path.read_bytes() == data, path
        else:
            path.write_bytes(data)
    for m in info['movies']:
        if args.movie is not None and m['movie'] != args.movie:
            continue
        path = PSP / f'movie{m["movie"]:02d}.jgc'
        if args.bake:
            if args.check:
                with tempfile.TemporaryDirectory(prefix='jgr-movie-import-') as tmp:
                    rebuilt = Path(tmp) / path.name
                    bake(m, rebuilt)
                    assert rebuilt.read_bytes() == path.read_bytes(), path
            else:
                bake(m, path)
        if args.check or args.bake:
            verify_mesh(m, path)
            print(f'{path.name}: {len(m["segments"])} source segments, verified import', flush=True)
    print(f'35 movies, {sum(len(m["segments"]) for m in info["movies"])} segments, '
          f'{len(info["voices"])} byte-exact bank-31 ADX files. Campaign playback is separate.')


if __name__ == '__main__':
    main()
