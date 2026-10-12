#!/usr/bin/env python3
"""Regenerate pre-owner-split contacts required by interactive prop exporters."""
import argparse
from pathlib import Path
import sys
from legacy_geometry import run_export,world_end


def build(tree,stage,output):
    sys.path.insert(0,str(tree/'decompile/tools'))
    output.parent.mkdir(parents=True,exist_ok=True)
    if stage==2:
        from kogane_collision import command
        args=[v for v in command(output)[2:] if not v.startswith('--dynexclude=')]
    elif stage==6:
        args=['afs/STAGE6/000_st6dat.bin',str(output),'--grid=8cc6757c','--sectors=81',
              '--dyn=8c20ae08','--dynprotos=8c20b5bc,36','--dynrec=52','--dyncontact=default:27']
    else:raise ValueError('Unsupported collision context')
    run_export(tree,args)
    data=output.read_bytes();output.write_bytes(data[:world_end(data)])


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--tree',type=Path,required=True)
    parser.add_argument('--stage',type=int,choices=(2,6),required=True);parser.add_argument('--out',type=Path,required=True)
    a=parser.parse_args();build(a.tree.resolve(),a.stage,a.out.resolve())
if __name__=='__main__':main()
