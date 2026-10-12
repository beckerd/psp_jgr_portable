/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>
typedef struct {
    int type;uint32_t root,texlist,flags;const char *file;
} SceneModelBinding;


#include "source_data_pointers.h"
SceneModelBinding EVENT_MODELS_0[2] __asm__("jgr_ptr_scene_models_EVENT_MODELS_0") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_1[2] __asm__("jgr_ptr_scene_models_EVENT_MODELS_1") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_4[1] __asm__("jgr_ptr_scene_models_EVENT_MODELS_4") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_7[9] __asm__("jgr_ptr_scene_models_EVENT_MODELS_7") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_8[2] __asm__("jgr_ptr_scene_models_EVENT_MODELS_8") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_9[1] __asm__("jgr_ptr_scene_models_EVENT_MODELS_9") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_10[2] __asm__("jgr_ptr_scene_models_EVENT_MODELS_10") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_23[6] __asm__("jgr_ptr_scene_models_EVENT_MODELS_23") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_24[1] __asm__("jgr_ptr_scene_models_EVENT_MODELS_24") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_25[12] __asm__("jgr_ptr_scene_models_EVENT_MODELS_25") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_26[2] __asm__("jgr_ptr_scene_models_EVENT_MODELS_26") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_31[2] __asm__("jgr_ptr_scene_models_EVENT_MODELS_31") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_32[1] __asm__("jgr_ptr_scene_models_EVENT_MODELS_32") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_33[2] __asm__("jgr_ptr_scene_models_EVENT_MODELS_33") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_34[1] __asm__("jgr_ptr_scene_models_EVENT_MODELS_34") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_35[9] __asm__("jgr_ptr_scene_models_EVENT_MODELS_35") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_36[4] __asm__("jgr_ptr_scene_models_EVENT_MODELS_36") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_41[1] __asm__("jgr_ptr_scene_models_EVENT_MODELS_41") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_42[1] __asm__("jgr_ptr_scene_models_EVENT_MODELS_42") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_43[11] __asm__("jgr_ptr_scene_models_EVENT_MODELS_43") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_44[4] __asm__("jgr_ptr_scene_models_EVENT_MODELS_44") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_49[7] __asm__("jgr_ptr_scene_models_EVENT_MODELS_49") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_50[4] __asm__("jgr_ptr_scene_models_EVENT_MODELS_50") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_59[2] __asm__("jgr_ptr_scene_models_EVENT_MODELS_59") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_60[8] __asm__("jgr_ptr_scene_models_EVENT_MODELS_60") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_61[3] __asm__("jgr_ptr_scene_models_EVENT_MODELS_61") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_62[3] __asm__("jgr_ptr_scene_models_EVENT_MODELS_62") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_73[2] __asm__("jgr_ptr_scene_models_EVENT_MODELS_73") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_75[1] __asm__("jgr_ptr_scene_models_EVENT_MODELS_75") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_78[10] __asm__("jgr_ptr_scene_models_EVENT_MODELS_78") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_79[2] __asm__("jgr_ptr_scene_models_EVENT_MODELS_79") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_80[2] __asm__("jgr_ptr_scene_models_EVENT_MODELS_80") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_84[10] __asm__("jgr_ptr_scene_models_EVENT_MODELS_84") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_85[2] __asm__("jgr_ptr_scene_models_EVENT_MODELS_85") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_88[11] __asm__("jgr_ptr_scene_models_EVENT_MODELS_88") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_89[2] __asm__("jgr_ptr_scene_models_EVENT_MODELS_89") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_90[4] __asm__("jgr_ptr_scene_models_EVENT_MODELS_90") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_91[13] __asm__("jgr_ptr_scene_models_EVENT_MODELS_91") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_92[1] __asm__("jgr_ptr_scene_models_EVENT_MODELS_92") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_97[1] __asm__("jgr_ptr_scene_models_EVENT_MODELS_97") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_98[3] __asm__("jgr_ptr_scene_models_EVENT_MODELS_98") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_99[1] __asm__("jgr_ptr_scene_models_EVENT_MODELS_99") __attribute__((aligned(16)));
SceneModelBinding EVENT_MODELS_103[2] __asm__("jgr_ptr_scene_models_EVENT_MODELS_103") __attribute__((aligned(16)));
unsigned char literal_pool_9[3397] __asm__("jgr_ptr_scene_models_literal_pool_9") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {3025183912,3472,40,16,EVENT_MODELS_0},
    {3277296702,3520,40,16,EVENT_MODELS_1},
    {3007144113,3568,20,16,EVENT_MODELS_4},
    {708063499,3600,180,16,EVENT_MODELS_7},
    {3129683098,3792,40,16,EVENT_MODELS_8},
    {3448511500,3840,20,16,EVENT_MODELS_9},
    {897160478,3872,40,16,EVENT_MODELS_10},
    {2271057767,3920,120,16,EVENT_MODELS_23},
    {423166660,4048,20,16,EVENT_MODELS_24},
    {1849569874,4080,240,16,EVENT_MODELS_25},
    {4147602408,4320,40,16,EVENT_MODELS_26},
    {1883817738,4368,40,16,EVENT_MODELS_31},
    {3913389744,4416,20,16,EVENT_MODELS_32},
    {2655430182,4448,40,16,EVENT_MODELS_33},
    {2241413,4496,20,16,EVENT_MODELS_34},
    {1998914323,4528,180,16,EVENT_MODELS_35},
    {3995882153,4720,80,16,EVENT_MODELS_36},
    {1057575373,4800,20,16,EVENT_MODELS_41},
    {2785017975,4832,20,16,EVENT_MODELS_42},
    {3506909409,4864,220,16,EVENT_MODELS_43},
    {1331930434,5088,80,16,EVENT_MODELS_44},
    {835901951,5168,140,16,EVENT_MODELS_49},
    {1360351258,5312,80,16,EVENT_MODELS_50},
    {684320958,5392,40,16,EVENT_MODELS_59},
    {2050491353,5440,160,16,EVENT_MODELS_60},
    {222245711,5600,60,16,EVENT_MODELS_61},
    {2486592245,5664,60,16,EVENT_MODELS_62},
    {4197081890,5728,40,16,EVENT_MODELS_73},
    {323601943,5776,20,16,EVENT_MODELS_75},
    {1845017258,5808,200,16,EVENT_MODELS_78},
    {452954684,6016,40,16,EVENT_MODELS_79},
    {3837472343,6064,40,16,EVENT_MODELS_80},
    {3822512718,6112,200,16,EVENT_MODELS_84},
    {2496781016,6320,40,16,EVENT_MODELS_85},
    {3932202597,6368,220,16,EVENT_MODELS_88},
    {2640811763,6592,40,16,EVENT_MODELS_89},
    {4255129366,6640,80,16,EVENT_MODELS_90},
    {2326212480,6720,260,16,EVENT_MODELS_91},
    {330202682,6992,20,16,EVENT_MODELS_92},
    {1673824949,7024,20,16,EVENT_MODELS_97},
    {4084963108,7056,60,16,EVENT_MODELS_98},
    {2222761906,7120,20,16,EVENT_MODELS_99},
    {2548615789,7152,40,16,EVENT_MODELS_103},
    {3676215236,7200,3397,16,literal_pool_9},
};
int source_data_ptr_scene_models_load(void){return source_data_pointers_load_file("source_data_ptr_scene_models.jsd",sections,44u,10597u,0x02d7d1adu,170u);}
#endif
