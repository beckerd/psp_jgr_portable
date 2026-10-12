#ifndef JGR_STAFF_REPLAY_H
#define JGR_STAFF_REPLAY_H
#include "scene.h"
#include "ctrl.h"
typedef struct {
    Scene source;CtrlStream stream;CtrlPlay input;
    int row,phase,tick,done,error;uint32_t camera;
    int startup,clock,load_request;
} StaffReplay;
int staff_replay_bind(StaffReplay *,const void *,unsigned length);
/* 027ee4 -> sequential 018824/018a0c task-19 lifetimes; 1 publishes spawn. */
int staff_replay_step(StaffReplay *);
/* Original 027ee4 phases 0/1/2 wait for task 99, then phase 3 replays.
   Bind starts with card 0 pending. Requests 1/19 are published in order.
   Busy is supplied by the I/O adapter, never a hardcoded emulator delay. */
int staff_replay_step_loaded(StaffReplay *,int loader_busy);
void staff_replay_camera(const StaffReplay *,const float position[3],SceneCamera *);
/* 0c8948 flag 20 targets the actor's transformed +854 body point, not
   a world-Y offset. Kind 0 leaves the ordinary-camera choice to the caller. */
void staff_replay_camera_target(const StaffReplay *,const float position[3],const float body[3],SceneCamera *);
#endif
