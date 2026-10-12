/* Record the real fire renderer's submissions using synthetic model headers. */
#define JGR_SOURCE_DATA_PTR_HUD_TEXT_INC
#define JGR_SOURCE_DATA_PTR_TRICK_HUD_TABLES_INC
static const char *HUD_NOTICE_TEXT[2],*TRICK_NAMES[7][30];
static char HUD_DIGITS[11][3],TRICK_SUFFIX[16];
static int TRICK_POINTS[20],TRICK_BONUS[16];
static unsigned char TRICK_GRIND_IDS[16][2][6],TRICK_AIR_IDS[16][2][4];
static inline void sceGuTexScale(float u,float v){}
#define JGR_TEST_GUM_MODEL 1
#include "../src/enemycombat_draw.c"
#include <assert.h>
#include <stdio.h>
static unsigned calls;
static Chars *expected_bank;
static unsigned expected_image;
static const float (*expected_planes)[4];
static float last_root[16];
void chars_draw_material_pass_clipped(Chars *c,int image,const float *root,int motion,float frame,int pass,const float planes[6][4])
{
    assert(c==expected_bank && image==(int)expected_image && motion==-1 && frame==0);
    assert(c->batch[c->bones[c->chars[image].bone0].batch0].flags&2);
    assert(pass==(int)(calls%2) && planes==expected_planes);
    memcpy(last_root,root,sizeof last_root);calls++;
}
void chars_draw_material_alpha_clipped(Chars *c,int image,const float *root,float alpha,const unsigned short *map,const float planes[6][4])
{assert(0);}
int main(void)
{
    EnemyCombatWorld w={0};JgcHeader header={0};header.n_chars=16;
    JgcChar models[16]={{0}};JgcBone bones[16]={{0}};JgcBatch batches[16]={{0}};
    for(unsigned i=0;i<16;i++){models[i].bone0=i;bones[i].batch0=i;bones[i].nbatch=1;batches[i].flags=409;}
    w.flame_models.chars=models;w.flame_models.bones=bones;w.flame_models.batch=batches;
    float view[16]={0,0,1,0, 0,1,0,0, -1,0,0,0, 0,0,0,1};
    float planes[6][4]={{0}};expected_bank=&w.flame_models;expected_planes=planes;
    w.player_burn=(PlayerBurn){.remaining=4,.scale=1.25f,.pos={10,20,30}};
    enemycombat_draw_player_fire(&w,view,planes);assert(!calls); /* Missing bank is harmless. */
    w.flame_models.h=&header;
    for(unsigned frame=0;frame<16;frame++) {
        expected_image=w.player_burn.image=frame;EnemyCombatWorld before=w;
        /* Draw remains independent of stream pools and does not advance
           simulation. Repeated paused renders preserve frame and size. */
        for(int repeat=0;repeat<3;repeat++)enemycombat_draw_player_fire(&w,view,planes);
        assert(!memcmp(&w,&before,sizeof w));
        for(unsigned i=0;i<16;i++)assert(batches[i].flags==409);
        assert(last_root[3]==10 && last_root[7]==20 && last_root[11]==30 && last_root[15]==1);
        assert(last_root[8]==-1.25f && last_root[5]==1.25f && last_root[2]==1.25f);
    }
    assert(calls==16*3*2);
    w.player_burn.remaining=0;enemycombat_draw_player_fire(&w,view,planes);assert(calls==96);
    w.player_burn.remaining=1;w.player_burn.scale=0;enemycombat_draw_player_fire(&w,view,planes);assert(calls==96);
    puts("player burn render checks passed");return 0;
}
