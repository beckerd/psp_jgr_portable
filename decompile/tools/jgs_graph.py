#!/usr/bin/env python3
"""Rival-gang route graphs from a mission-set image (JMSc +0x10, accessor FUN_8c09fef8 -> FUN_8c03c140 global 0x8c2c3808; FUN_8c03c254
picks, every 4th tick, the graph of the player's area whose node box holds the player -> 0x8c2c54f4; FUN_8c06d91c = node lookup).
+0x10 -> 6 area words -> 0-terminated graph pointer lists; graph {n, nodes*, xform*}; node (0x2c) {min[3], max[3], pos[3], nlinks, links* (16 B each: {node, x, y, z})}.
Output 'GRA1': u32 nareas(6); per area u32 ngraphs; per graph u32 n, u32 has_xform, f xform[12]; n x {f min[3], f max[3], f pos[3], i32 nlinks, i32 link0};
then u32 nlinks_total, {i32 node, f x, y, z} links[] (FUN_8c06e520 ranks a node's links by the distance from the target to this point)."""
import struct, sys
d=open(sys.argv[1],"rb").read(); N=len(d)
cands=[struct.unpack_from("<I",d,o)[0] for o in range(0,N-3,4)]
B=max(((sum(1 for v in cands if b<=v<b+N and (v-b)%4==0),b) for b in range(0x8cd80000,0x8ce00000,0x1000)))[1]
w=lambda a: struct.unpack_from("<I",d,a-B)[0]; f=lambda a: struct.unpack_from("<f",d,a-B)[0]; i32=lambda a: struct.unpack_from("<i",d,a-B)[0]; ok=lambda a: B<=a<B+N-3
areas=[]; links=[]
t=w(B+0x10)
for area in range(6):
    gl=w(t+4*area) if ok(t+4*area) else 0; graphs=[]
    a=gl
    while ok(gl) and ok(a) and w(a) and len(graphs)<16:
        g=w(a); a+=4
        if not ok(g): break
        n,nodes,xf=w(g),w(g+4),w(g+8); nodesl=[]
        if n>256 or not ok(nodes): break
        xform=[f(xf+4*k) for k in range(12)] if ok(xf) and xf else [1,0,0,0,0,1,0,0,0,0,1,0]
        for i in range(n):
            nd=nodes+i*0x2c
            if not ok(nd+0x2b): break
            nl=i32(nd+0x24); lp=w(nd+0x28); l0=len(links)
            if 0<=nl<=16 and ok(lp) and ok(lp+16*nl):
                for k in range(nl): links.append((i32(lp+16*k),f(lp+16*k+4),f(lp+16*k+8),f(lp+16*k+12)))
            else: nl=0
            nodesl.append(([f(nd+4*k) for k in range(9)],nl,l0))
        graphs.append((1 if (ok(xf) and xf) else 0,xform,nodesl))
    areas.append(graphs)
with open(sys.argv[2],"wb") as out:
    out.write(struct.pack("<4sI",b"GRA1",6))
    for graphs in areas:
        out.write(struct.pack("<I",len(graphs)))
        for hx,xform,nodesl in graphs:
            out.write(struct.pack("<II12f",len(nodesl),hx,*xform))
            for v,nl,l0 in nodesl: out.write(struct.pack("<9fii",*v,nl,l0))
    out.write(struct.pack("<I",len(links)))
    for l,x,y,z in links: out.write(struct.pack("<i3f",l,x,y,z))
ng=sum(len(g) for g in areas); nn=sum(len(x[2]) for g in areas for x in g)
if ng: print(f"{sys.argv[1]}: {ng} graphs, {nn} nodes, {len(links)} links -> {sys.argv[2]}")
