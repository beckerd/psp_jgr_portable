#!/usr/bin/env python3
"""Recorded controller streams and trial checkpoint scripts from a mission-set image (JMSc) -> psp/setNN_ctrl.bin ('CTL2').
Set +0x44 (FUN_8c09ff98): 0-terminated list of segment tables; a table is 0xc-byte entries {stream*, float start distance, flags
bit0 wait for the player, bit1 loop, bit2 stop} (controller task FUN_8c0278c0 modes 3/4). A stream is a 0x20-byte spawn record
{char id, float speed, x, y, z, u32, s32 yaw (BAM), u32} (FUN_8c01aeb0 teleports the skater to it) followed by 4-byte records
{repeat count | 0x80 = end, buttons (1 jump, 4 spray, 8 dash), direction (s8, <<8 = BAM), magnitude 0..255}.
Set +0x48 (FUN_8c09ffa8): pointer array of 0x1c-byte trial entries {type, p1, p2, quadsA*, nA, quadsB*, nB}; quads are the 144-byte
{6 points, 6 normals} volumes of FUN_8c0c7ff4. Types (player checks FUN_8c08e5ec..8c08e8d4): 0 grind distance >= p1 in A,
1 compares float +618 against float p2, 2/3 the same during wall rides, 4 jump-entry trick (fail if none),
5 tag in A, 6 brake-release gesture, 7 jump entry, 8 sixth ground-control update in A, 9 end = success;
inside B at any time = fail.
Set files chain further JMSc sets through header +0x50 (loader FUN_8c09fec8 index = task 0x1c +0x94, set by event 0x15).
Output 'CTL2': u32 nsets; per set: u32 ntab; per table u32 nseg + nseg x {u32 stream idx, float dist, u32 flags}; u32 nstream; per stream 8 words header,
u32 nrec, nrec x 4 bytes; u32 ntrial; per entry {u32 type, u32 p1, u32 p2, u32 nA, u32 nB} + (nA+nB) x 144 bytes."""
import struct, sys
from mission_chain import chain_offsets
d=open(sys.argv[1],"rb").read(); N=len(d); B=0x8cde6000
w=lambda a: struct.unpack_from("<I",d,a-B)[0]; f=lambda a: struct.unpack_from("<f",d,a-B)[0]; ok=lambda a: B<=a<B+N-3
# Seventeen supplied slots are identical legacy JMSb placeholders, not
# current-layout JMSc chains. Preserve an explicitly empty CTL2 bank; never
# reinterpret their +50 as a chain or manufacture a playable set zero.
sets=[] if d[:4]==b'JMSb' else [B+at for at in chain_offsets(d,b'JMSc')]
if not sets: print(f'{sys.argv[1]}: unsupported legacy JMSb (no controller sets)')
if len(sets)>8: raise ValueError('CTL2 runtime capacity exceeded; do not truncate source sets')
out=open(sys.argv[2],"wb"); out.write(struct.pack("<4sI",b"CTL2",len(sets))); report=[]
for S in sets:
  streams=[]; tabs=[]
  def stream_idx(s):
      if s in streams: return streams.index(s)
      streams.append(s); return len(streams)-1
  t=w(S+0x44)
  if ok(t):
      p=t
      while ok(p) and ok(w(p)) and len(tabs)<16:
          tb=w(p); p+=4; segs=[]; q=tb
          while ok(q) and ok(w(q)) and w(q+8)<8 and len(segs)<64:
              segs.append((stream_idx(w(q)),f(q+4),w(q+8))); q+=12
          tabs.append(segs)
  sdata=[]
  for s in streams:
      hdr=d[s-B:s-B+0x20]; recs=b""; q=s+0x20
      while ok(q):
          r=d[q-B:q-B+4]; recs+=r; q+=4
          if r[0]&0x80 or len(recs)>65536: break
      sdata.append((hdr,recs))
  trial=[]
  t=w(S+0x48)
  if ok(t):
      p=t
      while ok(p) and ok(w(p)) and len(trial)<64:
          e=w(p); p+=4; ty,p1,p2,qa,na,qb,nb=[w(e+k*4) for k in range(7)]
          # Set 15, chained set 2 has TWO A and SEVENTY-FIVE B volumes.
          # A guessed 64-volume cap silently erased that recruitment trial.
          if ty>9: break
          if (na and not (B<=qa and qa+na*144<=B+N)) or (nb and not (B<=qb and qb+nb*144<=B+N)):
              raise ValueError(f'out-of-image trial volumes at {e:08x}')
          qs=b""
          if na and ok(qa): qs+=d[qa-B:qa-B+na*144]
          else: na=0
          if nb and ok(qb): qs+=d[qb-B:qb-B+nb*144]
          else: nb=0
          trial.append((ty,p1,p2,na,nb,qs))
          if ty==9: break
  out.write(struct.pack("<I",len(tabs)))
  for segs in tabs:
      out.write(struct.pack("<I",len(segs)))
      for si,dist,fl in segs: out.write(struct.pack("<IfI",si,dist,fl))
  out.write(struct.pack("<I",len(sdata)))
  for hdr,recs in sdata: out.write(hdr); out.write(struct.pack("<I",len(recs)//4)); out.write(recs)
  out.write(struct.pack("<I",len(trial)))
  for ty,p1,p2,na,nb,qs in trial: out.write(struct.pack("<5I",ty,p1,p2,na,nb)); out.write(qs)
  report.append((len(tabs),[len(r)//4 for h,r in sdata],[e[0] for e in trial]))
out.close()
print(f"{sys.argv[1]}: {len(sets)} sets: " + "; ".join(f"tables {a} streams {b} trial {c}" for a,b,c in report))
