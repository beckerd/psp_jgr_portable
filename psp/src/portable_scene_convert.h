#ifndef JGR_PORTABLE_SCENE_CONVERT_H
#define JGR_PORTABLE_SCENE_CONVERT_H
#include "portable_disc.h"
#include <stdio.h>
/* Streaming archives and bounded printable subtitle conversion from original
 * disc members. These routines contain no event commands, text or imagery. */
int jgr_scene_convert(JgrDisc *,const JgrDiscFile *,unsigned operation,FILE *);
/* CAS1 is sequential: checksum the exact final bytes as they are written.
 * Caller must still check flush/close, expected checksum and output size. */
int jgr_scene_dependencies_crc(JgrDisc *,FILE *,uint32_t *crc);
#endif
