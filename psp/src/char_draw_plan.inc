/* Immutable traversal only: no poses, textures, GE packet addresses or
   frame-owned pointers. Entries survive packet eviction and are invalidated
   with their bank. The ring and directory replace part of the GU arena. */
#ifndef DRAW_PLAN_ENTRIES
#define DRAW_PLAN_ENTRIES 2048u
#endif
#define DRAW_PLAN_SLOTS 64u
typedef struct { unsigned short batch,bone; unsigned group_end; } DrawPlanEntry;
typedef struct {
    const Chars *bank;
    unsigned ci,kind,first,count,group_limit;
    unsigned short pass[4];
} DrawPlan;
static DrawPlan draw_plans[DRAW_PLAN_SLOTS];
static DrawPlanEntry draw_plan_entries[DRAW_PLAN_ENTRIES];
static unsigned draw_plan_cursor,draw_plan_victim;
static CharPlanProfile plan_profile;
#ifdef __PSP__
typedef char draw_plan_budget[(sizeof draw_plans+sizeof draw_plan_entries<=CHAR_DRAW_PLAN_BYTES) ? 1:-1];
#endif
#ifdef JGR_CHAR_PLAN_REFERENCE
static int draw_plans_enabled;
#else
static int draw_plans_enabled=1;
#endif
void chars_draw_plan_mode(int enabled){draw_plans_enabled=!!enabled;}
void chars_plan_totals(CharPlanProfile *out){*out=plan_profile;}
void chars_plan_profile(CharPlanProfile *out){*out=plan_profile;memset(&plan_profile,0,sizeof plan_profile);}
static unsigned ge_group_limit(const Chars *,const JgcChar *);
/* 0: PLAY; 1: EVENT opaque bodies/additive last; 2: mode-2 materials;
   3: fading mode-2, where ALL strips keep hierarchy order in pass 1. */
static unsigned draw_plan_pass(const JgcBatch *b,unsigned kind)
{
    if(kind>=2)return kind==3 || (b->flags&1) ? 1:0;
    if(kind==1 && (b->flags&17)==17 && ((b->flags>>8)&7)==1)return 2;
    return b->flags&8 ? 0:1;
}
static void forget_draw_plans(const Chars *c)
{for(unsigned i=0;i<DRAW_PLAN_SLOTS;i++)if(draw_plans[i].bank==c)draw_plans[i].bank=NULL;}
static DrawPlan *draw_plan_get(const Chars *c,unsigned ci,unsigned kind)
{
    if(!draw_plans_enabled)return NULL;
    size_t hash=((size_t)c>>4)^((size_t)c>>12)^ci*13u^kind*7u;
    DrawPlan *set=draw_plans+(hash&15u)*4,*slot=NULL;
    for(unsigned i=0;i<4;i++) {
        if(set[i].bank==c && set[i].ci==ci && set[i].kind==kind){plan_profile.hits++;return set+i;}
        if(!set[i].bank)slot=set+i;
    }
    unsigned started=render_clock();
    const JgcChar *ch=c->chars+ci;
    unsigned count=0,counts[3]={0};
    if(c->h->n_batches>65535 || ch->nbones>65535)return NULL;
    for(unsigned i=0;i<ch->nbones;i++) {
        const JgcBone *bone=c->bones+ch->bone0+i;
        if(bone->batch0>c->h->n_batches || bone->nbatch>c->h->n_batches-bone->batch0)return NULL;
        count+=bone->nbatch;
        if(count>DRAW_PLAN_ENTRIES)return NULL;
        for(unsigned j=bone->batch0;j<bone->batch0+bone->nbatch;j++)counts[draw_plan_pass(c->batch+j,kind)]++;
    }
    unsigned first=draw_plan_cursor;
    if(first+count>DRAW_PLAN_ENTRIES)first=0;
    for(unsigned i=0;i<DRAW_PLAN_SLOTS;i++) {
        DrawPlan *p=draw_plans+i;
        if(p->bank && p->first<first+count && first<p->first+p->count)p->bank=NULL;
    }
    if(!slot)slot=set+(draw_plan_victim++&3u);
    *slot=(DrawPlan){.bank=c,.ci=ci,.kind=kind,.first=first,.count=count,
        .group_limit=kind<2 ? ge_group_limit(c,ch):0,
        .pass={0,counts[0],counts[0]+counts[1],count}};
    unsigned at[3]={0,counts[0],counts[0]+counts[1]};
    for(unsigned i=0;i<ch->nbones;i++) {
        const JgcBone *bone=c->bones+ch->bone0+i;
        for(unsigned j=bone->batch0;j<bone->batch0+bone->nbatch;j++) {
            unsigned pass=draw_plan_pass(c->batch+j,kind);
            draw_plan_entries[first+at[pass]++]=(DrawPlanEntry){j,i,0};
        }
    }
    draw_plan_cursor=first+count;plan_profile.built++;plan_profile.entries+=count;
    plan_profile.build_us+=render_clock()-started;return slot;
}
typedef struct {const DrawPlan *plan;unsigned at,end,bi,bt,kind,pass;} DrawPlanCursor;
static DrawPlanCursor draw_plan_begin(const DrawPlan *p,unsigned kind,unsigned pass)
{
    DrawPlanCursor it={.plan=p,.kind=kind,.pass=pass};
    if(p){it.at=p->first+p->pass[pass];it.end=p->first+p->pass[pass+1];}
    return it;
}
static int draw_plan_next(DrawPlanCursor *it,const Chars *c,const JgcChar *ch,
                          unsigned *bi,unsigned *bt,DrawPlanEntry **entry)
{
    *entry=NULL;
    if(it->plan) {
        if(it->at==it->end)return 0;
        *entry=draw_plan_entries+it->at++;*bi=(*entry)->bone;*bt=(*entry)->batch;
        plan_profile.visited++;return 1;
    }
    while(it->bi<ch->nbones) {
        const JgcBone *bone=c->bones+ch->bone0+it->bi;
        if(it->bt<bone->nbatch) {
            unsigned batch=bone->batch0+it->bt++;
            if(draw_plan_pass(c->batch+batch,it->kind)!=it->pass)continue;
            *bi=it->bi;*bt=batch;return 1;
        }
        it->bi++;it->bt=0;
    }
    return 0;
}
