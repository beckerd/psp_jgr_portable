#ifndef JGR_PORTABLE_CONVERT_H
#define JGR_PORTABLE_CONVERT_H
#include "portable_disc.h"
#include <stdio.h>
/* Streaming output; temporary decode buffers are freed before returning. */
int jgr_convert_tex2d(JgrDisc *,const JgrDiscFile *,unsigned maximum,FILE *);
int jgr_convert_list2d(JgrDisc *,const JgrDiscFile *,uint32_t base,
                      uint32_t table,unsigned count,FILE *);
/* Reusable bounded texture primitives. Caller frees the returned buffer.
   Half-size reduction may premultiply its input in place. */
unsigned char *jgr_pvr_decode_payload(const unsigned char *,unsigned bytes,
    unsigned pixel_format,unsigned data_format,unsigned width,unsigned height);
unsigned char *jgr_rgba_half_bicubic(unsigned char *,unsigned width,unsigned height);
#endif
