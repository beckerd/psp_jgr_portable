#ifndef JGR_KOGANE_COVER_H
#define JGR_KOGANE_COVER_H
#include "charanim.h"
#include "collide.h"
#include "zones.h"
typedef struct {Chars models;Collision *collision;int handle,closed;} KoganeCover;
int kogane_cover_load(KoganeCover *,Collision *);
void kogane_cover_free(KoganeCover *);
int kogane_cover_select(KoganeCover *,unsigned mission_flags);
void kogane_cover_draw(KoganeCover *,const Zones *,int sector,const float eye[3],const float planes[6][4],int pass);
#endif
