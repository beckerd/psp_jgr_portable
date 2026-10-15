#!/usr/bin/env python3
"""Recover sector ownership and original distant geometry for an existing JGS.

Near vertices, indices, palettes and pixels are copied from the input, not
requantized. Original near meshes are rebuilt only to verify the provenance
of every placement and texture index. Far meshes use the source NJS trees.
"""
import argparse
import collections
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import subprocess
import tempfile

import numpy as np
import ninjalib as nl
from pvr_decode import decode
from shibuya_prop_types import SHIBUYA_KNOCKABLE_TYPES

ROOT = Path(__file__).resolve().parents[2]
DC = ROOT / 'decompile'
EB, SB = 0x8c010000, 0x8cb00000
# (sector table, near table, far table, stride, count, far texlist table).
# Source 031018/03119c/03125c, 0393a4/0394a8/039558,
# 048358/04846c/048580, 0f45a8/0f4684/0f4760, 0f54d0/0f5604/0f56a4.
TABLES = {
 1: dict(BLD=(0x8c105e98,0x8c1063b4,0x8c1068dc,40,165,0x8c106b70),
         F=(0x8c105f94,0x8c106e0c,0x8c106f9c,40,50,0x8c107064),
         IT=(0x8c106090,0x8c10712c,0x8c1072dc,40,54,0x8c1073b4),
         DYN=(0x8c10618c,0x8c107564,0,40,28,0)),
 2: dict(BLD=(0x8c107ef4,0x8c108cf0,0,40,327,0),
         F=(0x8c1080bc,0x8c109728,0x8c109990,52,77,0x8c109ac4),
         IT=(0x8c107d2c,0x8c1086a0,0x8c1089c8,52,101,0x8c108b5c),
         DYN=(0x8c108284,0x8c10a2dc,0,40,91,0)),
 3: dict(BLD=(0x8c1ba6f0,0x8c1bab40,0x8c1baed8,40,115,0x8c1bb0a4),
         F=(0x8c1ba860,0x8c1bb530,0,52,239,0),
         IT=(0x8c1ba7a8,0x8c1bb270,0,40,88,0),
         DYN=(0x8c1ba9d0,0x8c1bc4cc,0,52,32,0)),
 5: dict(BLD=(0x8c202e28,0x8c2033b4,0x8c20357c,40,56,0x8c203660),
         F=(0x8c203060,0x8c203c74,0x8c203e24,52,35,0x8c203eb4),
         IT=(0x8c202f44,0x8c203744,0x8c2039dc,40,82,0x8c203b28),
         DYN=(0x8c203298,0x8c204084,0,52,20,0)),
 6: dict(BLD=(0x8c20a8f8,0x8c20af4c,0x8c20b114,40,56,0x8c20b1f8),
         F=(0x8c20aa3c,0x8c20b2dc,0,40,1,0),
         IT=(0x8c20ab80,0x8c20b2ec,0x8c20b334,52,8,0x8c20b358))}
SECTORS={1:(63,0x8c174424),2:(114,0x8c17b58c),3:(46,0x8c176e18),5:(71,0x8c1fbadc),6:(81,0x8c201604)}
IDENTITY=nl.placement_matrix((0,0,0),(0,0,0))
# 0393a4 applies full placement TRS. These three additional house instances
# support rooftop tags 3:1, 3:7 and 3:9; omitted scale detached their paint.
KOGANE_TAG_ROOFS=(0x8cd6b198,0x8cd6bbb8,0x8cd6bb84)
def kogane_roof_placement(pid,record):
 return pid in (87,88) or (record and record['address'] in KOGANE_TAG_ROOFS)

def powerline_placement(stage,tag,pid):
 # 039558 / 048580: overhead wires and utility poles use full record TRS.
 return tag=='F' and ((stage==2 and (23<=pid<=27 or pid==70)) or
                      (stage==3 and pid in (7,95)))

class Source:
 def __init__(self, stage):
  self.exe=(DC/'extracted/1ST_READ.BIN').read_bytes()
  self.data=(DC/f'afs/STAGE{stage}/000_st{stage}dat.bin').read_bytes()
  # Some original far slots point to executable-resident empty NJS trees.
  self.image=bytearray(SB-EB+len(self.data));self.image[:len(self.exe)]=self.exe;self.image[SB-EB:]=self.data
 def words(self,a,n=1):return struct.unpack_from('<'+'I'*n,self.image,a-EB)
 def float(self,a):return struct.unpack_from('<f',self.image,a-EB)[0]
 def valid(self,a):return EB<=a<EB+len(self.exe) or SB<=a<SB+len(self.data)
 def records(self, table, sectors, stride, count):
  records=[]
  for sector in range(sectors):
   p=self.words(table+sector*4)[0]
   if not SB<=p<SB+len(self.data):continue
   pointers=SB<=self.words(p)[0]<SB+len(self.data)
   for _ in range(10000):
    a=self.words(p)[0] if pointers else p
    if pointers and not a:break
    pid=struct.unpack_from('<i',self.image,a-EB)[0]
    if not pointers and pid < -1:break
    if 0<=pid<count:
     pos=struct.unpack_from('<3f',self.image,a+4-EB);ang=struct.unpack_from('<3i',self.image,a+16-EB)
     records.append(dict(sector=sector,pid=pid,pos=pos,ang=ang,address=a,
                         scale=struct.unpack_from('<3f',self.image,a+28-EB) if stride==52 else (1.,1.,1.)))
    p+=4 if pointers else stride
   else:raise ValueError('unterminated placement list')
  return records

def key(tag,pid,pos,ang):return (tag,pid,struct.pack('<3f3i',*pos,*ang))

def placement_for(stage,tag,pid,pos,ang,record):
 # Only audited banks change their legacy placement transform. Kogane's
 # sloped roof pairs use 0393a4: translate, Z/Y/X rotation, record scale.
 # See kogane_roof_assets.py for the lossless near/far geometry migration.
 roof=stage==2 and tag=='IT' and kogane_roof_placement(pid,record)
 powerline=powerline_placement(stage,tag,pid)
 audited=stage==1 or roof or powerline or (stage==3 and tag=='IT' and pid in (7,8,9,22,23,24,25,30,49,59,72,73,74,86))
 if audited:
  matrix=nl.placement_matrix(pos,ang)
 else:
  rx,ry,rz=[a*nl.A2R for a in ang]
  matrix=nl.mmul(nl.mtrans(*pos),nl.mmul(nl.mrotx(rx),nl.mmul(nl.mroty(ry),nl.mrotz(rz))))
 return nl.mmul(matrix,nl.mscale(*record['scale'])) if roof or powerline else matrix

def placement_geometry(stage,tag,pid,pos,ang,record):
 matrix=placement_for(stage,tag,pid,pos,ang,record)
 if stage==2 and tag=='IT' and record and record['address'] in KOGANE_TAG_ROOFS:
  # Keep the shipped subdivision and paint/UV interpolation. Applying the
  # source scale after splitting restores geometry without more triangles.
  rx,ry,rz=[a*nl.A2R for a in ang]
  old=nl.mmul(nl.mtrans(*pos),nl.mmul(nl.mrotx(rx),nl.mmul(nl.mroty(ry),nl.mrotz(rz))))
  return old,nl.mmul(matrix,nl.minv(old))
 return matrix,None

def placement_light_transform(stage,tag,pid,pos,ang,record,matrix):
 # Geometry-only corrections intentionally retained the shipping vertex
 # colors. Fresh-disc generation must shade with that historical transform,
 # including both LODs, while positions follow the corrected source matrix.
 changed=powerline_placement(stage,tag,pid) or (stage==2 and tag=='IT' and kogane_roof_placement(pid,record)) or (stage==3 and tag=='IT' and pid in (7,8,9,22,23,24,25,30,49,59,72,73,74,86))
 if not changed:return None
 rx,ry,rz=[a*nl.A2R for a in ang]
 old=nl.mmul(nl.mtrans(*pos),nl.mmul(nl.mrotx(rx),nl.mmul(nl.mroty(ry),nl.mrotz(rz))))
 return nl.mmul(old,nl.minv(matrix))

def tree(source, root, matrix):
 """Original 0c9e64/234fda hierarchy, including eval-break flag 0x10."""
 stack=[(root,matrix)];visited=set()
 while stack:
  address,parent=stack.pop()
  if not address:continue
  assert source.valid(address) and address not in visited, hex(address)
  visited.add(address);node=nl.read_object(source.image,address,EB)
  m=nl.mmul(parent,nl.node_matrix(node['ev'],node['pos'],node['ang'],node['scl']))
  yield node,m
  if node['sib']:stack.append((node['sib'],parent))
  if node['child'] and not node['ev']&16:stack.append((node['child'],m))

def rule(stage,tag,sector,pid,source):
 # kind: 0 no placement cutoff, 1 standard circle, 2 large circle,
 # 3 small circle, 4 Shibuya circle+wedge, 5 Shibuya item distance+wedge;
 # 6/7 special architecture, 8 Kogane background, 9 generic Shibuya props.
 kind,lod=0,0.
 if stage==1:
  kind={'BLD':4,'F':0,'IT':5}.get(tag,0);lod={'BLD':360000.,'F':360000.,'IT':90000.}.get(tag,0.)
  if tag=='DYN' and pid in SHIBUYA_KNOCKABLE_TYPES:kind=9
 elif stage==2:
  kind={'BLD':2,'F':1,'IT':1}.get(tag,0);lod=90000. if tag in ('F','IT') else 0.
 elif stage==3:
  if tag in ('BLD','F'):
   fn=source.words(0x8c17f958+sector*4)[0];kind={0:0,0x8c0c3498:1,0x8c0c34c0:2,0x8c0c3470:3}[fn]
  if tag=='BLD':lod=source.float(0x8c17fac8+sector*4)
 elif stage==5:
  if tag in ('BLD','IT','F'):lod=source.float({'BLD':0x8c2045f8,'IT':0x8c2045fc,'F':0x8c204600}[tag])**2
 elif stage==6:
  kind=(0 if sector==0 else 2 if pid<4 else 1) if tag=='BLD' else 1 if tag=='IT' else 0
  if tag in ('BLD','IT'):lod=source.float(0x8c20bb48 if tag=='BLD' else 0x8c20bb50)**2
 return kind,lod

def refresh_prop_visibility(input_path,output_path):
 """Update only verified handler tags in a matching, already baked JRS1.

 The resident city geometry and its fingerprint are unchanged. Reading the
 original placement lists keeps this migration keyed to source addresses,
 rather than guessing from geometry shape or applying a rule to all DYN.
 """
 from stage_visibility_assets import fnv
 data=input_path.read_bytes();meta=bytearray(input_path.with_suffix('.jrs').read_bytes())
 magic,stage,ns,nr,near,no,nb,key=struct.unpack_from('<4s7I',meta)
 if magic!=b'JRS1' or stage!=1 or ns!=SECTORS[1][0]:raise ValueError('Shibuya JRS1 required')
 if len(meta)!=32+16*ns+32*nr or (no,nb)!=struct.unpack_from('<2I',data,8):raise ValueError('JRS1 bank shape mismatch')
 if fnv(data[:struct.unpack_from('<I',data,36)[0]])!=key:raise ValueError('JRS1 bank fingerprint mismatch')
 source=Source(1);table,_,_,stride,count,_=TABLES[1]['DYN']
 wanted={r['address']:r for r in source.records(table,ns,stride,count) if r['pid'] in SHIBUYA_KNOCKABLE_TYPES}
 found=set();changed=0
 for i in range(nr):
  offset=32+16*ns+32*i;r=struct.unpack_from('<4H3fHHfI',meta,offset)
  original=wanted.get(r[-1])
  if original is None:
   if r[8]==9:raise ValueError('unverified kind-9 placement')
   continue
  if r[7]!=original['sector'] or r[4:7]!=original['pos'] or not r[1] or r[8] not in (0,9) or r[9]!=0:
   raise ValueError('generic prop metadata does not match source')
  found.add(r[-1]);changed+=r[8]!=9;struct.pack_into('<H',meta,offset+22,9)
 if found!=set(wanted):raise ValueError('generic prop placement missing from bank')
 output_path.parent.mkdir(parents=True,exist_ok=True);output_path.write_bytes(meta)
 return dict(stage=1,placements=len(found),changed=changed,metadata_bytes=len(meta),
             output_sha256=hashlib.sha256(meta).hexdigest(),geometry_sha256=hashlib.sha256(data).hexdigest())

def retire_handler_roots(data, side, roots):
 """Retire legacy identity copies of models placed by private handlers.

 Keep object/batch IDs stable for near-bank provenance and sidecars. The
 geometry compactor removes these now-unused vertex/index payloads.
 """
 result=bytearray(data);oo,ob=struct.unpack_from('<2I',data,28);retired=[]
 for i,(off,kind,_) in enumerate(side):
  if kind!='root' or off not in roots:continue
  first,count=struct.unpack_from('<2I',result,oo+i*32+16)
  for j in range(first,first+count):struct.pack_into('<2H',result,ob+j*16+8,0,0)
  struct.pack_into('<I',result,oo+i*32+20,0);retired.append(i)
 return bytes(result),retired

def append_jgs(original,far,tex_ids,new_textures,metadata,output):
 nt,no,nb,nv,ni=struct.unpack_from('<5I',original,4);ot,oo,ob,ov,oi,op=struct.unpack_from('<6I',original,24)
 objects=bytearray(original[oo:oo+no*32]);batches=bytearray(original[ob:ob+nb*16]);pixels=bytearray(original[op:])
 textures=[list(struct.unpack_from('<4H2I',original,ot+i*16)) for i in range(nt)]
 for t in textures:t[4]-=op;t[5]-=op
 for recid,r in enumerate(metadata):
  for i in range(r['near_first'],r['near_first']+r['near_count']):struct.pack_into('<I',objects,i*32+28,recid+1)
 for o,recid in zip(far.objs,far.record_ids):
  v=list(struct.unpack('<4f4I',o));v[4]+=nb;v[6]=2;v[7]=recid+1;objects+=struct.pack('<4f4I',*v)
 for t,blob in new_textures:
  textures.append([*t[:4],len(pixels),len(pixels)+1024]);pixels+=blob
 for vo,io,vcount,icount,tex,flags in far.batches:
  ti=tex_ids.get(tex,0)
  if tex!=0xffff and textures[ti][2]&1:flags|=1
  batches+=struct.pack('<II4H',vo+nv,io+ni,vcount,icount,ti,flags)
 counts=(len(textures),no+len(far.objs),nb+len(far.batches),nv+far.nv,ni+far.ni)
 sections=[b'',bytes(objects),bytes(batches),original[ov:ov+nv*24]+far.verts,original[oi:oi+ni*2]+far.idx,bytes(pixels)]
 sizes=[16*len(textures),*map(len,sections[1:])];offsets=[];offset=80
 for size in sizes:offsets.append(offset);offset=(offset+size+15)&~15
 sections[0]=b''.join(struct.pack('<4H2I',*t[:4],t[4]+offsets[5],t[5]+offsets[5]) for t in textures)
 result=bytearray(b'JGS1'+struct.pack('<5I6I',*counts,*offsets)+original[48:72])
 for off,section in zip(offsets,sections):result+=b'\0'*(off-len(result));result+=section
 output.write_bytes(result)
 return counts

def compact_geometry(path):
 """Share byte-identical batch payloads and aligned contained slices.

 GU batch order, vertices, indices, textures and bounds are unchanged.
 No quantization, remeshing or runtime decompression is involved.
 """
 d=path.read_bytes();counts=list(struct.unpack_from('<5I',d,4))
 ot,oo,ob,ov,oi,op=struct.unpack_from('<6I',d,24)
 batches=[list(struct.unpack_from('<II4H',d,ob+i*16)) for i in range(counts[2])]
 payloads=[]
 for kind,stride,base in ((0,24,ov),(1,2,oi)):
  spans=[d[base+b[kind]*stride:base+(b[kind]+b[kind+2])*stride] for b in batches]
  offsets={};packed=bytearray()
  for blob in sorted(set(spans),key=lambda b:(-len(b),b)):
   at=packed.find(blob)
   while at>=0 and at%stride:at=packed.find(blob,at+1)
   if at<0:at=len(packed);packed+=blob
   offsets[blob]=at//stride
  for b,blob in zip(batches,spans):b[kind]=offsets[blob]
  counts[3+kind]=len(packed)//stride;payloads.append(packed)
 new_oi=(ov+len(payloads[0])+15)&~15;new_op=(new_oi+len(payloads[1])+15)&~15
 result=bytearray(d[:ov]);struct.pack_into('<5I',result,4,*counts);struct.pack_into('<II',result,40,new_oi,new_op)
 for i,b in enumerate(batches):struct.pack_into('<II4H',result,ob+i*16,*b)
 for i in range(counts[0]):
  clut,pixels=struct.unpack_from('<II',result,ot+i*16+8)
  struct.pack_into('<II',result,ot+i*16+8,clut+new_op-op,pixels+new_op-op)
 for offset,blob in ((ov,payloads[0]),(new_oi,payloads[1]),(new_op,d[op:])):
  result+=b'\0'*(offset-len(result));result+=blob
 path.write_bytes(result)
 return counts,len(d)-len(result)

def bake(stage,input_path,output_dir):
 source=Source(stage);layout=json.loads((DC/f'stage{stage}_layout.json').read_text())
 tm=json.loads((DC/f'stage{stage}_texmap.json').read_text());texmap={int(k):v for k,v in tm['texmap'].items()}
 with tempfile.TemporaryDirectory(prefix='jgr-render-texmap-') as tmp:
  full=Path(tmp)/'texmap.json'
  subprocess.run([sys.executable,'tools/static_texmap.py','extracted/1ST_READ.BIN',str(stage),str(full)],cwd=DC,check=True,stdout=subprocess.DEVNULL)
  fulltm=json.loads(full.read_text())
 assert fulltm['packs']==tm['packs'];lists={int(k):v for k,v in fulltm.get('texlists',{}).items()}
 fullmap={int(k):v for k,v in fulltm['texmap'].items()}
 data=input_path.read_bytes();nt,no,nb,nv,ni=struct.unpack_from('<5I',data,4);ot,oo,ob,ov,oi,op=struct.unpack_from('<6I',data,24)
 side=json.loads(Path(str(input_path)+'.map.json').read_text());assert len(side)==no
 # Rebuilding an already annotated bank must replace its appended geometry,
 # not mistake the previous far meshes for additional identity placements.
 previous_far={}
 jrs=input_path.with_suffix('.jrs')
 if jrs.exists():
  from stage_visibility_assets import fnv
  h=struct.unpack_from('<4s7I',jrs.read_bytes())
  assert h[0]==b'JRS1' and h[1]==stage and h[5:7]==(no,nb) and h[7]==fnv(data[:ov])
  metadata=jrs.read_bytes()
  for i in range(h[3]):
   r=struct.unpack_from('<4H3fHHfI',metadata,32+h[2]*16+i*32)
   previous_far[r[-1]]=r[2:4]
  no=h[4];side=side[:no]
  nb=max(struct.unpack_from('<2I',data,oo+i*32+16)[0]+struct.unpack_from('<I',data,oo+i*32+20)[0] for i in range(no))
  data=bytearray(data);struct.pack_into('<II',data,8,no,nb);data=bytes(data)
 objects=[struct.unpack_from('<4f4I',data,oo+i*32) for i in range(no)]
 batches=[struct.unpack_from('<II4H',data,ob+i*16) for i in range(nb)]
 vertex_dtype=np.dtype([('uv','<f4',2),('color','<u4'),('xyz','<f4',3)])
 oldv=np.frombuffer(data,dtype=vertex_dtype,count=nv,offset=ov)
 ns,neighbour_table=SECTORS[stage];record_lookup=collections.defaultdict(collections.deque)
 for tag,(table,near,far,stride,count,fartex) in TABLES[stage].items():
  for r in source.records(table,ns,stride,count):record_lookup[key(tag,r['pid'],r['pos'],r['ang'])].append(r)
 placements=collections.defaultdict(list)
 for tag,pid,pos,ang in layout['inst']:
  root=layout['proto'][tag][pid]
  if root<0:continue
  q=record_lookup.get(key(tag,pid,pos,ang));r=q.popleft() if q else None
  # Legacy GRP roots have no renderable source category; retain them.
  if tag in TABLES[stage]:assert r is not None,(stage,tag,pid,pos)
  placements[root].append((tag,pid,pos,ang,r))
 root_objects=collections.defaultdict(list)
 for i,(off,kind,_) in enumerate(side):root_objects[off,kind].append(i)
 records=[];tex_ids={};far=nl.JgsBuilder();far.record_ids=[];map_far=[];near_verified=0
 def reuse_far_textures(address,start):
  if address not in previous_far:return
  old_first,count=previous_far[address]
  assert count==len(far.objs)-start
  for old_id,new in zip(range(old_first,old_first+count),far.objs[start:]):
   original=struct.unpack_from('<4f4I',data,oo+old_id*32);replacement=struct.unpack('<4f4I',new)
   assert original[5]==replacement[5]
   for oi0,ni0 in zip(range(original[4],original[4]+original[5]),range(replacement[4],replacement[4]+replacement[5])):
    old=struct.unpack_from('<II4H',data,ob+oi0*16);new=far.batches[ni0]
    assert old[2:4]==tuple(new[2:4])
    if new[4]!=0xffff:
     assert new[4] not in tex_ids or tex_ids[new[4]]==old[4]
     tex_ids[new[4]]=old[4]
 nl.MAXEDGE=120.;nl.BAKED_LIGHT='shibuya' if stage==1 else None
 def mapped(mapping,slot):
  assert mapping is not None and slot<len(mapping) and mapping[slot]>=0,('unresolved source texture',stage,slot,hex(off),tag,pid,hex(node['addr']))
  return mapping[slot]
 for (off,kind),object_ids in root_objects.items():
  ps=placements[off] if kind=='proto' else [(None,0,(0,0,0),(0,0,0),None)]
  cursor=0
  for tag,pid,pos,ang,record in ps:
   first=cursor;temp=nl.JgsBuilder();matrix,post=placement_geometry(stage,tag,pid,pos,ang,record)
   light_transform=placement_light_transform(stage,tag,pid,pos,ang,record,matrix)
   # Replay the legacy converter's exact hierarchy for provenance checking.
   nodes=nl.walk_tree(source.data,SB+off,SB,matrix) if kind!='model' else [(dict(model=SB+off,ev=0,addr=SB+off),matrix)]
   for node,m in nodes:
    if node['model'] and not node['ev']&8:
     mapping=fullmap.get(node['addr']-SB,fullmap.get(off))
     temp.add_model(source.data,node['model'],SB,m,lambda s: mapped(mapping,s),twosided=(tag=='F'),post_transform=post,light_matrix=nl.mmul(light_transform,m) if light_transform is not None else None)
   for t in temp.objs:
    assert cursor<len(object_ids),(stage,off,cursor,len(object_ids))
    oid=object_ids[cursor];cursor+=1;o=objects[oid];tn=struct.unpack('<4f4I',t)
    assert o[5]==tn[5],(stage,off,oid,o,tn)
    for oldbi,newbi in zip(range(o[4],o[4]+o[5]),range(tn[4],tn[4]+tn[5])):
     a,b=batches[oldbi],temp.batches[newbi];assert a[2:4]==tuple(b[2:4]),(stage,off,oid,a,b)
     vv=np.frombuffer(temp.verts,dtype=vertex_dtype,count=b[2],offset=b[0]*24);ovv=oldv[a[0]:a[0]+a[2]]
     assert np.max(abs(vv['xyz']-ovv['xyz']),initial=0)<.015 and np.array_equal(vv['uv'],ovv['uv']),(stage,hex(off),oid,'geometry changed',tag,pid,pos,ang,float(np.max(abs(vv['xyz']-ovv['xyz']))),bool(np.array_equal(vv['uv'],ovv['uv'])))
     assert data[oi+a[1]*2:oi+(a[1]+a[3])*2]==temp.idx[b[1]*2:(b[1]+b[3])*2]
     if b[4]!=0xffff:
      assert b[4] not in tex_ids or tex_ids[b[4]]==a[4],(stage,b[4],a[4],tex_ids.get(b[4]))
      tex_ids[b[4]]=a[4]
    near_verified+=1
   if record is None or cursor==first:continue
   ids=object_ids[first:cursor];assert ids==list(range(ids[0],ids[-1]+1))
   ri=len(records);kind_id,lod=rule(stage,tag,record['sector'],pid,source)
   entry=dict(near_first=ids[0],near_count=len(ids),far_first=no+len(far.objs),far_count=0,pos=pos,
              sector=record['sector'],kind=kind_id,lod=lod,tag=tag,pid=pid,address=record['address'])
   records.append(entry)
   table,near,far_table,stride,count,fartex=TABLES[stage][tag]
   if not far_table:entry['lod']=0.;continue
   farroot=source.words(far_table+pid*4)[0];nearroot=source.words(near+pid*4)[0]
   if farroot==nearroot:entry['lod']=0.;continue
   rootlist=lists.get(source.words(fartex+pid*4)[0])
   n0=len(far.objs)
   for node,m in tree(source,farroot,matrix):
    if not node['model'] or node['ev']&8:continue
    mapping=rootlist
    far.add_model(source.image,node['model'],EB,m,lambda s:mapped(mapping,s),twosided=(tag=='F'),post_transform=post,light_matrix=nl.mmul(light_transform,m) if light_transform is not None else None)
   reuse_far_textures(record['address'],n0)
   entry['far_count']=len(far.objs)-n0
   far.record_ids += [ri]*entry['far_count'];map_far += [[farroot-SB,'far',1]]*entry['far_count']
  assert cursor==len(object_ids),(stage,off,kind,cursor,len(object_ids))
 handler_roots=set()
 if stage==1:
  # 030f80: extra architecture in area 3. 031110: the special player
  # volume's bridge, followed by sector 3 and its neighbors (030df4).
  for pointer,near,tex_table,kind in ((0x8c105e54,0x8c1063b4,source.words(0x8c030ffc)[0],6),
                                    (0x8c105e58,0x8c106e04,source.words(0x8c031180)[0],7)):
   for record in source.records(pointer,1,40,165 if kind==6 else 2):
    pid=record['pid'];root=source.words(near+pid*4)[0];mapping=lists[source.words(tex_table+pid*4)[0]]
    handler_roots.add(root-SB)
    n0=len(far.objs);ri=len(records)
    for node,m in tree(source,root,nl.placement_matrix(record['pos'],record['ang'])):
     if node['model'] and not node['ev']&8:far.add_model(source.image,node['model'],EB,m,lambda slot:mapped(mapping,slot))
    reuse_far_textures(record['address'],n0)
    count=len(far.objs)-n0
    records.append(dict(near_first=0,near_count=0,far_first=no+n0,far_count=count,pos=record['pos'],
                        sector=0,kind=kind,lod=0.,tag='extra',pid=pid,address=record['address']))
    far.record_ids += [ri]*count;map_far += [[root-SB,'extra',1]]*count
 if stage==2:
  # 039224 -> 0392f8: global IT placements, gated by 2e2cd4 (the sky
  # enable bit). These are the elevated highway, supports and skyline;
  # they have no sector ownership and must not inherit IT's near cutoff.
  for record in source.records(0x8c107ce8,1,52,101):
   pid=record['pid'];root=source.words(0x8c1086a0+pid*4)[0]
   mapping=lists[source.words(0x8c108834+pid*4)[0]]
   matrix=nl.mmul(nl.placement_matrix(record['pos'],record['ang']),nl.mscale(*record['scale']))
   n0=len(far.objs);ri=len(records)
   for node,m in tree(source,root,matrix):
    if node['model'] and not node['ev']&8:far.add_model(source.image,node['model'],EB,m,lambda slot:mapped(mapping,slot))
   reuse_far_textures(record['address'],n0)
   count=len(far.objs)-n0
   records.append(dict(near_first=0,near_count=0,far_first=no+n0,far_count=count,pos=record['pos'],
                       sector=0,kind=8,lod=0.,tag='background',pid=pid,address=record['address']))
   far.record_ids += [ri]*count;map_far += [[root-SB,'background',1]]*count
 # Textures already present keep their exact palettes/pixels. Only source
 # textures used exclusively by a far mesh are quantized for the first time.
 wanted=sorted(far.used-set(tex_ids));images=[]
 if wanted:
  for pack in tm['packs']:
   raw=(DC/pack).read_bytes();p=0
   while True:
    p=raw.find(b'GBIX',p)
    if p<0:break
    q=raw.find(b'PVRT',p+8,p+64);length=struct.unpack_from('<I',raw,q+4)[0]
    images.append(decode(raw[q:q+length+8]) if len(images) in wanted else None);p+=8
 extra=[]
 for tex in wanted:
  im=images[tex];w,h=im.size
  while max(w,h)>128:w//=2;h//=2
  w=max(16,w);h=max(8,h);im=im.resize((w,h),nl.Image.Resampling.LANCZOS)
  q=im.quantize(256,method=nl.Image.Quantize.FASTOCTREE);pal=q.getpalette('RGBA');clut=bytes((pal+[0]*1024)[:1024])
  tex_ids[tex]=nt+len(extra);extra.append(((w,h,int(im.getextrema()[3][0]<255),0),clut+nl.swizzle8(q.tobytes(),w,h)))
 output_dir.mkdir(parents=True,exist_ok=True);out=output_dir/f'stage{stage}.jgs'
 # 031110 places its two-entry panorama bank through 105e58. The legacy
 # generic root scan missed this short prototype table and exported the
 # panorama at identity too, leaving a giant wall across Love Trap.
 cleaned,retired=retire_handler_roots(data,side,handler_roots)
 counts=append_jgs(cleaned,far,tex_ids,extra,records,out)
 counts,shared_bytes=compact_geometry(out)
 Path(str(out)+'.map.json').write_text(json.dumps(side+map_far))
 # JRS1: counts/fingerprint, per-sector 128-bit neighbor masks, 32-byte
 # placement records, then object->record mapping already in JgsObj.pad.
 from stage_visibility_assets import fnv
 header=struct.pack('<4s7I',b'JRS1',stage,ns,len(records),no,counts[1],counts[2],fnv(out.read_bytes()[:struct.unpack_from('<I',out.read_bytes(),36)[0]]))
 meta=bytearray(header)
 for sector in range(ns):
  mask=1<<sector;p=source.words(neighbour_table+sector*8+4)[0]
  for _ in range(128):
   v=source.words(p)[0];p+=4
   if v>=0x80000000:break
   assert v<ns;mask|=1<<v
  meta+=mask.to_bytes(16,'little')
 for r in records:
  meta+=struct.pack('<4H3fHHfI',r['near_first'],r['near_count'],r['far_first'],r['far_count'],*r['pos'],r['sector'],r['kind'],r['lod'],r['address'])
 (output_dir/f'stage{stage}.jrs').write_bytes(meta)
 report=dict(stage=stage,source_sha256=hashlib.sha256(data).hexdigest(),output_sha256=hashlib.sha256(out.read_bytes()).hexdigest(),retired_identity_objects=retired,
             shared_geometry_bytes=shared_bytes,near_objects_verified=near_verified,placements=len(records),far_objects=len(far.objs),far_batches=len(far.batches),far_triangles=sum(b[3]//3 for b in far.batches),
             far_geometry_bytes=len(far.verts)+len(far.idx),extra_textures=len(extra),metadata_bytes=len(meta),counts=counts,placements_detail=records)
 (output_dir/f'stage{stage}_render.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({k:v for k,v in report.items() if k!='placements_detail'}),flush=True)

if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('stage',type=int,choices=TABLES);p.add_argument('--input',type=Path);p.add_argument('--out',type=Path,required=True)
 p.add_argument('--prop-visibility-only',action='store_true',help='Refresh Shibuya generic prop rules without rewriting geometry')
 a=p.parse_args();input_path=a.input or ROOT/f'psp/stage{a.stage}.jgs'
 if a.prop_visibility_only:
  if a.stage!=1:p.error('--prop-visibility-only requires stage 1')
  print(json.dumps(refresh_prop_visibility(input_path,a.out/'stage1.jrs')))
 else:bake(a.stage,input_path,a.out)
