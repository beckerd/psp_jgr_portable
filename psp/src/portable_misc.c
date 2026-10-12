/* Small first-launch formats decoded from the supported original disc. */
#include "portable_misc.h"
#include "portable_convert.h"
#include "portable_models.h"
#include "portable_hardware.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#define EXE_BASE 0x8c010000u
#define LAST_BASE 0x8cb00000u

typedef struct {JgrDisc *disc;JgrDiscFile exe,data;uint32_t base;int failed;} Misc;
static uint32_t mu32(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static unsigned mu16(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static void mput(unsigned char *p,uint32_t n){for(unsigned i=0;i<4;i++)p[i]=n>>(8*i);}
static int mword(FILE *out,uint32_t n){unsigned char b[4];mput(b,n);return fwrite(b,1,4,out)!=4?-1:0;}
static int mread(Misc *m,uint32_t address,void *out,unsigned bytes)
{
 const JgrDiscFile *f=address>=m->base?&m->data:&m->exe;unsigned base=address>=m->base?m->base:EXE_BASE;
 if(m->failed||address<base||address-base>f->size||bytes>f->size-(address-base)||jgr_disc_read(m->disc,f,address-base,out,bytes)){m->failed=1;return -1;}return 0;
}
static uint32_t mw(Misc *m,uint32_t a){unsigned char b[4];return mread(m,a,b,4)?0:mu32(b);}
static int mcopy(Misc *m,uint32_t at,unsigned bytes,FILE *out)
{unsigned char buffer[4096];while(bytes){unsigned n=bytes<sizeof buffer?bytes:sizeof buffer;if(mread(m,at,buffer,n)||fwrite(buffer,1,n,out)!=n)return -1;at+=n;bytes-=n;}return 0;}
static int open_misc(Misc *m,JgrDisc *d,const char *source,unsigned base)
{memset(m,0,sizeof *m);m->disc=d;m->base=base;return jgr_disc_find(d,"1ST_READ.BIN",&m->exe)||jgr_disc_source(d,source,&m->data)?-1:0;}
static int texture_pixels(JgrDisc *d,const JgrDiscFile *f,unsigned at,unsigned char **pixels,unsigned *width,unsigned *height)
{
 unsigned char head[128];unsigned n=f->size>at?f->size-at:0;if(n>sizeof head)n=sizeof head;
 if(n<16||jgr_disc_read(d,f,at,head,n))return -1;
 unsigned off=0;
 while(off+16<=n&&memcmp(head+off,"PVRT",4))off++;
 if(off+16>n)return -1;
 unsigned size=mu32(head+off+4);if(size<8||size>4*1024*1024)return -1;size-=8;
 if(size>f->size-at-off-16)size=f->size-at-off-16;
 unsigned char *raw=malloc(size?size:1);if(!raw)return -1;
 *width=mu16(head+off+12);*height=mu16(head+off+14);
 int r=jgr_disc_read(d,f,at+off+16,raw,size);*pixels=r?NULL:jgr_pvr_decode_payload(raw,size,head[off+8],head[off+9],*width,*height);free(raw);return *pixels?0:-1;
}
static int rgba16(FILE *out,const unsigned char *pixels,unsigned count,int reverse)
{unsigned char buffer[2048];while(count){unsigned n=count<1024?count:1024;for(unsigned i=0;i<n;i++){
 unsigned r=pixels[4*i]>>4,g=pixels[4*i+1]>>4,b=pixels[4*i+2]>>4,a=pixels[4*i+3]>>4;
 unsigned v=reverse?(a<<12|r<<8|g<<4|b):(a<<12|b<<8|g<<4|r);buffer[i*2]=v;buffer[i*2+1]=v>>8;
 }if(fwrite(buffer,2,n,out)!=n)return -1;pixels+=n*4;count-=n;}return 0;}
static int hud_texture(JgrDisc *d,const char *name,FILE *out)
{
 Misc m;JgrDiscFile texture;const char *source=!strcmp(name,"gamehud.bin")?"decompile/afs/U_GAME/001_gamedisptxr.bin":!strcmp(name,"spraydigits.bin")?"decompile/afs/E_GAME/001_gamedisptxr.bin":"decompile/afs/ENEMY/034_tabantxr.bin";
 if(open_misc(&m,d,source,0x8c800000)||jgr_disc_source(d,source,&texture))return -1;
 unsigned count=!strcmp(name,"gamehud.bin")?2:1;if(fwrite("TEX1",1,4,out)!=4||mword(out,count))return -1;
 for(unsigned i=0;i<count;i++){
  unsigned at=!strcmp(name,"tabanfx.bin")?23936:mw(&m,0x8c177fc4+4*(!strcmp(name,"gamehud.bin")?i+1:2))-0x8c800000;
  unsigned char *pixels=NULL,dimensions[4];unsigned w,h;if(m.failed||texture_pixels(d,&texture,at,&pixels,&w,&h))return -1;
  dimensions[0]=w;dimensions[1]=w>>8;dimensions[2]=h;dimensions[3]=h>>8;
  int r=fwrite(dimensions,1,4,out)!=4||rgba16(out,pixels,w*h,0);free(pixels);if(r)return -1;
 }return 0;
}
static int final_mask(JgrDisc *d,unsigned index,FILE *out)
{
 static const char *names[]={"000_e_grfm01.bin","001_e_grfm02.bin","002_e_grfm03.bin","003_e_grfm05.bin","004_e_postm.bin"};
 if(index>4)return -1;
 char source[80];snprintf(source,sizeof source,"decompile/afs/U_GRF/%s",names[index]);
 JgrDiscFile f;if(index>4||jgr_disc_source(d,source,&f)||f.size>1024*1024)return -1;
 unsigned char *raw=malloc(f.size?f.size:1),*pixels=NULL;if(!raw)return -1;int r=-1;
 if(jgr_disc_read(d,&f,0,raw,f.size))goto done;
 pixels=jgr_pvr_decode_payload(raw,f.size,2,4,256,256);if(pixels)r=rgba16(out,pixels,256*256,1);
 done:free(raw);free(pixels);return r;
}
static int node_count(const JgrModelNode *node,const double matrix[16],unsigned ordinal,int parent,void *user)
{(void)node;(void)matrix;(void)ordinal;(void)parent;unsigned *count=user;return ++*count<=64?0:-1;}
static int hud_motion(JgrDisc *d,const char *name,FILE *out)
{
 static const uint32_t cells[16][3]={{0x78,0xb4,0},{0x84,0xac,0},{0x94,0xd8,0},{0x98,0xd4,0},{0x90,0xcc,0x100},{0x7c,0xb8,0xf0},{0x9c,0xe8,0x104},{0x70,0xac,0},{0xa4,0xe0,0},{0xa8,0xe4,0x10c},{0x88,0xc4,0xf8},{0x74,0xb0,0xec},{0xa0,0xdc,0x108},{0x8c,0xc8,0xfc},{0x80,0xbc,0xf4},{0x1dc,0x1e0,0}};
 static const uint32_t police[4][2]={{0x54,0x58},{0x60,0x64},{0x68,0x6c},{0x48,0x4c}};
 unsigned failure=!strcmp(name,"failurehud.bin"),count=failure?16:4;uint32_t rows[16][3],first=UINT32_MAX,last=0;Misc m;
 if(open_misc(&m,d,"decompile/afs/U_GAME/000_gamedispdat.bin",0x8c980000))return -1;
 for(unsigned i=0;i<count;i++){
  rows[i][0]=mw(&m,m.base+(failure?cells[i][0]:police[i][0]));double identity[16];jgr_model_identity(identity);unsigned nodes=0;
  if(m.failed||jgr_model_walk(d,&m.data,m.base,rows[i][0],identity,0,1,node_count,&nodes))return -1;
  for(unsigned ch=0;ch<2;ch++){
   unsigned cell=failure?cells[i][ch+1]:ch?0:police[i][1];rows[i][ch+1]=cell?mw(&m,m.base+cell):0;unsigned motion=rows[i][ch+1];if(!motion)continue;
   unsigned md=mw(&m,motion);if(mw(&m,motion+8)!=0x30007)return -1;
   if(motion<first)first=motion;
   if(motion+12>last)last=motion+12;
   if(md<first)first=md;
   if(md+nodes*24>last)last=md+nodes*24;
   for(unsigned b=0;b<nodes;b++)for(unsigned k=0;k<3;k++){unsigned p=mw(&m,md+b*24+k*4),n=mw(&m,md+b*24+12+k*4);if(p){if(n>65536||p>UINT32_MAX-n*16)return -1;if(p<first)first=p;if(p+n*16>last)last=p+n*16;}}
  }
 }
 if(m.failed||first<m.base||last<first||last-m.base>m.data.size)return -1;
 if(fwrite(failure?"FHM1":"PHM2",1,4,out)!=4||mword(out,first)||mword(out,last-first))return -1;
 if(failure){if(mword(out,count))return -1;for(unsigned i=0;i<count;i++)for(unsigned k=0;k<3;k++)if(mword(out,rows[i][k]))return -1;}
 else for(unsigned i=0;i<count;i++)if(mword(out,rows[i][1]))return -1;
 return mcopy(&m,first,last-first,out);
}
#include "portable_misc_final.inc"
#include "portable_misc_extra.inc"
int jgr_misc_known(const char *name)
{
 static const char *names[]={"gamehud.bin","spraydigits.bin","tabanfx.bin","failurehud.bin","policehud.bin","finaltagmask.bin","finalcoll.bin","finalbosscoll.bin","finalcollapse.bin","garageshadow.bin","skatetrails.bin","enemyaudio.bin","rivaltag0.bin","rivaltag1.bin","rivaltag2.bin","rivaltag3.bin","rivaltag4.bin"};
 if(!name)return 0;
 if(jgr_hardware_known(name))return 1;
 for(unsigned i=0;i<sizeof names/sizeof names[0];i++)if(!strcmp(name,names[i]))return 1;
 return 0;
}
int jgr_misc_convert(JgrDisc *d,const char *name,FILE *out)
{
 if(!d||!out||!jgr_misc_known(name))return -1;
 if(!strcmp(name,"gamehud.bin")||!strcmp(name,"spraydigits.bin")||!strcmp(name,"tabanfx.bin"))return hud_texture(d,name,out);
 if(!strcmp(name,"failurehud.bin")||!strcmp(name,"policehud.bin"))return hud_motion(d,name,out);
 if(!strcmp(name,"finaltagmask.bin"))return final_mask(d,0,out);
 if(!strncmp(name,"rivaltag",8))return final_mask(d,(unsigned)(name[8]-'0'),out);
 if(jgr_hardware_known(name))return jgr_hardware_convert(d,name,out);
 if(!strcmp(name,"garageshadow.bin"))return garage_shadow(d,out);
 if(!strcmp(name,"skatetrails.bin"))return skate_trails(d,out);
 if(!strcmp(name,"enemyaudio.bin"))return enemy_audio(d,out);
 return final_collision(d,name,out);
}
