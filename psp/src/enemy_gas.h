#ifndef JGR_ENEMY_GAS_H
#define JGR_ENEMY_GAS_H
/* 07a2d0..07ad28: Kidoutai grenade task, including the invisible ballistic
   flight and the two original impact animations. Fixed 30 Hz updates. */
#define ENEMY_GAS_CAPACITY 32
/* Love Trap has sixteen anchored launchers; two volleys can overlap at
   90-tick spacing. A fixed pool avoids allocation while combat is busy. */
typedef struct {
    int active,state,age,variant,skin,wall;
    float pos[3],step[3],wall_pos[3],frame,alpha;
} EnemyGas;
typedef struct {
    EnemyGas gas[ENEMY_GAS_CAPACITY];
    unsigned created,impacts,dropped,expired;
    void *context;
    int (*probe)(void *,const float *,const float *,unsigned,float *,float *);
    void (*sound)(void *,int,const float *);
    unsigned (*random15)(void *);
    int (*atan_bam)(float,float);
    float (*sin_bam)(int);
    float frames;
} EnemyGases;
int enemy_gas_create(EnemyGases *,const float *origin,const float *feet,float speed,int heading,int aim,int skin);
void enemy_gas_tick(EnemyGases *);
void enemy_gas_reset(EnemyGases *);
#endif
