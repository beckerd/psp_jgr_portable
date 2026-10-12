#ifndef JGR_MOVIE_SHADOW_H
#define JGR_MOVIE_SHADOW_H
#include "movie.h"
#include "garage_shadow.h"
/* 09d8c8 / 0bfdcc / 0bfe90: original type-58 geometry, source light,
   and 500-unit extrusion. Callback adapts the closed volume to PSP GE. */
int movie_shadow_actor(const Movie *,const Chars *,unsigned actor,const float view[16],GarageShadowSubmit,void *);
#endif
