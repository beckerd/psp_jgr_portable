"""Exact authored-mip selection with optional session source fingerprints.

Synthetic tests need no game data. Supported-disc integration is opt-in and
compares against the existing asset catalog, including missing-cache fallback.
"""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[2]
SOURCES=['portable_stage','portable_stage_pack','portable_scratch','portable_models','portable_disc','portable_indexed','portable_convert','portable_stage_derived']
HARNESS=r'''
#include "portable_stage_derived.h"
#include "portable_indexed.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
static void failed_capture(JgrDisc *disc,const JgrStageTextureSource *source,const unsigned char *rgba,FILE *stream,void *user){
 (void)disc;(void)source;(void)rgba;(void)user;
 /* An optional failed reader can leave EOF; the stage owner restores its
  * append cursor and clean stream state before the next texture write. */
 fseek(stream,0,SEEK_END);while(fgetc(stream)!=EOF){}
}
int main(int argc,char **argv){
 if(argc!=6)return 2;
 JgrDisc *disc=NULL,*other=NULL;char error[192];unsigned stage=(unsigned)strtoul(argv[3],NULL,10);
 if(jgr_disc_open(&disc,argv[1],error,sizeof error)){fprintf(stderr,"%s\n",error);return 3;}
 int capture=!strcmp(argv[5],"failed-observer")||!strcmp(argv[5],"capture")||!strcmp(argv[5],"reset")||!strcmp(argv[5],"other-disc")||!strcmp(argv[5],"partial")||!strcmp(argv[5],"no-identity");
 if(!strcmp(argv[5],"fallback"))jgr_stage_mips_test_limits(2048,0);
 if(!strcmp(argv[5],"partial"))jgr_stage_mips_test_limits(1,8192);
 if(!strcmp(argv[5],"no-identity"))jgr_stage_mips_test_limits(0,8192);
 if(capture){
  jgr_stage_mips_cache_begin(disc);jgr_stage_texture_capture(disc,!strcmp(argv[5],"failed-observer")?failed_capture:jgr_stage_mips_capture,NULL);
  FILE *city=tmpfile(),*owners=tmpfile();if(!city||!owners)return 4;
  if(jgr_stage_city_geometry(disc,stage,argv[4],city,owners))return 5;
  if(fclose(city)||fclose(owners))return 6;
  jgr_stage_texture_capture(NULL,NULL,NULL);
  if(!strcmp(argv[5],"reset"))jgr_stage_mips_cache_begin(disc);
  if(!strcmp(argv[5],"other-disc")&&jgr_disc_open(&other,argv[1],error,sizeof error))return 7;
 }
 FILE *jgs=fopen(argv[2],"rb");if(!jgs)return 8;
 JgrStageMipsStats captured;jgr_stage_mips_stats(&captured);jgr_stage_mips_stats_reset();
 clock_t start=clock();int result=jgr_stage_mips(other?other:disc,jgs,stage,stdout);clock_t finish=clock();
 JgrStageMipsStats stats;jgr_stage_mips_stats(&stats);
 fprintf(stderr,"{\"status\":%d,\"cpu_seconds\":%.9f,\"captured_before\":%u,\"sources\":%u,\"identity_hits\":%u,\"base_encodes\":%u,\"aliases\":%u,\"lower_candidates\":%u,\"eager_fallbacks\":%u}\n",result,(double)(finish-start)/CLOCKS_PER_SEC,captured.captured,stats.sources,stats.identity_hits,stats.base_encodes,stats.aliases,stats.lower_candidates,stats.eager_fallbacks);
 fclose(jgs);jgr_stage_mips_cache_end();jgr_disc_close(other);jgr_disc_close(disc);return result?9:fflush(stdout)!=0;
}
'''
UNIT=r'''
#include <assert.h>
#include <stdlib.h>
static size_t fail_malloc_size,fail_realloc_size;
static void *test_malloc(size_t n){return n==fail_malloc_size?NULL:malloc(n);}
static void *test_realloc(void *p,size_t n){return n==fail_realloc_size?NULL:realloc(p,n);}
#define malloc test_malloc
#define realloc test_realloc
#include "portable_stage_derived.c"
#undef malloc
#undef realloc
static unsigned old_twiddle(unsigned x,unsigned y){unsigned n=0;for(unsigned bit=0;bit<10;bit++){n|=((y>>bit)&1)<<(2*bit);n|=((x>>bit)&1)<<(2*bit+1);}return n;}
static void source_image(unsigned char *raw,unsigned size,unsigned value){for(unsigned i=0;i+1<size;i+=2){raw[i]=value;raw[i+1]=value>>8;}}
int main(void){
 for(unsigned y=0;y<1024;y++)for(unsigned x=0;x<1024;x++)assert(mip_twiddle(x,y)==old_twiddle(x,y));
 /* Stage capture uses the shared PVR decoder. Prove its complete base image
  * equals mip_decode for every captured layout and pixel format. */
 const unsigned formats[]={2,4,0x11,0x12};
 for(unsigned width=64;width<=256;width*=2)for(unsigned format=0;format<4;format++)for(unsigned pf=0;pf<3;pf++) {
  unsigned bytes=3*width*width+4096;unsigned char *payload=malloc(bytes);assert(payload);
  for(unsigned i=0;i<bytes;i++)payload[i]=(unsigned char)(i*73+(i>>7));
  unsigned char *shared=jgr_pvr_decode_payload(payload,bytes,pf,formats[format],width,width);
  unsigned char *authored=mip_decode(payload,bytes,pf,formats[format],width,width);
  assert(shared&&authored&&!memcmp(shared,authored,width*width*4));free(shared);free(authored);free(payload);
 }
 /* Independent lower levels with one solid source color and exact opaque
  * palette. Same base aliases may still disagree, or have transparent lower
  * levels; transparent aliases do not establish or conflict with a match. */
 unsigned char raw[10924],palette[1024]={0};MipSource source={1,2,64,sizeof raw,0};
 source_image(raw,sizeof raw,0xf800);palette[0]=255;palette[3]=255;palette[4+1]=255;palette[4+3]=255;
 MipTexture t={0};t.width=t.height=64;t.cost=1280;
 assert(!mip_lower(raw,&source,palette,&t));assert(t.matched&&t.quality&&!t.ambiguous);
 assert(!mip_lower(raw,&source,palette,&t));assert(!t.ambiguous);
 source_image(raw,sizeof raw,0x07e0);assert(!mip_lower(raw,&source,palette,&t));assert(t.ambiguous);free(t.lower);
 memset(&t,0,sizeof t);t.width=t.height=64;t.cost=1280;source.pf=0;source_image(raw,sizeof raw,0x7c00);
 assert(!mip_lower(raw,&source,palette,&t));assert(!t.matched&&!t.lower);
 source_image(raw,sizeof raw,0xfc00);assert(!mip_lower(raw,&source,palette,&t));assert(t.matched&&t.quality);
 source_image(raw,sizeof raw,0x7c00);assert(!mip_lower(raw,&source,palette,&t));assert(!t.ambiguous);free(t.lower);
 /* Quality belongs to the first opaque alias, even when later aliases encode
  * identically but have a different squared error against the palette. */
 memset(&t,0,sizeof t);t.width=t.height=64;t.cost=1280;source.pf=1;memset(palette,0,sizeof palette);palette[3]=255;
 source_image(raw,sizeof raw,0xffff);assert(!mip_lower(raw,&source,palette,&t));assert(t.matched&&!t.quality);
 source_image(raw,sizeof raw,0);assert(!mip_lower(raw,&source,palette,&t));assert(!t.ambiguous&&!t.quality);free(t.lower);
 JgrDisc *disc=(JgrDisc *)(uintptr_t)1,*other=(JgrDisc *)(uintptr_t)2;
 JgrStageTextureSource capture={{4096,20000},16,64,64,64,64,0,1024};
 unsigned char image[64*64*4];memset(image,255,sizeof image);FILE *encoded=tmpfile();assert(encoded);
 unsigned char data[5120];memset(data,37,sizeof data);assert(fwrite(data,1,sizeof data,encoded)==sizeof data);
 jgr_stage_mips_cache_begin(disc);fail_malloc_size=sizeof(MipIdentities);
 jgr_stage_mips_capture(disc,&capture,image,encoded,NULL);assert(!mip_cache.count&&!mip_cache.slabs);fail_malloc_size=0;
 jgr_stage_mips_capture(disc,&capture,image,encoded,NULL);
 MipIdentity *identity=mip_identity(disc,&capture.file,capture.gbix,64);assert(identity&&identity->opaque);
 JgrSha256 hash;unsigned char digest[32];jgr_sha256_init(&hash);jgr_sha256_update(&hash,data,sizeof data);jgr_sha256_final(&hash,digest);assert(!memcmp(identity->sha,digest,32));
 assert(!mip_identity(other,&capture.file,capture.gbix,64));assert(!mip_identity(disc,&capture.file,capture.gbix,128));
 JgrDiscFile changed=capture.file;changed.size++;assert(!mip_identity(disc,&changed,capture.gbix,64));changed=capture.file;changed.offset++;assert(!mip_identity(disc,&changed,capture.gbix,64));
 assert(!mip_identity(disc,&capture.file,capture.gbix+4,64));
 jgr_stage_mips_cache_begin(disc);assert(!mip_identity(disc,&capture.file,capture.gbix,64));
 capture.pixels=20000;jgr_stage_mips_capture(disc,&capture,image,encoded,NULL);assert(!mip_identity(disc,&capture.file,capture.gbix,64));
 capture.pixels=1024;image[3]=0;jgr_stage_mips_capture(disc,&capture,image,encoded,NULL);assert(mip_identity(disc,&capture.file,capture.gbix,64)->opaque==0);
 jgr_stage_mips_cache_end();assert(!mip_cache.slabs&&!mip_cache.count&&!mip_cache.disc);fclose(encoded);
 /* Optional metadata exhaustion is explicit, bounded, and leaves existing
  * alias order intact so the caller can choose its complete eager fallback. */
 MipAliases aliases={0};memset(&t,0,sizeof t);fail_realloc_size=64*sizeof(MipAlias);
 assert(mip_alias(&aliases,&t,2,16)==-1);assert(!aliases.rows&&!aliases.count&&!t.first);fail_realloc_size=0;
 jgr_stage_mips_test_limits(0,2);
 assert(!mip_alias(&aliases,&t,2,16));assert(!mip_alias(&aliases,&t,3,20));assert(mip_alias(&aliases,&t,4,24)==-1);
 assert(t.first==1&&t.last==2&&aliases.rows[0].next==2&&aliases.rows[1].next==0);free(aliases.rows);
 return 0;
}
'''

class MipsTests(unittest.TestCase):
 @classmethod
 def setUpClass(cls):
  cls.tmp=tempfile.TemporaryDirectory(prefix='jgr-mips-');cls.work=Path(cls.tmp.name)
  cls.flags=['-std=c99','-D_XOPEN_SOURCE=700','-DJGR_STAGE_MIPS_TESTING','-O2','-ffp-contract=off','-Wall','-Wextra','-Werror','-I'+str(ROOT/'psp/src')]
  (cls.work/'unit.c').write_text(UNIT);cls.unit=cls.work/'unit'
  subprocess.run(['cc',*cls.flags,str(cls.work/'unit.c'),*[str(ROOT/'psp/src'/f'{n}.c') for n in ('portable_disc','portable_indexed','portable_convert')],'-lm','-o',str(cls.unit)],check=True)
  (cls.work/'harness.c').write_text(HARNESS);cls.exe=cls.work/'mips'
  subprocess.run(['cc',*cls.flags,str(cls.work/'harness.c'),*[str(ROOT/'psp/src'/f'{n}.c') for n in SOURCES],'-lm','-o',str(cls.exe)],check=True)
 @classmethod
 def tearDownClass(cls):cls.tmp.cleanup()
 def test_exact_twiddle_alias_opacity_quality_and_session_bound_fingerprints(self):
  subprocess.run([str(self.unit)],check=True,timeout=20)
 def run_fixture(self,stage,mode):
  gdi=os.environ.get('JGR_TEST_GDI');assets=os.environ.get('JGR_TEST_ASSETS')
  if not gdi or not assets:self.skipTest('Set JGR_TEST_GDI and JGR_TEST_ASSETS for supported-disc checks')
  result=subprocess.run([str(self.exe),gdi,str(Path(assets)/f'stage{stage}.jgs'),str(stage),str(self.work),mode],capture_output=True,timeout=180)
  self.assertEqual(result.returncode,0,result.stderr.decode())
  catalog={r['path']:r for r in json.loads((ROOT/'tools/portable/asset_catalog.json').read_text())['files']}
  row=catalog[f'stage{stage}.jgm'];self.assertEqual(len(result.stdout),row['bytes']);self.assertEqual(hashlib.sha256(result.stdout).hexdigest(),row['sha256'])
  self.assertFalse(list(self.work.glob('*.tmp')))
  return json.loads(result.stderr)
 def test_all_five_cold_and_captured_and_alias_exhaustion_are_exact(self):
  for stage in (1,2,3,5,6):
   with self.subTest(stage=stage):
    cold=self.run_fixture(stage,'cold');warm=self.run_fixture(stage,'capture');eager=self.run_fixture(stage,'fallback')
    self.assertEqual(cold['identity_hits'],0);self.assertGreater(warm['identity_hits'],0);self.assertGreater(warm['captured_before'],0)
    self.assertLess(warm['base_encodes'],cold['base_encodes']);self.assertLess(cold['lower_candidates'],eager['lower_candidates']);self.assertEqual(eager['eager_fallbacks'],1)
 def test_partial_missing_reset_and_other_disc_metadata_are_exact(self):
  for mode in ('partial','no-identity','reset','other-disc','failed-observer'):
   with self.subTest(mode=mode):
    stats=self.run_fixture(1,mode)
    if mode=='partial':self.assertEqual(stats['captured_before'],1)
    elif mode in ('no-identity','other-disc'):self.assertEqual(stats['identity_hits'],0)
 def test_psp_compile(self):
  cc=os.environ.get('PSP_CC') or shutil.which('psp-gcc')
  if not cc:self.skipTest('PSP compiler not configured')
  sdk=Path(os.environ.get('PSPSDK',Path(cc).resolve().parent.parent/'psp/sdk'))
  for name in ('portable_stage','portable_stage_derived'):
   subprocess.run([cc,'-I'+str(sdk/'include'),'-O2','-std=gnu99','-Wall','-Wextra','-Werror','-ffp-contract=off','-DJGR_PORTABLE_ASSETS=1','-include',str(ROOT/'psp/src/portable_assets.h'),'-c',str(ROOT/'psp/src'/f'{name}.c'),'-o',str(self.work/f'{name}.o')],check=True)
if __name__=='__main__':unittest.main()
