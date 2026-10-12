#ifndef JGR_CANPLAY_H
#define JGR_CANPLAY_H
#include "player.h"
/* 04dcdc static task kernel. Contact is the PREVIOUS collision pass. */
void can_task_tick(Can *can,int contact,float frames);
/* Source-body adapter; at most one 051f0c can award per logical tick. */
int canplay_update(Graffiti *,Player *,const Chars *can_bank,float dt);
/* Register next-tick queries from the completed actor's collision pose. */
void canplay_refresh_contacts(Graffiti *,const Player *);
#endif
