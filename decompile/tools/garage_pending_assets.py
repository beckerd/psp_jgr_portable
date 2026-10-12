#!/usr/bin/env python3
"""Export task 22a's ORIGINAL invitation cast, camera, text and image lists.

These are 105be0/105d0c, not the dancing residents' 105ba4/105c58.
"""
from pathlib import Path
import json
import struct
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[2]
DEC=ROOT/'decompile'
B=0x8c800000

def extract():
    exe=(DEC/'extracted/1ST_READ.BIN').read_bytes()
    dat=(DEC/'afs/U_GARAGE/001_garagedat.bin').read_bytes()
    txr=(DEC/'afs/U_GARAGE/002_garagetxr.bin').read_bytes()
    u=lambda a:struct.unpack_from('<I',exe,a-0x8c010000)[0]
    v=lambda a:struct.unpack_from('<I',dat,a-B)[0]
    offsets=[];o=0
    while True:
        o=txr.find(b'GBIX',o)
        if o<0:break
        offsets.append(o);o+=8
    rows=[];models=[-1]*15;lengths=[0]*15
    for c in (3,4,5,6,7,8,9,12):
        root=v(u(0x8c105be0+4*c));mot=v(u(0x8c105d0c+4*c))
        tl=u(0x8c105aec+4*c)
        tm=[offsets.index(u(tl+8+k*4)-B) for k in range(u(tl+4))]
        models[c]=len(rows);lengths[c]=v(mot)>>16
        assert root and mot and lengths[c]>1
        rows.append(f'Invite{c}:afs/U_GARAGE/001_garagedat.bin:afs/U_GARAGE/002_garagetxr.bin:'
                    f'0,0,0,0:rawat:{B:x}:{root:x}:{mot:x}:texmap='+','.join(map(str,tm))+':lighting=2')
    lines=['/* Generated from original US garage data by garage_pending_assets.py. */',
           'static const signed char PENDING_MODEL[15]={'+','.join(map(str,models))+'};',
           'static const unsigned PENDING_FRAMES[15]={'+','.join(map(str,lengths))+'};']
    placement=struct.unpack_from('<3f3i',dat,v(B+0x9c)-B+8)
    assert placement[3:]==(0,0,0)
    lines.append('static const float PENDING_POSITION[3]={'+','.join(x.hex()+'f' for x in placement[:3])+'};')
    h=v(B+0x19c);md=v(h);n=v(h+4)
    assert v(h+8)==0x401c1 and [v(md+16+k*4) for k in range(4)]==[n]*4
    ptr=[v(md+4*k) for k in range(4)]
    lines.append(f'static const float PENDING_CAMERA[{n}][6]={{')
    for k in range(n):
        assert v(ptr[0]+16*k)==v(ptr[1]+16*k)==k
        values=struct.unpack_from('<3f',dat,ptr[0]-B+k*16+4)+struct.unpack_from('<3f',dat,ptr[1]-B+k*16+4)
        lines.append('    {'+','.join(x.hex()+'f' for x in values)+'},')
    lines.append('};')
    for name,index in [('ROLL',2),('FOV',3)]:
        values=[]
        for k in range(n):
            assert v(ptr[index]+8*k)==k
            values.append(struct.unpack_from('<i',dat,ptr[index]-B+8*k+4)[0])
        lines.append(f'static const int PENDING_{name}[{n}]={{'+','.join(map(str,values))+'};')
    texts=['']*13
    for c,off in enumerate((0x8c,0xa4,0x90,0x9c,0x98,0xa0,0x94,0x94,0x94,0x94),3):
        p=v(v(B+0x34c)+off)-B;texts[c]=dat[p:dat.index(0,p)].decode('ascii')
    lines.append('static const char *const PENDING_TEXT[13]={'+','.join(json.dumps(s)for s in texts)+'};')
    # 04c364/370/3c4/3dc: question, highlighted YES/NO, button legend.
    cells=[u(a)for a in (0x8c04c364,0x8c04c374,0x8c04c3c8,0x8c04c3dc)]+[B+0x418]
    quads=[];ranges=[]
    for cell in cells:
        h=v(cell);ep,count=v(h),v(h+4);start=len(quads)
        for k in range(count):
            slot,flags,x,y,w,ht,rot,z,pu,pv,*tail=struct.unpack_from('<HH4fhh2f4I4f',dat,ep-B+k*64)
            assert rot==0 and len(set(tail[:4]))==1 and flags in (0x2580,0x2590)
            u0,v0,u1,v1=tail[4:]
            if flags&0x10:u0,u1=u1,u0
            quads.append((slot,tail[0],x,y,w,ht,u0,v0,u1,v1))
        ranges.append((start,len(quads)-start))
    lines.append('static const GarageQuad PENDING_QUADS[]={')
    for slot,color,*values in quads:
        lines.append('    {'+f'{slot},0x{color:08x}u,'+','.join(x.hex()+'f' for x in values)+'},')
    lines+=['};','static const unsigned PENDING_LISTS[][2]={'+','.join('{%d,%d}'%r for r in ranges)+'};','']
    return '\n'.join(rows)+'\n','\n'.join(lines)

if __name__=='__main__':
    spec,inc=extract()
    if '--check' in sys.argv:
        assert (DEC/'garage_pending_spec.txt').read_text()==spec
        assert (ROOT/'psp/src/garage_pending.inc').read_text()==inc
        print('Original invitation cast, camera and UI verified')
    elif '--write' in sys.argv:
        (DEC/'garage_pending_spec.txt').write_text(spec)
        (ROOT/'psp/src/garage_pending.inc').write_text(inc)
    elif '--bake' in sys.argv:
        subprocess.run([sys.executable,'tools/jgc_convert.py','../psp/garage_pending.jgc',
                        '--spec=garage_pending_spec.txt','--source-material=1','--source-environment=1','--texmax=256'],cwd=DEC,check=True)
    else:print(inc)
