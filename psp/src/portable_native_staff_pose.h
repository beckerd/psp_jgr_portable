#ifndef JGR_PORTABLE_NATIVE_STAFF_POSE_H
#define JGR_PORTABLE_NATIVE_STAFF_POSE_H
#include "portable_native_world.h"
typedef struct JgrNativeStaffPoseRecorder JgrNativeStaffPoseRecorder;
typedef int (*JgrNativeStaffPoseSample)(void *,Player *,const SceneCamera *,unsigned part,unsigned cut);
/* Caller owns a seekable empty w+b stream. The bounded recorder writes native
 * state changes around animation callbacks; it contains no emulated pointers
 * or captured original frames. begin installs the native-player observer. */
JgrNativeStaffPoseRecorder *jgr_native_staff_pose_record_create(FILE *);
int jgr_native_staff_pose_record_begin(JgrNativeStaffPoseRecorder *,Player *,unsigned part);
int jgr_native_staff_pose_record_frame(JgrNativeStaffPoseRecorder *,const Player *,
    const SceneCamera *,unsigned part,unsigned cut,unsigned camera_tracks_body);
/* Finalizes counts and restores the logical end, allowing the staff driver to
 * append its separately validated wall/segment metadata. */
int jgr_native_staff_pose_record_finish(JgrNativeStaffPoseRecorder *);
void jgr_native_staff_pose_record_destroy(JgrNativeStaffPoseRecorder *);
/* Rebuild one cast's animation/attachments using the shared native world.
 * sample returns 1 to continue. Normal/spray banks and all scalar tables must
 * already be loaded; the recorder stream remains caller-owned. Reads use a
 * bounded 64 KiB window and stop at the declared event count, before metadata.
 * A configured progress callback returns nonzero to continue, zero to cancel. */
int jgr_native_staff_pose_replay(const JgrNativeConfig *,FILE *,Chars *normal,Chars *spray,
    unsigned model,unsigned character,JgrNativeStaffPoseSample sample,void *user);
#endif
