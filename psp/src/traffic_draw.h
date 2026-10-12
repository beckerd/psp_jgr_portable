#ifndef JGR_TRAFFIC_DRAW_H
#define JGR_TRAFFIC_DRAW_H
#include "charanim.h"
#include "object_view.h"
#include "pathworld.h"

typedef struct {
    unsigned frames,candidates,source_rejected,frustum_rejected,close_kept;
    unsigned drawn,near_drawn,far_drawn,posed_bones,triangles,us,sample_prepares;
    unsigned bounds_prepares,state_batches;
} TrafficDrawProfile;

/* 06b920 uses circle 0c3498 (stage_render_medium_circle, slot 0) and the
   0c3344 wedge whose apex sits tag_back behind the eye. The stage owns the
   circle's live range, including Kogane's sticky sector overrides. Set
   objects (091f64) share this view. */
static inline void traffic_view_setup(ObjectView *v,const ObjectView *objects,float ahead,float radius)
{
    *v=*objects;
    if(!isfinite(ahead) || !isfinite(radius) || radius<=0.f){v->valid=0;return;}
    v->cx=v->eye[0]+v->fx*ahead;v->cz=v->eye[2]+v->fz*ahead;
    v->radius2=radius*radius;
}
static inline int traffic_view_visible(const ObjectView *v,const float position[3],int close_draw)
{
    /* 06baa0 schedules 06bc4c inside player distance squared 2500.
       That pass bypasses the ordinary task gates. Keep its visible union
       in our single vehicle pass; simulation/contacts never use this test. */
    return close_draw || object_prop_visible(v,position[0],position[1],position[2]);
}
void traffic_draw_world(Chars *,PathWorld *,const ObjectView *,const float planes[6][4]);
void traffic_draw_batch_mode(int enabled); /* diagnostic reference; default on */
void traffic_draw_profile(TrafficDrawProfile *); /* read and reset */
void traffic_draw_totals(TrafficDrawProfile *); /* current interval; does not reset */
#endif
