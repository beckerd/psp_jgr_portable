#!/usr/bin/env python3
"""050d5c/050e48: both Kogane water texlists use the same 16 frames.

Keep all original RGBA colours in one shared T8 palette, without resizing
or quantization. Only the immutable frames change; original JGS geometry,
vertex colours and source task visibility remain in charge of placement.
"""
import argparse
import json
import struct
from pathlib import Path
from pvr_decode import decode
from ninjalib import swizzle8
ROOT=Path(__file__).resolve().parents[2]
GBIX=(20088,*range(20009,20024))

def build():
    source=ROOT/'decompile';mapping=json.loads((source/'stage2_texmap.json').read_text())
    images={};ids=[]
    for pack in mapping['packs']:
        data=(source/pack).read_bytes();offset=0
        while (at:=data.find(b'GBIX',offset))>=0:
            offset=at+8;gbix=struct.unpack_from('<I',data,at+8)[0];ids.append(gbix)
            if gbix not in GBIX:continue
            p=data.find(b'PVRT',at+8,at+64);size=struct.unpack_from('<I',data,p+4)[0]
            image=decode(data[p:p+size+8]).convert('RGBA');assert image.size==(64,64)
            images[gbix]=list(image.getdata())
    for address in (0x8c1a8108,0x8c1a8210):
        assert tuple(ids[i] for i in mapping['texlists'][str(address)])==GBIX
    colours=sorted({c for pixels in images.values() for c in pixels});assert len(colours)==32
    palette=bytes(v for c in colours for v in c)+bytes(1024-len(colours)*4)
    lookup={c:i for i,c in enumerate(colours)}
    frames=b''.join(swizzle8(bytes(lookup[c] for c in images[g]),64,64) for g in GBIX)
    return struct.pack('<4s7I',b'KWS1',16,64,64,60,64,1088,66624)+bytes(32)+palette+frames

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--check',action='store_true');args=parser.parse_args()
    target=ROOT/'psp/kogane_surface.bin';data=build()
    if args.check:assert target.read_bytes()==data
    else:target.write_bytes(data)
