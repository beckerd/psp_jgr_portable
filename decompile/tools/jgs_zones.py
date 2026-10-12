#!/usr/bin/env python3
"""Stage zones (sectors) for zone gating. `FUN_8c0c2c1c(.., neighbours, zonelist, nzones, counts, start)` per stage; zone detection
`FUN_8c0dcd3c`: list {records*, n}, record 0x38 {f32 bbox[6]?, quadsA*, nA, quadsB*, nB, zone, aux} with 144-byte volumes (6 points + 6
normals, `FUN_8c0c7ff4`), position inside A and not inside B -> zone. Neighbours: nzones x {u32, s32* list (-1 terminated)}.
usage: jgs_zones.py STAGE_DAT EXE_BIN NEIGHBOUR_TABLE ZONELIST_PTR NZONES OUT.bin COUNTS_TABLE
'ZON1': u32 nzones, u32 nrec, u32 counts[6] (zones per area, global zone = sum(counts[:area]) + index); per rec {s32 zone, s32 aux, f32 head[6], u32 nA, u32 nB, (nA+nB) x 144 B}; then per zone: u32 n, n x s32."""
import struct, sys
sd = open(sys.argv[1], "rb").read(); SB = 0x8cb00000
e = open(sys.argv[2], "rb").read(); EB = 0x8c010000
def rd(a, fmt="<I"):
    if EB <= a < EB + len(e): return struct.unpack_from(fmt, e, a - EB)[0]
    if SB <= a < SB + len(sd): return struct.unpack_from(fmt, sd, a - SB)[0]
    return None
def blob(a, n):
    if EB <= a < EB + len(e): return e[a - EB:a - EB + n]
    return sd[a - SB:a - SB + n]
NB = int(sys.argv[3], 16); ZL = rd(int(sys.argv[4], 16)); NZ = int(sys.argv[5]); CT = int(sys.argv[7], 16)
counts = [rd(CT + 4 * i) for i in range(6)]
recs = rd(ZL); n = rd(ZL + 4); out = bytearray(struct.pack("<4sII6I", b"ZON1", NZ, n, *counts)); zones = set()
for i in range(n):
    r = recs + 0x38 * i; head = blob(r, 24); qa, na, qb, nb, zone, aux = [rd(r + 0x18 + 4 * k) for k in range(6)]
    if zone > 0x7fffffff: zone -= 1 << 32
    out += struct.pack("<ii", zone, aux) + head + struct.pack("<II", na, nb); zones.add(zone)
    if na: out += blob(qa, na * 144)
    if nb: out += blob(qb, nb * 144)
for z in range(NZ):
    lp = rd(NB + 8 * z + 4); lst = []
    if lp and rd(lp) is not None:
        while True:
            v = rd(lp + 4 * len(lst), "<i")
            if v is None or v < 0 or len(lst) > 64: break
            lst.append(v)
    out += struct.pack("<I", len(lst)) + struct.pack("<%di" % len(lst), *lst)
open(sys.argv[6], "wb").write(out)
print(f"{sys.argv[6]}: {NZ} zones, counts {counts}, {n} volume records (areas seen {len(zones)}), {len(out):,} bytes")
