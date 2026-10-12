#!/usr/bin/env python3
"""Restore 07f9cc's car/beacon NJS_TEXLIST without rebaking shared geometry.

Only PatCar batch material references/blend flags and missing texture payloads
change. Existing colours, vertex owners, indices, skeletons and motions stay
byte-identical. This also preserves the PLT1 geometry fingerprint.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
from character_roster import header, model_bank
from jgc_compact import compact
from texture_binding import resolve_texture_binding

ROOT = Path(__file__).resolve().parents[2]


def texture(data, index):
    h = header(data)
    w, ht, flags, pad, clut, pixels = struct.unpack_from('<4H2I', data, h[12]+16*index)
    return w, ht, flags, pad, data[clut:clut+1024], data[pixels:pixels+w*ht]


def append_texture(data, tex):
    h = list(header(data)); old = bytes(data)
    for i in range(h[5]):
        if texture(old, i) == tex:
            return old, i
    # Grow by a cache line so every original aligned section stays aligned.
    at = h[13]; out = bytearray(old[:at])+bytearray(64)+bytearray(old[at:])
    for i in range(h[5]):
        for field in (8, 12):
            p = h[12]+16*i+field
            struct.pack_into('<I', out, p, struct.unpack_from('<I', old, p)[0]+64)
    for i in range(h[6]):
        p = h[13]+i*8+4
        struct.pack_into('<I', out, p+64, struct.unpack_from('<I', old, p)[0]+64)
    for i in (13, 14, 15):
        h[i] += 64
    out.extend(bytes(-len(out) % 64)); clut = len(out); out.extend(tex[4])
    pixels = len(out); out.extend(tex[5])
    index = h[5]; h[5] += 1
    struct.pack_into('<4H2I', out, h[12]+16*index, *tex[:4], clut, pixels)
    struct.pack_into('<16I', out, 4, *h)
    return bytes(out), index


def repair(original, reference):
    old_car, _ = model_bank(original, 26)
    a, b = header(old_car), header(reference)
    assert a[:4] == b[:4] and a[6] == b[6]
    assert old_car[a[8]:a[8]+a[1]*48] == reference[b[8]:b[8]+b[1]*48]
    for i in range(a[3]):
        x = old_car[a[10]+i*28:a[10]+(i+1)*28]
        y = reference[b[10]+i*28:b[10]+(i+1)*28]
        assert x[:8]+x[12:] == y[:8]+y[12:], ('source geometry changed', i)
    mapping = {}
    result = original
    for i in range(b[5]):
        result, mapping[i] = append_texture(result, texture(reference, i))
    out = bytearray(result); h = header(out)
    bone0, bones = struct.unpack_from('<2I', out, h[7]+26*48)
    count = 0
    for bone in range(bone0, bone0+bones):
        first, n = struct.unpack_from('<2I', out, h[8]+bone*48+40)
        for batch in range(first, first+n):
            old = struct.unpack_from('<2I4H', out, h[9]+batch*16)
            new = struct.unpack_from('<2I4H', reference, b[9]+count*16)
            assert old[2:4] == new[2:4]
            assert out[h[11]+old[1]*2:h[11]+(old[1]+old[3])*2] == reference[b[11]+new[1]*2:b[11]+(new[1]+new[3])*2]
            # Untextured outline batches deliberately retain their old slot.
            if not old[5] & 4:
                struct.pack_into('<2H', out, h[9]+batch*16+12, mapping[new[4]], new[5])
            count += 1
    assert count == b[2]
    # Verify all original vertex/index/key payloads survived the insertion.
    oh = header(original)
    for section, size in ((10, oh[3]*28), (11, oh[4]*2), (14, oh[15]-oh[14])):
        assert out[h[section]:h[section]+size] == original[oh[section]:oh[section]+size]
    return bytes(out)


def build(check=False):
    exe = (ROOT/'decompile/extracted/1ST_READ.BIN').read_bytes()
    word = lambda a: struct.unpack_from('<I', exe, a-0x8c010000)[0]
    assert word(0x8c07facc) == 0x8c1b6200
    assert word(0x8c1b6200) == 0x8c1b8d20
    assert word(0x8c1b8d24) == 2
    spec = (ROOT/'decompile/chars_spec.txt').read_text().splitlines()[26]
    assert spec.endswith(':texbinding=1,8c010000,8c1b8d28,2,8ca00000,3:material=1')
    fields = spec.split(':')
    source_slots = resolve_texture_binding(next(part[11:] for part in fields if part.startswith('texbinding=')),
                                          (ROOT/'decompile'/fields[2]).read_bytes(), ROOT/'decompile/extracted/1ST_READ.BIN')
    with tempfile.TemporaryDirectory(prefix='jgr-patcar-') as directory:
        reference = Path(directory)/'car.jgc'
        subprocess.run([sys.executable, 'tools/jgc_convert.py', str(reference), '--toon=1', spec],
                       cwd=ROOT/'decompile', check=True, stdout=subprocess.PIPE)
        data = repair((ROOT/'psp/chars.jgc').read_bytes(), reference.read_bytes())
    assets = {'chars.jgc': data, 'chars_compact.jgc': compact(data)}
    for name, blob in assets.items():
        path = ROOT/'psp'/name
        if check:
            assert path.read_bytes() == blob, name
        else:
            path.write_bytes(blob)
    info = dict(source_initializer='8c07f9cc', texture_list='8c1b8d20',
                source_materials=True, beacon_blend='source alpha plus destination one',
                source_slots=source_slots, model=26, geometry_and_motion_preserved=True,
                assets={k: dict(bytes=len(v), sha256=hashlib.sha256(v).hexdigest()) for k, v in assets.items()})
    if not check:
        (ROOT/'psp/patrol_car_materials.json').write_text(json.dumps(info, indent=2)+'\n')
        path = ROOT/'psp/playerlight.json'; light = json.loads(path.read_text())
        light['shared_bank_sha256'] = hashlib.sha256(data).hexdigest()
        path.write_text(json.dumps(light, indent=2)+'\n')
    print(json.dumps(info, indent=2))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    build(parser.parse_args().check)
