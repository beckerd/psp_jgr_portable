#!/usr/bin/env python3
"""Synthetic media tests: no Dreamcast files, ROM data or captures required."""
import hashlib
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from disc import Disc,DiscError,extract_afs,safe_name
from pipeline import Node,order
from setup_assets import coverage,publish

def both32(b,o,v):struct.pack_into('<I',b,o,v);struct.pack_into('>I',b,o+4,v)
def both16(b,o,v):struct.pack_into('<H',b,o,v);struct.pack_into('>H',b,o+2,v)
def record(name,lba,size,flags=0):
    b=bytearray(33+len(name)+(len(name)%2==0));b[0]=len(b);both32(b,2,lba);both32(b,10,size);b[25]=flags;both16(b,28,1);b[32]=len(name);b[33:33+len(name)]=name;return b

def fixture(root,raw=False,name=b'1ST_READ.BIN;1'):
    blocks=[bytearray(2048) for _ in range(21)]
    blocks[0][:16]=b'SEGA SEGAKATANA ';blocks[0][64:74]=b'MK-51058  ';blocks[0][74:80]=b'V1.005'
    p=blocks[16];p[:7]=b'\x01CD001\x01';p[40:72]=b'TEST'+b' '*28;both16(p,128,2048);p[156:190]=record(b'\0',45018,2048,2)
    d=record(b'\0',45018,2048,2)+record(b'\1',45018,2048,2)+record(name,45020,7)
    blocks[18][:len(d)]=d;blocks[20][:7]=b'fixture'
    sectors=[]
    for b in blocks:
        if raw:sectors.append(b'\x00'+b'\xff'*10+b'\x00'+b'\0\0\0\1'+b+bytes(288))
        else:sectors.append(b)
    (root/'track 3.bin').write_bytes(b''.join(sectors))
    gdi=root/'test.gdi';gdi.write_text(f'1\n1 45000 4 {2352 if raw else 2048} "track 3.bin" 0\n');return gdi

class DiscTests(unittest.TestCase):
    def test_cooked_and_raw_extract_equal(self):
        for raw in (False,True):
            with self.subTest(raw=raw),tempfile.TemporaryDirectory() as d:
                root=Path(d)
                with Disc(fixture(root,raw)) as disc:
                    self.assertEqual(disc.entry('1st_read.bin').size,7)
                    with patch('disc.SUPPORTED_EXE',{hashlib.sha256(b'fixture').hexdigest():dict(id='fixture',bytes=7,product='MK-51058',version='V1.005')}):
                        self.assertEqual(disc.identify()['id'],'fixture')
                    out=root/'out';rows=disc.extract(out)
                    self.assertEqual((out/'1ST_READ.BIN').read_bytes(),b'fixture');self.assertEqual(rows[0]['bytes'],7)
    def test_wrong_release_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            with Disc(fixture(Path(d))) as disc:
                with self.assertRaisesRegex(DiscError,'Unsupported executable'):disc.identify()
    def test_path_traversal_rejected(self):
        for name in ('../bad','/bad','a/../../b','a\\b','a//b','a/./b','a\nb','a\x7fb'):
            with self.subTest(name=name),self.assertRaises(DiscError):safe_name(name)
        with tempfile.TemporaryDirectory() as d:
            with self.assertRaises(DiscError):Disc(fixture(Path(d),name=b'../bad;1'))
    def test_bad_extent_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);gdi=fixture(root);p=root/'track 3.bin';b=bytearray(p.read_bytes());both32(b,16*2048+156+2,999999);p.write_bytes(b)
            with self.assertRaisesRegex(DiscError,'extent'):Disc(gdi)
    def test_afs_bounds_and_original_name(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);p=root/'TEST.AFS';b=bytearray(83);b[:4]=b'AFS\0';struct.pack_into('<5I',b,4,1,80,3,32,48);b[32:40]=b'file.bin';b[80:]=b'abc';p.write_bytes(b)
            extract_afs(p,root/'out');self.assertEqual((root/'out/TEST/000_file.bin').read_bytes(),b'abc')
            struct.pack_into('<I',b,12,100);p.write_bytes(b)
            with self.assertRaisesRegex(DiscError,'exceeds'):extract_afs(p,root/'bad')
    def test_raw_mode2_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            root=Path(d);gdi=fixture(root,True);p=root/'track 3.bin';b=bytearray(p.read_bytes());b[15]=2;p.write_bytes(b)
            with self.assertRaisesRegex(DiscError,'MODE1'):Disc(gdi)
    def test_graph_dependencies_and_cycles(self):
        self.assertEqual([n.name for n in order([Node('b',after=('a',)),Node('a')])],['a','b'])
        with self.assertRaises(ValueError):order([Node('a',after=('b',)),Node('b',after=('a',))])
    def test_coverage_requires_exact_bytes(self):
        expected={'a':dict(bytes=1,sha256='a'),'b':dict(bytes=2,sha256='b')}
        self.assertEqual(coverage(expected,[dict(path='a',bytes=1,sha256='z')]),(['b'],['a']))

if __name__=='__main__':unittest.main()
