#ifndef JGR_ENEMY_WAVE_H
#define JGR_ENEMY_WAVE_H
#define ENEMY_WAVE_CAPACITY 8
/* 0fdfc0..0fe700: Denki's ground-following discharge. Floor queries
   published at the end of a tick are consumed on the following tick. */
typedef struct {int active,state,frame,age,yaw,pitch,floor,contact,floor_yaw;float pos[3],floor_pos[3],normal[3],hit_pos[3];} EnemyWave;
typedef struct {
    EnemyWave waves[ENEMY_WAVE_CAPACITY];unsigned created,dropped;
    void *context;int hit_frames;
    float (*sin_bam)(int);int (*atan_bam)(float,float);
    int (*probe)(void *,const float *,const float *,unsigned,float *,float *);
    int (*route)(void *,const float *,const float *,int,float *,float *,int *);
    int (*contact)(void *,const float *,const float *);
    void (*hit)(void *,const float *);
    void (*hit_position)(void *,float *);
} EnemyWaves;
void enemy_wave_create(EnemyWaves *,const float *,int);
void enemy_wave_tick(EnemyWaves *);
void enemy_wave_reset(EnemyWaves *);
#endif
