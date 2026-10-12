#ifndef JGR_ENEMY_SHELL_H
#define JGR_ENEMY_SHELL_H
#define ENEMY_SHELL_CAPACITY 16
/* 0a46e0..0a4a14: invisible Tank shell, followed by explosion variant 3.
   The original shell travels the initial target distance in 14 updates. */
typedef struct {int active,state,ticks,wall,contact;float pos[3],target[3],direction[3],speed,wall_pos[3];} EnemyShell;
typedef struct {
    EnemyShell shells[ENEMY_SHELL_CAPACITY];unsigned created,dropped;
    void *context;
    int (*probe)(void *,const float *,const float *,unsigned,float *,float *);
    int (*contact)(void *,const float *,float);
    void (*explosion)(void *,const float *,int);
} EnemyShells;
void enemy_shell_create(EnemyShells *,const float *,const float *);
void enemy_shell_tick(EnemyShells *);
void enemy_shell_reset(EnemyShells *);
#endif
