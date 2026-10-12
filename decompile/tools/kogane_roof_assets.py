#!/usr/bin/env python3
"""Restore Kogane roof/house models from the original 0393a4 placements.

The old export used Rx/Ry/Rz and omitted the placement scale. Accept only
verified old or corrected source geometry; preserve UVs, colors, materials,
indices and object IDs. Repair both LODs and rebuild their visibility bounds.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

import numpy as np
import ninjalib as nl
from stage_render_assets import Source, TABLES, SB, placement_geometry, kogane_roof_placement
from stage_visibility_assets import fnv, bake as visibility_bake

ROOT=Path(__file__).resolve().parents[2]
VERTEX=np.dtype([('uv','<f4',2),('color','<u4'),('xyz','<f4',3)])

def repair(bank):
 source=Source(2);original=bank.read_bytes();data=bytearray(original)
 metadata=bytearray(bank.with_suffix('.jrs').read_bytes())
 magic,stage,sectors,nrecords,no,_,_,fingerprint=struct.unpack_from('<4s7I',metadata)
 nt,objects,nb,nv,ni,ot,oo,ob,ov,oi,op=struct.unpack_from('<11I',data,4)
 assert magic==b'JRS1' and stage==2 and fingerprint==fnv(data[:ov])
 records={}
 for i in range(nrecords):
  r=struct.unpack_from('<4H3fHHfI',metadata,32+sectors*16+i*32)
  records[r[-1]]=r[:4]
 texmap=json.loads((ROOT/'decompile/stage2_texmap.json').read_text())['texmap']
 table,near,far,stride,count,_=TABLES[2]['IT']
 placements=[r for r in source.records(table,sectors,stride,count) if kogane_roof_placement(r['pid'],r)]
 assert len(placements)==9
 nl.MAXEDGE=120.;nl.BAKED_LIGHT=None
 report=[];changed=[];written={}
 for r in placements:
  rx,ry,rz=[a*nl.A2R for a in r['ang']]
  old=nl.mmul(nl.mtrans(*r['pos']),nl.mmul(nl.mrotx(rx),nl.mmul(nl.mroty(ry),nl.mrotz(rz))))
  new,post=placement_geometry(2,'IT',r['pid'],r['pos'],r['ang'],r)
  ids=records[r['address']];delta=0.;used=[]
  for lod,root_table in enumerate((near,far)):
   root=source.words(root_table+r['pid']*4)[0];mapping=texmap[str(root-SB)]
   built=[]
   for matrix,correction in ((old,None),(new,post)):
    builder=nl.JgsBuilder()
    for node,m in nl.walk_tree(source.data,root,SB,matrix):
     if node['model'] and not node['ev']&8:
      builder.add_model(source.data,node['model'],SB,m,lambda slot:mapping[slot],post_transform=correction)
    built.append(builder)
   before,after=built;first,count=ids[lod*2:lod*2+2]
   assert len(before.objs)==len(after.objs)==count
   for local,oid in enumerate(range(first,first+count)):
    used.append(oid)
    actual=struct.unpack_from('<4f4I',data,oo+oid*32)
    a=struct.unpack('<4f4I',before.objs[local]);b=struct.unpack('<4f4I',after.objs[local])
    assert actual[5]==a[5]==b[5]
    for bi,ai,zi in zip(range(actual[4],actual[4]+actual[5]),range(a[4],a[4]+a[5]),range(b[4],b[4]+b[5])):
     dst=struct.unpack_from('<II4H',data,ob+bi*16);aa=before.batches[ai];zz=after.batches[zi]
     assert dst[2:4]==tuple(aa[2:4])==tuple(zz[2:4]), (r,oid,bi,dst[2:4],aa[2:4],zz[2:4])
     assert data[oi+dst[1]*2:oi+(dst[1]+dst[3])*2]==before.idx[aa[1]*2:(aa[1]+aa[3])*2]==after.idx[zz[1]*2:(zz[1]+zz[3])*2]
     av=np.frombuffer(before.verts,dtype=VERTEX,count=aa[2],offset=aa[0]*24)
     zv=np.frombuffer(after.verts,dtype=VERTEX,count=zz[2],offset=zz[0]*24)
     dv=np.frombuffer(data,dtype=VERTEX,count=dst[2],offset=ov+dst[0]*24)
     assert np.array_equal(dv['uv'],av['uv']) and np.array_equal(dv['uv'],zv['uv'])
     assert min(np.max(abs(dv['xyz']-av['xyz'])),np.max(abs(dv['xyz']-zv['xyz'])))<.015,(r,oid,bi)
     delta=max(delta,float(np.max(abs(av['xyz']-zv['xyz']))))
     for vi in range(dst[2]):
      at=ov+(dst[0]+vi)*24+12;value=zv['xyz'][vi].tobytes()
      assert at not in written or written[at]==value,'conflicting shared geometry'
      written[at]=value;data[at:at+12]=value
    # The source builder includes every transformed point in this sphere.
    struct.pack_into('<4f',data,oo+oid*32,*b[:4])
  row=dict(type=r['pid'],address=f'{r["address"]:08x}',sector=r['sector'],position=r['pos'],
           angles=r['ang'],scale=r['scale'],objects=used,max_vertex_change=delta)
  report.append(row)
  if delta>.015:changed.append(row)
 # No other batch may alias a repaired vertex payload.
 repaired={oid for r in report for oid in r['objects']}
 for oid in range(objects):
  if oid in repaired:continue
  first,count=struct.unpack_from('<2I',data,oo+oid*32+16)
  for bi in range(first,first+count):
   vo,_,nv,_,_,_=struct.unpack_from('<II4H',data,ob+bi*16)
   assert not any(ov+(vo+i)*24+12 in written for i in range(nv))
 struct.pack_into('<I',metadata,28,fnv(data[:ov]))
 return bytes(data),bytes(metadata),dict(source_sha256=hashlib.sha256(source.data).hexdigest(),
    placements=report,changed=changed,output_sha256=hashlib.sha256(data).hexdigest())

def main():
 ap=argparse.ArgumentParser(description=__doc__)
 ap.add_argument('--bank',type=Path,default=ROOT/'psp/stage2.jgs')
 ap.add_argument('--check',action='store_true');args=ap.parse_args()
 data,jrs,report=repair(args.bank)
 if args.check:
  assert args.bank.read_bytes()==data
  assert args.bank.with_suffix('.jrs').read_bytes()==jrs
 else:
  args.bank.write_bytes(data);args.bank.with_suffix('.jrs').write_bytes(jrs)
  visibility_bake(args.bank,args.bank.with_suffix('.jvo'))
  args.bank.with_name('kogane_roofs.json').write_text(json.dumps(report,indent=2)+'\n')
 print(f'Kogane roofs: {len(report["placements"])} placements verified, {len(report["changed"])} corrected in both LODs')

if __name__=='__main__':main()
