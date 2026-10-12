#include "portable_replay_demo.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define MAX_AUDIO 4096u
#define MAX_SPRAY 1024u
typedef struct {unsigned tick,order,kind;int program;} Audio;
struct JgrReplayDemo {
 unsigned owner[2],nstops,nsprays,failed,last_tick;
 Audio stops[MAX_AUDIO];
 unsigned char sprays[MAX_SPRAY][24];
};
static uint32_t word(const unsigned char *p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put(unsigned char *p,uint32_t n){p[0]=n;p[1]=n>>8;p[2]=n>>16;p[3]=n>>24;}
static void half(unsigned char *p,unsigned n){p[0]=n;p[1]=n>>8;}
static float value(const unsigned char *p){uint32_t n=word(p);float v;memcpy(&v,&n,4);return v;}
static int finite_value(const unsigned char *p){return (word(p)&0x7f800000u)!=0x7f800000u;}
JgrReplayDemo *jgr_replay_demo_create(void){return calloc(1,sizeof(JgrReplayDemo));}
int jgr_replay_demo_sample(JgrReplayDemo *d,const unsigned char a[0x900])
{
 if(!d||!a||d->failed)return -1;
 unsigned tick=word(a+0x98),active=word(a+0x294),ref=word(a+0x2b8),variant=word(a+0x560)==word(a+0x580);
 if(!tick||tick>=65536||active>1||ref>=65536||(d->last_tick&&tick!=d->last_tick+1))goto fail;
 if(active&&(ref<160||ref>166))goto fail;
 for(unsigned i=0;i<2;i++){
  unsigned owner=word(a+0x874+i*4);
  if(d->owner[i]&&!owner){if(d->nstops==MAX_AUDIO)goto fail;d->stops[d->nstops++]=(Audio){tick,10000+i,3+i,-1};}
  d->owner[i]=owner;
 }
 unsigned char cue[24]={0};half(cue,tick);half(cue+2,ref);cue[4]=active;cue[5]=variant;
 memcpy(cue+8,a+0x29c,16);
 for(unsigned i=0;i<4;i++)if(!finite_value(cue+8+i*4))goto fail;
 unsigned same=0;
 if(d->nsprays){
  unsigned char *prior=d->sprays[d->nsprays-1];unsigned before=prior[0]|(unsigned)prior[1]<<8;
  same=!memcmp(cue+2,prior+2,4);
  for(unsigned i=0;i<3;i++)if(value(cue+12+i*4)!=value(prior+12+i*4))same=0;
  float expected=(float)((double)value(prior+8)+(active?tick-before:0));uint32_t bits;memcpy(&bits,&expected,4);
  if(word(cue+8)!=bits)same=0;
 }
 if(!same){if(d->nsprays==MAX_SPRAY)goto fail;memcpy(d->sprays[d->nsprays++],cue,24);}
 d->last_tick=tick;return 0;
 fail:d->failed=1;return -1;
}
static int audio_order(const void *left,const void *right)
{
 const Audio *a=left,*b=right;
 if(a->tick!=b->tick)return a->tick<b->tick?-1:1;
 return a->order<b->order?-1:a->order>b->order;
}
int jgr_replay_demo_finish(JgrReplayDemo *d,const PortableReplayCue *c,unsigned count,FILE *out)
{
 if(!d||d->failed||!out||!d->last_tick||!d->nsprays||count>MAX_AUDIO-d->nstops||(count&&!c))return -1;
 unsigned total=count+d->nstops;Audio *events=malloc((total?total:1)*sizeof *events);if(!events)return -1;
 int result=-1;unsigned active[2]={0,0},parent=count?c[0].parent:0;
 for(unsigned i=0;i<count;i++){
  if(c[i].tick>65535||c[i].tick>d->last_tick||c[i].kind<1||c[i].kind>4||c[i].bank!=(c[i].kind==1?7u:4u)||c[i].program>127||c[i].parent!=parent)goto done;
  events[i]=(Audio){c[i].tick,i,c[i].kind,(int)c[i].program};
 }
 memcpy(events+count,d->stops,d->nstops*sizeof *events);qsort(events,total,sizeof *events,audio_order);
 for(unsigned i=0;i<total;i++)if(events[i].kind>=3){
  unsigned k=events[i].kind-3;
  if(active[k]!=(events[i].program<0))goto done;
  active[k]=events[i].program>=0;
 }
 if(active[0]||active[1])goto done;
 unsigned char header[16]={0},row[4];memcpy(header,"RDC1",4);put(header+4,total);put(header+8,d->nsprays);put(header+12,d->last_tick);
 if(fwrite(header,1,16,out)!=16)goto done;
 for(unsigned i=0;i<total;i++){half(row,events[i].tick);row[2]=events[i].kind;row[3]=(unsigned char)events[i].program;if(fwrite(row,1,4,out)!=4)goto done;}
 if(fwrite(d->sprays,24,d->nsprays,out)!=d->nsprays)goto done;
 result=0;
 done:free(events);return result;
}
void jgr_replay_demo_destroy(JgrReplayDemo *d){free(d);}
