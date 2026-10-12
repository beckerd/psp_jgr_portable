#ifndef JGR_GARAGE_LIGHT_H
#define JGR_GARAGE_LIGHT_H
#include <math.h>
#include "charanim.h"
/* 02229a..022346, 025b60..025c0c and 04beac..04bf58 install the
   same normal/cel lights for the room, selection and invitation passes.
   Source normalizes the view-space direction. With an orthonormal view,
   the view rotations cancel in the skin kernel's M^T L calculation.
   Modifier-volume shadows (0ba380) are a separate render path. */
static inline void garage_light_values(CharLight *normal,CharLight *cel)
{
    *normal=(CharLight){{0,-.4f,.34f},.45f,.4f,{0xffffffffu,0}};
    *cel=*normal;
    float length=sqrtf(.4f*.4f+.34f*.34f);
    for(int k=0;k<3;k++)cel->direction[k]/=length;
    cel->diffuse=1;cel->ambient=0;cel->color[1]=0xff5f7597u;
}
static inline void garage_light_begin(void)
{
    /* chars_scene_lighting retains pointers through the entire draw pass. */
    static CharLight normal,cel;
    garage_light_values(&normal,&cel);chars_scene_lighting(&normal,&cel);
}
static inline void garage_light_end(void){chars_scene_lighting(NULL,NULL);}
#endif
