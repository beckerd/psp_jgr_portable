"""Optional JCL1 model directory inside JGC's reserved header space.

Public character IDs stay unchanged. Private far models share the original
texture images and motion/channel/key ranges; only their geometry is added.
Directory words at 68 are {magic, source_count, offset}; zero means no LOD.
The directory precedes off_chars and contains one far model ID per source.
"""
import struct


def header(data):
    return struct.unpack_from('<16I', data, 4)


def lod_models(data):
    h = header(data)
    if h[7] < 80 or data[68:72] != b'JCL1':
        return {}
    count, at = struct.unpack_from('<2I', data, 72)
    if not 0 < count <= h[0] or at < 80 or at % 4 or at + 4 * count > h[7]:
        raise ValueError('JCL1 directory span')
    result = {}
    for ci, far in enumerate(struct.unpack_from('<%dI' % count, data, at)):
        if far:
            if far == ci or far >= h[0]:
                raise ValueError('JCL1 model index')
            near = struct.unpack_from('<4I', data, h[7] + ci * 48)
            low = struct.unpack_from('<4I', data, h[7] + far * 48)
            if near[1:] != low[1:]:
                raise ValueError('JCL1 shared pose')
            result[ci] = far
    if any(far in result for far in result.values()):
        raise ValueError('JCL1 chained model')
    return result


def texture(data, index):
    h = header(data)
    w, ht, flags, pad, clut, pix = struct.unpack_from('<4H2I', data, h[12] + index * 16)
    return (w, ht, flags, pad, data[clut:clut + 1024], data[pix:pix + w * ht])


def pack_models(refs, directory=None, seed_textures=None):
    """refs=(data, model, motion_data, motion_model), all payloads lossless.

    Returns image and (input image, original vertex) for each output vertex.
    This map also retains the independent PLT1 lighting companion.
    """
    directory = directory or {}
    bones = bytearray(); batches = bytearray(); verts = bytearray(); indices = bytearray()
    characters = []; textures = []; texture_ids = {}; sources = []; batch_sources = []
    motions = []; channels = bytearray(); keys = bytearray(); motion_ids = {}; key_ids = {}; channel_ids = {}
    def align(blob, n=16):
        blob.extend(bytes(-len(blob) % n)); return len(blob)
    def add_texture(record):
        if record not in texture_ids:
            texture_ids[record] = len(textures); textures.append(record)
        return texture_ids[record]
    if seed_textures is not None:
        for ti in range(header(seed_textures)[5]):
            add_texture(texture(seed_textures, ti))
    for data, ci, motion_data, motion_ci in refs:
        h = header(data); mh = header(motion_data)
        assert data[:4] == motion_data[:4] == b'JGC2'
        ch = bytearray(data[h[7] + ci * 48:h[7] + (ci + 1) * 48])
        b0, bn, _, _ = struct.unpack_from('<4I', ch)
        _, mbn, m0, mn = struct.unpack_from('<4I', motion_data, mh[7] + motion_ci * 48)
        assert bn == mbn
        nb = len(bones) // 48
        for bi in range(bn):
            bone = bytearray(data[h[8] + (b0 + bi) * 48:h[8] + (b0 + bi + 1) * 48])
            parent = struct.unpack_from('<h', bone)[0]
            assert parent < 0 or b0 <= parent < b0 + bi
            struct.pack_into('<h', bone, 0, -1 if parent < 0 else nb + parent - b0)
            first, count = struct.unpack_from('<2I', bone, 40)
            struct.pack_into('<I', bone, 40, len(batches) // 16)
            for j in range(first, first + count):
                v, ix, nv, ni, tex, flags = struct.unpack_from('<2I4H', data, h[9] + j * 16)
                batches.extend(struct.pack('<2I4H', len(verts) // 28, len(indices) // 2,
                                           nv, ni, add_texture(texture(data, tex)), flags))
                verts.extend(data[h[10] + v * 28:h[10] + (v + nv) * 28])
                indices.extend(data[h[11] + ix * 2:h[11] + (ix + ni) * 2])
                sources.extend((data, v + k) for k in range(nv)); batch_sources.append((data, v))
            bones.extend(bone)
        mk = (id(motion_data), m0, mn, bn)
        if mk not in motion_ids:
            motion_ids[mk] = len(motions)
            for mi in range(m0, m0 + mn):
                frames, at = struct.unpack_from('<2I', motion_data, mh[13] + mi * 8)
                rows = bytearray()
                for bi in range(bn):
                    row = []
                    for kind, (count, off) in enumerate(struct.iter_unpack('<2I', motion_data[at + bi * 24:at + (bi + 1) * 24])):
                        payload = motion_data[mh[14] + off:mh[14] + off + count * (6 if kind == 1 else 12)]
                        key = (2 if kind == 1 else 4, payload)
                        if key not in key_ids:
                            key_ids[key] = align(keys, key[0]); keys.extend(payload)
                        row.extend((count, key_ids[key]))
                    rows.extend(struct.pack('<6I', *row))
                payload = bytes(rows)
                if payload not in channel_ids:
                    channel_ids[payload] = len(channels); channels.extend(payload)
                motions.append((frames, channel_ids[payload]))
        struct.pack_into('<4I', ch, 0, nb, bn, motion_ids[mk], mn)
        characters.append(ch)
    out = bytearray(80)
    if directory:
        n = max(directory) + 1
        struct.pack_into('<4s2I', out, 68, b'JCL1', n, 80)
        out.extend(struct.pack('<%dI' % n, *(directory.get(i, 0) for i in range(n))))
    oc = align(out); out.extend(b''.join(characters))
    ob = align(out); out.extend(bones)
    ba = align(out); out.extend(batches)
    ov = align(out); out.extend(verts)
    oi = align(out); out.extend(indices)
    ot = align(out); out.extend(bytes(len(textures) * 16))
    om = align(out); channel_start = om + len(motions) * 8
    for frames, at in motions: out.extend(struct.pack('<2I', frames, channel_start + at))
    out.extend(channels); ok = align(out); out.extend(keys); op = align(out)
    for ti, (w, ht, flags, pad, clut, pix) in enumerate(textures):
        co = align(out, 64); out.extend(clut); po = align(out, 64); out.extend(pix)
        struct.pack_into('<4H2I', out, ot + ti * 16, w, ht, flags, pad, co, po)
    struct.pack_into('<4s16I', out, 0, b'JGC2', len(characters), len(bones) // 48,
                     len(batches) // 16, len(verts) // 28, len(indices) // 2,
                     len(textures), len(motions), oc, ob, ba, ov, oi, ot, om, ok, op)
    return bytes(out), sources, batch_sources
