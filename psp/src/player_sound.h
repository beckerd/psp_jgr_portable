#ifndef JGR_PLAYER_SOUND_H
#define JGR_PLAYER_SOUND_H
#include <stdint.h>
#include <string.h>
/* Requests from the original movement/animation producers. RESET is a
   platform owner-lifetime operation, never a Dreamcast program number. */
enum {PLAYER_SOUND_RESET,PLAYER_SOUND_VOICE,PLAYER_SOUND_EFFECT,
      PLAYER_SOUND_GRIND,PLAYER_SOUND_SPRAY,PLAYER_SOUND_SYSTEM};
enum {PLAYER_VOICE_COLLISION,PLAYER_VOICE_DAMAGE,PLAYER_VOICE_TRICK,
      PLAYER_VOICE_JUMP,PLAYER_VOICE_GRIND};
#if JGR_PORTABLE_ASSETS
#include "source_data_player_sound_tables.inc"
#else
#include "generated/source_data/player_sound_tables.inc"
#endif
/* 01ae30: shared with trick-pose selection, including unsigned overflow. */
static inline unsigned source_player_random(unsigned *seed,float speed,unsigned tick,unsigned modulus)
{
    uint32_t bits;memcpy(&bits,&speed,4);unsigned before=*seed&65535;
    *seed=(before*0x103)&65535;return (uint32_t)(before+bits+tick)%modulus;
}
/* 05cc24/58, 05ccdc, 05cd10, 05cd44, 05cc8c. A missing audio device or
   muted playback must NOT skip the RNG call. -1 is an intentional silence. */
static inline int source_player_voice(int kind,unsigned *seed,float speed,unsigned tick,unsigned *grind_ticks)
{
    if(kind==PLAYER_VOICE_GRIND){
        if(++*grind_ticks!=30)return -1;
        unsigned r=source_player_random(seed,speed,tick,3);return r<2?(int)r:-1;
    }
    unsigned r=source_player_random(seed,speed,tick,kind==PLAYER_VOICE_JUMP?3:2);
    return kind==PLAYER_VOICE_COLLISION ? (r?5:6):kind==PLAYER_VOICE_DAMAGE ? (r?3:4):
           kind==PLAYER_VOICE_TRICK ? (r?0:1):7+(int)r;
}
/* 03ed98 samples integer frames on either side of the advance. It does
   NOT fire once per skipped frame, nor twice for simultaneous contacts. */
static inline int source_player_foot_contact(int type,int before,int after)
{
    if((unsigned)before>=28 || (unsigned)after>=28)return 0;
    int row=(type==3 || type==4)?0:type==8?2:type==9?4:-1;
    if(row<0)return 0;
    return (!PLAYER_FOOT_CONTACT[row][before] && PLAYER_FOOT_CONTACT[row][after]==1) ||
        (type!=9 && !PLAYER_FOOT_CONTACT[row+1][before] && PLAYER_FOOT_CONTACT[row+1][after]==1);
}
/* 05cf14: boost wins over the +548 contact override, then surface table. */
static inline int source_player_skate_sound(unsigned surface,int boost,int contact)
{return boost?17:contact?15:surface<64?PLAYER_SURFACE_SOUND[surface]:-1;}
static inline int source_player_grind_sound(unsigned surface)
{return surface==3 || surface==15 ? 1:2;} /* list 10dbdc */
#endif
