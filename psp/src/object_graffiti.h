#ifndef OBJECT_GRAFFITI_H
#define OBJECT_GRAFFITI_H
#include <stddef.h>
#include <stdint.h>
#include "charanim.h"

typedef struct { uint32_t stroke,reveal,complete,started,busy; } GfoProgress;
typedef struct { int score,clear_target,empty,sound,task,trial,accepted,large; } GfoPress;
/* 01abb8. Allocation, sound and player animation are emitted as effects. */
GfoPress gfo_press(GfoProgress *p,unsigned kind,unsigned anchors,int *cans,
                   unsigned player_flags,int edge,int blocked);
uint32_t gfo_colour(const GfoProgress *p,uint32_t word,int mode); /* 0c5128 */
typedef struct { float u,v; uint32_t reveal; float pos[3]; unsigned bone; } GfoVertex;
typedef struct {
    uint32_t bank,chain,index,source,kind,nvertex,flags,nbones;
    unsigned char descriptor[228]; /* original descriptor +4..e8 */
} GfoTemplate;
static inline const JgcBone *gfo_bones(const GfoTemplate *t) { return (const JgcBone *)(t+1); }
static inline const GfoVertex *gfo_vertices(const GfoTemplate *t) { return (const GfoVertex *)(gfo_bones(t)+t->nbones); }
typedef struct {
    unsigned handle,parent; const GfoTemplate *tpl;
    GfoProgress progress; int task,registered,visible;
    float matrix[16],center[3],normal[3],volume[36];
} GfoObject;
#define GFO_CAPACITY 64
typedef struct {
    const unsigned char *data; size_t size;
    const GfoTemplate *templates[64]; unsigned count,serial,target;
    GfoObject objects[GFO_CAPACITY];
} GfoWorld;
/* Caller owns aligned immutable bank bytes; validates entire bank first. */
int gfo_bank(GfoWorld *w,const void *data,size_t size);
/* Append immutable owner templates without resetting live reveal tasks. */
int gfo_add_bank(GfoWorld *w,const void *data,size_t size);
void gfo_reset(GfoWorld *w);
unsigned gfo_create(GfoWorld *w,unsigned parent,unsigned bank,unsigned chain,unsigned index);
void gfo_delete_parent(GfoWorld *w,unsigned parent);
GfoObject *gfo_find(GfoWorld *w,unsigned handle);
unsigned gfo_word(GfoWorld *w,unsigned handle,unsigned offset);
void gfo_transform(GfoObject *o,const float matrix[16]); /* 0c4ab4, column-major */
/* 0c48dc: LAST registered containing volume wins, no guessed radius/heading. */
int gfo_contains(const GfoObject *o,const float position[3]);
void gfo_select(GfoWorld *w,const float position[3]);
/* Reveals survive leaving the volume, but not deletion of their owner. */
unsigned gfo_tick(GfoWorld *w); /* bit 1: 08e7f0; bit 2: 0186b4 */
GfoPress gfo_spray(GfoWorld *w,int *cans,unsigned player_flags,int edge,int blocked);
/* GU binding; supplied GRF small art is ARGB4444 (095380, 1ba578[0]). */
unsigned short *gfo_texture_load(const char *path);
void gfo_draw(const GfoWorld *w,const unsigned short *texture);
/* One shared GF can be drawn by several members, each with its own pose.
   root is row-major, motion is local to the owner character. */
void gfo_draw_actor(const GfoObject *,const unsigned short *texture,const Chars *,
                    int character,int motion,float frame,const float root[16]);

/* PLAY skaters supply their completed/blended local channels (019cfc). */
void gfo_draw_actor_pose(const GfoObject *,const unsigned short *texture,
                         const struct CharPose *,const float root[16]);

#endif
