#ifndef SKATEFX_H
#define SKATEFX_H
typedef struct { int bone; float pos[3]; } SkateAnchor;
typedef struct { int active; float frame, matrix[16]; } SkateBurst;
typedef struct {int kind;float scale,head[3];} SprayBubble;
/* 0447f4: six-tick grow/shrink, including shrink before changing art. */
static inline void spray_bubble_tick(SprayBubble *b,int ready,int kind,int spraying)
{
    if(spraying){b->scale=0;return;}
    if(!ready || kind!=b->kind) {
        b->scale-=0x1.555556p-3f;
        if(b->scale<=0){b->scale=0;if(ready)b->kind=kind;}
    } else {b->scale+=0x1.555556p-3f;if(b->scale>1)b->scale=1;}
}
typedef struct {
    int active,enabled,countdown;
    float current[3],anchor[3],previous_body[3],frame,matrix[16];
} SkateStreak;
typedef struct { int active;float frame,alpha,matrix[16]; } SkateTail;
/* Completed original effect inputs. Attachment order: 0..7, 19..22.
   Recorded title playback supplies these without running player physics. */
typedef struct {
    int flags,motion,mirrored,grind_pose,dash,boost,obstruction;
    float frame,speed,external[3],points[12][3];
    int travel_angles[3];
} SkateInput;
typedef struct {int count,enabled;float uv_shift,points[10][2][3];} SkateRibbon;
typedef struct {
    int initialized;
    SkateBurst grind[2],trick[2];
    SkateBurst spray; /* 047084: original ten-frame paint mist */
    SkateBurst impact; /* 043904: 15-frame hit burst at collision anchor 18 */
    SprayBubble bubble;
    SkateStreak wall[2];
    /* Original tasks share the bounded 0x127..0x1b9 effect pool. This
       per-skater PSP pool covers a new two-foot tail every tick for 15 ticks. */
    SkateTail tails[32];
    SkateBurst dash_sparks[24]; /* two emitting ticks per foot / 28-tick stride; 12-tick lifetime */
    SkateBurst water_sparks[16]; /* 046a24/046be4: one 15-frame shallow splash per tick */
    unsigned water_random;
    int dash_age;
    float dash_points[2][3][2][3],previous_foot[2][3];
    SkateRibbon trail[2],trail_tails[20]; /* original ten-sample ribbons, bounded retirement */
} SkateEffects;
#endif
