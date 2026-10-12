#!/usr/bin/env python3
"""Dump a bank's 2D image lists (FUN_8c0b09b4 format) into an 'L2D1' file.
usage: jgs_2dlist.py BANK.bin BASE OUT.bin [--table=off,count]   (table = pointer table of list headers at BANK+off)
list header {entries*, n, float x, y, z, int rot, float sx, sy}; entry 0x40 bytes {u16 slot, u16 pad, float x,y,w,h, s16 rot, s16 z,
float pu,pv, ARGB[4], float u0,v0,u1,v1} (decoded from FUN_8c0b0ad4).
OUT: 'L2D1', u32 count, then per list: u32 n, f32 x,y,z, i32 rot, f32 sx,sy, n*0x40 raw entries. Lists whose pointer does not
resolve to a valid header are written with n=0."""
import struct, sys
args=[a for a in sys.argv[1:] if not a.startswith("--")]; opts=dict(a[2:].split("=",1) for a in sys.argv[1:] if a.startswith("--"))
b=open(args[0],"rb").read(); B=int(args[1],16); toff,tcount=[int(x,0) for x in opts.get("table","0,96").split(",")]
rw=lambda a: struct.unpack_from("<I",b,a-B)[0]
out=[]; good=0
for i in range(tcount):
    L=rw(B+toff+i*4)
    ok=B<=L<B+len(b)-32
    if ok:
        e,n=rw(L),rw(L+4); ok=B<=e<B+len(b) and 0<n<=256 and e+n*0x40<=B+len(b)
    if not ok: out.append(struct.pack("<I3fi2f",0,0,0,0,0,1,1)); continue
    x,y,z=struct.unpack_from("<3f",b,L-B+8); rot=struct.unpack_from("<i",b,L-B+20)[0]; sx,sy=struct.unpack_from("<2f",b,L-B+24)
    out.append(struct.pack("<I3fi2f",n,x,y,z,rot,sx,sy)+b[e-B:e-B+n*0x40]); good+=1
open(args[2],"wb").write(b"L2D1"+struct.pack("<I",tcount)+b"".join(out))
print("wrote",args[2],tcount,"lists,",good,"valid")
