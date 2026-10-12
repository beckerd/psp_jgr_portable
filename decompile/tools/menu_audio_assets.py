#!/usr/bin/env python3
"""Original menu SMSB bank 0 and English garage SOSB banks 1/2, unchanged.

0e6354 routes these handles separately. AICA bank manager 2a2fa8 at
pending prompt tick 40 verifies all source bytes, excluding allocation
padding. Selection uses bank 1 (060_g_voice), invitations bank 2
(061_g_voice2). No substituted Japanese or decoded half-rate SFX bank.
"""
import argparse
import hashlib
import json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
SOURCES={'menuseq.bin':'SND_SE/071_sys_se.msb',
         'menupatch.bin':'SND_SE/077_sys_se.mpb',
         'garageselect.bin':'E_SND_SE/060_g_voice.osb',
         'garagevoice.bin':'E_SND_SE/061_g_voice2.osb'}
def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check',action='store_true');ap.add_argument('--reference',type=Path)
    args=ap.parse_args();manifest={}
    for name,source in SOURCES.items():
        data=(ROOT/'decompile/afs'/source).read_bytes();target=ROOT/'psp'/name
        if args.reference and name!='garageselect.bin':
            kind={'menuseq.bin':'smsb','menupatch.bin':'smpb','garagevoice.bin':'sosb'}[name]
            assert (args.reference/f'pending_{kind}.bin').read_bytes()[:len(data)]==data,source
        if args.check:assert target.read_bytes()==data,target
        else:target.write_bytes(data)
        manifest[name]={'source':source,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
    target=ROOT/'decompile/menu_audio_manifest.json'
    if args.check:assert json.loads(target.read_text())==manifest
    else:target.write_text(json.dumps(manifest,indent=2)+'\n')
    print(json.dumps(manifest,indent=2))
if __name__=='__main__':main()
