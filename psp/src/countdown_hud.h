#ifndef JGR_COUNTDOWN_HUD_H
#define JGR_COUNTDOWN_HUD_H
#include "charpose.h"
/* Source 050260, GAME cells 1bc/1c4/1cc/1d4. Call after world draws. */
void countdown_hud_draw(Chars *,int model,const float projection[16],const float view[16]);
#endif
