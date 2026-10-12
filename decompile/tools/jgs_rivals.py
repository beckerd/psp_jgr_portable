#!/usr/bin/env python3
"""Rival / police squad records from a mission-set image (JMSc header +0x18, accessor FUN_8c09ff10; event 2/3 -> FUN_8c059c1c(idx) ->
FUN_8c06f9a4 -> FUN_8c070454 -> FUN_8c06f630(type) -> group creator 0x8c1b4df0[type]): record {u32 type (0x18/0x19 police squads, others
rival gangs), u32 sub, ptr, u32 count, entries*, ...}; entry (28 bytes) {float x,y,z, u32, s32 yaw(BAM), u32, u32 pose}.
Output 'RIV1', u32 n, per record: u32 type, u32 count, then count x {float x,y,z, float yaw_rad, u32 pose}."""
import struct, sys, math
d=open(sys.argv[1],"rb").read(); N=len(d)
cands=[struct.unpack_from("<I",d,o)[0] for o in range(0,N-3,4)]
B=max(((sum(1 for v in cands if b<=v<b+N and (v-b)%4==0),b) for b in range(0x8cd80000,0x8ce00000,0x1000)))[1]
w=lambda a: struct.unpack_from("<I",d,a-B)[0]; f=lambda a: struct.unpack_from("<f",d,a-B)[0]; i=lambda a: struct.unpack_from("<i",d,a-B)[0]; ok=lambda a: B<=a<B+N-3
recs=[]
t=w(B+0x18)
if ok(t):
    p=t
    while ok(p) and w(p) and len(recs)<64:
        r=w(p); p+=4
        if not ok(r): break
        ty=w(r); cnt=w(r+0xc); ent=w(r+0x10)
        if cnt>64 or not ok(ent): recs.append((ty,[])); continue
        ents=[]
        for k in range(cnt):
            e=ent+k*28
            if not ok(e+24): break
            ents.append((f(e),f(e+4),f(e+8),i(e+16)*2*math.pi/65536,w(e+24)))
        recs.append((ty,ents))
with open(sys.argv[2],"wb") as out:
    out.write(struct.pack("<4sI",b"RIV1",len(recs)))
    for ty,ents in recs:
        out.write(struct.pack("<II",ty,len(ents)))
        for x,y,z,yaw,pose in ents: out.write(struct.pack("<4fI",x,y,z,yaw,pose))
print(f"{sys.argv[1]}: {len(recs)} rival records, types {[r[0] for r in recs]}, members {sum(len(r[1]) for r in recs)} -> {sys.argv[2]}")
