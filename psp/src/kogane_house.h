#ifndef JGR_KOGANE_HOUSE_H
#define JGR_KOGANE_HOUSE_H
#include "player.h"
#include "zones.h"
#include "object_view.h"
typedef struct KoganeHouse KoganeHouse;
int kogane_house_load(KoganeHouse **);
void kogane_house_free(KoganeHouse **);
void kogane_house_reset(KoganeHouse *);
void kogane_house_tick(KoganeHouse *,const Player *,const Zones *,int sector);
void kogane_house_contacts(KoganeHouse *,const Player *);
/* Opaque pass 0 precedes actors; translucent pass 1 follows opaque geometry. */
void kogane_house_draw(KoganeHouse *,const float eye[3],const float forward[3],const float planes[6][4],
                       const ObjectView *small,const ObjectView *medium,int pass);
/* Read-only fixture observations; no state/progress injection. */
void kogane_house_info(const KoganeHouse *,unsigned out[4]);
#endif
