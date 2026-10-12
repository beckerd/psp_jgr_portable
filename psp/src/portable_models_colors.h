#ifndef JGR_PORTABLE_MODELS_COLORS_H
#define JGR_PORTABLE_MODELS_COLORS_H
#include "portable_models.h"
/* Match the source PLAY head palette to spray or companion geometry. Streams
 * are the first four temporary JGC sections; lighting holds optional PLT rows.
 * mode 1: spray head, 2: exact companion surface, 3: attached-frame expression. */
int jgr_models_face_colors(JgrDisc *,const JgrJgcCharacter *,const JgrDiscFile *,
                          FILE *sections[10],FILE *lighting,unsigned mode);
#endif
