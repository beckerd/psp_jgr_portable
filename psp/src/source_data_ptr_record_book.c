/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>
typedef struct {int model,frames;} BookModel;


#include "source_data_pointers.h"
BookModel BOOK_OPEN[6] __asm__("jgr_ptr_record_book_BOOK_OPEN") __attribute__((aligned(16)));
BookModel BOOK_RETURN[4] __asm__("jgr_ptr_record_book_BOOK_RETURN") __attribute__((aligned(16)));
BookModel BOOK_STATIC[3] __asm__("jgr_ptr_record_book_BOOK_STATIC") __attribute__((aligned(16)));
BookModel BOOK_SELECT[4] __asm__("jgr_ptr_record_book_BOOK_SELECT") __attribute__((aligned(16)));
BookModel BOOK_BACK[1] __asm__("jgr_ptr_record_book_BOOK_BACK") __attribute__((aligned(16)));
BookModel RECORD_OPEN[17] __asm__("jgr_ptr_record_book_RECORD_OPEN") __attribute__((aligned(16)));
BookModel RECORD_BACK[2] __asm__("jgr_ptr_record_book_RECORD_BACK") __attribute__((aligned(16)));
BookModel RECORD_STORY[3] __asm__("jgr_ptr_record_book_RECORD_STORY") __attribute__((aligned(16)));
BookModel RECORD_JET[4] __asm__("jgr_ptr_record_book_RECORD_JET") __attribute__((aligned(16)));
BookModel RECORD_CITY[5] __asm__("jgr_ptr_record_book_RECORD_CITY") __attribute__((aligned(16)));
BookModel RECORD_KIND[20] __asm__("jgr_ptr_record_book_RECORD_KIND") __attribute__((aligned(16)));
BookModel OPTION_BACK[2] __asm__("jgr_ptr_record_book_OPTION_BACK") __attribute__((aligned(16)));
BookModel OPTION_OPEN[3] __asm__("jgr_ptr_record_book_OPTION_OPEN") __attribute__((aligned(16)));
BookModel OPTION_SOUND_OPEN[1] __asm__("jgr_ptr_record_book_OPTION_SOUND_OPEN") __attribute__((aligned(16)));
BookModel OPTION_RUMBLE_OPEN[1] __asm__("jgr_ptr_record_book_OPTION_RUMBLE_OPEN") __attribute__((aligned(16)));
BookModel OPTION_VALUE_OPEN[1] __asm__("jgr_ptr_record_book_OPTION_VALUE_OPEN") __attribute__((aligned(16)));
BookModel OPTION_RUMBLE_VALUE_OPEN[1] __asm__("jgr_ptr_record_book_OPTION_RUMBLE_VALUE_OPEN") __attribute__((aligned(16)));
BookModel OPTION_SELECT[3] __asm__("jgr_ptr_record_book_OPTION_SELECT") __attribute__((aligned(16)));
BookModel OPTION_SOUND1[2] __asm__("jgr_ptr_record_book_OPTION_SOUND1") __attribute__((aligned(16)));
BookModel OPTION_SOUND0[2] __asm__("jgr_ptr_record_book_OPTION_SOUND0") __attribute__((aligned(16)));
BookModel OPTION_RUMBLE1[2] __asm__("jgr_ptr_record_book_OPTION_RUMBLE1") __attribute__((aligned(16)));
BookModel OPTION_RUMBLE0[2] __asm__("jgr_ptr_record_book_OPTION_RUMBLE0") __attribute__((aligned(16)));
BookModel RECORD_DOWN __asm__("jgr_ptr_record_book_RECORD_DOWN") __attribute__((aligned(16)));
BookModel RECORD_UP __asm__("jgr_ptr_record_book_RECORD_UP") __attribute__((aligned(16)));
BookModel RECORD_PORTRAIT[15] __asm__("jgr_ptr_record_book_RECORD_PORTRAIT") __attribute__((aligned(16)));
float RECORD_SCROLL[5] __asm__("jgr_ptr_record_book_RECORD_SCROLL") __attribute__((aligned(16)));
int RECORD_PAGE[5] __asm__("jgr_ptr_record_book_RECORD_PAGE") __attribute__((aligned(16)));
unsigned RECORD_COLOR[6] __asm__("jgr_ptr_record_book_RECORD_COLOR") __attribute__((aligned(16)));
float RECORD_PORTRAIT_XY[10] __asm__("jgr_ptr_record_book_RECORD_PORTRAIT_XY") __attribute__((aligned(16)));
float RECORD_JET_XY[10] __asm__("jgr_ptr_record_book_RECORD_JET_XY") __attribute__((aligned(16)));
float RECORD_STORY_XY[10] __asm__("jgr_ptr_record_book_RECORD_STORY_XY") __attribute__((aligned(16)));
float RECORD_SCORE_XY[10] __asm__("jgr_ptr_record_book_RECORD_SCORE_XY") __attribute__((aligned(16)));
float RECORD_STORY_SCORE_XY[2] __asm__("jgr_ptr_record_book_RECORD_STORY_SCORE_XY") __attribute__((aligned(16)));
float RECORD_PTS_XY[10] __asm__("jgr_ptr_record_book_RECORD_PTS_XY") __attribute__((aligned(16)));
float RECORD_STORY_PTS_XY[2] __asm__("jgr_ptr_record_book_RECORD_STORY_PTS_XY") __attribute__((aligned(16)));
int RECORD_SCORE_RIGHT[5] __asm__("jgr_ptr_record_book_RECORD_SCORE_RIGHT") __attribute__((aligned(16)));
float RECORD_JET_SCALE[20] __asm__("jgr_ptr_record_book_RECORD_JET_SCALE") __attribute__((aligned(16)));
float RECORD_STORY_SCALE[4] __asm__("jgr_ptr_record_book_RECORD_STORY_SCALE") __attribute__((aligned(16)));
const char * RECORD_TEXT[68] __asm__("jgr_ptr_record_book_RECORD_TEXT") __attribute__((aligned(16)));
unsigned char literal_pool_85[1305] __asm__("jgr_ptr_record_book_literal_pool_85") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {3669378521,1776,48,16,BOOK_OPEN},
    {3789694365,1824,32,16,BOOK_RETURN},
    {3334222519,1856,24,16,BOOK_STATIC},
    {227471442,1888,32,16,BOOK_SELECT},
    {319293258,1920,8,16,BOOK_BACK},
    {537925111,1936,136,16,RECORD_OPEN},
    {3920516964,2080,16,16,RECORD_BACK},
    {584991784,2096,24,16,RECORD_STORY},
    {724840464,2128,32,16,RECORD_JET},
    {2839254119,2160,40,16,RECORD_CITY},
    {3215207050,2208,160,16,RECORD_KIND},
    {3826740307,2368,16,16,OPTION_BACK},
    {766062272,2384,24,16,OPTION_OPEN},
    {1901515215,2416,8,16,OPTION_SOUND_OPEN},
    {4183307948,2432,8,16,OPTION_RUMBLE_OPEN},
    {2462904456,2448,8,16,OPTION_VALUE_OPEN},
    {154144708,2464,8,16,OPTION_RUMBLE_VALUE_OPEN},
    {1290671749,2480,24,16,OPTION_SELECT},
    {1434712768,2512,16,16,OPTION_SOUND1},
    {579127894,2528,16,16,OPTION_SOUND0},
    {1137773061,2544,16,16,OPTION_RUMBLE1},
    {886454931,2560,16,16,OPTION_RUMBLE0},
    {2560558696,2576,8,16,&RECORD_DOWN},
    {797382845,2592,8,16,&RECORD_UP},
    {215716319,2608,120,16,RECORD_PORTRAIT},
    {3406268015,2736,20,16,RECORD_SCROLL},
    {2422874227,2768,20,16,RECORD_PAGE},
    {2950564089,2800,24,16,RECORD_COLOR},
    {133844117,2832,40,16,RECORD_PORTRAIT_XY},
    {446754702,2880,40,16,RECORD_JET_XY},
    {995421243,2928,40,16,RECORD_STORY_XY},
    {2212669020,2976,40,16,RECORD_SCORE_XY},
    {3622628061,3024,8,16,RECORD_STORY_SCORE_XY},
    {2025474296,3040,40,16,RECORD_PTS_XY},
    {50612509,3088,8,16,RECORD_STORY_PTS_XY},
    {392434405,3104,20,16,RECORD_SCORE_RIGHT},
    {2022471490,3136,80,16,RECORD_JET_SCALE},
    {719597150,3216,16,16,RECORD_STORY_SCALE},
    {3219861908,3232,272,16,RECORD_TEXT},
    {2567299158,3504,1305,16,literal_pool_85},
};
int source_data_ptr_record_book_load(void){return source_data_pointers_load_file("source_data_ptr_record_book.jsd",sections,40u,4809u,0x637a36d5u,68u);}
#endif
