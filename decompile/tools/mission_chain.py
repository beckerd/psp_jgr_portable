"""Original loaded-image chains, not a scan for pointer-looking words.

041f78 starts at 8cde0000 and follows JMDc +ec; 09fec8 starts at
8cde6000 and follows JMSc +50. A zero link ends either chain. Invalid
links are errors, never permission to silently truncate an export.
"""
import struct
from pathlib import Path

# Confirmed by the original US loader/callback captures. E_ images are
# alternate authored variants, not merely translated strings.
US_MISSIONS={'S1M07','S2M03','S2M07','S3M07'}
def mission_source(path):
    path=Path(path)
    if path.stem.split('_')[-1].upper() in US_MISSIONS:
        return path.parent.parent/'MISSIONDATA'/path.name
    return path


def chain_offsets(data, magic=b'JMDc'):
    base, link = {b'JMDc': (0x8cde0000, 0xec),
                  b'JMSc': (0x8cde6000, 0x50)}[magic]
    offsets = []
    at = 0
    while True:
        if at < 0 or at % 4 or at + link + 4 > len(data):
            raise ValueError(f'{magic!r}: out-of-image link {base+at:08x}')
        if at in offsets:
            raise ValueError(f'{magic!r}: cyclic link {base+at:08x}')
        if data[at:at+4] != magic:
            raise ValueError(f'{magic!r}: invalid header at {base+at:08x}')
        offsets.append(at)
        address = struct.unpack_from('<I', data, at + link)[0]
        if address == 0:
            return offsets
        at = address - base
