#ifndef BENTEN_TRAIN_H
#define BENTEN_TRAIN_H
#include "player.h"
#include "collide.h"
typedef struct BentenTrain BentenTrain;
int benten_train_load(BentenTrain **,Collision *);
int bantam_train_load(BentenTrain **,Collision *);
/* Original mission events 27/28 scheduler and 29 route/frame/rate percent. */
int bantam_train_event(BentenTrain *,int code,unsigned route,unsigned frame,unsigned percent);
void benten_train_free(BentenTrain **);
void benten_train_reset(BentenTrain *);
/* Original 30 Hz task clock, before player collision. Never a render tick. */
int benten_train_tick(BentenTrain *,const Player *,int sector);
int benten_train_contacts(BentenTrain *,const Player *,int *mode,int *yaw);
void benten_train_draw(BentenTrain *,const float eye[3],const float planes[6][4],int pass);
void benten_train_info(const BentenTrain *,unsigned out[6]);
#endif
