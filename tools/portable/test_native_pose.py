#!/usr/bin/env python3
"""Exact native pose cursor/interpolation tests; optional locally supplied JGC.

Set JGR_NATIVE_POSE_ASSET to a generated chars.jgc for real-bank coverage.
No original assets, animation keys or poses are included in these tests.
"""
from pathlib import Path
import os
import shlex
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
PROBE = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "portable_native_pose.h"
#include "pose_sample.h"
#include "source_fmac.h"
void real_chars_sample_prepare(const Chars*,const JgcChar*,int,float,CharSampleCursor*);
static unsigned long long prepares,compares;
void chars_sample_prepare(const Chars*c,const JgcChar*ch,int m,float f,CharSampleCursor*p)
{prepares++;real_chars_sample_prepare(c,ch,m,f,p);}

/* The previous production sampler: generic TRS, then source-rounded overwrite.
 * This intentionally retains the duplicate arithmetic as an independent oracle. */
static void reference_sample(const Chars*c,const JgcChar*ch,int motion,float frame,unsigned bone,CharPose*pose)
{
 CharSampleCursor cursor;chars_sample_prepare(c,ch,motion,frame,&cursor);
 chars_sample_prepared(&cursor,bone,pose);if(!cursor.channels)return;
 const JgcChan*cn=cursor.channels+bone;
 unsigned counts[2]={cn->npos,cn->nscl},offsets[2]={cn->off_pos,cn->off_scl};float*outputs[2]={pose->pos,pose->scl};
 for(unsigned kind=0;kind<2;kind++)if(counts[kind]){
  const float*base=(const float*)(cursor.keys+offsets[kind]);
  const float*a=base+(cursor.k<(int)counts[kind]?cursor.k:counts[kind]-1)*3;
  const float*b=base+(cursor.k1<(int)counts[kind]?cursor.k1:counts[kind]-1)*3;
  for(unsigned k=0;k<3;k++)outputs[kind][k]=source_fmac(b[k]-a[k],cursor.t,a[k]);
 }
}
static void reference_blend(CharPose*a,const CharPose*b,float weight)
{
 if(weight==0.f)return;if(weight==1.f){*a=*b;return;}
 for(unsigned k=0;k<3;k++){
  a->pos[k]=source_fmac(b->pos[k]-a->pos[k],weight,a->pos[k]);
  a->scl[k]=source_fmac(b->scl[k]-a->scl[k],weight,a->scl[k]);
  a->ang[k]+=(int)((short)(b->ang[k]-a->ang[k])*weight);
 }
 a->ev&=b->ev|~7u;a->keyed|=b->keyed;
}
static void reference_overlay(const Chars*c,const JgcChar*ch,int motion,float frame,unsigned bone,float weight,CharPose*pose)
{
 CharPose other;reference_sample(c,ch,motion,frame,bone,&other);
 const JgcChan*cn=(const JgcChan*)chars_data(chars_motion_owner(c),c->motions[motion].chan0)+bone;
 if(!cn->npos){memcpy(other.pos,pose->pos,12);other.ev=(other.ev&~1u)|(pose->ev&1u);}
 if(!cn->nang){memcpy(other.ang,pose->ang,12);other.ev=(other.ev&~2u)|(pose->ev&2u);}
 if(!cn->nscl){memcpy(other.scl,pose->scl,12);other.ev=(other.ev&~4u)|(pose->ev&4u);}
 other.keyed|=pose->keyed;reference_blend(pose,&other,weight);
}
static void equal(const CharPose*a,const CharPose*b)
{assert(!memcmp(a,b,sizeof *a));compares++;}
static void check(const Chars*c,const JgcChar*ch,int m,float f,unsigned bone)
{
 CharPose a,b;reference_sample(c,ch,m,f,bone,&a);jgr_native_chars_sample_pose(c,ch,m,f,bone,&b);equal(&a,&b);
 if(m<0)return;
 const float weights[]={0.f,1.f,0.375f,-0.25f,1.25f};
 for(unsigned w=0;w<sizeof weights/sizeof *weights;w++){
  CharPose x=a,y=b;
  reference_overlay(c,ch,m,-f+0.125f,bone,weights[w],&x);
  jgr_native_chars_overlay_pose(c,ch,m,-f+0.125f,bone,weights[w],&y);equal(&x,&y);
  reference_blend(&x,&a,weights[w]);jgr_native_chars_blend_pose(&y,&b,weights[w]);equal(&x,&y);
 }
}
static void bind(Chars*c,unsigned char*data)
{
 memset(c,0,sizeof *c);c->base=data;c->h=(JgcHeader*)data;
 c->chars=(JgcChar*)(data+c->h->off_chars);c->bones=(JgcBone*)(data+c->h->off_bones);c->motions=(JgcMotion*)(data+c->h->off_motions);
}
static void synthetic(Chars*c)
{
 unsigned char*data=calloc(1,65536);assert(data);JgcHeader*h=(JgcHeader*)data;
 h->n_chars=1;h->n_bones=24;h->n_motions=32;h->off_chars=128;h->off_bones=256;h->off_motions=2048;h->off_keys=32768;h->off_pix=65536;
 bind(c,data);c->chars->nbones=24;c->chars->nmotions=32;
 for(unsigned b=0;b<24;b++){
  JgcBone*p=c->bones+b;p->ev=b*9;
  for(unsigned k=0;k<3;k++){p->pos[k]=(b-12.f)*(k+1);p->scl[k]=1.f+k*0.25f;p->ang[k]=(int)b*17000+(int)k;}
 }
 for(unsigned m=0;m<32;m++){
  c->motions[m]=(JgcMotion){m%9,4096+m*24*sizeof(JgcChan)};
  JgcChan*cn=(JgcChan*)(data+c->motions[m].chan0);
  for(unsigned b=0;b<24;b++)cn[b]=(JgcChan){b%4?1+b%8:0,0,b%5?1+b%8:0,256,b%3?1+b%8:0,128};
 }
 const float values[]={-0.f,0.f,0x1.fffffep-20f,-0x1.fffffep-10f,100.f,-200.f,0x1.000002p10f,0x1.ffffffp1f};
 float*pos=(float*)(data+h->off_keys),*scl=(float*)(data+h->off_keys+128);short*ang=(short*)(data+h->off_keys+256);
 for(unsigned i=0;i<24;i++){pos[i]=values[i%8];scl[i]=values[(i+3)%8];ang[i]=(short)(i%3==0 ? 32760:i%3==1?-32760:i*4001);}
}
static void all_synthetic(Chars*c)
{
 const float frames[]={-10000.75f,-8.f,-1.125f,-0.f,0.f,0.125f,0.99999f,1.f,3.5f,7.99999f,8.f,10000.25f};
 for(unsigned m=0;m<32;m++)for(unsigned f=0;f<sizeof frames/sizeof *frames;f++)for(unsigned b=0;b<24;b++)check(c,c->chars,m,frames[f],b);
 for(unsigned b=0;b<24;b++)check(c,c->chars,-1,0,b);
}
static void reuse(Chars*c)
{
 jgr_native_chars_pose_forget();prepares=0;CharPose p;
 /* Six simultaneous clips cover a skating blend plus overlays for each bone. */
 for(unsigned repeat=0;repeat<2;repeat++)for(unsigned b=0;b<24;b++)for(unsigned m=0;m<6;m++)jgr_native_chars_sample_pose(c,c->chars,m,3.25f,b,&p);
 assert(prepares==6);printf("reuse_samples=288 prepare_calls=%llu\n",prepares);
 jgr_native_chars_pose_forget();prepares=0;
 jgr_native_chars_sample_pose(c,c->chars,0,0.f,0,&p);jgr_native_chars_sample_pose(c,c->chars,0,-0.f,0,&p);
 assert(prepares==2); /* Bit-exact frame key, including signed zero. */
}
static void lifetime(Chars*c)
{
 Chars owner,borrowed;synthetic(&owner);borrowed=*c;borrowed.motion_owner=&owner;
 for(unsigned i=0;i<64;i++)check(&borrowed,borrowed.chars,i%32,i*.125f,i%24);
 /* Key payloads remain live, not memoized; descriptor rebinding and nframes
    changes cannot accidentally reuse a cursor for different storage. */
 ((float*)(owner.base+owner.h->off_keys))[3]=-987.25f;
 check(&borrowed,borrowed.chars,1,.5f,1);
 c->motions[1].nframes=2;check(&borrowed,borrowed.chars,1,2.5f,1);
 c->motions[1].chan0=c->motions[2].chan0;check(&borrowed,borrowed.chars,1,2.5f,1);
 borrowed.bones=owner.bones;check(&borrowed,borrowed.chars,1,2.5f,1);
 owner.motion_data=malloc(65536-owner.h->off_motions);assert(owner.motion_data);
 memcpy(owner.motion_data,owner.base+owner.h->off_motions,65536-owner.h->off_motions);
 check(&borrowed,borrowed.chars,1,2.5f,1);
 jgr_native_chars_pose_forget();free(owner.motion_data);free(owner.base);
 /* Reused descriptors, independent banks and eviction all keep exact results. */
 for(unsigned n=0;n<64;n++){
  synthetic(&owner);((float*)(owner.base+owner.h->off_keys))[3]=(float)n;
  for(unsigned f=0;f<32;f++)check(&owner,owner.chars,1,f*.125f,1);
  jgr_native_chars_pose_forget();free(owner.base);
 }
}
static double seconds(void){struct timespec t;assert(!clock_gettime(CLOCK_MONOTONIC,&t));return t.tv_sec+t.tv_nsec*1e-9;}
static void benchmark(Chars*c)
{
 volatile float sink=0;CharPose p;const unsigned loops=12000;double begin=seconds();prepares=0;
 for(unsigned t=0;t<loops;t++)for(unsigned b=0;b<c->chars->nbones;b++)for(unsigned m=0;m<6;m++){
  reference_sample(c,c->chars,c->chars->motion0+m,t*.125f,b,&p);sink+=p.pos[0];
 }
 double old=seconds()-begin;unsigned long long old_calls=prepares;prepares=0;jgr_native_chars_pose_forget();begin=seconds();
 for(unsigned t=0;t<loops;t++)for(unsigned b=0;b<c->chars->nbones;b++)for(unsigned m=0;m<6;m++){
  jgr_native_chars_sample_pose(c,c->chars,c->chars->motion0+m,t*.125f,b,&p);sink+=p.pos[0];
 }
 printf("host_only baseline_seconds=%.6f candidate_seconds=%.6f baseline_prepares=%llu candidate_prepares=%llu samples=%u sink=%.9g\n",old,seconds()-begin,old_calls,prepares,loops*c->chars->nbones*6,(double)sink);
}
static void real_bank(const char*path)
{
 FILE*f=fopen(path,"rb");assert(f);assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>0);rewind(f);
 unsigned char*data=malloc((size_t)size);assert(data);assert(fread(data,1,(size_t)size,f)==(size_t)size);assert(!fclose(f));
 Chars c;bind(&c,data);assert(!memcmp(c.h->magic,"JGC2",4));unsigned clips=0;
 for(unsigned i=0;i<c.h->n_chars;i++){
  const JgcChar*ch=c.chars+i;
  for(unsigned m=0;m<ch->nmotions;m++){
   unsigned motion=ch->motion0+m;float n=(float)c.motions[motion].nframes;
   float frames[]={-n-.125f,-0.f,0.f,.375f,n*.5f,n-.125f,n,n+.25f};clips++;
   for(unsigned k=0;k<sizeof frames/sizeof *frames;k++)for(unsigned b=0;b<ch->nbones;b++)check(&c,ch,motion,frames[k],b);
  }
 }
 printf("real_chars=%u referenced_clips=%u comparisons=%llu\n",c.h->n_chars,clips,compares);benchmark(&c);
 jgr_native_chars_pose_forget();free(data);
}
int main(int argc,char**argv)
{
 assert(argc>=2);if(!strcmp(argv[1],"real")){assert(argc==3);real_bank(argv[2]);return 0;}
 Chars c;synthetic(&c);
 if(!strcmp(argv[1],"exact"))all_synthetic(&c);
 else if(!strcmp(argv[1],"reuse"))reuse(&c);
 else if(!strcmp(argv[1],"lifetime"))lifetime(&c);
 else {assert(!strcmp(argv[1],"benchmark"));benchmark(&c);}
 jgr_native_chars_pose_forget();free(c.base);printf("comparisons=%llu\n",compares);return 0;
}
'''


class NativePose(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='jgr-native-pose-')
        cls.directory = Path(cls.temp.name)
        (cls.directory/'probe.c').write_text(PROBE)
        flags = [os.environ.get('CC', 'cc'), '-O2', '-ffp-contract=off',
                 '-ffunction-sections', '-fdata-sections', '-DJGR_DIAGNOSTICS=0',
                 '-I'+str(ROOT/'psp/src'), '-I'+str(ROOT/'psp/tests/render_stubs'),
                 '-I'+str(ROOT/'psp/tests/stubs')]
        flags += shlex.split(os.environ.get('JGR_NATIVE_POSE_CFLAGS', ''))
        subprocess.run(flags + ['-Dchars_sample_prepare=real_chars_sample_prepare',
                       '-c', str(ROOT/'psp/src/charpose.c'), '-o', str(cls.directory/'charpose.o')], check=True)
        cls.exe = cls.directory/'probe'
        subprocess.run(flags + [str(cls.directory/'probe.c'), str(cls.directory/'charpose.o'),
                       str(ROOT/'psp/src/portable_native_pose.c'),
                       '-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections',
                       '-lm', '-o', str(cls.exe)], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def run_probe(self, *args):
        subprocess.run([str(self.exe), *args], check=True)

    def test_source_rounding_sparse_channels_and_overlay(self):
        self.run_probe('exact')

    def test_prepare_reuse_across_bones_and_clips(self):
        self.run_probe('reuse')

    def test_borrowed_storage_rebinding_mutation_eviction_and_forget(self):
        self.run_probe('lifetime')

    @unittest.skipUnless(os.environ.get('JGR_NATIVE_POSE_ASSET'), 'set JGR_NATIVE_POSE_ASSET for local real-bank parity')
    def test_generated_real_bank_against_previous_sampler(self):
        self.run_probe('real', os.environ['JGR_NATIVE_POSE_ASSET'])

    @unittest.skipUnless(os.environ.get('JGR_NATIVE_POSE_BENCHMARK'), 'optional host-only timing')
    def test_host_timing(self):
        self.run_probe('benchmark')


if __name__ == '__main__':
    unittest.main()
