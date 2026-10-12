"""Offline Shibuya lighting; no extra runtime normals, passes or vertices.

02f5a4 installs the normal/cel lights through 0c3828/0c3ac4. The light
parameters are read from the user's executable on first use. This is a
static bake, not dynamic cel-boundary or cast-shadow rendering.
"""
from collections.abc import Mapping
from functools import lru_cache
import math
from pathlib import Path
import struct

ROOT = Path(__file__).resolve().parents[2]
EXE_BASE = 0x8c010000

# Presentation adjustment for the PSP's static character bake. This blends
# toward the original unlit material, keeping texture hues and lit regions.
# Keep it separate from the recovered Dreamcast light constants below.
# Key by original PLAY file: chars_spec's legacy names label actual Cube
# (recodat / model 10) "Slate", and label Garam's plugdat "Cube".
CHARACTER_SHADOW_LIFT = {'012_gumdat.bin': 0.35, '022_recodat.bin': 0.45}


def character_shadow_lift(source):
    return CHARACTER_SHADOW_LIFT.get(source.rsplit('/', 1)[-1], 0)

def _decimal32(raw):
    """Preserve the legacy bake's shortest-decimal Python float arithmetic.

    Loading binary32 straight into Python would change a few vertex bytes:
    the original converter used the shortest decimal representation of each
    source parameter. Derive that representation without storing its value.
    """
    value = struct.unpack('<f', raw)[0]
    if not math.isfinite(value):
        raise ValueError('Non-finite light parameter in user executable')
    for digits in range(1, 10):
        candidate = float(format(value, f'.{digits}g'))
        if struct.pack('<f', candidate) == raw:
            return candidate
    raise ValueError('Cannot represent source light parameter')


def profile_from_executable(data):
    """Decode Shibuya constructor literal locations from the supported disc.

    Addresses describe the input format; no original light values or palette
    are embedded here. The setup pipeline validates the executable identity.
    """
    def raw(address, size):
        offset = address - EXE_BASE
        if offset < 0 or offset + size > len(data):
            raise ValueError('Truncated executable light parameters')
        return data[offset:offset + size]

    def number(address):
        return _decimal32(raw(address, 4))

    def color(address):
        value = struct.unpack('<I', raw(address, 4))[0]
        if value > 255:
            raise ValueError('Invalid light palette channel in user executable')
        return value

    # The original RGB register source is a hardware FLDI1 instruction.
    # Validate its opcode rather than accepting a fallback game profile.
    instruction = struct.unpack('<H', raw(0x8c02f74c, 2))[0]
    if instruction & 0xf0ff != 0xf09d:
        raise ValueError('Unsupported executable light initializer')
    rgb = (1.0,) * 3
    axis = number(0x8c02f910)
    return {
        'direction': (axis, number(0x8c02f914), axis),
        'diffuse': number(0x8c02f90c), 'ambient': number(0x8c02f904),
        'rgb': rgb, 'cel_direction': (axis,) * 3,
        'cel_colors': ((color(0x8c02f924),) * 3,
                       (color(0x8c02f920), color(0x8c02f920), color(0x8c02f91c))),
    }


@lru_cache(maxsize=1)
def _load_profile(name):
    if name != 'shibuya':
        raise KeyError(name)
    return profile_from_executable((ROOT / 'decompile/extracted/1ST_READ.BIN').read_bytes())


class _Profiles(Mapping):
    # Import and option validation do not require local game data; only an
    # actual color bake opens the verified user input.
    def __contains__(self, name):
        return name == 'shibuya'

    def __getitem__(self, name):
        return _load_profile(name)

    def __iter__(self):
        return iter(('shibuya',))

    def __len__(self):
        return 1


PROFILES = _Profiles()


def shade(profile, material, normal, toon=False):
    p = PROFILES[profile]
    if toon:
        # 2956e0: signed -dot; 0b8b1c's first palette is the <=0 region.
        value = -sum(a*b for a, b in zip(p['cel_direction'], normal))
        rgb = [v/255 for v in p['cel_colors'][value > 0]]
        intensity = 1.0
    else:
        direction = p['direction']
        length = math.sqrt(sum(v*v for v in direction))
        value = p['ambient'] - p['diffuse']*sum(a*b/length for a, b in zip(direction, normal))
        intensity = max(p['ambient'], value)
        rgb = p['rgb']
    # ARGB source material -> ABGR vertex color, retaining alpha exactly.
    channels = [(material >> shift) & 255 for shift in (16, 8, 0)]
    r, g, b = [max(0, min(255, int(c*light*intensity))) for c, light in zip(channels, rgb)]
    return (material & 0xff000000) | (b << 16) | (g << 8) | r
