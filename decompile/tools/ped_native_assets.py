#!/usr/bin/env python3
"""Export versioned native pedestrian strips with exact resident-JGC attributes.

JPN1 is a bounded load-time sidecar, not another animation or texture bank.
Native cache definitions keep distinct identities across slot overwrites;
authored strips retain their signed parity and reference order. Existing JGC
records provide the already audited baked colours and floating UV values.
Every native triangle is matched to its existing JGC triangle before output.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
from dataclasses import dataclass
import hashlib
import json
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
HEADER = struct.Struct('<4s7I')
MODEL = struct.Struct('<6H5I')
PAIR = struct.Struct('<2H')
GROUP = struct.Struct('<8H')
VERTEX_STRIDES = {0x22: 12, 0x29: 24}
STRIP_EXTRA = {64: 0, 65: 2, 66: 2, 67: 3, 68: 5, 69: 5,
               70: 2, 71: 4, 72: 4, 73: 1, 74: 3, 75: 3}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def unpack(fmt, data, offset):
    size = struct.calcsize(fmt)
    require(0 <= offset <= len(data) - size, f'out-of-range {fmt} at {offset:#x}')
    return struct.unpack_from(fmt, data, offset)


def fnv1a(chunks):
    value = 2166136261
    for chunk in chunks:
        for byte in chunk:
            value = ((value ^ byte) * 16777619) & 0xffffffff
    return value


class Jgc:
    def __init__(self, data):
        self.data = bytes(data)
        require(data[:4] in (b'JGC2', b'JGC3'), 'native sidecars require owner-local JGC2/3')
        self.h = unpack('<16I', data, 4)
        self.stride = 40 if data[:4] == b'JGC3' else 28
        require(list(self.h[7:]) == sorted(self.h[7:]) and self.h[7] >= 68
                and self.h[15] <= len(data), 'invalid JGC section order')
        require(self.h[3] <= 65535, 'JPN1 source vertex IDs exceed u16')
        sizes = (48, 48, 16, self.stride, 2, 16, 8)
        for i, size in enumerate(sizes):
            require(self.h[i] * size <= self.h[8+i] - self.h[7+i], 'overlapping JGC sections')

    def char(self, ci):
        require(0 <= ci < self.h[0], 'invalid character')
        return unpack('<4I4f16s', self.data, self.h[7] + ci*48)

    def bone(self, bi):
        require(0 <= bi < self.h[1], 'invalid bone')
        return unpack('<hH3f3i3f2I', self.data, self.h[8] + bi*48)

    def batch(self, bi):
        require(0 <= bi < self.h[2], 'invalid batch')
        result = unpack('<II4H', self.data, self.h[9] + bi*16)
        vo, io, nv, ni, tex, flags = result
        require(vo+nv <= self.h[3] and io+ni <= self.h[4] and ni % 3 == 0, 'invalid batch span')
        require(flags & 4 or tex < self.h[5], 'invalid batch texture')
        return result

    def vertex(self, vi):
        require(0 <= vi < self.h[3], 'invalid vertex')
        at = self.h[10] + vi*self.stride
        return self.data[at:at+self.stride]

    def batch_indices(self, bi):
        vo, io, nv, ni, _, _ = self.batch(bi)
        indices = unpack(f'<{ni}H', self.data, self.h[11] + io*2)
        require(all(i < nv for i in indices), 'batch index out of bounds')
        return indices

    def geometry_hash(self):
        chunks = []
        for bi in range(self.h[2]):
            vo, io, nv, ni, _, _ = self.batch(bi)
            chunks += [self.data[self.h[9]+bi*16:self.h[9]+(bi+1)*16],
                       self.data[self.h[10]+vo*self.stride:self.h[10]+(vo+nv)*self.stride],
                       self.data[self.h[11]+io*2:self.h[11]+(io+ni)*2]]
        return fnv1a(chunks)


@dataclass
class Position:
    owner: int
    slot: int
    generation: int
    address: int
    xyz: bytes
    source_vertex: int | None = None


@dataclass
class Corner:
    position: int
    uv: bytes
    raw_uv: tuple[int, int]
    source_vertex: int | None = None


@dataclass
class Strip:
    owner: int
    address: int
    signed_count: int
    kind: int
    source_texture: int
    flags: int
    diffuse: int
    raw_flags: int
    corners: list[Corner]
    raw: bytes
    runtime_texture: int = 0
    runtime_flags: int = 0

    def triangles(self):
        flip = self.signed_count < 0
        for k in range(len(self.corners)-2):
            a, b = k, k+1
            if bool(k & 1) ^ flip:
                a, b = b, a
            yield (self.corners[a], self.corners[b], self.corners[k+2])


@dataclass
class NativeModel:
    name: str
    nodes: list[tuple[int, int, bytes]]
    positions: list[Position]
    strips: list[Strip]
    source_path: Path
    source_sha256: str
    texture_path: Path
    texture_count: int


def read_native(spec, spec_dir):
    parts = spec.split(':')
    require(len(parts) >= 8 and parts[4] == 'rawat', 'people source must use an explicit rawat root')
    name, path = parts[0], spec_dir / parts[1]
    data = path.read_bytes()
    base, root = int(parts[5], 16), int(parts[6], 16)
    require(tuple(map(float, parts[3].split(','))) == (0, 0, 0, 0), 'unexpected baked placement')
    tex_override = next((int(p[4:]) for p in parts[8:] if p.startswith('tex=')), None)
    texture_path = spec_dir / parts[2]
    txr = texture_path.read_bytes()
    ntextures = txr.count(b'GBIX')
    require(ntextures > 0, 'missing source texture bank')
    require(all(p.startswith(('tex=', 'mot=')) for p in parts[8:]), 'unsupported people export option')
    nodes, positions, strips = [], [], []
    slots, generations, visited = {}, defaultdict(int), set()
    stack = [(root, -1)]
    while stack:
        address, parent = stack.pop()
        require(address not in visited, 'cycle or reused node in native hierarchy')
        visited.add(address)
        ev, model = unpack('<2I', data, address-base)
        raw_transform = data[address-base+8:address-base+44]
        require(len(raw_transform) == 36, 'truncated native transform')
        child, sibling = unpack('<2I', data, address-base+44)
        owner = len(nodes)
        nodes.append((parent, ev, raw_transform))
        if sibling:
            stack.append((sibling, parent))
        if child:
            stack.append((child, owner))
        if not model:
            continue
        vl, pl = unpack('<2I', data, model-base)
        require(bool(vl) == bool(pl), f'{name}: partial native model')
        if not vl:
            continue
        at = vl-base
        while True:
            kind, _ = unpack('<2B', data, at)
            if kind == 0xff:
                break
            if kind == 0:
                at += 4
                continue
            require(kind in VERTEX_STRIDES, f'{name}: unsupported vertex chunk {kind:#x}')
            size, first, count = unpack('<3H', data, at+2)
            end, stride = at+4+size*4, VERTEX_STRIDES[kind]
            require(count > 0 and at+8+count*stride <= end <= len(data), 'invalid native vertex chunk')
            for i in range(count):
                vertex_at = at+8+i*stride
                xyz = unpack('<3f', data, vertex_at)
                require(all(map(math.isfinite, xyz)), 'non-finite native position')
                # Existing JGC exporter applies its identity matrix before
                # packing. Retain that exact float representation (including
                # its treatment of zero) while preserving native slot/version.
                x, y, z = xyz
                packed = struct.pack('<3f', 1*x+0*y+0*z+0, 0*x+1*y+0*z+0, 0*x+0*y+1*z+0)
                slot = first+i
                positions.append(Position(owner, slot, generations[slot], base+vertex_at, packed))
                generations[slot] += 1
                slots[slot] = len(positions)-1
            at = end
        at, texture, diffuse = pl-base, 0, 0xffffffff
        while True:
            kind, flags = unpack('<2B', data, at)
            if kind == 0xff:
                break
            if kind <= 5:
                at += 2
                continue
            if kind in (8, 9):
                texture = unpack('<H', data, at+2)[0] & 0x1fff
                at += 4
                continue
            if 16 <= kind <= 31 or 56 <= kind <= 58:
                size = unpack('<H', data, at+2)[0]
                if 16 <= kind <= 31 and kind & 1:
                    diffuse = unpack('<I', data, at+4)[0]
                require(at+4+size*2 <= len(data), 'truncated material chunk')
                at += 4+size*2
                continue
            require(kind in STRIP_EXTRA, f'{name}: unsupported polygon chunk {kind:#x}')
            size, count = unpack('<2H', data, at+2)
            end, extra, aux = at+4+size*2, STRIP_EXTRA[kind], count >> 14
            require(end <= len(data), 'truncated strip chunk')
            textured = kind in (65, 66, 68, 69, 71, 72)
            divisor = 255.0 if kind in (65, 68, 71, 73) else 1023.0 if kind in (66, 69, 72, 74) else 1.0
            source_texture = (tex_override if tex_override is not None else min(texture, ntextures-1)) if textured else 0xffff
            mapped_flags = (1 if flags & 8 else 0) | (2 if flags & 16 else 0) | (0 if textured else 4) | (8 if flags & 1 else 0)
            cur = at+6
            for _ in range(count & 0x3fff):
                start = cur
                signed_count = unpack('<h', data, cur)[0]
                cur += 2
                require(3 <= abs(signed_count) <= 32767, 'unsupported short or oversized native strip')
                corners = []
                for _ in range(abs(signed_count)):
                    slot = unpack('<H', data, cur)[0]
                    require(slot in slots, f'{name}: undefined native cache slot {slot}')
                    uv = unpack('<2h', data, cur+2) if textured else (0, 0)
                    corners.append(Corner(slots[slot], struct.pack('<2f', uv[0]/divisor, uv[1]/divisor), uv))
                    cur += 2+extra*2
                cur += (abs(signed_count)-2)*aux*2
                require(cur <= end, 'strip overran chunk')
                strips.append(Strip(owner, base+start, signed_count, kind, source_texture,
                                    mapped_flags, diffuse, flags, corners, data[start:cur]))
            require(cur == end or (end-cur == 2 and unpack('<H', data, cur)[0] == 0),
                    f'{name}: unexplained strip payload {end-cur} bytes at {base+at:08x}')
            at = end
    return NativeModel(name, nodes, positions, strips, path, hashlib.sha256(data).hexdigest(), texture_path, ntextures)


def join_authored_strips(strips):
    """Join (signed count, indices) without altering any authored strip."""
    result = []
    for signed_count, indices in strips:
        require(len(indices) == abs(signed_count) and len(indices) >= 3, 'invalid authored strip')
        flip = signed_count < 0
        if not result:
            if flip:
                result.append(indices[0])
        else:
            parity = len(result) & 1
            result.extend((result[-1], indices[0]))
            if bool(parity) != flip:
                result.append(indices[0])
        result.extend(indices)
    return result


def expand_strip(indices):
    for i in range(len(indices)-2):
        a, b, c = indices[i], indices[i+1], indices[i+2]
        if i & 1:
            a, b = b, a
        if a != b and a != c and b != c:
            yield a, b, c


def match_model(bank, ci, native, texture_map):
    char = bank.char(ci)
    bone0, nbones = char[:2]
    require(char[8].split(b'\0', 1)[0].decode() == native.name[:15], 'model order/name mismatch')
    require(nbones == len(native.nodes), f'{native.name}: skeleton size changed')
    ordered = []
    source_vertices = 0
    for owner, node in enumerate(native.nodes):
        bone = bank.bone(bone0+owner)
        require((bone[0]-bone0 if bone[0] >= 0 else -1) == node[0] and bone[1] == node[1], 'bone hierarchy changed')
        require(struct.pack('<3f3i3f', *bone[2:11]) == node[2], 'bind transform changed')
        groups = defaultdict(list)
        for strip in native.strips:
            if strip.owner == owner:
                groups[(strip.source_texture, strip.flags)].append(strip)
        batch0, nbatch = bone[-2:]
        require(len(groups) == nbatch, f'{native.name} bone {owner}: material groups differ')
        for bi, ((source_texture, flags), strips) in zip(range(batch0, batch0+nbatch), sorted(groups.items())):
            vo, _, nv, ni, tex, runtime_flags = bank.batch(bi)
            expected_tex = 0 if source_texture == 0xffff else texture_map[source_texture]
            expected_flags = flags
            if source_texture != 0xffff:
                texture_flags = unpack('<H', bank.data, bank.h[12]+expected_tex*16+4)[0]
                if texture_flags & 1:
                    expected_flags |= 1
            require((tex, runtime_flags) == (expected_tex, expected_flags), f'{native.name}: material state mismatch')
            native_triangles = [tri for strip in strips for tri in strip.triangles()]
            indices = bank.batch_indices(bi)
            require(len(native_triangles)*3 == ni, f'{native.name}: triangle count mismatch')
            for k, triangle in enumerate(native_triangles):
                for j, corner in enumerate(triangle):
                    source_vertex = vo+indices[k*3+j]
                    record = bank.vertex(source_vertex)
                    position = native.positions[corner.position]
                    require(record[:8] == corner.uv, f'{native.name}: native UV mismatch')
                    require(record[12:24] == position.xyz and unpack('<H', record, 24)[0] == position.owner,
                            f'{native.name}: native owner/position mismatch')
                    if corner.source_vertex is not None:
                        require(bank.vertex(corner.source_vertex)[:28] == record[:28], 'one native corner has inconsistent JGC attributes')
                    corner.source_vertex = source_vertex
                    if position.source_vertex is None:
                        position.source_vertex = source_vertex
            for strip in strips:
                require(all(c.source_vertex is not None for c in strip.corners), 'unreferenced strip endpoint')
                strip.runtime_texture, strip.runtime_flags = tex, runtime_flags
            ordered.extend(strips)
            source_vertices += nv
    # Existing renderer emits every outline before every body, maintaining
    # hierarchy/material order within each pass. Native strips themselves
    # are not triangulated/reordered to achieve that contract.
    ordered = [s for s in ordered if s.runtime_flags & 8] + [s for s in ordered if not s.runtime_flags & 8]
    position_order = sorted((i for i, p in enumerate(native.positions) if p.source_vertex is not None),
                            key=lambda i: (native.positions[i].owner, i))
    position_map = {native_id: i for i, native_id in enumerate(position_order)}
    positions = [(native.positions[i].source_vertex, native.positions[i].owner) for i in position_order]
    emissions, groups, indices = [], [], []
    expected_stream = []
    start = 0
    while start < len(ordered):
        end = start+1
        key = ordered[start].runtime_texture, ordered[start].runtime_flags
        while end < len(ordered) and (ordered[end].runtime_texture, ordered[end].runtime_flags) == key:
            end += 1
        first_vertex, first_index = len(emissions), len(indices)
        vertex_map, authored, triangle_count = {}, [], 0
        for strip in ordered[start:end]:
            strip_indices = []
            for corner in strip.corners:
                pi = position_map[corner.position]
                source = corner.source_vertex
                attribute = bank.vertex(source)[:12]
                vkey = pi, attribute
                if vkey not in vertex_map:
                    vertex_map[vkey] = len(emissions)-first_vertex
                    emissions.append((pi, source))
                strip_indices.append(vertex_map[vkey])
            authored.append((strip.signed_count, strip_indices))
            for tri in strip.triangles():
                expected_stream.append((key, tuple(bank.vertex(c.source_vertex)[:28] for c in tri)))
                triangle_count += 1
        joined = join_authored_strips(authored)
        require(len(list(expand_strip(joined))) == triangle_count, 'authored degenerate triangle requires explicit fallback')
        indices.extend(joined)
        groups.append((first_vertex, len(vertex_map), first_index, len(joined), key[0], key[1], triangle_count, 1))
        start = end
    require(positions and groups, f'{native.name}: empty native model')
    require(len(positions) <= 256 and len(emissions) <= 512 and len(groups) <= 128 and len(indices) <= 2048, 'model exceeds runtime bounds')
    model = dict(positions=positions, emissions=emissions, groups=groups, indices=indices,
                 source_vertices=source_vertices, nbones=nbones)
    actual = decode_model(bank, model)
    require(actual == expected_stream, f'{native.name}: native strip joining changed ordered triangles')
    provenance = dict(name=native.name, source=str(native.source_path.relative_to(ROOT)), source_sha256=native.source_sha256,
                      source_textures=str(native.texture_path.relative_to(ROOT)),
                      native_positions=len(native.positions), retained_positions=len(positions), source_vertices=source_vertices,
                      owner_runs=sum(i == 0 or positions[i-1][1] != p[1] for i, p in enumerate(positions)),
                      emissions=len(emissions), groups=len(groups), indices=len(indices), triangles=len(actual),
                      positions=[dict(native_id=i, owner=p.owner, slot=p.slot, generation=p.generation,
                                      address=f'{p.address:08x}', source_vertex=p.source_vertex,
                                      runtime_position=position_map.get(i)) for i, p in enumerate(native.positions)],
                      strips=[dict(address=f'{s.address:08x}', owner=s.owner, signed_count=s.signed_count,
                                   chunk_type=s.kind, raw_flags=s.raw_flags, diffuse=f'{s.diffuse:08x}',
                                   source_texture=s.source_texture, texture=s.runtime_texture, flags=s.runtime_flags,
                                   native_position_ids=[c.position for c in s.corners], raw_uv=[c.raw_uv for c in s.corners],
                                   source_vertices=[c.source_vertex for c in s.corners], raw_hex=s.raw.hex()) for s in native.strips])
    return model, provenance


def decode_model(bank, model):
    result = []
    for fv, nv, fi, ni, tex, flags, triangles, strip in model['groups']:
        ix = model['indices'][fi:fi+ni]
        require(all(i < nv for i in ix), 'group index out of bounds')
        expanded = list(expand_strip(ix)) if strip else [ix[k:k+3] for k in range(0, ni, 3)]
        require(len(expanded) == triangles, 'group triangle count mismatch')
        for tri in expanded:
            records = []
            for vi in tri:
                pi, source = model['emissions'][fv+vi]
                source_position, owner = model['positions'][pi]
                record, position = bank.vertex(source), bank.vertex(source_position)
                require(record[12:24] == position[12:24] and unpack('<H', record, 24)[0] == owner, 'emission position differs')
                records.append(record[:28])
            result.append(((tex, flags), tuple(records)))
    return result


def serialize(bank, models):
    output = bytearray(HEADER.size+len(models)*MODEL.size)
    def add(fmt, records):
        output.extend(bytes(-len(output) % 4))
        offset = len(output)
        for record in records:
            output.extend(fmt.pack(*record))
        return offset
    for ci, model in enumerate(models):
        op = add(PAIR, model['positions'])
        ov = add(PAIR, model['emissions'])
        og = add(GROUP, model['groups'])
        oi = add(struct.Struct('<H'), [(i,) for i in model['indices']])
        MODEL.pack_into(output, HEADER.size+ci*MODEL.size,
                        len(model['positions']), len(model['emissions']), len(model['groups']), len(model['indices']),
                        model['source_vertices'], model['nbones'], op, ov, og, oi, 0)
    output.extend(bytes(-len(output) % 4))
    HEADER.pack_into(output, 0, b'JPN1', 1, len(output), bank.geometry_hash(), len(models), HEADER.size, 0, 0)
    return bytes(output)


def decode_sidecar(bank, data):
    magic, version, size, geometry, count, offset, zero, zero2 = unpack(HEADER.format, data, 0)
    require(magic == b'JPN1' and version == 1 and size == len(data) and not zero and not zero2, 'invalid sidecar header')
    require(count == bank.h[0] and geometry == bank.geometry_hash(), 'sidecar geometry mismatch')
    require(offset == HEADER.size and offset+count*MODEL.size <= len(data), 'invalid model directory')
    models = []
    previous_end = offset+count*MODEL.size
    for ci in range(count):
        np, nv, ng, ni, source, bones, op, ov, og, oi, reserved = unpack(MODEL.format, data, offset+ci*MODEL.size)
        require(np and nv and ng and np <= 256 and nv <= 512 and ng <= 128 and ni <= 2048 and not reserved, 'invalid model bounds')
        require(bones == bank.char(ci)[1], 'sidecar skeleton mismatch')
        require(op >= previous_end and all(o % 4 == 0 and o >= offset+count*MODEL.size for o in (op, ov, og, oi)), 'invalid section offset')
        require(op+np*4 <= ov and ov+nv*4 <= og and og+ng*16 <= oi and oi+ni*2 <= len(data), 'overlapping model sections')
        previous_end = (oi+ni*2+3) & ~3
        model = dict(positions=[unpack(PAIR.format, data, op+i*4) for i in range(np)],
                     emissions=[unpack(PAIR.format, data, ov+i*4) for i in range(nv)],
                     groups=[unpack(GROUP.format, data, og+i*16) for i in range(ng)],
                     indices=list(unpack(f'<{ni}H', data, oi)), source_vertices=source, nbones=bones)
        require([p[1] for p in model['positions']] == sorted(p[1] for p in model['positions']), 'owner runs are not contiguous')
        require(all(owner < bones for _, owner in model['positions']), 'invalid owner')
        require(all(pi < np for pi, _ in model['emissions']), 'invalid position reference')
        allowed_vertices, expected_source = set(), 0
        bone0 = bank.char(ci)[0]
        for bi in range(bone0, bone0+bones):
            first, number = bank.bone(bi)[-2:]
            for at in range(first, first+number):
                vo, _, nv, _, _, _ = bank.batch(at)
                allowed_vertices.update(range(vo, vo+nv))
                expected_source += nv
        require(source == expected_source, 'source vertex count changed')
        require(all(vi in allowed_vertices and unpack('<H', bank.vertex(vi), 24)[0] == owner
                    for vi, owner in model['positions']), 'position source is not owned by this model')
        require(all(vi in allowed_vertices for _, vi in model['emissions']), 'emission source is not owned by this model')
        for fv, nvert, fi, nidx, tex, flags, _, primitive in model['groups']:
            require(nvert and nidx >= 3 and fv+nvert <= len(model['emissions']) and fi+nidx <= ni
                    and primitive in (0, 1) and (primitive or nidx % 3 == 0), 'invalid group span')
            require(flags & 4 or tex < bank.h[5], 'invalid group texture')
        decode_model(bank, model)
        models.append(model)
    return models


def build(city):
    spec_path = ROOT / f'decompile/people{city}_spec.txt'
    bank_path = ROOT / f'psp/people{city}_compact.jgc'
    bank = Jgc(bank_path.read_bytes())
    specs = [line.strip() for line in spec_path.read_text().splitlines() if line.strip() and not line.lstrip().startswith('#')]
    require(len(specs) == bank.h[0], 'spec count differs from resident bank')
    natives = [read_native(spec, spec_path.parent) for spec in specs]
    texture_banks, next_texture = {}, 0
    for native in natives:
        key = native.texture_path.resolve()
        if key not in texture_banks:
            texture_banks[key] = next_texture
            next_texture += native.texture_count
        for strip in native.strips:
            if strip.source_texture != 0xffff:
                strip.source_texture += texture_banks[key]
    used = sorted({s.source_texture for n in natives for s in n.strips if s.source_texture != 0xffff})
    require(len(used) == bank.h[5], 'source texture selection differs from resident bank')
    texture_map = {value: i for i, value in enumerate(used)}
    pairs = [match_model(bank, ci, native, texture_map) for ci, native in enumerate(natives)]
    models, provenance = [p[0] for p in pairs], [p[1] for p in pairs]
    data = serialize(bank, models)
    decoded = decode_sidecar(bank, data)
    require(decoded == models, 'sidecar serialization changed content')
    require(len(data) <= 56*1024, 'bank exceeds bounded runtime sidecar pool')
    report = dict(format='JPN1', version=1, city=city, bytes=len(data), sha256=hashlib.sha256(data).hexdigest(),
                  bank=str(bank_path.relative_to(ROOT)), bank_sha256=hashlib.sha256(bank.data).hexdigest(),
                  geometry_hash=f'{bank.geometry_hash():08x}', models=len(models),
                  positions=sum(len(m['positions']) for m in models), emissions=sum(len(m['emissions']) for m in models),
                  groups=sum(len(m['groups']) for m in models), indices=sum(len(m['indices']) for m in models),
                  triangles=sum(sum(g[6] for g in m['groups']) for m in models),
                  maximum_model={key:max(len(m[key]) for m in models) for key in ('positions', 'emissions', 'groups', 'indices')},
                  contract='Exact ordered existing-JGC triangles/UV/colour/owner/XYZ/material state; native versioned slots and signed strips preserved',
                  provenance=provenance)
    return data, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--all', action='store_true')
    mode.add_argument('--city', type=int, choices=(1, 2, 3, 5))
    parser.add_argument('--out', type=Path)
    parser.add_argument('--report', type=Path)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    require(not args.all or not args.out, '--out requires --city')
    results = []
    # Validate every requested bank before changing any output asset.
    for city in (1, 2, 3, 5) if args.all else (args.city,):
        data, report = build(city)
        output = args.out or ROOT / f'psp/people{city}_native.jpn'
        results.append((output, data, report))
    for output, data, report in results:
        if args.check:
            require(output.read_bytes() == data, f'{output}: generated bytes differ')
        else:
            output.parent.mkdir(parents=True, exist_ok=True)
            output.write_bytes(data)
        print(f'people{report["city"]}: {len(data)} bytes, {report["models"]} models, '
              f'{report["positions"]} native positions, {report["indices"]} strip references, '
              f'{report["triangles"]} exact ordered triangles')
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps([r[2] for r in results] if args.all else results[0][2], indent=2)+'\n')


if __name__ == '__main__':
    main()
