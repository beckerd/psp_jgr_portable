#!/usr/bin/env python3
"""Per-object texture-slot -> texpack-index maps from the exe alone (no RAM dump).
Loader FUN_8c01674c state 7 (file kind 1 = texture pack): entry +0xc -> list of {refs*, data*} pairs (one per pack texture, data =
pack load base + GBIX block offset); refs = array of {NJS_TEXLIST*, slot} ended by {0,-1}; each ref binds texlist->names[slot] to that
texture. Object -> texlist comes from the static object-pointer / record-pointer arrays (record = {NJS_TEXNAME* names, n}).
usage: static_texmap.py EXE STAGE OUT.json  (STAGE 1..3; packs = afs/STAGE{n}TXP_*)"""
import struct, sys, re, glob, os, json
exe=open(sys.argv[1],"rb").read(); BASE=0x8c010000; END=BASE+len(exe); stage=int(sys.argv[2]); out=sys.argv[3]
rw=lambda a: struct.unpack_from("<I",exe,a-BASE)[0]
names=[m.group(0).decode()[:-4] for m in re.finditer(rb"[A-Z_0-9]+\.AFS",exe)]
IMG=0x8cb00000; IMGEND=0x8cb00000+0x400000
# 1. kind-1 entries for this stage's packs
entries={}
for a in range(BASE,END-0x40,4):
    if rw(a+0x14)==1 and rw(a)<len(names) and rw(a+4)<0x100 and 0x8c800000<=rw(a+8)<0x8d000000 and BASE<=rw(a+0xc)<END:
        nm=names[rw(a)]
        if nm.startswith("STAGE%dTXP"%stage): entries.setdefault((nm,rw(a+4)),rw(a+0xc))
packs=[]; texlist_map={}   # texlist addr -> {slot: k}
k=0
def gbix_offsets(path):
    b=open(path,"rb").read(); i=0; offs=[]
    while True:
        i=b.find(b"GBIX",i)
        if i<0: break
        offs.append(i); i+=8
    return offs
order=sorted(entries.keys(), key=lambda t:(t[0]!="STAGE%dTXP_ALL"%stage, t[0], t[1]))
# stages 5/6: the stNtxr image is a kind-0 entry (STAGEn#1 -> 0x8c800000): pairs {texlist*, ptrs*}, ptrs[slot] = 0x8c800000 + GBIX
# block offset (FUN_8c01674c case 6); same slot map, just a different table shape
KIND0={5:0x8c209908, 6:0x8c219698}
if not order and stage in KIND0:
    path=sorted(glob.glob("afs/STAGE%d/001_st%dtxr.bin"%(stage,stage)))[0]; offs=gbix_offsets(path); packs.append(path)
    q=KIND0[stage]
    while rw(q)!=0:
        tl,pa=rw(q),rw(q+4); n=rw(tl+4)
        for slot in range(n):
            off=rw(pa+4*slot)-0x8c800000
            if off in offs: texlist_map.setdefault(tl,{})[slot]=offs.index(off)
        q+=8
    k=len(offs)
for (nm,fi) in order:
    files=sorted(glob.glob("afs/%s/*.txp"%nm))
    if fi>=len(files): print("missing pack",nm,fi); continue
    path=files[fi]; offs=gbix_offsets(path); packs.append(path)
    lst=entries[(nm,fi)]; pairs=[]; q=lst
    while rw(q)!=0: pairs.append((rw(q),rw(q+4))); q+=8
    base=min(d for _,d in pairs)
    if len(pairs)!=len(offs): print("warning: %s#%d pairs %d != textures %d"%(nm,fi,len(pairs),len(offs)))
    for refs,data in pairs:
        off=data-base
        if off in offs: kk=k+offs.index(off)
        else: print("warning: data offset %x not a GBIX block in %s"%(off,path)); continue
        q=refs
        while BASE<=q<END and rw(q)!=0:
            texlist_map.setdefault(rw(q),{})[rw(q+4)]=kk; q+=8
    k+=len(offs)
print("packs",len(packs),"textures",k,"texlists referenced",len(texlist_map))
# 2. object -> record runs anywhere in the exe
tex={}
isimg=lambda v: IMG<=v<IMGEND and (v&3)==0
isrec=lambda v: BASE<=v<END and (v&3)==0 and BASE<=rw(v)<END and 0<rw(v+4)<=64
a=BASE; nobj=0
while a<END-8:
    if isimg(rw(a)):
        s=a
        while a<END and isimg(rw(a)): a+=4
        objs=[rw(x) for x in range(s,a,4)]
        r=a
        while r<END and rw(r)==0 and r<a+4*len(objs): r+=4   # a zero terminator may sit between the two arrays: skip it (RAM extractor semantics)
        r0=r
        while r<END and (isrec(rw(r)) or rw(r)==0) and r<r0+4*len(objs): r+=4
        recs=[rw(x) for x in range(r0,r,4)]
        while recs and recs[-1]==0: recs.pop()
        if len(recs)>=1 and len(recs)<=len(objs):
            # Most discovered runs include unrelated leading object pointers,
            # so retain the independently RAM-validated suffix alignment.
            # Stage 1's F arrays are the one proven exception: FUN_8c03119c
            # indexes 0x8c106e0c[id] and 0x8c106ed4[id] directly.  Its 50th
            # object is untextured and the corresponding pointer is the zero
            # after 49 records, hence this exact pair must be prefix-aligned.
            direct_f = stage==1 and s==0x8c106e0c and r0==0x8c106ed4
            objs_=objs[:len(recs)] if direct_f else objs[-len(recs):]
            for o,rc in zip(objs_,recs):
                if not rc or rc not in texlist_map: continue
                n=rw(rc+4); m=texlist_map[rc]; tex[str(o-IMG)]=[m.get(i,-1) for i in range(n)]; nobj+=1
        a=max(a,r)
    else: a+=4
texlists={str(a):[m.get(i,-1) for i in range(rw(a+4))] for a,m in texlist_map.items()}
json.dump({"packs":packs,"texmap":tex,"texlists":texlists},open(out,"w"))
un=sum(v.count(-1) for v in tex.values()); print("objects",len(tex),"slots",sum(len(v) for v in tex.values()),"unresolved",un,"->",out)
