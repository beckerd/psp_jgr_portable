#define JGR_PORTABLE_ASSETS_IMPLEMENTATION 1
#include "portable_assets.h"
#undef fopen
#undef stat
#include "portable_mission_cast.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
static unsigned word(const unsigned char*p){return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
typedef struct{FILE*f;unsigned size,at;} Input;
static FILE *directory_open(void *opaque,const char *name)
{
 const char *root=opaque;char path[1024];if(!root||snprintf(path,sizeof path,"%s/%s",root,name)>=(int)sizeof path)return NULL;struct stat st;
#ifdef __PSP__
 int status=stat(path,&st);
#else
 int status=lstat(path,&st);
#endif
 if(status||!S_ISREG(st.st_mode)||st.st_size<0||(uint64_t)st.st_size>16*1024*1024)return NULL;
 return fopen(path,"rb");
}
static int open_input(Input *i,FILE *(*open)(void *,const char *),void *user,const char *name)
{
 memset(i,0,sizeof *i);if(!open)return -1;i->f=open(user,name);if(!i->f)return -1;
 if(fseek(i->f,0,SEEK_END)){fclose(i->f);i->f=NULL;return -1;}long bytes=ftell(i->f);
 if(bytes<0||(unsigned long)bytes>16*1024*1024||fseek(i->f,0,SEEK_SET)){fclose(i->f);i->f=NULL;return -1;}
 i->size=(unsigned)bytes;return 0;
}
static int read_bytes(Input*i,void*out,unsigned n){if(n>i->size-i->at||fread(out,1,n,i->f)!=n)return -1;
 i->at+=n;return 0;}
static int skip(Input*i,unsigned n){if(n>i->size-i->at||fseek(i->f,n,SEEK_CUR))return -1;
 i->at+=n;return 0;}
static int u32(Input*i,unsigned*out){unsigned char b[4];if(read_bytes(i,b,4))return -1;
 *out=word(b);return 0;}
static int header(Input*i,const char*magic,unsigned*count){unsigned char b[8];if(read_bytes(i,b,8)||memcmp(b,magic,4))return -1;
 *count=word(b+4);return *count>8192?-1:0;}
static int finish(Input*i,int result){if(!result&&i->at!=i->size)result=-1;if(fclose(i->f))result=-1;i->f=NULL;return result;}
static int player_map(JgrDisc*d,unsigned map[16])
{
 JgrDiscFile afs,file;unsigned char b[12];if(jgr_disc_find(d,"JETRADIO/PLAYER.AFS",&afs)||jgr_disc_read(d,&afs,0,b,8))return -1;
 unsigned n=word(b+4),found=0,seen=0;if(n>512)return -1;
 for(unsigned member=0;member<n;member++){
  if(jgr_disc_afs(d,&afs,member,&file)||file.size<12||jgr_disc_read(d,&file,0,b,12))return -1;
 if(memcmp(b,"PLAY",4))continue;
 unsigned table=word(b+8);
  if(jgr_disc_read(d,&file,table,b,4))return -1;
 unsigned character=word(b);if(character>=16||(seen&(1u<<character))||found>=16)return -1;
 map[character]=found++;seen|=1u<<character;
 }
 return seen==65535?0:-1;
}
static int controllers(Input*i,const unsigned models[16],uint64_t*mask)
{
 unsigned chains;if(header(i,"CTL2",&chains)||chains>64)return -1;
 unsigned char*used=calloc(8192,1);if(!used)return -1;
 int result=-1;
 for(unsigned chain=0;chain<chains;chain++){
  memset(used,0,8192);unsigned n;if(u32(i,&n)||n>8192)goto done;
  for(unsigned j=0;j<n;j++){unsigned rows;if(u32(i,&rows)||rows>8192)goto done;
 for(unsigned k=0;k<rows;k++){unsigned ch;if(u32(i,&ch)||ch>=8192||skip(i,8))goto done;
 used[ch]=1;}}
  if(u32(i,&n)||n>8192)goto done;
 for(unsigned j=0;j<n;j++){unsigned char b[36];if(read_bytes(i,b,36))goto done;
 unsigned rows=word(b+32);if(rows>8192||skip(i,rows*4))goto done;
 if(used[j])*mask|=UINT64_C(1)<<models[word(b)&15];}
  if(u32(i,&n)||n>8192)goto done;
 for(unsigned j=0;j<n;j++){unsigned char b[20];if(read_bytes(i,b,20))goto done;
 unsigned a=word(b+12),count=word(b+16);if(a>8192||count>8192||skip(i,(a+count)*144))goto done;}
 }
 result=0;
done:free(used);return result;
}
static int groups(Input*i,uint64_t*mask)
{
 /* These are the port's model IDs selected by ordinary native enemy dispatch;
  * every actual group/member/pose is read from the generated encounter bank. */
 static const unsigned model[33]={16,17,18,30,48,49,31,27,46,50,29,26,34,35,36,37,38,39,42,43,44,40,41,32,28,33,16,17,27,30,29,22,37};
 unsigned chains;if(header(i,"GRP1",&chains)||chains>64)return -1;
 for(unsigned chain=0;chain<chains;chain++){
  unsigned n;if(u32(i,&n)||n>8192)return -1;
 for(unsigned j=0;j<n;j++){
   unsigned char b[44];if(read_bytes(i,b,44))return -1;
 unsigned kind=word(b),count=word(b+40);if(kind>=33||count>8192)return -1;
   if(count&&(kind<26||kind>30))*mask|=UINT64_C(1)<<model[kind];
   for(unsigned member=0;member<count;member++){
    if(read_bytes(i,b,28))return -1;
 unsigned pose=word(b+24);if(!member||kind<26||kind>30)continue;
    int selected=kind==26?(pose?26:16):kind==27?(pose?28:17):kind==28?27:kind==29?(pose?-1:30):29;
    if(selected>=0)*mask|=UINT64_C(1)<<(unsigned)selected;
   }
  }
 }
 return 0;
}
static int waves(Input*i,uint64_t*mask)
{unsigned chains;if(header(i,"WAV1",&chains)||chains>64)return -1;
 for(unsigned chain=0;chain<chains;chain++){unsigned n;if(u32(i,&n)||n>8192||skip(i,n*68))return -1;
 if(n)*mask|=UINT64_C(1)<<16;}return 0;}
static int pedestrians(Input*i,uint64_t*mask)
{unsigned n;if(header(i,"PED3",&n))return -1;
 for(unsigned j=0;j<n;j++){unsigned char b[28];if(read_bytes(i,b,28))return -1;
 unsigned type=word(b),count=word(b+4);if(count>8192||skip(i,count*12))return -1;
 *mask|=UINT64_C(1)<<((type>=43&&type<=46)?16:19+type%3);}return 0;}
int jgr_mission_cast_checked(JgrDisc*d,FILE *(*open)(void *,const char *),void *user,FILE*out)
{
 unsigned models[16];uint64_t masks[60]={0},valid=0;Input input;int result=-1;unsigned count;
 if(!d||!open||!out||player_map(d,models)||open_input(&input,open,user,"missions.bin"))return -1;
 if(header(&input,"MIS3",&count)||count>512)goto mission_done;
 for(unsigned j=0;j<count;j++){
  unsigned char b[88];if(input.size<8+count*88||fseek(input.f,8+j*88,SEEK_SET)||fread(b,1,88,input.f)!=88)goto mission_done;
  unsigned bank=word(b+8),blob=word(b+60),record=word(b+68),mode=word(b+4);if(bank>=60||blob>input.size||record>input.size-blob||input.size-blob-record<36)goto mission_done;
  if(fseek(input.f,blob+record+32,SEEK_SET)||fread(b,1,4,input.f)!=4)goto mission_done;
 unsigned character=word(b);valid|=UINT64_C(1)<<bank;
  if(character>=1&&character<=13)masks[bank]|=UINT64_C(1)<<models[character-1];else if(character==14)masks[bank]|=UINT64_C(1)<<models[15];
  if(mode==15)masks[bank]|=UINT64_C(1)<<models[3];
 }
 result=0;
mission_done:fclose(input.f);if(result)return -1;
 for(unsigned bank=0;bank<60;bank++)if(valid&(UINT64_C(1)<<bank))for(unsigned kind=0;kind<4;kind++){
  const char*name=kind==0?"ctrl":kind==1?"groups":kind==2?"waves":"peds";char file[40];snprintf(file,sizeof file,"set%u_%s.bin",bank,name);if(open_input(&input,open,user,file))return -1;
  int r=kind==0?controllers(&input,models,&masks[bank]):kind==1?groups(&input,&masks[bank]):kind==2?waves(&input,&masks[bank]):pedestrians(&input,&masks[bank]);if(finish(&input,r))return -1;
 }
 if(open_input(&input,open,user,"title_route.bin"))return -1;
 unsigned char title[12];int r=read_bytes(&input,title,12);fclose(input.f);if(r||word(title+8)>=16)return -1;
 unsigned char b[8];for(unsigned i=0;i<61;i++){uint64_t value=i<60?masks[i]:valid;for(unsigned k=0;k<8;k++)b[k]=(unsigned char)(value>>(8*k));if(fwrite(b,1,8,out)!=8)return -1;}
 unsigned selected=models[word(title+8)];for(unsigned k=0;k<4;k++)b[k]=(unsigned char)(selected>>(8*k));return fwrite(b,1,4,out)==4?0:-1;
}

int jgr_mission_cast(JgrDisc *disc,const char *directory,FILE *output)
{return jgr_mission_cast_checked(disc,directory_open,(void *)directory,output);}
