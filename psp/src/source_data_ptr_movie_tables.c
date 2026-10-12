/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>



#include "source_data_pointers.h"
unsigned char MOVIE_SEGMENTS[35] __asm__("jgr_ptr_movie_tables_MOVIE_SEGMENTS") __attribute__((aligned(16)));
short MOVIE_VOICES[35][7] __asm__("jgr_ptr_movie_tables_MOVIE_VOICES") __attribute__((aligned(16)));
signed char MOVIE_NEXT[35] __asm__("jgr_ptr_movie_tables_MOVIE_NEXT") __attribute__((aligned(16)));
unsigned char MOVIE_FLAGS[35] __asm__("jgr_ptr_movie_tables_MOVIE_FLAGS") __attribute__((aligned(16)));
struct { int movie,segment,tick,voice; } MOVIE_AUDIO_CUES[2] __asm__("jgr_ptr_movie_tables_MOVIE_AUDIO_CUES") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {3841686589,128,35,16,MOVIE_SEGMENTS},
    {1244353950,176,490,16,MOVIE_VOICES},
    {163141709,672,35,16,MOVIE_NEXT},
    {392476304,720,35,16,MOVIE_FLAGS},
    {3454895671,768,32,16,MOVIE_AUDIO_CUES},
};
int source_data_ptr_movie_tables_load(void){return source_data_pointers_load_file("source_data_ptr_movie_tables.jsd",sections,5u,800u,0xbe2ce1a1u,0u);}
#endif
