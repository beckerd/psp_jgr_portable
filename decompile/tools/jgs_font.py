#!/usr/bin/env python3
"""Losslessly retain E_FONTMAIN's transparent, white and BLACK texels.

FNT2: 24-byte header, 16 RGBA palette entries, then linear 4-bit indices.
The old FNT1 coverage exporter erased all 15,746 black outline texels.
Input may be the original PVR bank or a decoded PNG. Cells are 12x24,
42 per row, indexed by character code (not character minus 0x20).
"""
import sys, struct
from pathlib import Path
from PIL import Image

def convert(path,index=0):
    data=Path(path).read_bytes()
    if data[:4]==b'GBIX':
        from pvr_decode import decode
        pvr=8+struct.unpack_from('<I',data,4)[0]
        for _ in range(index):
            next_gbix=data.index(b'GBIX',pvr+8+struct.unpack_from('<I',data,pvr+4)[0])
            pvr=next_gbix+8+struct.unpack_from('<I',data,next_gbix+4)[0]
        im=decode(data[pvr:])
    else:im=Image.open(path).convert('RGBA')
    w,h=im.size;assert (w,h)==(512,512)
    palette=list(dict.fromkeys(im.getdata()));assert len(palette)<=16
    lookup={p:i for i,p in enumerate(palette)};out=bytearray(w*h//2)
    for i,p in enumerate(im.getdata()):out[i//2]|=lookup[p]<<(4*(i&1))
    palette += [(0,0,0,0)]*(16-len(palette))
    assert index in (0,1)
    return struct.pack('<4s5I',b'FNT2',w,h,24 if index else 12,24,21 if index else 42)+bytes(v for p in palette for v in p)+out

if __name__=='__main__':
    data=convert(sys.argv[1],int(sys.argv[3]) if len(sys.argv)>3 else 0);Path(sys.argv[2]).write_bytes(data)
    print(sys.argv[2],len(data),'bytes; original RGBA retained')
