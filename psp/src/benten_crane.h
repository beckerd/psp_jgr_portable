#ifndef BENTEN_CRANE_H
#define BENTEN_CRANE_H
#include "player.h"
#include "zones.h"
#include "object_view.h"
typedef struct BentenCrane BentenCrane;
int benten_crane_load(BentenCrane **,Collision *);
void benten_crane_free(BentenCrane **);
void benten_crane_reset(BentenCrane *);
int benten_crane_tick(BentenCrane *,const Player *,const Zones *,int);
void benten_crane_draw(BentenCrane *,const float planes[6][4],const ObjectView *medium,int);
void benten_crane_info(const BentenCrane *,unsigned out[5]);
#endif
