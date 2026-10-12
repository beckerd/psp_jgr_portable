#!/usr/bin/env python3
"""Mission table from E_MISSIONDATA JMDc images -> psp/missions.bin ('MIS3').
Each JMDc file holds a chain of mission records linked by +0xec (recruit trials: kind 6 demo -> kind 4 attempt -> ...;
loader FUN_8c041f78 walks the chain). Header: 'MIS3', u32 n. Per record (88 bytes): u32 stage_index(+0x8), mission_id(+0xc),
set(+0x10), kind(+0x14), rival(+0x18), float start[3](+0x70), char name[16], u32 time_sec(+0xe4), u32 pretag(+0x24), u32 base
(load address), u32 blob_off, u32 blob_len, u32 rec_off (record offset inside the blob), s32 next (record index or -1),
u32 flags(+0x28), s32 yaw(+0x80, BAM), u32 first (index of the file's first record: events 0x14/0x15 select the next
record/set index within the file for the loader task 0x1c +0x90/+0x94). The raw JMDc image follows (blob) so the PSP build can walk the real trigger/event lists."""
import struct, glob, os, sys
from mission_chain import chain_offsets, mission_source
recs=[]; blobs=b""
for fp in sorted(glob.glob("afs/E_MISSIONDATA/*.bin")):
    fp=str(mission_source(fp))
    d=open(fp,"rb").read()
    if d[:4]!=b"JMDc": continue
    N=len(d); base=0x8cde0000
    name=os.path.basename(fp).split("_")[-1].split(".")[0].upper()
    blob_off=len(blobs); chain=chain_offsets(d)
    first=len(recs)
    for k,r in enumerate(chain):
        ws=struct.unpack_from("<7I",d,r); pos=struct.unpack_from("<3f",d,r+0x70)
        tsec=struct.unpack_from("<I",d,r+0xe4)[0]; pretag=struct.unpack_from("<I",d,r+0x24)[0]
        flags=struct.unpack_from("<I",d,r+0x28)[0]; yaw=struct.unpack_from("<i",d,r+0x80)[0]
        nxt=first+k+1 if k+1<len(chain) else -1
        recs.append(struct.pack("<5I3f16s5IIiIiI",ws[2],ws[3],ws[4],ws[5],ws[6],*pos,name.encode(),tsec,pretag,base,blob_off,N,r,nxt,flags,yaw,first))
    blobs+=d+b"\0"*((-N)%16)
hdr=8+len(recs)*88
target=sys.argv[1] if len(sys.argv)>1 else "../psp/missions.bin"
with open(target,"wb") as out:
    out.write(struct.pack("<4sI",b"MIS3",len(recs)))
    for r in recs:
        r=bytearray(r); off=struct.unpack_from("<I",r,60)[0]; struct.pack_into("<I",r,60,off+hdr); out.write(r)
    out.write(blobs)
print(len(recs),"mission records,",len(blobs),"bytes of images ->",target)
