#!/usr/bin/env python3
"""Restore verified Dreamcast enemy near/far meshes without duplicating motions.

Only source pointer pairs selected by the actor draw callbacks are listed.
The first 51 IDs, near geometry, textures and animation samples are retained.
Rivals use their existing running pair; their separate waiting pair is not
silently substituted. Kairiki's separate attack bank remains independent.
"""
import argparse, hashlib, json, struct, subprocess, sys, tempfile
from pathlib import Path
from jgc_lod import header, lod_models, pack_models, texture
from character_roster import archive, casts, model_bank
from jgc_compact import compact
from ninjalib import parse_plist
from cutscene_sources import pvr_offsets

ROOT=Path(__file__).resolve().parents[2];P=ROOT/'psp';D=ROOT/'decompile'
# public model, near/far executable pointer slots, constructor/draw addresses.
PAIRS=(
 (16,0x8c1b5ae0,0x8c1b5ae4,'076310/0778b4'),
 (17,0x8c1b5b90,0x8c1b5b98,'0796c4/0778b4'),
 (48,0x8c1b5b94,0x8c1b5b9c,'079bd0/0778b4'),
 (18,0x8c1b5c68,0x8c1b5c6c,'0949c4/0778b4'),
 (30,0x8c1b5cb4,0x8c1b5cb8,'07b104/0778b4'),
 (32,0x8c1b5d50,0x8c1b5d54,'07b810/0778b4'),
 (49,0x8c1b5d58,0x8c1b5d5c,'07b810/0778b4'),
 (31,0x8c1b5dd8,0x8c1b5ddc,'08d304/0778b4'),
 (27,0x8c1b5e84,0x8c1b5e88,'07c5b0/0778b4'),
 (28,0x8c1b5f44,0x8c1b5f48,'099adc/099ed4'),
 (46,0x8c1b5f3c,0x8c1b5f40,'099adc/099ed4'),
 (33,0x8c1b5fd0,0x8c1b5fd4,'07dc1c/0778b4'),
 (50,0x8c1b5fc8,0x8c1b5fcc,'07dc1c/0778b4'),
 (38,0x8c1b6094,0x8c1b6098,'09bfe8/09c2d0'),
 (39,0x8c1b60d8,0x8c1b60e0,'0859f0/085e5c'),
 (40,0x8c1b65c0,0x8c1b65c4,'0f93a0/0f9814'),
 (42,0x8c1b6548,0x8c1b6560,'0dfe2c/0dfa7c'),
 (43,0x8c1b654c,0x8c1b6564,'0dfe2c/0dfa7c'),
 (44,0x8c1b6550,0x8c1b6568,'0dfe2c/0dfa7c'),
)
EXISTING_FAR={28:45,46:47}
FAR_TEXLIST={42:0x8c1b6590,43:0x8c1b6594,44:0x8c1b6598}

def far_texture_mapping(exe,slot,texture_data):
    """Decode user texture-list bindings; only the input location is public."""
    def word(address):
        offset=address-0x8c010000
        if offset<0 or offset+4>len(exe):
            raise ValueError('Far texture list outside user executable')
        return struct.unpack_from('<I',exe,offset)[0]
    tl=word(slot);count=word(tl+4)
    offsets=sorted(pvr_offsets(texture_data))
    if not count or count>len(offsets):
        raise ValueError('Invalid far texture list size')
    return tuple(offsets.index(word(tl+8+i*4)-0x8ca00000) for i in range(count))

def source_tree(data,root):
    result=[];stack=[(root,-1)];seen=set()
    while stack:
        address,parent=stack.pop();assert address not in seen;seen.add(address)
        off=address-0x8ca00000;assert 0<=off<=len(data)-52
        words=struct.unpack_from('<13I',data,off);index=len(result)
        triangles=0
        if words[1]:
            polygon=struct.unpack_from('<I',data,words[1]-0x8ca00000+4)[0]
            triangles=len(parse_plist(data,polygon-0x8ca00000))
        result.append((parent,data[off:off+4]+data[off+8:off+44],triangles))
        if words[12]:stack.append((words[12],parent))
        if words[11]:stack.append((words[11],index))
    return result

def geometry_hash(data):
    h=header(data);digest=2166136261
    for i,v in enumerate(data[h[10]:h[10]+h[3]*28]):
        if not 8<=i%28<12:digest=((digest^v)*16777619)&0xffffffff
    return digest

def build():
    original=(P/'chars.jgc').read_bytes();light=(P/'playerlight.bin').read_bytes()
    oh=header(original);assert light[:4]==b'PLT1' and len(light)==20+oh[3]*20
    assert struct.unpack_from('<3I',light,4)==(oh[3],oh[0],geometry_hash(original))
    exe=(D/'extracted/1ST_READ.BIN').read_bytes();rows=(D/'chars_spec.txt').read_text().splitlines()
    refs=[(original,i,original,i) for i in range(51)];directory={};light_sources={id(original):light};report=[]
    with tempfile.TemporaryDirectory(prefix='jgr-source-enemy-lod-') as td:
        for ci,near_slot,far_slot,callbacks in PAIRS:
            near=struct.unpack_from('<I',exe,near_slot-0x8c010000)[0]
            far=struct.unpack_from('<I',exe,far_slot-0x8c010000)[0]
            fields=rows[ci].split(':');raw=(D/fields[1]).read_bytes()
            nt,ft=source_tree(raw,near),source_tree(raw,far)
            assert len(nt)==len(ft) and [r[0] for r in nt]==[r[0] for r in ft],(ci,'source bone order')
            b0,bn=struct.unpack_from('<2I',original,oh[7]+ci*48);assert bn==len(nt)
            for bi,(parent,bind,_) in enumerate(nt):
                bone=original[oh[8]+(b0+bi)*48:oh[8]+(b0+bi+1)*48]
                assert struct.unpack_from('<h',bone)[0]==(-1 if parent<0 else b0+parent)
                assert bone[2:40]==bind[:2]+bind[4:],(ci,bi,'current near differs from source')
            if ci in EXISTING_FAR:
                index=EXISTING_FAR[ci];refs[index]=(original,index,original,ci)
            else:
                options=[s for s in fields[4:] if s.startswith(('tex=','texmap=','texbinding=','material=','cull='))]
                if ci in FAR_TEXLIST:
                    mapping=far_texture_mapping(exe,FAR_TEXLIST[ci],(D/fields[2]).read_bytes())
                    options=[s for s in options if not s.startswith(('tex=','texmap=','texbinding='))]
                    options.append('texmap='+','.join(map(str,mapping)))
                spec=':'.join([fields[0]+'Low',*fields[1:4],'rawat','8ca00000',f'{far:x}','0',*options])
                path=Path(td)/f'{ci}.jgc';lp=Path(td)/f'{ci}.plt'
                # fire_body_assets.py's corrected PVR8 is already resident
                # at128; all other original actor atlases retain the256 cap.
                cap=128 if ci in (33,50) else 256
                subprocess.run([sys.executable,'tools/jgc_convert.py',str(path),spec,f'--texmax={cap}','--lighting-sidecar='+str(lp)],
                               cwd=D,check=True,capture_output=True)
                data=path.read_bytes();light_sources[id(data)]=lp.read_bytes();index=len(refs)
                refs.append((data,0,original,ci))
            directory[ci]=index
            report.append(dict(model=ci,name=fields[0],near_slot=hex(near_slot),far_slot=hex(far_slot),
                               near_root=hex(near),far_root=hex(far),callbacks=callbacks,
                               bones=len(nt),near_triangles=sum(t[2] for t in nt),far_triangles=sum(t[2] for t in ft),far_model=index))
        original_textures={texture(original,i) for i in range(oh[5])}
        for data,_,_,owner in refs:
            missing=[i for i in range(header(data)[5]) if texture(data,i) not in original_textures]
            assert not missing or owner==44, ('unexpected new far texture',owner,missing)
        shared,sources,_=pack_models(refs,directory,seed_textures=original)
        records=bytearray()
        for bank,v in sources:records.extend(light_sources[id(bank)][20+v*20:20+(v+1)*20])
        sh=header(shared);newlight=b'PLT1'+struct.pack('<4I',sh[3],sh[0],geometry_hash(shared),20)+records
        packed=compact(shared);roster=archive(packed,newlight)
    assert lod_models(shared)==directory
    assets={'chars.jgc':shared,'chars_compact.jgc':packed,'chars_roster.bin':roster,'playerlight.bin':newlight}
    manifest={'models':report,'public_models':51,'private_far_models':len(refs)-51,
              'textures_added':len({texture(shared,i) for i in range(sh[5])}-original_textures),
              'texture_bytes_added':sum(1024+t[0]*t[1] for t in ({texture(shared,i) for i in range(sh[5])}-original_textures)),
              'motion_policy':'exact existing near channel/key ranges shared by each far model',
              'limitations':['Rival waiting-state alternate pairs and Kairiki attack-bank alternate pairs remain separate existing paths.'],
              'artifacts':{name:{'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()} for name,data in assets.items()}}
    return assets,manifest

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');a=ap.parse_args()
    assets,manifest=build()
    for name,data in assets.items():
        if a.check:assert (P/name).read_bytes()==data,name
        else:(P/name).write_bytes(data)
    if not a.check:
        (P/'enemy_lod.json').write_text(json.dumps(manifest,indent=2)+'\n')
        info=json.loads((P/'playerlight.json').read_text());h=header(assets['chars.jgc'])
        info.update(bytes=len(assets['playerlight.bin']),sha256=manifest['artifacts']['playerlight.bin']['sha256'],
                    shared_bank_sha256=manifest['artifacts']['chars.jgc']['sha256'],vertices=h[3],models=h[0])
        (P/'playerlight.json').write_text(json.dumps(info,indent=2)+'\n')
    print(json.dumps(manifest,indent=2))
if __name__=='__main__':main()
