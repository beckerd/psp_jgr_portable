#!/usr/bin/env python3
"""Build runtime data blobs from freshly generated source declarations."""
import argparse
from pathlib import Path
import shutil
import tempfile
from source_data_numeric import generate


def build(tree,compiler,abi_source,family='numeric'):
    compiler=shutil.which(compiler)
    if not compiler:raise ValueError('PSP compiler required for numeric asset layout; supply --cc')
    generator=generate
    if family=='pointers':
        from source_data_pointers import generate as generator
    elif family!='numeric':raise ValueError('Unknown runtime data family')
    with tempfile.TemporaryDirectory(prefix=family+'-assets-',dir=tree.parent) as tmp:
        out=Path(tmp)/'generated'
        report=generator(tree/'psp/src',out,compiler)
        for path in sorted((out/'source/psp/src').iterdir()):
            expected=abi_source/path.name
            if not expected.is_file() or expected.read_bytes()!=path.read_bytes():
                raise ValueError('Generated asset ABI does not match source build: '+path.name)
        for path in (out/'PRIVATE-assets').iterdir():shutil.copyfile(path,tree/'psp'/path.name)
        shutil.copyfile(out/'receipt.json',tree.parent/(family+'-assets.json'))
    return report


def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--tree',type=Path,required=True)
    p.add_argument('--cc',required=True);p.add_argument('--abi-source',type=Path,required=True)
    p.add_argument('--family',choices=('numeric','pointers'),default='numeric');a=p.parse_args()
    build(a.tree.resolve(),a.cc,a.abi_source.resolve(),a.family)
if __name__=='__main__':main()
