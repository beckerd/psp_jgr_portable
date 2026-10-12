/* Payload-free external storage and reviewed dispatch code. */
#ifndef JGR_SOURCE_DATA_PTR_MOVIE_TABLES_INC
#define JGR_SOURCE_DATA_PTR_MOVIE_TABLES_INC
/* Generated from 1ST_READ.BIN 1e5284 and all 35 DEMO images. */
extern unsigned char MOVIE_SEGMENTS[35] __asm__("jgr_ptr_movie_tables_MOVIE_SEGMENTS");
typedef char jgr_ptr_movie_tables_MOVIE_SEGMENTS_abi[(sizeof(MOVIE_SEGMENTS)==35u)?1:-1];
extern short MOVIE_VOICES[35][7] __asm__("jgr_ptr_movie_tables_MOVIE_VOICES");
typedef char jgr_ptr_movie_tables_MOVIE_VOICES_abi[(sizeof(MOVIE_VOICES)==490u)?1:-1];
extern signed char MOVIE_NEXT[35] __asm__("jgr_ptr_movie_tables_MOVIE_NEXT");
typedef char jgr_ptr_movie_tables_MOVIE_NEXT_abi[(sizeof(MOVIE_NEXT)==35u)?1:-1];
extern unsigned char MOVIE_FLAGS[35] __asm__("jgr_ptr_movie_tables_MOVIE_FLAGS");
typedef char jgr_ptr_movie_tables_MOVIE_FLAGS_abi[(sizeof(MOVIE_FLAGS)==35u)?1:-1];
/* 0de0e0 caption opcode 13, in authored script order. */
extern struct { int movie,segment,tick,voice; } MOVIE_AUDIO_CUES[2] __asm__("jgr_ptr_movie_tables_MOVIE_AUDIO_CUES");
typedef char jgr_ptr_movie_tables_MOVIE_AUDIO_CUES_abi[(sizeof(MOVIE_AUDIO_CUES)==32u)?1:-1];

#endif
