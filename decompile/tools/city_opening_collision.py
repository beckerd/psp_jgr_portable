#!/usr/bin/env python3
"""Retire live event/knockable bodies and contacts for absent city props."""
import argparse,json,struct,subprocess,sys,tempfile
from pathlib import Path
from city_opening_assets import CONFIG,ROOT,DEC
from stage_render_assets import Source
CAP=DEC/'campaign_captures/interactive_architecture_20260916'
def rebuild(stage,output,baseline_path=None):
    c=CONFIG[stage]
    contacts={}
    if stage==3:
        groups={28:[3,14,*range(25,32),40,41,42,49,*range(51,57)],34:[6],32:[11,16,17],27:[13,21],8:[22],7:[32,33,34],30:[35],42:[37,38,39],33:[47],5:[48],65535:[62,63,64]}
        default=31
        for sound,types in groups.items():
            for ty in types:contacts[ty]=sound
    else:default=27;contacts={0:28,1:30,2:31,3:31,4:32,17:32,5:39,12:7,13:7,14:34,15:28}
    contact=f'default:{default}'+''.join(f',{k}:{v}' for k,v in contacts.items())
    if stage==5:
        # 0f4684 only draws IT decorations. The old bank incorrectly bound
        # their ids/positions to F prototypes at 203d94, making 400 phantom
        # collision triangles. Normal F floors are in the world grid;
        # 0ff7b4 registers only mission-swapped F lists (not these IT props).
        # The legacy DYN table 204084 contains collision nodes, not the
        # render models at 203f44/203fe4. Restored ordinary props and the
        # windows/doors now have live render/contact owners; neither their
        # boxes nor the unrelated IT decorations belong in the static bank.
        # Keep the original world grid byte-exact. Architecture, knockable,
        # mission and traffic providers register their own contacts.
        subprocess.run([sys.executable,'tools/jgs_collision.py',
            'afs/STAGE5/000_st5dat.bin',str(output.resolve()),
            '--grid=8cc3b3d4'],cwd=DEC,check=True)
        return
    baseline=Path(baseline_path or CAP/f'baseline_stage{stage}.col').read_bytes()
    with tempfile.TemporaryDirectory(prefix='jgr-opening-collision-') as tmp:
        out=Path(tmp)/'dyn.col';owners=Path(tmp)/'owners.json'
        subprocess.run([sys.executable,'tools/jgs_collision.py',f'afs/STAGE{stage}/000_st{stage}dat.bin',str(out),
            f'--sectors={c["sectors"]-1}',f'--dyn={c["table"]:x}',f'--dynprotos={c["coll"]:x},{c["count"]}',
            '--dynrec=52','--dyncontact='+contact,'--owners-json='+str(owners)],cwd=DEC,check=True)
        generated=out.read_bytes();owner=json.loads(owners.read_text())
    count,offset=struct.unpack_from('<II',baseline,4);ndyn=struct.unpack_from('<I',generated,4)[0]
    assert ndyn==({3:3080,5:1536}[stage])
    first=count-ndyn
    assert all(struct.unpack_from('<H',baseline,16+i*44+42)[0]==2 for i in range(first,count))
    records=Source(stage).records(c['table'],c['sectors'],52,c['count'])
    wanted={r['address']:r['pid'] for r in records if r['pid'] in c['types']}
    # Ordinary knockable bodies use the same original DYN owner table as
    # these doors. They are analytic interaction boxes, never permanent
    # floor/wall triangles. Keep the canonical source handler inventory as
    # the authority; unrelated dynamic meshes retain their shipping bytes.
    from street_prop_assets import source_records
    _,props,_=source_records(stage)
    legacy_addresses={r['address'] for r in records}
    # Secondary handler banks were never baked into this legacy collision
    # section. Their live bodies need no static triangle removal.
    knockable={r['address']:r['pid'] for r in props if r['address'] in legacy_addresses}
    assert not set(wanted)&set(knockable)
    wanted.update(knockable)
    removed=set();seen=set()
    for address,ty,start,n in owner:
        if address in wanted:
            assert ty==wanted[address] and n==12
            if address not in knockable:
                # These Y-only architecture placements remain byte-exact.
                assert baseline[16+(first+start)*44:16+(first+start+n)*44]==generated[16+start*44:16+(start+n)*44],hex(address)
            else:
                # Legacy generic props include multi-axis placements whose
                # old XYZ rotation differs from the corrected source ZYX.
                # Stable owner ranges identify only the removed bytes;
                # assert their masks/category/source rather than rewriting
                # any retained geometry to make a fresh export match.
                for j in range(n):
                    assert baseline[16+(first+start+j)*44+36:16+(first+start+j+1)*44]==generated[16+(start+j)*44+36:16+(start+j+1)*44],hex(address)
            removed.update(range(first+start,first+start+n));seen.add(address)
    assert seen==set(wanted)
    triangles=b''.join(baseline[16+i*44:16+(i+1)*44] for i in range(count) if i not in removed)
    result=struct.pack('<4sIII',b'COL1',count-len(removed),16+len(triangles),0x31535043)+triangles+baseline[offset:]
    if stage==3:
        # Legacy export paired IT decoration placements (1ba7a8, stride 40)
        # with F floor prototypes (1bbca8). 04846c never registers IT bodies;
        # 0e5628 uses F placements (1ba860, stride 52), not this table.
        # Remove the 436 misbound triangles; the source world grid and every
        # retained dynamic/contact triangle remain byte-exact.
        n,off=struct.unpack_from('<II',result,4)
        kept=[result[16+i*44:16+(i+1)*44] for i in range(n) if struct.unpack_from('<H',result,16+i*44+42)[0]!=1]
        assert n-len(kept)==436
        result=struct.pack('<4sIII',b'COL1',len(kept),16+len(kept)*44,0x31535043)+b''.join(kept)+result[off:]
    output.write_bytes(result)
    print(f'Stage {stage}: retired {len(removed)} static triangles for {len(seen)} live owners ({len(knockable)} knockable); removed {436 if stage==3 else 0} misbound IT triangles; world grid retained.')
if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true')
    ap.add_argument('--baseline-dir',type=Path,help='Directory of regenerated legacy-transform banks')
    args=ap.parse_args()
    for stage in CONFIG:
        target=ROOT/f'psp/stage{stage}.col'
        if args.check:
            with tempfile.TemporaryDirectory() as tmp:
                p=Path(tmp)/'bank.col';rebuild(stage,p,args.baseline_dir/f'baseline_stage{stage}.col' if args.baseline_dir else None);assert p.read_bytes()==target.read_bytes()
        else:rebuild(stage,target,args.baseline_dir/f'baseline_stage{stage}.col' if args.baseline_dir else None)
