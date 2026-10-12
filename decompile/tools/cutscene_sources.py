"""Bounded readers for the US executable's EVENT/DEMO dependency tables.

Addresses, archive indices and aliases come from the loader, not filenames.
This module describes source assets, never whether a campaign route works.
"""
from functools import lru_cache
from pathlib import Path
import hashlib
import struct

ROOT = Path(__file__).resolve().parents[2]
DEC = ROOT / 'decompile'
EVENT_BASE = 0x8c900000
MOVIE_BASE = 0x8c800000


class Image:
    def __init__(self, data, base):
        self.data, self.base = data, base

    def read(self, address, size):
        offset = address - self.base
        if offset < 0 or size < 0 or offset + size > len(self.data):
            raise ValueError(f'address {address:#x}+{size} outside {self.base:#x} image')
        return self.data[offset:offset + size]

    def u(self, address):
        if address & 3:
            raise ValueError(f'unaligned word {address:#x}')
        return struct.unpack('<I', self.read(address, 4))[0]

    def array(self, address, limit=2048):
        for index in range(limit):
            value = self.u(address + 4 * index)
            if not value:
                return
            yield value
        raise ValueError(f'unterminated pointer array at {address:#x}')

    def commands(self, address):
        for _ in range(4096):
            words = struct.unpack('<5I', self.read(address, 20))
            yield address, words
            if words[0] == 100:
                return
            address += 20
        raise ValueError('unterminated EVENT stream')


@lru_cache(None)
def executable():
    return Image((DEC / 'extracted/1ST_READ.BIN').read_bytes(), 0x8c010000)


@lru_cache(None)
def afs_file(folder, index):
    paths = list((DEC / 'afs' / folder).glob(f'{index:03d}_*'))
    if len(paths) != 1:
        raise ValueError(f'{folder} entry {index}: expected one file, got {paths}')
    return paths[0]


def fingerprint(path):
    data = path.read_bytes()
    return dict(source=str(path.relative_to(DEC)), bytes=len(data),
                sha256=hashlib.sha256(data).hexdigest())


def texture_image(path):
    """Original AFS sector read, including verified on-disc sector tail.

    EVENT_4050's last PVR advertises 16 bytes beyond its AFS logical length.
    01674c reads 298f2c's sector count << 11, so the original receives the
    actual disc padding too. Never fabricate those pixels or mutate the
    extracted file; require the matching source archive to supply the tail.
    """
    data = path.read_bytes()
    archive = DEC / 'extracted/JETRADIO' / (path.parent.name + '.AFS')
    index = int(path.name.split('_')[0])
    with archive.open('rb') as f:
        header = f.read(8)
        assert header[:4] in (b'AFS\0', b'AFS ') and index < struct.unpack_from('<I', header, 4)[0]
        f.seek(8 + index * 8)
        offset, length = struct.unpack('<2I', f.read(8))
        assert offset % 2048 == 0 and length == len(data)
        f.seek(offset);loaded = f.read((length + 2047) & ~2047)
    assert loaded[:length] == data and len(loaded) == (length + 2047) & ~2047
    return loaded


def event_descriptors():
    ex = executable()
    result = []
    for index in range(112):
        address = 0x8c1d0fd0 + index * 0x44
        # 0eacb8 -> 0165a4 -> 05bc38 case 10. The short 8-byte
        # request list is NOT the subsequent 32-byte file-load record.
        request = ex.u(ex.u(0x8c0eacd0) + 4 * index)
        assert ex.u(request) == 0x10 and ex.u(request + 8) == 0x22
        record = ex.u(ex.u(0x8c05be64) + ex.u(request + 4) * 4)
        assert ex.u(record) == 0x43 and ex.u(record + 8) == EVENT_BASE
        assert ex.u(record + 20) == 2 and ex.u(record + 32) == 0x52
        textures = ex.u(ex.u(0x8c0eacec) + 4 * index)
        assert ex.u(textures) == 0x43 and ex.u(textures + 32) == 0x52
        dat_index, txr_index = ex.u(record + 4), ex.u(textures + 4)
        result.append(dict(id=index, flags=ex.u(address + 4),
            name=ex.read(address + 8, 24).split(b'\0')[0].decode('ascii'),
            stage=ex.u(address + 0x3c), chapter=ex.u(address + 0x40),
            loader_request=request, loader_record=record,
            source_index=dat_index, texture_index=txr_index,
            source=str(afs_file('EVENT', dat_index).relative_to(DEC)),
            textures=str(afs_file('EVENT', txr_index).relative_to(DEC))))
    return result


def pvr_offsets(data):
    """Walk actual chunk boundaries; GBIX bytes may occur inside pixels."""
    offsets = {}
    at = 0
    while at < len(data):
        if at + 8 > len(data) or data[at:at + 4] != b'GBIX':
            raise ValueError(f'invalid GBIX at {at:#x}')
        offsets[at] = len(offsets)
        pvr = at + 8 + struct.unpack_from('<I', data, at + 4)[0]
        if pvr + 16 > len(data) or data[pvr:pvr + 4] != b'PVRT':
            raise ValueError(f'invalid PVRT at {pvr:#x}')
        end = pvr + 8 + struct.unpack_from('<I', data, pvr + 4)[0]
        if end <= pvr + 8 or end > len(data):
            raise ValueError('PVR chunk outside texture image')
        at = end
    return offsets


def texture_lists(image, table, texture_data, texture_base):
    references = {}
    for i in range(1024):
        tl = image.u(table + 8 * i)
        if not tl:
            break
        ptr = image.u(table + 8 * i + 4)
        count = image.u(tl + 4)
        if not 0 < count <= 256:
            raise ValueError('invalid texlist size')
        references[tl] = [image.u(ptr + 4 * j) - texture_base for j in range(count)]
    else:
        raise ValueError('unterminated texture-loader table')
    # Some DEMO TXRs retain unused tail bytes from another image. Read ONLY
    # original loader pointers, never scan pixel bytes for plausible GBIX.
    addresses = sorted({p for row in references.values() for p in row})
    for at in addresses:
        if at < 0 or at + 8 > len(texture_data) or texture_data[at:at + 4] != b'GBIX':
            raise ValueError(f'bad source texture binding {at:#x}')
        pvr = at + 8 + struct.unpack_from('<I', texture_data, at + 4)[0]
        if pvr + 16 > len(texture_data) or texture_data[pvr:pvr + 4] != b'PVRT':
            raise ValueError('bad source PVR header')
        end = pvr + 8 + struct.unpack_from('<I', texture_data, pvr + 4)[0]
        if end > len(texture_data) or end <= pvr + 16:
            raise ValueError('truncated source PVR')
    index = {p: i for i, p in enumerate(addresses)}
    return {tl: [index[p] for p in row] for tl, row in references.items()}, addresses


def check_untextured(image, root):
    from ninjalib import read_object, parse_plist
    pending, seen = [root], set()
    while pending:
        address = pending.pop()
        if address in seen:
            raise ValueError('cyclic object hierarchy')
        seen.add(address)
        image.read(address, 52)
        node = read_object(image.data, address, image.base)
        if node['model']:
            plist = image.u(node['model'] + 4)
            if plist:
                assert all(t[0] == 0xffff for t in parse_plist(
                    image.data, plist - image.base, source_environment=True)), hex(root)
        pending.extend(p for p in (node['child'], node['sib']) if p)


def movie_manifest(movie):
    if not 0 <= movie < 35:
        raise ValueError('movie id outside original table')
    folder = f'DEMO{movie + 1:02d}'
    source, textures = afs_file(folder, 0), afs_file(folder, 1)
    image = Image(source.read_bytes(), MOVIE_BASE)
    lists, offsets = texture_lists(image, image.u(MOVIE_BASE + 8), textures.read_bytes(), MOVIE_BASE)
    ex = executable()
    voices = ex.u(0x8c1e5284 + movie * 4)
    # 0e9118 searches the 0x4c-byte DEMO descriptors by id. Tail fields
    # drive 015164/0153a4's save prompt and chained movies, not EVENT flags.
    descriptor = 0x8c1db660 + movie * 0x4c
    assert ex.u(descriptor) == movie
    next_movie = struct.unpack('<i', ex.read(descriptor + 0x44, 4))[0]
    descriptor_flags = ex.u(descriptor + 0x48)
    assert -1 <= next_movie < 35 and descriptor_flags <= 3
    segments = []
    for index, segment in enumerate(image.array(image.u(MOVIE_BASE + 4), 32)):
        camera, light, models = (image.u(segment + i * 4) for i in range(3))
        initial, motion, flags = (image.u(camera + i * 4) for i in range(3))
        assert image.u(motion + 8) == 0x401c1
        actors = []
        for actor in image.array(models, 256):
            root, mot, tl, af = (image.u(actor + i * 4) for i in range(4))
            assert af in (1, 2, 8, 0x12, 0x20, 0x41, 0x81, 0x101)
            assert not mot or image.u(mot + 8) in (0x30007, 0x20003)
            if not tl:
                check_untextured(image, root)
            actors.append(dict(address=actor, root=root, motion=mot, flags=af, texlist=tl,
                texture_map=lists[tl] if tl else [], frames=image.u(mot + 4) if mot else 0))
        voice = struct.unpack('<i', ex.read(voices + index * 4, 4))[0]
        assert -1 <= voice < 184
        segments.append(dict(address=segment, camera=initial, camera_motion=motion,
            camera_flags=flags, camera_frames=image.u(motion + 4), light=light,
            voice=voice, actors=actors))
    # 0de0e0 dispatches 28-byte timed caption commands. Opcode 13 calls
    # 0de300 -> 0e4608 -> 02d568: start a bank-31 voice immediately, without
    # the segment preload barrier. DEMO32 deliberately has segment voice -1.
    audio_cues = []
    script = image.u(image.u(MOVIE_BASE))
    for index in range(512):
        address = script + index * 28
        command, segment, tick, reference = struct.unpack('<4i', image.read(address, 16))
        if command == 16:
            break
        if command == 13:
            assert 0 <= segment < len(segments) and tick >= 0 and -1 <= reference < 184
            audio_cues.append(dict(address=address, segment=segment, tick=tick, voice=reference))
    else:
        raise ValueError('unterminated movie caption script')
    return dict(movie=movie, next_movie=next_movie, flags=descriptor_flags, source=str(source.relative_to(DEC)),
        textures=str(textures.relative_to(DEC)), source_sha256=fingerprint(source)['sha256'],
        texture_sha256=fingerprint(textures)['sha256'], texture_offsets=offsets, segments=segments,
        audio_cues=audio_cues)
