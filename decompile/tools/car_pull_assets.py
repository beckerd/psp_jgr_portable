#!/usr/bin/env python3
"""Export original traffic rear-grip planes and anchor points, without resampling."""
import argparse,struct,sys
from pathlib import Path
from path_assets import source,BASE,ROOT

def export(stage):
 data,_,types,_=source(stage)
 out=bytearray(struct.pack('<4sII',b'GRP1',stage,len(types)))
 for t in types:
  address=t['slip'];out+=struct.pack('<I',address)
  payload=data[address-BASE:address-BASE+156] if address else bytes(156)
  assert len(payload)==156
  out+=payload
 return bytes(out)

def main():
 p=argparse.ArgumentParser();p.add_argument('--check',action='store_true');a=p.parse_args()
 for stage in (1,2,3,5,6):
  data=export(stage);path=ROOT/'psp'/f'traffic{stage}_grips.bin'
  if a.check:assert path.read_bytes()==data,path
  else:path.write_bytes(data)
  print(path.name,len(data),'original planes/anchors')
if __name__=='__main__':main()
