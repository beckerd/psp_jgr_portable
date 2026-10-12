#!/usr/bin/env python3
"""Rival records as sub-record arrays (mission-set +0x18 list; record task 0x8a3 FUN_8c070454 makes one 0x118 group object per
sub-record {type, 0, quad* (4 xz pairs), nentries, entries*}.  The entry layout depends on type: 8/10/24 use dedicated units'
24-byte 3D route records, 11/12/14 use the source mover's
20-byte {pos[3], speed, action}, 13/15/32 use the helicopter controller's 24-byte {pos[3], speed, route attribute, flags}, 17/21
use 16-byte {pos[3], value}, and the rest use 28-byte {pos[3], BAM rot[3], last}.  Output
'GRP1': u32 nrec; per record u32 nsub; per sub: u32 type, u32 trigger kind (FUN_8c06e40c table 0x8c1b4c78: 0 player inside the
quad, 1 radius+angle, 2 line crossing), f quad[8], u32 nent; per entry: f pos[3], f aux[3], i32 last.  For mover entries aux[0]
preserves the source speed and last preserves the action.  Helicopter entries preserve speed/route attribute in aux[0:2] and
flags in last; for ordinary entries aux is the rotation in radians."""
import struct, sys, math
d=open(sys.argv[1],"rb").read(); N=len(d)
cands=[struct.unpack_from("<I",d,o)[0] for o in range(0,N-3,4)]
B=max(((sum(1 for v in cands if b<=v<b+N and (v-b)%4==0),b) for b in range(0x8cd80000,0x8ce00000,0x1000)))[1]
w=lambda a: struct.unpack_from("<I",d,a-B)[0]; f=lambda a: struct.unpack_from("<f",d,a-B)[0]; i32=lambda a: struct.unpack_from("<i",d,a-B)[0]; ok=lambda a: B<=a<B+N-3
recs=[]
t=w(B+0x18); q=t
while ok(t) and ok(q) and w(q) and len(recs)<64:
    r=w(q); q+=4; subs=[]; s=r
    while ok(r) and ok(s+19) and w(s)!=0xffffffff and len(subs)<8:
        typ,trig,quad,cnt,ent=w(s),w(s+4),w(s+8),w(s+12),w(s+16)
        qd=[f(quad+4*i) for i in range(8)] if ok(quad) else [0.0]*8
        ents=[]
        if ok(ent) and cnt<=(256 if typ==0x1f else 32):
            # FUN_8c070c84 (8/0xa/0x18): 24-byte Rocket/Tank route entries.  Word
            # +0x14 packs route flags in its low half and the initial unit
            # count in its high half (read by FUN_8c070c84 at +0x16).
            # FUN_8c070da0 (0x11/0x15): 16-byte waypoints;
            # FUN_8c070d28 (0xb/0xc/0xe): 20-byte mover waypoints;
            # FUN_8c0a7dd0/80f4/81c8/8390 (0xd/0xf/0x20): 24-byte
            # helicopter waypoints.  The old generic 28-byte decode shifted
            # every helicopter point after entry zero by four bytes.
            # 070f50/0afbb4: mines are a position-only array, not a route
            # or an eight-member formation (Explosion contains 135).
            stride = 12 if typ==0x1f else 16 if typ in (0x11,0x15) else 20 if typ in (0xb,0xc,0xe) else 24 if typ in (8,0xa,0x18,0xd,0xf,0x20) else 28
            for i in range(cnt):
                e=ent+stride*i
                if not ok(e+stride-1): break
                if stride==12: ents.append(([f(e),f(e+4),f(e+8)],[0.0,0.0,0.0],0))
                elif stride==16: ents.append(([f(e),f(e+4),f(e+8)],[0.0,0.0,0.0],i32(e+12)))
                elif stride==20: ents.append(([f(e),f(e+4),f(e+8)],[f(e+12),0.0,0.0],i32(e+16)))
                elif stride==24: ents.append(([f(e),f(e+4),f(e+8)],[f(e+12),f(e+16),0.0],i32(e+20)))
                else: ents.append(([f(e),f(e+4),f(e+8)],[i32(e+12+4*k)*2*math.pi/65536 for k in range(3)],i32(e+24)))
        subs.append((typ,trig,qd,ents)); s+=20
    recs.append(subs)
with open(sys.argv[2],"wb") as out:
    out.write(struct.pack("<4sI",b"GRP1",len(recs)))
    for subs in recs:
        out.write(struct.pack("<I",len(subs)))
        for typ,trig,qd,ents in subs:
            out.write(struct.pack("<II8fI",typ,trig,*qd,len(ents)))
            for pos,rot,last in ents: out.write(struct.pack("<6fi",*pos,*rot,last))
print(f"{sys.argv[1]}: {len(recs)} records, {sum(len(s) for s in recs)} sub-records -> {sys.argv[2]}")
