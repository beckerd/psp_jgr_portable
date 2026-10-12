#ifndef JGR_ENEMY_MISSILE_H
#define JGR_ENEMY_MISSILE_H
#include <stdint.h>
#define ENEMY_MISSILE_CAPACITY 16
typedef struct {
    int active,state,variant,ticks,angles[3],turn[2],phase,phase_step,trail_at,trail_toggle;
    int wall,contact;
    float previous[3],pos[3],center[3],origin[3],target[3],direction[3],weave[3];
    float speed,amplitude,texture_v,frame,flash_frame,smoke_frame,alpha;
    float wall_pos[3],trail[30][3];
} EnemyMissile;
typedef struct {
    EnemyMissile missiles[ENEMY_MISSILE_CAPACITY];unsigned created,dropped,hits,probes;
    void *context;
    unsigned (*random15)(void *);
    int (*atan_bam)(float,float);
    float (*sin_bam)(int);
    int (*probe)(void *,const float *,const float *,unsigned,float *,float *);
    int (*contact)(void *,const float *,const float *,int); /* position/previous; original sphere radius 3 */
    void (*explosion)(void *,const float *,int);
} EnemyMissiles;
void enemy_missile_create(EnemyMissiles *,const float *,const float *,int);
void enemy_missile_tick(EnemyMissiles *,const float *player);
void enemy_missile_move(EnemyMissiles *,EnemyMissile *,const float *player);
void enemy_missile_reset(EnemyMissiles *);
#endif
