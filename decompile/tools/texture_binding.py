"""Resolve legacy converter texture bindings from the user's source files.

Public rows carry an executable pointer-array address, count and optional
rotation, matching native model recipe kind 1 / mode 3. Decoded texture-slot
lists are temporary conversion data, never part of the public specification.
"""
from pathlib import Path
import struct


def resolve_texture_binding(binding, texture, executable_path=Path('extracted/1ST_READ.BIN')):
    """Return source-bank indices for one bounded executable pointer array.

The native recipe's six fields are image kind, image base, array address,
packed count/rotation, texture base, and reader mode. Other reader modes are
deliberately rejected here until a legacy converter requires them.
"""
    try:
        fields = binding.split(',')
        if len(fields) != 6:
            raise ValueError
        kind, base, array, packed, texture_base, mode = (
            int(value, 10 if index in (0, 5) else 16)
            for index, value in enumerate(fields))
    except (AttributeError, TypeError, ValueError):
        raise ValueError('Invalid source texture binding fields') from None
    if (kind != 1 or mode != 3 or
            any(value < 0 or value > 0xffffffff for value in (base, array, packed, texture_base))):
        raise ValueError('Unsupported source texture binding reader')
    count, rotation = packed & 0xffff, packed >> 16
    if not 1 <= count <= 256 or rotation >= count or array & 3:
        raise ValueError('Invalid source texture binding extent')
    executable = Path(executable_path).read_bytes()
    offset = array - base
    if offset < 0 or array + count * 4 > 0x100000000 or offset + count * 4 > len(executable):
        raise ValueError('Texture pointer array outside user executable')

    # Preserve the legacy converter/native recipe's bank numbering. Validate
    # every referenced chunk; an unknown pointer must not become texture zero.
    offsets = {}
    at = 0
    while True:
        at = texture.find(b'GBIX', at)
        if at < 0:
            break
        offsets[at] = len(offsets)
        at += 8
    result = []
    for index in range(count):
        address = struct.unpack_from('<I', executable, offset + ((index + rotation) % count) * 4)[0]
        at = address - texture_base
        if at not in offsets or at + 8 > len(texture):
            raise ValueError('Texture pointer does not identify a source GBIX chunk')
        pvr = at + 8 + struct.unpack_from('<I', texture, at + 4)[0]
        if pvr + 16 > len(texture) or texture[pvr:pvr + 4] != b'PVRT':
            raise ValueError('Invalid source PVRT header')
        size = struct.unpack_from('<I', texture, pvr + 4)[0]
        if size < 8 or pvr + 8 + size > len(texture):
            raise ValueError('Source PVR chunk exceeds texture bank')
        result.append(offsets[at])
    return result
