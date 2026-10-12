#!/usr/bin/env python3
"""Original Shibuya type-0 barrier, six placements, motion and static owners."""
# These owners use the original normal draw path 234fc6 / mode 1,
# which submits both face directions (unlike the PLAY cel-hull path).

import argparse,hashlib,json,struct,subprocess,sys,tempfile
from pathlib import Path
from stage_render_assets import Source,EB,SB
ROOT=Path(__file__).resolve().parents[2];DEC=ROOT/'decompile'
def generate(out):
 s=Source(1);root=s.words(s.words(0x8c030d1c)[0])[0]
 texlist=s.words(s.words(0x8c030d24)[0])[0];motion=s.words(0x8c105e5c)[0]
 subprocess.run([sys.executable,'tools/static_texmap.py','extracted/1ST_READ.BIN','1',str(out/'textures.json')],cwd=DEC,check=True,stdout=subprocess.DEVNULL)
 tm=json.loads((out/'textures.json').read_text());mapping=tm['texlists'][str(texlist)]
 textures=out/'textures.bin';textures.write_bytes(b''.join((DEC/p).read_bytes() for p in tm['packs']))
 # Index by GBIX entries as the original loader does. PVRT-size scanning
 # skips embedded entries in these packs and is not a valid texture index.
 b=textures.read_bytes();offsets=[];at=0
 while True:
  at=b.find(b'GBIX',at)
  if at<0:break
  offsets.append(at);at+=8
 gbix=[struct.unpack_from('<I',b,offsets[i]+8)[0] for i in mapping]
 assert mapping==[70,71,72] and gbix==[10011,10012,10013]
 row=f'barrier:afs/STAGE1/000_st1dat.bin:{textures}:0,0,0,0:rawat:{SB:x}:{root:x}:{motion:x}:texmap='+','.join(map(str,mapping))+':lighting=1:cull=none'
 subprocess.run([sys.executable,'tools/jgc_convert.py',str(out/'shibuya_gates.jgc'),'--motions=1','--texmax=512','--source-material=1','--source-environment=1',row],cwd=DEC,check=True)
 places=[p for p in s.records(0x8c10618c,63,40,28) if p['pid']==0];assert len(places)==6
 stage=(ROOT/'psp/stage1.jgs').read_bytes();objects=struct.unpack_from('<I',stage,28)[0]
 side=json.loads((ROOT/'psp/stage1.jgs.map.json').read_text());ids=[i for i,v in enumerate(side) if v[0]==root-SB]
 assert len(ids)==6
 collision=(ROOT/'psp/stage1.col').read_bytes();off=struct.unpack_from('<I',collision,8)[0]
 _,world,patches,entries=struct.unpack_from('<4I',collision,off);off+=16+1028*4+patches*32+entries*12
 magic,owners,np,_=struct.unpack_from('<4I',collision,off);assert magic==0x31504943
 ranges={a:(first,count) for a,ty,first,count in (struct.unpack_from('<4I',collision,off+16+16*i) for i in range(owners))}
 assert not set(ranges).intersection(p['address'] for p in places), 'barriers already baked into city collision'
 node=s.words(0x8c107644)[0];mesh=s.words(node+4)[0]
 assert node==0x8cd87f9c and s.words(node)[0]==0x00030006
 assert struct.unpack_from('<3f3i3f',s.image,node+12-EB)==(0,0,0,0,0,0,1,1,1)
 assert s.words(node+0x3c,2)==(0,0)
 vb,pb,nv,np=struct.unpack_from('<IIHH',s.image,mesh-EB);assert (nv,np)==(20,5)
 triangles=[]
 for i in range(np):
  idx,flags,mask,surf=struct.unpack_from('<IIHH',s.image,pb+i*36+24-EB)
  strip_ids=struct.unpack_from('<'+'h'*((flags&65535)+2),s.image,idx-EB)
  wind=bool(flags&0x40000000)
  for j in range(flags&65535):
   indices=strip_ids[j:j+3] if wind else (strip_ids[j],strip_ids[j+2],strip_ids[j+1])
   vertices=[v for index in indices for v in struct.unpack_from('<3f',s.image,vb+index*12-EB)]
   triangles.append((vertices,surf,mask,(1 if flags&0x80000000 else 0)|(0 if wind else 2),2))
   wind=not wind
 assert len(triangles)==10
 def f(v):return float(v).hex()+'f'
 def arr(v):return '{'+','.join(f(x) for x in v)+'}'
 lines=['/* Generated from Shibuya 030688/030c94/057b88. */','static const BarrierPlacement barrier_placements[]={']
 assert len(places)==len(ids)==6
 for p,oi in zip(places,ids):
  obj=struct.unpack_from('<4f4I',stage,objects+oi*32)
  p.update(object=oi,object_fields=obj)
  lines.append('    {%du,%du,%s,{%s},{%s,%s}},'%(p['sector'],oi,arr(p['pos']),','.join(map(str,p['ang'])),','.join(f(x) for x in obj[:4]),','.join(str(x)+'u' for x in obj[4:])))
 lines+=['};','static const ColTri barrier_collision[]={']
 for v,surf,mask,flags,src in triangles:lines.append('    {%s,%du,%du,%du,%du},'%(arr(v),surf,mask,flags,src))
 lines+=['};','']
 (out/'shibuya_gate_data.inc').write_text('\n'.join(lines))
 meta=dict(root=f'{root:08x}',motion=f'{motion:08x}',frames=s.words(motion)[0]>>16,texlist=f'{texlist:08x}',texture_indices=mapping,gbix=gbix,placements=places,collision_node=f'{node:08x}',collision_triangles=triangles,stage_sha256=hashlib.sha256(stage).hexdigest(),collision_sha256=hashlib.sha256(collision).hexdigest(),model_sha256=hashlib.sha256((out/'shibuya_gates.jgc').read_bytes()).hexdigest())
 assert meta['frames']==50
 (out/'shibuya_gate_assets.json').write_text(json.dumps(meta,indent=2)+'\n')
def main():
 ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
 with tempfile.TemporaryDirectory(prefix='jgr-barriers-') as t:
  p=Path(t);generate(p)
  for name in ('shibuya_gates.jgc','shibuya_gate_assets.json','shibuya_gate_data.inc'):
   target=ROOT/'psp'/('src' if name.endswith('.inc') else '')/name
   if args.check:assert target.read_bytes()==(p/name).read_bytes(),name
   else:target.write_bytes((p/name).read_bytes())
if __name__=='__main__':main()
