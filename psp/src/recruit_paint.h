#ifndef JGR_RECRUIT_PAINT_H
#define JGR_RECRUIT_PAINT_H
#include "graffiti.h"
#include "player.h"

/* Recruitment routes contain completed actor/pose/camera callbacks. The
 * recorded spray edge can therefore see state 5 already published. Retain
 * the normal airborne/grind rejection, admitting only that recorded entry. */
static inline int recruit_paint_block_large(const Player *p,int spray_pressed,int police_gripped)
{return police_gripped || (p->state!=1 && !(p->state==5 && spray_pressed));}

/* The live graffiti kernel owns paint, input, score and completion. Playback
 * owns the actor, camera, effects and audio: never run these callbacks twice. */
static inline void recruit_paint_actor(void *context,Graffiti *g,Spot *s,int action,int cue)
{(void)context;(void)g;(void)s;(void)action;(void)cue;}

/* Call once AFTER recorded input is evaluated, never from interpolated draw
 * samples. The route's completed animation is already a tick ahead of that
 * input; 03bd08 consumes the preceding animation's 0407d4 reveal increment. */
static inline void recruit_paint_after_input(Graffiti *g,const Player *p)
{g->reveal_step=player_recorded_spray_reveal_step(p);}
#endif
