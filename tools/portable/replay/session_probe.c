/* Private end-to-end session proof. Only original disc + generated bind banks
 * are inputs; expected route bytes are checked by the host after execution. */
#include "portable_replay_session.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static FILE *dependency(void *user,const char *name)
{char path[2048];if(snprintf(path,sizeof path,"%s/%s",(const char*)user,name)>=(int)sizeof path)return NULL;return fopen(path,"rb");}
static int progress(unsigned long long instructions,void *user)
{unsigned long long *previous=user;if(instructions/500000000!=*previous/500000000){fprintf(stderr,"instructions=%llu\n",instructions);*previous=instructions;}return 1;}
int main(int argc,char **argv)
{
 if(argc<5){fprintf(stderr,"usage: session-proof GDI VERIFIED_ASSETS SCRATCH OUTPUT_NAME...\n");return 2;}
 JgrDisc *disc=NULL;char error[192];unsigned long long last=0;
 if(jgr_disc_open(&disc,argv[1],error,sizeof error)){fprintf(stderr,"%s\n",error);return 2;}
 JgrReplaySession *s=jgr_replay_session_create(disc,argv[3],dependency,argv[2],progress,&last,error,sizeof error);
 if(!s){fprintf(stderr,"%s\n",error);jgr_disc_close(disc);return 2;}
 int result=0;
 for(int i=4;i<argc;i++){
  char path[2048];if(snprintf(path,sizeof path,"%s/%s",argv[3],argv[i])>=(int)sizeof path){result=2;break;}
  FILE *out=fopen(path,"wb");fprintf(stderr,"generating %s\n",argv[i]);
  int failure=out?jgr_replay_session_generate(s,argv[i],out):-1;
  if(out&&fclose(out))failure=-1;
  printf("%s %d\n",argv[i],failure);fflush(stdout);
  if(failure){fprintf(stderr,"%s\n",jgr_replay_session_error(s));result=1;break;}
 }
 jgr_replay_session_destroy(s);jgr_disc_close(disc);return result;
}
