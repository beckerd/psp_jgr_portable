#ifndef JGR_PAUSE_MAP_H
#define JGR_PAUSE_MAP_H
#include "charanim.h"
typedef struct {float x,y,z,scale;int models[6];} PauseMapEntry;
typedef struct {int stage,region,layout,clock,kind,chapter,remaining;} PauseMap;
typedef struct {float x,z;int angle,icon;float size,depth;} PauseMapMarker;
/* 034c14. The full-area indices are not the number of streamed zones. */
static inline int pause_map_region(int stage,int chapter,int kind,int area)
{
    const int limit[3]={4,4,5};if((unsigned)stage>=3)return 0;
    if(area>limit[stage] || chapter==15 || chapter==18 || chapter==20 || kind==7 || kind==8 || (!stage && !area))return limit[stage];
    return area;
}
/* 03355c: mission code, not the filename's Mxx or chain ordinal. */
static inline int pause_map_layout(int chapter,int region)
{
    switch(chapter){case 1:case 6:return 2;case 2:return 3;
    case 3:case 7:case 9:return 1;case 5:case 10:return 0;
    case 11:case 15:case 18:return 4;case 20:return 5;default:return region;}
}
/* 0340d4 enumerates 03e198, which filters tag task +b4 (required).
   Optional green world arrows must never become red map objectives. */
static inline int pause_map_tag_visible(const PauseMap *p,int area,int required,int complete)
{
    const int all[6]={4,4,5,-1,-1,-1};
    if((unsigned)p->stage>=6)return 0;
    return required && !complete && (p->chapter==10 || p->region==all[p->stage] || p->region==area);
}
static inline int pause_map_rival_icon(int character)
{return character==10 || character==15 ? 5:character==11 ? 6:character==12 ? 7:1;}
/* 06ff90 registrations: 085ba0 Onishima, 0a5da4 helicopters,
   07ba50 SWAT, 07de6c blue SWAT, 099cd8 blue Rocket. Ordinary
   pedestrian/wave actors and red Rocket (kind 8) do not register. */
static inline int pause_map_enemy_icon(int kind,unsigned flags,int animation)
{
    if(flags&0x120)return -1;
    if(kind==17)return 3;
    if(kind==13 || kind==15 || kind==32)return flags&0x1000000 ? -1:8;
    if(kind==23)return animation==20 ? -1:4;
    if(kind==24 || kind==25)return 4;
    return -1;
}
void pause_map_marker_matrix(const PauseMapEntry *,const PauseMapMarker *,float (*sine)(int),float[16]);
struct Graffiti;
void pause_map_select(PauseMap *,int area,const struct Graffiti *);
int pause_map_load(Chars *);
const PauseMapEntry *pause_map_entry(const PauseMap *);
void pause_map_draw(Chars *,const PauseMap *,const PauseMapMarker *,int,float (*sine)(int),const float[16],const float[16]);
void pause_map_text(const PauseMap *);
#endif
