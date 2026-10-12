#include "ending_flow.h"
#include <string.h>
#if JGR_PORTABLE_ASSETS
#include "source_data_ending_tables.inc"
#else
#include "generated/source_data/ending_tables.inc"
#endif
void ending_flow_init(EndingFlow *s){memset(s,0,sizeof *s);s->yes=1;}
static void next(EndingFlow *s,int phase){s->phase=phase;s->sub=0;}
EndingRequest ending_flow_step(EndingFlow *s,const EndingInputs *in)
{
    switch(s->phase){
    case 0:next(s,1);return END_STOP_AUDIO;
    case 1:case 3:
        switch(s->sub){
        case 0:
            if(s->phase==3 && in->loader_busy)break;
            s->sub=1;return s->phase==1 ? END_LOAD_MOVIE25:END_LOAD_MOVIE26;
        case 1:if(!in->loader_busy){s->sub=2;return END_START_MOVIE;}break;
        case 2:if(!in->movie_busy)s->sub=3;break;
        case 3:s->sub=4;return END_RELEASE_MOVIE;
        case 4:next(s,s->phase+1);break;
        }break;
    case 2:
        switch(s->sub){
        case 0:s->sub=1;return END_LOAD_GAME; /* stage index 0, chapter 1; retain skater */
        case 1:if(!in->loader_busy){s->sub=2;return END_LOAD_STAGE;}break;
        case 2:if(!in->loader_busy){s->sub=3;return END_LOAD_MISSION;}break;
        case 3:if(!in->loader_busy){s->sub=4;return END_CREATE_STAGE;}break;
        case 4:if(!in->stage_busy){s->sub=5;return END_LOAD_SKATER;}break;
        case 5:if(!in->loader_busy){s->sub=6;return END_LOAD_STAFF;}break;
        case 6:if(!in->loader_busy){s->sub=7;return END_START_STAFF;}break;
        case 7:if(!in->staff_busy)s->sub=8;break;
        case 8:s->sub=9;return END_RELEASE_STAGE;
        case 9:next(s,3);break;
        }break;
    case 4:
        if(s->sub==0){if(!in->loader_busy){s->sub=1;return END_LOAD_SAVE;}}
        else if(s->sub==1){if(!in->loader_busy)s->sub=2;}
        else if(s->sub==2){
            /* Unlike result task a00, the ending accepts A only. */
            if(in->pressed&4){s->sub=3;s->previous_axis=in->axis;return s->yes ? END_SAVE_YES:END_SAVE_NO;}
            if((in->pressed&0xc0) || (s->previous_axis+.5f<in->axis && (in->analog_mask&12)))s->yes=!s->yes;
            s->previous_axis=in->axis;
        }else if(s->sub==3){next(s,5);return END_RELEASE_SAVE;}
        break;
    case 5:return END_DONE;
    }
    return END_WAIT;
}
void staff_card_init(StaffCard *s){memset(s,0,sizeof *s);s->kind=-1;}
int staff_card_start(StaffCard *s,int kind,int hold,int buffer,int page,unsigned random15)
{
    if(s->kind!=-1 || (unsigned)kind>=4 || (unsigned)buffer>=3 || (unsigned)page>=2 || random15>=32768)return 0;
    s->phase=0;s->kind=kind;s->hold=hold;s->buffer=buffer;s->page=page;
    s->offset=STAFF_SLIDE_START[kind];s->target=STAFF_SLIDE_END[kind];
    s->pattern=(int)(((float)random15/32768.f)*4.f);return 1;
}
void staff_card_step(StaffCard *s)
{
    if(s->kind<0)return;
    if(s->phase==0){s->step=(STAFF_SLIDE_END[s->kind]-STAFF_SLIDE_START[s->kind])/30.f;
        s->target=STAFF_SLIDE_END[s->kind];s->phase=1;}
    else if(s->phase==1 || s->phase==3){
        s->offset+=s->step;
        if(s->step>0 ? s->offset>s->target:s->offset<s->target)s->offset=s->target;
        if(s->offset==s->target){if(s->phase==1)s->phase=2;else s->kind=-1;}
    }else if(s->phase==2 && --s->hold<0){
        s->step=-s->step;s->target=STAFF_SLIDE_START[s->kind];s->phase=3;
    }
}
void staff_card_viewport(const StaffCard *s,float out[6])
{
    float w=640,h=480,x=320,y=240,sx=1,sy=1,v=s->offset;
    if(s->kind==0 && v>0){w=640-v;x=w*.5f+v;sx=w/640;}
    if(s->kind==1 && v<0){w=v+640;x=w*.5f;sx=w/640;}
    if(s->kind==2 && v>0){h=480-v;y=h*.5f+v;sy=h/480;}
    if(s->kind==3 && v<0){h=v+480;y=h*.5f;sy=h/480;}
    out[0]=w;out[1]=h;out[2]=x;out[3]=y;out[4]=sx;out[5]=sy;
}
void staff_timeline_init(StaffTimeline *s)
{memset(s,0,sizeof *s);s->next_image=2;s->load_image=s->load_buffer=-1;}
int staff_timeline_step(StaffTimeline *s,int clock,int loader_busy,StaffCard *card,unsigned random15)
{
    s->load_image=s->load_buffer=-1;
    if(s->error)return 0;
    if(s->phase==0){
        if((unsigned)s->pc>=sizeof STAFF_COMMANDS/sizeof *STAFF_COMMANDS){s->error=1;return 0;}
        const int *cmd=STAFF_COMMANDS[s->pc];
        if(cmd[0]==4){s->phase=1;s->pc++;}
        else if(cmd[0]==5){s->buffer=cmd[1];s->pc++;}
        else if(cmd[0]!=-1 && (float)clock>=(float)cmd[1]){
            if(!staff_card_start(card,cmd[0],cmd[2],s->buffer,s->page[s->buffer],random15)){s->error=1;return 0;}
            s->phase=1;s->pc++;return 2;
        }
    }else if(s->phase==1 && card->kind==-1){
        if(++s->page[s->buffer]<2)s->phase=0;
        else{s->load_image=s->next_image++;s->load_buffer=s->buffer;s->phase=2;return 1;}
    }else if(s->phase==2 && !loader_busy){
        s->page[s->buffer]=0;if(++s->buffer>1)s->buffer=0;s->phase=0;
    }
    return 0;
}
