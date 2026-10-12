/* First-install replay session. Original execution memory is destroyed before
 * gameplay; subsequent launches use only catalog-verified generated assets. */
#include "portable_replay_session.h"
#include "portable_replay_bootstrap.h"
#include "portable_replay_pack.h"
#include "portable_replay_demo.h"
#include "portable_replay_demo_table.h"
#include "portable_replay_staff.h"
#include "portable_replay_staff_reuse.h"
#include "portable_hardware.h"
#ifndef JGR_NATIVE_REPLAY
#define JGR_NATIVE_REPLAY 0
#endif
#if JGR_NATIVE_REPLAY
#include "portable_native_demo.h"
#include "portable_native_title.h"
#include "portable_native_staff.h"
#include "portable_native_tables.h"
#include "portable_native_math.h"
#include "portable_native_rival.h"
#ifdef __PSP__
#include "portable_scratch.h"
#endif
#endif
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#undef fopen
#undef stat
#undef remove
#undef rename
#if JGR_REPLAY_STATS
#ifndef JGR_REPLAY_CLOCK
#ifdef __PSP__
#include <pspkernel.h>
#define JGR_REPLAY_CLOCK() sceKernelGetSystemTimeWide()
#else
#include <sys/time.h>
static uint64_t replay_clock(void)
{struct timeval t;gettimeofday(&t,NULL);return (uint64_t)t.tv_sec*1000000u+t.tv_usec;}
#define JGR_REPLAY_CLOCK() replay_clock()
#endif
#endif
static JgrReplayStats replay_stats;
static uint64_t replay_started,replay_stage_started;
static unsigned replay_job_depth,replay_request_reused;
static void replay_accrue(JgrReplayStats *out,uint64_t elapsed)
{
 out->total_us+=elapsed;
 switch(out->state){
 case JGR_REPLAY_TABLES:out->tables_us+=elapsed;break;
 case JGR_REPLAY_BOOTSTRAP:out->bootstrap_us+=elapsed;break;
 case JGR_REPLAY_SETUP:out->setup_us+=elapsed;break;
 case JGR_REPLAY_GENERATION:out->generation_us+=elapsed;break;
 case JGR_REPLAY_PACKING:out->packing_us+=elapsed;break;
 case JGR_REPLAY_COPY:out->copy_us+=elapsed;break;
 }
 if(out->family<JGR_REPLAY_FAMILIES)out->family_us[out->family]+=elapsed;
}
void jgr_replay_stats_stage(unsigned state)
{
 if(!replay_stats.active)return;
 uint64_t now=JGR_REPLAY_CLOCK();
 replay_accrue(&replay_stats,now-replay_stage_started);
 replay_stage_started=now;replay_stats.state=state;
}
void jgr_replay_stats_reset(void)
{memset(&replay_stats,0,sizeof replay_stats);replay_stats.job_index=~0u;replay_stats.family=JGR_REPLAY_FAMILIES;replay_started=replay_stage_started=0;replay_job_depth=replay_request_reused=0;}
void jgr_replay_stats(JgrReplayStats *out)
{
 if(!out)return;
 *out=replay_stats;
 if(out->active){uint64_t now=JGR_REPLAY_CLOCK();out->active_us=now-replay_started;
  out->state_us=now-replay_stage_started;replay_accrue(out,out->state_us);}
}
static void replay_request_begin(const char *name)
{
 replay_request_reused=0;replay_stats.output_requests++;replay_stats.active=1;replay_stats.job_index=~0u;
 replay_stats.backend=0;replay_stats.family=JGR_REPLAY_FAMILIES;replay_stats.state=JGR_REPLAY_SETUP;
 replay_started=replay_stage_started=JGR_REPLAY_CLOCK();
 snprintf(replay_stats.output,sizeof replay_stats.output,"%s",name?name:"");
}
static void replay_request_end(int result,uint64_t bytes)
{
 jgr_replay_stats_stage(JGR_REPLAY_IDLE);replay_stats.active=0;
 replay_stats.active_us=replay_stats.state_us=0;
 if(result)replay_stats.failures++;else{replay_stats.outputs++;replay_stats.reused_outputs+=replay_request_reused;replay_stats.output_bytes+=bytes;}
}
#define REPLAY_STAT(statement) do{statement;}while(0)
#else
void jgr_replay_stats_reset(void){}
void jgr_replay_stats(JgrReplayStats *out){if(out)memset(out,0,sizeof *out);}
#define REPLAY_STAT(statement) ((void)0)
#endif
typedef struct {
 PortableReplaySelection selection;
 const char *outputs[3];
 unsigned character;
 int demo;
 PortableReplayGangFixture fixture;
} ReplayJob;
#include "portable_replay_session_data.inc"
#define JOBS (sizeof replay_jobs/sizeof replay_jobs[0])
struct JgrReplaySession {
 JgrDisc *disc;char scratch[1024],error[192];
 JgrTableDependency dependency;void *dependency_user;
 JgrReplayProgress progress;void *progress_user;
 PortableReplayBootstrap *boot;
 JgrReplayPack *pack[3];FILE *records[3];
 JgrReplayDemo *demo;JgrReplayStaff *staff;
 FILE *staff_stream;unsigned staff_ready;
 unsigned char *staff_metadata;unsigned staff_metadata_bytes;
 unsigned char *cues[19];unsigned cue_bytes[19];
 unsigned tick[3],camera[3],part[3],seen[3],created[8];
 unsigned boot_kind;
 int current,last;
 char native_assets[1024];unsigned native_tables_ready;
#if JGR_NATIVE_REPLAY
 JgrNativeCache *native_cache;
#endif
};
static uint32_t word(const unsigned char *p)
{return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static int error(JgrReplaySession *s,const char *message)
{snprintf(s->error,sizeof s->error,"%s",message);return -1;}
static int path(JgrReplaySession *s,unsigned slot,char name[1200])
{return snprintf(name,1200,"%s/replay-%s-%u.tmp",s->scratch,slot<3?"records":"output",slot<3?slot:slot-3)>=1200?-1:0;}
static FILE *temporary(JgrReplaySession *s,unsigned slot)
{
 char name[1200];struct stat st;if(slot>=8||path(s,slot,name))return NULL;
#ifdef __PSP__
 int exists=stat(name,&st);
#else
 int exists=lstat(name,&st);
#endif
 if((!exists&&!S_ISREG(st.st_mode))||(exists&&errno!=ENOENT))return NULL;
 FILE *f=fopen(name,"w+b");if(f)s->created[slot]=1;return f;
}
static void close_job(JgrReplaySession *s)
{
 if(s->boot)portable_replay_bootstrap_frame_observer(s->boot,NULL,NULL);
 for(unsigned i=0;i<3;i++){
  jgr_replay_pack_destroy(s->pack[i]);s->pack[i]=NULL;
  if(s->records[i])fclose(s->records[i]);
  s->records[i]=NULL;
 }
 jgr_replay_demo_destroy(s->demo);s->demo=NULL;
 jgr_replay_staff_destroy(s->staff);s->staff=NULL;
}
static int player_model(JgrDisc *disc,unsigned character,unsigned *model)
{
 /* The compact bank orders PLAY members by source archive order. Character
    identities live in the user-supplied headers, not a public binding table. */
 JgrDiscFile archive,file;unsigned char header[12];
 if(character>=16||!model||jgr_disc_find(disc,"JETRADIO/PLAYER.AFS",&archive)||
    jgr_disc_read(disc,&archive,0,header,8))return -1;
 unsigned count=word(header+4),seen=0,models=0;
 if(count>512)return -1;
 for(unsigned member=0;member<count;member++){
  if(jgr_disc_afs(disc,&archive,member,&file)||file.size<12||
     jgr_disc_read(disc,&file,0,header,12))return -1;
  if(memcmp(header,"PLAY",4))continue;
  unsigned table=word(header+8);
  if(jgr_disc_read(disc,&file,table,header,4))return -1;
  unsigned identity=word(header);
  if(identity>=16||(seen&(1u<<identity))||models>=16)return -1;
  if(identity==character)*model=models;
  models++;seen|=1u<<identity;
 }
 return seen==65535?0:-1;
}
static int prepare_pack(JgrReplaySession *s,unsigned slot,unsigned character)
{
 if(character>=16||slot>=3)return error(s,"Invalid replay character");
 unsigned model=16;
 if(player_model(s->disc,character,&model))return error(s,"Invalid original player identities");
 const ReplayJob *job=replay_jobs+s->current;
 FILE *normal=s->dependency(s->dependency_user,"chars.jgc"),*spray=NULL;
 if(job->selection.kind==PORTABLE_REPLAY_STAFF){char name[40];snprintf(name,sizeof name,"spraymesh%02u.jgc",model);spray=s->dependency(s->dependency_user,name);}
 s->records[slot]=temporary(s,slot);
 if(normal&&s->records[slot]&&(job->selection.kind!=PORTABLE_REPLAY_STAFF||spray))
  s->pack[slot]=jgr_replay_pack_create(s->records[slot],normal,model,spray,character);
 if(normal)fclose(normal);
 if(spray)fclose(spray);
 if(!s->pack[slot])return error(s,"Replay bind bank or scratch unavailable");
 unsigned sections=job->selection.kind==PORTABLE_REPLAY_GANG?3:job->selection.kind==PORTABLE_REPLAY_RIVAL?2:0;
 if(sections&&jgr_replay_pack_rival(s->pack[slot],sections))return error(s,"Replay section layout invalid");
 return 0;
}
static int sample(void *user,const unsigned char a[0x900],const unsigned char c[0x290],const unsigned char input[0xd0],const unsigned char pose[0x400])
{
 JgrReplaySession *s=user;const ReplayJob *job=replay_jobs+s->current;
 unsigned kind=job->selection.kind,slot=0,part=0,cut=0,variant=0,tick=word(a+0x98),camera=word(c+0x274);
 unsigned rival=kind==PORTABLE_REPLAY_RIVAL||kind==PORTABLE_REPLAY_GANG;
 if(rival){
  if(!word(input+0xb8))return 1;
  if(kind==PORTABLE_REPLAY_GANG){unsigned member=word(input+0x90);if(member<107||member>109)return !error(s,"Invalid gang member");slot=member-107;}
  part=word(input+0xc0);
 }
 unsigned character=job->character,actor_character=word(a+0x7c);
 unsigned sentinel=(kind==PORTABLE_REPLAY_STAFF||kind==PORTABLE_REPLAY_TITLE)&&actor_character==16;
 if(!sentinel&&character!=actor_character)return !error(s,"Replay character mismatch");
 if(!s->pack[slot]&&prepare_pack(s,slot,character))return 0;
 if(kind==PORTABLE_REPLAY_STAFF){
  if(!jgr_replay_staff_sample(s->staff,a,c,input,&part,&cut,&variant))return !error(s,"Credits observation failed");
 }else{
  if(!rival)part=s->part[slot]+(s->seen[slot]&&tick<s->tick[slot]);
  cut=(part>0&&tick==1)||(s->seen[slot]&&camera!=s->camera[slot]);
 }
 if(jgr_replay_pack_sample(s->pack[slot],a,c,pose,part,cut,variant)||
    (s->demo&&jgr_replay_demo_sample(s->demo,a)))return !error(s,"Replay sample is invalid or storage is full");
 s->tick[slot]=tick;s->camera[slot]=camera;s->part[slot]=part;s->seen[slot]=1;return 1;
}
static int save_cues(JgrReplaySession *s,unsigned demo)
{
 FILE *out=temporary(s,6);if(!out)return error(s,"Cannot write replay audio observations");
 unsigned count=0;const PortableReplayCue *cues=portable_replay_bootstrap_cues(s->boot,&count);
 int result=jgr_replay_demo_finish(s->demo,cues,count,out);long size=-1;
 if(!result&&(fflush(out)||(size=ftell(out))<16||size>128*1024||fseek(out,0,SEEK_SET)))result=-1;
 unsigned char *data=result?NULL:malloc((size_t)size);
 if(!data||fread(data,1,(size_t)size,out)!=(size_t)size)result=-1;
 if(fclose(out))result=-1;
 if(result){free(data);return error(s,"Replay audio observations failed validation");}
 free(s->cues[demo]);s->cues[demo]=data;s->cue_bytes[demo]=(unsigned)size;return 0;
}
static int staff_pose_sample(void *user,const unsigned char a[0x900],const unsigned char c[0x290],
 const unsigned char input[0xd0],const unsigned char pose[0x400],unsigned part,unsigned cut,unsigned variant)
{
 (void)input;JgrReplaySession *s=user;unsigned character=replay_jobs[s->current].character;
 unsigned observed=word(a+0x7c);
 if(observed!=16&&observed!=character)return !error(s,"Credits pose character mismatch");
 if(!s->pack[0]&&prepare_pack(s,0,character))return 0;
 return jgr_replay_pack_sample(s->pack[0],a,c,pose,part,cut,variant)?
  !error(s,"Credits pose sample is invalid or storage is full"):1;
}
static int staff_metadata(JgrReplaySession *s,unsigned character,int record)
{
 FILE *out=temporary(s,6);if(!out)return error(s,"Cannot write credits metadata");
 int result=0;
 if(record){
  long bytes;
  if(!jgr_replay_staff_finish(s->staff,out)||fflush(out)||(bytes=ftell(out))<12||
     bytes>128*1024||fseek(out,0,SEEK_SET))result=-1;
  else {
   unsigned char *data=malloc((size_t)bytes);
   if(!data||fread(data,1,(size_t)bytes,out)!=(size_t)bytes){free(data);result=-1;}
   else {free(s->staff_metadata);s->staff_metadata=data;s->staff_metadata_bytes=(unsigned)bytes;}
  }
 }else{
  if(!s->staff_metadata||s->staff_metadata_bytes<12)result=-1;
  else {
   unsigned char identity[4]={(unsigned char)character,0,0,0};
   if(fwrite(s->staff_metadata,1,8,out)!=8||fwrite(identity,1,4,out)!=4||
      fwrite(s->staff_metadata+12,1,s->staff_metadata_bytes-12,out)!=s->staff_metadata_bytes-12)result=-1;
  }
 }
 if(fclose(out))result=-1;
 return result?error(s,"Credits metadata is incomplete"):0;
}
#if JGR_NATIVE_REPLAY
static int native_job_supported(const ReplayJob *job)
{
 return job->demo>=0 || job->selection.kind==PORTABLE_REPLAY_STAFF ||
        job->selection.kind==PORTABLE_REPLAY_GANG || job->selection.kind==PORTABLE_REPLAY_RIVAL ||
        job->selection.kind==PORTABLE_REPLAY_TITLE;
}
static int run_native_job(JgrReplaySession *s,unsigned index)
{
 const ReplayJob *job=replay_jobs+index;
 close_job(s);s->last=-1;s->current=(int)index;s->error[0]=0;
 /* The interpreter's guest memory must not coexist with native world/model
  * working sets on the PSP-1000. Native source state is independently loaded. */
 portable_replay_bootstrap_destroy(s->boot);s->boot=NULL;
 if(!s->native_tables_ready){
  jgr_replay_stats_stage(JGR_REPLAY_TABLES);
  if(jgr_native_tables_load(s->dependency,s->dependency_user,s->error,sizeof s->error) ||
     !jgr_native_math_load(s->disc))return s->error[0]?-1:error(s,"Native replay math parameters unavailable");
  s->native_tables_ready=1;
  jgr_replay_stats_stage(JGR_REPLAY_SETUP);
 }
 if(!s->native_cache){
  s->native_cache=jgr_native_cache_create(s->disc,s->scratch);
 }
#ifdef __PSP__
 jgr_native_cache_trim(s->native_cache,jgr_scratch_heap_available());
#endif
 JgrNativeConfig config={s->disc,s->native_assets,s->scratch,s->dependency,s->dependency_user,
                         s->progress,s->progress_user,s->error,sizeof s->error,s->native_cache};
 if(job->selection.kind==PORTABLE_REPLAY_GANG || job->selection.kind==PORTABLE_REPLAY_RIVAL){
  unsigned gang=job->selection.kind==PORTABLE_REPLAY_GANG;
  char mission[20];snprintf(mission,sizeof mission,"S%uM%02u",job->selection.stage,job->selection.chapter+1);
  for(unsigned member=0;member<(gang?3u:1u);member++){
   FILE *records=temporary(s,member),*out=temporary(s,3+member);
   int result=-1;
   if(records && out){
    setvbuf(records,NULL,_IOFBF,65536);setvbuf(out,NULL,_IOFBF,65536);
    result=jgr_native_rival_generate(&config,mission,job->selection.record,job->selection.subset,
             job->character,member,gang?3:2,gang?&job->fixture:NULL,records,out);
   }
   if(records && fclose(records))result=-1;
   if(out && fclose(out))result=-1;
   if(result)return s->error[0]?-1:error(s,"Native rival preparation failed");
  }
  s->last=(int)index;return 0;
 }
 if(job->selection.kind==PORTABLE_REPLAY_STAFF && !s->staff_ready){
  if(s->staff_stream){fclose(s->staff_stream);s->staff_stream=NULL;}
  s->staff_stream=temporary(s,7);
  if(!s->staff_stream || !jgr_native_staff_record(&config,s->staff_stream))
   return s->error[0]?-1:error(s,"Native credits common generation failed");
  s->staff_ready=1;
 }
 FILE *records=temporary(s,0),*out=temporary(s,3),*metadata=NULL;int result=-1;
 if(!records || !out){error(s,"Native replay scratch unavailable");goto done;}
 setvbuf(records,NULL,_IOFBF,65536);setvbuf(out,NULL,_IOFBF,65536);
 if(job->selection.kind==PORTABLE_REPLAY_STAFF){
  metadata=temporary(s,6);
  if(metadata && jgr_native_staff_generate(&config,job->character,s->staff_stream,records,out,metadata))result=0;
 }else if(job->selection.kind==PORTABLE_REPLAY_TITLE){
  result=jgr_native_title_generate(&config,job->character,records,out);
 }else{
  char mission[20];snprintf(mission,sizeof mission,"S%uM%02u",job->selection.stage,job->selection.chapter+1);
  unsigned char *cues=NULL;unsigned bytes=0;
  result=jgr_native_demo_generate(&config,mission,job->selection.record,job->selection.subset,
                                  job->character,records,out,&cues,&bytes);
  if(!result){free(s->cues[job->demo]);s->cues[job->demo]=cues;s->cue_bytes[job->demo]=bytes;}
  else free(cues);
 }
done:
 if(records && fclose(records))result=-1;
 if(out && fclose(out))result=-1;
 if(metadata && fclose(metadata))result=-1;
 if(result)return s->error[0]?-1:error(s,"Native replay generation is incomplete");
 s->last=(int)index;return 0;
}
#endif
static int run_job(JgrReplaySession *s,unsigned index);
static int run_job_impl(JgrReplaySession *s,unsigned index)
{
 if(index>=JOBS)return error(s,"Unknown replay selection");
 const ReplayJob *job=replay_jobs+index;
#if JGR_NATIVE_REPLAY
 if(s->native_assets[0]){
  if(!native_job_supported(job))return error(s,"Replay selection has no native generator");
  return run_native_job(s,index);
 }
#endif
 if(job->selection.kind==PORTABLE_REPLAY_STAFF&&!s->staff_ready&&job->character){
  unsigned first=0;
  while(first<JOBS&&(replay_jobs[first].selection.kind!=PORTABLE_REPLAY_STAFF||replay_jobs[first].character))first++;
  if(first==JOBS||run_job(s,first))return -1;
 }
 close_job(s);s->last=-1;s->current=(int)index;s->error[0]=0;
 memset(s->tick,0,sizeof s->tick);memset(s->part,0,sizeof s->part);memset(s->camera,0,sizeof s->camera);memset(s->seen,0,sizeof s->seen);
 /* Attract playback and credits own distinct original task lifecycles.
  * Re-entering STAFF after a gang chase retains incompatible task work.
  * Reuse within the credits family, but isolate its entry and exit. */
 unsigned kind=job->selection.kind;
 if(s->boot&&(kind==PORTABLE_REPLAY_TITLE||
    ((kind==PORTABLE_REPLAY_STAFF)!=(s->boot_kind==PORTABLE_REPLAY_STAFF)))){
  portable_replay_bootstrap_destroy(s->boot);s->boot=NULL;
 }
 jgr_replay_stats_stage(JGR_REPLAY_BOOTSTRAP);
 if(!s->boot)s->boot=portable_replay_bootstrap_create(s->disc,jgr_hardware_fsca(),32768);
 if(!s->boot)return error(s,"Not enough memory to create replay converter");
 if(job->demo>=0){s->demo=jgr_replay_demo_create();if(!s->demo)return error(s,"Not enough memory for replay cues");}
 int staff_record=kind==PORTABLE_REPLAY_STAFF&&!s->staff_ready;
 if(staff_record){s->staff=jgr_replay_staff_create(s->boot,job->character);if(!s->staff)return error(s,"Cannot create credits observer");}
 if(job->selection.kind==PORTABLE_REPLAY_GANG&&!portable_replay_bootstrap_gang_fixture(s->boot,&job->fixture))return error(s,"Invalid spectator input fixture");
 if(kind==PORTABLE_REPLAY_STAFF&&!staff_record){
  if(!portable_replay_bootstrap_staff_poses(s->boot,s->staff_stream,job->character,staff_pose_sample,s))return error(s,"Cannot select credits pose replay");
 }else if(!portable_replay_bootstrap_select(s->boot,&job->selection,sample,s))return error(s,"Cannot select original replay");
 s->boot_kind=kind;
 if(s->staff)portable_replay_bootstrap_frame_observer(s->boot,jgr_replay_staff_frame,s->staff);
 if(staff_record){
  s->staff_stream=temporary(s,7);
  if(!s->staff_stream||!portable_replay_bootstrap_staff_record(s->boot,s->staff_stream))return error(s,"Cannot record common credits inputs");
 }
 jgr_replay_stats_stage(JGR_REPLAY_GENERATION);
 int state=0;unsigned long long slices=0;
 while(!state){
  state=portable_replay_bootstrap_step(s->boot,100000);
  PortableReplayStatus status;portable_replay_bootstrap_status(s->boot,&status);
  if(s->progress&&!s->progress(status.instructions,s->progress_user))return error(s,"Installation cancelled");
  if(++slices>1000000)return error(s,"Original replay exceeded execution limit");
 }
 if(state<0){
  if(!s->error[0]){PortableReplayStatus status;portable_replay_bootstrap_status(s->boot,&status);snprintf(s->error,sizeof s->error,"Original replay stopped: fault %u at %08x (address %08x)",(unsigned)status.fault,(unsigned)status.pc,(unsigned)status.address);}
  return -1;
 }
 if(staff_record&&!portable_replay_bootstrap_staff_record_finish(s->boot))return error(s,"Common credits inputs are incomplete");
 jgr_replay_stats_stage(JGR_REPLAY_PACKING);
 unsigned count=job->selection.kind==PORTABLE_REPLAY_GANG?3:1;
 for(unsigned i=0;i<count;i++){
  FILE *out=temporary(s,3+i);if(!out)return error(s,"Cannot write converted replay");
  int result=jgr_replay_pack_finish(s->pack[i],out);if(fclose(out))result=-1;
  if(result)return error(s,"Converted replay is incomplete or storage is full");
 }
 if(kind==PORTABLE_REPLAY_STAFF&&staff_metadata(s,job->character,staff_record))return -1;
 if(staff_record)s->staff_ready=1;
 if(s->demo&&save_cues(s,(unsigned)job->demo))return -1;
 close_job(s);s->last=(int)index;return 0;
}
#if JGR_REPLAY_STATS
static unsigned replay_family(const ReplayJob *job)
{return job->selection.kind==PORTABLE_REPLAY_STAFF?JGR_REPLAY_CREDITS:
 job->selection.kind==PORTABLE_REPLAY_GANG || job->selection.kind==PORTABLE_REPLAY_RIVAL?JGR_REPLAY_OPPONENT:
 job->selection.kind==PORTABLE_REPLAY_TITLE?JGR_REPLAY_TITLE:JGR_REPLAY_DEMO;}
#endif
static int run_job(JgrReplaySession *s,unsigned index)
{
#if JGR_REPLAY_STATS
 unsigned nested=replay_job_depth++;
 unsigned previous_job=replay_stats.job_index,previous_family=replay_stats.family,previous_backend=replay_stats.backend;
 if(index<JOBS){
  jgr_replay_stats_stage(JGR_REPLAY_SETUP);replay_stats.job_index=index;
  replay_stats.family=replay_family(replay_jobs+index);replay_stats.backend=2;
#if JGR_NATIVE_REPLAY
  const ReplayJob *job=replay_jobs+index;
  if(s->native_assets[0] && native_job_supported(job))replay_stats.backend=1;
#endif
  replay_stats.jobs++;replay_stats.family_jobs[replay_stats.family]++;
  if(replay_stats.backend==1)replay_stats.native_jobs++;else replay_stats.interpreted_jobs++;
 }
#endif
 int result=run_job_impl(s,index);
#if JGR_REPLAY_STATS
 jgr_replay_stats_stage(JGR_REPLAY_SETUP);
 replay_job_depth--;
 if(nested){replay_stats.job_index=previous_job;replay_stats.family=previous_family;replay_stats.backend=previous_backend;}
#endif
 return result;
}
int jgr_replay_session_known(const char *name)
{
 if(!name)return 0;
 if(!strcmp(name,"source_data_ptr_challenge_demo_data.jsd"))return 1;
 for(unsigned i=0;i<JOBS;i++)for(unsigned j=0;j<3;j++)if(*replay_jobs[i].outputs[j]&&!strcmp(name,replay_jobs[i].outputs[j]))return 1;
 return 0;
}
JgrReplaySession *jgr_replay_session_create(JgrDisc *disc,const char *scratch,JgrTableDependency dependency,void *dependency_user,
    JgrReplayProgress progress,void *progress_user,char *message,size_t size)
{
 JgrReplaySession *s=NULL;
 if(disc&&scratch&&strlen(scratch)<1024&&dependency)s=calloc(1,sizeof *s);
 if(!s){if(message&&size)snprintf(message,size,"Cannot create first-install replay session");return NULL;}
 s->disc=disc;strcpy(s->scratch,scratch);s->dependency=dependency;s->dependency_user=dependency_user;
 s->progress=progress;s->progress_user=progress_user;s->last=s->current=-1;return s;
}
static int copy_output(FILE *from,FILE *out)
{
 /* Generation has released its transient world/pose working set. Borrow a
  * bounded transfer buffer now, not while the PSP-1000 is simulating a job.
  * This newly opened sequential reader needs no second stdio read buffer.
  * The caller owns destination buffering; it may already have used out. */
 unsigned char fallback[4096],*bulk=malloc(128u*1024u);
 if(bulk && setvbuf(from,NULL,_IONBF,0)){free(bulk);bulk=NULL;}
 unsigned char *buffer=bulk?bulk:fallback;
 size_t capacity=bulk?128u*1024u:sizeof fallback,n;int result=0;
 while((n=fread(buffer,1,capacity,from)))if(fwrite(buffer,1,n,out)!=n){result=-1;break;}
 if(ferror(from))result=-1;
 free(bulk);return result;
}
static int generate_output(JgrReplaySession *s,const char *name,FILE *out)
{
 if(!s||!out||!jgr_replay_session_known(name))return -1;
 if(!strcmp(name,"source_data_ptr_challenge_demo_data.jsd")){
  for(unsigned i=0;i<JOBS;i++)if(replay_jobs[i].demo>=0&&!s->cues[replay_jobs[i].demo]&&run_job(s,i))return -1;
  jgr_replay_stats_stage(JGR_REPLAY_TABLES);
  return jgr_replay_demo_table(s->disc,(const unsigned char **)s->cues,s->cue_bytes,out)?error(s,"Demonstration cue table mismatch"):0;
 }
 unsigned job=0,slot=0,found=0;
 for(unsigned i=0;i<JOBS&&!found;i++)for(unsigned j=0;j<3;j++)if(!strcmp(name,replay_jobs[i].outputs[j])){job=i;slot=j;found=1;break;}
 if(!found)return error(s,"Unknown converted replay");
 if(s->last!=(int)job){if(run_job(s,job))return -1;}
 else {REPLAY_STAT(replay_request_reused=1);}
 REPLAY_STAT(replay_stats.family=replay_family(replay_jobs+job);replay_stats.job_index=job);
 jgr_replay_stats_stage(JGR_REPLAY_COPY);
 if(replay_jobs[job].selection.kind==PORTABLE_REPLAY_STAFF&&slot==1)slot=3;
 char file[1200];if(path(s,3+slot,file))return error(s,"Replay scratch path is too long");
 FILE *from=fopen(file,"rb");if(!from)return error(s,"Converted replay scratch is missing");
 int result=copy_output(from,out);
 if(fclose(from))result=-1;
 return result?error(s,"Cannot publish converted replay"):0;
}
int jgr_replay_session_generate(JgrReplaySession *s,const char *name,FILE *out)
{
 REPLAY_STAT(replay_request_begin(name));
#if JGR_REPLAY_STATS
 long before=out?ftell(out):-1;
#endif
 int result=generate_output(s,name,out);
#if JGR_REPLAY_STATS
 long after=out?ftell(out):-1;
 replay_request_end(result,!result && before>=0 && after>=before?(uint64_t)(after-before):0);
#endif
 return result;
}
const char *jgr_replay_session_error(const JgrReplaySession *s){return s?s->error:"No replay session";}
int jgr_replay_session_native_assets(JgrReplaySession *s,const char *directory)
{
 if(!s || !directory || !*directory || strlen(directory)>=sizeof s->native_assets || s->current>=0)return -1;
#if JGR_NATIVE_REPLAY
 strcpy(s->native_assets,directory);return 0;
#else
 return error(s,"Native replay generation is not enabled in this build");
#endif
}
void jgr_replay_session_destroy(JgrReplaySession *s)
{
 if(!s)return;
 close_job(s);portable_replay_bootstrap_destroy(s->boot);
 if(s->staff_stream)fclose(s->staff_stream);
 free(s->staff_metadata);
#if JGR_NATIVE_REPLAY
 jgr_native_cache_destroy(s->native_cache);
 if(s->native_tables_ready)jgr_native_math_clear();
#endif
 for(unsigned i=0;i<19;i++)free(s->cues[i]);
 for(unsigned i=0;i<8;i++)if(s->created[i]){char file[1200];if(!path(s,i,file))remove(file);}
 free(s);
}
