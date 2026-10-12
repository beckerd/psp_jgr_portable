#!/usr/bin/env python3
"""Losslessly share identical JGC animation-channel tables.

No models, textures, keys, frame counts or animation channels are removed.
Only byte-identical channel tables share storage; pointer fields are rebased.
This addresses PSP RAM residency, not original-game behavior.
"""
import argparse
from pathlib import Path
import struct

def compact(data):
    data=bytes(data)
    if len(data)<68 or data[:4] not in (b'JGC1',b'JGC2',b'JGC3'):raise ValueError('JGC header')
    h=list(struct.unpack_from('<16I',data,4))
    def span(off,n,stride):
        if off>len(data) or n>(len(data)-off)//stride:raise ValueError('JGC span')
    nc,nb,_,_,_,nt,nm=h[:7]
    oc,ob,_,_,_,ot,om,ok,op=h[7:]
    if not 68<=oc<=ob<=h[9]<=h[10]<=h[11]<=ot<=om<=ok<=op<=len(data):raise ValueError('JGC section order')
    strides=(48,48,16,{b'JGC1':24,b'JGC2':28,b'JGC3':40}[data[:4]],2,16,8)
    for i,stride in enumerate(strides):
        if h[i]>(h[8+i]-h[7+i])//stride:raise ValueError('JGC overlapping sections')
    span(oc,nc,48);span(ob,nb,48);span(ot,nt,16);span(om,nm,8)
    owners=[None]*nm
    for i in range(nc):
        b,n,m,count=struct.unpack_from('<4I',data,oc+i*48)
        if b>nb or n>nb-b or m>nm or count>nm-m:raise ValueError('JGC model span')
        for j in range(m,m+count):
            # JCL1 far meshes retain the exact near skeleton and borrow its
            # motions; other overlapping ownership remains invalid.
            if owners[j] is not None and (data[68:72]!=b'JCL1' or owners[j]!=n):
                raise ValueError('overlapping motion ownership')
            owners[j]=n
    out=bytearray(data[:om])+bytearray(nm*8);cache={}
    def align(n):out.extend(bytes(-len(out)%n))
    for i,bones in enumerate(owners):
        if bones is None:raise ValueError('unowned motion')
        frames,ch=struct.unpack_from('<2I',data,om+i*8);span(ch,bones,24)
        if ch<om+nm*8 or ch+bones*24>ok:raise ValueError('JGC channel span')
        blob=data[ch:ch+bones*24]
        for bone in range(bones):
            row=struct.unpack_from('<6I',blob,bone*24)
            for k,stride in enumerate((12,6,12)):
                count,offset=row[k*2:k*2+2]
                if count and (offset>op-ok or count>(op-ok-offset)//stride):raise ValueError('JGC key span')
        if blob not in cache:align(16);cache[blob]=len(out);out.extend(blob)
        struct.pack_into('<2I',out,om+i*8,frames,cache[blob])
    align(16);h[14]=len(out);out.extend(data[ok:op]);align(16);h[15]=len(out)
    for i in range(nt):
        w,ht,_,_,clut,pix=struct.unpack_from('<4H2I',data,ot+i*16)
        if not w or not ht:raise ValueError('JGC texture dimensions')
        span(clut,1024,1);span(pix,w*ht,1)
        for at in (ot+i*16+8,ot+i*16+12):
            ptr=struct.unpack_from('<I',data,at)[0]
            if ptr<op or ptr>=len(data):raise ValueError('JGC pixel pointer')
            struct.pack_into('<I',out,at,ptr-op+h[15])
    out.extend(data[op:]);struct.pack_into('<16I',out,4,*h)
    return bytes(out)

def mesh_only(data):
    """Geometry/texture companion; runtime borrows validated resident motions.

    Do not use this bank alone for animation. No mesh, bind-pose or pixel
    bytes change. The original full bank remains the verification source.
    """
    data=bytes(data);compact(data) # validate every input span before slicing
    h=list(struct.unpack_from('<16I',data,4));oc,ot,om,op=h[7],h[12],h[13],h[15]
    out=bytearray(data[:om]);newpix=len(out);out.extend(data[op:])
    for i in range(h[0]):struct.pack_into('<2I',out,oc+i*48+8,0,0)
    for i in range(h[5]):
        for at in (ot+i*16+8,ot+i*16+12):
            ptr=struct.unpack_from('<I',data,at)[0];struct.pack_into('<I',out,at,ptr-op+newpix)
    h[6]=0;h[13]=h[14]=h[15]=newpix;struct.pack_into('<16I',out,4,*h)
    return bytes(out)

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('source',type=Path);ap.add_argument('output',type=Path);ap.add_argument('--check',action='store_true');ap.add_argument('--mesh-only',action='store_true');a=ap.parse_args()
    original=a.source.read_bytes();result=(mesh_only if a.mesh_only else compact)(original)
    if a.check:assert a.output.read_bytes()==result
    else:
        if a.source.resolve()==a.output.resolve():raise ValueError('keep the original bank separate')
        a.output.write_bytes(result)
    print(f'{len(original):,} -> {len(result):,} bytes; {len(original)-len(result):,} bytes saved; '+('geometry retained, motions require resident owner' if a.mesh_only else 'all source payloads retained'))
if __name__=='__main__':main()
