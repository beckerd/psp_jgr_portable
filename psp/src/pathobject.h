#ifndef JGR_PATHOBJECT_H
#define JGR_PATHOBJECT_H
#include <stdint.h>

/* Original task 8a8 / 06b024, not mission-set parked objects (8a9).
   event names the opcode-9 record; DC task +70 points at event+4. */
typedef struct {
    uint32_t event, handle, type, track, frames;
    int state, mode, special, pulse, armed, sound;
    float frame, rate, radius2;
    int angle;
    float pos[3]; int32_t rotation[3];
    float scale[2][3];
} PathObject;
typedef struct {
    float position[3], player[3]; int32_t rotation[3];
    int sound_blocked, sound_finished, sound_created;
} PathObjectInput;
enum {
    PATH_INTEGRATE=1, PATH_DELETE=2, PATH_SOUND_CREATE=4,
    PATH_SOUND_DELETE=8, PATH_SOUND_POSITION=16
};
void pathobject_mode(PathObject *,int mode); /* 06b318 */
void pathobject_resume(PathObject *);        /* 06b304: paused only */
void pathobject_init(PathObject *,uint32_t frames,uint32_t start,uint32_t speed,int mode);
/* One source task tick. Sampling and external task allocation are caller
   inputs; outputs preserve source call order (including deferred deletion).
   No dt multiplier: the original task executes at 30 Hz. */
unsigned pathobject_tick(PathObject *,const PathObjectInput *);
#endif
