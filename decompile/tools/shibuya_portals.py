#!/usr/bin/env python3
"""Extract all four Shibuya passage records registered by original 02f9f0."""
import argparse
from benten_portals import ROOT,generate

FIRST=0x8c1747c4

if __name__=='__main__':
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--check',action='store_true');args=ap.parse_args()
    text=generate(FIRST,'shibuya','shibuya_portals.py')
    path=ROOT/'psp/src/shibuya_portals.inc'
    if args.check:assert path.read_text()==text,'Shibuya portals differ from original executable'
    else:path.write_text(text)
