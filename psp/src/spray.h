#ifndef JGR_SPRAY_H
#define JGR_SPRAY_H
#include <stdint.h>
/* Fields of player +278/+374..388. Previous input is owned by 03b0fc,
   not by the cue evaluator. Keep that distinction for straight-stick edges. */
typedef struct {
    int skill, pattern, cue, bonus, previous_angle, phase;
    float previous_magnitude;
} SprayInput;
typedef struct { unsigned animate, sound; } SprayEvents;
enum { SPRAY_WAIT = 0, SPRAY_ACTIVE = 1, SPRAY_SUCCESS = 2, SPRAY_MISS = 3 };
int spray_delta(int from, int to);
int spray_cue(const SprayInput *s);
int spray_evaluate(SprayInput *s, int angle, float magnitude, SprayEvents *events);
/* Live PSP samples may skip the intermediate direction below 80% throw. */
int spray_evaluate_physical(SprayInput *s,int angle,float magnitude,SprayEvents *events);
/* Shared +9/288 completion kernel of 03b7bc/03b828/03b8a8.
   Caller owns the task's deletion and trial/ending callbacks. */
int spray_reveal_tick(uint32_t *stroke, uint32_t *progress, uint32_t *complete);
/* 03bd08: animation-driven large-tag reveal, with monotone GF high water. */
void spray_large_reveal(float *clock,uint32_t *reveal,unsigned cues,float animation_step);
int spray_character_skill(int character);
#endif
