#!/usr/bin/env python3
"""Police pursuit routes from a mission-set image (JMSc header +0x24, accessor FUN_8c09ff34; event 0x1f a1 -> record a1 ->
FUN_8c0d8120 on the police manager task 0x9fe). Record {points*, areas*, segments*, 0}:
  point   (0x48): pos[3], quad[4][3] (FUN_8c0d88d0 point-in-quad, diagonal p1-p2, y range p0.y..p1.y when they differ), r2 (float,
                  terminator < 0), i16 segment (rebuilt at runtime from the segments' start bytes), i16 nadj, adjacency* (nadj x {i32 idx, f cost})
  area    (0x44): pos[3], quad[4][3], r2, i16 point, i16 alt point (-2 none); terminator point < 0  (FUN_8c0d90a8 fallback lookup)
  segment (0x50): pos[3], 12 bytes 0, u8 kind (0 quad test, else FUN_8c0d8a54 line crossing; 0xff terminator), u8 nmembers,
                  u8 start point, u8, geom[12] (quad or line p0,p1), members* (0x24 each: pos[3], BAM rot[3] (drawn -x, y, -z), scale[3];
                  drawn by FUN_8c0d84f0 with effect-bank object 0x8c182fe8+0x94 and motion 0x8c18308c+0x94 while the pursuit is active)
Output 'POL1': u32 nrec; per record: u32 npts, nareas, nsegs, nmem, nadj; then npts x {f pos[3], f quad[12], f r2, i32 nadj, i32 adj0},
nareas x {f pos[3], f quad[12], f r2, i32 pt, i32 pt2}, nsegs x {f pos[3], i32 kind, i32 start, i32 nmem, i32 mem0, f geom[12]},
nmem x {f pos[3], f rot[3] (radians), f scale[3]}, nadj x {i32 idx, f cost}."""
import struct, sys, math
d=open(sys.argv[1],"rb").read(); N=len(d)
cands=[struct.unpack_from("<I",d,o)[0] for o in range(0,N-3,4)]
B=max(((sum(1 for v in cands if b<=v<b+N and (v-b)%4==0),b) for b in range(0x8cd80000,0x8ce00000,0x1000)))[1]
w=lambda a: struct.unpack_from("<I",d,a-B)[0]; f=lambda a: struct.unpack_from("<f",d,a-B)[0]; h=lambda a: struct.unpack_from("<h",d,a-B)[0]; i32=lambda a: struct.unpack_from("<i",d,a-B)[0]
ok=lambda a: B<=a<B+N-3
recs=[]
t=w(B+0x24)
if ok(t):
    k=0
    while k<32:
        r=w(t+4*k); k+=1
        if r==0: recs.append(None); continue
        if not ok(r) or not ok(w(r)): break
        pts=[]; areas=[]; segs=[]; mems=[]; adj=[]
        P=w(r); A=w(r+4); S=w(r+8)
        while ok(P+0x3c) and f(P+0x3c)>=0 and len(pts)<64:
            na=h(P+0x42); ap=w(P+0x44); a0=len(adj)
            if na<0 or na>16 or not ok(ap) or not ok(ap+8*na): break   # some sets' lists run into the next data without a terminator
            for j in range(na): adj.append((i32(ap+8*j),f(ap+8*j+4)))
            pts.append(([f(P+4*j) for j in range(3)],[f(P+0xc+4*j) for j in range(12)],f(P+0x3c),na,a0)); P+=0x48
        while pts and ok(A+0x42) and h(A+0x40)>=0 and len(areas)<128:
            if h(A+0x40)>=len(pts): break
            areas.append(([f(A+4*j) for j in range(3)],[f(A+0xc+4*j) for j in range(12)],f(A+0x3c),h(A+0x40),h(A+0x42))); A+=0x44
        while pts and ok(S+0x4f) and d[S+0x18-B]!=0xff and len(segs)<64:
            kind,nm,start=d[S+0x18-B],d[S+0x19-B],d[S+0x1a-B]; L=w(S+0x4c); m0=len(mems)
            if kind>1 or nm>16 or start>=len(pts) or not ok(L) or not ok(L+0x24*nm): break
            for j in range(nm):
                e=L+j*0x24; mems.append(([f(e+4*k2) for k2 in range(3)],[i32(e+0xc+4*k2)*2*math.pi/65536 for k2 in range(3)],[f(e+0x18+4*k2) for k2 in range(3)]))
            segs.append(([f(S+4*j) for j in range(3)],kind,start,nm,m0,[f(S+0x1c+4*j) for j in range(12)])); S+=0x50
        recs.append((pts,areas,segs,mems,adj))
while recs and recs[-1] is None: recs.pop()
with open(sys.argv[2],"wb") as out:
    out.write(struct.pack("<4sI",b"POL1",len(recs)))
    for rec in recs:
        if rec is None: out.write(struct.pack("<5I",0,0,0,0,0)); continue
        pts,areas,segs,mems,adj=rec
        out.write(struct.pack("<5I",len(pts),len(areas),len(segs),len(mems),len(adj)))
        for pos,q,r2,na,a0 in pts: out.write(struct.pack("<3f12ffii",*pos,*q,r2,na,a0))
        for pos,q,r2,p1,p2 in areas: out.write(struct.pack("<3f12ffii",*pos,*q,r2,p1,p2))
        for pos,kind,start,nm,m0,g in segs: out.write(struct.pack("<3fiiii12f",*pos,kind,start,nm,m0,*g))
        for pos,rot,scl in mems: out.write(struct.pack("<9f",*pos,*rot,*scl))
        for idx,c in adj: out.write(struct.pack("<if",idx,c))
n=sum(1 for r in recs if r); print(f"{sys.argv[1]}: {len(recs)} entries, {n} routes, pts {sum(len(r[0]) for r in recs if r)} segs {sum(len(r[2]) for r in recs if r)} members {sum(len(r[3]) for r in recs if r)} -> {sys.argv[2]}")
