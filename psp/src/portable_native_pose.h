#ifndef JGR_PORTABLE_NATIVE_POSE_H
#define JGR_PORTABLE_NATIVE_POSE_H
#include "charpose.h"
/* Native setup borrows immutable pose banks. Retire prepared cursors before
 * a bank is released/rebound; no sampled poses or animation state are kept. */
void jgr_native_chars_pose_forget(void);
void jgr_native_chars_sample_pose(const Chars *,const JgcChar *,int,float,unsigned,CharPose *);
void jgr_native_chars_blend_pose(CharPose *,const CharPose *,float);
void jgr_native_chars_overlay_pose(const Chars *,const JgcChar *,int,float,unsigned,float,CharPose *);
#endif
