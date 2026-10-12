#!/usr/bin/env python3
"""Convert an explicitly supplied 480x272 PNG to optional PSP setup artwork.

The output belongs in a PRIVATE installation at data/setup/background.jbg,
outside the verified game-asset pack. No artwork is bundled with this tool.
Transparent pixels are composited over black; no resizing is performed.
"""
import argparse
import hashlib
from io import BytesIO
import json
from pathlib import Path
import struct
import zlib

MAGIC = b'JGRBG01\0'
WIDTH, HEIGHT = 480, 272
PAYLOAD_BYTES = WIDTH * HEIGHT * 2
HEADER = struct.Struct('<8sIIII')
INSTALL_PATH = 'data/setup/background.jbg'


def validate(data):
    """Reject incomplete, altered, or unsupported JBG files."""
    if len(data) != HEADER.size + PAYLOAD_BYTES:
        raise ValueError('Invalid setup background length')
    magic, width, height, size, crc = HEADER.unpack_from(data)
    if (magic, width, height, size) != (MAGIC, WIDTH, HEIGHT, PAYLOAD_BYTES):
        raise ValueError('Invalid setup background header')
    if zlib.crc32(data[HEADER.size:]) & 0xffffffff != crc:
        raise ValueError('Invalid setup background checksum')
    return {'format': 'JGRBG01-RGB565-LE', 'width': width, 'height': height,
            'payload_bytes': size, 'payload_crc32': '%08x' % crc}


def encode_png(source):
    from PIL import Image

    source = Path(source)
    original = source.read_bytes()
    with Image.open(BytesIO(original)) as image:
        if image.format != 'PNG':
            raise ValueError('Setup background must be a PNG')
        if image.size != (WIDTH, HEIGHT):
            raise ValueError('Setup background must be exactly 480x272 pixels')
        if getattr(image, 'is_animated', False):
            raise ValueError('Setup background must be a single PNG image')
        rgba = image.convert('RGBA')
        rgb = Image.alpha_composite(Image.new('RGBA', rgba.size, (0, 0, 0, 255)), rgba).convert('RGB')
        payload = bytearray(PAYLOAD_BYTES)
        for index, (red, green, blue) in enumerate(rgb.getdata()):
            # PSP GU_PSM_5650: red low 5, green middle 6, blue high 5 bits.
            pixel = (red >> 3) | ((green >> 2) << 5) | ((blue >> 3) << 11)
            struct.pack_into('<H', payload, index * 2, pixel)
    crc = zlib.crc32(payload) & 0xffffffff
    data = HEADER.pack(MAGIC, WIDTH, HEIGHT, len(payload), crc) + payload
    metadata = validate(data)
    metadata.update(source='explicit-local-png', source_name=source.name,
                    source_sha256=hashlib.sha256(original).hexdigest(),
                    verified_game_asset=False, public_distribution=False)
    return data, metadata


def convert(source, destination):
    """Create a new file; never replace an existing installation artifact."""
    data, metadata = encode_png(source)
    destination = Path(destination)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with destination.open('xb') as output:
        output.write(data)
    return dict(metadata, bytes=len(data), sha256=hashlib.sha256(data).hexdigest())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True, help='Explicitly supplied 480x272 PNG')
    parser.add_argument('--out', type=Path, required=True, help='New private .jbg output file')
    args = parser.parse_args()
    try:
        print(json.dumps(convert(args.source, args.out), indent=2))
    except (ValueError, OSError) as error:
        parser.exit(1, str(error) + '\n')


if __name__ == '__main__':
    main()
