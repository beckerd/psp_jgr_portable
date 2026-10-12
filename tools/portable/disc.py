#!/usr/bin/env python3
"""Read a user's Dreamcast GDI, ISO9660 filesystem and CRI AFS archives.

No disc bytes are bundled. Supports high-density MODE1 data tracks in raw
2352-byte or cooked 2048-byte sectors. Paths and archive extents are checked
before extraction; the supported release is identified by executable SHA-256.
"""
from __future__ import annotations
import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path, PurePosixPath
import shlex
import struct

BLOCK = 2048
SUPPORTED_EXE = {
    '8b294765225fc32020dd59010464a730b710d0bd79f69c79f16218cc4feac6a6':
    {'id': 'jgr-us-v1.005', 'product': 'MK-51058', 'version': 'V1.005', 'bytes': 3075648}
}

class DiscError(ValueError):
    pass

def safe_name(name: str) -> str:
    p = PurePosixPath(name)
    if not name or '\\' in name or any(ord(c)<32 or ord(c)==127 for c in name) or p.is_absolute() or any(v in ('', '.', '..') for v in name.split('/')):
        raise DiscError(f'Unsafe disc path: {name!r}')
    return p.as_posix()

def sha256(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()

@dataclass(frozen=True)
class Track:
    number: int
    lba: int
    kind: int
    sector_size: int
    path: Path
    offset: int

    @property
    def sectors(self):
        size = self.path.stat().st_size - self.offset
        if size < 0 or size % self.sector_size:
            raise DiscError(f'Truncated or unaligned track: {self.path.name}')
        return size // self.sector_size

@dataclass(frozen=True)
class Entry:
    name: str
    lba: int
    size: int

class Disc:
    def __init__(self, gdi: Path):
        self.path = gdi.resolve()
        try:
            lines = [line.strip() for line in self.path.read_text(encoding='utf-8-sig').splitlines() if line.strip()]
            count = int(lines[0]); tracks = []
            if count != len(lines) - 1 or not 1 <= count <= 99:
                raise DiscError('GDI track count mismatch')
            for line in lines[1:]:
                fields = shlex.split(line)
                if len(fields) != 6: raise DiscError('Expected six GDI track fields')
                no, lba, kind, sector, name, off = fields
                no, lba, kind, sector, off = map(int, (no, lba, kind, sector, off))
                if no != len(tracks)+1 or min(lba, off) < 0 or kind not in (0,4) or sector not in (2048,2352):
                    raise DiscError('Unsupported GDI track metadata')
                relative = safe_name(name); path = (self.path.parent / relative).resolve()
                if not path.is_relative_to(self.path.parent): raise DiscError('Track escapes GDI directory')
                if not path.is_file(): raise DiscError(f'Missing track: {relative}')
                track = Track(no,lba,kind,sector,path,off)
                if not track.sectors: raise DiscError('Empty track')
                tracks.append(track)
            if any(a.lba >= b.lba for a,b in zip(tracks,tracks[1:])):
                raise DiscError('GDI tracks are not in LBA order')
            candidates = [t for t in tracks if t.kind==4 and t.lba >= 45000]
            if len(candidates) != 1: raise DiscError('Expected one high-density data track')
            self.tracks = tracks; self.data = candidates[0]
        except (IndexError, UnicodeError, OSError, ValueError) as exc:
            if isinstance(exc,DiscError): raise
            raise DiscError(f'Invalid GDI: {exc}') from exc
        self._stream = self.data.path.open('rb')
        try:
            self.ip = self.sector(self.data.lba)[:256]
            pvd = self.sector(self.data.lba+16)
            if pvd[:7] != b'\x01CD001\x01': raise DiscError('Missing ISO9660 primary volume descriptor')
            if self._both16(pvd,128) != BLOCK: raise DiscError('Unsupported ISO logical block size')
            self.volume = pvd[40:72].decode('ascii').strip()
            self.entries = list(self._walk(self._record(pvd[156:190]), '', set()))
            if len({e.name.casefold() for e in self.entries}) != len(self.entries):
                raise DiscError('Duplicate ISO file names')
        except Exception:
            self.close(); raise

    def close(self):
        self._stream.close()
    def __enter__(self): return self
    def __exit__(self,*args): self.close()
    @staticmethod
    def _both16(data,off):
        a,b=struct.unpack_from('<H',data,off)[0],struct.unpack_from('>H',data,off+2)[0]
        if a!=b: raise DiscError('ISO endian copies disagree')
        return a
    @staticmethod
    def _both32(data,off):
        a,b=struct.unpack_from('<I',data,off)[0],struct.unpack_from('>I',data,off+4)[0]
        if a!=b: raise DiscError('ISO endian copies disagree')
        return a
    def sector(self,lba):
        idx=lba-self.data.lba
        if not 0<=idx<self.data.sectors: raise DiscError(f'ISO extent outside data track at LBA {lba}')
        self._stream.seek(self.data.offset+idx*self.data.sector_size)
        raw=self._stream.read(self.data.sector_size)
        if len(raw)!=self.data.sector_size: raise DiscError('Truncated sector')
        if self.data.sector_size==2352:
            if raw[:12]!=b'\x00'+b'\xff'*10+b'\x00' or raw[15]!=1:
                raise DiscError('Only raw MODE1 data sectors are supported')
            return raw[16:2064]
        return raw
    def extent(self,lba,size):
        if size<0 or lba<self.data.lba or lba+(size+BLOCK-1)//BLOCK>self.data.lba+self.data.sectors:
            raise DiscError('ISO extent exceeds data track')
        return b''.join(self.sector(lba+i)[:min(BLOCK,size-i*BLOCK)] for i in range((size+BLOCK-1)//BLOCK))
    def _record(self,rec):
        if len(rec)<34 or rec[0]!=len(rec) or 33+rec[32]>len(rec): raise DiscError('Malformed ISO directory record')
        if rec[1] or rec[26] or rec[27] or rec[25]&0x80: raise DiscError('Extended/interleaved/multi-extent ISO files are unsupported')
        lba,size=self._both32(rec,2),self._both32(rec,10)
        return lba,size,rec[25],rec[33:33+rec[32]]
    def _walk(self,record,parent,ancestors):
        lba,size,flags,_=record
        if (lba,size) in ancestors or len(ancestors)>32 or size>16*1024*1024:
            raise DiscError('Invalid/cyclic ISO directory')
        data=self.extent(lba,size); off=0
        while off<len(data):
            n=data[off]
            if not n: off=(off//BLOCK+1)*BLOCK; continue
            if off%BLOCK+n>BLOCK or off+n>len(data): raise DiscError('Directory record crosses sector')
            child=self._record(data[off:off+n]);off+=n
            if child[3] in (b'\x00',b'\x01'): continue
            try: name=child[3].decode('ascii').split(';')[0]
            except UnicodeError as exc: raise DiscError('Non-ASCII ISO name') from exc
            name=safe_name(f'{parent}/{name}' if parent else name)
            if child[2]&2: yield from self._walk(child,name,ancestors|{(lba,size)})
            else:
                # Validate even zero-length files, without allocating their contents.
                if child[0]<self.data.lba or child[0]+(child[1]+BLOCK-1)//BLOCK>self.data.lba+self.data.sectors:
                    raise DiscError(f'File extent exceeds data track: {name}')
                yield Entry(name,child[0],child[1])
    def entry(self,name):
        rows=[e for e in self.entries if e.name.casefold()==name.casefold()]
        if len(rows)!=1: raise DiscError(f'Missing disc file {name}')
        return rows[0]
    def identify(self):
        e=self.entry('1ST_READ.BIN'); h=hashlib.sha256(self.extent(e.lba,e.size)).hexdigest()
        spec=SUPPORTED_EXE.get(h)
        if not spec or e.size!=spec['bytes']: raise DiscError(f'Unsupported executable SHA-256: {h}; expected Jet Grind Radio USA v1.005')
        if self.ip[:16]!=b'SEGA SEGAKATANA ' or self.ip[64:74].decode('ascii').strip()!=spec['product'] or self.ip[74:80].decode('ascii')!=spec['version']:
            raise DiscError('Boot sector does not match supported release')
        return dict(spec,executable_sha256=h)
    def fingerprint(self):
        release=self.identify()
        tracks=[dict(number=t.number,lba=t.lba,kind=t.kind,sector_size=t.sector_size,offset=t.offset,
                     file=t.path.relative_to(self.path.parent).as_posix(),bytes=t.path.stat().st_size,sha256=sha256(t.path)) for t in self.tracks]
        canonical=json.dumps([release,tracks],sort_keys=True,separators=(',',':')).encode()
        return dict(schema=1,release=release,volume=self.volume,gdi_sha256=sha256(self.path),tracks=tracks,
                    fingerprint=hashlib.sha256(canonical).hexdigest())
    def extract(self,out):
        out=Path(out);out.mkdir(parents=True,exist_ok=False)
        rows=[]
        for e in self.entries:
            path=out/e.name;path.parent.mkdir(parents=True,exist_ok=True);h=hashlib.sha256()
            with path.open('xb') as stream:
                for offset in range(0,e.size,512*BLOCK):
                    block=self.extent(e.lba+offset//BLOCK,min(512*BLOCK,e.size-offset));stream.write(block);h.update(block)
            rows.append(dict(path=e.name,bytes=e.size,sha256=h.hexdigest()))
        return rows

def extract_afs(path: Path,outroot: Path):
    """Preserve the established <archive>/<index>_<original name> convention."""
    data=path.read_bytes()
    if len(data)<8 or data[:4] not in (b'AFS\0',b'AFS '): raise DiscError(f'Not an AFS archive: {path.name}')
    count=struct.unpack_from('<I',data,4)[0]
    if count>100000 or 8+count*8>len(data): raise DiscError('Invalid AFS entry table')
    entries=[struct.unpack_from('<II',data,8+i*8) for i in range(count)]
    names=['']*count
    for ptr in (8+count*8,0x7fff8):
        if ptr+8>len(data):continue
        start,size=struct.unpack_from('<II',data,ptr)
        if start and start+size<=len(data) and size>=count*48:
            for i in range(count):
                try:names[i]=data[start+i*48:start+i*48+32].split(b'\0')[0].decode('ascii')
                except UnicodeError as exc:raise DiscError('Non-ASCII AFS filename') from exc
            break
    rows=[]
    for i,((start,size),name) in enumerate(zip(entries,names)):
        if start+size>len(data):raise DiscError(f'AFS entry {i} exceeds archive')
        if name and ('/' in name or '\\' in name):raise DiscError('Nested AFS name')
        relative=safe_name(f'{path.stem}/{i:03d}'+(f'_{name}' if name else ''))
        rows.append((relative,start,size))
    for relative,start,size in rows:
        dest=outroot/relative;dest.parent.mkdir(parents=True,exist_ok=True)
        with dest.open('xb') as stream:stream.write(data[start:start+size])
    return [dict(path=n,bytes=s) for n,_,s in rows]

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('gdi',type=Path);p.add_argument('--extract',type=Path);p.add_argument('--fingerprint',type=Path);a=p.parse_args()
    with Disc(a.gdi) as disc:
        record=disc.fingerprint()
        if a.extract:record['files']=disc.extract(a.extract)
    if a.fingerprint:a.fingerprint.write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record,indent=2))
if __name__=='__main__':main()
