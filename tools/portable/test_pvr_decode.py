"""Payload-free PVR addressing parity, including malformed and truncated inputs."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
DRIVER = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
static int fail_alloc;
static void *decoder_malloc(size_t n){return fail_alloc?NULL:malloc(n);}
#define malloc decoder_malloc
#include "portable_convert.c"
#undef malloc
/* No filesystem/disc input is needed by this synthetic decoder test. */
int jgr_disc_read(JgrDisc *d,const JgrDiscFile *f,uint32_t off,void *out,size_t n)
{(void)d;(void)f;(void)off;(void)out;(void)n;return -1;}
static unsigned reference_twiddle(unsigned x,unsigned y)
{unsigned n=0;for(unsigned i=0;i<10;i++){n|=((y>>i)&1)<<(2*i);n|=((x>>i)&1)<<(2*i+1);}return n;}
static unsigned reference_mip(unsigned width,int vq)
{unsigned n=0;for(unsigned s=1;s<width;s*=2){unsigned b=vq?s*s/4:s*s*2;n+=b?b:1;}return n;}
static unsigned entries_for(unsigned df,unsigned w)
{return (df==3||df==4)?256:df==0x10?(w<=16?16:w<=32?32:w<=64?128:256):(w<=16?16:w<=32?64:256);}
static unsigned offset_for(const unsigned char *b,unsigned size,unsigned df,unsigned w,unsigned h,unsigned x,unsigned y)
{
 unsigned at=0;
 if(df==1||df==2||df==0x12)at=(df==1?0:reference_mip(w,0)+2)+2*reference_twiddle(x,y);
 else if(df==3||df==4||df==0x10||df==0x11){
  unsigned entries=entries_for(df,w);
  unsigned index=entries*8+((df==4||df==0x11)?reference_mip(w,1):0)+reference_twiddle(x/2,y/2);
  if(index>=size||b[index]>=entries)return UINT32_MAX;
  at=8*b[index]+2*((x&1)*2+(y&1));
 }else if(df==9||df==0xb)at=2*(y*w+x);
 else if(df==0xd){unsigned s=w<h?w:h;at=2*((y/s*(w/s)+x/s)*s*s+reference_twiddle(x%s,y%s));}
 else return UINT32_MAX;
 return at>=size||size-at<2?UINT32_MAX:at;
}
static int compare(const unsigned char *b,unsigned size,unsigned pf,unsigned df,unsigned w,unsigned h)
{
 unsigned char *actual=jgr_pvr_decode_payload(b,size,pf,df,w,h);int valid=1;
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
  unsigned at=offset_for(b,size,df,w,h,x,y);
  if(at==UINT32_MAX){valid=0;goto done;}
 }
 assert(actual);
 for(unsigned y=0;y<h;y++)for(unsigned x=0;x<w;x++){
  unsigned at=offset_for(b,size,df,w,h,x,y);
  assert(actual);unsigned v=b[at]|((unsigned)b[at+1]<<8),r,g,blue,a;
  if(pf==0){r=(v>>10&31)*255/31;g=(v>>5&31)*255/31;blue=(v&31)*255/31;a=(v&32768)?255:0;}
  else if(pf==1){r=(v>>11&31)*255/31;g=(v>>5&63)*255/63;blue=(v&31)*255/31;a=255;}
  else{r=(v>>8&15)*17;g=(v>>4&15)*17;blue=(v&15)*17;a=(v>>12&15)*17;}
  unsigned char *p=actual+4*(y*w+x);assert(p[0]==r&&p[1]==g&&p[2]==blue&&p[3]==a);
 }
 done:assert((actual!=NULL)==valid);free(actual);return valid;
}
static unsigned required_bytes(unsigned df,unsigned w,unsigned h)
{
 if(df==1||df==2||df==0x12)return (df==1?0:reference_mip(w,0)+2)+2*reference_twiddle(w-1,h-1)+2;
 if(df==3||df==4||df==0x10||df==0x11)return entries_for(df,w)*8+((df==4||df==0x11)?reference_mip(w,1):0)+reference_twiddle((w-1)/2,(h-1)/2)+1;
 return 2*w*h;
}
int main(void)
{
 for(unsigned y=0;y<1024;y++)for(unsigned x=0;x<1024;x++)assert(twiddle(x,y)==reference_twiddle(x,y));
 assert(twiddle(UINT32_MAX,UINT32_MAX)==reference_twiddle(UINT32_MAX,UINT32_MAX));
 const unsigned formats[]={1,2,0x12,3,4,0x10,0x11,9,0xb,0xd};
 const unsigned bytes=3*1024*1024;unsigned char *payload=malloc(bytes);assert(payload);
 unsigned cases=0;
 for(unsigned k=0;k<sizeof formats/sizeof *formats;k++)for(unsigned w=1;w<=1024;w*=2){
  unsigned df=formats[k],entries=entries_for(df,w);int vq=df==3||df==4||df==0x10||df==0x11;
  for(unsigned i=0;i<bytes;i++){unsigned v=(i*37u+(i>>3)*17u+(i>>11))&255;payload[i]=(unsigned char)(vq&&i>=entries*8?v%entries:v);}
  for(unsigned h=1;h<=1024;h*=2)for(unsigned pf=0;pf<3;pf++){assert(compare(payload,bytes,pf,df,w,h));cases++;}
  /* Truncation, missing texel byte, or missing VQ codebook/index must fail. */
  assert(!jgr_pvr_decode_payload(payload,0,0,df,w,w));
  assert(!jgr_pvr_decode_payload(payload,1,0,df,w,w));
  if(vq&&entries<256){unsigned index=entries*8+((df==4||df==0x11)?reference_mip(w,1):0);unsigned char keep=payload[index];payload[index]=(unsigned char)entries;assert(!jgr_pvr_decode_payload(payload,bytes,0,df,w,w));payload[index]=keep;}
 }
 /* Exact last-byte bounds for tall/wide/single-pixel images. An unused Morton
    gap must not become a new rejection when validating whole blocks. */
 const unsigned dimensions[][2]={{1,1},{1,16},{16,1},{2,2},{4,32},{32,4},{16,16},{64,32},{32,64},{256,8},{8,256}};
 unsigned edge_cases=0;
 for(unsigned k=0;k<10;k++)for(unsigned d=0;d<sizeof dimensions/sizeof *dimensions;d++){
  unsigned df=formats[k],w=dimensions[d][0],h=dimensions[d][1],need=required_bytes(df,w,h),entries=entries_for(df,w);
  int vq=df==3||df==4||df==0x10||df==0x11;
  for(unsigned i=0;i<need;i++)payload[i]=(unsigned char)(vq&&i>=entries*8?i%entries:(i*1777u+(i>>2)));
  for(unsigned pf=0;pf<3;pf++){
   assert(compare(payload,need,pf,df,w,h));assert(!compare(payload,need-1,pf,df,w,h));edge_cases+=2;
  }
  if(w*h<=4)for(unsigned n=0;n<need;n++){assert(!compare(payload,n,0,df,w,h));edge_cases++;}
 }
 for(unsigned variant=0;variant<2;variant++){
  unsigned w=variant?8:1,h=variant?1:8,base=16*8,need=required_bytes(0x10,w,h);
  memset(payload,0,need);payload[base+(variant?1:2)]=16;
  assert(compare(payload,need,0,0x10,w,h));edge_cases++;
  payload[base+reference_twiddle((w-1)/2,(h-1)/2)]=16;
  assert(!compare(payload,need,0,0x10,w,h));edge_cases++;
 }
 fail_alloc=1;assert(!jgr_pvr_decode_payload(payload,bytes,0,1,16,16));fail_alloc=0;
 assert(!jgr_pvr_decode_payload(NULL,bytes,0,1,16,16));
 assert(!jgr_pvr_decode_payload(payload,bytes,3,1,16,16));
 assert(!jgr_pvr_decode_payload(payload,bytes,0,0,16,16));
 assert(!jgr_pvr_decode_payload(payload,bytes,0,1,0,16));
 assert(!jgr_pvr_decode_payload(payload,bytes,0,1,16,3));
 assert(!jgr_pvr_decode_payload(payload,bytes,0,1,2048,16));
 free(payload);printf("formats=10 sizes=121 colors=3 cases=%u twiddle_coordinates=1048576 edge_cases=%u\n",cases,edge_cases);return 0;
}
'''


class PvrDecode(unittest.TestCase):
    def test_reference_all_layouts_sizes_colors_and_bounds(self):
        if not shutil.which('cc'):
            self.skipTest('Host compiler unavailable')
        with tempfile.TemporaryDirectory(prefix='jgr-pvr-decode-') as tmp:
            work = Path(tmp)
            (work/'driver.c').write_text(DRIVER)
            exe = work/'decode'
            subprocess.run(['cc', '-std=c99', '-O2', '-ffp-contract=off',
                            '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
                            '-Wall', '-Wextra', '-Werror', '-I'+str(ROOT/'psp/src'),
                            str(work/'driver.c'), '-lm', '-o', str(exe)], check=True)
            result = subprocess.run([exe], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('cases=3630 twiddle_coordinates=1048576', result.stdout)

    def test_psp_compile(self):
        cc = os.environ.get('PSP_CC')
        if not cc:
            self.skipTest('Set PSP_CC')
        with tempfile.TemporaryDirectory(prefix='jgr-pvr-psp-') as tmp:
            subprocess.run([cc, '-std=c99', '-O2', '-Wall', '-Wextra', '-Werror',
                            '-I'+str(ROOT/'psp/src'), '-c', str(ROOT/'psp/src/portable_convert.c'),
                            '-o', str(Path(tmp)/'convert.o')], check=True)


if __name__ == '__main__':
    unittest.main()
