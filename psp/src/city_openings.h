#ifndef JGR_CITY_OPENINGS_H
#define JGR_CITY_OPENINGS_H
#include "player.h"
#include "zones.h"
#include "stage.h"
typedef struct CityOpenings CityOpenings;
int city_openings_load(CityOpenings **,int stage,Collision *,Stage *);
void city_openings_free(CityOpenings **);
void city_openings_reset(CityOpenings *);
int city_openings_tick(CityOpenings *,const Player *,const Zones *,int sector);
void city_openings_contacts(CityOpenings *,const Player *,unsigned actor);
void city_openings_surface(void *,int triangle);
void city_openings_draw(CityOpenings *,const float eye[3],const float forward[3],const float planes[6][4],int pass);
void city_openings_info(const CityOpenings *,unsigned out[4]);
#endif
