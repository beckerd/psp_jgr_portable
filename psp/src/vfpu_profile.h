#ifndef JGR_VFPU_PROFILE_H
#define JGR_VFPU_PROFILE_H
#include "diagnostics.h"
#if defined(__PSP__) && !defined(JGR_SCALAR_RENDER_MATH)
#define VFPU_PROFILE_POSE_ENABLED 1
#else
#define VFPU_PROFILE_POSE_ENABLED 0
#endif
#if defined(__PSP__) && !defined(JGR_SCALAR_CLIP_MATH)
#define VFPU_PROFILE_CLIP_ENABLED 1
#else
#define VFPU_PROFILE_CLIP_ENABLED 0
#endif
/* Elapsed CPU scope times include scalar bookkeeping, preservation, and any
   audio-thread preemption. They are NOT VFPU execution-unit occupancy. */
typedef struct {
    unsigned visibility_us,spheres;
    unsigned clip_calls,clip_vectors,clip_samples,clip_sample_us,clip_sample_max_us;
    unsigned vertex_chunks,vertices,vertex_samples,vertex_sample_us;
    unsigned attachment_us,attachment_matrices;
    unsigned transform_batches,transform_points,transform_exact_rows,transform_samples,transform_sample_us;
    unsigned sample_frames;
} VfpuProfile;
#if defined(__PSP__) && JGR_DIAGNOSTICS
unsigned vfpu_profile_transform_begin(void);
void vfpu_profile_transform_end(unsigned stamp,unsigned points,unsigned exact_rows);
unsigned vfpu_profile_clip_begin(void);
unsigned vfpu_profile_vertices_begin(void);
void vfpu_profile_vertices_end(unsigned stamp,unsigned vertices);
void vfpu_profile_attachment(unsigned us,unsigned matrices);
void vfpu_profile_frame_begin(void);
void vfpu_profile_clip_end(unsigned stamp,unsigned vectors);
void vfpu_profile_visibility(unsigned us,unsigned spheres);
void vfpu_profile_read(VfpuProfile *out);
#else
static inline unsigned vfpu_profile_transform_begin(void){return 0;}
static inline void vfpu_profile_transform_end(unsigned stamp,unsigned points,unsigned exact_rows){(void)stamp;(void)points;(void)exact_rows;}
static inline unsigned vfpu_profile_clip_begin(void){return 0;}
static inline unsigned vfpu_profile_vertices_begin(void){return 0;}
static inline void vfpu_profile_vertices_end(unsigned stamp,unsigned vertices){(void)stamp;(void)vertices;}
static inline void vfpu_profile_attachment(unsigned us,unsigned matrices){(void)us;(void)matrices;}
static inline void vfpu_profile_frame_begin(void){}
static inline void vfpu_profile_clip_end(unsigned stamp,unsigned vectors){(void)stamp;(void)vectors;}
static inline void vfpu_profile_visibility(unsigned us,unsigned spheres){(void)us;(void)spheres;}
static inline void vfpu_profile_read(VfpuProfile *out){*out=(VfpuProfile){0};}
#endif
#endif
