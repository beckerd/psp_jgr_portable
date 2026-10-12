#!/usr/bin/env python3
"""Original 08920c impact and 0a90cc helicopter projectile draw trees.

Executable root/motion/texture bindings from 1b66a0 and 1b6380/1b64d0.
"""
import argparse,json,subprocess,sys,tempfile,struct
from pathlib import Path
from cutscene_sources import executable,texture_lists,afs_file,fingerprint
ROOT=Path(__file__).resolve().parents[2];DEC=ROOT/'decompile'
def hardpoints():
    e=executable();rows=[]
    # Descriptor high word is point count, low word is the attachment bone.
    descriptors=[e.u(0x8c1c92cc+4*i) for i in range(4)]
    descriptors += [e.u(e.u(0x8c0a7d20)),e.u(e.u(0x8c09c2ac)),e.u(e.u(0x8c0a3730))]
    for d in descriptors:
        shape=e.u(d);p=e.u(d+4)
        for i in range(shape>>16):
            xyz=[struct.unpack('<f',struct.pack('<I',e.u(p+12*i+4*j)))[0] for j in range(3)]
            rows.append('    {'+str(shape&65535)+',{'+','.join(v.hex()+'f' for v in xyz)+'}},')
    # Denki's three-entry attachment program: torso, left hand, right hand.
    d=e.u(0x8c21bc20)
    for k in (1,2,0):
        shape=e.u(d+12*k);p=e.u(d+12*k+4)
        assert shape>>16==1
        xyz=struct.unpack('<3f',e.read(p,12))
        rows.append('    {'+str(shape&65535)+',{'+','.join(v.hex()+'f' for v in xyz)+'}},')
    return '/* Generated from original launcher and muzzle attachment descriptors. */\nstatic const struct {int bone;float pos[3];} ENEMY_HARDPOINTS[] = {\n'+'\n'.join(rows)+'\n};\n'
def manifest():
    e=executable();models=[];sources=[]
    for variant,dat,txr,base,binding,table in [
        (-1,45,44,0x8cff0000,0x8c1b9590,0x8c1b66a0),
        (0,25,24,0x8ca00000,0x8c1c98f0,0x8c1b6380),
        (1,29,28,0x8ca00000,0x8c1c9798,0x8c1b64d0)]:
        dp=afs_file('ENEMY',dat);tp=afs_file('ENEMY',txr)
        maps,offsets=texture_lists(e,binding,tp.read_bytes(),0x8c800000 if variant<0 else base)
        source=dict(data=fingerprint(dp),textures=fingerprint(tp),base=base,offsets=offsets)
        si=len(sources);sources.append(source);data=dp.read_bytes()
        for part in range(1 if variant<0 else 3):
            if variant<0: root,motion,tl=(e.u(table+4*i) for i in range(3))
            else:
                root=e.u(table+(0 if part==0 else 0x10 if part==1 else 0x14))
                motion=e.u(table+(4 if part==0 else 0x18 if part==1 else 0x1c))
                tl=e.u(table+(-4 if part==0 else 0xc))
            assert base<=root<base+len(data)-52 and base<=motion<base+len(data)-8
            frames=struct.unpack_from('<I',data,motion-base)[0]>>16
            models.append(dict(name='shotimpact' if variant<0 else f'missile{variant}_{part}',source=si,root=root,motion=motion,frames=frames,texture_map=maps[tl]))
    s=sources[0];maps,_=texture_lists(e,0x8c1b9590,afs_file('ENEMY',44).read_bytes(),0x8c800000)
    data=afs_file('ENEMY',45).read_bytes()
    for table in (0x8c1b66ec,0x8c1b66e0):
        root,motion,tl=(e.u(table+4*i) for i in range(3))
        frames=struct.unpack_from('<I',data,motion-0x8cff0000)[0]>>16
        models.append(dict(name=f'explosion{len(models)-7}',source=0,root=root,motion=motion,frames=frames,texture_map=maps[tl]))
    for variant,binding,table in [(0,0x8c1c98f0,0x8c1b6380),(1,0x8c1c9798,0x8c1b64d0)]:
        maps,_=texture_lists(e,binding,afs_file('ENEMY',24+variant*4).read_bytes(),0x8ca00000)
        model=dict(models[3+variant*3]);model['name']=f'missile{variant}_trail'
        model['texture_map']=[maps[e.u(table+8)][0]]*len(model['texture_map']);models.append(model)
    dp=afs_file('ENEMY',37);tp=afs_file('ENEMY',36);base=0x8ca00000
    maps,offsets=texture_lists(e,0x8c21bb80,tp.read_bytes(),base)
    si=len(sources);sources.append(dict(data=fingerprint(dp),textures=fingerprint(tp),base=base,offsets=offsets))
    for k in range(2):
        root=e.u(0x8c1b6674);motion=e.u(0x8c1b667c+4*k)
        frames=struct.unpack_from('<I',dp.read_bytes(),motion-base)[0]>>16
        models.append(dict(name='electric_hit' if k else 'electric_wave',source=si,root=root,motion=motion,frames=frames,texture_map=maps[e.u(0x8c1b6678)]))
    # 0fd4e4 rotates the ten-entry private texture list once per draw.
    # Tiny immutable variants preserve that binding without mutating meshes.
    tl=maps[e.u(0x8c1b6678)]
    for arc in range(3):
        for phase in range(10):
            models.append(dict(name=f'denki_arc{arc}_{phase}',source=si,root=e.u(0x8c1b6684+arc*4),motion=0,frames=0,texture_map=tl[phase:]+tl[:phase]))
    motion=e.u(0x8c1b6698)
    models.append(dict(name='denki_glow',source=si,root=e.u(0x8c1b6690),motion=motion,frames=struct.unpack_from('<I',dp.read_bytes(),motion-base)[0]>>16,texture_map=maps[e.u(0x8c1b6694)]))
    dp=afs_file('ENEMY',9);tp=afs_file('ENEMY',8);base=0x8ca00000
    maps,offsets=texture_lists(e,0x8c1b7c80,tp.read_bytes(),base)
    si=len(sources);sources.append(dict(data=fingerprint(dp),textures=fingerprint(tp),base=base,offsets=offsets))
    motion=e.u(0x8c1b5dd4)
    models.append(dict(name='bomb_blast_body',source=si,root=e.u(0x8c1b5dd0),motion=motion,frames=struct.unpack_from('<H',dp.read_bytes(),motion-base+2)[0],texture_map=maps[e.u(0x8c1b5d60)]))
    return dict(sources=sources,models=models)
def export(path):
    info=manifest();args=[sys.executable,'tools/jgc_convert.py',str(path),'--source-material=1','--source-environment=1']
    # jgc_convert's normal PVR scan order equals these complete enemy loader tables.
    for r in info['models']:
        s=info['sources'][r['source']]
        args.append(f"{r['name']}:{s['data']['source']}:{s['textures']['source']}:0,0,0,0:rawat:{s['base']:x}:{r['root']:x}:{r['motion']:x}:texmap="+','.join(map(str,r['texture_map'])))
    subprocess.run(args,cwd=DEC,check=True);return info
if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');a=ap.parse_args();p=ROOT/'psp/enemyprojectiles.jgc'
    if a.check:
        assert (ROOT/'psp/src/enemy_hardpoints.inc').read_text()==hardpoints()
        with tempfile.TemporaryDirectory(prefix='jgr-projectiles-') as t:
            q=Path(t)/p.name;info=export(q);assert p.read_bytes()==q.read_bytes();assert json.loads(p.with_suffix('.json').read_text())==info
    else:
        p.with_suffix('.json').write_text(json.dumps(export(p),indent=2)+'\n')
        (ROOT/'psp/src/enemy_hardpoints.inc').write_text(hardpoints())
