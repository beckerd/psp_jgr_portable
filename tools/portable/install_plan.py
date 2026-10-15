#!/usr/bin/env python3
"""Build payload-free on-device conversion instructions and report coverage.

The default refuses an incomplete converter catalog. --development emits a
diagnostic plan; it cannot turn an incomplete installation into a playable one.
Only source paths, formats, layout parameters, sizes and hashes are serialized.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
from package import safe_name,asset_manifest,LAYOUT_VERSION

MAGIC=b'JGRINS1\0'
HEADER=struct.Struct('<8s8I')
RECORD=struct.Struct('<6I32s192s192s')
ROOT=Path(__file__).resolve().parents[2]

# Parameters describe reviewed file-format operations, never converted payloads.
CONVERSIONS={
    'title_lists.bin': (3,'decompile/afs/U_TITLE/000_titledat.bin',(0x8cf00000,40,6,0)),
    'markedit_lists.bin': (3,'decompile/afs/E_MARKEDIT/001_markeditdat.bin',(0x8c900000,0,96,0)),
    'title_ui.bin': (2,'decompile/afs/U_SEGALOGO/002_logo_Stxr.bin',(512,0,0,0)),
    'garage_disp.bin': (2,'decompile/afs/U_GARAGE/000_garage_disptxr.bin',(512,0,0,0)),
    'markedit_disp.bin': (2,'decompile/afs/E_MARKEDIT/000_edittxr.bin',(512,0,0,0)),
    'practicehud_tex.bin': (2,'decompile/afs/E_TUTORIAL/001_tutorialtxr.bin',(512,0,0,0)),
}

def conversion_recipes():
    recipes=dict(CONVERSIONS)
    kinds={name:10+i for i,name in enumerate(('ctrl','graph','groups','obj','peds','police','rivals','waves','cans'))}
    for name,row in json.loads(Path(__file__).with_name('mission_sets.json').read_text())['files'].items():
        kind=name.rsplit('_',1)[1].removesuffix('.bin')
        args=row['arguments']
        sectors=int(args[1]) if args else 0
        if args and (len(args)!=2 or args[0]!='--sectors' or not 1<=sectors<=128):
            raise ValueError('Unexpected mission conversion arguments')
        recipes[name]=(kinds[kind],row['source'],(sectors,0,0,0))
    recipes['set_police.bin']=recipes['set3_police.bin']
    from tables_recipes import recipes as table_recipes
    for table in table_recipes():
        if table['special'] != 68:  # Replay fills this ABI only after validated observation.
            recipes[table['output']]=(20,table['source'],(0,0,0,0))
    from misc_recipes import recipes as misc_recipes
    recipes.update(misc_recipes())
    from scene_recipes import recipes as scene_recipes
    recipes.update(scene_recipes())
    from graffiti_recipes import recipes as graffiti_recipes
    recipes.update(graffiti_recipes())
    recipes['kogane_surface.bin']=(71,'decompile/extracted/1ST_READ.BIN',(0,0,0,0))
    recipes['grind_lights.jgs']=(88,'decompile/extracted/1ST_READ.BIN',(0,0,0,0))
    recipes['spray_bubble.jsd']=(90,'decompile/afs/PLAYER/016_player_effect_dat.bin',(0,0,0,0))
    from resource_recipes import recipes as resource_recipes
    recipes.update(resource_recipes())
    from model_recipes import recipes as model_recipes
    for name,job in model_recipes().items():
        recipes[name]=(job['operation'],job['source'],(0,0,0,0),job['block'])
    from world_recipes import recipes as world_recipes
    recipes.update(world_recipes())
    for stage in (1,2,3,5,6):
        recipes[f'stage{stage}_obj.jgs']=(60,f'decompile/afs/STAGE{stage}/000_st{stage}dat.bin',(stage,0,0,0))
    for stage in (1,2,3,5,6):
        source=f'decompile/afs/STAGE{stage}/000_st{stage}dat.bin'
        recipes[f'stage{stage}.jgs']=(63,source,(stage,0,0,0))
        recipes[f'stage{stage}.jrs']=(64,source,(stage,0,0,0))
    recipes.update(derived_recipes(recipes))
    for city in (1,2,3,5):
        source=f'people{city}_compact.jgc'
        if source in recipes:
            recipes[f'people{city}_native.jpn']=(92,source,(city,0,0,0))
    if 'chars_compact.jgc' in recipes and 'playerlight.bin' in recipes:
        recipes['chars_roster.bin']=(89,'chars_compact.jgc',(0,0,0,0))
    from replay_recipes import recipes as replay_recipes
    recipes.update(replay_recipes())
    if 'title_route.bin' not in recipes:
        recipes.pop('source_data_ptr_mission_cast.jsd',None)
    return recipes

def derived_recipes(available):
    """Register sidecars only when all native generated-asset dependencies exist.

    Stage object banks are insufficient: sidecars bind the FINAL city JGS,
    and visibility additionally needs final JRS renderer ownership metadata.
    Keep this guard until the base/post-render native stage providers land.
    """
    result={}
    for stage in (1,2,3,5,6):
        jgs,jrs=f'stage{stage}.jgs',f'stage{stage}.jrs'
        if jgs not in available:continue
        result[f'stage{stage}.jgm']=(61,jgs,(stage,0,0,0))
        if jrs in available:result[f'stage{stage}.jvo']=(62,jgs,(stage,0,0,0))
    return result

def make_plan(catalog,raw,conversions=None,manifest_crc=0,replay_profile='original',native_identity=None,source_root=ROOT):
    from replay_identity import select_catalog
    catalog=select_catalog(catalog,replay_profile,native_identity,source_root)
    conversions=conversion_recipes() if conversions is None else conversions
    rows=sorted(catalog['files'],key=lambda r:r['path'])
    if not rows or len(rows)>20000:
        raise ValueError('Invalid asset count')
    conversions=model_derivatives(rows,conversions)
    if replay_profile=='native-v1':
        from replay_identity import native_outputs
        native=native_outputs()
        if native.intersection(raw) or any(name not in conversions or conversions[name][0]!=91 for name in native):
            raise ValueError('Native replay profile requires every native replay converter')
        conversions={name:(job[0],job[1],(1,0,0,0)) if job[0]==91 else job
                     for name,job in conversions.items()}
    if any(type(row.get('crc32')) is not int or not 0<=row['crc32']<=0xffffffff for row in rows):
        raise ValueError('Asset catalog requires a CRC32 for every file')
    expected_crc=zlib.crc32(asset_manifest(rows)) & 0xffffffff
    if manifest_crc and manifest_crc!=expected_crc:
        raise ValueError('Manifest identity does not match the asset catalog')
    manifest_crc=expected_crc
    body=bytearray();appendix=bytearray();seen=set();implemented=[];missing=[]
    for row in rows:
        name=safe_name(row['path'])
        if name in seen or name.startswith(('data/','jgr_','shot_')) or name in ('assets-manifest.txt','level.txt'):
            raise ValueError('Duplicate or reserved asset name: '+name)
        seen.add(name)
        size=row['bytes'];digest=row['sha256']
        if type(size) is not int or not 0<=size<=0x7fffffff or len(digest)!=64:
            raise ValueError('Invalid asset metadata: '+name)
        digest=bytes.fromhex(digest)
        # Recipes use CRC32 for bulk asset integrity; the source executable
        # still has its independent SHA-256 fingerprint in the disc reader.
        # Keep the record width so model appendix offsets remain unchanged.
        digest=struct.pack('<I',row['crc32'])+bytes(28)
        op,source,args=0,'',(0,0,0,0)
        if name in raw:
            op,source=1,safe_name(raw[name])
        elif name in conversions:
            conversion=conversions[name]
            op,source,args=conversion[:3]
            if len(conversion)==4:
                block=conversion[3]
                if op!=30 or not isinstance(block,bytes) or not block or args[0] or args[1]:
                    raise ValueError('Invalid model instructions: '+name)
                args=(HEADER.size+RECORD.size*len(rows)+len(appendix),len(block),*args[2:])
                appendix+=block
            elif len(conversion)!=3:
                raise ValueError('Invalid conversion: '+name)
            safe_name(source)
        if op:
            implemented.append(name)
        else:
            missing.append(name)
        if len(args)!=4 or any(type(a) is not int or not 0<=a<=0xffffffff for a in args):
            raise ValueError('Invalid conversion parameters: '+name)
        body+=RECORD.pack(op,size,*args,digest,name.encode('ascii'),source.encode('ascii'))
    if len(appendix)>4*1024*1024-HEADER.size-len(body):
        raise ValueError('Conversion plan exceeds bundled recipe limit')
    header=HEADER.pack(MAGIC,4 if replay_profile=='native-v1' else 3,RECORD.size,len(rows),len(implemented),
                       zlib.crc32(body+appendix),manifest_crc,LAYOUT_VERSION,len(appendix))
    plan=header+body+appendix
    report=dict(schema=1,complete=not missing,files=len(rows),available=len(implemented),
                missing=missing,implemented=implemented,sha256=hashlib.sha256(plan).hexdigest(),
                bytes=len(plan),contains_game_payload=False,replay_profile=replay_profile,asset_integrity='crc32')
    return plan,report

def model_derivatives(rows,conversions):
    """Reuse complete siblings only when metadata proves the same conversion.

    Every dependency is a phase-zero model in this exact plan. A subset lacking
    the sibling retains its standalone source converter, including repair tools.
    """
    conversions=dict(conversions)
    models={r['path']:conversions[r['path']] for r in rows
            if r['path'] in conversions and len(conversions[r['path']])==4
            and conversions[r['path']][0]==30}
    normal={}
    for name in sorted(models):
        block=models[name][3]
        if not struct.unpack_from('<I',block,24)[0]&24:normal.setdefault(block,name)
    derived={}
    for name,job in models.items():
        block=bytearray(job[3]);flags=struct.unpack_from('<I',block,24)[0]
        if flags&24:
            struct.pack_into('<I',block,24,flags&~24)
            source=normal.get(bytes(block))
            if source:derived[name]=(source,2 if flags&16 else 1)
    groups={}
    for row in rows:
        if row['path'] in models:
            groups.setdefault((row['bytes'],row['sha256']),[]).append(row['path'])
    for names in groups.values():
        source=next((n for n in names if n not in derived),names[0])
        for name in names:
            if name!=source:derived[name]=derived.get(source,(source,0))
    for name,(source,mode) in derived.items():
        seen={name}
        while source in derived:
            if source in seen:raise ValueError('Cyclic model derivative')
            seen.add(source);source,next_mode=derived[source]
            if mode and next_mode:raise ValueError('Nested model transformation')
            mode=mode or next_mode
        conversions[name]=(93,source,(mode,0,0,0))
    return conversions

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--catalog',type=Path,default=Path(__file__).with_name('asset_catalog.json'))
    ap.add_argument('--raw',type=Path,default=Path(__file__).with_name('raw_assets.json'))
    ap.add_argument('--out',type=Path,required=True)
    ap.add_argument('--report',type=Path,required=True)
    ap.add_argument('--manifest-crc',type=lambda v:int(v,0),default=0)
    ap.add_argument('--replay-profile',choices=('original','native-v1'),default='native-v1')
    ap.add_argument('--native-identity',type=Path)
    ap.add_argument('--development',action='store_true')
    ap.add_argument('--replace',action='store_true',help='Replace previous generated build metadata')
    a=ap.parse_args()
    plan,report=make_plan(json.loads(a.catalog.read_text()),json.loads(a.raw.read_text())['files'],manifest_crc=a.manifest_crc,replay_profile=a.replay_profile,
                          native_identity=json.loads(a.native_identity.read_text()) if a.native_identity else None)
    if not a.replace and (a.out.exists() or a.report.exists()):
        ap.error('Outputs must be new paths')
    if any(p.is_symlink() or (p.exists() and not p.is_file()) for p in (a.out,a.report)):
        ap.error('Output must be an ordinary metadata file')
    a.report.parent.mkdir(parents=True,exist_ok=True)
    a.report.write_text(json.dumps(report,indent=2)+'\n')
    if not report['complete'] and not a.development:
        ap.exit(2,f"Native converters cover {report['available']}/{report['files']} assets; incomplete release plan refused.\n")
    a.out.parent.mkdir(parents=True,exist_ok=True)
    a.out.write_bytes(plan)
    print(f"Native conversion plan: {report['available']}/{report['files']} assets; complete={report['complete']}")

if __name__=='__main__':
    main()
