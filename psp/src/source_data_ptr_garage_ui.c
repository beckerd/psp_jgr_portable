/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>
typedef struct {
    unsigned slot,color;
    float x,y,w,h,u0,v0,u1,v1;
} GarageQuad;


#include "source_data_pointers.h"
unsigned char UI_ORDER[15] __asm__("jgr_ptr_garage_ui_UI_ORDER") __attribute__((aligned(16)));
float UI_STATS[15][3] __asm__("jgr_ptr_garage_ui_UI_STATS") __attribute__((aligned(16)));
const char * UI_STAT_NAMES[3] __asm__("jgr_ptr_garage_ui_UI_STAT_NAMES") __attribute__((aligned(16)));
float UI_STAT_POS[3][2] __asm__("jgr_ptr_garage_ui_UI_STAT_POS") __attribute__((aligned(16)));
const char * UI_CHAPTERS[3] __asm__("jgr_ptr_garage_ui_UI_CHAPTERS") __attribute__((aligned(16)));
const char * UI_MENU_PROMPTS[7] __asm__("jgr_ptr_garage_ui_UI_MENU_PROMPTS") __attribute__((aligned(16)));
const char * UI_MISSIONS[24] __asm__("jgr_ptr_garage_ui_UI_MISSIONS") __attribute__((aligned(16)));
const char * UI_PROMPTS[24] __asm__("jgr_ptr_garage_ui_UI_PROMPTS") __attribute__((aligned(16)));
float UI_MARKS[24][2] __asm__("jgr_ptr_garage_ui_UI_MARKS") __attribute__((aligned(16)));
GarageQuad UI_QUADS[41] __asm__("jgr_ptr_garage_ui_UI_QUADS") __attribute__((aligned(16)));
unsigned char UI_LISTS[41][2] __asm__("jgr_ptr_garage_ui_UI_LISTS") __attribute__((aligned(16)));
float UI_NEXT_X __asm__("jgr_ptr_garage_ui_UI_NEXT_X") __attribute__((aligned(16)));
char UI_BASIC_CHARS[30] __asm__("jgr_ptr_garage_ui_UI_BASIC_CHARS") __attribute__((aligned(16)));
unsigned UI_BASIC_CLUT[16] __asm__("jgr_ptr_garage_ui_UI_BASIC_CLUT") __attribute__((aligned(16)));
unsigned char UI_BASIC_PIXELS[8192] __asm__("jgr_ptr_garage_ui_UI_BASIC_PIXELS") __attribute__((aligned(16)));
unsigned char literal_pool_13[681] __asm__("jgr_ptr_garage_ui_literal_pool_13") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {2290114042,1280,15,16,UI_ORDER},
    {720284104,1296,180,16,UI_STATS},
    {1471451036,1488,12,16,UI_STAT_NAMES},
    {4217574547,1504,24,16,UI_STAT_POS},
    {3769040708,1536,12,16,UI_CHAPTERS},
    {2922364581,1552,28,16,UI_MENU_PROMPTS},
    {326527051,1584,96,16,UI_MISSIONS},
    {3063089064,1680,96,16,UI_PROMPTS},
    {1103311921,1776,192,16,UI_MARKS},
    {2723188636,1968,1640,16,UI_QUADS},
    {1972326855,3616,82,16,UI_LISTS},
    {1095461390,3712,4,16,&UI_NEXT_X},
    {687965890,3728,30,16,UI_BASIC_CHARS},
    {400683184,3760,64,16,UI_BASIC_CLUT},
    {337884962,3824,8192,16,UI_BASIC_PIXELS},
    {1592994543,12016,681,16,literal_pool_13},
};
int source_data_ptr_garage_ui_load(void){return source_data_pointers_load_file("source_data_ptr_garage_ui.jsd",sections,16u,12697u,0x62366f78u,61u);}
#endif
