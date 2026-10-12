#!/usr/bin/env python3
"""Extract the game's real collision geometry into a COL1 file (decompiled from FUN_8c0b42d4 / FUN_8c0b6b1c /
FUN_8c0b409c / FUN_8c0317e4).
usage: jgs_collision.py STAGE.bin OUT.col --grid=0x8cd76ad4 [--items=0x8c106090] [--sectors=63]
  world grid : 32x32 cells ((cz&31)*32+(cx&31)) x {entry*, count}; entry {verts*, patch*, info, 1} (16 bytes)
  patch      : 0x24 bytes {bbox min/max (6 f), idx* (+0x18), n|flags (+0x1c: low16 = triangle count,
               bit31 = double-sided, bit30 = initial winding), u16 mask (+0x20), u16 surface (+0x22)}
  items      : 40-byte placement records in the stage image {id, pos[3], ang[3] BAM, flags, collider*, task};
               collider = node tree (0x44 bytes) {u16 type(6), u16 mask, mesh*, ?, pos[3], rot[3] BAM, scl[3], .., child* (+0x3c),
               sibling* (+0x40)}; mesh {verts*, patches*, u16 nverts, u16 npatches}
COL1: 'COL1', u32 ntri, u32 patch_offset, u32 CPS1 signature (zero for legacy), then
per tri: 9 float, u16 surf, u16 mask, u16 flags(bit0 double-sided), u16 src.
CPS1: version/world_count/patch_count/entry_count; 1028 u32 starts (3 alignment
pads), 32-byte boxes {3f,count,3f,first}, 12-byte entries {u16 first,patch;
s16 ymin,ymax;u16 mask,key}. Source world IDs must fit 16 bits."""
import struct, sys, math
sys.path.insert(0, __file__.rsplit("/", 1)[0]); from ninjalib import mmul, mtrans, mscale, mrotx, mroty, mrotz, A2R, xform, placement_matrix
args=[a for a in sys.argv[1:] if not a.startswith("--")]; opts=dict(a[2:].split("=",1) for a in sys.argv[1:] if a.startswith("--"))
e=open("extracted/1ST_READ.BIN","rb").read(); EB=0x8c010000
ew=lambda a: struct.unpack_from("<I",e,a-EB)[0]
d=open(args[0],"rb").read(); N=len(d); B=0x8cb00000
EN=len(e)
RAM=open(opts["ram"],"rb").read() if "ram" in opts else None; RB=0x8c000000
def ok(v): return (B<=v<B+N) or (EB<=v<EB+EN and v<0x8c2b0000) or (RAM is not None and RB<=v<RB+len(RAM))
def _rd(fmt,a):
    if B<=a<B+N: return struct.unpack_from(fmt,d,a-B)[0]
    if EB<=a<EB+EN and a<0x8c2b0000 and RAM is None: return struct.unpack_from(fmt,e,a-EB)[0]
    if RAM is not None: return struct.unpack_from(fmt,RAM,a-RB)[0]
    return struct.unpack_from(fmt,e,a-EB)[0]
su=lambda a: _rd("<I",a); sh=lambda a: _rd("<H",a); ss=lambda a: _rd("<h",a); sf=lambda a: _rd("<f",a); si=lambda a: _rd("<i",a)
tris=[]
event_boxes=[]
item_owners=[]; item_patches=[]; item_owner_ids=[]; item_owner=None
def capture_item_patch(first):
    if item_owner is None or first==len(tris): return
    points=[struct.unpack("<3f",struct.pack("<3f",*v)) for t in tris[first:] for v in t[0]]
    low=[min(v[k] for v in points) for k in range(3)]
    high=[max(v[k] for v in points) for k in range(3)]
    item_patches.append((*low,len(tris)-first,*high,first));item_owner_ids.append(item_owner)
def begin_item(address,kind):
    global item_owner
    item_owner=len(item_owners);item_owners.append([address,kind&0xffffffff,len(tris),0])
def end_item():
    global item_owner
    item_owners[item_owner][3]=len(tris)-item_owners[item_owner][2]
    if not item_owners[item_owner][3]:item_owners.pop()
    item_owner=None
source_patches=[]; source_ids={}; source_entries=[]; source_starts=[]; nworld=0
def emit_patch(verts_addr, patch, m, src):
    first=len(tris)
    idx=su(patch+0x18); fl=su(patch+0x1c); n=fl&0xffff; mask=sh(patch+0x20); surf=sh(patch+0x22)
    if n==0 or not ok(idx): return
    dbl=1 if fl&0x80000000 else 0; wind=1 if fl&0x40000000 else 0
    ids=[ss(idx+k*2) for k in range(n+2)]
    def V(i):
        a=verts_addr+i*12; p=(sf(a),sf(a+4),sf(a+8))
        out=xform(m,p) if m else p
        if not all(math.isfinite(v) for v in out):
            raise ValueError(f'non-finite collision vertex: patch {patch:#x}, vertex {a:#x}, source {src}')
        return out
    v0,v1=V(ids[0]),V(ids[1])
    for k in range(n):
        v2=V(ids[2+k])
        # FUN_8c0b42d4: plane from (v0,v1,v2), winding bit toggles per triangle (strip); the flipped ones are (v0,v2,v1)
        tris.append(((v0,v1,v2) if wind else (v0,v2,v1), surf, mask, dbl, src))   # bit30 clear = flipped (verified: ground normals up)
        wind^=1; v0,v1=v1,v2
    capture_item_patch(first)
def emit_mesh(mesh, m, src):
    vb,pb,nv,npat=su(mesh),su(mesh+4),sh(mesh+8),sh(mesh+10)
    if not (ok(vb) and ok(pb)): return
    for i in range(npat): emit_patch(vb, pb+i*0x24, m, src)
def walk_node(n, m, src, depth=0):
    while ok(n) and depth<16:
        t=sh(n); mesh=su(n+4)
        pos=(sf(n+0xc),sf(n+0x10),sf(n+0x14)); rot=(si(n+0x18),si(n+0x1c),si(n+0x20)); scl=(sf(n+0x24),sf(n+0x28),sf(n+0x2c))
        # 0b6b1c calls 27a06c with r5=0: Z, Y, X, then scale.
        lm=mmul(m, mmul(placement_matrix(pos,rot), mscale(*scl)))
        if t==6 and ok(mesh): emit_mesh(mesh, lm, src)
        elif t==4 and ok(mesh) and 'solid-only' not in opts:
            # analytic box: {hx, ymin, hz, hx, ymax, hz} (FUN_8c0b6ec4 shape); emitted as 12 one-sided triangles
            hx,y0,hz,y1=sf(mesh),sf(mesh+4),sf(mesh+8),sf(mesh+16)
            c=[(-hx,y0,-hz),(hx,y0,-hz),(hx,y0,hz),(-hx,y0,hz),(-hx,y1,-hz),(hx,y1,-hz),(hx,y1,hz),(-hx,y1,hz)]
            wc=[xform(lm,q) for q in c]
            faces=[(4,7,6),(4,6,5),(0,1,2),(0,2,3),(0,4,5),(0,5,1),(1,5,6),(1,6,2),(2,6,7),(2,7,3),(3,7,4),(3,4,0)]
            first=len(tris)
            for a,b,cc in faces: tris.append(((wc[a],wc[b],wc[cc]),0x1c,0xffff,0,src))
            event_boxes.append((first,len(tris)-first,n,src))
            capture_item_patch(first)
        child=su(n+0x3c)
        if ok(child) and child!=n: walk_node(child, lm, src, depth+1)
        n=su(n+0x40)
# world grid
grid=int(opts.get("grid","0"),16)
if grid:
    cells=0
    for c in range(1024):
        source_starts.append(len(source_entries))
        p,cnt=su(grid+c*8),su(grid+c*8+4)
        if not ok(p) or cnt==0 or cnt>4096: continue
        cells+=1
        for k in range(cnt):
            en=p+k*16; vb,pa=su(en),su(en+4)
            if ok(vb) and ok(pa):
                first=len(tris); emit_patch(vb, pa, None, 0); count=len(tris)-first
                if not count: continue
                key=(vb,pa)
                if key not in source_ids:
                    bounds=tuple(sf(pa+j*4) for j in range(6))
                    if not all(math.isfinite(x) for x in bounds) or any(bounds[j]>bounds[j+3] for j in range(3)):
                        raise ValueError(f'invalid source bounds at {pa:#x}')
                    source_ids[key]=len(source_patches)
                    source_patches.append((*bounds[:3],count,*bounds[3:],first))
                source_entries.append((first,source_ids[key],ss(en+8),ss(en+10),sh(en+12),sh(en+14)))
    nworld=len(tris); source_starts.append(len(source_entries))
    assert len(source_patches)<65536 and nworld<=65536
    print(f"world grid: {cells} cells, {len(tris)} triangles")
# item colliders
items=int(opts.get("items","0"),16); nsect=int(opts.get("sectors","63"),0)
RECSZ=int(opts.get("recsize","40"),0); COLOFS=int(opts.get("colofs","32"),0)   # Bantam/Grind Square lists use 0x34-byte records with the collider at +0x2c
TYPECOL={int(k,0):int(v,16) for k,v in (kv.split(":") for kv in opts["typecol"].split(","))} if "typecol" in opts else {}   # record type -> collider node (module init patches, e.g. FUN_8c0f43c8 / FUN_8c0f5134)
protos=[]
item_sectors=None
if 'itemsectors' in opts:
    address,count=opts['itemsectors'].split(',')
    item_sectors={ew(int(address,16)+i*4) for i in range(int(count))}
global_items={int(v,0) for v in opts.get('globalitems','').split(',') if v}
if "protos" in opts:
    pa,pn=opts["protos"].split(","); pa=int(pa,16); pn=int(pn)
    protos=[ew(pa+k*4) for k in range(pn)]
if items:
    n0=len(tris); recs=0; withcol=0
    for s in range(nsect+1):
        a=ew(items+s*4)
        if not ok(a): continue
        while a-B+RECSZ<=N:
            pid=si(a)
            if pid<-1: break
            recs+=1
            # Optional original registration policy: some item tables also
            # contain render-only copies of geometry already in the world
            # grid. Their prototype existing does not register a collider.
            if item_sectors is not None and s not in item_sectors and pid not in global_items:
                a+=RECSZ
                continue
            # 0396ec: Kogane's 52-byte floor records have a runtime cache
            # HANDLE at +0x2c, not a collider pointer. Bind by original id.
            col=0 if 'protos-only' in opts else su(a+COLOFS)
            if not ok(col) and pid in TYPECOL: col=TYPECOL[pid]
            if not ok(col) and 0<=pid<len(protos) and ok(protos[pid]) and sh(protos[pid])==6: col=protos[pid]
            if ok(col):
                withcol+=1
                pos=(sf(a+4),sf(a+8),sf(a+12)); ang=(si(a+16),si(a+20),si(a+24))
                m=placement_matrix(pos,ang)
                begin_item(a,pid);walk_node(col, m, 1);end_item()
            a+=RECSZ
    print(f"items: {recs} records, {withcol} with colliders, {len(tris)-n0} triangles")
if 'staticnodes' in opts:
    # 0396ec also registers the 45 original global nodes at 1796a8.
    address,count=opts['staticnodes'].split(',');address=int(address,16);count=int(count)
    identity=[1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1];before=len(tris)
    for k in range(count):
        node=ew(address+k*4)
        if node:
            begin_item(node,0xffffffff);walk_node(node,identity,1);end_item()
    print(f'static nodes: {count} slots, {len(tris)-before} triangles')
# dynamic props (FUN_8c030664 / FUN_8c057280): 40-byte records {type, pos[3], rot[3], flags, ...} in --dyn sector lists,
# collider = --dynprotos[type] (node tree), contact type from the live-task map (RAM dump) / FUN_8c030664 switch
DYN_CONTACT={4:0x20,12:0x1f,13:0x20,14:0x21,15:0x1c,16:8,17:8,18:8,19:8,20:0x20,21:0x20,22:0x1b,27:0x20,10:0x20,0x14:0x20,0x15:0x20,0x16:0x1b}
DYN_DEFAULT=None
if "dyncontact" in opts:   # per-stage contact map from the stage module's spawn switch: "default:0x1b,1:0x1e,..."
    DYN_CONTACT={}
    for kv in opts["dyncontact"].split(","):
        k,v=kv.split(":")
        if k=="default": DYN_DEFAULT=int(v,0)
        else: DYN_CONTACT[int(k,0)]=int(v,0)
DYNREC=int(opts.get("dynrec","40"),0)   # Benten/Bantam/Grind Square placement records are 0x34 bytes
DYN_EXCLUDE={int(v,0) for v in opts.get('dynexclude','').split(',') if v}
if "dyn" in opts:
    dyn=int(opts["dyn"],16); da,dn=opts["dynprotos"].split(","); da=int(da,16); dn=int(dn)
    dprotos=[ew(da+k*4) for k in range(dn)]
    n0=len(tris); recs=0; withcol=0
    for s_ in range(nsect+1):
        lp=ew(dyn+s_*4)
        if not ok(lp): continue
        recl=[]
        if ok(su(lp)):      # pointer list
            q=lp
            while ok(su(q)) and q-lp<40000: recl.append(su(q)); q+=4
        else:
            q=lp
            while ok(q) and si(q)>=-1: recl.append(q); q+=DYNREC
        for a in recl:
            ty=si(a)
            if ty in DYN_EXCLUDE: continue  # original event body volumes, owned by their live tasks
            recs+=1
            nodes=[]
            if (ty in DYN_CONTACT or DYN_DEFAULT is not None) and 0<=ty<len(dprotos) and ok(dprotos[ty]):
                pr=dprotos[ty]
                if sh(pr) in (4,6): nodes=[pr]
                else:
                    lst,cnt=su(pr),su(pr+4)
                    if ok(lst) and cnt<64: nodes=[su(lst+i*12) for i in range(cnt) if ok(su(lst+i*12))]
            if nodes:
                withcol+=1
                pos=(sf(a+4),sf(a+8),sf(a+12)); ang=(si(a+16),si(a+20),si(a+24))
                m=placement_matrix(pos,ang)
                k0=len(tris)
                begin_item(a,ty)
                for nd in nodes: walk_node(nd, m, 2)
                end_item()
                ct=DYN_CONTACT.get(ty,DYN_DEFAULT)
                for k in range(k0,len(tris)): v,surf,mask,dbl,src=tris[k]; tris[k]=(v,ct,mask,dbl,src)
    print(f"dyn: {recs} records, {withcol} with colliders, {len(tris)-n0} triangles")
# set-placed object colliders (JMSc +0x34 records -> FUN_8c090ff0 task 0x8a9): per-type type-6 mesh nodes in the stage image
# (Shibuya 0x8c107ae8 / 0x8c107b30 ...), written in LOCAL space so the PSP places them by the record's position / yaw:
# --setobj=OUT.sob:TABLE,N[:TABLE2,N2] -> 'SOB1', u32 ntypes, per type u32 ntris, ntris x {9 floats, u16 surf, u16 mask, u16 dbl, u16 pad}
if "setobj" in opts:
    parts=opts["setobj"].split(":"); outp=parts[0]; tabs=[(int(x.split(",")[0],16),int(x.split(",")[1])) for x in parts[1:]]
    nty=max(n for a,n in tabs); per=[]
    for ty in range(nty):
        k0=len(tris)
        for ta,tn in tabs:
            if ty<tn:
                nd=su(ta+ty*4)
                if ok(nd) and sh(nd) in (4,6): walk_node(nd, mtrans(0.,0.,0.), 3)
        per.append(tris[k0:]); del tris[k0:]
    with open(outp,"wb") as f:
        f.write(struct.pack("<4sI",b"SOB1",nty))
        for lst in per:
            f.write(struct.pack("<I",len(lst)))
            for (v,surf,mask,dbl,src) in lst: f.write(struct.pack("<9f4H",*v[0],*v[1],*v[2],surf,mask,dbl,0))
    print("setobj colliders:",[len(l) for l in per],"->",outp)
with open(args[1],"wb") as f:
    f.write(struct.pack("<4sIII",b"COL1",len(tris),16+len(tris)*44 if grid else 0,0x31535043 if grid else 0))
    for (v,surf,mask,dbl,src) in tris:
        f.write(struct.pack("<9f4H",*v[0],*v[1],*v[2],surf,mask,dbl,src))
    if grid:
        # CPS1: original world grid/patches. Triangles and logical IDs stay
        # unchanged; props continue after nworld. Aligned 32-byte boxes
        # permit four bounds tests per VFPU batch without a second copy.
        f.write(struct.pack('<4I',1,nworld,len(source_patches),len(source_entries)))
        f.write(struct.pack('<1028I',*(source_starts+[0]*3)))
        for p in source_patches:f.write(struct.pack('<3fI3fI',*p))
        for e in source_entries:f.write(struct.pack('<HHhhHH',*e))
        if item_owners:
            # Optional CIP1 follows CPS1. Existing triangles, world metadata,
            # IDs and per-cell ordering are unchanged. Boxes enclose the
            # rounded emitted vertices, retaining source strip boundaries.
            f.write(struct.pack('<4I',0x31504943,len(item_owners),len(item_patches),0))
            for owner in item_owners:f.write(struct.pack('<4I',*owner))
            for patch in item_patches:f.write(struct.pack('<3fI3fI',*patch))
            f.write(struct.pack('<'+'I'*len(item_owner_ids),*item_owner_ids))
            print(f'item patches: {len(item_owners)} owners, {len(item_patches)} patches')
if 'owners-json' in opts:
    import json
    with open(opts['owners-json'],'w') as f:json.dump(item_owners,f)
if 'event-boxes-json' in opts:
    import json
    with open(opts['event-boxes-json'],'w') as f:json.dump(event_boxes,f)
import collections
print("surface types:",sorted(collections.Counter(t[1] for t in tris).items()))
xs=[p[0] for t in tris for p in t[0]]; ys=[p[1] for t in tris for p in t[0]]; zs=[p[2] for t in tris for p in t[0]]
if tris:
    print(f"bbox x {min(xs):.0f}..{max(xs):.0f} y {min(ys):.0f}..{max(ys):.0f} z {min(zs):.0f}..{max(zs):.0f}; wrote {args[1]} ({len(tris)} tris)")
else:
    print(f"wrote {args[1]} (0 world triangles; separate set-object bank only)")
