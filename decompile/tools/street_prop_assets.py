#!/usr/bin/env python3
"""Export the original rigid street-prop task records and analytic bodies.

The executable dispatch, rather than legacy DYN layout names, chooses the
owners. Local collider TRS/hierarchy and exact sphere radii are retained;
source 0b7078 transforms sphere centers but copies radii without scaling.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import struct
import tempfile

import ninjalib as nl
from stage_render_assets import Source, EB, tree, IDENTITY
from street_prop_types import STAGES, AUDITED_EMPTY_STAGES, SPECIAL_FAMILIES, CHILD_YAW_MOTIONS

ROOT=Path(__file__).resolve().parents[2]

def source_model_summary(source,root):
    """Source strip triangles before PSP edge subdivision; empty far roots matter."""
    triangles=objects=0
    for node,_ in tree(source,root,IDENTITY):
        if not node['model'] or node['ev']&8:continue
        _,plist=source.words(node['model'],2)
        count=len(nl.parse_plist(source.image,plist-EB)) if plist else 0
        triangles+=count;objects+=bool(count)
    return dict(triangles=triangles,objects=objects)

def source_animations():
    source=Source(3);result=[]
    for ty,motion in CHILD_YAW_MOTIONS.items():
        model=source.words(STAGES[3]['modeltable']+ty*4)[0]
        nodes=[n for n,_ in tree(source,model,IDENTITY)]
        assert len(nodes)==2 and all(n['model'] for n in nodes)
        assert nodes[0]['child']==nodes[1]['addr'] and not nodes[0]['sib']
        assert not nodes[1]['child'] and not nodes[1]['sib']
        for n in nodes:
            assert n['pos']==(0.,0.,0.) and n['ang']==(0,0,0) and n['scl']==(1.,1.,1.)
        header,md=source.words(motion,2);frames=header>>16
        assert frames=={51:141,52:130,53:190}[ty] and header&65535==0x11
        assert source.words(md,4)==(0,0,0,0)
        keyed,position,angle,scale=source.words(md+16,4)
        assert keyed==0x10 and not position and not scale
        triples=[struct.unpack_from('<3h',source.image,angle-EB+i*6) for i in range(frames)]
        assert all(x==z==0 for x,_,z in triples)
        child=nodes[1]
        result.append(dict(stage=3,type=ty,motion=motion,frames=frames,axis=1,
                           period=frames-1,format='dense_s16',yaw=[a[1] for a in triples],
                           child=child['addr'],child_mesh=child['model'],child_index=1,
                           position=child['pos'],angles=child['ang'],scale=child['scl']))
    # Kogane's lone type30 is the other animated rigid task (0e1ce0).
    # Its classic NJS_MOTION has a dense absolute child-roll key per frame;
    # the original draw uses the same root and child rigid meshes.
    source=Source(2);ty=30;motion=source.words(0x8c107d28)[0]
    nodes=[n for n,_ in tree(source,source.words(STAGES[2]['modeltable']+ty*4)[0],IDENTITY)]
    assert len(nodes)==2 and all(n['model'] for n in nodes)
    assert nodes[0]['child']==nodes[1]['addr'] and not nodes[0]['sib']
    assert not nodes[1]['child'] and not nodes[1]['sib']
    assert nodes[0]['pos']==(0.,0.,0.) and nodes[0]['ang']==(0,0,0) and nodes[0]['scl']==(1.,1.,1.)
    md,frames,flags=source.words(motion,3)
    assert frames==180 and flags==0x20003 and source.words(md,4)==(0,0,0,0)
    position,angle,nposition,nangle=source.words(md+16,4)
    assert not position and not nposition and nangle==frames
    keys=[struct.unpack_from('<4i',source.image,angle-EB+i*16) for i in range(frames)]
    assert all(frame==i and x==y==0 for i,(frame,x,y,z) in enumerate(keys))
    child=nodes[1]
    far=source.words(STAGES[2]['far_modeltable']+ty*4)[0]
    far_nodes=[n for n,_ in tree(source,far,IDENTITY)]
    assert len(far_nodes)==1 and far_nodes[0]['ev']==23 and far_nodes[0]['model']
    assert not far_nodes[0]['child'] and not far_nodes[0]['sib']
    result.append(dict(stage=2,type=ty,motion=motion,frames=frames,axis=2,
                       period=frames,format='keyed_s32',yaw=[((z+32768)&65535)-32768 for _,_,_,z in keys],
                       child=child['addr'],child_mesh=child['model'],child_index=1,
                       far_model=far,far_child=0,far_objects=1,
                       position=child['pos'],angles=child['ang'],scale=child['scl']))
    return result

def source_records(stage=1):
    source=Source(stage);placements=[];boxes=[]
    for bank,config in enumerate([STAGES[stage],*STAGES[stage].get('extra_banks',[])]):
        bank_placements=[p for p in source.records(config['tasktable'],config['sectors'],
                                                  config['stride'],config['typecount'])
                         if p['pid'] in config['types']]
        assert collections.Counter(p['pid'] for p in bank_placements)==config['counts'],(stage,bank)
        types={}
        for ty in sorted(config['counts']):
            first=len(boxes);seen=set()
            def walk(node,parent=-1):
                while node:
                    assert source.valid(node) and node not in seen,hex(node)
                    seen.add(node)
                    kind,attribute,vertices=struct.unpack_from('<HHI',source.image,node-EB)
                    assert kind in (2,4) and attribute==25,(stage,ty,hex(node),kind,attribute)
                    position=struct.unpack_from('<3f',source.image,node+12-EB)
                    angles=struct.unpack_from('<3i',source.image,node+24-EB)
                    scale=struct.unpack_from('<3f',source.image,node+36-EB)
                    index=len(boxes)-first
                    boxes.append(dict(type=ty,node=node,kind=kind,attribute=attribute,source_bank=bank,
                                      vertices=struct.unpack_from('<24f',source.image,vertices-EB) if kind==4 else (0.,)*24,
                                      sphere=struct.unpack_from('<4f',source.image,vertices-EB) if kind==2 else (0.,)*4,
                                      center=struct.unpack_from('<3f',source.image,node+48-EB),
                                      position=position,angles=angles,scale=scale,parent=parent))
                    child,node=source.words(node+60,2)
                    if child:walk(child,index)
            walk(source.words(config['collisiontable']+ty*4)[0])
            assert len(boxes)>first,(stage,ty)
            types[ty]=(first,len(boxes)-first)
        for p in bank_placements:
            ty=p['pid'];description=config['types'][ty]
            p.update(first_box=types[ty][0],boxes=types[ty][1],source_bank=bank,
                     sound=description['sound'],reaction=description['reaction'],accepts=description['accepts'],
                     model=source.words(config['modeltable']+ty*4)[0],
                     texlist=source.words(config['texlisttable']+ty*4)[0],
                     far_model=source.words(config['far_modeltable']+ty*4)[0] if config.get('far_modeltable') else 0,
                     far_texlist=source.words(config['far_texlisttable']+ty*4)[0] if config.get('far_texlisttable') else 0,
                     lod_squared=config.get('lod_squared',0.),
                     animation=(list(CHILD_YAW_MOTIONS).index(ty)+1 if stage==3 and bank==0 and ty in CHILD_YAW_MOTIONS
                                else 4 if stage==2 and ty==30 else 0))
            assert source.valid(p['model']) and source.valid(p['texlist'])
            if p['lod_squared']:
                assert source.valid(p['far_model'])
                assert source.valid(p['far_texlist']) or (p['far_texlist']==0 and
                       source_model_summary(source,p['far_model'])['triangles']==0)
        placements+=bank_placements
    assert len({p['address'] for p in placements})==len(placements)
    return source,placements,boxes

def generate(out):
    def vec(values):return '{'+','.join(float(v).hex()+'f' for v in values)+'}'
    def ints(values):return '{'+','.join(map(str,values))+'}'
    records={stage:source_records(stage) for stage in STAGES}
    max_count=max(len(p) for _,p,_ in records.values())
    max_shapes=max(p['boxes'] for _,placements,_ in records.values() for p in placements)
    lines=['/* Generated by street_prop_assets.py; original US task dispatch. */',
           '/* Node attributes, body masks and sound banks are separate source fields. */',
           '#define STREET_PROP_MAX_COUNT %du'%max_count,
           '#define STREET_PROP_MAX_SHAPES %du'%max_shapes]
    animations=source_animations()
    for a in animations:
        lines.append('static const int16_t street_prop_yaw_%d[]={%s};'%
                     (a['type'],','.join(map(str,a['yaw']))))
    lines.append('static const StreetPropAnimation street_prop_animations[]={')
    for a in animations:
        lines.append('    {%du,street_prop_yaw_%d,%s,%s,%s,%du},'%
                     (a['frames'],a['type'],vec(a['position']),ints(a['angles']),vec(a['scale']),a['axis']))
    lines+=['};']
    reports=[]
    for stage,config in STAGES.items():
        source,placements,boxes=records[stage]
        render_models=[];seen_models=set()
        for p in placements:
            key=(p['source_bank'],p['pid'])
            if key in seen_models:continue
            seen_models.add(key)
            render_models.append(dict(source_bank=p['source_bank'],type=p['pid'],
                model=p['model'],texlist=p['texlist'],near=source_model_summary(source,p['model']),
                far_model=p['far_model'],far_texlist=p['far_texlist'],
                far=source_model_summary(source,p['far_model']) if p['far_model'] else None,
                lod_squared=p['lod_squared']))
        lines.append('static const StreetPropBox street_prop_boxes_%d[]={'%stage)
        for b in boxes:
            lines.append('    {%s,%s,%du,%s,%s,%s,%s,%d,%du}, /* type %d, node %08x */'%
                         (vec(b['vertices']),vec(b['center']),b['attribute'],vec(b['sphere']),
                          vec(b['position']),ints(b['angles']),vec(b['scale']),b['parent'],
                          b['kind'],b['type'],b['node']))
        lines+=['};','static const StreetPropPlacement street_prop_placements_%d[]={'%stage]
        for p in placements:
            lines.append('    {0x%08xu,%du,%du,%du,%du,%du,%d,%s,%s,%s,%du},'%
                         (p['address'],p['pid'],p['sector'],p['first_box'],p['boxes'],p['reaction'],
                          p['sound'],vec(p['pos']),ints(p['ang']),vec(p['scale']),p['animation']))
        lines+=['};']
        reports.append(dict(stage=stage,name=config['name'],sectors=config['sectors'],
                            source_sha256=hashlib.sha256(source.data).hexdigest(),
                            executable_sha256=hashlib.sha256(source.exe).hexdigest(),
                            spawn_handler='%08x'%config['spawn_handler'],dispatch_handler='%08x'%config['dispatch_handler'],
                            tasktable='%08x'%config['tasktable'],modeltable='%08x'%config['modeltable'],
                            collisiontable='%08x'%config['collisiontable'],texlisttable='%08x'%config['texlisttable'],
                            far_modeltable='%08x'%config.get('far_modeltable',0),
                            far_texlisttable='%08x'%config.get('far_texlisttable',0),
                            lod_squared=config.get('lod_squared',0.),render_models=render_models,
                            layer=16,active_distance_squared=900,boxes=boxes,placements=placements,
                            extra_banks=config.get('extra_banks',[])))
    lines+=['static const StreetPropStage street_prop_stages[]={']
    for stage,(_,placements,boxes) in records.items():
        lines.append('    {%du,%du,%du,%du,street_prop_placements_%d,street_prop_boxes_%d},'%
                     (stage,STAGES[stage]['sectors'],len(placements),len(boxes),stage,stage))
    lines+=['};','']
    (out/'street_props_data.inc').write_text('\n'.join(lines))
    sound_masks=[0]*7;sequence_masks=[0]*7
    for stage,(_,placements,_) in records.items():
        for p in placements:
            sound=p['sound']
            if sound==0:continue
            if sound>0:
                assert sound<64
                sound_masks[stage]|=1<<sound
            else:
                assert -sound<32
                sequence_masks[stage]|=1<<-sound
    audio=['/* Generated from the audible, authored street prop placements. */',
           'static const uint64_t street_prop_sound_masks[7]={',
           '    '+','.join('UINT64_C(0x%x)'%v for v in sound_masks)+'};',
           'static const unsigned street_prop_sequence_masks[7]={',
           '    '+','.join('0x%xu'%v for v in sequence_masks)+'};','']
    (out/'street_prop_audio.inc').write_text('\n'.join(audio))
    report=dict(stages=reports,total_placements=sum(len(p) for _,p,_ in records.values()),animations=animations,
                audited_empty_stages=AUDITED_EMPTY_STAGES,special_families=SPECIAL_FAMILIES)
    (out/'street_props.json').write_text(json.dumps(report,indent=2)+'\n')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check',action='store_true');args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='jgr-street-props-') as tmp:
        out=Path(tmp);generate(out)
        for name in ('street_props_data.inc','street_prop_audio.inc','street_props.json'):
            target=ROOT/'psp'/('src' if name.endswith('.inc') else '')/name
            if args.check:assert target.read_bytes()==(out/name).read_bytes(),name
            else:target.write_bytes((out/name).read_bytes())
    print('Rigid street props: '+', '.join('%s %d'%(c['name'],sum(sum(b['counts'].values()) for b in [c,*c.get('extra_banks',[])])) for c in STAGES.values()))

if __name__=='__main__':main()
