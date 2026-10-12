#ifndef GAMEPLAY_AUDIO_PRELOAD_H
#define GAMEPLAY_AUDIO_PRELOAD_H
#include <stdint.h>

typedef struct {
    int stage,character,world_error,player_error,enemy_error;
    uint64_t kinds;
} GameplayAudioPreload;

/* Main-thread lifecycle. Begin only while gameplay sound owners are retired
   and the scene's own banks are fully loaded. The worker uses the existing
   audio loaders; it never touches actors, rendering, or the runtime log. */
int gameplay_audio_preload_begin(int stage,int character,uint64_t kinds);
/* Join before gameplay, bank retirement/replacement, resume, or shutdown.
   Returns zero if no job exists. NULL discards the result, not its banks.
   Failed/unfinished banks retain the ordinary synchronous load fallback. */
int gameplay_audio_preload_finish(GameplayAudioPreload *out);
#endif
