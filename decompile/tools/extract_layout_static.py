#!/usr/bin/env python3
"""Extract placement layouts for a stage from the executable's static tables + the stage image (no emulator needed).
usage: extract_layout_static.py STAGE.bin OUT.json CAT=TABLE,PROTO_NEAR[,PROTO_FAR] ...
Table = 63 sector entries -> either contiguous 40-byte records {id,x,y,z,ax,ay,az,...} (id<=-2 ends) or a
null-terminated list of record pointers (auto-detected)."""
import struct, sys, json
e=open("extracted/1ST_READ.BIN","rb").read(); EB=0x8c010000
ew=lambda a: struct.unpack_from("<I",e,a-EB)[0]
args=[a for a in sys.argv[1:] if not a.startswith("--")]; opts=dict(a[2:].split("=",1) for a in sys.argv[1:] if a.startswith("--"))
NSECT=int(opts.get("sectors","63"))
stage=args[0]; out=args[1]; d=open(stage,"rb").read(); N=len(d); B=0x8cb00000
instage=lambda v: B<=v<B+N and (v&3)==0
su=lambda a: struct.unpack_from("<I",d,a-B)[0]; sf=lambda a: struct.unpack_from("<f",d,a-B)[0]; si=lambda a: struct.unpack_from("<i",d,a-B)[0]
def read_array(base):
    out=[]; a=base
    while instage(ew(a)) and len(out)<600: out.append(ew(a)-B); a+=4
    return out
def rec(a): return (si(a),[sf(a+4),sf(a+8),sf(a+12)],list(struct.unpack_from("<3i",d,a+16-B)))
proto={}; inst=[]; exclude=set()
for spec in args[2:]:
    tag,rest=spec.split("="); parts=rest.split(",")
    stride=40; far=None; count=None
    while parts and parts[-1] and parts[-1][0] in "sn" and parts[-1][1:].isdigit():
        if parts[-1][0]=="s": stride=int(parts[-1][1:])
        else: count=int(parts[-1][1:])     # nNN: fixed prototype count (entries that are not stage pointers stay -1)
        parts=parts[:-1]
    parts=[int(x,16) if x else 0 for x in parts]; table,near=parts[0],parts[1]
    if len(parts)>2 and parts[2]: far=parts[2]
    P=read_array(near) if count is None else [(ew(near+k*4)-B if instage(ew(near+k*4)) else -1) for k in range(count)]
    grp=tag.startswith("GRP")
    if grp:   # group structs: model pointer at +4 -> model instances
        P=[su(B+g+4)-B if instage(su(B+g+4)) else -1 for g in P]
    proto[tag]=P
    if far: exclude|=set(read_array(far))
    n=0
    for s in range(NSECT):
        lp=ew(table+s*4)
        if not instage(lp): continue
        first=su(lp)
        if instage(first):     # pointer list
            p=lp
            while instage(su(p)) and p-lp<40000:
                pid,pos,ang=rec(su(p))
                if 0<=pid<len(P) and P[pid]>=0: inst.append([tag,pid,pos,ang]); n+=1
                p+=4
        else:                  # contiguous records
            a=lp
            while a-B+stride<=N:
                pid,pos,ang=rec(a)
                if pid<-1: break
                if 0<=pid<len(P) and P[pid]>=0: inst.append([tag,pid,pos,ang]); n+=1
                a+=stride
    print(f"  {tag}: protos {len(P)} far {len(exclude)} instances {n}")
model_inst=[[proto[t][pid],pos,ang] for t,pid,pos,ang in inst if t.startswith("GRP")]
inst=[i for i in inst if not i[0].startswith("GRP")]
proto={t:P for t,P in proto.items() if not t.startswith("GRP")}
protoset=sorted(set(v for P in proto.values() for v in P if v>=0))
exclude=sorted(exclude-set(protoset))
json.dump({"proto":proto,"inst":inst,"protoset":protoset,"exclude":exclude,"model_inst":model_inst},open(out,"w"))
print("wrote",out,"instances",len(inst))
