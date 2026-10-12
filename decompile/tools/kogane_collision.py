#!/usr/bin/env python3
"""Restore Kogane interactive contacts using original US registration tables.

0398ac: world grid *107cc4.
0396ec: 114 floor lists at 1080bc, 52-byte records, type -> node 109bf8;
         plus 45 global nodes at 1796a8. +2c is a runtime handle, NOT geometry.
03861c: dynamic-prop contact types; retain the existing static export's
         0x1c fallback for parameter-driven special tasks (not their full AI).
The restored door/gate/panel/window owners now live in kogane_house.c.
Other shipping collision bytes are retained by rebuild() below.
"""
from pathlib import Path
import struct
import json
import subprocess
import sys

DEC=Path(__file__).resolve().parents[1]
CONTACT_GROUPS={
    0x1f:[2,6,10,11,13,14,16,22,34,43,64,15,35,36,62,63,65,70,88,89,27,28,29,66,59,60],
    0x1b:[3,12,20,21,90,67],0x21:[4,8,19,30],0x23:[5,18],8:[7],
    0x25:[17],0x1e:[23,24,25,26],0x27:[58,76,85,86,87],
}

def command(output):
    exe=(DEC/'extracted/1ST_READ.BIN').read_bytes()
    grid=struct.unpack_from('<I',exe,0x8c107cc4-0x8c010000)[0]
    contacts='default:0x1c'+''.join(f',{t}:{c}' for c,types in CONTACT_GROUPS.items() for t in types)
    return [sys.executable,'tools/jgs_collision.py','afs/STAGE2/000_st2dat.bin',str(output),
            f'--grid={grid:x}','--items=8c1080bc','--sectors=113','--recsize=52',
            '--protos=8c109bf8,77','--protos-only=1','--staticnodes=8c1796a8,45',
            '--dyn=8c108284','--dynprotos=8c10a2dc,91','--dyncontact='+contacts,
            '--dynexclude=4,5,17,18,58,76,85,86,87']  # Original interactive event bodies, now registered at runtime

def rebuild(output, baseline_path=None):
    """Separate analytic event volumes from the tested solid world bank.

    The current generic exporter also changes placement rotation order on
    unrelated props. Keep those existing triangle bytes until their own audit;
    use source owner ranges to identify runtime owners and type-4 volumes.
    """
    import tempfile
    from kogane_house_assets import TYPES
    from kogane_cover_assets import removed_records
    from stage_render_assets import Source
    wanted={r['address'] for r in Source(2).records(0x8c108284,114,40,91) if r['pid'] in TYPES}
    wanted.update(r['address'] for r in removed_records())
    baseline=Path(baseline_path or DEC/'campaign_captures/kogane_doors_20260916/baseline_stage2.col').read_bytes()
    with tempfile.TemporaryDirectory(prefix='kogane-collision-') as tmp:
        full=Path(tmp)/'all.col'
        boxes_path=Path(tmp)/'event_boxes.json'
        subprocess.run([a for a in command(full) if not a.startswith('--dynexclude=')]+
                       [f'--event-boxes-json={boxes_path}'],cwd=DEC,check=True)
        source=full.read_bytes()
        event_boxes=json.loads(boxes_path.read_text())
    count,offset=struct.unpack_from('<II',baseline,4)
    assert struct.unpack_from('<II',source,4)==(count,offset)
    _,world,patches,entries=struct.unpack_from('<4I',source,offset)
    end=offset+16+1028*4+patches*32+entries*12
    assert source[offset:end]==baseline[offset:]
    assert source[16:16+world*44]==baseline[16:16+world*44]
    signature,owners,_,_=struct.unpack_from('<4I',source,end)
    assert signature==0x31504943
    removed=set();addresses=[];owners_raw=[]
    for i in range(owners):
        address,kind,first,n=struct.unpack_from('<4I',source,end+16+i*16)
        if address in wanted:
            removed.update(range(first,first+n));addresses.append(address)
        owners_raw.append((address,kind,first,n))
    assert len(addresses)==48
    # Type-4 nodes are analytic INTERACTION volumes (0b6d5c/0b6ec4), not
    # type-6 world meshes (0b6a8c/0b6b1c). The legacy exporter made their
    # six faces solid with mask 0xffff and reused the event-category number
    # as a floor material. Category 0x1b thereby became a foot-trip surface.
    # Keep them out of floor/body/camera/water queries. Their task/event
    # interactions belong to the runtime owners, never the world mesh.
    for first,n,node,src in event_boxes:
        assert first>=world and src in (1,2)
        removed.update(range(first,first+n))
    remap=[];remaining=0
    for i in range(count):
        remap.append(remaining)
        remaining+=i not in removed
    kept=[]
    for address,kind,first,n in owners_raw:
        retained=sum(i not in removed for i in range(first,first+n))
        if retained:kept.append((address,kind,remap[first],retained))
    triangles=b''.join(baseline[16+i*44:16+(i+1)*44] for i in range(count) if i not in removed)
    # Attach owner boxes to the shipping triangles themselves. The newer
    # export has corrected rotations on some unrelated props, so its boxes
    # cannot safely describe the retained geometry. Preserve every triangle,
    # alias and query order; the suffix only rejects provably distant owners.
    boxes=[]
    for address,kind,first,n in kept:
        xyz=[struct.unpack_from('<9f',triangles,(first+j)*44) for j in range(n)]
        low=[min(row[k] for row in xyz for k in range(axis,9,3)) for axis in range(3)]
        high=[max(row[k] for row in xyz for k in range(axis,9,3)) for axis in range(3)]
        boxes.append(struct.pack('<3fI3fI',*low,n,*high,first))
    suffix=struct.pack('<4I',0x31504943,len(kept),len(boxes),0)
    suffix+=b''.join(struct.pack('<4I',*row) for row in kept)
    suffix+=b''.join(boxes)+struct.pack('<'+'I'*len(boxes),*range(len(boxes)))
    result=struct.pack('<4sIII',b'COL1',count-len(removed),16+len(triangles),0x31535043)+triangles+baseline[offset:]+suffix
    output.write_bytes(result)
    print(f'Removed {len(removed)} event triangles ({len(event_boxes)} analytic boxes, {len(addresses)} runtime interactive placements); all remaining triangles and world grid byte-exact.')

if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output',nargs='?',type=Path,default=DEC.parent/'psp/stage2.col')
    parser.add_argument('--baseline',type=Path,help='Explicit regenerated legacy-transform bank')
    args=parser.parse_args()
    rebuild(args.output.resolve(),args.baseline)
