#include "vfpu_profile.h"
#if defined(__PSP__) && JGR_DIAGNOSTICS
#include <pspkernel.h>
static VfpuProfile profile;
static unsigned frame_number,sample_frame;
void vfpu_profile_frame_begin(void)
{
    /* Time every clip in one whole frame out of sixteen. Unlike sampling a
       single triangle then multiplying by hundreds, this does not magnify
       one audio preemption into a fictitious many-millisecond math cost. */
    sample_frame=(++frame_number&15)==0;
    profile.sample_frames+=sample_frame;
}
unsigned vfpu_profile_clip_begin(void)
{
    profile.clip_calls++;
    if(!sample_frame)return 0;
    return sceKernelGetSystemTimeLow()+1;
}
unsigned vfpu_profile_vertices_begin(void)
{profile.vertex_chunks++;return sample_frame ? sceKernelGetSystemTimeLow()+1:0;}
void vfpu_profile_vertices_end(unsigned stamp,unsigned vertices)
{
    profile.vertices+=vertices;
    if(stamp){profile.vertex_samples++;profile.vertex_sample_us+=sceKernelGetSystemTimeLow()-(stamp-1);}
}
void vfpu_profile_attachment(unsigned us,unsigned matrices)
{profile.attachment_us+=us;profile.attachment_matrices+=matrices;}
void vfpu_profile_clip_end(unsigned stamp,unsigned vectors)
{
    profile.clip_vectors+=vectors;
    if(!stamp)return;
    unsigned elapsed=sceKernelGetSystemTimeLow()-(stamp-1);
    profile.clip_samples++;profile.clip_sample_us+=elapsed;
    if(elapsed>profile.clip_sample_max_us)profile.clip_sample_max_us=elapsed;
}
void vfpu_profile_visibility(unsigned us,unsigned spheres)
{profile.visibility_us+=us;profile.spheres+=spheres;}
void vfpu_profile_read(VfpuProfile *out)
{*out=profile;profile=(VfpuProfile){0};}

unsigned vfpu_profile_transform_begin(void)
{profile.transform_batches++;return sample_frame ? sceKernelGetSystemTimeLow()+1:0;}
void vfpu_profile_transform_end(unsigned stamp,unsigned points,unsigned exact_rows)
{
    profile.transform_points+=points;profile.transform_exact_rows+=exact_rows;
    if(stamp){profile.transform_samples++;profile.transform_sample_us+=sceKernelGetSystemTimeLow()-(stamp-1);}
}

#endif
