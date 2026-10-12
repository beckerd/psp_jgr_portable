#!/usr/bin/env python3
"""Police wave lineups from a mission-set image (JMSc header +0x28, accessor FUN_8c09ff40; event 0x21 -> FUN_8c0dd540 picks entry
[current route segment] of list a1 -> FUN_8c0dc768 wave object): record {positions[4..5] (12 bytes each) .. +0x3c yaw list (s32 BAM),
+0x50 u16 duration (60), +0x54.. camera}. Output 'WAV1', u32 nlists, per list: u32 n, per entry: 4 x {float x,y,z, float yaw}, u32 duration."""
import struct, sys, math
d=open(sys.argv[1],"rb").read(); N=len(d)
cands=[struct.unpack_from("<I",d,o)[0] for o in range(0,N-3,4)]
B=max(((sum(1 for v in cands if b<=v<b+N and (v-b)%4==0),b) for b in range(0x8cd80000,0x8ce00000,0x1000)))[1]
w=lambda a: struct.unpack_from("<I",d,a-B)[0]; f=lambda a: struct.unpack_from("<f",d,a-B)[0]; i=lambda a: struct.unpack_from("<i",d,a-B)[0]; ok=lambda a: B<=a<B+N-3
lists=[]
t=w(B+0x28)
if ok(t):
    p=t
    while ok(p) and w(p) and len(lists)<16:
        L=w(p); p+=4; ents=[]; q=L
        while ok(q) and w(q)!=0xffffffff and len(ents)<16:
            r=w(q); q+=4
            if not ok(r+0x54): continue
            pts=[(f(r+k*12),f(r+k*12+4),f(r+k*12+8),i(r+0x3c+k*4)*2*math.pi/65536) for k in range(4)]
            ents.append((pts,struct.unpack_from("<H",d,r+0x50-B)[0]))
        lists.append(ents)
with open(sys.argv[2],"wb") as out:
    out.write(struct.pack("<4sI",b"WAV1",len(lists)))
    for ents in lists:
        out.write(struct.pack("<I",len(ents)))
        for pts,dur in ents:
            for x,y,z,yaw in pts: out.write(struct.pack("<4f",x,y,z,yaw))
            out.write(struct.pack("<I",dur))
print(f"{sys.argv[1]}: {len(lists)} wave lists, {sum(len(l) for l in lists)} entries -> {sys.argv[2]}")
