#include "portable_replay_demo_table.h"
#include "portable_tables.h"
#include <string.h>
typedef struct {const char *mission,*path,*label;unsigned subset,character;} DemoSelector;
#include "portable_replay_demo_table_data.inc"
typedef struct {const unsigned char *const *observed;const unsigned *sizes;} Observed;
static unsigned rw(const unsigned char *p){return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static unsigned rh(const unsigned char *p){return p[0]|(unsigned)p[1]<<8;}
static void rp(unsigned char *p,unsigned v){for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(i*8));}
static int rstring(unsigned char *pool,unsigned capacity,unsigned *used,const char *s,unsigned *offset)
{
    unsigned n=(unsigned)strlen(s)+1,at=0;
    while(at<*used){const unsigned char *end=memchr(pool+at,0,*used-at);if(!end)return -1;unsigned length=(unsigned)(end-pool-at)+1;if(length==n&&!memcmp(pool+at,s,n))break;at=(at+length+3)&~3u;}
    if(at>capacity||n>capacity-at)return -1;
    if(at>=*used){memcpy(pool+at,s,n);*used=at+n;}*offset=at;return 0;
}
static void rfix(unsigned char *blob,unsigned index,unsigned member,unsigned target,unsigned offset)
{
    unsigned char *f=blob+40+40*16+index*16;rp(f,38);rp(f+4,member);rp(f+8,target);rp(f+12,offset);
}
static int aggregate(JgrDisc *disc,const char *name,const JgrTableSection *sections,unsigned count,unsigned char *data,unsigned size,void *user)
{
    (void)disc;(void)name;Observed *input=user;unsigned pool_used=0;
    if(count!=40||size<40+40*16+95*16||sections[38].size!=19*36)return -1;
    for(unsigned i=0;i<19;i++){
        const unsigned char *raw=input->observed[i];unsigned bytes=input->sizes[i];if(!raw||bytes<16||memcmp(raw,"RDC1",4))return -1;
        unsigned audio=rw(raw+4),sprays=rw(raw+8),tick=rw(raw+12);if(audio>8192||!sprays||sprays>8192||!tick||tick>=65536||16+audio*4+sprays*24!=bytes||sections[i*2].size!=audio*4||sections[i*2+1].size!=sprays*24)return -1;
        unsigned previous=0;
        for(unsigned j=0;j<audio;j++){const unsigned char *cue=raw+16+j*4;unsigned t=rh(cue),kind=cue[2];int program=(signed char)cue[3];if(t<previous||t>tick||kind<1||kind>4||program< -1||(program<0&&kind<3))return -1;
            previous=t;}
        previous=0;
        for(unsigned j=0;j<sprays;j++){
            const unsigned char *cue=raw+16+audio*4+j*24;unsigned t=rh(cue),ref=rh(cue+2);
            if(!t||t<=previous||t>tick||cue[4]>1||cue[5]>1||cue[6]||cue[7]||(cue[4]&&(ref<160||ref>166)))return -1;
            for(unsigned k=0;k<4;k++)if((rw(cue+8+k*4)&0x7f800000u)==0x7f800000u)return -1;
            previous=t;
        }
        memcpy(data+sections[i*2].offset,raw+16,audio*4);memcpy(data+sections[i*2+1].offset,raw+16+audio*4,sprays*24);
        unsigned char *record=data+sections[38].offset+i*36;const DemoSelector *selector=demo_selectors+i;rp(record+12,selector->subset);rp(record+16,selector->character);rp(record+24,audio);rp(record+32,sprays);
        const char *strings[3]={selector->mission,selector->path,selector->label};
        for(unsigned j=0;j<3;j++){unsigned at;if(rstring(data+sections[39].offset,sections[39].size,&pool_used,strings[j],&at))return -1;rfix(data,i*5+j,i*36+j*4,39,at);}
        rfix(data,i*5+3,i*36+20,i*2,0);rfix(data,i*5+4,i*36+28,i*2+1,0);
    }
    return pool_used==sections[39].size?0:-1;
}
int jgr_replay_demo_table(JgrDisc *disc,const unsigned char *observed[19],const unsigned sizes[19],FILE *out)
{
    if(!disc||!observed||!sizes||!out)return -1;
    Observed context={observed,sizes};
    return jgr_convert_table_fill(disc,"source_data_ptr_challenge_demo_data.jsd",out,aggregate,&context);
}
