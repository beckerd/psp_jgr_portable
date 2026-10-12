#!/usr/bin/env python3
"""Source-table recipes for live city prop and sector banks."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys

ZONES={1:(0x8c174424,0x8c105e3c,63,0x8c175b20),2:(0x8c17b58c,0x8c107cc8,114,0x8c17e3c8),
       3:(0x8c176e18,0x8c1bc6c0,46,0x8c177cac),5:(0x8c1fbadc,0x8c204140,71,0x8c1fbd1c),
       6:(0x8c201604,0x8c20b650,81,0x8c20188c)}
# The two initial contact tables passed to original 090ff0 by each stage.
COLLIDERS={1:((0x8c107ae8,18),(0x8c107b30,18)),2:((0x8c10a5a0,1),),
           3:((0x8c1bc91c,13),(0x8c1bc950,13)),5:((0x8c204440,17),(0x8c204484,17)),
           6:((0x8c20b98c,17),(0x8c20b9d0,17))}


def build(tree,stage):
    sys.path.insert(0,str(tree/'decompile/tools'))
    from audit_setobjs import SPECS
    dat,texmap,count,near,far,texture,fixed=SPECS[stage]
    def run(script,*args):
        subprocess.run([sys.executable,'tools/'+script,*map(str,args)],cwd=tree/'decompile',check=True)
    out='../psp/'
    roots=(['--roots='+','.join([f'{near:x}']*count+[f'{far:x}']*count),
            '--roottexlists='+','.join([str(texture)]*(count*2))] if fixed else
           [f'--roottable={near}',f'--appendroottable={far}',f'--rootcount={count}',
            f'--roottextable={texture}',f'--appendroottextable={texture}'])
    run('jgs_convert.py','afs/'+dat,'8cb00000',out+f'stage{stage}_obj.jgs',f'--texmap={texmap}',*roots)
    if stage==5:
        # Type16's retained unused legacy slot predates the fallback provenance
        # bit. Preserve its shipping flags; pixels and geometry are untouched.
        bank=tree/'psp/stage5_obj.jgs';data=bytearray(bank.read_bytes())
        oo,ob=struct.unpack_from('<2I',data,28)
        for object_id in (16,33):
            first,batches=struct.unpack_from('<2I',data,oo+object_id*32+16)
            for index in range(first,first+batches):
                at=ob+index*16+14;flags=struct.unpack_from('<H',data,at)[0]
                struct.pack_into('<H',data,at,flags&~16)
        bank.write_bytes(data)
    recipe=':'.join(f'{table:x},{count}' for table,count in COLLIDERS[stage])
    run('jgs_collision.py','afs/'+dat,str(tree/'psp'/f'set-collider-scratch-{stage}.col'),
        '--setobj='+out+f'stage{stage}.sob:'+recipe)
    (tree/'psp'/f'set-collider-scratch-{stage}.col').unlink()
    neighbor,zone,count,counts=ZONES[stage]
    run('jgs_zones.py','afs/'+dat,'extracted/1ST_READ.BIN',f'{neighbor:x}',f'{zone:x}',count,out+f'zones{stage}.bin',f'{counts:x}')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--tree',type=Path,required=True);parser.add_argument('--stage',type=int,choices=ZONES,required=True)
    args=parser.parse_args();build(args.tree.resolve(),args.stage)

if __name__=='__main__':main()
