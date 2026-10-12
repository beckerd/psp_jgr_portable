#include <pspkernel.h>
#include <string.h>
#include <stddef.h>
#include "audio_decode.h"
/* One producer (stream reader), one consumer (audio callback). The ME never
   sees this mailbox: it gets a private copy inside the mixer transaction.
   Cancellation therefore needs no cross-core access to a reader stack or
   file buffer, and a late reply cannot publish into a replacement request. */
static StreamDecodeJob mailbox;
static volatile int enabled,state; /* 0 idle, 1 queued, 2 done, 3 copied */
static unsigned generation,completed,cancelled,blocks_completed;
void audio_decode_enable(int on)
{
    unsigned key=sceKernelCpuSuspendIntr();enabled=on;
    if(!on)state=0;
    sceKernelCpuResumeIntr(key);
}
int audio_decode_request(const unsigned char *p,unsigned blocks,int channels,int c1,int c2,int hist[2][2],short *out)
{
    if(!enabled || !blocks || blocks>STREAM_DECODE_BLOCKS || (channels!=1 && channels!=2))return 0;
    unsigned key=sceKernelCpuSuspendIntr();
    if(!enabled || state){sceKernelCpuResumeIntr(key);return 0;}
    mailbox.serial=++generation;mailbox.blocks=blocks;mailbox.channels=channels;
    mailbox.coefficient1=c1;mailbox.coefficient2=c2;memcpy(mailbox.history,hist,sizeof mailbox.history);
    memcpy(mailbox.input,p,blocks*18*channels);state=1;sceKernelCpuResumeIntr(key);
    unsigned started=sceKernelGetSystemTimeLow();
    while(enabled && state!=0 && state!=2 && sceKernelGetSystemTimeLow()-started<46000u)sceKernelDelayThread(200);
    key=sceKernelCpuSuspendIntr();int ok=state==2;
    if(ok){memcpy(out,mailbox.pcm,blocks*32*2*sizeof *out);memcpy(hist,mailbox.history,sizeof mailbox.history);completed++;blocks_completed+=blocks;}
    else cancelled++;
    state=0;sceKernelCpuResumeIntr(key);return ok;
}
void audio_decode_take(StreamDecodeJob *job)
{
    unsigned key=sceKernelCpuSuspendIntr();job->blocks=0;
    if(enabled && state==1) {
        /* Exclude unused PCM from the input copy. */
        memcpy(job,&mailbox,offsetof(StreamDecodeJob,pcm));state=3;
    }
    sceKernelCpuResumeIntr(key);
}
void audio_decode_retire(const StreamDecodeJob *job)
{
    unsigned key=sceKernelCpuSuspendIntr();
    if(job->blocks && enabled && state==3 && job->serial==mailbox.serial) {
        memcpy(mailbox.pcm,job->pcm,job->blocks*32*2*sizeof(short));memcpy(mailbox.history,job->history,sizeof mailbox.history);state=2;
    }
    sceKernelCpuResumeIntr(key);
}
void audio_decode_profile(unsigned out[3])
{
    unsigned key=sceKernelCpuSuspendIntr();out[0]=completed;out[1]=blocks_completed;out[2]=cancelled;
    completed=blocks_completed=cancelled=0;sceKernelCpuResumeIntr(key);
}
