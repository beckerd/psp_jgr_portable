#!/usr/bin/env python3
"""Bake conservative opaque occluders and object boxes; never alter JGS geometry.

Only single-loop, manifold, convex coplanar patches become occluders. Optional
component splitting certifies edge-connected islands independently; alpha
batches, holes and nonplanar surfaces are excluded. No blocker spans a gap.
Occluders are inset two world units inside the verified source coverage.

Shipping rebakes use --coverage-from <existing bank> to re-certify and retain
the tested layout. A full bake may fill more of the current fixed arena than
an older bank. Splitting disconnected components is experimental and opt-in.
"""
from pathlib import Path
import struct, math, collections, argparse, json, hashlib
import numpy as np
from jgs_instances import expand_world_vertices

VISIBILITY_BYTES=144*1024

def load(path):
 d=Path(path).read_bytes();nt,no,nb,nv,ni=struct.unpack_from('<5I',d,4);ot,oo,ob,ov,oi,op=struct.unpack_from('<6I',d,24)
 verts=np.frombuffer(d,dtype=np.dtype([('uv','<f4',2),('col','<u4'),('p','<f4',3)]),count=nv,offset=ov)['p'].astype('float64');idx=np.frombuffer(d,dtype='<u2',count=ni,offset=oi).astype('int64')
 objs=[struct.unpack_from('<4f4I',d,oo+32*i) for i in range(no)];batches=[struct.unpack_from('<II4H',d,ob+16*i) for i in range(nb)]
 return d,objs,batches,verts,idx

def connected_patches(triangles):
 """Keep disconnected opaque islands separate without filling their gaps.

 Connectivity uses the same rounded edge keys as the coverage proof below.
 Joining by an entire edge (not merely a corner) leaves touching islands
 independent. Every resulting component still has to pass the unchanged
 manifold, winding, single-boundary, convexity and covered-area checks.
 """
 parents=list(range(len(triangles)));owners={}
 def root(i):
  while parents[i]!=i:
   parents[i]=parents[parents[i]];i=parents[i]
  return i
 for i,t in enumerate(triangles):
  keys=[tuple(np.round(p,3)) for p in t]
  for a,b in zip(keys,keys[1:]+keys[:1]):
   edge=tuple(sorted((a,b)))
   if edge in owners:parents[root(i)]=root(owners[edge])
   else:owners[edge]=i
 components=collections.defaultdict(list)
 for i,t in enumerate(triangles):components[root(i)].append(t)
 return components.values()

def patches(objs,batches,verts,idx,excluded=(),split_components=True):
 found=[]
 for oi,o in enumerate(objs):
  if o[6]&1 or oi in excluded:continue
  groups=collections.defaultdict(list)
  for bi in range(o[4],o[4]+o[5]):
   vo,io,nv,ni,tex,fl=batches[bi]
   if fl&1:continue
   tris=verts[vo+idx[io:io+ni].reshape(-1,3)]
   for t in tris:
    n=np.cross(t[1]-t[0],t[2]-t[0]);ln=np.linalg.norm(n)
    if ln<.01:continue
    n/=ln
    if n[np.argmax(abs(n))]<0:n=-n
    dist=-n@t[0];key=(*np.round(n,3),round(dist,1))
    groups[key].append(t)
  for group in (part for plane in groups.values() for part in (connected_patches(plane) if split_components else (plane,))):
   ts=np.array(group);n=np.cross(ts[0,1]-ts[0,0],ts[0,2]-ts[0,0]);n/=np.linalg.norm(n);dist=-n@ts[0,0]
   if np.max(np.abs(np.einsum('ij,j->i',ts.reshape(-1,3),n)+dist))>.01:continue
   edges=collections.Counter();directed=collections.Counter();coords={};area=0
   for t in ts:
    ks=[tuple(np.round(p,3)) for p in t];coords.update(zip(ks,t))
    if np.sum(np.cross(t[1]-t[0],t[2]-t[0])*n)<0:ks.reverse()
    for a,b in zip(ks,ks[1:]+ks[:1]):
     edges[tuple(sorted((a,b)))]+=1;directed[a,b]+=1
    area+=np.linalg.norm(np.cross(t[1]-t[0],t[2]-t[0]))*.5
   if max(edges.values())>2:continue
   # Opposite internal edge directions prove a consistently oriented patch;
   # positive triangle winding plus a single convex boundary excludes folds,
   # overlapping coverage and holes disguised by an equal total area.
   if any(count==2 and (directed[a,b]!=1 or directed[b,a]!=1) for (a,b),count in edges.items()):continue
   boundary=[e for e,c in edges.items() if c==1];adj=collections.defaultdict(list)
   for a,b in boundary:adj[a].append(b);adj[b].append(a)
   if not adj or any(len(v)!=2 for v in adj.values()):continue
   order=[next(iter(adj))];prev=None;cur=order[0]
   for _ in range(len(adj)):
    nex=next(x for x in adj[cur] if x!=prev)
    if nex==order[0]:break
    order.append(nex);prev,cur=cur,nex
   if len(order)!=len(adj):continue
   ps=np.array([coords[k] for k in order])
   # Strip collinear subdivision boundary points.
   while len(ps)>3:
    keep=[]
    for j in range(len(ps)):
     a,b,c=ps[j-1],ps[j],ps[(j+1)%len(ps)]
     if np.linalg.norm(np.cross(b-a,c-b))> .01*np.linalg.norm(c-a):keep.append(j)
    if len(keep)==len(ps) or len(keep)<3:break
    ps=ps[keep]
   if len(ps)>4:continue
   crosses=np.array([np.cross(ps[j]-ps[j-1],ps[(j+1)%len(ps)]-ps[j])@n for j in range(len(ps))])
   if not (np.all(crosses>0) or np.all(crosses<0)):continue
   pa=sum(abs(np.cross(ps[j]-ps[0],ps[j+1]-ps[0])@n)*.5 for j in range(1,len(ps)-1))
   if abs(pa-area)>max(.01,area*.0001) or area<100:continue
   found.append((area,oi,ps))
 return sorted(found,reverse=True,key=lambda p:p[0])


def inset(points, amount=2.0):
    normal = np.cross(points[1]-points[0], points[2]-points[0])
    normal /= np.linalg.norm(normal)
    u = points[1]-points[0]; u /= np.linalg.norm(u)
    v = np.cross(normal, u)
    xy = np.array([[np.sum((p-points[0])*u), np.sum((p-points[0])*v)] for p in points])
    edges = np.roll(xy, -1, axis=0)-xy
    inward = np.array([[-e[1], e[0]] for e in edges])
    inward /= np.linalg.norm(inward, axis=1)[:,None]
    limit = np.sum(inward*xy, axis=1)+amount
    result = []
    for i in range(len(xy)):
        a,b=inward[i-1],inward[i]; det=a[0]*b[1]-a[1]*b[0]
        if abs(det)<1e-6:return None
        d,e=limit[i-1],limit[i]
        result.append([(d*b[1]-a[1]*e)/det,(a[0]*e-d*b[0])/det])
    result=np.array(result)
    if np.any(np.einsum('ij,kj->ik',result,inward)<limit-.0001):return None
    out=np.array([points[0]+p[0]*u+p[1]*v for p in result],dtype='<f4')
    # Quantization stays inside the original polygon by a full unit.
    check=np.array([[np.sum((p-points[0])*u),np.sum((p-points[0])*v)] for p in out])
    if np.any(np.einsum('ij,kj->ik',check,inward)<limit-1):return None
    area=sum(np.cross(out[j]-out[0],out[j+1]-out[0]) for j in range(1,len(out)-1))*.5
    if np.linalg.norm(area)<25:return None
    return out,area


def fnv(data, value=2166136261):
    for b in data:value=((value^b)*16777619)&0xffffffff
    return value


def movable_objects(source, data, objects, batches):
    """Only immutable city surfaces may provide permanent occlusion.

    Source kind-9 owners can move or retire after a body contact. Read the
    matching renderer ownership metadata, never infer mobility from size.
    """
    path=Path(source).with_suffix('.jrs')
    if not path.exists():return set()
    metadata=path.read_bytes()
    if len(metadata)<32:raise ValueError('truncated JRS1 ownership metadata')
    magic,stage,sectors,records,near,no,nb,key=struct.unpack_from('<4s7I',metadata)
    if magic!=b'JRS1' or len(metadata)!=32+16*sectors+32*records or (no,nb)!=(len(objects),len(batches)):
        raise ValueError('JRS1 ownership bank shape mismatch')
    if key!=fnv(data[:struct.unpack_from('<I',data,36)[0]]):
        raise ValueError('JRS1 ownership bank fingerprint mismatch')
    excluded=set();movable_records=set()
    for i in range(records):
        r=struct.unpack_from('<4H3fHHfI',metadata,32+16*sectors+32*i)
        if r[8]!=9:continue
        movable_records.add(i+1)
        first,count,far_first,far_count=r[:4]
        if not count or first+count>no or far_first<near or far_first+far_count>no or not math.isfinite(r[9]) or r[9]<0 or (far_count and r[9]<=0):
            raise ValueError('invalid movable owner geometry range')
        for start,length,flags in ((first,count,0),(far_first,far_count,2)):
            for owner in range(start,start+length):
                if objects[owner][6]&2!=flags or objects[owner][7]!=i+1:
                    raise ValueError('movable owner does not match JGS1 object')
                excluded.add(owner)
    if any(o[7] in movable_records and owner not in excluded for owner,o in enumerate(objects)):
        raise ValueError('movable object is outside its owner geometry range')
    return excluded


def lod_alias_records(source, objects):
    """Identify immutable owners of validated paired near/far JRS records.

    The caller first validates this bank's header and fingerprint through
    movable_objects. This additional range check binds every possible alias
    to the actual source record; coincident unrelated placements cannot alias.
    """
    path=Path(source).with_suffix('.jrs');result=[0]*len(objects)
    if not path.exists():return result
    metadata=path.read_bytes()
    _,_,sectors,records,near,no,_,_=struct.unpack_from('<4s7I',metadata)
    for i in range(records):
        r=struct.unpack_from('<4H3fHHfI',metadata,32+16*sectors+32*i)
        first,count,far_first,far_count=r[:4]
        if r[8]==9 or not count or not far_count:continue
        if first+count>near or far_first<near or far_first+far_count>no or not math.isfinite(r[9]) or r[9]<=0:
            raise ValueError('invalid paired LOD owner geometry range')
        for start,length,flags in ((first,count,0),(far_first,far_count,2)):
            for owner in range(start,start+length):
                if objects[owner][6]&2!=flags or objects[owner][7]!=i+1:
                    raise ValueError('paired LOD owner does not match JGS1 object')
                result[owner]=i+1
    return result


def retained_coverage(path, objects, fingerprint, grid_shape, boxes, group_bytes, group_count, capacity):
    """Retain only the layout of a bank bound to this exact source geometry.

    Its polygon records remain untrusted until the bake independently matches
    each one against a freshly certified immutable source patch below.
    """
    data=Path(path).read_bytes()
    if len(data)<36:raise ValueError('truncated retained visibility bank')
    magic,no,count,key,gx,gz,nx,nz,ng=struct.unpack_from('<4sIIIiiIII',data)
    if magic not in (b'JVO4',b'JVO5') or no!=objects or key!=fingerprint:
        raise ValueError('retained visibility source fingerprint mismatch')
    if (gx,gz,nx,nz)!=grid_shape or ng!=group_count or count>capacity:
        raise ValueError('retained visibility layout exceeds current arena')
    record_start=36+len(boxes);grid_start=record_start+count*80;grid_end=grid_start+nx*nz*128
    if len(data)!=grid_end+len(group_bytes) or data[36:record_start]!=boxes or data[grid_end:]!=group_bytes:
        raise ValueError('retained visibility boxes or groups differ from source')
    grid=data[grid_start:grid_end]
    if any(i!=65535 and i>=count for i in struct.unpack('<'+'H'*(len(grid)//2),grid)):
        raise ValueError('retained visibility candidate index is invalid')
    records=[]
    for i in range(count):
        record=data[record_start+i*80:record_start+(i+1)*80]
        owner,encoded=struct.unpack_from('<HH',record,76)
        if owner>=objects or encoded&7 not in (3,4) or (magic==b'JVO4' and encoded not in (3,4)) or (encoded>>3)>objects:
            raise ValueError('retained visibility owner or polygon count is invalid')
        records.append(record[:78]+struct.pack('<H',encoded&7))
    return records,grid


def bake(source, output, split_components=False, coverage_from=None, max_occluders=None):
    data,objects,batches,vertices,indices=load(source)
    moving=movable_objects(source,data,objects,batches)
    paired_records=lod_alias_records(source,objects)
    header=struct.unpack_from('<6I',data,24); off_vert,off_idx=header[3:5]
    fingerprint=fnv(data[:off_vert])
    for vo,io,nv,ni,_,_ in batches:
        fingerprint=fnv(data[off_vert+vo*24:off_vert+(vo+nv)*24],fingerprint)
        fingerprint=fnv(data[off_idx+io*2:off_idx+(io+ni)*2],fingerprint)
    # Fingerprints bind the stored local/shared payload. Spatial evidence
    # must use each object's authored instance placement in world space.
    batches,vertices=expand_world_vertices(data,objects,batches,vertices)
    boxes=bytearray(); box_centers=[]; box_extents=[]
    for obj in objects:
        parts=[vertices[b[0]:b[0]+b[2]] for b in batches[obj[4]:obj[4]+obj[5]] if b[2]]
        vv=np.concatenate(parts) if parts else np.zeros((1,3))
        assert np.isfinite(vv).all()
        low,high=vv.min(0),vv.max(0)
        center=np.array((low+high)*.5,dtype='<f4')
        extent=np.array(np.maximum(high-center,center-low)+.1,dtype='<f4')
        assert np.all(vv>=center-extent) and np.all(vv<=center+extent)
        boxes+=struct.pack('<6f',*center,*extent)
        box_centers.append(center);box_extents.append(extent)
    # Nearby objects share a conservative sphere rejection before individual
    # box/cone tests. This changes test cost only, never geometry ordering.
    bc=np.array(box_centers);be=np.array(box_extents);groups=bytearray();members=[]
    def group(ids):
        nonlocal groups
        if len(ids)>8:
            axis=int(np.argmax(np.ptp(bc[ids],axis=0)))
            ids=ids[np.argsort(bc[ids,axis],kind='stable')];middle=len(ids)//2
            group(ids[:middle]);group(ids[middle:]);return
        lo=(bc[ids]-be[ids]).min(0);hi=(bc[ids]+be[ids]).max(0)
        center=np.array((lo+hi)*.5,dtype='<f4')
        radius=float(np.linalg.norm(np.maximum(abs(lo-center),abs(hi-center))))+.2
        groups+=struct.pack('<4fHH',*center,radius,len(members),len(ids));members.extend(ids)
    group(np.arange(len(objects)))
    group_bytes=bytes(groups)+struct.pack('<'+'H'*len(members),*members)
    # A coarse camera-cell index avoids scanning the entire city's occluders
    # each frame. It selects candidates only; every rejection is still proven
    # against the exact live camera and object box at run time.
    centers=np.array([o[:3] for o in objects if 0<o[3]<1000])
    gx,gz=np.floor(centers[:,(0,2)].min(0)/512).astype(int)-1
    mx,mz=np.floor(centers[:,(0,2)].max(0)/512).astype(int)+1
    nx,nz=int(mx-gx+1),int(mz-gz+1)
    grid_bytes=nx*nz*64*2
    # Reserve per-object near-depth scratch at the tail of the same arena.
    capacity=min(1024,(VISIBILITY_BYTES-36-len(boxes)-4*len(objects)-grid_bytes-len(group_bytes))//80)
    if max_occluders is not None:
        if max_occluders<=0:raise ValueError('occluder limit must be positive')
        capacity=min(capacity,max_occluders)
    if capacity<=0:raise ValueError('object boxes exceed bounded visibility arena')
    retained=None
    if coverage_from is not None:
        retained=retained_coverage(coverage_from,len(objects),fingerprint,(gx,gz,nx,nz),
                                   boxes,group_bytes,len(groups)//20,capacity)
        required=set(retained[0]);verified={}
    certified=patches(objects,batches,vertices,indices,moving,split_components)
    equivalents=collections.defaultdict(list)
    for _,owner,points in certified:
        equivalents[tuple(sorted(tuple(p) for p in points))].append(owner)
    selected=[];seen=set();candidate_centers=[];candidate_areas=[];aliased=0
    for area,owner,points in certified:
        key=tuple(sorted(tuple(p) for p in points))
        if retained is None and key in seen:continue
        seen.add(key)
        inner=inset(points)
        if inner is None:continue
        points,area_vector=inner
        count=len(points)
        alternate=None
        if paired_records[owner]:
            alternate=next((other for other in equivalents[key]
                if other<=8190 and paired_records[other]==paired_records[owner]
                and (objects[other][6]&2)!=(objects[owner][6]&2)),None)
        # JVO5 keeps the 80-byte record: low three bits retain the polygon
        # count; high thirteen bits encode an optional opposite-LOD owner+1.
        # Geometry is stored only once and consumes one candidate-grid slot.
        encoded_count=count if alternate is None else count|((alternate+1)<<3)
        center=np.array(points.mean(axis=0),dtype='<f4')
        radius=float(np.linalg.norm(points-center,axis=1).max())+.1
        if count==3:points=np.concatenate((points,points[:1]))
        packed=struct.pack('<19fHH',*points.flatten(),*area_vector,*center,radius,owner,encoded_count)
        if retained is not None:
            proof=packed[:78]+struct.pack('<H',count)
            if proof in required:verified[proof]=packed
            if len(verified)==len(required):break
            continue
        selected.append(packed)
        if alternate is not None:aliased+=1
        candidate_centers.append(center);candidate_areas.append(area_vector)
        if len(selected)==capacity:break
    if retained is not None:
        if len(verified)!=len(required):raise ValueError('retained occluder is not a certified immutable source patch')
        selected=[verified[record] for record in retained[0]];grid=retained[1]
        aliased=sum(bool(struct.unpack_from('<H',record,78)[0]>>3) for record in selected)
    else:
        grid=bytearray();cc=np.array(candidate_centers);aa=np.array(candidate_areas)
        for z in range(gz,gz+nz):
            for x in range(gx,gx+nx):
                best=np.zeros(len(selected))
                for fx in (0,.5,1):
                    for fz in (0,.5,1):
                        for y in (-128,0,64,192,384,768):
                            delta=cc-np.array([(x+fx)*512,y,(z+fz)*512])
                            d2=np.maximum(25,np.sum(delta*delta,axis=1));facing=np.sum(aa*delta,axis=1)
                            best=np.maximum(best,(facing/d2)**2/d2)
                ids=list(np.argsort(-best,kind='stable')[:64]);ids += [65535]*(64-len(ids))
                grid+=struct.pack('<64H',*ids)
    result=struct.pack('<4sIIIiiIII',b'JVO5',len(objects),len(selected),fingerprint,gx,gz,nx,nz,len(groups)//20)+boxes+b''.join(selected)+grid+group_bytes
    assert len(result)+4*len(objects)<=VISIBILITY_BYTES
    Path(output).write_bytes(result)
    report={'source':str(source),'source_sha256':hashlib.sha256(data).hexdigest(),
            'output':str(output),'bytes':len(result),'objects':len(objects),
            'occluders':len(selected),'fingerprint':f'{fingerprint:08x}',
            'object_groups':len(groups)//20,'candidate_cells':nx*nz,'candidates_per_cell':64,
            'sha256':hashlib.sha256(result).hexdigest(),'inset_world_units':2.0,
            'movable_objects_excluded':len(moving),'opposite_lod_aliases':aliased,
            'split_components':split_components,'coverage_from':str(coverage_from) if coverage_from is not None else None}
    return report


if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('source',type=Path);ap.add_argument('output',type=Path)
    ap.add_argument('--report',type=Path)
    components=ap.add_mutually_exclusive_group()
    components.add_argument('--split-components',action='store_true',help='experimentally certify disconnected coplanar components independently')
    components.add_argument('--keep-plane-groups',action='store_true',help='explicitly retain the default original whole-plane certification')
    ap.add_argument('--coverage-from',type=Path,help='retain a source-matching bank layout after re-certifying every blocker')
    ap.add_argument('--max-occluders',type=int,help='retain the tested port candidate budget while certifying patches from source')
    args=ap.parse_args();report=bake(args.source,args.output,args.split_components,args.coverage_from,args.max_occluders)
    text=json.dumps(report,indent=2)+'\n'
    if args.report:args.report.write_text(text)
    print(text,end='')
