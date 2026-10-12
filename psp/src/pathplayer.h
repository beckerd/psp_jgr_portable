#ifndef JGR_PATHPLAYER_H
#define JGR_PATHPLAYER_H
#include "pathworld.h"
#include "player.h"
/* Diagnostic player-side traffic contact publication. Does not apply the
   still-unrecovered 04248c damage/recovery graph or the separate slip path. */
typedef struct {
    float previous[20][3];int valid;
    unsigned contacts,limbs,handle,group,samples;
    float speed;int mode,yaw;
} PathPlayerContacts;
int pathplayer_sample(PathPlayerContacts *,const PathWorld *,const Player *);
#endif
