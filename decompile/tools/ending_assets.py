#!/usr/bin/env python3
"""Recover ending movie descriptors and the ORIGINAL staff-roll playlist.

No guessed credits, renamed actors, generated cameras, or combined input
streams. The nine staff-roll streams are sequential task-19 lifetimes.
This export does not enable the unfinished campaign ending adapter.
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
B=0x8c800000

def sha(b):return hashlib.sha256(b).hexdigest()
def manifest():
    ex=(DEC/'extracted/1ST_READ.BIN').read_bytes()
    ew=lambda a:struct.unpack_from('<I',ex,a-0x8c010000)[0]
    movies=[]
    for movie in (25,26):
        name=f'DEMO{movie+1:02d}';dat=f'afs/{name}/000_{name.lower()}dat.bin';txr=f'afs/{name}/001_{name.lower()}txr.bin'
        d=(DEC/dat).read_bytes();t=(DEC/txr).read_bytes()
        def u(p):
            assert B<=p<=B+len(d)-4 and not p&3,hex(p)
            return struct.unpack_from('<I',d,p-B)[0]
        def array(p):
            for k in range(256):
                q=u(p+k*4)
                if not q:return
                yield q
            raise ValueError('unterminated ending list')
        offsets={};p=0
        while (p:=t.find(b'GBIX',p))>=0:offsets[p]=len(offsets);p+=8
        lists={};p=u(B+8)
        while u(p):
            tl,images=u(p),u(p+4);count=u(tl+4)
            lists[tl]=[offsets[u(images+4*k)-B] for k in range(count)];p+=8
        segments=[]
        for index,seg in enumerate(array(u(B+4))):
            camera,light,models=(u(seg+i*4)for i in range(3))
            initial,motion,flags=(u(camera+i*4)for i in range(3))
            assert u(motion+8)==0x401c1
            actors=[]
            for actor in array(models):
                root,mot,tl,af=(u(actor+i*4)for i in range(4))
                assert af in (1,2,0x20,0x81) and (not mot or u(mot+8)in (0x30007,0x20003))
                if not tl:
                    from ninjalib import read_object,parse_plist
                    pending=[root]
                    while pending:
                        obj=read_object(d,pending.pop(),B)
                        if obj['model']:
                            plist=u(obj['model']+4)
                            if plist:assert all(r[0]==0xffff for r in parse_plist(d,plist-B,source_environment=True))
                        pending.extend(q for q in (obj['child'],obj['sib'])if q)
                actors.append(dict(address=actor,root=root,motion=mot,flags=af,texlist=tl,
                    texture_map=lists[tl]if tl else [],frames=u(mot+4)if mot else 0))
            voice=struct.unpack('<i',struct.pack('<I',ew(ew(0x8c1e5284+movie*4)+index*4)))[0]
            segments.append(dict(address=seg,camera=initial,camera_motion=motion,camera_flags=flags,
                camera_frames=u(motion+4),light=light,voice=voice,actors=actors))
        movies.append(dict(movie=movie,source=dat,textures=txr,source_sha256=sha(d),texture_sha256=sha(t),segments=segments))
    d=(DEC/'afs/PLAYDEMO/004_staffroll.bin').read_bytes();base=0x8cfe0000
    u=lambda p:struct.unpack_from('<I',d,p-base)[0]
    records=[]
    for p in range(base,base+len(d)-20,20):
        w=struct.unpack_from('<5I',d,p-base)
        if not w[0]:break
        row=dict(address=p,words=w)
        if w[0]>1:
            stream=w[0];q=stream+32;raw=bytearray()
            while q+4<=base+len(d):
                rec=d[q-base:q-base+4];raw+=rec;q+=4
                if rec[0]&128:break
            assert raw and raw[-4]&128
            row['input_records']=len(raw)//4;row['input_sha256']=sha(raw)
            row['stream_header']=struct.unpack_from('<If3fIiI',d,stream-base)
            cameras=[];q=w[4]
            while q and q+16<=base+len(d):
                ev=struct.unpack_from('<4I',d,q-base)
                if not base<=ev[3]<=base+len(d)-40:break
                cameras.append(ev);q+=16
                # Each list ends with the authored 999-second hold; adjacent
                # streams' lists are contiguous, not part of this camera.
                if ev[0]==29970:break
            row['cameras']=cameras
        records.append(row)
    assert len(records)==11 and sum(r['words'][0]>1 for r in records)==9
    ui=(DEC/'afs/U_GAME/000_gamedispdat.bin').read_bytes()
    timeline=struct.unpack_from('<I',ui,0x1e8)[0];commands=[]
    for p in range(timeline-0x8c980000,len(ui)-12,12):
        row=struct.unpack_from('<3i',ui,p);commands.append(row)
        if row[0]==-1:break
    assert len(commands)==31 and commands[-1][0]==-1
    cards=[]
    for n in range(20):
        path=f'afs/U_GRF/{107+n:03d}_staff{n+1:02d}.bin';raw=(DEC/path).read_bytes()
        assert raw[:4]==b'GBIX' and struct.unpack_from('<2H',raw,28)==(512,512)
        cards.append(dict(source=path,sha256=sha(raw)))
    return dict(movies=movies,staff=dict(source='afs/PLAYDEMO/004_staffroll.bin',source_sha256=sha(d),
        stage=0,chapter=1,character='retained main+0x78',records=records,timeline=timeline,commands=commands,cards=cards,
        slide_start=struct.unpack_from('<4f',ex,0x21c070-0x10000),
        slide_end=struct.unpack_from('<4f',ex,0x21c080-0x10000),fade_tick=9150),
        playback_connected=True,campaign_enabled=False,remaining=['movie full-frame pose/material/light/caption/audio timing comparison',
            'staff ordinary-camera/player replay fidelity and source child/load ordering (PSP clock 8271 vs source 8289)',
            'staff stage sky/material presentation and 32-MiB allocation margin',
            'normal final-clear gate and earned campaign-through-credits validation'])

def include(info):
    s=info['staff'];lines=['/* Generated by ending_assets.py from original US data. */']
    lines.append('static const int STAFF_COMMANDS[][3]={'+','.join('{%d,%d,%d}'%tuple(r)for r in s['commands'])+'};')
    for name,key in [('STAFF_SLIDE_START','slide_start'),('STAFF_SLIDE_END','slide_end')]:
        lines.append('static const float '+name+'[]={'+','.join(x.hex()+'f'for x in s[key])+'};')
    return '\n'.join(lines)+'\n'

def runtime_assets(info):
    """Transport the original virtual-address images and decoded staff cards."""
    from pvr_decode import decode
    out={}
    for m in info['movies']:out[ROOT/f'psp/ending{m["movie"]}.bin']=(DEC/m['source']).read_bytes()
    # 0ee694 -> 02d568 -> 0e8470: narration descriptor bank 31, NOT BGM_TABLE.
    for ref,name in [(3,'combo_004'),(144,'e27_001'),(145,'e27_002'),(146,'e27_003')]:
        out[ROOT/f'psp/endingvoice{ref:03d}.adx']=(DEC/f'afs/SND_DJTALK/{ref:03d}_{name}.adx').read_bytes()
    out[ROOT/'psp/staffroll.bin']=(DEC/info['staff']['source']).read_bytes()
    for i,c in enumerate(info['staff']['cards']):
        raw=(DEC/c['source']).read_bytes();p=raw.index(b'PVRT');n=struct.unpack_from('<I',raw,p+4)[0]
        im=decode(raw[p:p+8+n]).convert('RGBA');assert im.size==(512,512)
        px=bytearray()
        for r,g,b,a in im.getdata():px+=struct.pack('<H',(a>>4)<<12|(b>>4)<<8|(g>>4)<<4|(r>>4))
        out[ROOT/f'psp/staff{i:02d}.bin']=struct.pack('<4sI2H',b'TEX1',1,512,512)+px
    ex=(DEC/'extracted/1ST_READ.BIN').read_bytes();u=lambda a:struct.unpack_from('<I',ex,a-0x8c010000)[0]
    lines=['/* Original 100d38 screen quads and packed 0..255 UVs. */','static const float STAFF_XY[4][3][8]={']
    for k in range(4):
        rows=[]
        for j in range(3):
            vals=struct.unpack_from('<8f',ex,u(0x8c21bf80+12*k+4*j)-0x8c010000)
            rows.append('{'+','.join(v.hex()+'f'for v in vals)+'}')
        lines.append('{'+','.join(rows)+'},')
    lines.append('};')
    for name,addr,count in [('STAFF_UV_PATTERN_X',0x8c21bfb0,4),('STAFF_UV_PATTERN_Y',0x8c21bff0,4),('STAFF_UV_PAGE_X',0x8c21c030,2),('STAFF_UV_PAGE_Y',0x8c21c050,2)]:
        lines.append(f'static const unsigned short {name}[{count}][8]={{'+','.join('{'+','.join(map(str,struct.unpack_from('<8H',ex,addr-0x8c010000+16*k)))+'}'for k in range(count))+'};')
    out[ROOT/'psp/src/staff_draw.inc']=('\n'.join(lines)+'\n').encode()
    return out

def bake(info, outdir=ROOT/'psp'):
    for m in info['movies']:
        # The scrolling DEMO26 comic uses 512x1024 source sheets. Actor's
        # default 128 cap left each sheet at 64x128, unreadable when enlarged.
        # Match the ordinary movie-25 import at 256x512, within GE limits.
        args=[sys.executable,'tools/jgc_convert.py',str(outdir/f'ending{m["movie"]}.jgc'),'--source-material=1','--source-environment=1',
            '--texmax='+str(512 if m['movie']==25 else 128)]
        for seg in m['segments']:
            for r in seg['actors']:
                row=f'Ending_{r["address"]:x}:{m["source"]}:{m["textures"]}:0,0,0,0:rawat:{B:x}:{r["root"]:x}:0'
                if r['texture_map']:row+=':texmap='+','.join(map(str,r['texture_map']))
                row+=':lighting='+('2' if r['flags']&2 else '1');args.append(row)
        subprocess.run(args,cwd=DEC,check=True)

def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');ap.add_argument('--bake',action='store_true');ap.add_argument('--runtime',action='store_true');a=ap.parse_args()
    info=manifest();outputs={DEC/'ending_manifest.json':json.dumps(info,indent=2)+'\n',ROOT/'psp/src/ending_tables.inc':include(info)}
    for p,data in outputs.items():
        if a.check:assert p.read_text()==data,p
        else:p.write_text(data)
    if a.runtime:
        for p,data in runtime_assets(info).items():
            if a.check:assert p.read_bytes()==data,p
            else:p.write_bytes(data)
    if a.bake:
        if a.check:
            with tempfile.TemporaryDirectory(prefix='jgr-ending-import-') as tmp:
                out=Path(tmp);bake(info,out)
                for m in info['movies']:
                    name=f'ending{m["movie"]}.jgc'
                    assert (out/name).read_bytes()==(ROOT/'psp'/name).read_bytes(),name
        else:bake(info)
    print('Ending source: five movie segments, nine sequential input streams, 25 timed cards, 20 original card textures')
if __name__=='__main__':main()
