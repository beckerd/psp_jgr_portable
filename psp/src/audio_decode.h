#ifndef JGR_AUDIO_DECODE_H
#define JGR_AUDIO_DECODE_H
#include "stream_decode.h"
#if defined(__PSP__) || defined(JGR_AUDIO_DECODE_TEST)
void audio_decode_enable(int enabled);
int audio_decode_request(const unsigned char *,unsigned,int,int,int,int [2][2],short *);
void audio_decode_take(StreamDecodeJob *);
void audio_decode_retire(const StreamDecodeJob *);
void audio_decode_profile(unsigned out[3]);
#else
static inline void audio_decode_enable(int enabled){(void)enabled;}
static inline int audio_decode_request(const unsigned char *p,unsigned n,int ch,int c1,int c2,int h[2][2],short *out)
{(void)p;(void)n;(void)ch;(void)c1;(void)c2;(void)h;(void)out;return 0;}
static inline void audio_decode_take(StreamDecodeJob *j){j->blocks=0;}
static inline void audio_decode_retire(const StreamDecodeJob *j){(void)j;}
static inline void audio_decode_profile(unsigned out[3]){out[0]=out[1]=out[2]=0;}
#endif
#endif
