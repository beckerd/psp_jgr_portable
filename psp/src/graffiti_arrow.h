#ifndef JGR_GRAFFITI_ARROW_H
#define JGR_GRAFFITI_ARROW_H
#include "graffiti.h"
#include "charanim.h"
#include "object_graffiti.h"
/* 03e8a4: source descriptor position/scale, camera-facing model. Input and
   output matrices are PSP row-major. This does not place gameplay targets. */
int graffiti_arrow_model(const Spot *,const float view[16],const float eye[3],float root[16]);
int graffiti_arrows_draw(Chars *,const Graffiti *,const float view[16],const float eye[3],int hidden);
#include "object_view.h"
int graffiti_arrows_draw_view(Chars *,const Graffiti *,const float view[16],const float eye[3],int hidden,const ObjectView *);
/* All matrices are row-major. Parent is the ORIGINAL draw attachment, not
   the tag mesh's descriptor TRS or the last member's interaction volume. */
void graffiti_arrow_billboard(const float descriptor[6],const float parent[16],const float view[16],float out[16]);
int graffiti_arrow_draw_at(Chars *,const float descriptor[6],const float parent[16],
                          const float view[16],int required,float frame);
int graffiti_arrow_draw_object(Chars *,const GfoObject *,const float parent[16],
                              const float view[16],int required,float frame,int hidden);
int graffiti_arrows_draw_final(Chars *,const GfoObject objects[5],const Graffiti *,const float view[16],int hidden);
const float *graffiti_arrow_skater(unsigned character);
int graffiti_arrow_rival_enabled(int mission_kind,int controller_table);
#endif
