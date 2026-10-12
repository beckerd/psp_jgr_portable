#ifndef JGR_TITLE_ROUTE_H
#define JGR_TITLE_ROUTE_H
#include "charpose.h"
#include "skatefx.h"
#include "scene.h"
#include <stdio.h>

/* Original fixed title performance. No collision/physics reconstruction.
   All public calls belong to the main thread. PSP routes share one reader;
   cache_storage owns both 33-frame buffers while cache names the current
   one. Free/resume drain pending reads before retiring their file/storage. */
typedef struct {
    unsigned char *image;
    unsigned char *cache;FILE *file;
    unsigned char *cache_storage;
    struct TitleRoutePrefetch *prefetch;
    char path[128];
    unsigned cache_first,cache_count;
    unsigned count,character,stride,nchannels;
    unsigned section[3]; /* optional three-part recorded performance; zero for titles */
    int has_water_point;
    const float *base;
    const unsigned *channels;
} TitleRoute;
typedef struct {
    float pos[3];int angle[3],state;
    SceneCamera camera;CharPose pose[20];
    unsigned tick,part;SkateInput effects;
    float water_point[3]; /* TRT3: original +54c ray result for moving rivals */
} TitleSample;
int title_route_load(TitleRoute *route,const char *path);
void title_route_free(TitleRoute *route);
void title_route_resume(TitleRoute *route);
int title_route_sample(TitleRoute *route,unsigned frame,float fraction,TitleSample *sample);
#endif
