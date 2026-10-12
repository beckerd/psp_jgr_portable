#!/usr/bin/env python3
"""Export original opcode-9 stage objects and sparse Ninja motion channels.

PTH1 contains source table identities plus compact, relocated motion/collider data.
Key payloads are copied byte-for-byte, never resampled to CNK s16 angles.
Models use the original stage-specific near/far root and texture-list tables.
Collision descriptor identities, vertices and patch flags are retained; this export does not pretend
that registering a model also implements 0b6a28/0b6f88/slip-volume contacts.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2];DEC=ROOT/'decompile'
sys.path.insert(0,str(DEC/'tools'))
from ninjalib import read_object
BASE=0x8cb00000;EXEBASE=0x8c010000;PACKBASE=0x10000000
# count, paths, near, far, texlist, paths, colliderA, colliderB, slip, sound,
# special-pulse hierarchy. These are callback arguments, not heuristics.
SPECS={
 1:(9,8,0x8c107744,0x8c107768,0x8c1077d4,0x8c107840,0x8c10778c,0x8c1077b0,0x8c10781c,0x8c1077f8,0),
 2:(2,15,0x8c10a540,0x8c10a540,0x8c10a550,0x8c10a558,0,0x8c10a548,0,0,0),
 3:(12,4,0x8c1bc6d8,0x8c1bc708,0x8c1c73f8,0x8c1bc744,0x8c1bc754,0x8c1bc784,0x8c1bc7b4,0x8c1bc738,1),
 5:(16,2,0x8c204150,0x8c204190,0x8c20a788,0x8c2041e0,0x8c2041e8,0x8c204228,0x8c204268,0x8c2041d0,1),
 6:(16,16,0x8c20b664,0x8c20b6a4,0x8c219ea0,0x8c20b6f4,0x8c20b734,0x8c20b774,0x8c20b7b4,0x8c20b6e4,1),
}

def source(stage):
    exe=(DEC/'extracted/1ST_READ.BIN').read_bytes()
    data=(DEC/f'afs/STAGE{stage}/000_st{stage}dat.bin').read_bytes()
    def ew(a):return struct.unpack_from('<I',exe,a-EXEBASE)[0] if a else 0
    def u(a):
        if not BASE<=a<=BASE+len(data)-4:raise ValueError(f'outside stage {stage}: {a:08x}')
        return struct.unpack_from('<I',data,a-BASE)[0]
    def tree(a):
        todo=[a] if a else [];out=[]
        while todo:
            a=todo.pop();assert a not in out,'cyclic model';out.append(a)
            n=read_object(data,a,BASE);todo.extend(p for p in (n['sib'],n['child']) if p)
        assert len(out)<=64
        return out
    nt,np,hi,lo,tx,paths,ca,cb,sl,se,special=SPECS[stage]
    tm=json.loads((DEC/('stage1_texmap_static.json' if stage==1 else f'stage{stage}_texmap.json')).read_text())
    types=[]
    for i in range(nt):
        roots=[ew(hi+4*i),ew(lo+4*i)];nodes=[tree(r) for r in roots]
        tl=ew(tx+4*i);mapping=tm['texlists'].get(str(tl),[])
        if any(roots):assert mapping and min(mapping)>=0,(stage,i,hex(tl))
        pulse=[]
        if special and roots[0]:
            children=[];a=u(roots[0]+44)
            while a:children.append(a);a=u(a+48)
            if len(children)>5:pulse=[nodes[0].index(a) for a in children[4:6]]
        sound=1 if stage==2 else ew(se+i*4) if stage==1 else struct.unpack_from('<b',exe,se-EXEBASE+i)[0]
        types.append(dict(type=i,roots=roots,nodes=nodes,texlist=tl,texture_map=mapping,
            collision=[ew(ca+4*i) if ca else 0,ew(cb+4*i) if cb else 0],
            slip=ew(sl+4*i) if sl else 0,sound=sound,pulse_bones=pulse))
    maxnodes=max(len(n) for t in types for n in t['nodes']);tracks=[]
    for i in range(np):
        a=ew(paths+4*i)
        if not a:
            tracks.append(dict(address=0,frames=0,kind=0,keys=[]));continue
        md,frames,kind=[u(a+k*4) for k in range(3)];channels=kind>>16
        assert kind in (0x20003,0x30007) and 0<frames<10000
        # MDATA contains the full near-model hierarchy. Far models traverse
        # its prefix, exactly like the original 0bfb24 call.
        keys=[]
        for bone in range(maxnodes):
            row=[]
            for ch in range(channels):
                at=md+bone*channels*8;ptr,n=u(at+4*ch),u(at+channels*4+ch*4)
                assert not ptr or 0<n<=frames,(stage,i,bone,ch,n)
                payload=data[ptr-BASE:ptr-BASE+n*16] if ptr else b''
                assert len(payload)==(n*16 if ptr else 0)
                if ptr:
                    times=[struct.unpack_from('<I',payload,k*16)[0] for k in range(n)]
                    assert times[0]==0 and times[-1]<frames and all(a<b for a,b in zip(times,times[1:]))
                row.append((ptr,n,payload))
            keys.append(row)
        tracks.append(dict(address=a,frames=frames,kind=kind,keys=keys))
    return data,tm,types,tracks

def export(stage,directory):
    data,tm,types,tracks=source(stage);directory=Path(directory);directory.mkdir(parents=True,exist_ok=True)
    image=bytearray(16);cache={}
    def append(blob):
        if blob in cache:return cache[blob]
        image.extend(bytes(-len(image)%4))
        p=PACKBASE+len(image);image.extend(blob);cache[blob]=p;return p
    def relocate_collision(a):
        if not a:return 0
        node=bytearray(data[a-BASE:a-BASE+68]);assert len(node)==68
        kind,flags,mesh=struct.unpack_from('<HHI',node)
        assert not any(struct.unpack_from('<2I',node,60)), 'unexpected collider hierarchy'
        assert struct.unpack_from('<I',node,8)[0]==0, 'unexpected collision attachment callback'
        if kind==4:
            vertices=data[mesh-BASE:mesh-BASE+96];assert len(vertices)==96
            target=append(vertices)
        else:
            assert kind==6
            verts,patches,nv,np=struct.unpack_from('<IIHH',data,mesh-BASE)
            target_vertices=append(data[verts-BASE:verts-BASE+nv*12]);assert nv>0
            target_patches=bytearray()
            for j in range(np):
                patch=bytearray(data[patches-BASE+j*36:patches-BASE+(j+1)*36]);assert len(patch)==36
                ids,count=struct.unpack_from('<2I',patch,24);nindices=(count&65535)+2
                payload=data[ids-BASE:ids-BASE+nindices*2]
                assert len(payload)==nindices*2
                assert all(0<=v<nv for v in struct.unpack('<%dh'%nindices,payload))
                struct.pack_into('<I',patch,24,append(payload));target_patches.extend(patch)
            target=append(struct.pack('<IIHH',target_vertices,append(bytes(target_patches)),nv,np))
        struct.pack_into('<I',node,4,target)
        return append(bytes(node))
    for t in types:
        t['packed_collision']=[relocate_collision(a) for a in t['collision']]
        t['packed_collision_directory']=append(struct.pack('<2I',*t['packed_collision']))
    paths=[]
    for t in tracks:
        if not t['address']:
            paths.append((0,0,0,0));continue
        md=bytearray()
        for row in t['keys']:
            md.extend(struct.pack('<%dI'%(len(row)*2),
                *[append(payload) if ptr else 0 for ptr,n,payload in row],*[n for ptr,n,payload in row]))
        p=append(struct.pack('<3I',append(bytes(md)),t['frames'],t['kind']))
        paths.append((t['address'],p,len(t['keys']),t['frames']))
    with tempfile.TemporaryDirectory(prefix='jgr-traffic-export-') as temp:
        txr=Path(temp)/'textures.txp';txr.write_bytes(b''.join((DEC/p).read_bytes() for p in tm['packs']))
        args=[sys.executable,str(DEC/'tools/jgc_convert.py'),str(directory/f'traffic{stage}.jgc'),'--motions=0','--source-material=1','--source-environment=1']
        models=[]
        for t in types:
            t['models']=[]
            for lod,root in enumerate(t['roots']):
                if not root:t['models'].append(0xffffffff);continue
                t['models'].append(len(models));models.append((t['type'],lod))
                args.append(f"traffic_{t['type']}_{lod}:afs/STAGE{stage}/000_st{stage}dat.bin:{txr}:0,0,0,0:rawat:8cb00000:{root:x}:0:texmap="+','.join(map(str,t['texture_map'])))
        subprocess.run(args,cwd=DEC,check=True)
    # Header + 48-byte type records + 16-byte track records + relocated image.
    off_types=32;off_paths=off_types+len(types)*48;off_image=off_paths+len(paths)*16
    out=bytearray(struct.pack('<4s7I',b'PTH1',stage,len(types),len(paths),off_types,off_paths,off_image,len(image)))
    for t in types:
        pulse=t['pulse_bones'] or [0xffffffff,0xffffffff]
        out.extend(struct.pack('<12I',*t['roots'],t['texlist'],*t['collision'],t['slip'],t['sound']&0xffffffff,
            *pulse,*t['models'],t['packed_collision_directory']))
    for p in paths:out.extend(struct.pack('<4I',*p))
    out.extend(image);(directory/f'traffic{stage}.bin').write_bytes(out)
    info=dict(stage=stage,source_sha256=hashlib.sha256(data).hexdigest(),types=types,
        tracks=[dict(address=t['address'],frames=t['frames'],kind=t['kind'],bones=len(t['keys']),
            key_sha256=[[hashlib.sha256(blob).hexdigest() for ptr,n,blob in row] for row in t['keys']]) for t in tracks],
        bytes=len(out),remaining=['source dynamic collider registration/slip responses','matched live draw/LOD/pulse/light validation'])
    (directory/f'traffic{stage}.json').write_text(json.dumps(info,indent=2)+'\n')
    print(f'Stage {stage}: {len(types)} types, {len(paths)} paths, {len(out):,} motion/table bytes')
    return info

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--stages',type=int,nargs='+',default=list(SPECS));ap.add_argument('--out',type=Path,default=ROOT/'psp');args=ap.parse_args()
    for stage in args.stages:export(stage,args.out)
if __name__=='__main__':main()
