#ifndef JGR_SET_COLLISION_H
#define JGR_SET_COLLISION_H
#include "collide.h"
#include <math.h>
/* SOB1 placement adapter shared by normal mission creation and input-only
   tests. Preserve source order, winding and surface type. Caller rebuilds
   the grid after all placements, before any collision consumer runs. */
static inline int set_collision_append(Collision *world,const ColTri *source,
    unsigned count,float x,float y,float z,float yaw)
{
    ColTri buf[64];float c=cosf(yaw),s=sinf(yaw);
    while(count) {
        unsigned n=count<64 ? count:64;
        for(unsigned k=0;k<n;k++) {
            buf[k]=source[k];
            for(int v=0;v<3;v++) {
                float lx=buf[k].v[v*3],ly=buf[k].v[v*3+1],lz=buf[k].v[v*3+2];
                buf[k].v[v*3]=c*lx+s*lz+x;buf[k].v[v*3+1]=ly+y;
                buf[k].v[v*3+2]=-s*lx+c*lz+z;
            }
            buf[k].src=4;
        }
        int error=col_append(world,buf,n);if(error)return error;
        source+=n;count-=n;
    }
    return 0;
}
#endif
