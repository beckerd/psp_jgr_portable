/* Synthetic packing bounds, full-row equality and stdio-call regression. */
#include "portable_replay_pack.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

static FILE *counted;
static unsigned calls,fail_at;
size_t pack_fwrite(const void *data,size_t size,size_t count,FILE *stream)
{
    if(stream==counted && ++calls==fail_at)return count?count-1:0;
    return fwrite(data,size,count,stream);
}
static void word(unsigned char *p,uint32_t x)
{p[0]=x;p[1]=x>>8;p[2]=x>>16;p[3]=x>>24;}
static unsigned value(const unsigned char *p)
{return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static int varies(unsigned pattern,unsigned k)
{return pattern==1?k==119:pattern==2?(k%7==3):pattern==3;}

static void fixture(unsigned sections,unsigned pattern,unsigned fail)
{
    FILE *rows=tmpfile(),*out=tmpfile();assert(rows&&out);
    JgrReplayPack *pack=jgr_replay_pack_create_native(rows,15);assert(pack);
    if(sections)assert(!jgr_replay_pack_rival(pack,sections));
    unsigned channels=0;for(unsigned k=0;k<120;k++)channels+=varies(pattern,k);
    for(unsigned i=0;i<3;i++){
        JgrReplayNativeSample sample={0};float target[3]={.25f,.5f,.75f};
        sample.tick=i;sample.part=sections?i:0;sample.rival_target=target;
        sample.effects.obstruction=sections && i==1;sample.effects.motion=100+i;
        for(unsigned k=0;k<120;k++){
            float v=(float)k+(varies(pattern,k)?(float)i:0.f);
            if(k%6<3)sample.pose[k/6].position[k%6]=v;
            else sample.pose[k/6].scale[k%6-3]=v;
        }
        assert(!jgr_replay_pack_native_sample(pack,&sample));
    }
    counted=out;calls=0;fail_at=fail?3+channels:0;
    int status=jgr_replay_pack_finish(pack,out);counted=NULL;
    if(fail){
        assert(status==-1);assert(calls==fail_at);
        assert(jgr_replay_pack_finish(pack,out)==-1);
    }else{
        assert(!status);assert(calls==2+channels+3);
        unsigned stride=428+channels*4+(sections?12:0);
        unsigned bytes=512+channels*4+stride*3;
        assert(ftell(out)==(long)bytes);assert(!fseek(out,0,SEEK_SET));
        unsigned char *actual=malloc(bytes),*expected=calloc(1,bytes);assert(actual&&expected);
        assert(fread(actual,1,bytes,out)==bytes);
        memcpy(expected,sections?"TRT3":"TRT2",4);
        word(expected+4,3);word(expected+8,15);word(expected+12,20);
        word(expected+16,stride);word(expected+20,channels);
        if(sections){word(expected+24,1);word(expected+28,2);}
        unsigned at=512;
        for(unsigned k=0;k<120;k++){
            float v=(float)k;uint32_t bits;memcpy(&bits,&v,4);word(expected+32+k*4,bits);
            if(varies(pattern,k)){word(expected+at,k);at+=4;}
        }
        for(unsigned i=0;i<3;i++){
            unsigned char *row=expected+at;word(row+56,i);
            if(sections)row[60]=(unsigned char)i;
            unsigned position=224;
            for(unsigned k=0;k<120;k++)if(varies(pattern,k)){
                float v=(float)k+(float)i;uint32_t bits;memcpy(&bits,&v,4);word(row+position,bits);position+=4;
            }
            word(row+position+4,100+i);word(row+position+24,sections&&i==1);
            if(sections&&i==1){word(row+position+204,0x3e800000);word(row+position+208,0x3f000000);word(row+position+212,0x3f400000);}
            at+=stride;
        }
        assert(at==bytes);assert(value(actual+16)==stride);assert(!memcmp(actual,expected,bytes));
        free(actual);free(expected);
    }
    jgr_replay_pack_destroy(pack);fclose(rows);fclose(out);
}
int main(void)
{
    for(unsigned sections=0;sections<=3;sections+=3)
        for(unsigned pattern=0;pattern<4;pattern++){
            fixture(sections,pattern,0);fixture(sections,pattern,1);
        }
    return 0;
}
