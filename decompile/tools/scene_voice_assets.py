#!/usr/bin/env python3
"""Package unmodified English SOSB banks for original scene opcode 43.

0cd8c0 / 1ce4a8 select common bank 4 for refs 0..14 and character bank 6
for refs 15..30. 0e7558 maps these to Manatee slots 0/1, programs ref/ref-15.
The Beat sample offsets produce the captured AICA bases 31000 / 53000.
This archive is data transport, not a replacement sound sequence.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
ROOT=Path(__file__).resolve().parents[2]
def character_banks():
    # 0ca2ac selects these resource scripts by the original character ID.
    # Asset labels in chars_spec are not a character-ID/voice lookup.
    exe=(ROOT/'decompile/extracted/1ST_READ.BIN').read_bytes()
    def u(address):return struct.unpack_from('<I',exe,address-0x8c010000)[0]
    resources=u(0x8c05bf94)
    banks=[]
    for character in range(16):
        script=0x8c1cded0+character*40
        assert u(script)==5 and u(script+4)==character and u(script+8)==0x1f
        record=u(resources+u(script+12)*4)
        assert u(record)==0x1d and u(record+32)==0x52
        banks.append(u(record+4))
    return banks
def build():
    folder=ROOT/'decompile/afs/E_SND_SE'
    paths=[folder/'005_chara_se.osb']
    for bank in character_banks():
        matches=list(folder.glob(f'{bank:03d}_*.osb'))
        assert len(matches)==1,(bank,matches);paths.append(matches[0])
    table=bytearray(17*8);images=bytearray();seen={};manifest=[]
    for i,p in enumerate(paths):
        data=p.read_bytes();assert data[:4]==b'SOSB'
        if p not in seen:seen[p]=8+len(table)+len(images);images.extend(data)
        struct.pack_into('<2I',table,i*8,seen[p],len(data))
        manifest.append(dict(slot=i,source=str(p.relative_to(ROOT)),length=len(data),sha256=hashlib.sha256(data).hexdigest()))
    return b'OSV1'+struct.pack('<I',17)+table+images,manifest
def main():
    ap=argparse.ArgumentParser(description=__doc__);ap.add_argument('--check',action='store_true');args=ap.parse_args()
    data,manifest=build();p=ROOT/'psp/voices.bin'
    if args.check:
        assert p.read_bytes()==data;assert json.loads(p.with_suffix('.json').read_text())==manifest
    else:
        p.write_bytes(data);p.with_suffix('.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(f'17 source bank slots, {len(data):,} bytes (selected pair loaded at runtime).')
if __name__=='__main__':main()
