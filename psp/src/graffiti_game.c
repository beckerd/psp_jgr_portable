/* Source cue/reveal kernels, score-task ownership and the city/final-target
   spray actor/camera binding. */
#include "graffiti.h"
#if JGR_PORTABLE_ASSETS
#include "source_data_spraytab.inc"
#else
#include "generated/source_data/spraytab.h"
#endif
#include <math.h>
#include <stdio.h>
#include <string.h>
#define SPRAY_TICK (1.f/30.f)
#define SPRAY_BAM (65536.f/(2.f*3.14159265f))
static float hud_sine_fallback(int angle){return sinf((angle&65535)*(6.2831853071795864769f/65536.f));}
static void hud_begin(Graffiti *g)
{
    const int *row=PATROWS[g->input.skill*7+g->input.pattern];
    g->hud=(SprayHud){.count=row[3],.cue=g->input.cue};
    for(int j=0;j<g->hud.count;j++) {
        g->hud_cues[j]=row[j];
        /* All 20 normal/miss headers in PLAYER_EFFECT have 30 frames. */
        g->hud.rows[j]=(SprayHudRow){.delay=6*j,.frames=30};
    }
}
static void hud_tick(Graffiti *g)
{
    if(!g->hud.count)return;
    for(int j=0;j<g->hud.count;j++)g->hud.rows[j].child_alive=g->score_hud.flight[j].alive;
    spray_hud_tick(&g->hud,1,g->mg_wait && !g->hud.mode ? g->hud.count:g->input.cue,
                   g->hud_sine ? g->hud_sine:hud_sine_fallback);
}
static void actor(Graffiti *g,Spot *s,int action,int cue)
{
    /* Both camera branches need the original GF basis. SPT6 city records
       and final attached targets supply it. */
    if(g->actor && s->camera_valid) {
        g->actor(g->actor_context,g,s,action,cue);g->actor_ticks++;
    }
}
static void actor_exit_tick(Graffiti *g,Spot *s,int cue)
{
    actor(g,s,SPRAY_ACT_EXIT,cue);
    /* 03bfa8/03c0f0 returns to 0198f8's common actor epilogue. Keep
       this separate from EXIT: reset/teardown is not a simulation tick. */
    actor(g,s,SPRAY_ACT_FINISH,0);
}

int graffiti_score_retire(Graffiti *g)
{
    if(!g)return 0;
    int score=g->score+spray_score_flush(&g->score_hud);
    g->score=g->mg_award=0;
    return score;
}
/* 041ec0 retires live score children into 0418a8's tally; 041bbc arms
   that owner. Neither operation awards the same points directly. */
static void score_exit(Graffiti *g)
{
    spray_score_finish_flights(&g->score_hud);
    spray_score_arm(&g->score_hud);
    g->mg_award=0;
}
void graffiti_reset(Graffiti *g)
{
    if(g->mg_active && g->near>=0 && g->near<g->n)actor(g,g->spots+g->near,SPRAY_ACT_EXIT,0);
    (void)graffiti_score_retire(g);
    g->near=-1;g->tagged=0;g->progress=0;g->presses=0;
    g->mg_active=g->mg_wait=g->mg_award=g->mg_setup=0;g->tick_acc=0;g->pending=g->events=0;
    g->mg_flash=0;
    g->mg_reveal=0;g->reveal_clock=g->reveal_step=0;
    g->hud_player_state=0; /* incoming actor publishes its own state next tick */
    g->hud=(SprayHud){.deleted=1};
    g->finish=(FinishHud){0};
    memset(&g->input,0,sizeof g->input);
    for (int i=0;i<g->n;i++) {
        Spot *s=&g->spots[i];s->tagged=0;s->stroke=s->reveal=s->complete=0;s->started=s->revealing=0;s->arrow_frame=0;
    }
}
static void pick_cue(Graffiti *g)
{
    g->mg_cue=spray_cue(&g->input);
    const int *c=CUES[g->mg_cue];int mid=spray_delta(0,(c[0]+c[1])/2);
    const char *dir=mid>-8192 && mid<=8192 ? "UP" : mid>8192 && mid<=24576 ? "RIGHT" : mid>24576 || mid<=-24576 ? "DOWN" : "LEFT";
    if (!c[6]) snprintf(g->mg_text,sizeof g->mg_text,"STICK %s",dir);
    else snprintf(g->mg_text,sizeof g->mg_text,"FROM %s ROTATE %s",dir,c[6]==1 ? "CLOCKWISE" : "COUNTER-CW");
}
static void finish_spot(Graffiti *g,Spot *s,unsigned event)
{
    if (!s->tagged) { s->tagged=1;g->tagged++;g->events|=event; }
}
unsigned graffiti_interrupt(Graffiti *g)
{
    if(!g || !g->mg_active)return 0;
    unsigned completed=0;
    if(g->near>=0 && g->near<g->n) {
        Spot *s=g->spots+g->near;
        /* 03c05c sets GF+14c and calls 08e760 only after the last stroke.
           Destroying score children runs 041ec0; cans/progress stay spent. */
        if(s->stroke>=s->nstrokes && !s->tagged) {
            s->complete=1;finish_spot(g,s,GRAFFITI_TAG);completed=GRAFFITI_TAG;
        }
        actor(g,s,SPRAY_ACT_INTERRUPT,0);
    }
    score_exit(g);
    g->mg_active=g->mg_wait=g->mg_award=g->mg_setup=0;
    g->mg_flash=0;g->near=-1;g->pending=0;g->tick_acc=0;
    g->hud.deleted=1;
    return completed;
}
static int find_spot(Graffiti *g,float x,float y,float z)
{
    float best=900.f;int result=-1;
    for (int i=0;i<g->n;i++) {
        Spot *s=g->spots+i;
        if(s->tagged || s->complete || s->revealing || !s->active)continue;
        if(s->hasvol) {
            int inside=1;
            for(int k=0;k<6 && inside;k++) {
                if(s->attached || s->source) {
                    /* Same original FSUB/FIPR order as gfo_contains. */
                    float dx=s->pts[3*k]-x,dy=s->pts[3*k+1]-y,dz=s->pts[3*k+2]-z;
                    double d=(double)dx*s->nrm[3*k];d+=(double)dy*s->nrm[3*k+1];d+=(double)dz*s->nrm[3*k+2];
                    if((float)d<0)inside=0;
                } else {
                    float dx=x-s->pts[3*k],dy=y+7.6f-s->pts[3*k+1],dz=z-s->pts[3*k+2];
                    if(dx*s->nrm[3*k]+dy*s->nrm[3*k+1]+dz*s->nrm[3*k+2]>0)inside=0;
                }
            }
            if(inside) {
                /* 0c48dc overwrites the query result for every matching
                   registered volume, including static city graffiti. */
                result=i;
            }
        } else {
            float dx=x-s->x,dy=y-s->y,dz=z-s->z,d=dx*dx+dz*dz+dy*dy*.25f;
            if(d<best){best=d;result=i;}
        }
    }
    return result;
}
int graffiti_rival_spray(Graffiti *g,float x,float y,float z,int *cans,int pressed)
{
    if(!g || !pressed || !cans || *cans<1)return -1;
    int index=find_spot(g,x,y,z);if(index<0)return -1;
    Spot *s=g->spots+index;
    /* 01abb8 rejects an already claimed small tag; large anchors enter
       another handler. A rival never consumes the player's cans or score. */
    if(!s->source_data || s->nstrokes || s->started)return -1;
    s->started=1;s->stroke=s->reveal=0;s->revealing=2;--*cans;
    /* The source's newly created 03b8a8 child runs later in this tick.
       The press frame is reveal tick one; completion is 31 ticks later. */
    spray_reveal_tick(&s->stroke,&s->reveal,&s->complete);
    return index;
}
static void spray_sound(Graffiti *g,Spot *s,int cue)
{if(g->actor)g->actor(g->actor_context,g,s,SPRAY_ACT_SOUND,cue);}
static int graffiti_action_tick(Graffiti *g,float x,float y,float z,float yaw,unsigned edges,int angle,float magnitude,int character,int physical)
{
    finish_hud_tick(&g->finish);
    /* Independent source tasks keep revealing after the skater moves away. */
    for(int i=0;i<g->n;i++) {
        Spot *s=g->spots+i;
        /* 03e6f8 advances even while the target is being sprayed; only
           GF+14c (complete) stops it. Draw visibility is separate. */
        if(s->active && !s->complete && !s->tagged) {
            s->arrow_frame+=1.f;if(s->arrow_frame>=120.f)s->arrow_frame-=120.f;
        }
        if(s->revealing && spray_reveal_tick(&s->stroke,&s->reveal,&s->complete)) {
            if(s->revealing==2)g->events|=GRAFFITI_RIVAL_FINISH; /* 03b8a8 -> 0186b4(1) */
            else finish_spot(g,s,GRAFFITI_FINISH); /* 03b828 -> 08e7f0 */
            s->revealing=0;
        }
    }
    if (!g->mg_active)g->near=find_spot(g,x,y,z);
    if(g->near<0 || g->near>=g->n){if(g->mg_active){score_exit(g);g->mg_active=0;}return 0;}
    Spot *s=g->spots+g->near;
    if (!s->active || s->tagged) {if(g->mg_active){actor_exit_tick(g,s,0);score_exit(g);}g->mg_active=0;g->near=-1;return 0;}
    if (!g->mg_active) {
        if (!(edges&1))return 0;
        if(g->cancount<=0){spray_sound(g,s,SPRAY_SOUND_EMPTY);return 0;}
        /* Only 06bdf0 consumes 01abb8's large-tag return value. Air,
           wall and grind handlers may spray small tags but cannot enter
           the blocking large-tag handler (01baa8/01d7ec/020a38). */
        if(s->nstrokes && g->block_large)return 0;
        /* Never invent a large-tag sequence when an old export lacks +f0. */
        if(!s->source_data && s->size>=2) {
            snprintf(g->mg_text,sizeof g->mg_text,"UPDATE ORIGINAL SPOT DATA");return 0;
        }
        s->started=1;s->yaw=yaw;
        if (!s->nstrokes) {
            /* 01abb8 nonzero type / zero positions: ONE can, 500 points,
               08e7a8 now, then a separate 32-tick 03b828 completion. */
            /* Small tags use a partial upper-body motion, not the blocking
               camera/actor task. No camera volume or movement lock needed. */
            if(g->actor)g->actor(g->actor_context,g,s,SPRAY_ACT_SMALL,0);
            spray_sound(g,s,SPRAY_SOUND_SMALL); /* 01abb8 -> 05cd8c + 05cdc8 */
            s->stroke=s->reveal=0;s->revealing=1;g->cancount--;g->score+=500;
            g->events|=GRAFFITI_DEFER;g->near=-1;return 0;
        }
        g->mg_active=1;g->mg_stroke=s->stroke;g->mg_nstrokes=s->nstrokes;
        g->input=(SprayInput){.skill=spray_character_skill(character),.previous_angle=angle,.previous_magnitude=magnitude};
        g->mg_wait=g->mg_award=0;g->progress=(float)s->stroke/s->nstrokes;pick_cue(g);
        g->mg_setup=1;actor(g,s,SPRAY_ACT_ENTER,0);
        if(!g->mg_active)score_exit(g);
        return g->mg_active; /* 03aedc captures the previous input, does not evaluate it. */
    }
    g->actor_damage=0;actor(g,s,SPRAY_ACT_PRE,0);
    if(!g->mg_active){score_exit(g);return 0;}
    if(g->actor_damage) {
        /* 03b12c: query, retire the graffiti children, then dispatch
           the accepted collision and its return-to-control handler. */
        graffiti_interrupt(g);actor(g,s,SPRAY_ACT_DAMAGE,0);g->actor_damage=0;return 0;
    }
    if(!g->mg_setup || g->mg_wait)actor(g,s,SPRAY_ACT_IDLE,0);
    if(g->mg_wait) {
        if(!--g->mg_wait)g->mg_award=0; /* compatibility mirror, never a second payout */
    } else if(s->stroke>=s->nstrokes) {
        s->complete=1;finish_spot(g,s,GRAFFITI_TAG); /* 03bfa8 -> 08e760, no deferred checkpoint at entry */
        actor_exit_tick(g,s,0);score_exit(g);g->mg_active=0;g->near=-1;return 0;
    } else if(g->cancount<=0) {
        spray_sound(g,s,SPRAY_SOUND_EMPTY); /* 03b0fc setup rejection */
        actor_exit_tick(g,s,0);score_exit(g);g->mg_active=0;return 0;
    } else {
        if(g->mg_setup){actor(g,s,SPRAY_ACT_SETUP,0);g->mg_setup=0;g->mg_reveal=0;g->reveal_clock=0;if(!g->mg_active){score_exit(g);return 0;}hud_begin(g);}
        if(g->mg_reveal)spray_large_reveal(&g->reveal_clock,&s->reveal,PATROWS[g->input.skill*7+g->input.pattern][3],g->reveal_step);
        int cue=spray_cue(&g->input);
        SprayEvents ev;int result=physical ? spray_evaluate_physical(&g->input,angle,magnitude,&ev):spray_evaluate(&g->input,angle,magnitude,&ev);
        if(ev.animate)actor(g,s,SPRAY_ACT_CUE,cue);
        /* 03c0fc runs AFTER cue animation selection, BEFORE advancing
           the stroke/pattern. Its RNG shares the player's motion stream. */
        if(ev.sound)spray_sound(g,s,result==SPRAY_SUCCESS && s->stroke+1>=s->nstrokes ? SPRAY_SOUND_FINAL:SPRAY_SOUND_CUE);
        if(result==SPRAY_MISS) {
            spray_sound(g,s,SPRAY_SOUND_SYSTEM+10); /* 03be54 -> 05cedc, BEFORE miss-pose RNG */
            /* 040ef0 changes the existing task's models/motions, keeping
               its current per-row scale. It does not create full-size Xs. */
            g->hud.mode=1;g->hud.age=0;
            for(int j=0;j<g->hud.count;j++)g->hud.rows[j].frame=0;
            actor(g,s,SPRAY_ACT_MISS,0);g->mg_setup=1;
            g->cancount--;g->input.pattern=0;g->input.cue=g->input.phase=g->input.bonus=0;
            spray_score_arm(&g->score_hud);
            g->mg_wait=7;g->mg_flash=7*SPRAY_TICK;pick_cue(g); /* 041310 miss +158: 0..6 then delete */
        } else if(result==SPRAY_SUCCESS) {
            unsigned previous_stroke=s->stroke; /* 03beb0 saves GF+144 before increment */
            const int *row=PATROWS[g->input.skill*7+g->input.pattern];
            int amounts[3]={0,0,0};
            g->mg_award=g->input.bonus;
            for(int k=0;k<row[3];k++)g->mg_award+=(amounts[k]=CUES[row[k]][5]);
            spray_score_begin(&g->score_hud,amounts,(unsigned)row[3],g->input.bonus);
            g->cancount--;s->stroke++;s->reveal=0;g->mg_stroke=s->stroke;
            g->progress=(float)s->stroke/s->nstrokes;
            if(s->stroke>=s->nstrokes)finish_hud_start(&g->finish); /* 03beb0 -> 0e8e6c(0,60) */
            /* 05cef4 / 1948d0: fixed stroke tones, distinct from the
               preceding random PLAY gesture voice. 05cee8 uses SYS12. */
            
#if JGR_PORTABLE_ASSETS
#include "source_data_mixed_graffiti_game_c__tone_0.inc"
#else

#include "generated/source_data/graffiti_game_c__tone_0.inc"

#endif

            spray_sound(g,s,SPRAY_SOUND_SYSTEM+(s->stroke>=s->nstrokes ? 12:
                        tone[previous_stroke<8?previous_stroke:7]));
            g->input.pattern++;g->input.cue=g->input.phase=g->input.bonus=0;
            /* 03b0fc waits for the cue HUD task, then clears phase 3 to 0
               without setting up the next stroke in that callback. Preserve
               that extra poll: source setup/exit occurs success +22 ticks.
               The independent score children still live for 20 ticks. */
            g->mg_wait=21;
            g->mg_setup=1;
            if(s->stroke<s->nstrokes)pick_cue(g);
        } else {if(result==SPRAY_ACTIVE)g->mg_reveal=1;if(ev.sound)pick_cue(g);}
    }
    g->input.previous_angle=angle;g->input.previous_magnitude=magnitude;
    /* 03b6d6..03b7a8 checks R/jump AFTER this tick's cue evaluation and
       previous-input publication. A simultaneous final gesture/cancel may
       complete the tag; early cancellation silently discarded that input.
       R wins over jump when both edges are present. */
    if(edges&3) {
        score_exit(g);
        if(s->stroke>=s->nstrokes){s->complete=1;finish_spot(g,s,GRAFFITI_TAG);}
        actor_exit_tick(g,s,!(edges&1) && !!(edges&2));
        g->mg_active=0;g->mg_wait=g->mg_award=0;return 0;
    }
    actor(g,s,SPRAY_ACT_TICK,0);
    hud_tick(g);
    return 1;
}
static int graffiti_tick(Graffiti *g,float x,float y,float z,float yaw,unsigned edges,int angle,float magnitude,int character,int physical)
{
    /* The persistent tally is visited before the spray actor. Its newly
       created score children are visited later in the same source tick. */
    /* Loaders and lightweight callers may begin with a zeroed Graffiti. */
    if(g->score_hud.next_y==0.f && !g->score_hud.deleted)spray_score_init(&g->score_hud);
    g->score+=spray_score_tick(&g->score_hud,g->hud_player_state);
    int hold=graffiti_action_tick(g,x,y,z,yaw,edges,angle,magnitude,character,physical);
    spray_score_flights_tick(&g->score_hud,g->hud_player_state);
    return hold;
}
static int graffiti_update_samples(Graffiti *g,float x,float y,float z,float yaw,int pressed,int cancel,int angle,float magnitude,int character,float dt,int physical)
{
    g->events=0;g->actor_ticks=0;
    if(!isfinite(dt) || dt<=0)return g->mg_active;
    if(dt>.25f)dt=.25f;
    if(!isfinite(magnitude))magnitude=0;
    /* Pickups are separate 04dcdc/body-collision tasks, not an XZ-radius
       side effect of entering the graffiti input dispatcher. */
    g->pending|=(pressed?1:0)|(cancel?2:0);g->tick_acc+=dt;
    int hold=g->mg_active;
    while(g->tick_acc+1e-7f>=SPRAY_TICK) {
        unsigned edges=g->pending;g->pending=0;
        hold=graffiti_tick(g,x,y,z,yaw,edges,angle,magnitude,character,physical);
        g->tick_acc-=SPRAY_TICK;if(g->tick_acc<0)g->tick_acc=0;
    }
    g->mg_flash=fmaxf(0,g->mg_flash-dt);
    return hold;
}
int graffiti_update_input(Graffiti *g,float x,float y,float z,float yaw,int pressed,int cancel,int angle,float magnitude,int character,float dt)
{return graffiti_update_samples(g,x,y,z,yaw,pressed,cancel,angle,magnitude,character,dt,0);}
int graffiti_update(Graffiti *g,float x,float y,float z,float yaw,int pressed,int cancel,float ax,float ay,int character,float dt)
{
    if(!isfinite(ax) || !isfinite(ay))ax=ay=0;
    int angle=ax==0.f && ay==0.f ? 16384:(int)(atan2f(ax,-ay)*SPRAY_BAM)&0xff00; /* 103df0(0,0) */
    float magnitude=(int)(fminf(1,sqrtf(ax*ax+ay*ay))*255.f)*(1.f/255.f);
    return graffiti_update_samples(g,x,y,z,yaw,pressed,cancel,angle,magnitude,character,dt,1);
}
