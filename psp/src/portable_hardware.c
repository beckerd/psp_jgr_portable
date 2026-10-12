/* Install-time serialization of generic hardware model tables.
 * Original MANATEE pitch bytes are read only from the user's disc.
 * Hardware constants retain their upstream GPL-2.0-or-later license;
 * see portable_hardware_{fsca,aica}.inc and the corresponding notices. */
#include "portable_hardware.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
static const uint32_t fsca[32768]={
#include "portable_hardware_fsca.inc"
};
#include "portable_hardware_aica.inc"
const uint32_t *jgr_hardware_fsca(void){return fsca;}
static void word(unsigned char *p,uint32_t v)
{p[0]=v;p[1]=v>>8;p[2]=v>>16;p[3]=v>>24;}
static uint32_t crc(const unsigned char *p,unsigned n)
{
 uint32_t c=~0u;
 while(n--){c^=*p++;for(unsigned i=0;i<8;i++)c=(c>>1)^((0u-(c&1u))&0xedb88320u);}
 return ~c;
}
static void section(unsigned char *b,unsigned row,uint32_t id,unsigned off,unsigned size)
{unsigned char *p=b+32+row*16;word(p,id);word(p+4,off);word(p+8,size);word(p+12,16);}
static int emit(FILE *out,unsigned char *b,unsigned count,unsigned size)
{
 unsigned header=32+count*16;
 memcpy(b,"JGSD",4);word(b+4,1);word(b+8,count);word(b+12,size);
 word(b+16,crc(b+32,count*16));word(b+20,crc(b+header,size-header));word(b+24,header);
 return fwrite(b,1,size,out)==size?0:-1;
}
int jgr_hardware_known(const char *name)
{return name&&(!strcmp(name,"source_data_player_fsca.jsd")||!strcmp(name,"source_data_smsb_tables.jsd"));}
int jgr_hardware_convert(JgrDisc *d,const char *name,FILE *out)
{
 if(!out||!jgr_hardware_known(name))return -1;
 unsigned sine=!strcmp(name,"source_data_player_fsca.jsd"),size=sine?131120:2384;
 unsigned char *b=calloc(1,size);if(!b)return -1;
 int result=-1;
 if(sine){
  section(b,0,3791995794u,48,131072);
  for(unsigned i=0;i<32768;i++)word(b+48+4*i,fsca[i]);
 }else{
  JgrDiscFile driver;
  if(!d||jgr_disc_find(d,"JETRADIO/MANATEE.DRV",&driver)||
     jgr_disc_read(d,&driver,32+0x4210,b+96,228)||
     jgr_disc_read(d,&driver,32+0x42f4,b+336,1536))goto done;
  section(b,0,2505759850u,96,228);section(b,1,2870355212u,336,1536);
  section(b,2,518474426u,1872,256);section(b,3,3374969356u,2128,256);
  for(unsigned i=0;i<64;i++){
   /* Preserve the established binary32 rounding of the hardware timings. */
   double a=(float)AEG_Attack_Time[i],v=(float)AEG_DSR_Time[i];
   uint32_t attack=a<0?0:a==0?65536u:(uint32_t)floor(65536.0/(1.0-1.0/pow(640.0,1.0/(44.1*a)))+.5);
   uint32_t dsr=v<0?0:v==0?67108863u:(uint32_t)floor(67108863.0/(44.1*v)+.5);
   word(b+1872+i*4,attack);word(b+2128+i*4,dsr);
  }
 }
 result=emit(out,b,sine?1:4,size);
 done:free(b);return result;
}
