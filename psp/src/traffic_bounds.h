#ifndef JGR_TRAFFIC_BOUNDS_H
#define JGR_TRAFFIC_BOUNDS_H
#include "charanim.h"
#include "scene.h"
/* Bound around the already sampled/validated animated first root position.
   Root translation cancels; all child translation/scale keys are bounded.
   Returns infinity on unsupported hierarchy/channel spans or nonfinite
   contributing keys so the caller always draws. */
float traffic_motion_radius(const Chars *,unsigned,const Scene *,unsigned);
/* Same-build diagnostic: disable to retain the original root-key walk. */
void traffic_bounds_mode(int enabled);
#endif
