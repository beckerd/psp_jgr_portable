#include "pathmission.h"
static unsigned spawn(void *ctx,unsigned event,const unsigned args[7])
{return pathworld_spawn(&((PathCampaign *)ctx)->world,event,args);}
static void control(void *ctx,unsigned handle,int opcode)
{pathworld_control(&((PathCampaign *)ctx)->world,handle,opcode);}
static void reset(void *ctx){pathworld_reset(&((PathCampaign *)ctx)->world);}
static void released(void *ctx,unsigned event,unsigned handle)
{PathCampaign *c=ctx;mission_path_released(c->mission,event,handle);}
int pathmission_attach(PathCampaign *c,MissionState *m)
{
    if(!c || !m || !c->world.bank || c->world.error || c->world.count)return -1;
    if((c->mission && c->mission!=m) || (m->path_ctx && m->path_ctx!=c))return -2;
    c->mission=m;c->world.context=c;c->world.released=released;
    m->path_ctx=c;m->path_spawn=spawn;m->path_control=control;m->path_reset=reset;m->task_parents|=4;
    return 0;
}
void pathmission_detach(PathCampaign *c)
{
    if(!c)return;
    pathworld_reset(&c->world);
    MissionState *m=c->mission;
    if(m && m->path_ctx==c){m->path_ctx=NULL;m->path_spawn=NULL;m->path_control=NULL;m->path_reset=NULL;m->task_parents&=~4u;}
    c->world.context=NULL;c->world.released=NULL;c->mission=NULL;
}
