/* City-only timing lives at the caller's two GFX_STAGE boundaries, not at
   Stage ownership: moving props reuse the city bank outside those scopes. */
static CityProfiler city_profiler;
static StageCityProfile city_record;
static StageProfile city_work_before;
static const Stage *city_owner;
static unsigned city_enabled,city_span,city_draw_completed;
static unsigned city_calibration_steps,city_calibration_us;

static unsigned city_clock(void)
{
#ifdef JGR_TEST_CITY_CLOCK
    return JGR_TEST_CITY_CLOCK();
#elif defined(__PSP__) && JGR_DIAGNOSTICS
    return sceKernelGetSystemTimeLow();
#else
    return 0;
#endif
}
static void city_frame_begin(void)
{
    city_profile_reset(&city_profiler,city_enabled);
    memset(&city_record,0,sizeof city_record);
    city_record.calibration_steps=city_calibration_steps;
    city_record.calibration_us=city_calibration_us;
    city_owner=NULL;city_draw_completed=0;
}
/* Empty transitions measure clock/accounting cost on the running machine.
   Keep the minimum of five trials to limit scheduling contamination. This
   is a calibration, not an exact deduction from measured frame times. The
   on/off renderer benchmark measures the wider instrumentation overhead. */
static void __attribute__((noinline)) city_calibrate(void)
{
    CityProfiler scratch;unsigned best=~0u;
    for(unsigned trial=0;trial<5;trial++) {
        city_profile_reset(&scratch,1);
        city_profile_open(&scratch,0,city_clock());
        for(unsigned i=0;i<1024;i++)
            city_profile_switch(&scratch,(i&1) ? CITY_SETUP:CITY_MATERIAL,city_clock());
        city_profile_close(&scratch,city_clock());
        if(scratch.timing.prepare_us<best)best=scratch.timing.prepare_us;
    }
    city_calibration_steps=1025; /* transitions/end after the initial stamp */
    city_calibration_us=best;
}
void stage_city_profile_mode(int enabled)
{
    city_enabled=JGR_DIAGNOSTICS && !!enabled;
    if(city_enabled && !city_calibration_steps)city_calibrate();
    city_frame_begin();
}
void stage_city_profile_begin(const Stage *s,unsigned span)
{
    if(!city_enabled)return;
    if(!s || span>1 || city_owner){city_profiler.timing.errors++;return;}
    city_owner=s;city_span=span;city_draw_completed=0;
    city_work_before=profile;
    city_profile_open(&city_profiler,span,city_clock());
}
void stage_city_profile_end(void)
{
    if(!city_enabled)return;
    if(!city_owner){city_profiler.timing.errors++;return;}
    city_profile_close(&city_profiler,city_clock());
    /* Unsigned field deltas preserve the existing non-resetting counters.
       memcpy avoids aliasing a struct through an unsigned pointer. */
    for(unsigned i=0;i<sizeof(StageProfile);i+=sizeof(unsigned)) {
        unsigned before,after,total;
        memcpy(&before,(const unsigned char *)&city_work_before+i,sizeof before);
        memcpy(&after,(const unsigned char *)&profile+i,sizeof after);
        memcpy(&total,(const unsigned char *)&city_record.work+i,sizeof total);
        total+=after-before;memcpy((unsigned char *)&city_record.work+i,&total,sizeof total);
    }
    if(city_span==1 && city_draw_completed) {
        city_record.drawn_objects+=city_owner->drawn_obj;
        city_record.drawn_batches+=city_owner->drawn_batches;
        city_record.drawn_triangles+=city_owner->drawn_tris;
        city_record.texture_switches+=city_owner->texswitch;
    }
    city_owner=NULL;
}
void stage_city_profile_get(StageCityProfile *out)
{
    *out=city_record;out->timing=city_profiler.timing;
    if(city_owner)out->timing.errors++; /* caller requested an incomplete span */
}
#if JGR_DIAGNOSTICS
typedef struct {unsigned previous;} CityScope;
static inline CityScope city_scope_begin(const Stage *s,unsigned phase)
{
    CityScope scope={CITY_INACTIVE};
    if(!city_owner || (s && s!=city_owner))return scope;
    scope.previous=city_profiler.phase;
    if(scope.previous!=phase)city_profile_switch(&city_profiler,phase,city_clock());
    return scope;
}
static inline void city_scope_end(CityScope *scope)
{
    if(scope->previous!=CITY_INACTIVE && city_owner && city_profiler.phase!=scope->previous)
        city_profile_switch(&city_profiler,scope->previous,city_clock());
}
/* GCC cleanup closes every early return while preserving the parent's
   bucket. Same-phase nesting is free of clock reads. No per-triangle hooks. */
#define CITY_SCOPE(stage,phase) CityScope city_scope __attribute__((cleanup(city_scope_end)))=city_scope_begin(stage,phase)
#else
#define CITY_SCOPE(stage,phase) ((void)0)
#endif
