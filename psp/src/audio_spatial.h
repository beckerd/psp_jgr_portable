#ifndef JGR_AUDIO_SPATIAL_H
#define JGR_AUDIO_SPATIAL_H
#include <stdint.h>
#include <math.h>
typedef struct {int backend;float range,far_fraction,near_fraction;} SourceAudioProfile;
typedef struct {
    int mode,override,scene,bank,program;
    int sosb_min,sosb_max,adx_min,adx_max;
} SourceAudioContext;
typedef struct {int volume,pan;} SourceAudioLevels;
typedef struct {float distance,horizontal;int direction,angle,sign;} SourceSoundPosition;
/* 02d140: source-minus-listener and the original *unwrapped* BAM
   difference. Supply 103df0, not libc atan2 or a shortest-angle helper. */
static inline SourceSoundPosition source_sound_position(const float source[3],const float listener[3],
                                                        int yaw,int (*atan_bam)(float,float))
{
    float x=source[0]-listener[0],y=source[1]-listener[1],z=source[2]-listener[2];
    SourceSoundPosition p={sqrtf((x*x+y*y)+z*z),sqrtf(x*x+z*z),atan_bam(x,z),0,0};
    if(p.direction<0)p.direction+=65536;
    int difference=p.direction-yaw;
    p.sign=difference>0?-1:difference<0?1:0;p.angle=difference<0?-difference:difference;
    return p;
}
/* 0e6ee0, including 0e7be4's special cases and 0e7c88's forced-centre
   program. angle/sign are 02d140's UNWRAPPED difference, not shortest yaw. */
static inline SourceAudioLevels source_audio_levels(const SourceAudioProfile *p,const SourceAudioContext *c,
                                                     float distance,int angle,int sign)
{
    int adx=p->backend==2,lo=-60,hi=127,alo=-280,ahi=0;
    if(c->mode==1 && !c->override){lo=c->sosb_min;hi=c->sosb_max;alo=c->adx_min;ahi=c->adx_max;}
    if((c->bank==16 && c->program==18)||(c->bank==17 && c->program==4))hi-=50;
    if(c->mode==1 && !c->override){if(c->scene==16)hi=-127;else if(hi< -40)hi=-40;}
    if(adx){lo=alo;hi=ahi;}
    SourceAudioLevels out={hi,0};
    if(p->range!=-1.f && c->mode!=0) {
        float far=p->range*(1.f-p->far_fraction),near=p->range*p->near_fraction;
        if(!(far>distance))out.volume=lo;
        else if(distance>near) {
            float tail=p->range*p->far_fraction;
            float factor=((p->range-distance)-tail)/(p->range-(near+tail));
            float value=(float)(hi-lo)*factor;value+=(float)lo;out.volume=(int)value;
        }
        int bound=adx?15:127;
        if(angle>0 && angle<16384)out.pan=sign*(int)(((float)angle*bound)/16384.f);
        else if(angle>16384 && angle<32768)out.pan=sign*(int)(((32768.f-angle)*bound)/16384.f);
        else if(angle==16384)out.pan=bound; /* original ignores sign at this exact angle */
    }
    if(c->bank==4 && c->program==0)out.pan=0;
    return out;
}
#if JGR_PORTABLE_ASSETS
#include "source_data_audio_spatial_tables.inc"
#else
#include "generated/source_data/audio_spatial_tables.inc"
#endif
static inline int source_audio_clamp(int value,int maximum)
{return value<0?0:value>maximum?maximum:value;}
/* CRI 21e600/21e69e -> PSTM 2a1b48/2a17c4. Preserve both signed
   integer divisions; this is not a continuous decibel approximation. */
static inline int source_adx_driver_volume(int volume)
{
    if(volume<=-959)return -127;
    if(volume>=0)return 127;
    int q=(128-volume*255)/-959;
    int v=(q*255)/168;
    return v< -127 ? -127:v>127 ? 127:v;
}
static inline int source_adx_driver_pan(int pan)
{if(pan< -15)pan=-15;if(pan>15)pan=15;return (pan*127)/15;}
static inline void source_adx_registers(int volume,int pan,unsigned *level,unsigned *total)
{
    /* Original CRI PSTM command 21 starts with base volume 254, centre
       64 and direct send 127. MANATEE 61f4/6280 apply the live offsets. */
    unsigned tl=MANATEE_TL[source_audio_clamp(254+2*source_adx_driver_volume(volume),255)];
    *level=(MANATEE_SEND[63]<<8)|MANATEE_PAN[source_audio_clamp(64+source_adx_driver_pan(pan),127)];
    *total=tl<<8;
}
/* Original ARM7 driver: SOSP start 53ec/54b8, volume command 5640,
   pan command 56cc. signed API params are packed +128 by 29d754/29d658.
   Returned registers permit direct comparison to observed AICA channels. */
static inline void source_osb_registers(const unsigned char sosp[64],int volume,int pan,
                                        unsigned *level,unsigned *total)
{
    int v=(int)(int8_t)volume,p=(int)(int8_t)pan;
    unsigned tl=MANATEE_TL[source_audio_clamp(255-sosp[25]+2*v,255)];
    unsigned balance=MANATEE_PAN[source_audio_clamp(MANATEE_PAN_BASE[sosp[22]&31]+p,127)];
    unsigned send=MANATEE_SEND[(sosp[23]&15)*4];
    *level=(send<<8)|balance;*total=(tl<<8)|sosp[24];
}
static inline void source_aica_gains(unsigned level,unsigned total,unsigned gains[2])
{
    static const unsigned send[16]={255,112,104,96,88,80,72,64,56,48,40,32,24,16,8,0};
    unsigned full=((total&64)?0:(total>>8)&255)+send[(level>>8)&15];
    unsigned pan=full+send[(~level)&15];
    unsigned a=full<255?AICA_TL_GAIN[full]:0,b=pan<255?AICA_TL_GAIN[pan]:0;
    gains[0]=(level&16)?a:b;gains[1]=(level&16)?b:a;
}
/* 16-bit decoded sample -> AICA's 20-bit DSP input, before stereo pan. */
static inline int source_aica_dsp(int sample,unsigned total,unsigned send)
{
    static const unsigned attenuation[16]={255,112,104,96,88,80,72,64,56,48,40,32,24,16,8,0};
    unsigned tl=total+attenuation[send&15];
    return tl>=255?0:(int)(((int64_t)sample*AICA_TL_GAIN[tl])>>11);
}
#endif
