/* Disc-only production API proof. Bind banks are locally generated files. */
#include "portable_replay_bootstrap.h"
#include "portable_replay_staff_reuse.h"
#include "portable_replay_staff.h"
#include "portable_replay_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
typedef struct {PortableReplayBootstrap*boot;JgrReplayStaff*staff;JgrReplayPack*pack;unsigned samples;} Capture;
static unsigned word(const unsigned char*p){return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static int model_index(JgrDisc*disc,unsigned character,unsigned*model)
{
 JgrDiscFile archive,file;unsigned char header[12];
 if(jgr_disc_find(disc,"JETRADIO/PLAYER.AFS",&archive)||jgr_disc_read(disc,&archive,0,header,8))return 0;
 unsigned count=word(header+4),models=0;if(count>512)return 0;
 for(unsigned member=0;member<count;member++){
  if(jgr_disc_afs(disc,&archive,member,&file)||file.size<12||jgr_disc_read(disc,&file,0,header,12))return 0;
  if(memcmp(header,"PLAY",4))continue;
  unsigned table=word(header+8);if(jgr_disc_read(disc,&file,table,header,4))return 0;
  if(word(header)==character){*model=models;return 1;}models++;
 }return 0;
}
static int pose(void*context,const unsigned char*a,const unsigned char*c,const unsigned char*i,const unsigned char*p,unsigned part,unsigned cut,unsigned variant)
{Capture*x=context;(void)i;x->samples++;return !jgr_replay_pack_sample(x->pack,a,c,p,part,cut,variant);}
static int sample(void*context,const unsigned char*a,const unsigned char*c,const unsigned char*i,const unsigned char*p)
{Capture*x=context;unsigned part,cut,variant;return jgr_replay_staff_sample(x->staff,a,c,i,&part,&cut,&variant)&&pose(x,a,c,i,p,part,cut,variant);}
int main(int argc,char**argv)
{
 if(argc!=6){fprintf(stderr,"usage: staff-api GDI HARDWARE GENERATED_BIND_DIRECTORY OUTPUT_DIRECTORY LAST_CHARACTER\n");return 2;}
 setvbuf(stdout,NULL,_IOLBF,0);
 JgrDisc*disc=NULL;char error[192],path[2048];uint32_t hardware[32768];FILE*f=fopen(argv[2],"rb");
 if(!f || fread(hardware,1,sizeof hardware,f)!=sizeof hardware || fgetc(f)!=EOF)return 2;fclose(f);
 if(jgr_disc_open(&disc,argv[1],error,sizeof error)<0){fprintf(stderr,"%s\n",error);return 2;}
 Capture x={0};x.boot=portable_replay_bootstrap_create(disc,hardware,32768);if(!x.boot)return 2;
 (void)mkdir(argv[4],0700);snprintf(path,sizeof path,"%s/common.swi",argv[4]);FILE*events=fopen(path,"w+b");if(!events)return 2;
 unsigned char metadata[131072];unsigned metadata_bytes=0;unsigned last=(unsigned)strtoul(argv[5],NULL,0);uint64_t prior=0;
 for(unsigned ch=0;ch<=last && ch<16;ch++){
  unsigned model;if(!model_index(disc,ch,&model))return 2;
  snprintf(path,sizeof path,"%s/chars.jgc",argv[3]);FILE*normal=fopen(path,"rb");
  snprintf(path,sizeof path,"%s/spraymesh%02u.jgc",argv[3],model);FILE*spray=fopen(path,"rb");
  snprintf(path,sizeof path,"%s/pack.tmp",argv[4]);FILE*scratch=fopen(path,"w+b");
  if(!normal || !spray || !scratch)return 2;
  x.pack=jgr_replay_pack_create(scratch,normal,model,spray,ch);fclose(normal);fclose(spray);if(!x.pack)return 2;x.samples=0;
  if(!ch){
   PortableReplaySelection selection={1,0,0,0,0,PORTABLE_REPLAY_STAFF};
   x.staff=jgr_replay_staff_create(x.boot,ch);if(!x.staff || !portable_replay_bootstrap_select(x.boot,&selection,sample,&x) ||
     !portable_replay_bootstrap_staff_record(x.boot,events))return 2;
   portable_replay_bootstrap_frame_observer(x.boot,jgr_replay_staff_frame,x.staff);
  }else if(!portable_replay_bootstrap_staff_poses(x.boot,events,ch,pose,&x))return 2;
  int result=0;unsigned slices=0;while(!result && slices++<100000)result=portable_replay_bootstrap_step(x.boot,100003);
  PortableReplayStatus status;portable_replay_bootstrap_status(x.boot,&status);
  printf("{\"character\":%u,\"result\":%d,\"samples\":%u,\"instructions\":%llu,\"fault\":%u,\"pc\":%u,\"address\":%u}\n",ch,result,x.samples,(unsigned long long)(status.instructions-prior),status.fault,status.pc,status.address);prior=status.instructions;
  if(result!=1)return 1;
  snprintf(path,sizeof path,"%s/staffroute%02u.bin",argv[4],ch);f=fopen(path,"wb");if(!f || jgr_replay_pack_finish(x.pack,f))return 2;fclose(f);
  jgr_replay_pack_destroy(x.pack);x.pack=NULL;fclose(scratch);
  if(!ch){
   if(!portable_replay_bootstrap_staff_record_finish(x.boot))return 2;
   snprintf(path,sizeof path,"%s/staffroute00_meta.bin",argv[4]);f=fopen(path,"w+b");
   if(!f || !jgr_replay_staff_finish(x.staff,f) || fseek(f,0,SEEK_SET))return 2;
   metadata_bytes=(unsigned)fread(metadata,1,sizeof metadata,f);if(!feof(f))return 2;fclose(f);
   jgr_replay_staff_destroy(x.staff);x.staff=NULL;
  }else{
   metadata[8]=(unsigned char)ch;snprintf(path,sizeof path,"%s/staffroute%02u_meta.bin",argv[4],ch);f=fopen(path,"wb");
   if(!f || fwrite(metadata,1,metadata_bytes,f)!=metadata_bytes)return 2;fclose(f);
  }
 }
 fclose(events);portable_replay_bootstrap_destroy(x.boot);jgr_disc_close(disc);return 0;
}
