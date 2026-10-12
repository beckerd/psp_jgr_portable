#!/usr/bin/env python3
"""Original completed live rival performances for the three tag races."""
import argparse,hashlib,json,struct
from pathlib import Path
from title_route_assets import ROOT
from mission_chain import chain_offsets
from poison_route_assets import pack_sections

CAPTURE=ROOT/'decompile/campaign_captures/challenge_routes_20260919'
SPECS=[dict(mission='S1M07',stage=1,chapter=6,record=2,set=1,bank=7,character=0,player=3),
       dict(mission='S2M07',stage=2,chapter=6,record=0,set=0,bank=16,character=4,player=0),
       dict(mission='S3M07',stage=3,chapter=6,record=0,set=0,bank=26,character=8,player=0)]

def layout(spec):
    bank=spec['bank'];path=ROOT/f'decompile/afs/MISSIONSET/{bank:03d}_mission_set_{bank:02d}.bin'
    data=path.read_bytes();base=0x8cde6000
    word=lambda a:struct.unpack_from('<I',data,a-base)[0]
    header=base+chain_offsets(data,b'JMSc')[spec['set']]
    tables=word(header+0x44);assert word(tables+4)==0
    table=word(tables);result=[]
    for section in range(2):
        stream=word(table+section*12);raw=struct.unpack_from('<I4f3i',data,stream-base)
        assert raw[0]==spec['character']
        flags=word(table+section*12+8);assert flags==(3 if section else 1)
        records=[];offset=stream-base+32
        while not data[offset]&128:
            records.append(list(data[offset:offset+4]));offset+=4
        result.append(dict(stream=stream,character=raw[0],speed=raw[1],start=list(raw[2:5]),
                           angles=list(raw[5:]),flags=flags,records=records,
                           activation_distance=struct.unpack_from('<f',data,table-base+4)[0],
                           active_ticks=sum(r[0]+1 for r in records)-1))
    return result

def source_sections(spec):
    folder=CAPTURE/('dc_rival_'+spec['mission'].lower())
    assert 'CHALLENGE_RIVAL complete two sections and return-to-start' in (folder/'flycast.log').read_text()
    data=(folder/'rival0.bin').read_bytes();assert data[:8]==struct.pack('<4sI',b'PRT2',4192)
    assert (len(data)-8)%4192==0
    routes=layout(spec);sections=[[],[]]
    for at in range(8,len(data),4192):
        frame=data[at:at+4192];word=lambda off:struct.unpack_from('<I',frame,off)[0]
        if not word(0xc48):continue
        segment=word(0xc50);assert segment<2
        assert word(0x7c)==spec['character']
        assert word(0x98)==len(sections[segment])+1
        route=routes[segment];index=(word(0xc00)-route['stream']-32)//4
        assert 0<=index<len(route['records'])
        assert frame[0xbfc:0xc00]==bytes(route['records'][index])
        sections[segment].append(frame)
    for i,section in enumerate(sections):
        assert len(section)==routes[i]['active_ticks'],(spec['mission'],i,len(section),routes[i]['active_ticks'])
        assert list(struct.unpack_from('<3f',section[0],0xb8))==routes[i]['start']
    return sections

def build(check=False):
    reports=[]
    for spec in SPECS:
        sections=source_sections(spec);data,report,starts,stride=pack_sections(sections,spec['character'])
        name='challenge_rival_'+spec['mission'].lower()+'.bin';path=ROOT/'psp'/name
        if check:assert path.read_bytes()==data
        else:path.write_bytes(data)
        source=CAPTURE/('dc_rival_'+spec['mission'].lower())/'rival0.bin'
        report.update(spec,path=name,source=str(source.relative_to(ROOT)),
                      capture_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
                      controller_sha256=hashlib.sha256((ROOT/f"psp/set{spec['bank']}_ctrl.bin").read_bytes()).hexdigest(),
                      section_counts=[len(s) for s in sections],section_starts=starts,stride=stride+12,bytes=len(data))
        reports.append(report)
    path=ROOT/'psp/challenge_rivals.json';text=json.dumps(reports,indent=2)+'\n'
    if check:assert path.read_text()==text
    else:path.write_text(text)
    print('Original challenge rivals:',sum(r['frames'] for r in reports),'samples, six sections')
    return reports

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');build(ap.parse_args().check)
