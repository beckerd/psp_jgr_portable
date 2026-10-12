#!/usr/bin/env python3
"""Lossless mission-cast archive. Full models, keys, colours and pixels are retained.
JCR1 indexes 51 independent JGC2 images plus original vertex spans for PLT1.
The PSP reads just the selected images directly into one resident bank.
"""
from pathlib import Path
import struct
import argparse
from jgc_lod import lod_models,pack_models
ROOT=Path(__file__).resolve().parents[2]
P=ROOT/'psp'
MODEL=(4,6,3,2,1,7,10,0,15,8,13,11,12,5,9,14)
GANG=(16,17,18,30,48,49,31,27,46,50,29,26,34,35,36,37,38,39,42,43,44,40,41,32,28,33,16,17,27,30,29,22,37)
def u(data,at):return struct.unpack_from('<I',data,at)[0]
def align(data,n=16):data.extend(bytes(-len(data)%n));return len(data)
def header(data):return struct.unpack_from('<16I',data,4)
def model_bank(data,ci):
    far=lod_models(data).get(ci)
    if far is not None:
        bank,_,source=pack_models([(data,ci,data,ci),(data,far,data,ci)],{0:1})
        return bank,[v for _,v in source]
    h=header(data);nc,nb,nbat,nv,ni,nt,nm,oc,ob,obat,ov,oi,ot,om,ok,op=h
    ch=bytearray(data[oc+ci*48:oc+(ci+1)*48]);b0,bn,m0,mn=struct.unpack_from('<4I',ch)
    bones=bytearray(data[ob+b0*48:ob+(b0+bn)*48]);batches=bytearray();verts=bytearray();idx=bytearray();texids=[];source=[]
    for bi in range(bn):
        at=bi*48;parent=struct.unpack_from('<h',bones,at)[0]
        assert parent<0 or b0<=parent<b0+bi
        struct.pack_into('<h',bones,at,-1 if parent<0 else parent-b0)
        first,count=struct.unpack_from('<2I',bones,at+40);struct.pack_into('<I',bones,at+40,len(batches)//16)
        for j in range(first,first+count):
            v,i,nvi,nii,t,flags=struct.unpack_from('<2I4H',data,obat+j*16)
            assert v+nvi<=nv and i+nii<=ni and t<nt
            if t not in texids:texids.append(t)
            batches.extend(struct.pack('<2I4H',len(verts)//28,len(idx)//2,nvi,nii,texids.index(t),flags))
            source.append(v);verts.extend(data[ov+v*28:ov+(v+nvi)*28]);idx.extend(data[oi+i*2:oi+(i+nii)*2])
    keys=bytearray();channels=bytearray();motions=[];ct={};kt={}
    for mi in range(m0,m0+mn):
        nf,at=struct.unpack_from('<2I',data,om+mi*8)
        assert at>=om+nm*8 and at+bn*24<=ok
        if at not in ct:
            ct[at]=len(channels)
            for bi in range(bn):
                row=[]
                for kind,(count,off) in enumerate(struct.iter_unpack('<2I',data[at+bi*24:at+(bi+1)*24])):
                    size=count*(6 if kind==1 else 12)
                    assert off+size<=op-ok
                    pair=(off,size)
                    if pair not in kt:
                        kt[pair]=align(keys,4);keys.extend(data[ok+off:ok+off+size])
                    row.extend((count,kt[pair]))
                channels.extend(struct.pack('<6I',*row))
        motions.append((nf,ct[at]))
    out=bytearray(80);co=len(out);struct.pack_into('<4I',ch,0,0,bn,0,mn);out.extend(ch)
    bo=align(out);out.extend(bones);ba=align(out);out.extend(batches)
    vo=align(out);out.extend(verts);io=align(out);out.extend(idx)
    to=align(out);out.extend(bytes(len(texids)*16));mo=align(out)
    ca=mo+len(motions)*8
    for nf,at in motions:out.extend(struct.pack('<2I',nf,ca+at))
    out.extend(channels);ko=align(out);out.extend(keys);po=align(out)
    for ti,old in enumerate(texids):
        w,ht,flags,pad,clut,pix=struct.unpack_from('<4H2I',data,ot+old*16)
        cl=align(out,64);out.extend(data[clut:clut+1024]);px=align(out,64);out.extend(data[pix:pix+w*ht])
        struct.pack_into('<4H2I',out,to+ti*16,w,ht,flags,pad,cl,px)
    struct.pack_into('<4s16I',out,0,b'JGC2',1,bn,len(batches)//16,len(verts)//28,len(idx)//2,len(texids),mn,co,bo,ba,vo,io,to,mo,ko,po)
    return bytes(out),source

def archive(data,light):
    h=header(data);assert 51<=h[0]<=102 and data[:4]==b'JGC2'
    assert light[:4]==b'PLT1' and u(light,4)==h[3] and u(light,8)==h[0] and len(light)==20+h[3]*20
    digest=2166136261
    for i,value in enumerate(data[h[10]:h[10]+h[3]*28]):
        if not 8<=i%28<12:digest=((digest^value)*16777619)&0xffffffff
    assert digest==u(light,12), 'playerlight.bin belongs to different geometry'
    out=bytearray(32+51*16)
    for ci in range(51):
        bank,source=model_bank(data,ci);at=align(out,64);out.extend(bank);mp=align(out)
        out.extend(struct.pack('<%dI'%len(source),*source))
        struct.pack_into('<4I',out,32+ci*16,at,len(bank),mp,len(source))
    struct.pack_into('<4s7I',out,0,b'JCR1',51,len(out),h[3],h[0],u(light,12),1,0)
    return bytes(out)

def ctrl_characters(data):
    assert data[:4]==b'CTL2';p=8;result=set()
    for _ in range(u(data,4)):
        n=u(data,p);p+=4;used=set()
        for _ in range(n):
            cnt=u(data,p);p+=4
            used.update(u(data,p+j*12) for j in range(cnt));p+=cnt*12
        n=u(data,p);p+=4
        for si in range(n):
            ch=u(data,p);nr=u(data,p+32);p+=36+nr*4
            if si in used:result.add(MODEL[ch&15])
        n=u(data,p);p+=4
        for _ in range(n):a,b=struct.unpack_from('<2I',data,p+12);p+=20+(a+b)*144
    assert p==len(data)
    return result

def group_characters(data):
    assert data[:4]==b'GRP1';p=8;result=set()
    for _ in range(u(data,4)):
        n=u(data,p);p+=4
        for _ in range(n):
            kind=u(data,p);count=u(data,p+40);p+=44
            assert kind<len(GANG)
            if count and kind not in range(26,31):result.add(GANG[kind])
            for i in range(count):
                pose=u(data,p+24);p+=28
                if not i or kind not in range(26,31):continue
                model={26:26 if pose else 16,27:28 if pose else 17,28:27,29:-1 if pose else 30,30:29}[kind]
                if model>=0:result.add(model)
    assert p==len(data)
    return result

def casts():
    data=(P/'missions.bin').read_bytes();assert data[:4]==b'MIS3';out={};names={}
    for i in range(u(data,4)):
        at=8+i*88;bank=u(data,at+8);blob=u(data,at+60);rec=u(data,at+68)
        out.setdefault(bank,set());names.setdefault(bank,set()).add(data[at+32:at+48].split(b'\0')[0].decode())
        ch=u(data,blob+rec+0x20)
        if 1<=ch<=13:out[bank].add(MODEL[ch-1])
        elif ch==14:out[bank].add(MODEL[15])
        # 0369dc changes PLAY to Combo between the two recruitment runs.
        if u(data,at+4)==0x0f:out[bank].add(MODEL[3])
    for bank,models in out.items():
        models.update(ctrl_characters((P/f'set{bank}_ctrl.bin').read_bytes()))
        models.update(group_characters((P/f'set{bank}_groups.bin').read_bytes()))
        waves=(P/f'set{bank}_waves.bin').read_bytes();assert waves[:4]==b'WAV1';p=8
        for _ in range(u(waves,4)):
            n=u(waves,p);p+=4+n*68
            if n:models.add(16)
        assert p==len(waves)
        # Preserve the legacy pedestrian-family adapter as well as the
        # stage-specific people bank. These are actual PED table references.
        ped=(P/f'set{bank}_peds.bin').read_bytes();assert ped[:4]==b'PED3';p=8
        for _ in range(u(ped,4)):
            typ,n=u(ped,p),u(ped,p+4);p+=28+n*12
            models.add(16 if 43<=typ<=46 else 19+typ%3)
        assert p==len(ped)
    return out,names

def generated_casts():
    sets,names=casts();n=max(sets)+1
    result=['/* Generated by character_roster.py from complete mission/set banks. */',
            'static const unsigned long long MISSION_CAST[%d]={'%n]
    for i in range(n):
        mask=sum(1<<m for m in sets.get(i,()))
        result.append('    0x%013xULL, /* %d: %s */'%(mask,i,', '.join(sorted(names.get(i,())))))
    result+=['};','static const unsigned long long MISSION_CAST_VALID=0x%016xULL;'%sum(1<<i for i in sets),'static const unsigned TITLE_CAST_MODEL=%d;'%MODEL[u((P/'title_route.bin').read_bytes(),8)],'']
    return '\n'.join(result)

def main():
    args=argparse.ArgumentParser();args.add_argument('--check',action='store_true');opts=args.parse_args()
    blob=archive((P/'chars_compact.jgc').read_bytes(),(P/'playerlight.bin').read_bytes())
    text=generated_casts()
    for path,value in ((P/'chars_roster.bin',blob),(P/'src/mission_cast.inc',text.encode())):
        if opts.check:assert path.read_bytes()==value,path
        else:path.write_bytes(value)
    sets,names=casts()
    print('archive',len(blob),'bytes;',len(sets),'complete mission-set casts')
    for i in sorted(sets):print(i,','.join(sorted(names[i])),sorted(sets[i]))
if __name__=='__main__':main()
