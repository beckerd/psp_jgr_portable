#ifndef JGR_STREET_PROPS_H
#define JGR_STREET_PROPS_H
#include "player.h"
#include "zones.h"
#include "stage.h"

typedef struct StreetProps StreetProps;
/* Source-audited generic families; other stage machinery has dedicated owners. */
int street_props_supported(int stage);
int street_props_load(StreetProps **owner,int stage,const Collision *,Stage *);
void street_props_free(StreetProps **owner);
void street_props_reset(StreetProps *);
/* Last completed live render camera, including debug/cutscene overrides;
   only source tasks whose registration is view-gated consume this. */
void street_props_set_view(StreetProps *,const float eye[3],const float forward[3],float tangent);
/* Sector membership and the source 30-unit player interaction range are
   independent of camera visibility. Call before the player simulation. */
void street_props_tick(StreetProps *,const Player *,const Zones *,int sector);
/* Consume the completed player pose, preserving player physics. Source
   contacts launch the rigid prop on the following logical tick. */
void street_props_contacts(StreetProps *,const Player *);
void street_props_draw(StreetProps *,const float eye[3],const float forward[3],
                       float tangent,const float clip[16],int pass);
/* Currently resident, nearby idle, hit in resident sectors, cumulative
   contacts, logical ticks. Counts reset on campaign/level reset. */
void street_props_info(const StreetProps *,unsigned out[5]);
/* Flying, retired while resident, submitted object draws, sphere probes. */
void street_props_effect_info(const StreetProps *,unsigned out[4]);
#endif
