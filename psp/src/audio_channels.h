#ifndef JGR_AUDIO_CHANNELS_H
#define JGR_AUDIO_CHANNELS_H
#include <stdint.h>
/* 0b1b68: eight SOSP slots, eight SMSB slots and ADX slot 3.
   These are wrapper channels, NOT the ARM driver's 64 AICA note voices. */
typedef struct {
    uintptr_t owner[17];
    unsigned char protected_owner[17],next[4];
} SourceAudioChannels;
static inline int source_audio_protected(int bank,int program)
{return (bank==0 && (program==12 || program==13 || program==15)) ||
        ((bank==4 || bank==5) && program==0);}
static inline int source_audio_channel_acquire(SourceAudioChannels *s,
    uintptr_t owner,unsigned cls,int protect,uintptr_t *displaced)
{
    int slot=-1,pair=-1,first=0;
    *displaced=0;if(!owner)return -1;
    if(cls<8)slot=(int)cls;
    else switch(cls){
    case 8:pair=0;first=3;break;
    case 9:slot=8;break;
    case 10:slot=11;break;case 11:slot=12;break;
    case 12:slot=13;break;case 13:slot=14;break;case 14:slot=15;break;
    case 15:pair=1;first=9;break;case 16:pair=2;first=11;break;
    case 17:pair=3;first=13;break;case 18:slot=16;break;
    default:return -1;
    }
    if(pair>=0){
        slot=!s->owner[first]?first:!s->owner[first+1]?first+1:first+s->next[pair];
        /* Original updates the alternating cursor even when a protected
           incumbent subsequently rejects this request. */
        s->next[pair]=(unsigned char)(slot==first);
    }
    if(s->owner[slot] && s->protected_owner[slot])return -1;
    *displaced=s->owner[slot];s->owner[slot]=owner;s->protected_owner[slot]=!!protect;
    return slot;
}
static inline void source_audio_channel_release(SourceAudioChannels *s,uintptr_t owner)
{
    if(!owner)return;
    for(unsigned i=0;i<17;i++)if(s->owner[i]==owner){s->owner[i]=0;s->protected_owner[i]=0;}
}
#endif
