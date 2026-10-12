/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>
typedef struct {int backend;float range,far_fraction,near_fraction;} SourceAudioProfile;
typedef struct {
    unsigned kind,bank,reference,file,program,backend,channel,logical_bank,patch_file,patch_bank;
    SourceAudioProfile profile;
} EventAudioCue;


#include "source_data_pointers.h"
EventAudioCue EVENT_AUDIO_0[2] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_0") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_1[3] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_1") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_8[4] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_8") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_9[2] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_9") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_10[3] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_10") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_24[3] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_24") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_25[4] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_25") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_26[3] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_26") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_31[4] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_31") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_32[2] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_32") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_33[2] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_33") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_34[1] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_34") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_35[1] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_35") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_36[1] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_36") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_41[1] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_41") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_44[1] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_44") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_49[2] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_49") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_50[1] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_50") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_59[1] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_59") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_60[7] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_60") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_61[2] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_61") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_73[4] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_73") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_75[1] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_75") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_79[2] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_79") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_80[3] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_80") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_85[2] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_85") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_88[4] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_88") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_89[3] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_89") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_90[4] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_90") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_91[2] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_91") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_92[3] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_92") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_98[2] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_98") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_99[1] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_99") __attribute__((aligned(16)));
EventAudioCue EVENT_AUDIO_103[1] __asm__("jgr_ptr_event_audio_tables_EVENT_AUDIO_103") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {1650292646,592,112,16,EVENT_AUDIO_0},
    {358238000,704,168,16,EVENT_AUDIO_1},
    {1820786580,880,224,16,EVENT_AUDIO_8},
    {461491970,1104,112,16,EVENT_AUDIO_9},
    {3524769226,1216,168,16,EVENT_AUDIO_10},
    {4267123216,1392,168,16,EVENT_AUDIO_24},
    {2303726214,1568,224,16,EVENT_AUDIO_25},
    {274285372,1792,168,16,EVENT_AUDIO_26},
    {2535909342,1968,224,16,EVENT_AUDIO_31},
    {238007908,2192,112,16,EVENT_AUDIO_32},
    {2032699122,2304,112,16,EVENT_AUDIO_33},
    {3880522577,2416,56,16,EVENT_AUDIO_34},
    {2420843463,2480,56,16,EVENT_AUDIO_35},
    {155349629,2544,56,16,EVENT_AUDIO_36},
    {3630657817,2608,56,16,EVENT_AUDIO_41},
    {2819458454,2672,56,16,EVENT_AUDIO_44},
    {3602708779,2736,112,16,EVENT_AUDIO_49},
    {3061543118,2848,56,16,EVENT_AUDIO_50},
    {3483879530,2912,56,16,EVENT_AUDIO_59},
    {2639667981,2976,392,16,EVENT_AUDIO_60},
    {3931181979,3376,112,16,EVENT_AUDIO_61},
    {491013110,3488,224,16,EVENT_AUDIO_73},
    {4096255683,3712,56,16,EVENT_AUDIO_75},
    {4254182120,3776,112,16,EVENT_AUDIO_79},
    {64294531,3888,168,16,EVENT_AUDIO_80},
    {1941961228,4064,112,16,EVENT_AUDIO_85},
    {219055793,4176,224,16,EVENT_AUDIO_88},
    {2047456807,4400,168,16,EVENT_AUDIO_89},
    {449724354,4576,224,16,EVENT_AUDIO_90},
    {1841893204,4800,112,16,EVENT_AUDIO_91},
    {4106247918,4912,168,16,EVENT_AUDIO_92},
    {336967664,5088,112,16,EVENT_AUDIO_98},
    {1662158694,5200,56,16,EVENT_AUDIO_99},
    {380744320,5264,56,16,EVENT_AUDIO_103},
};
int source_data_ptr_event_audio_tables_load(void){return source_data_pointers_load_file("source_data_ptr_event_audio_tables.jsd",sections,34u,5320u,0x70323741u,0u);}
#endif
