#include "portable_replay_staff.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#define MAX_TASKS 2000
#define MAX_DESCRIPTORS (6*512)
#define MAX_LIVE_WALLS 512
#define MAX_SPRAYS 1024
#define MAX_WALL_CUES 3072
typedef struct {uint32_t descriptor,id;} Descriptor;
typedef struct {uint32_t descriptor,value[5];} Wall;
struct JgrReplayStaff {
    PortableReplayBootstrap *boot;
    unsigned character,count,observed,parts,prior_actor,prior_tick,prior_row,prior_camera;
    uint32_t segment[9][6],spray[MAX_SPRAYS][8],wall_cue[MAX_WALL_CUES][8];
    unsigned sprays,wall_cues,descriptors,live_walls;
    Descriptor descriptor[MAX_DESCRIPTORS];
    Wall wall[MAX_LIVE_WALLS];
    uint32_t stack[MAX_TASKS],gf[MAX_TASKS],revealer[MAX_TASKS][2];
};
static uint32_t word(const unsigned char*p,unsigned at)
{return (uint32_t)p[at]|(uint32_t)p[at+1]<<8|(uint32_t)p[at+2]<<16|(uint32_t)p[at+3]<<24;}
static uint32_t readword(JgrReplayStaff*s,uint32_t at)
{unsigned char b[4];return portable_replay_bootstrap_read(s->boot,at,b,4)?word(b,0):0;}
static float real(uint32_t bits){float v;memcpy(&v,&bits,4);return v;}
static int valid(uint32_t p,unsigned n){return p>=0x8c000000 && p<=0x8d000000-n && !(p&3);}
static uint32_t staff_task(JgrReplayStaff*s)
{
    unsigned n=1,visits=0;s->stack[0]=readword(s,0x8c2e9c9c);
    while(n && visits++<MAX_TASKS){uint32_t p=s->stack[--n];if(!valid(p,0x28))continue;
        if((readword(s,p+4)&65535)==0x1b)return p;
        if(n>MAX_TASKS-3)return 0;
        s->stack[n++]=readword(s,p+0x24);s->stack[n++]=readword(s,p+0x1c);
    }return 0;
}
static int descriptor_order(const void*a,const void*b)
{uint32_t x=((const Descriptor*)a)->descriptor,y=((const Descriptor*)b)->descriptor;return x<y?-1:x>y;}
static int refresh_descriptors(JgrReplayStaff*s)
{
    s->descriptors=s->live_walls=0;
    for(unsigned area=0;area<6;area++){
        uint32_t table=readword(s,0x8cddf04c+area*4);
        for(unsigned index=0;index<512 && valid(table+index*4,4);index++){
            uint32_t desc=readword(s,table+index*4);if(!desc)break;
            if(s->descriptors==MAX_DESCRIPTORS)return 0;
            s->descriptor[s->descriptors++]=(Descriptor){desc,(area<<16)|index};
        }
    }
    qsort(s->descriptor,s->descriptors,sizeof *s->descriptor,descriptor_order);return 1;
}
static int descriptor_id(JgrReplayStaff*s,uint32_t desc,uint32_t*id)
{
    unsigned lo=0,hi=s->descriptors;
    while(lo<hi){unsigned mid=lo+(hi-lo)/2;uint32_t value=s->descriptor[mid].descriptor;
        if(value<desc)lo=mid+1;else hi=mid;
    }
    if(lo==s->descriptors || s->descriptor[lo].descriptor!=desc)return 0;
    *id=s->descriptor[lo].id;return 1;
}
static int controller_prefix(JgrReplayStaff*s,unsigned row,unsigned tick,const unsigned char*c)
{
    uint32_t stream=readword(s,0x8cfe0000+row*20),record=stream+32;
    if(!valid(record,4))return 0;
    unsigned repeat=readword(s,record)&127;
    for(unsigned i=0;i<tick;i++){
        if(repeat)repeat--;else{record+=4;if(!valid(record,4))return 0;repeat=readword(s,record)&127;}
        if(readword(s,record)&128)return 0;
    }
    return word(c,0x70)==record && word(c,0x78)==repeat && word(c,0x6c)==readword(s,record);
}
JgrReplayStaff*jgr_replay_staff_create(PortableReplayBootstrap*b,unsigned character)
{
    if(!b || character>=16)return NULL;
    JgrReplayStaff*s=calloc(1,sizeof *s);if(s){s->boot=b;s->character=character;}return s;
}
int jgr_replay_staff_sample(JgrReplayStaff*s,const unsigned char a[0x900],const unsigned char c[0x290],const unsigned char controller[0xd0],
    unsigned *part,unsigned *cut,unsigned *variant)
{
    if(!s || !a || !c || !controller || !part || !cut || !variant || s->count>=30000)return 0;
    uint32_t staff=staff_task(s),rowptr=readword(s,staff+0x6c),actor=word(c,0x18),tick=word(a,0x98);
    unsigned character=word(a,0x7c),replay=word(c,0x78),newpart=!s->parts || actor!=s->prior_actor || tick<s->prior_tick || rowptr!=s->prior_row;
    if(!staff || (character!=16 && character!=s->character) || rowptr<0x8cfe0014 || (rowptr-0x8cfe0000)%20 || replay!=tick || !word(a,0x560))return 0;
    unsigned row=(rowptr-0x8cfe0000)/20-1;
    if(newpart){
        if(s->parts>=9 || row!=s->parts+2 || tick<1 || tick>8 || !controller_prefix(s,row,tick,controller) || !refresh_descriptors(s))return 0;
        uint32_t*segment=s->segment[s->parts++];segment[0]=row;segment[1]=s->count;segment[2]=0;segment[3]=replay-1;segment[4]=tick;segment[5]=0;
    }else if(tick!=s->prior_tick+1)return 0;
    *part=s->parts-1;*cut=newpart || s->prior_camera!=word(c,0x274);*variant=word(a,0x560)==word(a,0x580);
    uint32_t spray[8]={s->count,word(a,0x294)!=0,*variant,0,0,0,0,0};
    if(spray[1]){
        spray[3]=word(a,0x2b8);spray[4]=word(a,0x29c);
        if(spray[3]>65535)return 0;
        for(unsigned i=0;i<3;i++)spray[5+i]=word(a,0x2a0+i*4);
        for(unsigned i=4;i<8;i++)if(!isfinite(real(spray[i])))return 0;
    }
    int changed=newpart || !s->sprays;
    if(!changed){
        uint32_t expected[8];memcpy(expected,s->spray[s->sprays-1],sizeof expected);
        if(expected[1]){float next=real(expected[4])+(float)(s->count-expected[0]);memcpy(expected+4,&next,4);}
        expected[0]=s->count;changed=memcmp(expected,spray,sizeof spray)!=0;
    }
    if(changed){if(s->sprays==MAX_SPRAYS)return 0;memcpy(s->spray[s->sprays++],spray,sizeof spray);}
    s->segment[s->parts-1][2]++;s->prior_actor=actor;s->prior_tick=tick;s->prior_row=rowptr;s->prior_camera=word(c,0x274);s->count++;
    return 1;
}
int jgr_replay_staff_frame(void*context)
{
    JgrReplayStaff*s=context;if(!s)return 0;
    if(s->count==s->observed)return 1;
    s->observed=s->count;
    /* Discover the newly loaded world's walls after its creation pass. The
     * initial state is already zero; subsequent part boundaries retain their
     * existing live tasks and are observed immediately. */
    if(s->count==1)return 1;
    unsigned n=1,visits=0,ng=0,nr=0;s->stack[0]=readword(s,0x8c2e9c9c);
    while(n && visits++<MAX_TASKS){uint32_t p=s->stack[--n];if(!valid(p,0x70))continue;
        uint32_t exec=readword(s,p+8),work=readword(s,p+0x6c);
        if(exec==0x8c03e6f8 && valid(work,0x158))s->gf[ng++]=work;
        else if(exec==0x8c03b828 || exec==0x8c03b8a8){s->revealer[nr][0]=work;s->revealer[nr++][1]=exec==0x8c03b828?1:2;}
        if(n>MAX_TASKS-3)return 0;
        s->stack[n++]=readword(s,p+0x24);s->stack[n++]=readword(s,p+0x1c);
    }
    if(n)return 0;
    for(unsigned g=0;g<ng;g++){
        uint32_t desc=readword(s,s->gf[g]+0xb8),id,state[5];if(!descriptor_id(s,desc,&id))continue;
        for(unsigned j=0;j<4;j++)state[j]=readword(s,s->gf[g]+0x144+j*4);
        state[4]=0;for(unsigned j=0;j<nr;j++)if(s->revealer[j][0]==s->gf[g])state[4]=s->revealer[j][1];
        if(state[2]>1 || state[3]>1)return 0;
        unsigned at=0;while(at<s->live_walls && s->wall[at].descriptor!=desc)at++;
        if(at<s->live_walls && !memcmp(s->wall[at].value,state,sizeof state))continue;
        if(s->wall_cues==MAX_WALL_CUES || at==MAX_LIVE_WALLS)return 0;
        uint32_t*cue=s->wall_cue[s->wall_cues++];cue[0]=s->count-1;cue[1]=id>>16;cue[2]=id&65535;memcpy(cue+3,state,sizeof state);
        if(at==s->live_walls)s->live_walls++;
        s->wall[at].descriptor=desc;memcpy(s->wall[at].value,state,sizeof state);
    }return 1;
}
static int wall_order(const void*a,const void*b)
{const uint32_t*x=a,*y=b;for(unsigned i=0;i<3;i++)if(x[i]!=y[i])return x[i]<y[i]?-1:1;return 0;}
static int emit_words(FILE*out,const uint32_t*words,unsigned count)
{
    unsigned char b[4];
    for(unsigned i=0;i<count;i++){uint32_t v=words[i];b[0]=v;b[1]=v>>8;b[2]=v>>16;b[3]=v>>24;if(fwrite(b,1,4,out)!=4)return 0;}return 1;
}
int jgr_replay_staff_finish(JgrReplayStaff*s,FILE*out)
{
    if(!s || !out || s->parts!=9 || s->count<=8000 || 32+9*24+(s->sprays+s->wall_cues)*32>128*1024)return 0;
    PortableReplayStatus status;portable_replay_bootstrap_status(s->boot,&status);if(!status.completed)return 0;
    qsort(s->wall_cue,s->wall_cues,sizeof *s->wall_cue,wall_order);
    for(unsigned i=1;i<s->wall_cues;i++)if(!wall_order(s->wall_cue[i-1],s->wall_cue[i]))return 0;
    uint32_t header[7]={1,s->character,s->count,9,s->sprays,s->wall_cues,0};
    return fwrite("SRT1",1,4,out)==4 && emit_words(out,header,7) && emit_words(out,&s->segment[0][0],9*6) &&
        emit_words(out,&s->spray[0][0],s->sprays*8) && emit_words(out,&s->wall_cue[0][0],s->wall_cues*8);
}
void jgr_replay_staff_destroy(JgrReplayStaff*s){if(s){memset(s,0,sizeof *s);free(s);}}
