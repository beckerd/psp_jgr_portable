#ifndef JGR_PORTABLE_NATIVE_DEMO_H
#define JGR_PORTABLE_NATIVE_DEMO_H
#include "portable_native_world.h"
typedef struct JgrNativeDemoCues JgrNativeDemoCues;
/* Bounded observer for native PLAY events and completed small-spray state. */
JgrNativeDemoCues *jgr_native_demo_cues_create(Player *);
void jgr_native_demo_cues_sound(void *,int kind,int program);
int jgr_native_demo_cues_sample(JgrNativeDemoCues *);
int jgr_native_demo_cues_finish(JgrNativeDemoCues *,unsigned char **,unsigned *);
void jgr_native_demo_cues_destroy(JgrNativeDemoCues *);
/* Scratch is an empty seekable stream. Output is TRT2; cue output is RDC1.
 * Mission and chain ordinal select user-authored stage/set/controller data.
 * Caller owns the returned cue buffer and both file streams. */
int jgr_native_demo_generate(const JgrNativeConfig *,const char *mission,unsigned record,
    unsigned subset,unsigned character,FILE *scratch,FILE *output,unsigned char **cues,unsigned *cue_bytes);
#endif
