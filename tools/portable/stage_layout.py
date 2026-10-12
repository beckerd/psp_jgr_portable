#!/usr/bin/env python3
"""Regenerate stage placement inputs from executable tables, without captures."""
import json
from pathlib import Path
import struct
import subprocess
import sys
from stage_render_assets import TABLES, SECTORS

def build(stage):
    commands=[]
    for tag,(table,near,far,stride,count,_) in TABLES[stage].items():
        commands.append(f'{tag}={table:x},{near:x},{far:x},s{stride},n{count}')
    # Group structures use a separate source model pointer at +4.
    groups={2:(0x8c1080bc,0x8c109bf8),3:(0x8c1ba860,0x8c1bbca8),5:(0x8c203060,0x8c203d94)}
    if stage in groups:
        table,near=groups[stage];commands.append(f'GRP={table:x},{near:x},s52')
    output=Path(f'stage{stage}_layout.json')
    subprocess.run([sys.executable,'tools/extract_layout_static.py',f'afs/STAGE{stage}/000_st{stage}dat.bin',str(output),f'--sectors={SECTORS[stage][0]}',*commands],check=True)
    data=json.loads(output.read_text())
    if stage==1:
        # 03144c initializes all IT group types only in four authored sectors;
        # elsewhere only 0x2e..0x34 are group-rendered. Stride is ten words.
        exe=Path('extracted/1ST_READ.BIN').read_bytes();bank=Path('afs/STAGE1/000_st1dat.bin').read_bytes()
        ew=lambda p:struct.unpack_from('<I',exe,p-0x8c010000)[0]
        special=struct.unpack_from('<4I',exe,0x8c106384-0x8c010000);rows=[]
        for sector in range(63):
            at=ew(0x8c106090+4*sector)
            if not at:continue
            while True:
                kind=struct.unpack_from('<i',bank,at-0x8cb00000)[0]
                if kind < -1:break
                if kind>=0 and (sector in special or 0x2e<=kind<=0x34):
                    record=ew(0x8c10748c+4*kind)
                    if record:
                        model=struct.unpack_from('<I',bank,record+4-0x8cb00000)[0]
                        rows.append([model-0x8cb00000,list(struct.unpack_from('<3f',bank,at+4-0x8cb00000)),list(struct.unpack_from('<3i',bank,at+16-0x8cb00000))])
                at+=40
        data['model_inst']=rows
    if stage==6:data['proto']['DYN']=[]
    output.write_text(json.dumps(data))
if __name__=='__main__':build(int(sys.argv[1]))
