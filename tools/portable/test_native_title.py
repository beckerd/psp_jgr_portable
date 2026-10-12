#!/usr/bin/env python3
"""Synthetic bounds/timeline tests for the source-driven title converter."""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROBE = r'''
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "portable_native_title.c"
void jgr_native_player_follow_input(const Player *p,FollowInput *in)
{memset(in,0,sizeof *in);in->pos[0]=p->x;in->pos[1]=p->y;in->pos[2]=p->z;memcpy(in->body,in->pos,12);in->body[1]+=10;}
float jgr_native_player_fsca_sin(int n){return sinf((n&65535)*(6.283185307f/65536.f));}
static void put(unsigned char *p,unsigned n){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(n>>(i*8));}
static void pf(unsigned char *p,float f){unsigned n;memcpy(&n,&f,4);put(p,n);}
int main(int argc,char **argv)
{
 assert(argc==2);unsigned char image[256]={0};TitleSource source={image,sizeof image};CtrlStream stream;
 put(image,16);pf(image+4,2);pf(image+8,1);pf(image+12,2);pf(image+16,3);image[36]=128;
 if(!strcmp(argv[1],"stream")){
  assert(!stream_read(&source,TITLE_BASE,&stream));assert(stream.nrec==2&&stream.chr==16&&stream.rec==image+32);
  assert(stream_read(&source,TITLE_BASE-1,&stream));assert(stream_read(&source,0xfffffff0u,&stream));
  source.bytes=35;assert(stream_read(&source,TITLE_BASE,&stream));source.bytes=sizeof image;
  image[36]=0;assert(stream_read(&source,TITLE_BASE,&stream));image[36]=128;
  put(image,17);assert(stream_read(&source,TITLE_BASE,&stream));put(image,16);
  put(image+4,0x7f800000u);assert(stream_read(&source,TITLE_BASE,&stream));put(image+4,0x7fc00001u);assert(stream_read(&source,TITLE_BASE,&stream));
  return 0;
 }
 Player player={0};player.x=1;player.y=2;player.z=3;player.logic_tick=1;
 JgrReplayNativeSample sample={0};unsigned pointer=TITLE_BASE+64;
 put(image+64,2);put(image+68,1);put(image+72,0x20);put(image+76,TITLE_BASE+128);
 put(image+80,100);put(image+84,1);put(image+88,0x2a);put(image+92,TITLE_BASE+168);
 pf(image+128,1);pf(image+132,2);pf(image+136,3);put(image+152,4);put(image+156,10000);pf(image+160,-1);pf(image+164,-1000);
 pf(image+168,0);pf(image+172,0);pf(image+176,0);put(image+192,5);put(image+196,11000);pf(image+200,-2);pf(image+204,-2000);
 if(!strcmp(argv[1],"camera")){
  assert(!camera_sample(&source,&pointer,&player,0,&sample));assert(pointer==TITLE_BASE+64&&!sample.cut);
  assert(sample.camera_position[0]==1&&sample.camera_direction[1]==1&&sample.camera_roll==4&&sample.near_clip==1);
  player.logic_tick=2;assert(!camera_sample(&source,&pointer,&player,0,&sample));assert(pointer==TITLE_BASE+64&&!sample.cut);
  player.logic_tick=3;assert(!camera_sample(&source,&pointer,&player,0,&sample));assert(pointer==TITLE_BASE+80&&sample.cut);
  assert(sample.camera_position[0]==player.x&&sample.camera_position[1]==player.y&&sample.camera_roll==5&&sample.far_clip==2000);
  player.logic_tick=1;pointer=TITLE_BASE+64;assert(!camera_sample(&source,&pointer,&player,1,&sample)&&sample.cut);
  return 0;
 }
 assert(!strcmp(argv[1],"invalid_camera"));
 pointer=0xffffffffu;assert(camera_sample(&source,&pointer,&player,0,&sample));pointer=TITLE_BASE+64;
 put(image+68,2);assert(camera_sample(&source,&pointer,&player,0,&sample));put(image+68,1);
 put(image+72,0xffffffffu);assert(camera_sample(&source,&pointer,&player,0,&sample));put(image+72,0x20);
 put(image+76,TITLE_BASE+240);assert(camera_sample(&source,&pointer,&player,0,&sample));put(image+76,TITLE_BASE+128);
 put(image+128,0x7fc00001u);assert(camera_sample(&source,&pointer,&player,0,&sample));
 return 0;
}
'''


class NativeTitle(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='jgr-native-title-')
        cls.root = Path(cls.temp.name)
        (cls.root/'probe.c').write_text(PROBE)
        cls.exe = cls.root/'probe'
        command = [os.environ.get('CC','cc'), '-O2', '-ffp-contract=off',
                   '-ffunction-sections', '-fdata-sections', '-DJGR_DIAGNOSTICS=0',
                   '-I'+str(ROOT/'psp/src'), '-I'+str(ROOT/'psp/tests/render_stubs'),
                   '-I'+str(ROOT/'psp/tests/stubs'), str(cls.root/'probe.c'),
                   '-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections',
                   '-lm', '-o', str(cls.exe)]
        subprocess.run(command, check=True, capture_output=True, text=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_stream_bounds_and_nonfinite_header(self):
        subprocess.run([str(self.exe),'stream'], check=True)

    def test_end_tick_camera_and_part_cut(self):
        subprocess.run([str(self.exe),'camera'], check=True)

    def test_invalid_camera_is_rejected(self):
        subprocess.run([str(self.exe),'invalid_camera'], check=True)


if __name__=='__main__':
    unittest.main()
