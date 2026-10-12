/* Real bank residency, attack creation and mission teardown, with only the
   asset loaders, pool-failure injection and unrelated sound stubbed. */
#include <stdlib.h>
/* HUD tables are unused here. Supply native-width host declarations instead
   of the exported tables' PSP pointer-size assertions. */
#define JGR_SOURCE_DATA_PTR_HUD_TEXT_INC
#define JGR_SOURCE_DATA_PTR_TRICK_HUD_TABLES_INC
static const char *HUD_NOTICE_TEXT[2],*TRICK_NAMES[7][30];
static char HUD_DIGITS[11][3],TRICK_SUFFIX[16];
static int TRICK_POINTS[20],TRICK_BONUS[16];
static unsigned char TRICK_GRIND_IDS[16][2][6],TRICK_AIR_IDS[16][2][4];
static int fail_fire_pool;
static void *prewarm_calloc(size_t n,size_t size)
{return fail_fire_pool ? NULL:calloc(n,size);}
#define calloc prewarm_calloc
#include "../src/enemycombat.c"
#undef calloc
#include <assert.h>
#include <limits.h>

static unsigned loads,frees;
static int primary_error;
static JgcHeader bank_header;
static JgcChar bank_chars[45];
static JgcMotion bank_motions[2];
enum {TEST_WARNING,TEST_GAS,TEST_FLAME,TEST_BURNING,TEST_CHAIN,TEST_BANKS};
static const char *extra_names[]={"enemywarning.jgc","enemygas.jgc","eventflare.jgc","burning.jgc","tabanfx.bin"};
static int extra_errors[TEST_BANKS];
static unsigned extra_loads[TEST_BANKS],extra_frees[TEST_BANKS];
static JgcHeader extra_headers[TEST_BANKS];
static JgcChar extra_chars[TEST_BANKS][16];
static unsigned char chain_pixels[8];
static int chain_images;
static const char *filename(const char *path)
{const char *slash=strrchr(path,'/');return slash ? slash+1:path;}
static int extra_index(const char *path)
{for(int i=0;i<TEST_BANKS;i++)if(!strcmp(filename(path),extra_names[i]))return i;return -1;}

int chars_load(Chars *c,const char *path)
{
    int index=extra_index(path);assert(!strchr(path,'/'));
    assert(index>=0 || !strcmp(filename(path),"enemyprojectiles.jgc"));
    loads++;
    int error=index>=0 ? extra_errors[index]:primary_error;
    if(index>=0)extra_loads[index]++;
    if(error)return error;
    assert(!c->base);c->h=index>=0 ? extra_headers+index:&bank_header;c->base=(unsigned char *)c->h;
    c->chars=index>=0 ? extra_chars[index]:bank_chars;c->motions=bank_motions;return 0;
}
void chars_free(Chars *c)
{if(c->base){if(c->h!=&bank_header){assert(c->h>=extra_headers && c->h<extra_headers+TEST_BANKS);extra_frees[c->h-extra_headers]++;}frees++;}memset(c,0,sizeof *c);}
void audio_free_enemies(void){}
void audio_enemy_chain_tick(int reset){assert(reset==1);}
void audio_enemy_held_tick(int reset){assert(reset==1);}
int tex2d_load(Tex2dSet *s,const char *path)
{
    assert(extra_index(path)==TEST_CHAIN && !s->data);loads++;extra_loads[TEST_CHAIN]++;
    assert(!strchr(path,'/'));int error=extra_errors[TEST_CHAIN];
    if(error)return error;
    s->n=chain_images;s->data=chain_pixels;return 0;
}
void tex2d_free(Tex2dSet *s)
{if(s->data){assert(s->data==chain_pixels);extra_frees[TEST_CHAIN]++;frees++;}memset(s,0,sizeof *s);}

static void reset_loader(int primary,unsigned count)
{
    loads=frees=0;primary_error=primary;
    memset(&bank_header,0,sizeof bank_header);bank_header.n_chars=count;
    memset(bank_chars,0,sizeof bank_chars);bank_chars[12].motion0=1;
    bank_motions[0].nframes=7;bank_motions[1].nframes=11;
    fail_fire_pool=0;chain_images=1;
    memset(extra_errors,0,sizeof extra_errors);
    memset(extra_loads,0,sizeof extra_loads);memset(extra_frees,0,sizeof extra_frees);
    memset(extra_headers,0,sizeof extra_headers);memset(extra_chars,0,sizeof extra_chars);
    extra_headers[TEST_WARNING].n_chars=1;extra_headers[TEST_GAS].n_chars=10;
    extra_chars[TEST_GAS][8].nbones=extra_chars[TEST_GAS][9].nbones=6;
    extra_chars[TEST_GAS][1].motion0=1;
    extra_headers[TEST_FLAME].n_chars=16;
    extra_headers[TEST_BURNING].n_chars=13;extra_headers[TEST_BURNING].n_tex=10;
}
static unsigned shot(EnemyCombatWorld *w,int mode)
{return shot_create(w,(float[3]){0,10,0},(float[3]){20,10,0},17,mode);}

static void test_relevant_types(void)
{
    const int required[]={5,7,8,10,13,15,16,17,22,24,26,28,29,30,31,32};
    reset_loader(0,45);EnemyCombatWorld w={0};
    for(int type=-2;type<65;type++) {
        int expected=0;for(unsigned i=0;i<sizeof required/sizeof *required;i++)expected|=type==required[i];
        assert(enemycombat_type_uses_projectiles(type)==expected);
        if(!expected) {
            if(enemycombat_type_uses_projectiles(type))enemycombat_prepare_projectiles(&w);
            assert(!loads && !w.attempted);
        }
    }
    assert(!enemycombat_type_uses_projectiles(INT_MIN));
    assert(!enemycombat_type_uses_projectiles(INT_MAX));
    assert(!enemycombat_prepare_projectiles(NULL));
}
static void test_prewarm_and_attacks_share_bank(void)
{
    reset_loader(0,45);EnemyCombatWorld w={0};
    assert(shot(&w,2) && !loads && !w.attempted); /* LOS alone needs no art. */
    assert(enemycombat_prepare_projectiles(&w));
    assert(loads==1 && w.attempted && !w.error && w.models.h==&bank_header);
    assert(w.shots.impact_frames==7 && w.waves.hit_frames==11);
    assert(enemycombat_prepare_projectiles(&w) && enemycombat_prepare_denki(&w));
    for(int i=0;i<20;i++)assert(shot(&w,i%2));
    assert(loads==1 && !frees && w.shots.created==21);
    enemycombat_reset(&w);
    assert(!w.shots.created && w.shots.impact_frames==7 && w.waves.hit_frames==11);
    assert(shot(&w,1) && loads==1); /* Respawn/reset retains residency. */
    enemycombat_free(&w);
    assert(frees==1 && !w.models.base && !w.attempted && !w.error);
    assert(enemycombat_prepare_projectiles(&w) && loads==2);
    enemycombat_free(&w);assert(frees==2);
}
static void test_failed_prewarm_preserves_lazy_first_use(void)
{
    reset_loader(-3,45);EnemyCombatWorld w={0};
    assert(!enemycombat_prepare_projectiles(&w) && w.error==-3 && loads==1 && !w.attempted);
    assert(shot(&w,2) && loads==1); /* Queries still work without render data. */
    primary_error=0;
    assert(shot(&w,0) && loads==2 && !w.error && w.attempted);
    assert(shot(&w,1) && enemycombat_prepare_projectiles(&w) && loads==2);
    enemycombat_free(&w);assert(!w.attempted && !w.error);
    reset_loader(-3,45);
    assert(!enemycombat_prepare_projectiles(&w) && loads==1 && !w.attempted);
    assert(!shot(&w,1) && loads==2 && w.error==-3 && w.attempted);
    primary_error=0;
    assert(!shot(&w,0) && !enemycombat_prepare_projectiles(&w) && loads==2);
    enemycombat_free(&w);
    assert(enemycombat_prepare_projectiles(&w) && loads==3);
    enemycombat_free(&w);assert(frees==1);
}
static void test_bad_or_missing_bank(void)
{
    reset_loader(0,44);EnemyCombatWorld w={0};
    assert(!enemycombat_prepare_projectiles(&w));
    assert(w.error==-2 && loads==1 && frees==1 && !w.models.base && !w.attempted);
    assert(!shot(&w,1) && loads==2 && frees==2);
    assert(!shot(&w,1) && loads==2);enemycombat_free(&w);assert(frees==2);
    reset_loader(-1,45);
    assert(!enemycombat_prepare_projectiles(&w) && w.error==-1 && loads==1 && !w.attempted);
    assert(!shot(&w,1) && loads==2);
    assert(!shot(&w,1) && loads==2);enemycombat_free(&w);assert(!frees);
}
static void test_presentation_release_preserves_live_effects(void)
{
    reset_loader(0,45);EnemyCombatWorld w={0};
    assert(enemycombat_prepare_projectiles(&w));
    w.shots.serial=47;w.shots.created=29;w.shots.shots[1].token=42;
    w.shots.shots[1].feedback=1;w.shots.context=&w;w.waves.context=&w;
    w.random=1234;w.pending_attribute=26;
    int *active[]={&w.shots.shots[ENEMY_SHOT_CAPACITY-1].active,
        &w.missiles.missiles[ENEMY_MISSILE_CAPACITY-1].active,
        &w.shells.shells[ENEMY_SHELL_CAPACITY-1].active,
        &w.waves.waves[ENEMY_WAVE_CAPACITY-1].active,
        &w.blasts[sizeof w.blasts/sizeof *w.blasts-1].active};
    for(unsigned i=0;i<sizeof active/sizeof *active;i++) {
        *active[i]=1;EnemyCombatWorld before=w;
        assert(!enemycombat_release_idle_projectiles(&w));
        assert(!memcmp(&w,&before,sizeof w) && !frees);*active[i]=0;
    }
    EnemyCombatWorld before=w;
    assert(enemycombat_release_idle_projectiles(&w) && frees==1);
    assert(!w.models.h && !w.models.base && !w.attempted && !w.error);
    memset(&before.models,0,sizeof before.models);before.attempted=before.error=0;
    assert(!memcmp(&w,&before,sizeof w)); /* No actor, token or callback reset. */
    assert(w.shots.impact_frames==7 && w.waves.hit_frames==11);
    assert(!enemycombat_release_idle_projectiles(&w) && frees==1);
    assert(enemycombat_prepare_projectiles(&w) && loads==2);
    assert(shot(&w,1) && loads==2);enemycombat_free(&w);
    w.attempted=1;w.error=-3;before=w;
    assert(!enemycombat_release_idle_projectiles(&w) && !memcmp(&w,&before,sizeof w));
    assert(!enemycombat_release_idle_projectiles(NULL));
}
static const unsigned extra_kind[TEST_BANKS]={17,4,23,6,21};
static const unsigned extra_bit[TEST_BANKS]={ENEMY_GRAPHICS_WARNING,ENEMY_GRAPHICS_GAS,
    ENEMY_GRAPHICS_FLAME,ENEMY_GRAPHICS_BURNING,ENEMY_GRAPHICS_CHAIN};
static uint64_t kind(unsigned type){return UINT64_C(1)<<type;}
static int lazy_extra(EnemyCombatWorld *w,unsigned bank)
{
    switch(bank) {
    case TEST_WARNING:return ensure_warning_models(w);
    case TEST_GAS:return ensure_gas_models(w);
    case TEST_FLAME:return enemycombat_prepare_flames(w);
    case TEST_BURNING:return enemycombat_prepare_burning(w);
    default:return ensure_taban_texture(w);
    }
}
static int *extra_attempted(EnemyCombatWorld *w,unsigned bank)
{
    switch(bank) {
    case TEST_WARNING:return &w->warning_attempted;
    case TEST_GAS:return &w->gas_attempted;
    case TEST_FLAME:return &w->flame_attempted;
    case TEST_BURNING:return &w->burning_attempted;
    default:return &w->taban_attempted;
    }
}
static void test_extra_bank_selection_and_reuse(void)
{
    /* Distinguish walking riot/Para/fire enemies and parked formation
       creators from similarly numbered attack sound families. */
    unsigned expected[64]={0};
    const unsigned warnings[]={7,8,9,13,15,16,17,24,25,26,28,29,32};
    for(unsigned i=0;i<sizeof warnings/sizeof *warnings;i++)expected[warnings[i]]=ENEMY_GRAPHICS_WARNING;
    expected[4]=expected[27]=ENEMY_GRAPHICS_GAS;expected[21]=ENEMY_GRAPHICS_CHAIN;
    expected[6]|=ENEMY_GRAPHICS_BURNING|ENEMY_GRAPHICS_FLAME;
    expected[9]|=ENEMY_GRAPHICS_BURNING|ENEMY_GRAPHICS_FLAME;
    expected[25]|=ENEMY_GRAPHICS_BURNING|ENEMY_GRAPHICS_FLAME;
    expected[23]=ENEMY_GRAPHICS_FLAME;
    assert(!enemycombat_graphics_for_kinds(0));
    for(unsigned i=0;i<64;i++)assert(enemycombat_graphics_for_kinds(kind(i))==expected[i]);
    reset_loader(0,45);EnemyCombatWorld w={0};
    assert(!enemycombat_prepare_graphics(&w,kind(0)|kind(1)|kind(3)|kind(44)) && !loads);
    assert(!enemycombat_prepare_graphics(NULL,UINT64_MAX));
    assert(enemycombat_prepare_graphics(&w,UINT64_MAX)==31u && loads==TEST_BANKS);
    assert(w.fires && w.gases.frames==11);
    assert(enemycombat_prepare_graphics(&w,UINT64_MAX)==31u && loads==TEST_BANKS);
    for(unsigned i=0;i<TEST_BANKS;i++)assert(lazy_extra(&w,i) && extra_loads[i]==1);
    enemycombat_reset(&w);
    assert(enemycombat_prepare_graphics(&w,UINT64_MAX)==31u && loads==TEST_BANKS);
    enemycombat_free(&w);
    assert(!w.fires && frees==TEST_BANKS);
    for(unsigned i=0;i<TEST_BANKS;i++)assert(!*extra_attempted(&w,i) && extra_frees[i]==1);
}
static void test_extra_prewarm_retry_and_memoization(void)
{
    for(unsigned bank=0;bank<TEST_BANKS;bank++) {
        reset_loader(0,45);EnemyCombatWorld w={0};extra_errors[bank]=-3;
        unsigned wanted=enemycombat_graphics_for_kinds(kind(extra_kind[bank]));
        unsigned ready=enemycombat_prepare_graphics(&w,kind(extra_kind[bank]));
        assert(ready==(wanted&~extra_bit[bank]));
        assert(!*extra_attempted(&w,bank) && extra_loads[bank]==1);
        extra_errors[bank]=0;
        assert(lazy_extra(&w,bank) && extra_loads[bank]==2);
        assert(enemycombat_prepare_graphics(&w,kind(extra_kind[bank]))==wanted && extra_loads[bank]==2);
        enemycombat_free(&w);
        /* An actual attack-time failure still memoizes, even if a later
           cutscene handoff tries optional preparation again. */
        reset_loader(0,45);extra_errors[bank]=-3;
        assert(!lazy_extra(&w,bank) && *extra_attempted(&w,bank));
        extra_errors[bank]=0;
        assert(!(enemycombat_prepare_graphics(&w,kind(extra_kind[bank]))&extra_bit[bank]));
        assert(extra_loads[bank]==1 && !lazy_extra(&w,bank));
        enemycombat_free(&w);
    }
}
static void test_extra_invalid_or_missing_banks(void)
{
    for(unsigned bank=0;bank<TEST_BANKS;bank++) {
        reset_loader(0,45);EnemyCombatWorld w={0};
        unsigned valid_count=extra_headers[bank].n_chars;
        if(bank==TEST_CHAIN)chain_images=2;else extra_headers[bank].n_chars=0;
        assert(!(enemycombat_prepare_graphics(&w,kind(extra_kind[bank]))&extra_bit[bank]));
        assert(!*extra_attempted(&w,bank) && extra_loads[bank]==1 && extra_frees[bank]==1);
        extra_headers[bank].n_chars=valid_count;chain_images=1;
        assert(lazy_extra(&w,bank) && extra_loads[bank]==2);enemycombat_free(&w);
        reset_loader(0,45);extra_errors[bank]=-1;
        assert(!(enemycombat_prepare_graphics(&w,kind(extra_kind[bank]))&extra_bit[bank]));
        assert(extra_loads[bank]==1);enemycombat_free(&w);
    }
}
static void test_fire_pool_and_dependency_failures(void)
{
    reset_loader(0,45);EnemyCombatWorld w={0};fail_fire_pool=1;
    assert(enemycombat_prepare_graphics(&w,kind(6))==ENEMY_GRAPHICS_FLAME);
    assert(!w.fires && !w.burning_models.h && !w.burning_attempted && w.burning_error==-3);
    assert(extra_frees[TEST_BURNING]==1 && extra_frees[TEST_FLAME]==0);
    fail_fire_pool=0;
    assert(enemycombat_prepare_burning(&w) && extra_loads[TEST_BURNING]==2 && extra_loads[TEST_FLAME]==1);
    enemycombat_free(&w);
    reset_loader(0,45);extra_errors[TEST_FLAME]=-3;
    assert(!enemycombat_prepare_graphics(&w,kind(6)));
    assert(!w.burning_attempted && !w.flame_attempted && !w.fires);
    assert(!extra_loads[TEST_BURNING] && extra_loads[TEST_FLAME]==1);
    extra_errors[TEST_FLAME]=0;
    assert(enemycombat_prepare_burning(&w) && extra_loads[TEST_BURNING]==1 && extra_loads[TEST_FLAME]==2);
    enemycombat_free(&w);
}
static void test_extra_release_preserves_live_owners(void)
{
    reset_loader(0,45);EnemyCombatWorld w={0};Gang gangs[2]={{0}};
    assert(enemycombat_prepare_graphics(&w,UINT64_MAX)==31u);
    w.random=123;w.gases.created=4;w.gases.context=&w;w.fire_serial=99;w.fire_tick=42;
    /* The last pool slots must protect banks just like the first. */
    w.gases.gas[ENEMY_GAS_CAPACITY-1].active=1;w.warning.scale=.01f;
    w.fires[ENEMY_FIRE_CAPACITY-1].token=55;gangs[1].active=1;gangs[1].kind=21;
    EnemyCombatWorld before=w;
    assert(!enemycombat_release_idle_graphics(&w,gangs,2));
    assert(!memcmp(&w,&before,sizeof w) && !frees);
    w.fires[ENEMY_FIRE_CAPACITY-1].token=0;w.burns[ENEMY_BURN_CAPACITY-1].life=-1;
    assert(!enemycombat_release_idle_graphics(&w,gangs,2));
    w.burns[ENEMY_BURN_CAPACITY-1].life=0;gangs[0].active=1;
    const int fire_types[]={6,9,25};
    for(unsigned i=0;i<sizeof fire_types/sizeof *fire_types;i++) {
        gangs[0].kind=fire_types[i];before=w;
        assert(!enemycombat_release_idle_graphics(&w,gangs,2));assert(!memcmp(&w,&before,sizeof w));
    }
    gangs[0].kind=23;
    assert(enemycombat_release_idle_graphics(&w,gangs,2)==ENEMY_GRAPHICS_BURNING);
    assert(!w.fires && w.flame_models.h);
    w.warning.scale=0;w.gang.warning=1;
    assert(!enemycombat_release_idle_graphics(&w,gangs,2));
    w.gang.warning=0;w.gang.report=1;
    assert(!enemycombat_release_idle_graphics(&w,gangs,2));
    w.gang.report=0;w.gases.gas[ENEMY_GAS_CAPACITY-1].active=0;
    gangs[0].active=gangs[1].active=0;
    assert(enemycombat_release_idle_graphics(&w,gangs,2)==(31u&~ENEMY_GRAPHICS_BURNING));
    assert(w.random==123 && w.gases.created==4 && w.gases.frames==11 && w.gases.context==&w);
    assert(w.fire_serial==99 && w.fire_tick==42 && !w.warning_attempted && !w.gas_attempted && !w.taban_attempted && !w.flame_attempted);
    assert(!enemycombat_release_idle_graphics(&w,NULL,0));
    assert(enemycombat_prepare_graphics(&w,UINT64_MAX)==31u);
    before=w;assert(!enemycombat_release_idle_graphics(&w,NULL,1));assert(!memcmp(&w,&before,sizeof w));
    enemycombat_free(&w);
    for(unsigned i=0;i<TEST_BANKS;i++)assert(extra_loads[i]==2 && extra_frees[i]==2);
    assert(!enemycombat_release_idle_graphics(NULL,NULL,0));
    reset_loader(0,45);extra_errors[TEST_GAS]=-3;assert(!ensure_gas_models(&w));before=w;
    assert(!enemycombat_release_idle_graphics(&w,NULL,0));assert(!memcmp(&w,&before,sizeof w));
    enemycombat_free(&w);
}
static int torso_valid=1;
int player_sound_points(const Player *p,float out[2][3])
{out[0][0]=p->x;out[0][1]=p->y+5;out[0][2]=p->z;return torso_valid;}
static void test_player_burn_lifetime_and_residency(void)
{
    reset_loader(0,45);Player p={0};p.health=10;p.x=20;p.y=30;p.z=40;
    EnemyCombatWorld w={0};w.player=&p;
    /* Bullet overlays and rejected fire contacts never increment fire_hits. */
    p.burn_ticks=90;p.bullet_hits=1;player_burn_tick(&w);
    assert(!w.player_burn.remaining && !loads);
    p.fire_hits++;player_burn_tick(&w);
    assert(w.player_burn.remaining>4.9f && w.player_burn.image==0);
    assert(w.player_burn.pos[0]==20 && w.player_burn.pos[1]==35 && w.player_burn.pos[2]==40);
    assert(loads==1 && w.flame_models.h && !w.fires && !w.burning_models.h);
    assert(!enemycombat_release_idle_graphics(&w,NULL,0));
    p.x=21;player_burn_tick(&w);assert(w.player_burn.pos[0]==20);
    p.x=22;player_burn_tick(&w);assert(w.player_burn.pos[0]==21);
    for(int tick=3;tick<90;tick++) {
        player_burn_tick(&w);
        if(tick<89)assert(w.player_burn.remaining>0 && w.player_burn.image==(unsigned)(tick%16));
        if(tick<71)assert(w.player_burn.scale>=1 && w.player_burn.scale<1.5f);
        if(tick==88)assert(w.player_burn.scale<.6f);
    }
    /* Source single-precision subtraction leaves a positive rounding tail
       at tick 90; 043cf8 retires the attachment on tick 91. */
    assert(w.player_burn.remaining>0 && w.player_burn.remaining<.00001f);
    player_burn_tick(&w);
    assert(!w.player_burn.remaining && !w.player_burn.scale && loads==1);
    player_burn_tick(&w);assert(!w.player_burn.remaining); /* No retrigger. */
    assert(enemycombat_release_idle_graphics(&w,NULL,0)==ENEMY_GRAPHICS_FLAME);
    p.fire_hits++;player_burn_tick(&w);assert(loads==2 && w.player_burn.remaining>0);
    enemycombat_reset(&w);player_burn_tick(&w);assert(!w.player_burn.remaining);
    p.fire_hits++;player_burn_tick(&w);p.health=0;player_burn_tick(&w);
    assert(!w.player_burn.remaining);
    p.health=10;p.fire_hits++;player_burn_tick(&w);p.reset_query=1;player_burn_tick(&w);
    assert(!w.player_burn.remaining);
    p.reset_query=0;p.fire_hits++;torso_valid=0;player_burn_tick(&w);
    assert(!w.player_burn.remaining);torso_valid=1;enemycombat_free(&w);
    /* A missing optional effect bank must not alter accepted player damage. */
    reset_loader(0,45);extra_errors[TEST_FLAME]=-1;
    p.fire_hits++;player_burn_tick(&w);
    assert(w.player_burn.remaining>0 && !w.flame_models.h && p.health==10);
    for(int i=0;i<90;i++)player_burn_tick(&w);
    assert(!w.player_burn.remaining && extra_loads[TEST_FLAME]==1);enemycombat_free(&w);
}
int main(void)
{
    test_relevant_types();test_prewarm_and_attacks_share_bank();
    test_failed_prewarm_preserves_lazy_first_use();test_bad_or_missing_bank();
    test_presentation_release_preserves_live_effects();
    test_extra_bank_selection_and_reuse();test_extra_prewarm_retry_and_memoization();
    test_extra_invalid_or_missing_banks();test_fire_pool_and_dependency_failures();
    test_extra_release_preserves_live_owners();
    test_player_burn_lifetime_and_residency();
    puts("enemy graphics prewarm residency/failure/teardown checks passed");return 0;
}
