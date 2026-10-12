#include <pspkernel.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <errno.h>
#include <sys/stat.h>
#include "story.h"
#include "chapter_card.h"
unsigned char story[0x400];
static unsigned char chapter_seen[3];
int story_chapter_card_take(void){return chapter_card_take(chapter_seen,story);}
static int notification_id = -1;
static int mission_souls;
void story_mission_souls_reset(void){mission_souls=0;}
int story_mission_souls(void){return mission_souls;}
void story_graffiti_acquired(int i,int unlock)
{if(i>=0 && i<0x66){if(unlock)story[0x37a+i]=1;notification_id=i;mission_souls++;}}
static void graffiti_unlock(int i) {story_graffiti_acquired(i,1);} /* 04e50c */
int story_notification_take(void) { int i = notification_id; notification_id = -1; return i; }
static int rank_value(int rank, int step) { if (rank < 0) rank = 0; if (rank > 5) rank = 5; return rank * step; }
static void story_graffiti_progress(int mid, int stage, int kind, int rank)
{
    /* FUN_8c036ef8.  Each city keeps its best rank for four/five story chapters
       and three Jet missions.  The ROM tables 8c101b68/6e/7a convert ranks to
       25%, 20%, and 20% contributions respectively. */
    static const int main0[5] = { 0x10, 0xb0, 0x150, 0x1f0, 0x290 };
    static const int first_mid[5] = { 1, 5, 9, 0x16, 0x1b };
    static const int count[5] = { 4, 4, 5, 1, 1 };
    static const int special[5][3] = { {0x88,0x60,0x38}, {0x128,0x100,0xd8}, {0x1c8,0x1a0,0x178}, {0x268,0x240,0x218}, {0x308,0x2e0,0x2b8} };
    int si = stage <= 2 ? stage : stage == 4 ? 3 : stage == 5 ? 4 : -1; /* JMDc stage index -> five graffiti cities */
    if (si < 0) return;
    if (kind == 0 && mid >= first_mid[si] && mid < first_mid[si] + count[si]) {
        int o = main0[si] + (mid - first_mid[si]) * 8; if (rank > story[o]) story[o] = (unsigned char)rank;
    } else {
        int k = kind == 1 ? 0 : kind == 2 ? 1 : kind == 8 ? 2 : -1;
        if (k >= 0 && special[si][k] >= 0 && rank > story[special[si][k]]) story[special[si][k]] = (unsigned char)rank;
    }
    if (si < 2) { int o = main0[si], p = 0; for (int i = 0; i < 4; i++) p += rank_value(story[o + i*8], 5); story[0x3e0 + si] = p; }
    else if (si == 2) { int p = 0; for (int i = 0; i < 5; i++) p += rank_value(story[0x150 + i*8], 4); story[0x3e2] = p; }
    else story[0x3e0 + si] = rank_value(story[main0[si]], 20);
    /* The special-mission delta table at 8c101b74 is six zero bytes in the US
       executable. Preserve their ranks above, but they do not alter percent. */
    if (story[0x3e0] >= 100 && !story[0x337]) { story[0x337]=1; graffiti_unlock(0x20); graffiti_unlock(0x42); graffiti_unlock(0x64); }
    if (story[0x3e1] >= 100 && !story[0x336]) { story[0x336]=1; graffiti_unlock(0x1e); graffiti_unlock(0x40); graffiti_unlock(0x62); }
    if (story[0x3e2] >= 100 && !story[0x338]) { story[0x338]=1; graffiti_unlock(0x1f); graffiti_unlock(0x41); graffiti_unlock(0x63); }
    if (*(int *)(story+8) != 0 && story[0x3e3] >= 100 && story[0x3e4] >= 100 && !story[0x339]) { story[0x339]=1; graffiti_unlock(0x21); graffiti_unlock(0x43); graffiti_unlock(0x65); }
    if (!story[0x33a] && story[0x35b] == 1 && story[0x3e0]>=100 && story[0x3e1]>=100 && story[0x3e2]>=100 && story[0x3e3]>=100 && story[0x3e4]>=100) story[0x33a]=2;
}
void story_reset(void)
{
    memset(story, 0, sizeof story);
    memset(chapter_seen,0,sizeof chapter_seen);
    story[0x32c]=1; /* 02c7d0: Beat is present in the garage from a new game. */
    /* FUN_8c02c7d0 new-game defaults: selections 0x3e5..0x3e7 = 0, graffiti flags 0..0x59 cleared, then Beat's S / L / XL tags */
    graffiti_unlock(0); graffiti_unlock(0x22); graffiti_unlock(0x44);
    notification_id = -1;   /* new-game defaults are established before the garage notification task is active */
    story_mission_souls_reset();
}
void story_mission_cleared(int mid, int chr, int stage, int kind, int rank)   /* FUN_8c036ef8: chr = JMDc +0x18 */
{
    if (mid < 0) return; /* invalid host input, not a source mission */
    /* 036f10: only ids <32 have chapter flags. Jet missions use id 32;
       writing 33b+32 overwrites the Kogane/Benten route byte at 35b! */
    if (mid >= 0x20) goto progress;
    story[0x33b + mid] = 1;   /* flag[0x33b + chapter id] = 1 */
    if (mid > 0x1b) goto progress;
    switch (mid) {
    case 0: if (chr == 0x12) { story[0x32d] = 1; graffiti_unlock(1); graffiti_unlock(0x23); graffiti_unlock(0x45); }          /* Gum joins */
            else if (chr == 0x15) { story[0x32e] = 1; graffiti_unlock(2); graffiti_unlock(0x24); graffiti_unlock(0x46); }      /* Tab joins */
            else goto progress;
            break;
    case 3: story[0x32f] = 2; story[0x332] = 2; goto progress;
    case 4: if (story[0x331] != 0) goto progress; story[0x331] = 2; break;
    case 5: if (story[0x335] == 0) story[0x335] = 2; if (story[0x35b] != 0) goto progress; story[0x35b] = 1; break;
    case 8: if (story[0x330] != 0) goto progress; story[0x330] = 2; break;
    case 9: if (story[0x333] == 0) story[0x333] = 2; if (story[0x35b] != 0) goto progress; story[0x35b] = 2; break;
    case 0xb: if (story[0x334] != 0) goto progress; story[0x334] = 2; break;
    case 0xe: if (chr != 0x1a) goto progress; story[0x331] = 1; graffiti_unlock(4); graffiti_unlock(0x26); graffiti_unlock(0x48); break;
    case 0xf: if (chr != 0x1c) goto progress; story[0x332] = 1; story[0x32f] = 1; graffiti_unlock(3); graffiti_unlock(0x25); graffiti_unlock(0x47); graffiti_unlock(7); graffiti_unlock(0x29); graffiti_unlock(0x4b); break;
    case 0x10: if (chr == 7) { story[0x335] = 1; graffiti_unlock(5); graffiti_unlock(0x27); graffiti_unlock(0x49); } break;
    case 0x11: if (chr == 0x18) story[0x33a] = 1; break;
    case 0x12: story[0x330] = 1; graffiti_unlock(9); graffiti_unlock(0x2b); graffiti_unlock(0x4d); break;
    case 0x13: if (chr == 10) { story[0x333] = 1; graffiti_unlock(6); graffiti_unlock(0x28); graffiti_unlock(0x4a); } break;
    case 0x14: story[0x334] = 1; graffiti_unlock(8); graffiti_unlock(0x2a); graffiti_unlock(0x4c); break;
    case 0x15: { 
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_story_c__z_0.inc"
#else

#include "generated/source_data/story_c__z_0.inc"

#endif

                 for (unsigned i = 0; i < sizeof z / sizeof z[0]; i++) story[z[i]] = 0;
                 int *loops = (int *)(story + 8); if (*loops != -1) (*loops)++; } break;   /* the ending: clear the chapter flags for the next loop, count loops */
    default: break;
    }
progress:
    story_graffiti_progress(mid, stage, kind, rank);
    story_mission_souls_reset(); /* 0379fe: rewards must not leak into the next result */
}
void story_entries(unsigned char a[24])
{
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_story_c__tbl_0.inc"
#else

#include "generated/source_data/story_c__tbl_0.inc"

#endif
   /* 0x8c10dd10 */
    const unsigned char *f = story; int w8 = *(const int *)(story + 8);
    for (int i = 0; i < 24; i++) a[i] = 2;
    if (f[0x33c] != 1) a[0] = 0;
    if (f[0x33c] == 1) { if (f[0x33d] != 1) a[1] = 0; if (f[0x340] != 1) a[4] = 0; if (f[0x344] != 1) a[8] = 0; }
    if (f[0x33d] == 1 && f[0x342] == 1 && f[0x346] == 1 && f[0x33e] != 1) a[2] = 0;
    if (f[0x340] == 1 && f[0x341] != 1) a[5] = 0;
    if (f[0x341] == 1 && f[0x342] != 1) a[6] = 0;
    if (f[0x344] == 1 && f[0x345] != 1) a[9] = 0;
    if (f[0x345] == 1 && f[0x346] != 1) a[10] = 0;
    if (f[0x356] == 1) { if (f[0x33f] != 1) a[3] = 0; if (f[0x343] != 1) a[7] = 0; if (f[0x347] != 1) a[11] = 0; }
    if (f[0x33f] == 1 && f[0x343] == 1 && f[0x347] == 1 && f[0x348] != 1) a[12] = 0;
    if (f[0x348] == 1 && f[0x350] != 1) a[13] = 0;
    if (f[tbl[0] + 0x33b] == 1 || w8) a[14] = 0;
    if (f[tbl[4] + 0x33b] == 1 || w8) a[17] = 0;
    if (f[tbl[8] + 0x33b] == 1 || w8) a[20] = 0;
    if (f[tbl[1] + 0x33b] == 1 || w8) a[15] = 0;
    if (f[tbl[5] + 0x33b] == 1 || w8) a[18] = 0;
    if (f[tbl[9] + 0x33b] == 1 || w8) a[21] = 0;
    if (f[0x331] == 1) a[16] = 0;
    if (f[0x330] == 1) a[19] = 0;
    if (f[0x334] == 1) a[22] = 0;
    if (w8) a[23] = 0;
}
int story_entry_stage(int e, int *chapter, int *special)
{
    static const signed char T[24][3] = { {0,0,-1},{0,1,-1},{0,2,10},{0,3,-1}, {1,0,-1},{1,1,-1},{1,2,12},{1,3,-1}, {2,0,-1},{2,1,-1},{2,2,11},{2,3,-1},
        {2,4,-1},{3,0,-1}, {0,7,-1},{0,8,-1},{0,9,0}, {1,7,-1},{1,8,-1},{1,9,1}, {2,7,-1},{2,8,-1},{2,9,2}, {-1,0,-1} };
    if (e < 0 || e >= 24) return -1;
    *chapter = T[e][1]; *special = T[e][2]; return T[e][0];
}
void story_grind_entries(unsigned char a[9])
{
    /* 0f6d48: chapter-two progression and post-ending Jet missions. */
    memset(a,2,9);
    if(story[0x33e]==1) {
        if(story[0x351]!=1)a[0]=0;
        else if(story[0x356]!=1)a[1]=0;
    }
    int completed;memcpy(&completed,story+8,sizeof completed);
    if(completed)for(int i=2;i<9;i++)a[i]=0;
}
int story_grind_entry_stage(int e,int *chapter,int *special)
{
    *chapter=*special=-1;
    if((unsigned)e>=8)return -1; /* Return-to-map is handled by the caller. */
    if(e<2){*chapter=0;return 4+e;}
    int city=(e-2)/3;
    *chapter=1+(e-2)%3;
    if(*chapter==3)*special=city ? 6:3;
    return 4+city;
}
int story_chapter_file(int stage, int chapter)
{
    /* 1048e8: all seven resource lists, each entry {17,file,34,0}.
       Do not let an out-of-range chapter alias the next stage's bank. */
    static const int base[7] = {0,12,24,36,40,46,52};
    static const int count[7] = {12,12,12,4,6,6,1};
    if ((unsigned)stage >= 7 || (unsigned)chapter >= (unsigned)count[stage]) return -1;
    return base[stage] + chapter;
}
int story_special_stage(void) { return story[0x351]==1 ? 5:4; }
int story_record_result(int mid,int player,int stage,int kind,int rank,int total,int commit)
{
    /* Each entry is {u32 score/u32 centiseconds, u8 rank,player,mid,pad}.
       Source shifts only seven bytes of a record, preserving padding. */
    if((unsigned)player>=16 || total<0 || (unsigned)rank>5)return 5;
    int city=stage<=2 ? stage:stage==4 ? 3:stage==5 ? 4:-1;
    if(city<0 || stage<0)return 5;
    unsigned value=(unsigned)total;
    int jet=kind==1 || kind==2 || kind==8,base;
    if(!jet) {
        static const int first[]={1,5,9,22,27},count[]={4,4,5,1,1};
        if(mid<first[city] || mid>=first[city]+count[city])return 5;
        base=12+city*160+(mid-first[city])*8;
        unsigned old;memcpy(&old,story+base,4);
        if(value<=old)return 5;
        if(commit){memcpy(story+base,&value,4);story[base+4]=rank;story[base+5]=player;story[base+6]=mid;}
        return 5; /* main chapters do not set result +78 to zero */
    }
    base=12+(city*4+(kind==1 ? 3:kind==2 ? 2:1))*40;
    /* SH-4 div 30, mul 100, add .5, floor; preserve f32 evaluation. */
    if(kind==8){float t=(float)total/30.f;t=t*100.f;t=t+.5f;value=(unsigned)floorf(t);}
    int place=5;
    for(int i=0;i<5;i++) {
        unsigned old;memcpy(&old,story+base+i*8,4);
        if(kind==8 ? !old || value<old:old<value){place=i;break;}
    }
    if(commit && place<5) {
        for(int i=4;i>place;i--)memcpy(story+base+i*8,story+base+(i-1)*8,7);
        int at=base+place*8;memcpy(story+at,&value,4);story[at+4]=rank;story[at+5]=player;story[at+6]=mid;
    }
    return place;
}
StoryResultRoute story_result_route(int mid,int character_mode,int kind,int new_record)
{
    /* 038518 is integer-only. +18 is the mission's character/music MODE,
       not the selected player's character id. */
    int branch=-1;
    switch(mid) {
    case 0: branch=character_mode==0x12 ? 1:character_mode==0x15 ? 2:-1;break;
    case 0xe: branch=character_mode==0x1a ? 5:-1;break;
    case 0xf: branch=character_mode==0x1c ? 3:6;break;
    case 0x10: branch=character_mode==7 ? 9:-1;break;
    case 0x11: branch=character_mode==0x18 ? 14:-1;break;
    case 0x12: branch=4;break;
    case 0x13: branch=character_mode==10 ? 7:-1;break;
    case 0x14: branch=8;break;
    }
    
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_story_c__scenes_0.inc"
#else

#include "generated/source_data/story_c__scenes_0.inc"

#endif
 /* 17951c */
    StoryResultRoute r={branch,branch>0 ? scenes[branch]:-1,7,-1};
    if(branch>0) {
        r.mode=(branch==1 || branch==6) ? 8:(branch==2 || branch==3) ? 7:15;
        if(branch==6)r.character=3; /* 036a62: Combo for the next S1M07 phase */
    } else if(kind==1 || kind==2 || kind==8)r.mode=new_record ? 15:5;
    return r;
}
int story_result_destination(int stage,int mid,int character_mode,int kind,int new_record)
{
    /* 0361ec case 0 bypasses every tally and recruitment branch in stage 3. */
    return stage==3 ? 14:story_result_route(mid,character_mode,kind,new_record).mode;
}
int story_phase_retire(int *retired,int writable,int phase,
                       int mid,int character_mode,int stage,int kind,int rank)
{
    if(!retired || *retired || !writable || phase!=1 || mid<0)return 0;
    *retired=1;
    story_mission_cleared(mid,character_mode,stage,kind,rank);
    return 1;
}
const char *story_file_name(int file)
{
    static const char *N[53] = { "S1M01","S1M02","S1M03","S1M04","S1M05","S1M06","S1M07","S1M08","S1M09","S1M10","S1M11","S1M12",
        "S2M01","S2M02","S2M03","S2M04","S2M05","S2M06","S2M07","S2M08","S2M09","S2M10","S2M11","S2M12",
        "S3M01","S3M02","S3M03","S3M04","S3M05","S3M06","S3M07","S3M08","S3M09","S3M10","S3M11","S3M12",
        "S4M01","S4M02","S4M03","S4M04","S5M01","S5M02","S5M03","S5M04","S5M05","S5M06","S6M01","S6M02","S6M03","S6M04","S6M05","S6M06","S7M01" };
    return (file >= 0 && file < 53) ? N[file] : 0;
}
#include "story_io.inc"
int story_load(const char *path)
{
    unsigned char loaded[sizeof story];int error=story_read_file(path,loaded);
    if(error){
        char backup[256];
        if(!story_io_path(backup,path,".bak") || story_read_file(backup,loaded))return error;
    }
    memcpy(story,loaded,sizeof story);
    chapter_seen_load(chapter_seen,story);
    notification_id=-1;return 0;
}
