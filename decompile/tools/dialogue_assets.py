#!/usr/bin/env python3
"""Audit every original mission intro, with explicit playable dialogue routes.

Selectors are JMDc +d8 (EVENT), +20 (secondary actor), not filename order.
An imported cinematic is never marked supported just because its bytes exist.
"""
import argparse,json,struct
from pathlib import Path
from mission_chain import chain_offsets
from cutscene_sources import event_descriptors,Image,DEC,ROOT

DIALOGUE_IDS=(2,3,5,6,15,16,17,19,20,21,22,27,28,29,30,37,38,39,52,53,54,55,
              63,64,65,66,67,68,71,76,77,82,83,86,87,106,107,108,109,110,111)
EXISTING_INTROS={4,7,74,97,102}
BESPOKE_IDS=(0,1,8,9,10,23,24,25,26,32,33,34,35,36,41,42,43,44,49,50,59,60,61,62,
             73,75,78,79,80,84,85,88,89,91,92,98,99)

def mission_scripts(data,offset):
    """059288/059748's typed registration tables, never a byte-pattern scan."""
    base=0x8cde0000
    def word(address):
        assert base<=address<=base+len(data)-4,hex(address)
        return struct.unpack_from('<I',data,address-base)[0]
    scripts={word(base+offset+field) for field in (0x54,0x58,0x5c)}
    for field,stride,event,negative in ((0x34,20,16,False),(0x38,24,20,False),
            (0x3c,12,8,True),(0x40,16,12,False),(0x44,8,4,True),
            (0x48,16,12,False),(0x4c,16,12,False),(0x50,12,8,False),(0x60,4,0,False)):
        at=word(base+offset+field)
        for _ in range(len(data)//stride):
            if not at:break
            first=word(at)
            if (first>=0x80000000 if negative else first==0):break
            scripts.add(word(at+event));at+=stride
        else:raise ValueError('unterminated mission registration table')
    for at in sorted(scripts):
        if not at:continue
        for _ in range(len(data)//32):
            code=word(at)
            if not code:break
            yield at,struct.unpack_from('<8I',data,at-base)
            at+=32
        else:raise ValueError('unterminated mission script')

def mode_source(rows,count=112):
    """Serialize the original mission selectors as user-owned numeric data."""
    modes=[0]*count;seen=set()
    for row in rows:
        if row['kind']!='PLAY dialogue':continue
        ident=row['id'];refs=row['references']
        if not 0<=ident<count or ident in seen or not refs:
            raise ValueError('Invalid dialogue table entry')
        mode=refs[0]['secondary']
        if not 1<=mode<=13 or any(r['secondary']!=mode for r in refs):
            raise ValueError('Inconsistent original dialogue selector')
        modes[ident]=mode;seen.add(ident)
    lines=['/* Original JMDc selectors; generated locally from the user disc. */',
           f'static const unsigned char scene_dialogue_modes[{count}]={{']
    for start in range(0,count,16):
        lines.append('    '+','.join(str(n) for n in modes[start:start+16])+',')
    return '\n'.join(lines+['};',''])

def audit():
    refs={}
    for path in sorted((DEC/'afs/E_MISSIONDATA').iterdir()):
        data=path.read_bytes()
        if data[:4]!=b'JMDc':continue
        for record,offset in enumerate(chain_offsets(data)):
            word=lambda at:struct.unpack_from('<i',data,offset+at)[0]
            id=word(0xd8)
            context=dict(source=str(path.relative_to(DEC)),record=record,
                kind=word(0x14),music_mode=word(0x18),secondary=word(0x20))
            if id>=0:refs.setdefault(id,[]).append(dict(context,origin='intro'))
            seen=set()
            for address,command in mission_scripts(data,offset):
                if command[0] not in (0xe,0x1c) or command[1] in seen:continue
                seen.add(command[1]);assert command[1]<112
                refs.setdefault(command[1],[]).append(dict(context,origin='mission event',address=address,opcode=command[0]))
    rows=[]
    for e in event_descriptors():
        if e['id'] not in refs:continue
        id=e['id'];im=Image((DEC/e['source']).read_bytes(),0x8c900000)
        commands=[w for phase in range(3) for _,w in im.commands(im.u(im.base+4*phase))]
        mode=refs[id][0]['secondary']
        if id in DIALOGUE_IDS:
            assert all(r['secondary']==mode for r in refs[id])
            assert any(w[0]==0x45 for w in commands) and mode in range(1,14)
        rows.append(dict(id=id,name=e['name'],references=refs[id],opcodes=sorted({w[0] for w in commands}),
            enabled=id in DIALOGUE_IDS or id in EXISTING_INTROS or id in BESPOKE_IDS,
            kind='PLAY dialogue' if id in DIALOGUE_IDS else 'bespoke cinematic',
            source_callbacks=(DEC/f'lesson{id}_tasks_reference.csv').exists() or
                (DEC/f'event{id}_tasks_reference.csv').exists()))
    return rows,mode_source(rows)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--check',action='store_true');a=p.parse_args()
    rows,source=audit();path=ROOT/'psp/src/scene_dialogue_data.inc'
    if a.check:assert path.read_text()==source
    else:path.write_text(source)
    target=DEC/'campaign_captures/dialogue_audit_20260912/coverage.json';target.parent.mkdir(exist_ok=True)
    target.write_text(json.dumps(rows,indent=2)+'\n')
    print(f'{len(rows)} mission-referenced EVENT descriptors: {sum(r["enabled"] for r in rows)} enabled, '
          f'{sum(not r["enabled"] for r in rows)} bespoke routes still under audit')
if __name__=='__main__':main()
