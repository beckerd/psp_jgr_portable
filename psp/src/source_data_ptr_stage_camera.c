/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>
typedef struct {
    int32_t type;
    float point[6][3],normal[6][3],eye[3],target[3];
    int32_t fov,roll,variable_fov;
    float near_point[3];int32_t near_fov;
    float far_point[3];int32_t far_fov;
} StageCameraRegion;


#include "source_data_pointers.h"
StageCameraRegion shibuya_passage_camera __asm__("jgr_ptr_stage_camera_shibuya_passage_camera") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {1836179383,64,216,16,&shibuya_passage_camera},
};
int source_data_ptr_stage_camera_load(void){return source_data_pointers_load_file("source_data_ptr_stage_camera.jsd",sections,1u,280u,0xa30ca9f6u,0u);}
#endif
