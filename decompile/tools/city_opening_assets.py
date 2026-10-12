#!/usr/bin/env python3
"""Original Benten gates/lantern frames and Bantam doors/breakable windows.

Bindings: 047e84/048b64, 0edee0/0efcd4; 0f43c8/0f4c44,
0ff9e4/100134. Event boxes are exported separately from solid floor strips.
"""
# These owners use the original normal draw path 234fc6 / mode 1,
# which submits both face directions (unlike the PLAY cel-hull path).

import argparse, hashlib, json, struct, subprocess, sys, tempfile
from pathlib import Path
from stage_render_assets import Source, tree, IDENTITY, EB, SB
import ninjalib as nl
ROOT=Path(__file__).resolve().parents[2]; DEC=ROOT/'decompile'
CONFIG={
 3:dict(name='benten',types=(23,48,6,47,50),table=0x8c1ba918,sectors=46,count=70,near=0x8c1bc064,tex=0x8c1bc17c,coll=0x8c1bc294,
        far=0,fartex=0,lights=0x8c175d90,motions=(0x8cd99c98,0x8cd9c710,0x8cd98ee0,0x8cd9faa8,0x8cda0dec),frames=(10,60,80,60,90),sound=(31,5,34,33,31),bank=(16,4,16,16,16),kind=(0,1,5,5,6),lod2=0),
 5:dict(name='bantam',types=(1,5),table=0x8c203298,sectors=71,count=20,near=0x8c203f44,tex=0x8c203f94,coll=0x8c204084,
        far=0x8c203fe4,fartex=0x8c204034,lights=0x8c1fbd34,motions=(0x8cc54544,0x8cc545e8),frames=(30,23),sound=(30,39),bank=(16,16),kind=(2,3),lod2=0),
}
def body_boxes(s,node,parent=IDENTITY):
    boxes=[]
    while node:
        kind,flags,mesh=struct.unpack_from('<HHI',s.image,node-EB);assert (kind,flags)==(4,25)
        pos=struct.unpack_from('<3f',s.image,node+12-EB);ang=struct.unpack_from('<3i',s.image,node+24-EB)
        scale=struct.unpack_from('<3f',s.image,node+36-EB)
        m=nl.mmul(parent,nl.mmul(nl.placement_matrix(pos,ang),nl.mscale(*scale)))
        boxes.append(dict(node=f'{node:08x}',vertices=[nl.xform(m,struct.unpack_from('<3f',s.image,mesh+i*12-EB)) for i in range(8)],
                          center=nl.xform(m,struct.unpack_from('<3f',s.image,node+48-EB))))
        child,node=s.words(node+60,2)
        if child:boxes+=body_boxes(s,child,m)
    return boxes

def floors(s):
    result=[]
    for node in (0x8cc3e2f8,0x8cc3e3a4):
        kind,flags,mesh=struct.unpack_from('<HHI',s.image,node-EB);assert (kind,flags)==(6,1)
        assert struct.unpack_from('<3f3i3f',s.image,node+12-EB)==(0,0,0,0,0,0,1,1,1)
        vb,pb,nv,np=struct.unpack_from('<IIHH',s.image,mesh-EB);assert (nv,np)==(4,1)
        ix,flag,mask,surface=s.words(pb+24,2)+(struct.unpack_from('<HH',s.image,pb+32-EB))
        assert (flag,mask,surface)==(0x40000002,1,6)
        ids=struct.unpack_from('<4h',s.image,ix-EB)
        verts=[struct.unpack_from('<3f',s.image,vb+12*i-EB) for i in ids]
        result += [dict(vertices=[verts[0],verts[1],verts[2]],surface=surface,mask=mask,reversed=0),
                   dict(vertices=[verts[1],verts[3],verts[2]],surface=surface,mask=mask,reversed=2)]
    return result

def generate(directory,stage):
    c=CONFIG[stage];s=Source(stage);name=c['name'];rows=[];types=[];bindings=[];boxes=[]
    # Include private-handler texture lists absent from the old static mapping.
    with tempfile.TemporaryDirectory(prefix='jgr-opening-textures-') as tmp:
        tmfile=Path(tmp)/'textures.json'
        subprocess.run([sys.executable,'tools/static_texmap.py','extracted/1ST_READ.BIN',str(stage),str(tmfile)],cwd=DEC,check=True,stdout=subprocess.DEVNULL)
        tm=json.loads(tmfile.read_text());textures=Path(tmp)/'textures.bin'
        textures.write_bytes(b''.join((DEC/p).read_bytes() for p in tm['packs']))
        for ti,ty in enumerate(c['types']):
            near=s.words(c['near']+ty*4)[0];far=s.words(c['far']+ty*4)[0] if c['far'] else near
            motion=c['motions'][ti];frames=s.words(motion)[0]>>16;assert frames==c['frames'][ti]
            first=len(boxes);boxes+=body_boxes(s,s.words(c['coll']+ty*4)[0]);assert len(boxes)-first==1
            for lod,root in enumerate((near,)):
                tl=s.words((c['fartex'] if lod and c['fartex'] else c['tex'])+ty*4)[0]
                mapping=tm['texlists'].get(str(tl))
                if mapping is None:
                    assert all(not n['model'] for n,_ in tree(s,root,IDENTITY));mapping=[0]
                bindings.append(dict(type=ty,lod=lod,texlist=f'{tl:08x}',indices=mapping if tl else []))
                rows.append(f'{name}{ty}_{lod}:afs/STAGE{stage}/000_st{stage}dat.bin:{textures}:0,0,0,0:rawat:{SB:x}:{root:x}:{motion:x}:texmap='+','.join(map(str,mapping))+':lighting=1:cull=none')
            types.append(dict(type=ty,kind=c['kind'][ti],near=f'{near:08x}',far=f'{far:08x}',motion=f'{motion:08x}',frames=frames,
                              first_box=first,boxes=len(boxes)-first,sound=c['sound'][ti],bank=c['bank'][ti]))
        subprocess.run([sys.executable,'tools/jgc_convert.py',str(directory/f'{name}_openings.jgc'),'--motions=1','--texmax=512','--source-material=1','--source-environment=1',*rows],cwd=DEC,check=True)
    placements=[r for r in s.records(c['table'],c['sectors'],52,c['count']) if r['pid'] in c['types']]
    assert len(placements)==(11 if stage==3 else 12) and len({p['address'] for p in placements})==len(placements)
    side=json.loads((ROOT/f'psp/stage{stage}.jgs.map.json').read_text())
    assert not {v[0] for v in side}&{int(t[k],16)-SB for t in types for k in ('near','far')}
    def f(v):return float(v).hex()+'f'
    def vector(v):return '{'+','.join(f(x) for x in v)+'}'
    lines=['/* Generated from original US Dreamcast bindings by city_opening_assets.py. */',f'static const OpeningType {name}_types[]={{']
    for t in types:lines.append('    {%du,%du,%du,%du,%du,%du,%du},'%(t['type'],t['kind'],t['frames'],t['first_box'],t['boxes'],t['sound'],t['bank']))
    lines+=['};',f'static const OpeningBox {name}_boxes[]={{']
    for b in boxes:lines.append('    {'+vector([x for v in b['vertices'] for x in v])+','+vector(b['center'])+'},')
    lines+=['};',f'static const OpeningPlacement {name}_placements[]={{']
    for p in placements:lines.append('    {0x%08xu,%du,%du,%s,{%s},%s},'%(p['address'],c['types'].index(p['pid']),p['sector'],vector(p['pos']),','.join(map(str,p['ang'])),vector(p['scale'])))
    light=[struct.unpack_from('<8f',s.exe,c['lights']+i*32-EB) for i in range(c['sectors'])]
    lines+=['};',f'static const float {name}_lights[{c["sectors"]}][8]={{']+[vector(v)+',' for v in light]+['};']
    floor=floors(s) if stage==5 else []
    if floor:
        lines+=['static const ColTri bantam_door_floors[]={']
        for t in floor:lines.append('    {'+vector([x for v in t['vertices'] for x in v])+f',{t["surface"]},{t["mask"]},{t["reversed"]},2'+'},')
        lines+=['};']
    lines+=[''];(directory/f'{name}_opening_data.inc').write_text('\n'.join(lines))
    metadata=dict(stage=stage,types=types,placements=placements,boxes=boxes,floors=floor,texture_bindings=bindings,lights=light,
                  light_table=f'{c["lights"]:08x}',source_sha256=hashlib.sha256(s.data).hexdigest(),model_sha256=hashlib.sha256((directory/f'{name}_openings.jgc').read_bytes()).hexdigest())
    (directory/f'{name}_opening_assets.json').write_text(json.dumps(metadata,indent=2)+'\n')

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    with tempfile.TemporaryDirectory(prefix='jgr-opening-export-') as tmp:
        d=Path(tmp)
        for stage,c in CONFIG.items():
            generate(d,stage)
            for suffix in ('_openings.jgc','_opening_assets.json','_opening_data.inc'):
                name=c['name']+suffix;target=ROOT/'psp'/('src' if name.endswith('.inc') else '')/name
                if args.check:assert target.read_bytes()==(d/name).read_bytes(),name
                else:target.write_bytes((d/name).read_bytes())
if __name__=='__main__':main()
