/* Private MIPS execution proof, not a first-install package. The common-world
 * input is a temporary host-generated fixture; the guest starts from the disc. */
#include "portable_replay_bootstrap.h"
#include "portable_replay_staff_reuse.h"
#include "portable_replay_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pspkernel.h>
#include <pspdebug.h>
PSP_MODULE_INFO("JGR Credits Pose API Proof",0,1,0);
PSP_MAIN_THREAD_ATTR(THREAD_ATTR_USER);
PSP_MAIN_THREAD_STACK_SIZE_KB(128);
PSP_HEAP_SIZE_KB(-512);
static JgrReplayPack*pack;static unsigned samples;
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
static int sample(void*u,const unsigned char*a,const unsigned char*c,const unsigned char*i,const unsigned char*p,unsigned part,unsigned cut,unsigned variant)
{(void)u;(void)i;samples++;return !jgr_replay_pack_sample(pack,a,c,p,part,cut,variant);}
int main(void)
{
 pspDebugScreenInit();pspDebugScreenPrintf("Private credits pose API proof\n");
 if(!freopen("bootstrap.log","w",stderr)||!freopen("replay-result.tmp","w",stdout))return 2;
 setvbuf(stderr,NULL,_IONBF,0);char disc_path[512],error[192];unsigned character=7;
 JgrDisc*disc=NULL;PortableReplayBootstrap*boot=NULL;uint32_t*hardware=malloc(131072);int result=-1;
 FILE*f=fopen("disc-path.txt","r");if(!f||!fgets(disc_path,sizeof disc_path,f))goto done;fclose(f);disc_path[strcspn(disc_path,"\r\n")]=0;
 f=fopen("character.txt","r");if(!f||fscanf(f,"%u",&character)!=1||character>=16)goto done;fclose(f);
 f=fopen("fsca-hardware.bin","rb");if(!hardware||!f||fread(hardware,1,131072,f)!=131072||fgetc(f)!=EOF)goto done;fclose(f);
 if(jgr_disc_open(&disc,disc_path,error,sizeof error)<0){fprintf(stderr,"%s\n",error);goto done;}
 boot=portable_replay_bootstrap_create(disc,hardware,32768);if(!boot)goto done;
 unsigned model;if(!model_index(disc,character,&model))goto done;
 FILE*normal=fopen("chars.jgc","rb"),*spray=fopen("spraymesh.jgc","rb"),*scratch=fopen("pack.tmp","w+b"),*events=fopen("common.swi","rb");
 if(!normal||!spray||!scratch||!events)goto done;
 pack=jgr_replay_pack_create(scratch,normal,model,spray,character);fclose(normal);fclose(spray);
 if(!pack||!portable_replay_bootstrap_staff_poses(boot,events,character,sample,NULL))goto done;
 result=0;unsigned slices=0;
 while(!result&&slices++<100000){
  result=portable_replay_bootstrap_step(boot,100003);
  if(slices%500==0){PortableReplayStatus s;portable_replay_bootstrap_status(boot,&s);fprintf(stderr,"progress samples=%u instructions=%llu fault=%u\n",samples,(unsigned long long)s.instructions,(unsigned)s.fault);}
 }
 if(result!=1)goto done;
 f=fopen("staffroute.bin","wb");if(!f||jgr_replay_pack_finish(pack,f)||fclose(f)){result=-1;goto done;}
 fclose(events);fclose(scratch);
 f=fopen("common-meta.bin","rb");FILE*out=fopen("staffroute_meta.bin","wb");
 if(!f||!out){result=-1;goto done;}
 unsigned char buffer[4096];size_t n;unsigned offset=0;
 while((n=fread(buffer,1,sizeof buffer,f))){if(!offset){if(n<12){result=-1;break;}buffer[8]=(unsigned char)character;buffer[9]=buffer[10]=buffer[11]=0;}
  if(fwrite(buffer,1,n,out)!=n){result=-1;break;}offset+=(unsigned)n;}
 if(ferror(f)||fclose(f)||fclose(out))result=-1;
 done:;
 PortableReplayStatus status;portable_replay_bootstrap_status(boot,&status);
 printf("{\"character\":%u,\"result\":%d,\"samples\":%u,\"instructions\":%llu,\"fault\":%u,\"pc\":%u,\"address\":%u}\n",character,result,samples,(unsigned long long)status.instructions,(unsigned)status.fault,(unsigned)status.pc,(unsigned)status.address);
 fclose(stdout);rename("replay-result.tmp","replay-result.json");
 jgr_replay_pack_destroy(pack);portable_replay_bootstrap_destroy(boot);jgr_disc_close(disc);free(hardware);sceKernelExitGame();return result==1?0:1;
}
