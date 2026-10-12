"""Synthetic source-row reuse: exact bytes, semantic keys, rebasing and fallback."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
DRIVER = r'''
#include <assert.h>
#include <stdlib.h>
static int fail_realloc;
static void *optional_realloc(void *p,size_t n){if(fail_realloc){fail_realloc=0;return NULL;}return realloc(p,n);}
#define realloc optional_realloc
#include "portable_models.c"
#undef realloc
#define BLOCK 1024u
#define COPIES 100u
typedef struct {unsigned char data[BLOCK*COPIES];} Input;
int jgr_disc_read(JgrDisc *d,const JgrDiscFile *f,uint32_t off,void *out,size_t n){
 if(off>f->size||n>f->size-off||f->offset+off+n>BLOCK*COPIES)return -1;
 memcpy(out,((Input *)d)->data+f->offset+off,n);return 0;
}
int jgr_disc_find(JgrDisc *d,const char *n,JgrDiscFile *f){(void)d;(void)n;(void)f;return -1;}
static void fixture(Input *in,unsigned block){
 unsigned at=block*BLOCK;unsigned char *p=in->data+at;memset(p,0,BLOCK);
 w32(p+32,0);w32(p+36,at+96);wf(p+40,2);wf(p+44,3);wf(p+48,4);
 for(unsigned k=0;k<3;k++)wf(p+64+k*4,1);w32(p+76,at+352);
 w32(p+96,at+128);w32(p+100,at+256);wf(p+116,1);
 w16(p+128,41);w16(p+130,19);w16(p+132,2);w16(p+134,3);
 for(unsigned i=0;i<3;i++){wf(p+136+i*24,i==1);wf(p+140+i*24,i==2);wf(p+152+i*24,1);}
 p[208]=255;w16(p+256,8);w16(p+258,3);w16(p+260,17);w16(p+262,2);w32(p+264,0xffb08040);
 w16(p+268,65);w16(p+270,12);w16(p+272,1);w16(p+274,3);
 for(unsigned i=0;i<3;i++){w16(p+276+i*6,i+2);w16(p+278+i*6,i*127);w16(p+280+i*6,(2-i)*100);}
 p[296]=255;for(unsigned k=0;k<3;k++)wf(p+384+k*4,1);
}
static int texture(unsigned index,FILE *out,JgrJgcTexture *r,void *user){
 (void)user;unsigned char p[1152];memset(p,(unsigned char)index,sizeof p);r->width=16;r->height=8;r->flags=0;
 r->clut=(uint32_t)ftell(out);r->pixels=r->clut+1024;return fwrite(p,1,sizeof p,out)==sizeof p?0:-1;
}
static JgrJgcBuild *create(Input *in,FILE *sections[10]){
 for(unsigned i=0;i<10;i++){sections[i]=tmpfile();assert(sections[i]);}
 JgrJgcBuild *b=jgr_jgc_create((JgrDisc *)in,sections,0);assert(b);return b;
}
static void destroy(JgrJgcBuild *b,FILE *sections[10]){jgr_jgc_free(b);for(unsigned i=0;i<10;i++)assert(!fclose(sections[i]));}
static JgrJgcCharacter character(unsigned row){
 JgrJgcCharacter c;memset(&c,0,sizeof c);jgr_model_options_default(&c.model);
 c.source.size=BLOCK*COPIES;c.root=row*BLOCK+32;c.model.source_material=1;c.model.source_environment=1;c.model.source_lighting=1;c.model.texture_count=8;
 snprintf(c.name,sizeof c.name,"Synthetic_%u",row);c.placement[0]=(float)row;return c;
}
int main(void){
 Input *in=calloc(1,sizeof *in);assert(in);for(unsigned i=0;i<COPIES;i++)fixture(in,i);
 FILE *sections[10];JgrJgcBuild *b=create(in,sections);
 for(unsigned i=0;i<90;i++){
  JgrJgcCharacter c=character(i);uint16_t map[4]={0,1,2,4};
  if(i==2)wf(in->data+i*BLOCK+136,.125);
  if(i==4)wf(in->data+i*BLOCK+360,1); /* Child pose. */
  if(i==5)w32(in->data+i*BLOCK+264,0xff204080); /* Material color. */
  if(i==6)w16(in->data+i*BLOCK+278,33); /* UV. */
  if(i==7)w16(in->data+i*BLOCK+268,65|8*256); /* Strip flags. */
  if(i==8)c.model.source_lighting=2;
  if(i==9)c.model.light=0;
  if(i==10)c.model.texture_map=map,c.model.texture_slots=4;
  if(i==11)c.model.matrix[3]=.25;
  if(i==12)c.force_alpha=1;
  if(i==13)c.force_two_sided=1;
  if(i==14)c.effect_blend=16|(2<<5)|(4<<8);
  if(i==15)c.model.source_material=0;
  if(i>=20)c.model.matrix[3]=(double)i; /* Evict old optional metadata. */
  unsigned before=b->row_hits;
  assert(!jgr_jgc_add(b,&c));
  if(JGR_MODEL_ROW_REUSE){
   int hit=i==1||i==3||(i>=16&&i<20);assert(b->row_hits==before+(unsigned)hit);
   assert(b->row_count<=ROW_REUSE_ENTRIES&&b->row_bytes<=ROW_REUSE_BYTES);
  }else assert(!b->row_hits);
 }
 FILE *out=tmpfile();assert(out&&!jgr_jgc_finish(b,out,texture,NULL));assert(!b->rows&&!b->row_bytes);
 rewind(out);unsigned char buf[4096];size_t n;while((n=fread(buf,1,sizeof buf,out)))assert(fwrite(buf,1,n,stdout)==n);assert(!fclose(out));destroy(b,sections);
 /* Allocation failure in the optional collector falls back to ordinary geometry. */
 b=create(in,sections);JgrJgcCharacter c=character(0);if(JGR_MODEL_ROW_REUSE)fail_realloc=1;
 assert(!jgr_jgc_add(b,&c)&&!fail_realloc&&!b->row_hits);destroy(b,sections);
 /* Changed malformed hierarchy/vertex chunks must never be accepted as an old row. */
 for(unsigned bad=0;bad<3;bad++){
  fixture(in,1);b=create(in,sections);c=character(0);assert(!jgr_jgc_add(b,&c));c=character(1);
  if(bad==0)w32(in->data+BLOCK+76,BLOCK+32);
  else if(bad==1)wf(in->data+BLOCK+136,INFINITY);
  else w16(in->data+BLOCK+270,65535);
  assert(jgr_jgc_add(b,&c)&&!b->row_hits);destroy(b,sections);
 }
 free(in);return 0;
}
'''


class ModelRowReuse(unittest.TestCase):
    def test_exact_reference_layout_semantics_and_bounded_fallback(self):
        with tempfile.TemporaryDirectory(prefix='jgr-model-row-') as tmp:
            work = Path(tmp)
            source = work/'driver.c'
            source.write_text(DRIVER)
            outputs = []
            for enabled in (0, 1):
                exe = work/('cached' if enabled else 'reference')
                subprocess.run(['cc', '-std=gnu99', '-O1', '-g', '-ffp-contract=off',
                                '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                                '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'psp/src'),
                                '-DJGR_MODEL_ROW_REUSE='+str(enabled), str(source), '-lm', '-o', str(exe)], check=True)
                result = subprocess.run([exe], capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr.decode())
                outputs.append(result.stdout)
            self.assertEqual(outputs[0], outputs[1])
            self.assertGreater(len(outputs[0]), 10000)

    def test_psp_compile(self):
        cc = os.environ.get('PSP_CC')
        if not cc:
            self.skipTest('Set PSP_CC')
        with tempfile.TemporaryDirectory(prefix='jgr-row-psp-') as tmp:
            subprocess.run([cc, '-std=gnu99', '-O2', '-G0', '-ffp-contract=off',
                            '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'psp/src'),
                            '-c', str(ROOT/'psp/src/portable_models.c'), '-o', str(Path(tmp)/'models.o')], check=True)


if __name__ == '__main__':
    unittest.main()
