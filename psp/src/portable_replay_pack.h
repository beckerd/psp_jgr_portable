#ifndef JGR_PORTABLE_REPLAY_PACK_H
#define JGR_PORTABLE_REPLAY_PACK_H
#include <stdio.h>
#include <stdint.h>
typedef struct JgrReplayPack JgrReplayPack;
/* Resolved local channels. Missing animation channels must already contain
 * the selected normal/spray model's bind value. evaluation is the final
 * renderer evaluation mask; keyed uses bits 1=position,2=angles,4=scale.
 * TRT3 preserves keyed in the high three evaluation bits; TRT2 stores only
 * evaluation. Angles use integer binary turns, serialized modulo 65536. */
typedef struct {
 float position[3],scale[3];int32_t angle[3];uint32_t evaluation,keyed;
} JgrReplayNativePose;
/* All 204 serialized effect bytes, in SkateInput order. points are attachment
 * indices 0..7 then 19..22; external is the completed actor's external vector.
 * travel_angles are pitch, heading, roll (not the smoothed visual angles). */
typedef struct {
 int32_t flags,motion,mirrored,grind_pose,dash,boost,obstruction;
 float frame,speed,external[3],points[12][3];int32_t travel_angles[3];
} JgrReplayNativeEffects;
typedef struct {
 float position[3];int32_t angle[3];uint32_t state;
 float camera_position[3],camera_direction[3];int32_t camera_roll,camera_fov;
 float near_clip,far_clip; /* Route conventions, already sign-corrected. */
 uint32_t tick,part,cut;
 JgrReplayNativePose pose[20];JgrReplayNativeEffects effects;
 /* TRT3 only: completed obstruction target. Required when obstruction is
  * nonzero, otherwise omitted/zero. Borrowed only during sample(). */
 const float *rival_target;
} JgrReplayNativeSample;
/* Bind streams are verified generated JGC2 files, never bundled game data.
 * Scratch must be a seekable empty w+b stream, owned by the caller. */
JgrReplayPack *jgr_replay_pack_create(FILE *scratch,FILE *normal,unsigned model,
                                     FILE *spray,unsigned character);
/* Native producers resolve bind/model channels themselves, so this constructor
 * needs no JGC streams and allocates only the bounded pack workspace. */
JgrReplayPack *jgr_replay_pack_create_native(FILE *scratch,unsigned character);
/* Select TRT3 before the first sample. Active sections must arrive in order. */
int jgr_replay_pack_rival(JgrReplayPack *,unsigned sections);
/* Each complete original frame is resolved immediately into a 908-byte
 * temporary row (+12 bytes for TRT3). No capture-sized work buffer is allocated. Part/cut come
 * from the caller's original replay task observations. */
int jgr_replay_pack_sample(JgrReplayPack *,const unsigned char actor[0x900],
                           const unsigned char camera[0x290],
                           const unsigned char pose[0x400],unsigned part,
                           unsigned cut,unsigned spray_variant);
/* Stream one complete native frame through the same validation/compression
 * path as observed frames. No emulated addresses or snapshot layout required. */
int jgr_replay_pack_native_sample(JgrReplayPack *,const JgrReplayNativeSample *);
/* Stream compact TRT2, sharing all constant translation/scale channels. */
int jgr_replay_pack_finish(JgrReplayPack *,FILE *out);
unsigned jgr_replay_pack_count(const JgrReplayPack *);
void jgr_replay_pack_destroy(JgrReplayPack *);
#endif
