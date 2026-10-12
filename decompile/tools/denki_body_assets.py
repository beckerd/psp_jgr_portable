#!/usr/bin/env python3
"""Repair Denki's original 1b6658 -> 21bb44 body texture binding.

Keep every shared character mesh, motion and baked color. Only replace the
texture privately owned by Denki; rebuild the compact bank and cast archive.
"""
import argparse, hashlib, json, struct, subprocess, sys, tempfile
from pathlib import Path
from cutscene_sources import executable, afs_file, texture_lists
from character_roster import header, archive
from jgc_compact import compact
ROOT=Path(__file__).resolve().parents[2]
P=ROOT/'psp'

def repair(shared, source, model=41, bones=12, batches=25, companions=()):
    h=header(shared);sh=header(source)
    first,count=struct.unpack_from('<2I',shared,h[7]+model*48)
    owned=set();textured=set();ordinal=0
    for bi in range(first,first+count):
        at,n=struct.unpack_from('<2I',shared,h[8]+bi*48+40)
        for k in range(at,at+n):
            v,ix,nv,ni,tex,flags=struct.unpack_from('<2I4H',shared,h[9]+k*16)
            sv,si,snv,sni,st,sf=struct.unpack_from('<2I4H',source,sh[9]+ordinal*16)
            assert (nv,ni,flags)==(snv,sni,sf)
            assert shared[h[11]+2*ix:h[11]+2*(ix+ni)]==source[sh[11]+2*si:sh[11]+2*(si+ni)]
            for j in range(nv):
                a=shared[h[10]+28*(v+j):h[10]+28*(v+j+1)]
                b=source[sh[10]+28*(sv+j):sh[10]+28*(sv+j+1)]
                assert a[:8]+a[11:]==b[:8]+b[11:]
            owned.add(k)
            if not flags&4:textured.add(tex)
            ordinal+=1
    assert count==bones and ordinal==batches and len(textured)==1
    target=textured.pop()
    # Some source variants intentionally share one atlas. Call repair for
    # every companion too, so each mesh is checked against its source root.
    for ci in companions:
        first,count=struct.unpack_from('<2I',shared,h[7]+ci*48)
        for bi in range(first,first+count):
            at,n=struct.unpack_from('<2I',shared,h[8]+bi*48+40)
            owned.update(range(at,at+n))
    for k in range(h[2]):
        _,_,_,_,tex,flags=struct.unpack_from('<2I4H',shared,h[9]+k*16)
        assert k in owned or tex!=target or flags&4, 'texture shared with another model'
    out=bytearray(shared[:h[15]])
    for ti in range(h[5]):
        bank,at=(source,sh[12]) if ti==target else (shared,h[12]+ti*16)
        w,ht,flags,pad,clut,pix=struct.unpack_from('<4H2I',bank,at)
        out.extend(bytes(-len(out)%16));co=len(out);out.extend(bank[clut:clut+1024])
        out.extend(bytes(-len(out)%16));po=len(out);out.extend(bank[pix:pix+w*ht])
        struct.pack_into('<4H2I',out,h[12]+ti*16,w,ht,flags,pad,co,po)
    out.extend(bytes(-len(out)%16))
    return bytes(out)

def build():
    e=executable();maps,_=texture_lists(e,0x8c21bb80,afs_file('ENEMY',36).read_bytes(),0x8ca00000)
    assert maps[e.u(0x8c1b6658)]==[10]
    motions=','.join(f'{e.u(0x8c1b665c+4*i):x}' for i in range(5))
    with tempfile.TemporaryDirectory(prefix='jgr-denki-body-') as tmp:
        q=Path(tmp)/'body.jgc'
        spec=f'Denki:afs/ENEMY/037_denkidat.bin:afs/ENEMY/036_denkitxr.bin:730,0,712,180:rawat:8ca00000:{e.u(0x8c1b6650):x}:{motions}:tex=10'
        subprocess.run([sys.executable,'tools/jgc_convert.py',str(q),spec],cwd=ROOT/'decompile',check=True,capture_output=True)
        shared=repair((P/'chars.jgc').read_bytes(),q.read_bytes())
    packed=compact(shared)
    return {'chars.jgc':shared,'chars_compact.jgc':packed,'chars_roster.bin':archive(packed,(P/'playerlight.bin').read_bytes())}

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');a=ap.parse_args()
    for name,data in build().items():
        if a.check:assert (P/name).read_bytes()==data,name
        else:(P/name).write_bytes(data)
        print(name,len(data),hashlib.sha256(data).hexdigest())
    if not a.check:
        p=P/'playerlight.json';info=json.loads(p.read_text());info['shared_bank_sha256']=hashlib.sha256((P/'chars.jgc').read_bytes()).hexdigest();p.write_text(json.dumps(info,indent=2)+'\n')
