#ifndef STREAM_H
#define STREAM_H
/* Streams and decodes music.adx (CRI ADX, 44.1 kHz stereo, encoding 3) on a reader thread, honouring the loop points. */
void stream_init(const char *file);
void stream_shutdown(void);
enum { STREAM_CHAIN=1, STREAM_FILE=2 }; /* FILE respects authored ADX loops; CHAIN follows nodes. */
void stream_begin_change(void); /* quiesce reader before changing sequencer state */
void stream_set_chain(int on, const char *(*cb)(void));
void stream_suspend(void);
void stream_resume(void);
int  stream_active(void);
int  stream_fetch(short *l, short *r);
unsigned stream_decoded_frames(void);
typedef struct {
    int active,suspended,exhausted,thread_error,io_error;
    unsigned decoded_blocks,queued_frames,open_failures;
    unsigned published_buffers,read_us,decode_us,max_read_us,max_decode_us,reader_priority;
} StreamProfile;
/* Approximate read-only transport snapshot; no file I/O or buffer reset. */
void stream_profile(StreamProfile *out);
void stream_set_file(const char *file);   /* switch track (restarts from its beginning) */
#endif
