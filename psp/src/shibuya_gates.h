#ifndef JGR_SHIBUYA_GATES_H
#define JGR_SHIBUYA_GATES_H
#include "player.h"
#include "stage.h"
#include "zones.h"
typedef struct ShibuyaGates ShibuyaGates;
int shibuya_gates_load(ShibuyaGates **,Stage *,Collision *);
void shibuya_gates_free(ShibuyaGates **);
void shibuya_gates_reset(ShibuyaGates *);
int shibuya_gates_tick(ShibuyaGates *,const Player *,const Zones *,int sector);
void shibuya_gates_contact(void *,int triangle);
void shibuya_gates_draw(ShibuyaGates *,const float eye[3],const float forward[3],const float planes[6][4],int pass);
void shibuya_gates_info(const ShibuyaGates *,unsigned out[3]);
#endif
