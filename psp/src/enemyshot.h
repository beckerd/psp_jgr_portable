#ifndef JGR_ENEMYSHOT_H
#define JGR_ENEMYSHOT_H
#include <stdint.h>
/* Original shared gunshot task 08920c, states 08971c..0898f8.
   Mode 2 is a delayed visibility query, 0 a deliberate miss, 1 a shot.
   A completed visibility query retains its generation until slot reuse. */
#define ENEMY_SHOT_CAPACITY 64
typedef struct {
    unsigned token;
    int state,mode,kind,active,visible,wall,contact,feedback;
    float origin[3],target[3],pos[3],normal[3],frame;
    int angles[3];
} EnemyShot;
typedef struct {
    EnemyShot shots[ENEMY_SHOT_CAPACITY];
    unsigned serial,created,dropped,hits,probes;
    float impact_frames;
    void *context;
    int (*probe)(void *,const float *,const float *,unsigned,float *,float *);
    unsigned (*random15)(void *);
    int (*atan_bam)(float,float);
    float (*sin_bam)(int);
    int (*hit)(void *,const float *origin,int kind);
    void (*sound)(void *,int reference,const float *position);
} EnemyShots;
unsigned enemyshot_create(EnemyShots *,const float *from,const float *to,int kind,int mode);
int enemyshot_result(const EnemyShots *,unsigned token); /* -1 pending/stale, 0 clear, 1 blocked */
void enemyshot_tick(EnemyShots *,const float *player_anchor);
void enemyshot_reset(EnemyShots *);
#endif
