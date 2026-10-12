#include <pspkernel.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <errno.h>
#include "stream.h"
#include "audio_decode.h"

static char music_file[64] = "music.adx";
#define SBUF_FRAMES 2048
#define NBUF 48
#define FIRST_FRAMES 2048
#ifndef STREAM_READER_PRIORITY
/* Output runs at 0x12 (SDK pspAudio), main at 0x20. The former 0x30
   decoder starved whenever gameplay used the available CPU budget. */
#define STREAM_READER_PRIORITY 0x18
#endif

static short sbuf[NBUF][SBUF_FRAMES * 2];
static volatile int filled[NBUF], bufused[NBUF];
static volatile int quit, have_music, suspended, resume_req;
/* Power suspension and a track replacement are independent requests.
   Keep an epoch so even a complete sleep/wake between reader wakes forces
   the old Memory Stick handle to be closed and reopened. */
static volatile int power_suspended;
static volatile unsigned power_epoch;
static volatile int reader_paused;
static volatile int exhausted;
static unsigned saved_pos;
static int rd_buf, rd_pos, wr_buf, quick_start = 1;
static volatile unsigned decoded_frames;
static volatile int thread_error,io_error;
static volatile unsigned open_failures;
static int reader_id=-1;
static unsigned published_buffers,read_us,decode_us,max_read_us,max_decode_us;
static unsigned stream_time(void)
{
#ifdef __PSP__
    return sceKernelGetSystemTimeLow();
#else
    return 0;
#endif
}

/* ---- ADX state ---- */
static int chain_mode; static const char *(*end_cb)(void);   /* chain mode (DC stream mode 2): at the end of the file ask the sequencer for the next one instead of looping */
static int adx_ch, adx_rate, adx_start, adx_has_loop; static unsigned adx_total, adx_loop_start, adx_loop_end; static int adx_coef1, adx_coef2;
static int hist[2][2];
static unsigned char fbuf[36 * 512];       /* 512 sample-frames of stereo ADX per read */

static int be16(const unsigned char *p) { return (p[0] << 8) | p[1]; }
static unsigned be32(const unsigned char *p) { return ((unsigned)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

static int adx_open(FILE *f)
{
    unsigned char h[64];
    if (fread(h, 1, 64, f) != 64 || be16(h) != 0x8000 || h[4] != 3 || h[5] != 18 || h[6] != 4) return 0;
    adx_ch = h[7]; adx_rate = (int)be32(h + 8); adx_total = be32(h + 12); int hp = be16(h + 16); int ver = h[18];
    adx_start = be16(h + 2) + 4; adx_loop_start = 0; adx_loop_end = adx_total; adx_has_loop=0;
    if (ver == 3 && be32(h + 0x18)) { adx_has_loop=1; adx_loop_start = be32(h + 0x1c); adx_loop_end = be32(h + 0x24); }
    if (ver == 4 && be32(h + 0x24)) { adx_has_loop=1; adx_loop_start = be32(h + 0x28); adx_loop_end = be32(h + 0x30); }
    if((adx_ch!=1 && adx_ch!=2) || adx_rate<=0 || !adx_total ||
       adx_loop_start>=adx_loop_end || adx_loop_end>adx_total)return 0;
    double a = sqrt(2.0) - cos(2.0 * 3.14159265358979 * hp / adx_rate), b = sqrt(2.0) - 1.0, c = (a - sqrt((a + b) * (a - b))) / b;
    adx_coef1 = (int)(c * 2.0 * 4096.0); adx_coef2 = (int)(-(c * c) * 4096.0);
    if(chain_mode==STREAM_CHAIN){adx_loop_start=0;adx_loop_end=adx_total;}
    memset(hist, 0, sizeof hist);
    return adx_ch == 1 || adx_ch == 2;
}
/* decode one 32-sample frame per channel from p into interleaved stereo out */
static void adx_frame(const unsigned char *p, short *out)
{
    for (int ch = 0; ch < adx_ch; ch++) {
        const unsigned char *d = p + ch * 18; int scale = be16(d); const unsigned char *n = d + 2;
        int h0 = hist[ch][0], h1 = hist[ch][1];
        for (int i = 0; i < 32; i++) {
            int nib = (i & 1) ? (n[i >> 1] & 15) : (n[i >> 1] >> 4); if (nib >= 8) nib -= 16;
            int v = nib * scale + ((adx_coef1 * h0 + adx_coef2 * h1) >> 12);
            if (v > 32767) v = 32767; if (v < -32768) v = -32768;
            h1 = h0; h0 = v; out[i * 2 + ch] = (short)v;
            if (adx_ch == 1) out[i * 2 + 1] = (short)v;
        }
        hist[ch][0] = h0; hist[ch][1] = h1;
    }
}
static unsigned cur_sample;   /* absolute sample position of the next frame */
static void adx_seek_frame(FILE *f, unsigned sample) { cur_sample = sample - sample % 32; fseek(f, adx_start + (long)(cur_sample / 32) * 18 * adx_ch, SEEK_SET); memset(hist, 0, sizeof hist); }

static int reader_thread(SceSize args, void *argp)
{
    FILE *f = 0;
    unsigned observed_power=power_epoch;
    int saved_history[2][2]={{0}};
    while (!quit) {
        unsigned requested_power=power_epoch;
        if(suspended || power_suspended || observed_power!=requested_power) {
            if(f) {
                saved_pos=cur_sample;memcpy(saved_history,hist,sizeof hist);
                fclose(f);f=0;
            }
            observed_power=requested_power;
        }
        if (suspended || power_suspended) { reader_paused=1;sceKernelDelayThread(1000);continue; }
        reader_paused=0;
        if (resume_req) { resume_req = 0; for (int b = 0; b < NBUF; b++) filled[b] = 0; rd_buf = rd_pos = wr_buf = 0; quick_start = 1; memset(saved_history,0,sizeof saved_history); }
        /* A terminal sequencer node stays terminal. Previously f==NULL
           reopened the same file and silently looped one-shot credits. */
        if(exhausted){sceKernelDelayThread(4000);continue;}
        if (!f) {
            f = fopen(music_file, "rb");  if (!f) f = fopen("music.adx", "rb");
            if (f && adx_open(f)) { io_error=0;have_music = 1; adx_seek_frame(f, saved_pos);memcpy(hist,saved_history,sizeof hist); }
            else { io_error=f ? -1:(errno ? errno:EIO);open_failures++;if (f) fclose(f); f = 0; sceKernelDelayThread(500000); continue; }
        }
        /* Publish one 46 ms chunk per wake, instead of decoding up to
           2.2 seconds before yielding. Total PCM capacity stays 98,304
           frames. This bounds producer preemption of the game thread. */
        for (int b = 0; b < 1 && !filled[wr_buf]; b++) {
            int target = quick_start ? FIRST_FRAMES : SBUF_FRAMES; int got = 0; short *dst = sbuf[wr_buf];
            while (got + 32 <= target && !power_suspended && observed_power==power_epoch) {
                int want = (target - got) / 32; if (want > 512) want = 512;
                unsigned remaining_frames = (adx_loop_end > cur_sample) ? (adx_loop_end - cur_sample + 31) / 32 : 0;
                if (remaining_frames == 0 && chain_mode && !(chain_mode==STREAM_FILE && adx_has_loop)) { const char *nx = end_cb ? end_cb() : 0; if (nx) { strncpy(music_file, nx, 63); music_file[63] = 0; fclose(f); f = 0; saved_pos = 0;memset(saved_history,0,sizeof saved_history); break; } exhausted=1; fclose(f); f = 0; break; }
                if (remaining_frames == 0) { adx_seek_frame(f, adx_loop_start); continue; }
                if ((unsigned)want > remaining_frames) want = (int)remaining_frames;
                unsigned started=stream_time();
                int n = (int)fread(fbuf, 18 * adx_ch, want, f);
                unsigned elapsed=stream_time()-started;read_us+=elapsed;if(elapsed>max_read_us)max_read_us=elapsed;
                /* A valid loop wraps at its declared sample boundary above.
                   EOF before that boundary is truncation/I/O failure. Do
                   not spin forever at the now-higher decoder priority. */
                if (n <= 0) {
                    if(power_suspended || observed_power!=power_epoch)break;
                    io_error=ferror(f) && errno ? errno:EIO;exhausted=1;fclose(f);f=0;break;
                }
                started=stream_time();
                /* Keep startup/near-empty transport immediate. Once at
                   least three chunks are queued, the next audio callback
                   may decode this immutable read inside its ME job. */
                unsigned queued=0;for(int q=0;q<NBUF;q++)if(filled[q])queued++;
                if(queued<3 || !audio_decode_request(fbuf,n,adx_ch,adx_coef1,adx_coef2,hist,dst+got*2))
                    for(int i=0;i<n;i++)adx_frame(fbuf+i*18*adx_ch,dst+(got+i*32)*2);
                elapsed=stream_time()-started;decode_us+=elapsed;if(elapsed>max_decode_us)max_decode_us=elapsed;
                unsigned valid=n*32;if(valid>adx_loop_end-cur_sample)valid=adx_loop_end-cur_sample;
                got += valid; cur_sample += n * 32; decoded_frames += n;
            }
            if (got == 0) break;
            bufused[wr_buf] = got; filled[wr_buf] = 1; wr_buf = (wr_buf + 1) % NBUF; quick_start = 0;published_buffers++;
            if (!f) break;
        }
        sceKernelDelayThread(filled[wr_buf] ? 10000:1000);
    }
    if (f) fclose(f);
    return 0;
}
void stream_begin_change(void)
{
    have_music=0;suspended=1;
    /* Acknowledge the old file's close before replacing its name/cursor.
       The fixed 60 ms delay let the reader overwrite the NEW zero cursor
       with the OLD file position, and was not a scheduling guarantee. */
    if(reader_id>=0 && !thread_error)while(!reader_paused && !quit)sceKernelDelayThread(1000);
}
void stream_set_file(const char *file)
{
    stream_begin_change();
    if(file){strncpy(music_file,file,63);music_file[63]=0;}
    saved_pos=0;exhausted=0;resume_req=1;suspended=0;
}
void stream_init(const char *file)
{
    if(file){strncpy(music_file,file,63);music_file[63]=0;}
    reader_id=sceKernelCreateThread("music_stream",reader_thread,STREAM_READER_PRIORITY,0x10000,0,0);
    thread_error=reader_id<0 ? reader_id:sceKernelStartThread(reader_id,0,0);
}
void stream_shutdown(void) { quit = 1; }
void stream_set_chain(int on, const char *(*cb)(void)) { chain_mode = on; end_cb = cb; }   /* 0: legacy loop, CHAIN: callback, FILE: embedded loop or callback at EOF */
void stream_suspend(void) { if(!power_suspended){power_suspended=1;power_epoch++;} }
void stream_resume(void) { power_suspended=0; }
int stream_active(void) { return have_music; }
unsigned stream_decoded_frames(void) { return decoded_frames; }
void stream_profile(StreamProfile *out)
{
    unsigned queued=0;
    for(int i=0;i<NBUF;i++)if(filled[i]) {
        int remaining=bufused[i]-(i==rd_buf ? rd_pos:0);
        if(remaining>0)queued+=(unsigned)remaining;
    }
    *out=(StreamProfile){have_music,suspended || power_suspended,exhausted,thread_error,io_error,
        decoded_frames,queued,open_failures,published_buffers,read_us,decode_us,max_read_us,max_decode_us,STREAM_READER_PRIORITY};
}
int stream_fetch(short *l, short *r)
{
    if(power_suspended){*l=*r=0;return 0;}
    if (!filled[rd_buf]) { *l = *r = 0; if(exhausted)have_music=0; return 0; }
    *l = sbuf[rd_buf][rd_pos * 2]; *r = sbuf[rd_buf][rd_pos * 2 + 1]; rd_pos++;
    if (rd_pos >= bufused[rd_buf]) { filled[rd_buf] = 0; rd_buf = (rd_buf + 1) % NBUF; rd_pos = 0; }
    return 1;
}
