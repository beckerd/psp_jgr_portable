#ifndef JGR_MOVIE_H
#define JGR_MOVIE_H
#include "scene.h"
/* 051130 / 09d634 movie task pair. All pointers remain source addresses. */
typedef struct {
    Scene source;
    int movie,phase,segment,count,model0,model_count,error,done,audio_request;
    int next_audio,fade_out,audio_start,skipped,music_request,audio_stop;
    float frame,caption_frame,fade,letterbox;
    uint32_t descriptor;
} Movie;
int movie_bind(Movie *,const void *,unsigned length,int movie);
int movie_bind_pages(Movie *,unsigned char *const *,unsigned length,int movie);
/* Original 0ee694 / 1e5284 mapping, -1 means an authored silent segment. */
int movie_voice_reference(int movie,int segment);
/* Additional timed bank-31 clips from caption opcode 13, for preflight. */
int movie_script_voice_reference(int movie,int cue);
/* A positive audio_ready acknowledges the requested original stream. */
void movie_step(Movie *,int audio_ready);
void movie_skip(Movie *); /* original START gate, 051130 phase 14 */
float movie_actor_frame(const Movie *,uint32_t actor);
float movie_segment_end(const Movie *);
void movie_camera(const Movie *,SceneCamera *);
void movie_caption(const Movie *,char text[256],int *x,int *y);
uint32_t movie_actor(const Movie *,unsigned index);
#endif
