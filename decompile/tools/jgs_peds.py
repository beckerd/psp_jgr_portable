#!/usr/bin/env python3
"""Extract pedestrian spawn lists from a mission-set image (JMSc, E_MISSIONSET). Header +0x30 (accessor FUN_8c09ff58) -> 0-terminated
table of list pointers; event 6 {list idx, zone} (FUN_8c05a038 -> FUN_8c0899b4) spawns list idx. A list is 20-byte records terminated by
type 0xff: {u8 type(<0x4c; 0x2b/0x2c police), u8 route class(<0x19), u8 flag, u8 nwaypoints, waypoints* (float xyz each), u32 0,
s16 yaw(BAM), s16, u32}. Walking speed per route class from the exe table 0x8c1b98b0.
Output 'PED3', u32 n, per ped: u32 type, u32 nwp, u32 route, u32 flags, float yaw_rad, float speed, u32 list,
then nwp*3 floats.  PED3 preserves the two source bytes that select the normal motion and the post-collision reaction."""
import struct, sys, math
d=open(sys.argv[1],"rb").read(); N=len(d)
e=open("extracted/1ST_READ.BIN","rb").read(); EB=0x8c010000
speed=[struct.unpack_from("<f",e,0x8c1b98b0-EB+k*8)[0] for k in range(25)]
cands=[struct.unpack_from("<I",d,o)[0] for o in range(0,N-3,4)]
B=max(((sum(1 for v in cands if b<=v<b+N and (v-b)%4==0),b) for b in range(0x8cd80000,0x8ce00000,0x1000)))[1]
w=lambda a: struct.unpack_from("<I",d,a-B)[0]; ok=lambda a: B<=a<B+N-3
peds=[]; nlists=0
t=w(B+0x30)
if ok(t):
    p=t
    while ok(p) and w(p) and nlists<64:
        L=w(p); q=L
        while ok(q) and d[q-B]!=0xff and q-B+20<=N:
            ty,rt,fl,nw=d[q-B:q-B+4]; wp=w(q+4); yaw=struct.unpack_from("<h",d,q-B+12)[0]
            if ty<0x4c and rt<0x19 and 0<nw<=32 and ok(wp) and wp-B+nw*12<=N:
                pts=[struct.unpack_from("<3f",d,wp-B+k*12) for k in range(nw)]
                peds.append((ty,rt,fl,nw,yaw,nlists,pts))
            q+=20
        nlists+=1; p+=4
with open(sys.argv[2],"wb") as out:
    out.write(struct.pack("<4sI",b"PED3",len(peds)))
    for ty,rt,fl,nw,yaw,li,pts in peds:
        out.write(struct.pack("<IIIIffI",ty,nw,rt,fl,yaw*2*math.pi/65536,speed[rt] if 0<=rt<25 else 0.28,li))
        for pt in pts: out.write(struct.pack("<3f",*pt))
print(f"{sys.argv[1]}: base {B:#x}, {nlists} lists, {len(peds)} pedestrians -> {sys.argv[2]}")
