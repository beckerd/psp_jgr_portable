#ifndef GRIND_PROPS_H
#define GRIND_PROPS_H
#include "player.h"
#include "object_view.h"
typedef struct GrindProps GrindProps;
int grind_props_load(GrindProps **,Collision *);
void grind_props_free(GrindProps **);
void grind_props_reset(GrindProps *);
int grind_props_tick(GrindProps *,const Player *);
void grind_props_contacts(GrindProps *,const Player *);
void grind_props_draw(GrindProps *,const float eye[3],const float planes[6][4],const float clip[16],
                      const ObjectView *medium,int pass,int advance);
void grind_props_info(const GrindProps *,unsigned out[6]);
#endif
