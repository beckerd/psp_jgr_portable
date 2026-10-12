#ifndef JGR_REVERB_JOB_H
#define JGR_REVERB_JOB_H
#include "reverb_packed.h"
#define REVERB_JOB_FRAMES 1024u
typedef struct {
    ReverbStorage *storage;const int *sends;int (*wet)[2];
    unsigned bank,count,reset,serial;
} __attribute__((aligned(64))) ReverbJob;
static inline int reverb_job_execute(const ReverbJob *job)
{
    if(job->bank>1 || job->count>REVERB_JOB_FRAMES)return -1;
    PackedReverb *out=&job->storage->bank[job->bank^1];
    if(job->reset)packed_reverb_reset(out);
    else {
        const unsigned *src=(const unsigned *)&job->storage->bank[job->bank];unsigned *dst=(unsigned *)out;
        for(unsigned i=0;i<sizeof *out/sizeof *dst;i++)dst[i]=src[i];
    }
    for(unsigned i=0;i<job->count;i++)packed_reverb_next(out,job->sends[i],job->wet[i]);
    return (int)(0x13579bdfu^job->serial^job->count);
}
int reverb_job_worker(int param);
#endif
