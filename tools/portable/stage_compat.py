#!/usr/bin/env python3
"""Preserve the shipping Kogane packing order after geometry-only roof fixes.

Sorting uses reconstructed pre-correction source coordinates. Emitted vertices
keep the corrected coordinates and historical colors, exactly as the original
port migration did. Texture reordering contains indices only, never pixels.
"""
import argparse
import json
from pathlib import Path
import struct
import sys


def restore(tree):
    sys.path.insert(0,str(tree/'decompile/tools'))
    import ninjalib as nl
    from stage_render_assets import Source,TABLES,SB,kogane_roof_placement
    from stage_visibility_assets import fnv
    bank=tree/'psp/stage2.jgs';data=bytearray(bank.read_bytes());h=struct.unpack_from('<11I',data,4)
    nt,no,nb,nv,ni,ot,oo,ob,ov,oi,op=h
    meta=bytearray(bank.with_suffix('.jrs').read_bytes())
    _,_,sectors,nrecords,*_=struct.unpack_from('<4s7I',meta)
    records={}
    for i in range(nrecords):
        r=struct.unpack_from('<4H3fHHfI',meta,32+sectors*16+i*32);records[r[-1]]=r[:4]
    # This node runs immediately after the base near/far geometry compactor,
    # before local street-prop payloads append their independently packed spans.
    batches=[list(struct.unpack_from('<II4H',data,ob+i*16)) for i in range(nb)]
    spans=[bytes(data[ov+b[0]*24:ov+(b[0]+b[2])*24]) for b in batches];keys=list(spans)
    source=Source(2);texmap=json.loads((tree/'decompile/stage2_texmap.json').read_text())['texmap'];table,near,far,stride,count,_=TABLES[2]['IT']
    nl.MAXEDGE=120.;nl.BAKED_LIGHT=None
    for record in source.records(table,sectors,stride,count):
        if not kogane_roof_placement(record['pid'],record):continue
        rx,ry,rz=[a*nl.A2R for a in record['ang']]
        old=nl.mmul(nl.mtrans(*record['pos']),nl.mmul(nl.mrotx(rx),nl.mmul(nl.mroty(ry),nl.mrotz(rz))))
        for lod,root_table in enumerate((near,far)):
            root=source.words(root_table+record['pid']*4)[0];builder=nl.JgsBuilder();mapping=texmap[str(root-SB)]
            for node,m in nl.walk_tree(source.data,root,SB,old):
                if node['model'] and not node['ev']&8:builder.add_model(source.data,node['model'],SB,m,lambda slot:mapping[slot])
            first,count=records[record['address']][lod*2:lod*2+2]
            if count!=len(builder.objs):raise ValueError('Roof provenance object count changed')
            for local,oid in enumerate(range(first,first+count)):
                dst,n=struct.unpack_from('<2I',data,oo+oid*32+16);_,_,_,_,src,n0,_,_=struct.unpack('<4f4I',builder.objs[local])
                if n!=n0:raise ValueError('Roof batch count changed')
                for bi,si in zip(range(dst,dst+n),range(src,src+n)):
                    a=builder.batches[si];b=batches[bi]
                    if a[2:4]!=b[2:4]:raise ValueError('Roof subdivision changed')
                    order=bytearray(spans[bi])
                    for v in range(b[2]):order[v*24+12:v*24+24]=builder.verts[(a[0]+v)*24+12:(a[0]+v)*24+24]
                    keys[bi]=bytes(order)
    pairs={}
    for key,payload in zip(keys,spans):
        if key in pairs and pairs[key]!=payload:raise ValueError('Legacy alias changed geometry')
        pairs[key]=payload
    order=bytearray();packed=bytearray();offsets={}
    for key in sorted(pairs,key=lambda b:(-len(b),b)):
        at=order.find(key)
        while at>=0 and at%24:at=order.find(key,at+1)
        if at<0:at=len(order);order.extend(key);packed.extend(pairs[key])
        elif packed[at:at+len(key)]!=pairs[key]:raise ValueError('Legacy contained span changed geometry')
        offsets[key]=at//24
    if len(packed)!=nv*24:raise ValueError('Legacy geometry packing size changed')
    data[ov:ov+len(packed)]=packed
    permutation={int(k):v for k,v in json.loads(Path(__file__).with_name('stage_texture_order.json').read_text())['stages']['2'].items()}
    for i,(batch,key) in enumerate(zip(batches,keys)):
        batch[0]=offsets[key];batch[4]=permutation.get(batch[4],batch[4]);struct.pack_into('<II4H',data,ob+i*16,*batch)
    textures=[]
    for i in range(nt):
        w,ht,flags,pad,clut,pixels=struct.unpack_from('<4H2I',data,ot+i*16)
        textures.append((w,ht,flags,pad,bytes(data[clut:clut+1024]),bytes(data[pixels:pixels+w*ht])))
    reordered=list(textures)
    for old,new in permutation.items():reordered[new]=textures[old]
    data=data[:op]
    for i,t in enumerate(reordered):
        data.extend(bytes(-len(data)%16));clut=len(data);data.extend(t[4]);data.extend(bytes(-len(data)%16));pixels=len(data);data.extend(t[5])
        struct.pack_into('<4H2I',data,ot+i*16,*t[:4],clut,pixels)
    struct.pack_into('<I',meta,28,fnv(data[:ov]));bank.write_bytes(data);bank.with_suffix('.jrs').write_bytes(meta)


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--tree',type=Path,required=True)
    restore(parser.parse_args().tree.resolve())
if __name__=='__main__':main()
