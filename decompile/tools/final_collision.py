#!/usr/bin/env python3
"""Source-bound STAGELAST animated type-6 collision (0e9210 / 0ed318).

FCL1 keeps local vertices, patch winding/masks, the original collider hierarchy,
and its 500-frame s16 rotation channels. It does NOT bake a static floor or
derive collision from the visible mesh. Original traversal: 0be56c -> 0be878.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

DEC=Path(__file__).resolve().parents[1]
BASE=0x8cb00000
NODE=struct.Struct('<IiiIIII3f3i3fI')

def build(boss=False):
    exe=(DEC/'extracted/1ST_READ.BIN').read_bytes()
    data=(DEC/'afs/STAGELAST/001_lastdat.bin').read_bytes()
    def read(fmt,a):
        n=struct.calcsize(fmt)
        assert BASE<=a<=BASE+len(data)-n,hex(a)
        return struct.unpack_from(fmt,data,a-BASE)
    def word(a):return read('<I',a)[0]
    def ew(a):return struct.unpack_from('<I',exe,a-0x8c010000)[0]
    nodes=[];vertices=[];triangles=[];keys=bytearray();pieces=[]
    frames=719 if boss else 500
    for piece in range(2 if boss else 6):
        if boss:root,motion=ew(0x8c1d0ed0 if piece==0 else 0x8c1d0ee4),ew(0x8c1d0ecc) if piece==0 else 0
        else:root,motion=ew(0x8c1d0dec+piece*4),ew(0x8c1d0e04+piece*4)
        if motion:
            header,md=read('<II',motion);assert header>>16==frames and header&65535 in (0x11,0x15)
        else:md=0
        first=len(nodes);seen=set()
        def walk(node,parent):
            while node:
                assert node not in seen and len(seen)<128;seen.add(node)
                kind,flags,mesh=read('<HHI',node);assert kind in (0,6)
                index=len(nodes);local=index-first
                channels=read('<4I',md+local*16) if md else (0,0,0,0)
                assert channels[0] in (0,0x10) and channels[1]==channels[3]==0
                pos=read('<3f',node+12);ang=read('<3i',node+24);scale=read('<3f',node+36)
                v0,t0=len(vertices),len(triangles)
                if kind==6:
                    vb,pb,nv,np=read('<IIHH',mesh)
                    vertices.extend(read('<3f',vb+i*12) for i in range(nv))
                    for p in range(np):
                        patch=pb+p*36;ip,fl,mask,surf=read('<IIHH',patch+24)
                        n=fl&65535;ids=read('<'+'h'*(n+2),ip)
                        assert all(0<=j<nv for j in ids)
                        wind=bool(fl&0x40000000)
                        for k in range(n):
                            order=(ids[k],ids[k+1],ids[k+2]) if wind else (ids[k],ids[k+2],ids[k+1])
                            # Keep canonical geometry for body consumers, but
                            # retain the original sliding strip order for the
                            # probe plane and moving-contact interpolation.
                            patch_flags=int(bool(fl&0x80000000)) | (0 if wind else 2)
                            triangles.append((*[v0+j for j in order],surf,mask,patch_flags,index))
                            wind=not wind
                key=-1
                if channels[2]:
                    assert channels[0]==0x10
                    key=len(keys);keys.extend(data[channels[2]-BASE:channels[2]-BASE+frames*6])
                    assert len(keys)==key+frames*6
                nodes.append((node,piece,parent,v0,len(vertices)-v0,t0,len(triangles)-t0,*pos,*ang,*scale,key))
                walk(word(node+60),index);node=word(node+64)
        walk(root,-1)
        pieces.append(dict(piece=piece,root=root,motion=motion,nodes=len(nodes)-first))
    on=40;ov=on+len(nodes)*NODE.size;ot=ov+len(vertices)*12;ok=ot+len(triangles)*20
    out=bytearray(struct.pack('<4s9I',b'FCL1',len(nodes),len(vertices),len(triangles),frames,on,ov,ot,ok,ok+len(keys)))
    for row in nodes:
        row=(*row[:-1],ok+row[-1] if row[-1]>=0 else 0)
        out.extend(NODE.pack(*row))
    for row in vertices:out.extend(struct.pack('<3f',*row))
    for row in triangles:out.extend(struct.pack('<3I4H',*row))
    out.extend(keys)
    manifest=dict(source_sha256=hashlib.sha256(data).hexdigest(),pieces=pieces,
                  nodes=len(nodes),vertices=len(vertices),triangles=len(triangles),
                  key_bytes=len(keys),frames=frames,sha256=hashlib.sha256(out).hexdigest())
    return bytes(out),manifest

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--out',type=Path,default=DEC.parent/'psp/finalcoll.bin')
    ap.add_argument('--boss',action='store_true',help='0ec72c satellite and alternate final collider (use --out to select a separate file)')
    ap.add_argument('--collapse',action='store_true',help='0e9368 channel overrides from original table 1d0e2c')
    ap.add_argument('--check',action='store_true');args=ap.parse_args()
    assert not(args.boss and args.collapse)
    if args.boss and args.out==DEC.parent/'psp/finalcoll.bin':args.out=DEC.parent/'psp/finalbosscoll.bin'
    if args.collapse and args.out==DEC.parent/'psp/finalcoll.bin':args.out=DEC.parent/'psp/finalcollapse.bin'
    data,manifest=build_collapse() if args.collapse else build(args.boss)
    if args.check:
        assert args.out.read_bytes()==data
        assert json.loads(args.out.with_suffix('.json').read_text())==manifest
    else:
        args.out.write_bytes(data);args.out.with_suffix('.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(manifest))

def build_collapse():
    """FCO1 sparse per-node TRS overrides, in the original collider preorder.
    0baf64 prefers each second-motion channel independently; a missing
    collapse rotation must retain the separately advancing 500-frame clip.
    """
    exe=(DEC/'extracted/1ST_READ.BIN').read_bytes()
    data=(DEC/'afs/STAGELAST/001_lastdat.bin').read_bytes()
    primary,info=build();rows=[];keys=bytearray();pieces=[]
    for piece in range(4):
        motion=struct.unpack_from('<I',exe,0x8c1d0e2c+piece*4-0x8c010000)[0]
        header,md=struct.unpack_from('<II',data,motion-BASE)
        assert header>>16==640 and header&65535 in (0x11,0x15)
        nodes=[NODE.unpack_from(primary,40+i*NODE.size) for i in range(info['nodes'])]
        nodes=[n for n in nodes if n[1]==piece]
        # 0baf64 traverses the parallel NJS hierarchy at 1d0e3c. Its
        # parent/preorder must agree with the type-6 collider at 1d0dec;
        # base TRS values need not agree and are NOT substituted here.
        proxy=struct.unpack_from('<I',exe,0x8c1d0e3c+piece*4-0x8c010000)[0]
        parents=[]
        def proxy_walk(a,parent):
            while a:
                index=len(parents);parents.append(parent)
                child,sibling=struct.unpack_from('<II',data,a-BASE+44)
                proxy_walk(child,index);a=sibling
        proxy_walk(proxy,-1)
        first=sum(p['nodes'] for p in pieces)
        assert parents==[n[2]-first if n[2]>=0 else -1 for n in nodes]
        for i,node in enumerate(nodes):
            flags,*pointers=struct.unpack_from('<4I',data,md-BASE+i*16)
            assert flags&~0x15==0
            offsets=[]
            for ptr,stride,bit in zip(pointers,(12,6,12),(1,0x10,4)):
                assert bool(flags&bit)==bool(ptr)
                if ptr:
                    assert BASE<=ptr<=BASE+len(data)-640*stride
                    offsets.append(len(keys));keys.extend(data[ptr-BASE:ptr-BASE+640*stride])
                else:offsets.append(-1)
            rows.append((node[0],*offsets))
        pieces.append(dict(piece=piece,motion=motion,frames=640,nodes=len(nodes)))
    key_offset=24+len(rows)*16
    out=bytearray(struct.pack('<4s5I',b'FCO1',len(rows),640,24,key_offset,key_offset+len(keys)))
    for row in rows:out.extend(struct.pack('<4I',row[0],*[key_offset+k if k>=0 else 0 for k in row[1:]]))
    out.extend(keys)
    return bytes(out),dict(source_sha256=info['source_sha256'],pieces=pieces,nodes=len(rows),key_bytes=len(keys),sha256=hashlib.sha256(out).hexdigest())
if __name__=='__main__':main()
