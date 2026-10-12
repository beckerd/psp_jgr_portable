#!/usr/bin/env python3
"""Mission-set +1c attached graffiti, not wall spots or invented tag quads.

GFO2: <magic,count>, then count records <8I,228 raw descriptor bytes>,
followed by nbones * JgcBone (48 bytes), then nvertex * <2f,I,3f,I>.
Header words: bank, chain, index, source descriptor address, type, nvertex,
strip flags, nbones. Vertex colour is
the ORIGINAL reveal-graph word, not an RGBA colour (0c5128 consumes it).
Vertices retain their original local position and cache-owner bone. 0b8b1c
animates this tree with the owner's motion channels; baking its bind pose
detaches the tag from moving limbs. Every +50 chained set retains its own
+1c index namespace. Descriptor TRS is NOT prepended by the owner draw call.
"""
import argparse
from pathlib import Path
import struct
from ninjalib import read_object, parse_vlist, parse_plist

ROOT=Path(__file__).resolve().parents[2]
BASE=0x8cde6000

def entries(data):
    # Old JMSb placeholder banks are not JMSc encounter definitions.
    if data[:4]==b'JMSb':return
    def word(a):
        if not BASE<=a<=BASE+len(data)-4:raise ValueError(f'bad pointer {a:08x}')
        return struct.unpack_from('<I',data,a-BASE)[0]
    seen=set();s=BASE;chain=0
    while s:
        if s in seen or data[s-BASE:s-BASE+4]!=b'JMSc':raise ValueError('invalid JMSc chain')
        seen.add(s);table=word(s+0x1c);index=0
        while table and word(table):
            a=word(table)
            if a+0x104>BASE+len(data):raise ValueError('truncated descriptor')
            # All supplied attached templates have zero anchors/subvolumes.
            # Reject a new variant instead of silently dropping its data.
            assert word(a) in (0,1) and word(a+0xf0)==word(a+0xf8)==0
            yield chain,index,a
            table+=4;index+=1
        s=word(s+0x50);chain+=1

def mesh(data,a,base=BASE,materials=None):
    # Final-stage descriptors live in STAGELAST, not the JMSc overlay.
    BASE=base
    w=lambda p:struct.unpack_from('<I',data,p-BASE)[0]
    vertices=[];bones=[];flags=set();visited=set();cache={}
    def walk(node,graph,parent):
        if not node:return
        if node in visited:raise ValueError('cyclic mesh')
        visited.add(node);n=read_object(data,node,BASE)
        bone=len(bones)
        bones.append((parent,n['ev'],*n['pos'],*n['ang'],*n['scl'],0,0))
        if n['model']:
            vp,pp=struct.unpack_from('<II',data,n['model']-BASE)
            assert w(vp)&0x3f in (0x23,0x2a)
            v=parse_vlist(data,vp-BASE);first=struct.unpack_from('<H',data,vp-BASE+4)[0]
            reveal=w(graph)
            # CNK vertex-cache slots survive the next node's vlist. Its
            # triangles can reference vertices loaded by a previous node.
            # Keep THAT node's owner, not the current triangle's node.
            for idx,(pos,normal,colour) in v.items():
                cache[idx]=(w(reveal+4*(idx-first)),pos,bone)
            for texture,fl,colour,tri in parse_plist(data,pp-BASE,source_material=True):
                if materials is None:assert texture==0 and colour==0xffffffff
                else:materials.append((texture,fl,colour))
                flags.add(fl)
                for idx,uv in tri:vertices.append((*uv,cache[idx][0],*cache[idx][1],cache[idx][2]))
        walk(n['child'],w(graph+4),bone)
        walk(n['sib'],w(graph+8),parent)
    walk(w(a+0xfc),w(a+0x100),-1)
    if materials is None:assert len(flags)==1
    return min(flags),bones,vertices

def convert():
    rows=[]
    for bank in range(64):
        data=(ROOT/f'decompile/afs/MISSIONSET/{bank:03d}_mission_set_{bank:02d}.bin').read_bytes()
        for chain,index,a in entries(data):
            flags,bones,verts=mesh(data,a);kind=struct.unpack_from('<I',data,a-BASE)[0]
            row=struct.pack('<8I',bank,chain,index,a,kind,len(verts),flags,len(bones))+data[a-BASE+4:a-BASE+0xe8]
            row+=b''.join(struct.pack('<hH3f3i3f2I',*b) for b in bones)
            row+=b''.join(struct.pack('<2fI3fI',*v) for v in verts)
            rows.append(row)
    return struct.pack('<4sI',b'GFO2',len(rows))+b''.join(rows)

def skater_banks():
    """019704: PLAY +74, in a reserved portable namespace (63,7,char).

    One small file per skater allows encounters to retain only their owners'
    templates. Tutorial/traffic missions pay no extra resident bank cost.
    """
    rows={}
    for path in sorted((ROOT/'decompile/afs/PLAYER').glob('*.bin')):
        data=path.read_bytes()
        if data[:4]!=b'PLAY':continue
        w=lambda p:struct.unpack_from('<I',data,p)[0]
        table=w(8);character=w(table);a=w(table+0x74)
        assert character not in rows and w(a)==0 and w(a+0xf0)==w(a+0xf8)==0
        flags,bones,verts=mesh(data,a,base=0)
        row=struct.pack('<8I',63,7,character,a,0,len(verts),flags,len(bones))+data[a+4:a+0xe8]
        row+=b''.join(struct.pack('<hH3f3i3f2I',*b) for b in bones)
        row+=b''.join(struct.pack('<2fI3fI',*v) for v in verts)
        rows[character]=struct.pack('<4sI',b'GFO2',1)+row
    assert set(rows)==set(range(16))
    return rows

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('output',type=Path);ap.add_argument('--check',action='store_true');ap.add_argument('--skaters',action='store_true');args=ap.parse_args()
    if args.skaters:
        for character,data in skater_banks().items():
            path=args.output/f'skatergraffiti{character:02d}.bin'
            if args.check:assert path.read_bytes()==data
            else:path.write_bytes(data)
        print('16 original PLAY back-tag templates');raise SystemExit(0)
    data=convert()
    if args.check:assert args.output.read_bytes()==data
    else:args.output.write_bytes(data)
    print(f'{struct.unpack_from("<I",data,4)[0]} original attached graffiti entries, {len(data)} bytes')
