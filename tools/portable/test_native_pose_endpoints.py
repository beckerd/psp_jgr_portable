"""Native-only exact endpoint pruning against the unchanged gameplay pose tree.

The synthetic bank is authored test data. JGR_NATIVE_POSE_ASSET may name a local
converted chars.jgc; no original keys or poses are included here.
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest
import test_native_pose

ROOT = Path(__file__).resolve().parents[2]
PREFIX = r'''
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "player.h"
#include "portable_native_pose.h"
unsigned long long POSE_COUNTER;
static int locomotion_clip(const Player *p,int ref)
{
 const Chars *c=p->spray_variant&&p->spray_bank?p->spray_bank:p->pose_bank;
 const JgcChar *ch=c->chars+(c==p->pose_bank?p->pose_model:0);
 return ref%(int)ch->nmotions;
}
static void counted_sample(const Chars *c,const JgcChar *ch,int m,float f,unsigned b,CharPose *out)
{POSE_COUNTER++;jgr_native_chars_sample_pose(c,ch,m,f,b,out);}
#define chars_sample_pose counted_sample
#define chars_blend_pose jgr_native_chars_blend_pose
'''
DRIVER = r'''
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "player.h"
#include "portable_native_pose.h"
void reference_skating(const Player*,unsigned,CharPose*);
void candidate_skating(const Player*,unsigned,CharPose*);
void reference_mix(const Player*,int,int,float,float,unsigned,CharPose*);
void candidate_mix(const Player*,int,int,float,float,unsigned,CharPose*);
extern unsigned long long reference_samples,candidate_samples;
SYNTHETIC_BANK
static void compare(Chars *c,unsigned model,int full)
{
 const JgcChar *ch=c->chars+model;if(!ch->nmotions)return;
 Player p={0};p.pose_bank=c;p.pose_model=model;p.spray_bank=c;
 const float speeds[]={-1.f,0.f,0x1.da12f6p-2f,0x1.da12f6p-1f,0x1.da12f6p+0f,0x1.284bdap+1f,4.f};
 const float dash[]={-0.f,0.f,1.f,.375f,-.25f,1.25f};
 const float lean[]={-1.f,-.375f,0.f,.375f,1.f};
 const float boost[]={0.f,.25f,1.f};
 const float frames[]={-0.f,.375f,3.875f};
 for(unsigned bone=0;bone<ch->nbones;bone++){
  const float weights[]={-0.f,0.f,1.f,nextafterf(0.f,1.f),nextafterf(1.f,0.f),-.25f,.375f,1.25f};
  for(unsigned i=0;i<sizeof weights/sizeof *weights;i++){
   CharPose a,b;reference_mix(&p,1,3,1.125f,weights[i],bone,&a);candidate_mix(&p,1,3,1.125f,weights[i],bone,&b);
   assert(!memcmp(&a,&b,sizeof a));
  }
 }
 unsigned long long compares=0;
 for(unsigned variant=0;variant<(full?2u:1u);variant++)
 for(unsigned mirror=0;mirror<2;mirror++)
 for(unsigned f=0;f<(full?3u:1u);f++)
 for(unsigned v=0;v<sizeof speeds/sizeof *speeds;v++)
 for(int near=full?-1:0;near<=(full?1:0);near++)
 for(unsigned d=0;d<sizeof dash/sizeof *dash;d++)
 for(unsigned l=0;l<sizeof lean/sizeof *lean;l++)
 for(unsigned b=0;b<sizeof boost/sizeof *boost;b++){
  p.spray_variant=variant;p.mirrored=mirror;p.pose_frame=frames[f];
  p.speed=near?nextafterf(speeds[v],near<0?-INFINITY:INFINITY):speeds[v];
  p.pose_dash=dash[d];p.skate_lean=lean[l];p.pose_boost=boost[b];
  unsigned bones=variant?c->chars->nbones:ch->nbones;
  for(unsigned bone=0;bone<bones;bone++){
   CharPose a,z;reference_skating(&p,bone,&a);candidate_skating(&p,bone,&z);
   assert(!memcmp(&a,&z,sizeof a));compares++;
  }
 }
 printf("model=%u exact_joint_results=%llu\n",model,compares);
}
int main(int argc,char **argv)
{
 assert(argc>=2);Chars c;
 if(!strcmp(argv[1],"real")){
  assert(argc==3);FILE *f=fopen(argv[2],"rb");assert(f&&!fseek(f,0,SEEK_END));long n=ftell(f);assert(n>0);rewind(f);
  unsigned char *data=malloc(n);assert(data&&fread(data,1,n,f)==(size_t)n&&!fclose(f));bind(&c,data);
  assert(!memcmp(c.h->magic,"JGC2",4));
  for(unsigned m=0;m<c.h->n_chars;m++)compare(&c,m,0);
 }else {synthetic(&c);compare(&c,0,1);}
 assert(reference_samples>candidate_samples);
 printf("reference_samples=%llu candidate_samples=%llu saved=%llu\n",reference_samples,candidate_samples,reference_samples-candidate_samples);
 jgr_native_chars_pose_forget();free(c.base);return 0;
}
'''


class NativePoseEndpoints(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='jgr-pose-endpoints-')
        cls.directory = Path(cls.temp.name)
        source = (ROOT/'psp/src/player.c').read_text()
        block = source[source.index('static void sample_ref('):source.index('static void animation_pose(')]
        flags = ['cc', '-O2', '-std=gnu99', '-ffp-contract=off', '-Wall', '-Wextra', '-Werror',
                 '-Wno-misleading-indentation', '-ffunction-sections', '-fdata-sections',
                 '-fsanitize=address,undefined', '-DJGR_DIAGNOSTICS=0',
                 '-I'+str(ROOT/'psp/src'), '-I'+str(ROOT/'psp/tests/render_stubs'), '-I'+str(ROOT/'psp/tests/stubs')]
        objects = []
        for name,enabled in (('reference',0),('candidate',1)):
            path = cls.directory/(name+'.c')
            path.write_text(PREFIX.replace('POSE_COUNTER',name+'_samples') + block +
                '\nvoid '+name+'_skating(const Player*p,unsigned b,CharPose*out){skating_pose(p,b,out);}\n' +
                'void '+name+'_mix(const Player*p,int a,int b,float f,float w,unsigned bone,CharPose*out){mix_ref(p,a,b,f,w,bone,out);}\n')
            obj = cls.directory/(name+'.o');objects.append(obj)
            subprocess.run(flags+['-DPLAYER_NATIVE_POSE_ENDPOINTS='+str(enabled),'-c',str(path),'-o',str(obj)],check=True)
        helper = test_native_pose.PROBE
        helper = helper[helper.index('static void bind('):helper.index('static void all_synthetic(')]
        (cls.directory/'driver.c').write_text(DRIVER.replace('SYNTHETIC_BANK',helper))
        cls.exe = cls.directory/'probe'
        subprocess.run(flags+[str(cls.directory/'driver.c'),*map(str,objects),str(ROOT/'psp/src/charpose.c'),
            str(ROOT/'psp/src/portable_native_pose.c'),'-Wl,-dead_strip' if sys.platform=='darwin' else '-Wl,--gc-sections',
            '-lm','-o',str(cls.exe)],check=True)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def test_exact_endpoint_and_adjacent_float_weights_across_skating_tree(self):
        subprocess.run([str(self.exe),'synthetic'],check=True)

    @unittest.skipUnless(os.environ.get('JGR_NATIVE_POSE_ASSET'), 'set JGR_NATIVE_POSE_ASSET for local bank coverage')
    def test_generated_character_bank_pose_trees(self):
        subprocess.run([str(self.exe),'real',os.environ['JGR_NATIVE_POSE_ASSET']],check=True)



if __name__ == '__main__':
    unittest.main()
