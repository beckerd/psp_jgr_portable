/* Included by the bounded bootstrap engine so private machine state does not
 * become a public API. No captured data or initialized guest memory is here. */
#include "portable_crc32.h"
#define STAFF_INPUT_WORDS (0xc60/4)
#define STAFF_INPUT_EVENTS 150000u
struct StaffReuse {
 FILE *stream;unsigned mode,return_pc,return_sp,actor,kind;
 unsigned events,samples,parts,camera,operation,finished;
 unsigned char state[0xc60],previous[0x900],initial[0x900],poses[8][0x400],output[0x1060];
 uint32_t pose_at[8],stack,pool_page,pool_index,model_collision,crc;
 PortableReplayVM saved;
 PortableReplayStaffPoseSample sample;void *context;
};
static void staff_put16(unsigned char*p,unsigned n){p[0]=(unsigned char)n;p[1]=(unsigned char)(n>>8);}
static void staff_put32(unsigned char*p,uint32_t n){staff_put16(p,n);staff_put16(p+2,n>>16);}
static unsigned staff_u16(const unsigned char*p){return (unsigned)p[0]|(unsigned)p[1]<<8;}
static uint32_t staff_u32(const unsigned char*p){return staff_u16(p)|(uint32_t)staff_u16(p+2)<<16;}
static uint32_t staff_crc(uint32_t crc,const unsigned char*p,unsigned n)
{return jgr_crc32_add(crc,p,n);}
static int staff_ram(Devices*d,unsigned at,unsigned bytes,unsigned char**p)
{
 at&=0x1fffffff;if(at<0x0c000000 || at>0x0d000000 || bytes>0x0d000000-at)return 0;
 *p=d->gd.ram+(at-0x0c000000);return 1;
}
static int staff_preserve(unsigned offset)
{
 return offset==0x7c || offset==0x88 || offset==0x8c || offset==0x4e0 ||
  (offset>=0x558 && offset<0x5ac) || (offset>=0x5f0 && offset<0x600) ||
  offset==0x674 || offset==0x874 || offset==0x878;
}
static int staff_event_write(struct StaffReuse*s,unsigned kind,const unsigned char*p,unsigned bytes)
{
 unsigned char encoded[4+STAFF_INPUT_WORDS*6];unsigned count=0;
 if(bytes>sizeof s->state || (bytes&3) || s->events>=STAFF_INPUT_EVENTS)return 0;
 for(unsigned at=0;at<bytes;at+=4)if(memcmp(s->state+at,p+at,4)){
  unsigned char *row=encoded+4+count++*6;staff_put16(row,at/4);memcpy(row+2,p+at,4);
 }
 staff_put16(encoded,kind);staff_put16(encoded+2,count);unsigned size=4+count*6;
 if(fwrite(encoded,1,size,s->stream)!=size)return 0;
 s->crc=staff_crc(s->crc,encoded,size);memcpy(s->state,p,bytes);s->events++;return 1;
}
static int staff_record_sample(Devices*d,const unsigned char*a,const unsigned char*c,const unsigned char*i)
{
 struct StaffReuse*s=d->staff_reuse;if(!s || s->mode!=1)return 1;
 if(!s->parts || s->parts>9 || s->samples>=30000)return 0;
 memcpy(s->output,a,0x900);memcpy(s->output+0x900,c,0x290);memcpy(s->output+0xb90,i,0xd0);
 s->samples++;return staff_event_write(s,5,s->output,0xc60);
}
static int staff_reuse_hook(Devices*d,PortableReplayVM*v)
{
 struct StaffReuse*s=d->staff_reuse;if(!s)return 0;unsigned pc=v->pc&0x1fffffff;
 if(s->mode==2 && d->requested && pc==0x0c03ed98 && (guest(d,v->r[4]+4)&65535)==0x6a){
  unsigned char*p;s->actor=v->r[4];if(!staff_ram(d,s->actor,0x900,&p))return -1;
  memcpy(s->initial,p,0x900);
  for(unsigned n=0;n<8;n++){
   s->pose_at[n]=guest(d,s->actor+0x58c+n*4);
   if(!staff_ram(d,s->pose_at[n],0x400,&p))return -1;
   memcpy(s->poses[n],p,0x400);
  }
  s->saved=*v;s->stack=v->r[15];s->pool_page=guest(d,0x8c2f63a4);s->pool_index=guest(d,0x8c2f63a8);
  s->model_collision=guest(d,guest(d,s->actor+0x5f4)+4);s->mode=3;v->pc=0xfffffff0;return 1;
 }
 if(s->mode!=1)return 0;
 if(s->return_pc && v->pc==s->return_pc && v->r[15]==s->return_sp){
  unsigned char*p;if(!staff_ram(d,s->actor,0x900,&p) || !staff_event_write(s,s->kind+1,p,0x900))return -1;
  s->return_pc=0;d->staff_watch=0;
 }
 if(d->requested && !s->return_pc && (pc==0x0c03ed98 || pc==0x0c051d48) && (guest(d,v->r[4]+4)&65535)==0x6a){
  unsigned char*p;s->actor=v->r[4];if(!staff_ram(d,s->actor,0x900,&p))return -1;
  s->return_pc=v->pr;d->staff_watch=v->pr;s->return_sp=v->r[15];s->kind=pc==0x0c03ed98?1:3;
  if(pc==0x0c03ed98 && v->pr==0x8c01964a){if(++s->parts>9 || !staff_event_write(s,6,p,0x900))return -1;}
  if(!staff_event_write(s,s->kind,p,0x900))return -1;
 }
 return 0;
}
static int staff_pose_hook(PortableReplayVM*v,void*context)
{
 (void)context;unsigned pc=v->pc&0x1fffffff;
 /* These calls only enqueue sound or world collision contacts. The original
  * pose driver and attachment traversal execute normally. Their frame-local
  * contact-pool cursors are reset for each attachment pass below. */
 if(pc==0x0c05cf14 || pc==0x0c05cf80 || pc==0x0c05cfd0 || pc==0x0c0b37e4 ||
    pc==0x0c0c4ab4 || pc==0x0c0c4e08 || pc==0x0c056efc){v->r[0]=0;v->pc=v->pr;return 1;}
 return 0;
}
static int staff_restore(Devices*d,struct StaffReuse*s)
{
 unsigned char*p;if(!staff_ram(d,s->actor,0x900,&p))return 0;memcpy(p,s->initial,0x900);
 for(unsigned n=0;n<8;n++){if(!staff_ram(d,s->pose_at[n],0x400,&p))return 0;memcpy(p,s->poses[n],0x400);}
 if(!portable_replay_vm_write(&d->vm,0x8c2f63a4,4,s->pool_page) ||
    !portable_replay_vm_write(&d->vm,0x8c2f63a8,4,s->pool_index) ||
    !portable_replay_vm_write(&d->vm,guest(d,s->actor+0x5f4)+4,4,s->model_collision))return 0;
 uint64_t instructions=d->vm.instructions;d->vm=s->saved;d->vm.instructions=instructions;
 d->last_instruction=instructions;d->hook_urgent=1;d->complete=1;d->stopped_pc=d->vm.pc;
 d->samples=s->samples;return 1;
}
static int staff_event_read(struct StaffReuse*s,unsigned*kind)
{
 unsigned char encoded[4+STAFF_INPUT_WORDS*6];
 if(fread(encoded,1,4,s->stream)!=4)return 0;
 unsigned type=staff_u16(encoded),count=staff_u16(encoded+2);
 if(!type){
  unsigned char end[20];if(count || fread(end,1,sizeof end,s->stream)!=sizeof end ||
    memcmp(end,"END1",4) || staff_u32(end+4)!=s->events || staff_u32(end+8)!=s->samples ||
    staff_u32(end+12)!=s->parts || staff_u32(end+16)!=s->crc || s->parts!=9 || !s->samples ||
    fgetc(s->stream)!=EOF || ferror(s->stream))return 0;
  *kind=0;return 1;
 }
 unsigned words=type==5?STAFF_INPUT_WORDS:0x900/4;
 if(type>6 || count>words || s->events>=STAFF_INPUT_EVENTS || fread(encoded+4,6,count,s->stream)!=count)return 0;
 unsigned prior=0;
 for(unsigned n=0;n<count;n++){
  const unsigned char*row=encoded+4+n*6;unsigned at=staff_u16(row);
  if(at>=words || (n && at<=prior))return 0;
  prior=at;memcpy(s->state+at*4,row+2,4);
 }
 s->crc=staff_crc(s->crc,encoded,4+6*count);s->events++;*kind=type;return 1;
}
static int staff_pose_step(Devices*d,unsigned budget)
{
 struct StaffReuse*s=d->staff_reuse;unsigned char*actor;
 if(!s || s->mode!=3 || !staff_ram(d,s->actor,0x900,&actor))return -1;
 uint64_t start=d->vm.instructions;unsigned events=0;d->vm.hook=staff_pose_hook;
 while(d->vm.instructions-start<budget && events++<256){
  if(s->operation){
   unsigned left=budget-(unsigned)(d->vm.instructions-start);
   int ok=portable_replay_vm_continue(&d->vm,0xfffffff0,left);
   if(!ok)return d->vm.fault==PRVM_BUDGET?0:-1;
   s->operation=0;
  }
  if(d->vm.instructions-start>=budget)return 0;
  unsigned type;if(!staff_event_read(s,&type))return -1;
  if(!type){if(!staff_restore(d,s))return -1;s->finished=1;s->mode=4;return 1;}
  if(type==6){
   if(++s->parts>9)return -1;
   memcpy(actor,s->initial,0x900);
   for(unsigned n=0;n<8;n++){unsigned char*p;if(!staff_ram(d,s->pose_at[n],0x400,&p))return -1;memcpy(p,s->poses[n],0x400);}
   for(unsigned at=0;at<0x900;at+=4)if(!staff_preserve(at))memcpy(actor+at,s->state+at,4);
   memcpy(s->previous,s->state,0x900);continue;
  }
  if(type==2 || type==4){memcpy(s->previous,s->state,0x900);continue;}
  if(type==1 || type==3){
   for(unsigned at=0;at<0x900;at+=4){
    if(staff_preserve(at))continue;
    if((at>=0x5ac && at<0x62c) || (at>=0x678 && at<0x684)){
     if(memcmp(s->state+at,s->previous+at,4))memcpy(actor+at,s->state+at,4);
    }else memcpy(actor+at,s->state+at,4);
   }
   if(type==3 && (!portable_replay_vm_write(&d->vm,0x8c2f63a4,4,0) || !portable_replay_vm_write(&d->vm,0x8c2f63a8,4,0)))return -1;
   d->vm.r[4]=s->actor;d->vm.r[15]=s->stack;s->operation=type;
   unsigned left=budget-(unsigned)(d->vm.instructions-start);
   int ok=portable_replay_vm_run(&d->vm,type==1?0x8c03ed98:0x8c051d48,0xfffffff0,left);
   if(!ok)return d->vm.fault==PRVM_BUDGET?0:-1;
   s->operation=0;continue;
  }
  if(type==5){
   if(!s->parts || s->samples>=30000)return -1;
   unsigned char*out=s->output;memcpy(out,s->state,0xc60);
   static const unsigned fields[]={0x7c,0x560,0x57c,0x580,0x594,0x5ac,0x5cc};
   for(unsigned n=0;n<sizeof fields/sizeof *fields;n++)memcpy(out+fields[n],actor+fields[n],4);
   memcpy(out+0x678,actor+0x678,12+32*12);
   unsigned char*p;if(!staff_ram(d,staff_u32(actor+0x594),0x400,&p))return -1;memcpy(out+0xc60,p,0x400);
   unsigned camera=staff_u32(out+0x900+0x274),cut=!s->samples || staff_u32(out+0x98)==1 || camera!=s->camera;
   unsigned variant=staff_u32(actor+0x560)==staff_u32(actor+0x580);
   if(!s->sample(s->context,out,out+0x900,out+0xb90,out+0xc60,s->parts-1,cut,variant))return -1;
   s->camera=camera;s->samples++;d->samples=s->samples;
  }
 }
 return 0;
}
static void staff_reuse_release(Devices*d)
{if(d){free(d->staff_reuse);d->staff_reuse=NULL;d->staff_watch=0;}}
int portable_replay_bootstrap_staff_record(Devices*d,FILE*stream)
{
 if(!d || !stream || d->staff_reuse || !d->active || d->complete || d->samples ||
    d->selection.kind!=PORTABLE_REPLAY_STAFF || d->selection.character)return 0;
 struct StaffReuse*s=calloc(1,sizeof *s);if(!s)return 0;
 unsigned char header[16]={'S','W','I','1',1,0,0,0,0x60,0x0c,0,0,0,0,0,0};
 if(fwrite(header,1,sizeof header,stream)!=sizeof header){free(s);return 0;}
 s->stream=stream;s->mode=1;s->crc=UINT32_MAX;d->staff_reuse=s;return 1;
}
int portable_replay_bootstrap_staff_record_finish(Devices*d)
{
 if(!d || !d->staff_reuse)return 0;
 struct StaffReuse*s=d->staff_reuse;
 if(s->mode!=1 || !d->complete || s->parts!=9 || !s->samples || s->return_pc)return 0;
 unsigned char end[24]={0};memcpy(end+4,"END1",4);staff_put32(end+8,s->events);staff_put32(end+12,s->samples);
 staff_put32(end+16,s->parts);staff_put32(end+20,s->crc);
 int ok=fwrite(end,1,sizeof end,s->stream)==sizeof end && !fflush(s->stream);
 staff_reuse_release(d);return ok;
}
static int staff_no_sample(void*user,const unsigned char*a,const unsigned char*c,const unsigned char*i,const unsigned char*p)
{(void)user;(void)a;(void)c;(void)i;(void)p;return 0;}
int portable_replay_bootstrap_staff_poses(Devices*d,FILE*stream,unsigned character,PortableReplayStaffPoseSample sample,void*context)
{
 if(!d || !stream || !sample || character>=16 || (d->staff_reuse && d->staff_reuse->mode!=4))return 0;
 staff_reuse_release(d);if(fseek(stream,0,SEEK_SET))return 0;
 unsigned char header[16];
 if(fread(header,1,sizeof header,stream)!=sizeof header || memcmp(header,"SWI1",4) ||
    staff_u32(header+4)!=1 || staff_u32(header+8)!=0xc60 || staff_u32(header+12))return 0;
 struct StaffReuse*s=calloc(1,sizeof *s);if(!s)return 0;
 PortableReplaySelection selection={1,0,0,0,character,PORTABLE_REPLAY_STAFF};
 if(!portable_replay_bootstrap_select(d,&selection,staff_no_sample,NULL)){free(s);return 0;}
 s->stream=stream;s->mode=2;s->crc=UINT32_MAX;s->sample=sample;s->context=context;d->staff_reuse=s;
 portable_replay_bootstrap_frame_observer(d,NULL,NULL);return 1;
}
