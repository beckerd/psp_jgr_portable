#!/usr/bin/env python3
"""The five original STAGELAST graffiti descriptors and reveal meshes.

FGT1 <magic,5>, five <record_bytes,style,anchors,subvolumes> records,
GfoTemplate + bones + reveal vertices + per-triangle <texture,flags,diffuse>,
raw 32-byte anchors and 176-byte subvolumes. Source pointers are resolved
from the executable's bindings. Texture 1 / graph stroke 25 is original
background art; it must NOT be discarded as an unpainted player stroke.
"""
import argparse
from pathlib import Path
import struct
from object_graffiti import mesh
ROOT=Path(__file__).resolve().parents[2]
BASE=0x8cb00000
def convert():
    exe=(ROOT/'decompile/extracted/1ST_READ.BIN').read_bytes()
    data=(ROOT/'decompile/afs/STAGELAST/001_lastdat.bin').read_bytes()
    ew=lambda a:struct.unpack_from('<I',exe,a-0x8c010000)[0]
    w=lambda a:struct.unpack_from('<I',data,a-BASE)[0]
    addresses=[w(ew(0x8c1d0e54)+i*4) for i in range(4)]+[w(ew(0x8c1d0ea0))]
    rows=[]
    for i,a in enumerate(addresses):
        kind,nanchor,nvolume=w(a),w(a+0xf0),w(a+0xf8)
        assert (kind,nanchor,nvolume)==((3,7,1) if i<4 else (1,0,0))
        materials=[];flags,bones,verts=mesh(data,a,BASE,materials)
        assert len(materials)*3==len(verts) and all(m[0] in (0,1) for m in materials)
        row=struct.pack('<8I',4,0,i,a,kind,len(verts),flags,len(bones))+data[a-BASE+4:a-BASE+0xe8]
        row+=b''.join(struct.pack('<hH3f3i3f2I',*b) for b in bones)
        row+=b''.join(struct.pack('<2fI3fI',*v) for v in verts)
        row+=b''.join(struct.pack('<3I',*m) for m in materials)
        for pointer,count,stride in [(w(a+0xec),nanchor,32),(w(a+0xf4),nvolume,176)]:
            if count:
                assert BASE<=pointer<=BASE+len(data)-count*stride
                row+=data[pointer-BASE:pointer-BASE+count*stride]
        rows.append(struct.pack('<4I',16+len(row),w(a+0xe8),nanchor,nvolume)+row)
    return struct.pack('<4sI',b'FGT1',5)+b''.join(rows)
def mask_texture(index=0):
    # 095380: shared texlist slot 1, ARGB4444 VQ+mips 256x256. The
    # source file matches the live 8cfb8000 upload prefix byte-for-byte.
    from pvr_decode import decode
    names=('000_e_grfm01.bin','001_e_grfm02.bin','002_e_grfm03.bin','003_e_grfm05.bin','004_e_postm.bin')
    raw=(ROOT/'decompile/afs/U_GRF'/names[index]).read_bytes()
    chunk=b'PVRT'+struct.pack('<IBBHHH',len(raw)+8,2,4,0,256,256)+raw
    rgba=decode(chunk).getdata()
    return b''.join(struct.pack('<H',((a//17)<<12)|((r//17)<<8)|((g//17)<<4)|(b//17)) for r,g,b,a in rgba)
if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    out=ROOT/'psp/finalgraffiti.bin';data=convert()
    if args.check:assert out.read_bytes()==data
    else:out.write_bytes(data)
    mask=mask_texture();out=ROOT/'psp/finaltagmask.bin'
    if args.check:assert out.read_bytes()==mask
    else:out.write_bytes(mask)
    print('Five source final-stage targets:',len(data),'bytes')
