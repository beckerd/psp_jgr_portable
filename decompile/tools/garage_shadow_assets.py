#!/usr/bin/env python3
"""Retain original garage type-58 modifier geometry and its separate hierarchy.

Animation channels are borrowed from the primary resident bank at runtime.
No new shadow art, texture, projected polygon or duplicated motion bank.
"""
import argparse
from pathlib import Path
import struct
ROOT=Path(__file__).resolve().parents[2];DEC=ROOT/'decompile';BASE=0x8c800000
def export():
    exe=(DEC/'extracted/1ST_READ.BIN').read_bytes()
    dat=(DEC/'afs/U_GARAGE/001_garagedat.bin').read_bytes()
    def u(a):return struct.unpack_from('<I',dat if a>=BASE else exe,a-(BASE if a>=BASE else 0x8c010000))[0]
    def tree(a,parent=-1,result=None):
        if result is None:result=[]
        while a:
            ev,mesh=struct.unpack_from('<2I',dat,a-BASE)
            pos=struct.unpack_from('<3f3i3f',dat,a-BASE+8)
            child,sibling=struct.unpack_from('<2I',dat,a-BASE+44)
            own=len(result);result.append((parent,ev,pos,mesh))
            if child:tree(child,own,result)
            a=sibling
        return result
    residents=[];nodes=[];vertices=[];indices=[]
    for slot in range(15):
        root=u(u(0x8c105c1c+slot*4));primary=u(u(0x8c105ba4+slot*4))
        shadow=tree(root);body=tree(primary)
        if shadow:
            assert len(shadow)==len(body)<=64
            assert [x[0] for x in shadow]==[x[0] for x in body]
        residents.append((len(nodes),len(shadow)))
        for parent,ev,trs,mesh in shadow:
            v0,i0=len(vertices),len(indices);nv=ns=0
            if mesh:
                vl,pl=struct.unpack_from('<2I',dat,mesh-BASE)
                kind,size,offset,nv=struct.unpack_from('<4H',dat,vl-BASE)
                assert kind==34 and offset==0 and 0<nv<=32 and size==1+3*nv
                vertices.extend(struct.unpack_from('<3f',dat,vl-BASE+8+12*j) for j in range(nv))
                kind,size,ns=struct.unpack_from('<3H',dat,pl-BASE)
                assert kind==58 and 0<ns<=64
                p=pl-BASE+6
                for _ in range(ns):
                    count=struct.unpack_from('<h',dat,p)[0];p+=2
                    assert abs(count)>=3
                    vi=struct.unpack_from('<'+'H'*abs(count),dat,p)
                    assert max(vi)<nv
                    indices.append(count);indices.extend(vi);p+=2*abs(count)
                end=pl-BASE+4+size*2
                assert p<=end and dat[p:end]==bytes(end-p), (hex(mesh),p,end)
            nodes.append((parent,ev,*trs,v0,nv,i0,len(indices)-i0,ns))
    off_nodes=32+15*8;off_vertices=off_nodes+len(nodes)*60
    off_indices=off_vertices+len(vertices)*12;total=off_indices+len(indices)*2
    header=struct.pack('<4s7I',b'GSH1',len(nodes),len(vertices),len(indices),off_nodes,off_vertices,off_indices,total)
    result=header+b''.join(struct.pack('<2I',*r) for r in residents)
    result+=b''.join(struct.pack('<hH3f3i3f5I',*n) for n in nodes)
    result+=b''.join(struct.pack('<3f',*v) for v in vertices)
    result+=struct.pack('<'+'h'*len(indices),*indices)
    assert len(result)==total
    return result
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    data=export();path=ROOT/'psp/garageshadow.bin'
    if args.check:assert path.read_bytes()==data
    else:path.write_bytes(data)
    print(f'Original garage modifier hierarchies/strips: {len(data):,} bytes')
if __name__=='__main__':main()
