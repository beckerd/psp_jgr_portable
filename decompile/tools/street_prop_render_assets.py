#!/usr/bin/env python3
"""Add source-owned knockable meshes without rebaking existing city geometry.

The original object/batch/texture IDs and near/far boundary stay stable.
Appended kind-9 owners may contain near objects after that boundary. Every
placement is keyed by its original task record, never by visual similarity.
"""
import argparse
import bisect
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

import ninjalib as nl
from pvr_decode import decode
from stage_render_assets import EB, SB, tree, IDENTITY
from stage_visibility_assets import fnv
from street_prop_assets import source_records,source_model_summary
from jgs_instances import read_instances,encode_instances,JGS_O_INSTANCE

ROOT = Path(__file__).resolve().parents[2]
DC = ROOT / 'decompile'


def sections(data):
    counts = struct.unpack_from('<5I', data, 4)
    offsets = struct.unpack_from('<6I', data, 24)
    nt, no, nb, nv, ni = counts
    ot, oo, ob, ov, oi, op = offsets
    return counts, offsets, [data[ot:ot+16*nt], data[oo:oo+32*no],
                            data[ob:ob+16*nb], data[ov:ov+24*nv],
                            data[oi:oi+2*ni], data[op:]]


def pack_bank(original, counts, parts, extension=b''):
    offsets = []
    at = 80
    instance_offset=0
    for i,part in enumerate(parts):
        if i==3:
            instance_offset=at;at+=len(extension)
        offsets.append(at)
        at = (at+len(part)+15)&~15
    textures = bytearray(parts[0])
    for i in range(counts[0]):
        clut, pixels = struct.unpack_from('<2I', textures, i*16+8)
        struct.pack_into('<2I', textures, i*16+8,
                         clut+offsets[5], pixels+offsets[5])
    parts = [textures, *parts[1:]]
    result = bytearray(original[:80])
    struct.pack_into('<5I6I', result, 4, *counts, *offsets)
    for i,(offset, part) in enumerate(zip(offsets, parts)):
        if i==3 and extension:
            result+=b'\0'*(instance_offset-len(result));result+=extension
        result += b'\0'*(offset-len(result))
        result += part
    return bytes(result)


def source_builder(stage, source, placements, texlists):
    builder = nl.JgsBuilder()
    nl.MAXEDGE = 120.
    nl.BAKED_LIGHT = 'shibuya' if stage == 1 else None
    owners = []
    for placement in placements:
        mapping = texlists[str(placement['texlist'])]
        matrix = nl.mmul(nl.placement_matrix(placement['pos'], placement['ang']),
                         nl.mscale(*placement['scale']))
        first = len(builder.objs)

        def mapped(slot):
            assert slot < len(mapping) and mapping[slot] >= 0, (stage, placement['pid'], slot)
            return mapping[slot]

        for node, transform in tree(source, placement['model'], matrix):
            if node['model'] and not node['ev'] & 8:
                builder.add_model(source.image, node['model'], EB, transform, mapped)
        count = len(builder.objs)-first
        assert count, ('empty knockable model', stage, placement['pid'])
        owners.append((placement, first, count))
    return builder, owners


def bake_near(stage, input_path, output_dir):
    source, placements, _ = source_records(stage)
    original = input_path.read_bytes()
    assert original[:4] == b'JGS1'
    counts, offsets, raw = sections(original)
    nt, no, nb, nv, ni = counts
    meta = bytearray(input_path.with_suffix('.jrs').read_bytes())
    magic, city, sectors, nr, near, objects, batches, key = struct.unpack_from('<4s7I', meta)
    assert magic == b'JRS1' and city == stage and (objects, batches) == (no, nb)
    assert key == fnv(original[:offsets[3]])
    assert len(meta) == 32+16*sectors+32*nr
    side = json.loads(Path(str(input_path)+'.map.json').read_text())
    assert len(side) == no
    by_address = {}
    for i in range(nr):
        record = struct.unpack_from('<4H3fHHfI', meta, 32+16*sectors+32*i)
        assert record[-1] not in by_address
        by_address[record[-1]] = (i, record)
    missing = []
    changed = 0
    for placement in placements:
        existing = by_address.get(placement['address'])
        if existing is None:
            missing.append(placement)
            continue
        i, record = existing
        assert record[1] and not record[3] and not record[9]
        assert record[4:7] == placement['pos'] and record[7] == placement['sector']
        assert record[8] in (0, 9)
        changed += record[8] != 9
        struct.pack_into('<H', meta, 32+16*sectors+32*i+22, 9)
    wanted = {p['address'] for p in placements}
    assert all(r[-1] in wanted for _, r in by_address.values() if r[8] == 9)
    output_dir.mkdir(parents=True, exist_ok=True)
    out = output_dir / f'stage{stage}.jgs'
    added_textures = 0
    builder = None
    if missing:
        with tempfile.TemporaryDirectory(prefix='jgr-prop-meshes-') as tmp:
            tmp = Path(tmp)
            texmap = tmp/'textures.json'
            subprocess.run([sys.executable, 'tools/static_texmap.py', 'extracted/1ST_READ.BIN',
                            str(stage), str(texmap)], cwd=DC, check=True, stdout=subprocess.DEVNULL)
            texture_source = json.loads(texmap.read_text())
            builder, owners = source_builder(stage, source, missing, texture_source['texlists'])
            images = []
            for pack in texture_source['packs']:
                data = (DC/pack).read_bytes()
                at = 0
                while True:
                    at = data.find(b'GBIX', at)
                    if at < 0:
                        break
                    pvrt = data.find(b'PVRT', at+8, at+64)
                    assert pvrt >= 0
                    length = struct.unpack_from('<I', data, pvrt+4)[0]
                    images.append(decode(data[pvrt:pvrt+length+8]) if len(images) in builder.used else None)
                    at += 8
            fresh = tmp/'props.jgs'
            nl.write_jgs(fresh, builder, images, texmax=128)
            new = fresh.read_bytes()
            nc, noff, np = sections(new)
        textures, obj, batch, verts, indices, pixels = map(bytearray, raw)
        # Exact encoded palette/pixels identify shared source textures. Old
        # descriptors and their ordering remain untouched.
        texture_ids = {}
        for i in range(nt):
            w, h, flags, reserved, clut, pix = struct.unpack_from('<4H2I', textures, i*16)
            texture_ids.setdefault((w, h, flags, reserved, original[clut:clut+1024], original[pix:pix+w*h]), i)
            struct.pack_into('<2I', textures, i*16+8, clut-offsets[5], pix-offsets[5])
        remap = []
        for i in range(nc[0]):
            w, h, flags, reserved, clut, pix = struct.unpack_from('<4H2I', np[0], i*16)
            texture_key = (w, h, flags, reserved, new[clut:clut+1024], new[pix:pix+w*h])
            target = texture_ids.get(texture_key)
            if target is None:
                target = nt+added_textures
                texture_ids[texture_key] = target
                added_textures += 1
                start = len(pixels)
                assert not start & 15
                pixels += texture_key[4]+texture_key[5]
                textures += struct.pack('<4H2I', w, h, flags, reserved, start, start+1024)
            remap.append(target)
        for j in range(nc[2]):
            vo, io, vn, ix, tex, flags = struct.unpack_from('<II4H', np[2], j*16)
            batch += struct.pack('<II4H', vo+nv, io+ni, vn, ix, remap[tex] if not flags & 4 else 0, flags)
        record_ids = {}
        for placement, first, count in owners:
            rid = nr+len(record_ids)+1
            record_ids[placement['address']] = rid
            meta += struct.pack('<4H3fHHfI', no+first, count, no+nc[1], 0, *placement['pos'],
                                placement['sector'], 9, 0., placement['address'])
            for j in range(first, first+count):
                row = list(struct.unpack_from('<4f4I', np[1], j*32))
                row[4] += nb
                row[6] = 0
                row[7] = rid
                obj += struct.pack('<4f4I', *row)
                side.append([placement['model']-SB, 'street_prop', 1])
        assert len(record_ids) == len(missing)
        result = pack_bank(original, (nt+added_textures, no+nc[1], nb+nc[2], nv+nc[3], ni+nc[4]),
                           [textures, obj, batch, verts+np[3], indices+np[4], pixels])
        result = bytearray(result)
        bounds = struct.unpack_from('<6f', original, 48)
        struct.pack_into('<6f', result, 48, *(min(bounds[k], builder.bmin[k]) for k in range(3)),
                         *(max(bounds[k+3], builder.bmax[k]) for k in range(3)))
        result = bytes(result)
        struct.pack_into('<II', meta, 12, nr+len(missing), near)
        struct.pack_into('<3I', meta, 20, no+nc[1], nb+nc[2], fnv(result[:struct.unpack_from('<I', result, 36)[0]]))
    else:
        result = original
    out.write_bytes(result)
    out.with_suffix('.jrs').write_bytes(meta)
    Path(str(out)+'.map.json').write_text(json.dumps(side))
    report = dict(stage=stage, placements=len(placements), added_placements=len(missing),
                  changed_rules=changed, added_objects=(len(builder.objs) if builder else 0),
                  added_batches=(len(builder.batches) if builder else 0),
                  added_triangles=(builder.ni//3 if builder else 0), added_textures=added_textures,
                  original_bytes=len(original), output_bytes=len(result),
                  source_sha256=hashlib.sha256(source.data).hexdigest(),
                  original_sha256=hashlib.sha256(original).hexdigest(),
                  output_sha256=hashlib.sha256(result).hexdigest())
    (output_dir/f'stage{stage}_street_props.json').write_text(json.dumps(report, indent=2)+'\n')
    return report


def texture_source(stage):
    with tempfile.TemporaryDirectory(prefix='jgr-prop-textures-') as tmp:
        path=Path(tmp)/'textures.json'
        subprocess.run([sys.executable,'tools/static_texmap.py','extracted/1ST_READ.BIN',
                        str(stage),str(path)],cwd=DC,check=True,stdout=subprocess.DEVNULL)
        return json.loads(path.read_text())


def encoded_builder(builder,textures):
    images=[]
    for pack in textures['packs']:
        data=(DC/pack).read_bytes();at=0
        while True:
            at=data.find(b'GBIX',at)
            if at<0:break
            pvrt=data.find(b'PVRT',at+8,at+64);assert pvrt>=0
            length=struct.unpack_from('<I',data,pvrt+4)[0]
            images.append(decode(data[pvrt:pvrt+length+8]) if len(images) in builder.used else None)
            at+=8
    with tempfile.TemporaryDirectory(prefix='jgr-prop-encoded-') as tmp:
        path=Path(tmp)/'props.jgs';nl.write_jgs(path,builder,images,texmax=128)
        return path.read_bytes()


def relative_parts(data):
    counts,offsets,parts=sections(data);parts=list(map(bytearray,parts))
    for i in range(counts[0]):
        clut,pix=struct.unpack_from('<2I',parts[0],i*16+8)
        struct.pack_into('<2I',parts[0],i*16+8,clut-offsets[5],pix-offsets[5])
    return counts,parts


def append_geometry(original,fresh,owner_ids):
    """Append a builder bank; owner_ids has one (JRS ID,flags) per object."""
    (nt,no,nb,nv,ni),parts=relative_parts(original)
    nc,_,np=sections(fresh)
    textures,objects,batches,verts,indices,pixels=parts
    keys={};added=0
    for i in range(nt):
        w,h,flags,reserved,clut,pix=struct.unpack_from('<4H2I',textures,i*16)
        keys.setdefault((w,h,flags,reserved,bytes(pixels[clut:clut+1024]),bytes(pixels[pix:pix+w*h])),i)
    remap=[]
    for i in range(nc[0]):
        w,h,flags,reserved,clut,pix=struct.unpack_from('<4H2I',np[0],i*16)
        key=(w,h,flags,reserved,fresh[clut:clut+1024],fresh[pix:pix+w*h])
        slot=keys.get(key)
        if slot is None:
            slot=nt+added;added+=1;keys[key]=slot;start=len(pixels)
            assert not start&15
            pixels+=key[4]+key[5];textures+=struct.pack('<4H2I',w,h,flags,reserved,start,start+1024)
        remap.append(slot)
    assert len(owner_ids)==nc[1]
    for i,(owner,flags) in enumerate(owner_ids):
        row=list(struct.unpack_from('<4f4I',np[1],i*32));row[4]+=nb;row[6]=flags;row[7]=owner
        objects+=struct.pack('<4f4I',*row)
    for i in range(nc[2]):
        vo,io,vn,ix,tex,flags=struct.unpack_from('<II4H',np[2],i*16)
        batches+=struct.pack('<II4H',vo+nv,io+ni,vn,ix,remap[tex] if not flags&4 else 0,flags)
    result=pack_bank(original,(nt+added,no+nc[1],nb+nc[2],nv+nc[3],ni+nc[4]),
                     [textures,objects,batches,verts+np[3],indices+np[4],pixels])
    return result,added


def append_far(stage,data,meta,side,source,placements,textures):
    """Preserve source near/far ownership, including deliberately empty far roots."""
    selected=[p for p in placements if p['lod_squared']]
    if not selected:return data,meta,side,dict(far_placements=0,far_objects=0,far_textures=0)
    h=struct.unpack_from('<4s7I',meta);base=32+16*h[2]
    records={struct.unpack_from('<I',meta,base+32*i+28)[0]:i for i in range(h[3])}
    variants=[dict(p,model=p['far_model'],texlist=p['far_texlist']) for p in selected
              if source_model_summary(source,p['far_model'])['objects']]
    builder,owners=source_builder(stage,source,variants,textures['texlists'])
    first_object=struct.unpack_from('<I',data,8)[0];ids=[]
    ranges={}
    for p,first,count in owners:
        ri=records[p['address']];ranges[p['address']]=(first_object+first,count)
        ids.extend([(ri+1,2)]*count)
        side.extend([[p['model']-SB,'street_prop_far',1]]*count)
    result,added=append_geometry(data,encoded_builder(builder,textures),ids)
    total_objects,total_batches=struct.unpack_from('<2I',result,8)
    for p in selected:
        at=base+32*records[p['address']]
        assert struct.unpack_from('<H',meta,at+6)[0]==0
        first,count=ranges.get(p['address'],(total_objects,0))
        struct.pack_into('<2H',meta,at+4,first,count)
        struct.pack_into('<f',meta,at+24,p['lod_squared'])
    struct.pack_into('<3I',meta,20,total_objects,total_batches,
                     fnv(result[:struct.unpack_from('<I',result,36)[0]]))
    return result,meta,side,dict(far_placements=len(selected),far_objects=len(builder.objs),far_textures=added)


def shared_geometry(stage,data,meta,source,placements,textures):
    """Share full local vertex/index payloads; retain every instance's baked colors.

    Node-local source geometry supplies positions whenever topology matches.
    A scaled instance with different edge subdivision keeps its own topology;
    its inverse transform recovers local positions without merging variants.
    Ordinary city vertices/indices retain their exact byte payloads.
    """
    counts,offsets,raw=sections(data);nt,no,nb,nv,ni=counts
    _,parts=relative_parts(data);objects=parts[1];batches=parts[2]
    h=struct.unpack_from('<4s7I',meta);base=32+16*h[2]
    records={struct.unpack_from('<I',meta,base+32*i+28)[0]:
             struct.unpack_from('<4H3fHHfI',meta,base+32*i) for i in range(h[3])}
    instances=[None]*no;local_batches={};fallback=0;max_error=0.;source_cache={}
    nl.MAXEDGE=120.;nl.BAKED_LIGHT='shibuya' if stage==1 else None
    for p in placements:
        r=records[p['address']]
        for first,count,model,texlist in ((r[0],r[1],p['model'],p['texlist']),
                                        (r[2],r[3],p['far_model'],p['far_texlist'])):
            if not count:continue
            root=nl.mmul(nl.placement_matrix(p['pos'],p['ang']),nl.mscale(*p['scale']))
            nodes=[(n,m) for n,m in tree(source,model,root) if n['model'] and not n['ev']&8]
            assert len(nodes)==count,(stage,p['pid'],count,len(nodes))
            mapping=textures['texlists'][str(texlist)]
            for part,(node,matrix) in enumerate(nodes):
                oi=first+part;assert instances[oi] is None
                obj=struct.unpack_from('<4f4I',raw[1],oi*32)
                cache_key=(node['model'],texlist)
                if cache_key not in source_cache:
                    local=nl.JgsBuilder()
                    local.add_model(source.image,node['model'],EB,IDENTITY,lambda slot:mapping[slot])
                    source_cache[cache_key]=local
                local=source_cache[cache_key]
                assert len(local.objs)==1
                direct=len(local.batches)==obj[5]
                if direct:
                    for j in range(obj[5]):
                        vo,io,vn,ix,_,_=struct.unpack_from('<II4H',raw[2],(obj[4]+j)*16)
                        lv,li,ln,lx,_,_=local.batches[j]
                        if (vn,ix)!=(ln,lx) or raw[4][io*2:(io+ix)*2]!=local.idx[li*2:(li+lx)*2]:direct=False;break
                        if any(raw[3][(vo+v)*24:(vo+v)*24+8]!=local.verts[(lv+v)*24:(lv+v)*24+8] for v in range(vn)):
                            direct=False;break
                if not direct:fallback+=1
                inverse=nl.minv(matrix) if not direct else None
                stored=struct.unpack('<12f',struct.pack('<12f',*matrix[:12]));points=[]
                for j in range(obj[5]):
                    bi=obj[4]+j;vo,io,vn,ix,tex,flags=struct.unpack_from('<II4H',raw[2],bi*16)
                    payload=bytearray(raw[3][vo*24:(vo+vn)*24])
                    for v in range(vn):
                        world=struct.unpack_from('<3f',payload,v*24+12)
                        if direct:localpoint=struct.unpack_from('<3f',local.verts,(local.batches[j][0]+v)*24+12)
                        else:localpoint=nl.xform(inverse,world)
                        struct.pack_into('<3f',payload,v*24+12,*localpoint)
                        localpoint=struct.unpack_from('<3f',payload,v*24+12);points.append(localpoint)
                        reconstructed=nl.xform(stored,localpoint)
                        error=max(abs(a-b) for a,b in zip(world,reconstructed));max_error=max(max_error,error)
                        assert error<.01,(stage,p['pid'],oi,error)
                    assert bi not in local_batches
                    local_batches[bi]=bytes(payload)
                center=tuple(sum(p[k] for p in points)/len(points) for k in range(3))
                radius=max(sum((p[k]-center[k])**2 for k in range(3))**.5 for p in points)
                # Outward padding includes float storage/GE transform roundoff.
                instances[oi]=((*stored,0.,0.,0.,1.),(*center,radius+.001))
                struct.pack_into('<I',objects,oi*32+24,obj[6]|JGS_O_INSTANCE)
    # Existing city banks already share contained/overlapping batch slices.
    # Preserve that sharing by compacting the union of live world ranges;
    # concatenating each batch would silently inflate the city payload.
    def retained_ranges(field,size):
        intervals=[]
        for bi in range(nb):
            if bi in local_batches:continue
            row=struct.unpack_from('<II4H',raw[2],bi*16);start,count=row[field],row[field+2]
            if count:intervals.append((start,start+count))
        merged=[]
        for start,end in sorted(intervals):
            if merged and start<=merged[-1][1]:merged[-1]=(merged[-1][0],max(end,merged[-1][1]))
            else:merged.append((start,end))
        payload=bytearray();starts=[];mapping=[];source_bytes=raw[3+field]
        for start,end in merged:
            if field==1 and (len(payload)//2-start)&1:payload+=b'\0\0'
            starts.append(start);mapping.append((end,len(payload)//size));payload+=source_bytes[start*size:end*size]
        def remap(start,count):
            if not count:return 0
            i=bisect.bisect_right(starts,start)-1
            assert i>=0 and start+count<=mapping[i][0]
            return mapping[i][1]+start-starts[i]
        return payload,remap
    vertices,world_vertex=retained_ranges(0,24);indices,world_index=retained_ranges(1,2)
    if len(indices)&3:indices+=b'\0\0'
    shared={};duplicates=0
    for bi in range(nb):
        vo,io,vn,ix,tex,flags=struct.unpack_from('<II4H',raw[2],bi*16)
        if bi not in local_batches:
            struct.pack_into('<2I',batches,bi*16,world_vertex(vo,vn),world_index(io,ix));continue
        vp=local_batches[bi];ip=raw[4][io*2:(io+ix)*2]
        key=(vp,ip,tex,flags)
        offsets_out=shared.get(key)
        if offsets_out is None:
            offsets_out=(len(vertices)//24,len(indices)//2);shared[key]=offsets_out
            vertices+=vp;indices+=ip
            if len(indices)&3:indices+=b'\0\0'
        else:duplicates+=1
        struct.pack_into('<2I',batches,bi*16,*offsets_out)
    result=pack_bank(data,(nt,no,nb,len(vertices)//24,len(indices)//2),
                     [parts[0],objects,batches,vertices,indices,parts[5]],encode_instances(instances))
    struct.pack_into('<I',meta,28,fnv(result[:struct.unpack_from('<I',result,36)[0]]))
    assert sum(item is not None for item in read_instances(result))==sum(item is not None for item in instances)
    return result,meta,dict(instanced_objects=sum(item is not None for item in instances),
                           shared_batches=duplicates,subdivision_variants=fallback,
                           max_world_position_error=max_error,geometry_bytes_before=nv*24+ni*2,
                           geometry_bytes_after=len(vertices)+len(indices))


def bake(stage,input_path,output_dir):
    original=input_path.read_bytes();source,placements,_=source_records(stage)
    if any(read_instances(original)):
        # Idempotence is explicit. New source inventory requires the retained
        # world-space baseline rather than silently rebaking local coordinates.
        meta=input_path.with_suffix('.jrs').read_bytes();h=struct.unpack_from('<4s7I',meta)
        rows=[struct.unpack_from('<4H3fHHfI',meta,32+16*h[2]+32*i) for i in range(h[3])]
        owners={r[-1]:r for r in rows if r[8]==9}
        assert set(owners)=={p['address'] for p in placements}
        assert all(owners[p['address']][9]==p['lod_squared'] for p in placements)
        output_dir.mkdir(parents=True,exist_ok=True)
        for suffix in ('.jgs','.jrs','.jgs.map.json'):
            source_path=input_path if suffix=='.jgs' else input_path.with_suffix('.jrs') if suffix=='.jrs' else Path(str(input_path)+'.map.json')
            (output_dir/f'stage{stage}{suffix}').write_bytes(source_path.read_bytes())
        return dict(stage=stage,placements=len(placements),already_shared=True)
    report=bake_near(stage,input_path,output_dir);path=output_dir/f'stage{stage}.jgs'
    data=path.read_bytes();meta=bytearray(path.with_suffix('.jrs').read_bytes())
    side=json.loads(Path(str(path)+'.map.json').read_text());textures=texture_source(stage)
    data,meta,side,far=append_far(stage,data,meta,side,source,placements,textures)
    data,meta,shared=shared_geometry(stage,data,meta,source,placements,textures)
    path.write_bytes(data);path.with_suffix('.jrs').write_bytes(meta)
    Path(str(path)+'.map.json').write_text(json.dumps(side))
    report.update(far);report.update(shared);report.update(output_bytes=len(data),output_sha256=hashlib.sha256(data).hexdigest())
    (output_dir/f'stage{stage}_street_props.json').write_text(json.dumps(report,indent=2)+'\n')
    return report


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('stage', type=int, choices=(1, 2, 3, 5))
    parser.add_argument('--input', type=Path)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    print(json.dumps(bake(args.stage, args.input or ROOT/f'psp/stage{args.stage}.jgs', args.out)))
