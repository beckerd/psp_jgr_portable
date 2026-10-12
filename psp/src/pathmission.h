#ifndef JGR_PATHMISSION_H
#define JGR_PATHMISSION_H
#include "pathworld.h"
#include "mission.h"
#include "pathplayer.h"
/* Campaign interpreter adapter. No mission flags, progress or timings are
   manufactured here. Call attach explicitly ONLY for resident consumers.
   Normal gameplay remains gated while body response/slip/audio are missing. */
typedef struct {PathWorld world;MissionState *mission;PathPlayerContacts player_contacts;} PathCampaign;
int pathmission_attach(PathCampaign *,MissionState *);
void pathmission_detach(PathCampaign *);
#endif
