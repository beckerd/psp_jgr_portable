#ifndef JGR_SOURCE_LOCAL_LIGHT_H
#define JGR_SOURCE_LOCAL_LIGHT_H
#include <string.h>
#include "render_dot.h"

typedef struct {float direction[3];unsigned valid;} SourceLocalLightKey;

/* Matrix ownership is one actor draw: the caller clears valid at each actor
   entry, including separate material passes. Normal and cel batches can share
   the existing vector table, but only while the direction bits match. Compare
   bits so signed zero and every source light parameter remain observable. */
static inline int source_local_light_begin(SourceLocalLightKey *key,
    const float direction[3],unsigned char *valid,unsigned valid_bytes)
{
    if(key->valid && !memcmp(key->direction,direction,sizeof key->direction))return 0;
    memcpy(key->direction,direction,sizeof key->direction);key->valid=1;
    memset(valid,0,valid_bytes);return 1;
}

/* The hierarchy and direction must remain unchanged while valid is set.
   render_dot preserves the old ordered binary64 result, with its existing
   double fallback for values outside the exact integer fast path. */
static inline int source_local_light(float out[3],float cache[][3],
    unsigned char *valid,unsigned bone,const float matrix[16],const float direction[3])
{
    unsigned mask=1u<<(bone&7);
    if(valid[bone>>3]&mask){memcpy(out,cache[bone],sizeof(float)*3);return 1;}
    for(unsigned k=0;k<3;k++) {
        const float column[3]={matrix[k],matrix[4+k],matrix[8+k]};
        out[k]=render_dot(column,direction,3);
    }
    memcpy(cache[bone],out,sizeof(float)*3);valid[bone>>3]|=mask;return 0;
}
#endif
