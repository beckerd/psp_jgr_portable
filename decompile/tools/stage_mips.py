#!/usr/bin/env python3
"""Bake bounded authored Dreamcast mipmaps without changing the base JGS bank.

JGM1 header <4s7I>: magic, metadata FNV, texture count, record count,
pixel bytes, record offset, pixel offset, file size. Each 32-byte <4H6I>
record is texture, base width/height, levels=2, base palette+pixels FNV,
two pixel-relative offsets, two sizes, reserved=0. Pixels are swizzled T8
and share the immutable base palette. Runtime owns no extra palette.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

import numpy as np
from PIL import Image
from ninjalib import swizzle8
from stage_texture_residency import fnv

ROOT = Path(__file__).resolve().parents[2]
CAPTURE = ROOT / 'decompile/campaign_captures/source_lod_20261004'
CITIES = (1, 2, 3, 5, 6)
MIP_FORMATS = (2, 4, 0x11, 0x12)
PIXEL_BUDGET = 32768
RECORD_LIMIT = 16
MAX_RGB_RMSE = 12.0


def decode_mip(chunk, level=0):
    """Decode one authored square mip, level zero being the largest image.

Vectorized decoding is independent of the old base-only pixel loop. The
smallest VQ 1x1 sample is deliberately unsupported; exported levels are
at least 16x8 and never depend on its codebook-corner interpretation.
"""
    if len(chunk) < 16 or chunk[:4] != b'PVRT':
        raise ValueError('invalid PVRT header')
    length, pf, df, _, w, h = struct.unpack_from('<IBBHHH', chunk, 4)
    # Several original VQ headers overstate their chunk length. As in the
    # existing decoder, validate each actual level's required bytes below.
    if length < 8:
        raise ValueError('invalid PVRT size')
    if df not in MIP_FORMATS or pf not in (0, 1, 2):
        raise ValueError('unsupported authored mip format')
    if not w or w != h or w & (w-1) or not 0 <= level < w.bit_length():
        raise ValueError('invalid square mip level')
    size = w >> level
    payload = memoryview(chunk)[16:8+length]

    def morton(n):
        y, x = np.indices((n, n), dtype=np.uint32)
        address = np.zeros((n, n), dtype=np.uint32)
        for bit in range(n.bit_length()):
            address |= ((y >> bit) & 1) << (2*bit)
            address |= ((x >> bit) & 1) << (2*bit+1)
        return address

    if df in (2, 0x12):
        offset = 2 + sum(2*s*s for s in (1 << i for i in range(size.bit_length()-1)))
        if offset + 2*size*size > len(payload):
            raise ValueError('truncated twiddled mip')
        packed = np.frombuffer(payload, '<u2', size*size, offset)[morton(size)]
    else:
        if size < 2:
            raise ValueError('VQ 1x1 is outside the supported export range')
        entries = 256 if df == 4 else 16 if w <= 16 else 64 if w <= 32 else 256
        offset = entries*8 + sum(max(s*s//4, 1) for s in (1 << i for i in range(size.bit_length()-1)))
        if offset + size*size//4 > len(payload):
            raise ValueError('truncated VQ mip')
        codebook = np.frombuffer(payload, '<u2', entries*4).reshape(entries, 2, 2)
        indices = np.frombuffer(payload, 'u1', size*size//4, offset)[morton(size//2)]
        if int(indices.max()) >= entries:
            raise ValueError('VQ codebook index out of range')
        # Codebook order is x-major; image pixels are y-major.
        blocks = codebook[indices].transpose(0, 3, 1, 2)
        packed = blocks.reshape(size, size)
    rgba = np.empty((size, size, 4), dtype=np.uint8)
    if pf == 1:
        rgba[..., 0] = ((packed >> 11) & 31)*255//31
        rgba[..., 1] = ((packed >> 5) & 63)*255//63
        rgba[..., 2] = (packed & 31)*255//31
        rgba[..., 3] = 255
    elif pf == 0:
        rgba[..., 0] = ((packed >> 10) & 31)*255//31
        rgba[..., 1] = ((packed >> 5) & 31)*255//31
        rgba[..., 2] = (packed & 31)*255//31
        rgba[..., 3] = (packed >> 15)*255
    else:
        rgba[..., 0] = ((packed >> 8) & 15)*17
        rgba[..., 1] = ((packed >> 4) & 15)*17
        rgba[..., 2] = (packed & 15)*17
        rgba[..., 3] = ((packed >> 12) & 15)*17
    return Image.fromarray(rgba)


def base_payload(image, size):
    """Reproduce ninjalib.write_jgs exactly, for source identity only."""
    image = image.resize(size, Image.Resampling.LANCZOS)
    q = image.quantize(256, method=Image.Quantize.FASTOCTREE)
    pal = q.getpalette('RGBA') or q.getpalette()
    clut = bytearray()
    for i in range(256):
        if pal and i*4+3 < len(pal):
            pixel = pal[i*4:i*4+4]
        elif pal and i*3+2 < len(pal):
            pixel = [*pal[i*3:i*3+3], 255]
        else:
            pixel = [0, 0, 0, 0]
        clut.extend(pixel)
    return bytes(clut) + swizzle8(q.tobytes(), *size)


def quantize_mip(image, palette):
    """Nearest opaque base-palette entry, deterministic ties by palette ID."""
    rgba = np.asarray(image)
    if np.any(rgba[..., 3] != 255):
        raise ValueError('authored lower level is not opaque')
    pal = np.frombuffer(palette, np.uint8).reshape(256, 4)
    opaque = np.flatnonzero(pal[:, 3] == 255)
    if not len(opaque):
        raise ValueError('base palette has no opaque entries')
    colors, inverse = np.unique(rgba[..., :3].reshape(-1, 3), axis=0, return_inverse=True)
    distance = colors.astype(np.int32)[:, None, :] - pal[opaque, :3].astype(np.int32)[None, :, :]
    nearest = opaque[np.sum(distance*distance, axis=2).argmin(axis=1)]
    indices = nearest[inverse].astype(np.uint8).reshape(rgba.shape[:2])
    error = pal[indices, :3].astype(np.int32) - rgba[..., :3].astype(np.int32)
    metrics = dict(rgb_rmse=float(np.sqrt(np.mean(error*error))),
                   max_channel_error=int(np.abs(error).max()),
                   exact_pixel_fraction=float(np.mean(np.all(error == 0, axis=2))))
    return swizzle8(indices.tobytes(), image.width, image.height), metrics


def source_chunks(city):
    mapping = json.loads((ROOT/f'decompile/stage{city}_texmap.json').read_text())
    for pack in mapping['packs']:
        path = ROOT/'decompile'/pack
        data = path.read_bytes()
        start = 0
        ordinal = 0
        while True:
            start = data.find(b'GBIX', start)
            if start < 0:
                break
            pvrt = data.find(b'PVRT', start+8, start+64)
            if pvrt < 0:
                raise ValueError(f'missing PVRT: {path}:{start}')
            length = struct.unpack_from('<I', data, pvrt+4)[0]
            chunk = data[pvrt:pvrt+8+length]
            yield chunk, dict(pack=str(path.relative_to(ROOT)), index=ordinal,
                              gbix_offset=start, pvrt_offset=pvrt,
                              sha256=hashlib.sha256(chunk).hexdigest())
            ordinal += 1
            start += 8


def usage_weights(city, key, count, include):
    text = include.read_text()
    if not re.search(r'\{0x%08xu,%d,stage_texture_weight_%d\}' % (key, count, city), text):
        raise ValueError(f'stage {city}: texture usage metadata key mismatch')
    match = re.search(r'stage_texture_weight_%d\[\]=\{([^}]+)\}' % city, text)
    result = [int(x) for x in match[1].split(',')]
    if len(result) != count:
        raise ValueError('texture usage count mismatch')
    return result


def export_city(city, assets, usage):
    path = assets/f'stage{city}.jgs'
    data = path.read_bytes()
    if data[:4] != b'JGS1':
        raise ValueError('invalid JGS')
    nt = struct.unpack_from('<I', data, 4)[0]
    ot, _, _, ov, _, _ = struct.unpack_from('<6I', data, 24)
    key = fnv(data[:ov])
    weights = usage_weights(city, key, nt, usage)
    textures = [struct.unpack_from('<4H2I', data, ot+i*16) for i in range(nt)]
    wanted = {}
    payloads = {}
    for i, (w, h, flags, _, oc, op) in enumerate(textures):
        if flags & 1 or w < 64 or h < 32 or not weights[i] or (city == 2 and i == 60):
            continue
        payload = data[oc:oc+1024] + data[op:op+w*h]
        payloads[i] = payload
        wanted.setdefault((w, h, hashlib.sha256(payload).digest()), []).append(i)
    dimensions = sorted({(w, h) for w, h, _ in wanted})
    matches = {}
    decoded = 0
    for chunk, source in source_chunks(city):
        if chunk[9] not in MIP_FORMATS:
            continue
        image = decode_mip(chunk)
        decoded += 1
        if image.getextrema()[3][0] != 255:
            continue
        for size in dimensions:
            if size[0] != size[1] or size[0] > image.width:
                continue
            payload = base_payload(image, size)
            ids = wanted.get((*size, hashlib.sha256(payload).digest()), [])
            if not ids:
                continue
            lower = [decode_mip(chunk, image.width.bit_length()-size[0].bit_length()+level)
                     for level in (1, 2)]
            if any(im.getextrema()[3][0] != 255 for im in lower):
                continue
            for i in ids:
                assert payload == payloads[i], 'source fingerprint collision'
                encoded = [quantize_mip(im, payload[:1024]) for im in lower]
                matches.setdefault(i, []).append((source, encoded, image.size))
    eligible = []
    ambiguous = []
    quality_rejected = []
    for i, candidates in matches.items():
        if len({b''.join(level[0] for level in c[1]) for c in candidates}) != 1:
            ambiguous.append(i)
            continue
        errors = [level[1]['rgb_rmse'] for level in candidates[0][1]]
        if max(errors) > MAX_RGB_RMSE:
            quality_rejected.append(dict(texture=i, rgb_rmse=errors))
            continue
        eligible.append(i)
    eligible.sort(key=lambda i: (-weights[i]/sum(len(x[0]) for x in matches[i][0][1]), i))
    pixels = bytearray()
    records = bytearray()
    selected = []
    for i in eligible:
        source, encoded, source_size = matches[i][0]
        cost = sum(len(x[0]) for x in encoded)
        if len(selected) >= RECORD_LIMIT or len(pixels)+cost > PIXEL_BUDGET:
            continue
        w, h = textures[i][:2]
        offsets = []
        for blob, _ in encoded:
            assert len(pixels) % 16 == 0
            offsets.append(len(pixels))
            pixels.extend(blob)
        sizes = [len(x[0]) for x in encoded]
        records.extend(struct.pack('<4H6I', i, w, h, 2, fnv(payloads[i]), *offsets, *sizes, 0))
        selected.append(dict(texture=i, base_size=[w, h], source_size=list(source_size),
                             usage_weight=weights[i], mip_bytes=cost, source=source,
                             equivalent_sources=len(matches[i]),
                             levels=[dict(size=[w >> n, h >> n], **encoded[n-1][1]) for n in (1, 2)]))
    pixel_offset = 32+len(records)
    output = struct.pack('<4s7I', b'JGM1', key, nt, len(selected), len(pixels), 32,
                         pixel_offset, pixel_offset+len(pixels)) + records + pixels
    report = dict(city=city, base_sha256=hashlib.sha256(data).hexdigest(), metadata_key=key,
                  texture_count=nt, source_mipped_images=decoded, eligible_textures=len(eligible),
                  unmatched_textures=sorted(set(payloads)-set(matches)), ambiguous_textures=sorted(ambiguous),
                  quality_rejected=sorted(quality_rejected, key=lambda row: row['texture']),
                  record_count=len(selected), pixel_bytes=len(pixels), file_bytes=len(output),
                  sidecar_sha256=hashlib.sha256(output).hexdigest(), selected=selected)
    return bytes(output), report


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--assets', type=Path, default=ROOT/'psp')
    ap.add_argument('--usage', type=Path, default=ROOT/'psp/src/stage_texture_usage.inc')
    ap.add_argument('--report', type=Path)
    ap.add_argument('--city', '--stage', dest='city', type=int, choices=CITIES, action='append')
    ap.add_argument('--check', action='store_true')
    args = ap.parse_args()
    if args.report is None:
        # Separate make targets can run concurrently without racing the
        # all-city provenance report written by the complete export.
        args.report = CAPTURE / (f'mip_export_stage{args.city[0]}.json'
                                 if args.city and len(args.city) == 1 else 'mip_export.json')
    reports = []
    for city in args.city or CITIES:
        output, report = export_city(city, args.assets, args.usage)
        path = args.assets/f'stage{city}.jgm'
        if args.check:
            if path.read_bytes() != output:
                raise ValueError(f'{path}: authored mip sidecar is stale')
        else:
            path.write_bytes(output)
        reports.append(report)
        print(f'stage{city}: {report["record_count"]} textures, {report["pixel_bytes"]} mip bytes, {report["eligible_textures"]} eligible', flush=True)
    if not args.check:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(dict(pixel_budget=PIXEL_BUDGET, record_limit=RECORD_LIMIT,
                                              max_rgb_rmse=MAX_RGB_RMSE,
                                              quantization='nearest opaque base-palette RGB, no dithering',
                                              stages=reports), indent=2)+'\n')


if __name__ == '__main__':
    main()
