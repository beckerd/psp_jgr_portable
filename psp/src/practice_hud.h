#ifndef JGR_PRACTICE_HUD_H
#define JGR_PRACTICE_HUD_H
#include "pause_hud.h"
#include "list2d.h"
typedef struct {Chars models;Tex2dSet textures;List2dSet lists;} PracticeHud;
int practice_hud_load(PracticeHud *);
void practice_hud_free(PracticeHud *);
void practice_hud_draw(PracticeHud *,const PauseHud *,const float[16],const float[16]);
#endif
