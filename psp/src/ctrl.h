/* Recorded controller playback (DC controller task FUN_8c0278c0 modes 2/3/4) and the trial checkpoint script.
   Data: psp/setN_ctrl.bin (tools/jgs_ctrl.py, 'CTL2') = segment tables from mission set +0x44, input streams
   (0x20-byte spawn record + 4-byte records {repeat|0x80 end, buttons, dir, mag}) and the set +0x48 trial entries. */
#ifndef JGR_CTRL_H
#define JGR_CTRL_H
#include <math.h>
#include "game_controls.h"
typedef struct {float remainder;} CtrlFrameClock;
/* A recorded stream and all its consumers must advance together. Repeating
   one input over catch-up physics and then skipping records loses edges. */
static inline unsigned ctrl_frame_steps(CtrlFrameClock *clock,float dt)
{
    if(!isfinite(dt) || dt<=0)return 0;
    clock->remainder+=fminf(dt,.1f);
    unsigned steps=0;
    while(clock->remainder+1e-7f>=1.f/30.f && steps<3) {
        clock->remainder-=1.f/30.f;steps++;
    }
    if(clock->remainder<0)clock->remainder=0;
    return steps;
}
typedef struct { int chr; float speed, x, y, z; unsigned w5; int yaw; unsigned w7; int nrec; const unsigned char *rec; } CtrlStream;
typedef struct { int stream; float dist; unsigned flags; } CtrlSeg;      /* flags: 1 wait for the player within dist, 2 loop, 4 stop */
typedef struct { unsigned type, p1, p2, na, nb; const float *quads; } TrialEntry;   /* quads: (na+nb) x 36 floats {6 pts, 6 normals} */
typedef struct { int ntab; CtrlSeg *segs[16]; int nseg[16]; int nstream; CtrlStream streams[16]; int ntrial; TrialEntry trial[64]; } CtrlSub;   /* one JMSc set */
typedef struct { unsigned char *data; int nsub; CtrlSub sub[8]; } CtrlSet;   /* a set file = chain of sets (+0x50), selected by event 0x15 / loader +0x94 */
typedef struct {
    const CtrlStream *s; int pos, repeat; unsigned char cur[4]; int active, ended;   /* +0x70 record pointer, +0x78 repeat counter, +0x6c current record */
    unsigned prev;                                                                   /* previous buttons byte (+0x74) for "newly pressed" */
} CtrlPlay;
int  ctrl_load(CtrlSet *c, const char *path);
const CtrlSub *ctrl_sub(const CtrlSet *c,int index); /* no fallback to set zero */
void ctrl_free(CtrlSet *c);
void ctrl_play_start(CtrlPlay *p, const CtrlStream *s);   /* FUN_8c0277d8 for modes 2/3/4: first record loaded, repeat = count */
/* one 30 Hz tick: advance the record (FUN_8c0278c0 mode 4); returns 1 while playing, 0 once the 0x80 end record is reached */
int  ctrl_play_tick(CtrlPlay *p);
/* abstract input of the current record: stick x/y (unit circle, magnitude 0..1), buttons byte (1 jump, 4 spray, 8 dash) */
void ctrl_play_input(const CtrlPlay *p, float *ax, float *ay, unsigned *buttons);
#endif
