#!/usr/bin/env python3
"""0958b0 enemy underpaint uploads, selected by JMDc +24 via 0ca2ac."""
import struct,sys
from pathlib import Path
from final_graffiti import mask_texture
ROOT=Path(__file__).resolve().parents[2]
def generate():
    exe=(ROOT/'decompile/extracted/1ST_READ.BIN').read_bytes()
    word=lambda a:struct.unpack_from('<I',exe,a-0x8c010000)[0]
    logical=struct.unpack_from('<6I',exe,0x1ce420-0x10000)
    assert logical==(0,0,2,1,3,4)
    # 0958b0 requests logical files 102..106 from bank 27. 05bc38
    # resolves those through 1927bc; the last two AFS entries are swapped.
    files=word(0x8c05bf84)
    entries=[word(word(files+(102+i)*4)+4) for i in range(5)]
    assert entries==[0,1,2,4,3]
    mapping=[entries[i] for i in logical]
    yield ROOT/'psp/src/rival_graffiti.inc',('/* Original 0ca2ac / 1ce420 / 05bc38 / 1927bc: JMDc +24 to U_GRF entry. */\n'
        +'static const unsigned char rival_graffiti_mask[6]={'+','.join(map(str,mapping))+'};\n').encode()
    for index in range(5):yield ROOT/f'psp/rivaltag{index}.bin',mask_texture(index)
if __name__=='__main__':
    for path,data in generate():
        if '--check' in sys.argv:assert path.read_bytes()==data,path
        else:path.write_bytes(data)
