#ifndef JGR_MOVIE_VOICE_H
#define JGR_MOVIE_VOICE_H
#include <stdint.h>
typedef struct {
    const unsigned char *blocks;unsigned count,samples,position;
    float history[2];int16_t decoded[32];int active,ready,error;
} MovieVoice;
/* Audited ending narration: mono, 44100 Hz, 500 Hz filter, ADX v3.
   Borrows compressed data; no allocation or I/O in the sample callback. */
int movie_voice_bind(MovieVoice *,const void *,unsigned);
int movie_voice_format(const void *header,unsigned header_length,unsigned file_length,unsigned *start,unsigned *samples);
int16_t movie_voice_next(MovieVoice *);
#endif
