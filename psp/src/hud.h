#ifndef JGR_HUD_H
#define JGR_HUD_H
#include "hud_state.h"
#include "can_hud.h"
#include "message_hud.h"
#include "trick_hud.h"
#include "spray_score.h"
#include "object_graffiti.h"
/* GAME's original 2D entries/textures, source screen coordinates scaled
   independently to the PSP display. No heuristic atlas cuts. */
int hud_load(const char *path);
void hud_draw_game(const HealthHud *,const CanHud *,unsigned flags,int score,int ticks,float (*sine)(int));
void hud_draw_timer(const TimerHud *,unsigned flags);
void hud_draw_notice(const HudNotice *);
void hud_draw_message(const MessageHud *);
void hud_draw_tricks(const TrickHud *);
void hud_draw_spray_score(const SprayScore *);
int hud_draw_rival_counter(const GfoProgress *,const float root[16],const float view[16]);
#endif
