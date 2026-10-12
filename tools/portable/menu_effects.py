#!/usr/bin/env python3
"""Recover the original menu/effect converter recipes from user-disc banks.

Only addresses and conversion choices live here; no game payload is stored.
The table slots are documented in the title, STREET, RADIO and MARKEDIT draw
routines. Outputs are compared against the private baseline by setup_assets.
"""
import argparse
from pathlib import Path
import struct
import subprocess
import sys


def build(tree, only=None):
    dec = tree / 'decompile'
    psp = tree / 'psp'
    psp.mkdir(parents=True, exist_ok=True)

    def run(script, *args):
        subprocess.run([sys.executable, str((Path(__file__).parent / 'legacy_jgc_convert.py') if script == 'legacy_jgc_convert.py' else dec / 'tools' / script), *map(str, args)],
                       cwd=dec, check=True)

    def word(data, offset):
        return struct.unpack_from('<I', data, offset)[0]

    def strings(data, base, slot, count):
        table = word(data, slot) - base
        result = []
        for i in range(count):
            offset = word(data, table + 4*i) - base
            if not 0 <= offset < len(data):
                raise ValueError('String pointer is outside source bank')
            end = data.index(0, offset)
            result.append(data[offset:end])
        return b'\n'.join(result) + b'\n'

    def selected(name):
        return not only or name in only

    def original_culling(path):
        # These early JGC1 menu exports predate the global outline flag.
        # Retain their tested strip state, including title's explicit hull.
        data=bytearray(path.read_bytes())
        h=struct.unpack_from('<16I',data,4)
        for i in range(h[2]):
            off=h[9]+i*16+14
            struct.pack_into('<H',data,off,struct.unpack_from('<H',data,off)[0]&~8)
        path.write_bytes(data)

    if selected('title'):
        dat, txr = 'afs/U_TITLE/000_titledat.bin', 'afs/U_TITLE/001_titletxr.bin'
        data = (dec / dat).read_bytes()
        rows = [f'T{i}:{dat}:{txr}:0,0,0,0:rawat:8cf00000:'
                f'{word(data,4+4*i):x}:{word(data,20+4*i):x}' for i in range(4)]
        run('legacy_jgc_convert.py', psp/'title.jgc', '--motions=1', '--nolight=1', '--texwhite=1', *rows)
        original_culling(psp/'title.jgc')
    if selected('garage'):
        dat, txr = 'afs/U_GARAGE/001_garagedat.bin', 'afs/U_GARAGE/002_garagetxr.bin'
        data = (dec / dat).read_bytes()
        slots = (0x140,0x144,0x148,0x14c,0x150,0x158,0x15c,0x160,0x164)
        rows = [f'M{i}:{dat}:{txr}:0,0,0,0:rawat:8c800000:'
                f'{word(data,s):x}:{word(data,s+0x30):x}' for i,s in enumerate(slots)]
        run('legacy_jgc_convert.py', psp/'garage_mark.jgc', '--motions=1', '--nolight=1', *rows)
        original_culling(psp/'garage_mark.jgc')
        run('jgs_tex2d.py', 'afs/U_GARAGE/000_garage_disptxr.bin', psp/'garage_disp.bin')
        # 0fe98c binds two separate one-slot texture lists in the executable.
        rows = [f'RadioBase:{dat}:{txr}:0,0,0,0:rawat:8c800000:{word(data,0x340):x}:0:texbinding=1,8c010000,8c10e5ac,1,8c800000,3',
                f'RadioAnim:{dat}:{txr}:0,0,0,0:rawat:8c800000:{word(data,0x344):x}:{word(data,0x348):x}:texbinding=1,8c010000,8c10e5c4,1,8c800000,3']
        run('jgc_convert.py', psp/'radio.jgc', '--motions=1', '--source-material=1', '--nolight=1', '--texmax=512', *rows)
        (psp/'radio_names.txt').write_bytes(strings(data,0x8c800000,0x350,26)+strings(data,0x8c800000,0x354,26))
    if selected('markedit'):
        dat='afs/E_MARKEDIT/001_markeditdat.bin'
        data=(dec/dat).read_bytes()
        run('jgs_tex2d.py','afs/E_MARKEDIT/000_edittxr.bin',psp/'markedit_disp.bin')
        run('jgs_2dlist.py',dat,'8c900000',psp/'markedit_lists.bin','--table=0,96')
        (psp/'grf_names.txt').write_bytes(strings(data,0x8c900000,4,102))
        (psp/'grf_desc.txt').write_bytes(strings(data,0x8c900000,8,204))
    if selected('effects'):
        dat,txr='afs/PLAYER/016_player_effect_dat.bin','afs/PLAYER/017_player_effect_txr.bin'
        rows=[]
        for name,root,motion in [('Can1Far',0x8c8e1aec,0x8c8eadd0),('Can1Near',0x8c8e3288,0x8c8eaf60),
                                 ('Can5Far',0x8c8e283c,0x8c8eae98),('Can5Near',0x8c8e2d68,0x8c8eb028)]:
            rows.append(f'{name}:{dat}:{txr}:0,0,0,0:rawat:8c8e1000:{root:x}:{motion:x}')
        run('jgc_convert.py',psp/'cans.jgc','--motions=1','--texmax=256',*rows)
        run('legacy_jgc_convert.py',psp/'cordon.jgc','--motions=1',
            f'Cordon:{dat}:{txr}:0,0,0,0:rawat:8c8e1000:8c8ea7d4:8c8eee70')
    if selected('scene3025'):
        run('final_scene_assets.py','--scene','90')


if __name__ == '__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--tree',type=Path,required=True)
    ap.add_argument('--only',nargs='+',choices=['title','garage','markedit','effects','scene3025'])
    args=ap.parse_args()
    build(args.tree.resolve(),args.only)
