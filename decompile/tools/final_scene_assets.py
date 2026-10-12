#!/usr/bin/env python3
"""Source-bound EVENT_6198 (descriptor 103) actors; not campaign completion.

Roots/texlists come from opcode 2d, motions from 31, and texture bindings
from 56's table. The actor world track is left intact in scenes.bin. No
matching-by-name, approximate poses or synthesized animation is involved.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[2]
DEC=ROOT/'decompile'
BASE=0x8c900000
DAT='afs/EVENT/164_event_6198.bin'
TXR='afs/EVENT/165_event_6198txr.bin'

def manifest(descriptor=103):
    assert descriptor in (90,103)
    dat,texture=(DAT,TXR) if descriptor==103 else ('afs/EVENT/138_event_3025.bin','afs/EVENT/139_event_3025txr.bin')
    data=(DEC/dat).read_bytes();txr=(DEC/texture).read_bytes()
    def u(a):
        assert BASE<=a<=BASE+len(data)-4 and not a&3,hex(a)
        return struct.unpack_from('<I',data,a-BASE)[0]
    def commands(p):
        for k in range(1024):
            words=tuple(u(p+j*4) for j in range(5));yield words
            if words[0]==100:return
            p+=20
        raise ValueError('unterminated scene stream')
    offsets={};at=0
    while (at:=txr.find(b'GBIX',at))>=0:offsets[at]=len(offsets);at+=8
    setup=list(commands(u(BASE)));main=list(commands(u(BASE+4)))
    table=next(w[1] for w in setup if w[0]==0x56);lists={}
    while u(table):
        tl,ptr=u(table),u(table+4);count=u(tl+4);assert 0<count<=256
        lists[tl]=[offsets[u(ptr+4*k)-0x8cda0000] for k in range(count)]
        table+=8
    rows=[]
    for w in main:
        if w[0]!=0x2d:continue
        slot,root,texlist,flags=w[1:]
        motions=[q[2] for q in main if q[0] in (0x30,0x31,0x32,0x33,0x34) and q[1]==slot]
        assert len(motions)==1
        if descriptor==103:assert flags==2 and u(motions[0])&65535==0x11
        else:assert u(motions[0]+8)==0x30007
        track=next((q[2] for q in main if q[0]==0x2f and q[1]==slot),0)
        if not texlist:
            # EVENT_3025 slot 2 is entirely untextured. Verify that, rather
            # than guessing which actor's texture list was bound previously.
            from ninjalib import read_object,parse_plist
            pending=[root]
            while pending:
                node=read_object(data,pending.pop(),BASE)
                if node['model']:
                    plist=u(node['model']+4)
                    if plist:assert all(t[0]==0xffff for t in parse_plist(data,plist-BASE,source_environment=True))
                pending.extend(p for p in (node['child'],node['sib']) if p)
        rows.append(dict(slot=slot,root=root,texlist=texlist,flags=flags,
            texture_map=lists[texlist] if texlist else [],motions=motions,
            frames=[u(m)>>16 if descriptor==103 else u(m+4) for m in motions],world_track=track))
        if descriptor==90 and slot==2:
            # 234ea8 -> 23471e -> 231a04 emits the beam's vertex-colour
            # strips without software face rejection. All 34 live headers
            # also use ISP c8800000 / CullMode 1, not hardware face culling.
            rows[-1]['cull']='none'
    assert len(rows)==(2 if descriptor==103 else 4)
    info=dict(descriptor=descriptor,source=dat,textures=texture,models=rows,
        source_sha256=hashlib.sha256(data).hexdigest(),textures_sha256=hashlib.sha256(txr).hexdigest(),
        lighting='JGC3: unbaked material/local normals; flags 2 cel 2956e0, otherwise normal 2337b4',
        campaign_enabled=False,remaining=['ADX streaming-job boundaries and spatial mix','matched final-phase rendering and campaign verification'])
    if descriptor==90:
        info['animation_format']='Original sparse NJS_MDATA3 retained in scenes.bin; no dense CNK conversion'
        info['remaining']=['SMSB/effect sequencer and spatial mix','matched final-phase rendering and campaign verification']
    return info

def export(path,descriptor=103):
    info=manifest(descriptor);args=[sys.executable,str(DEC/'tools/jgc_convert.py'),str(path),'--motions=1','--source-material=1','--source-environment=1']
    for r in info['models']:
        motion=r['motions'][0] if descriptor==103 else 0
        spec=f"final_scene_{r['slot']}:{info['source']}:{info['textures']}:0,0,0,0:rawat:8c900000:{r['root']:x}:{motion:x}"
        if r['texture_map']:spec+=':texmap='+','.join(map(str,r['texture_map']))
        if 'cull' in r:spec+=':cull='+r['cull']
        spec+=':lighting='+('2' if r['flags']&2 else '1')
        args.append(spec)
    subprocess.run(args,cwd=DEC,check=True);return info

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');ap.add_argument('--scene',type=int,choices=[90,103],default=103);args=ap.parse_args()
    output=ROOT/('psp/scene6198.jgc' if args.scene==103 else 'psp/scene3025.jgc')
    if args.check:
        with tempfile.TemporaryDirectory(prefix='jgr-final-scene-') as tmp:
            p=Path(tmp)/output.name;info=export(p,args.scene)
            assert p.read_bytes()==output.read_bytes()
            assert json.loads(output.with_suffix('.json').read_text())==info
    else:
        info=export(output,args.scene);output.with_suffix('.json').write_text(json.dumps(info,indent=2)+'\n')
if __name__=='__main__':main()
