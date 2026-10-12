#!/usr/bin/env python3
"""Bake observed original title choreography; never reconstruct its physics.

Keeps original 30 Hz world/camera/pose samples. Constant local pose channels
are shared; BAM angles retain their original 16-bit values. No texture loss.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import tempfile
import sys

ROOT=Path(__file__).resolve().parents[2]
MODELS=[4,6,3,2,1,7,10,0,15,8,13,11,12,5,9,14]

def samples(source):
    data=(source/'title_actor_camera.bin').read_bytes()
    assert data[:16]==struct.pack('<4s3I',b'TAC2',0x900,0x290,0xd0)
    assert (len(data)-16)%4192==0
    with (source/'title.csv').open() as file:rows=list(csv.DictReader(file))
    assert len(rows)==(len(data)-16)//4192
    chars={int(r['character']) for r in rows};assert len(chars)==1
    character=chars.pop();assert 0<=character<16
    bank=(ROOT/'psp/chars.jgc').read_bytes()
    char_off,bone_off=struct.unpack_from('<2I',bank,32)
    bone0,nb=struct.unpack_from('<2I',bank,char_off+MODELS[character]*48);assert nb==20
    bind=[]
    for i in range(nb):
        off=bone_off+(bone0+i)*48
        bind.append((*struct.unpack_from('<3f3i3f',bank,off+4),struct.unpack_from('<H',bank,off+2)[0]))
    frames=[];part=0;last_tick=0;last_camera=None
    for i,row in enumerate(rows):
        b=data[16+i*4192:16+(i+1)*4192]
        u=lambda off:struct.unpack_from('<I',b,off)[0]
        f=lambda off:struct.unpack_from('<f',b,off)[0]
        tick=u(0x98)
        if tick<last_tick:part+=1
        last_tick=tick
        pointer=u(0x594);pose=b[3168:]
        address=struct.unpack_from('<I',pose,4)[0]-pointer
        joints=[]
        for j in range(nb):
            flags,pos,ang,scale=struct.unpack_from('<4I',pose,address+j*16)
            local=list(bind[j])
            for flag,p,count,fmt,start in [(1,pos,12,'3f',0),(16,ang,6,'3h',3),(4,scale,12,'3f',6)]:
                if flags&flag:
                    at=p-pointer;assert 0<=at<=len(pose)-count,(i,j,flags,hex(p),hex(pointer))
                    local[start:start+3]=struct.unpack_from('<'+fmt,pose,at)
                    local[9]&=~({1:1,16:2,4:4}[flag])
            joints.append(local)
        camera=u(0xb74)
        cut=(part>0 and tick==1) or (last_camera is not None and camera!=last_camera)
        last_camera=camera
        # Raw original visual orientation, not travel yaw. Camera +190 is
        # the final record actually submitted after the authored override.
        header=struct.pack('<3f3hH6fhh2fIHH',*[f(0xb8+4*j)for j in range(3)],
            *[((u(0x678+4*j)+32768)%65536)-32768 for j in range(3)],u(0x70),
            *[f(0xa90+4*j)for j in range(6)],
            ((u(0xaa8)+32768)%65536)-32768,((u(0xaac)+32768)%65536)-32768,
            -f(0xab0),-f(0xab4),tick,part,int(cut))
        assert len(header)==64
        frames.append((header,joints))
    return character,frames

def effect_samples(source):
    data=(source/'title_actor_camera.bin').read_bytes()
    result=[]
    for at in range(16,len(data),4192):
        b=data[at:at+0x900]
        u=lambda o:struct.unpack_from('<I',b,o)[0]
        f=lambda o:struct.unpack_from('<f',b,o)[0]
        # Complete source animation flags and exact collision attachment points.
        result.append(struct.pack('<7i41f3i',u(0x5cc),u(0x78),u(0x1dc),u(0x1e4),u(0x108),u(0x10c),u(0x548),
            f(0x5ac),f(0xdc),*[f(0x820+k*4) for k in range(3)],
            *[f(0x684+j*12+k*4) for j in [*range(8),19,20,21,22] for k in range(3)],
            *[struct.unpack_from('<i',b,o)[0] for o in (0x16c,0x168,0x170)]))
    return result

def bake(source):
    character,frames=samples(source);assert frames
    # Share constant position/scale components, including their exact bits.
    channels=[];base=bytearray()
    for j in range(20):
        for k in [0,1,2,6,7,8]:
            values=[struct.pack('<f',p[j][k]) for _,p in frames]
            if any(v!=values[0] for v in values[1:]):channels.append(j*6+([0,1,2,6,7,8].index(k)))
            base+=values[0]
    stride=428+len(channels)*4
    out=bytearray(struct.pack('<4s7I',b'TRT2',len(frames),character,20,stride,len(channels),0,0))
    out+=base+struct.pack('<'+'I'*len(channels),*channels)
    for (header,pose),effects in zip(frames,effect_samples(source)):
        out+=header
        for p in pose:out+=struct.pack('<3hH',*[(int(v)+32768)%65536-32768 for v in p[3:6]],p[9])
        for channel in channels:
            j,k=divmod(channel,6);out+=struct.pack('<f',pose[j][[0,1,2,6,7,8][k]])
        out+=effects
    return bytes(out),dict(character=character,model=MODELS[character],frames=len(frames),
        seconds=len(frames)/30,stride=stride,varying_float_channels=len(channels),bytes=len(out),
        source_sha256=hashlib.sha256((source/'title_actor_camera.bin').read_bytes()).hexdigest())

def ui_assets():
    with tempfile.TemporaryDirectory(prefix='jgr-title-ui-') as temp:
        folder=Path(temp)
        subprocess.run([sys.executable,str(ROOT/'decompile/tools/jgs_tex2d.py'),
            str(ROOT/'decompile/afs/U_SEGALOGO/002_logo_Stxr.bin'),str(folder/'title_ui.bin')],
            check=True,stdout=subprocess.DEVNULL)
        subprocess.run([sys.executable,str(ROOT/'decompile/tools/jgs_2dlist.py'),
            str(ROOT/'decompile/afs/U_TITLE/000_titledat.bin'),'8cf00000',str(folder/'title_lists.bin'),
            '--table=40,6'],check=True,stdout=subprocess.DEVNULL)
        return {p.name:p.read_bytes() for p in folder.iterdir()}

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source',type=Path);ap.add_argument('--out',type=Path,default=ROOT/'psp/title_route.bin')
    ap.add_argument('--check',action='store_true')
    args=ap.parse_args();data,report=bake(args.source)
    if args.check:assert args.out.read_bytes()==data
    else:args.out.write_bytes(data)
    for name,content in ui_assets().items():
        target=args.out.parent/name
        if args.check:assert target.read_bytes()==content,name
        else:target.write_bytes(content)
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
