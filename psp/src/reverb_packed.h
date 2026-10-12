#ifndef JGR_REVERB_PACKED_H
#define JGR_REVERB_PACKED_H
#include "reverb.h"
/* Two exact AICA-encoded states occupy almost the same space as the existing
   decoded ring. Each starts/ends on a private cache line. The ME writes only
   the uncommitted bank; timeout recovery reads the other bank after fencing
   the ME. This is the original delay codec, not lower-quality PCM. */
typedef struct {
    int32_t temp[128],mem[32];uint16_t ring[SOURCE_REVERB_MASK+1];unsigned cursor,started;
} __attribute__((aligned(64))) PackedReverb;
typedef union {SourceReverb cpu;PackedReverb bank[2];} __attribute__((aligned(64))) ReverbStorage;
static inline int packed_reverb_read(const PackedReverb *s,unsigned at)
{return source_reverb_unpack(s->ring[at]);}
static inline void packed_reverb_write(PackedReverb *s,unsigned at,int value)
{s->ring[at]=source_reverb_pack(value);}
#define SourceReverb PackedReverb
#define source_reverb_program packed_reverb_program
#define source_reverb_read packed_reverb_read
#define source_reverb_write packed_reverb_write
#if JGR_PORTABLE_ASSETS
#include "source_data_reverb_program.inc"
#else
#include "reverb_program.inc"
#endif
#undef SourceReverb
#undef source_reverb_program
#undef source_reverb_read
#undef source_reverb_write
static inline void packed_reverb_reset(PackedReverb *s)
{
    for(unsigned i=0;i<128;i++)s->temp[i]=0;
    for(unsigned i=0;i<32;i++)s->mem[i]=0;
    for(unsigned i=0;i<=SOURCE_REVERB_MASK;i++)s->ring[i]=0x6000;
    s->cursor=1;s->started=0;
}
static inline void packed_reverb_next(PackedReverb *s,int input,int out[2])
{
    if(!s->started && !input){s->cursor=s->cursor==1 ? SOURCE_REVERB_MASK+1:s->cursor-1;out[0]=out[1]=0;return;}
    s->started=1;packed_reverb_program(s,input,out);
}
/* In-place recovery: source bank zero expands backwards; bank one expands
   forwards (its encoded input stays ahead of the decoded output). Preserve
   metadata before either direction can overwrite it. ME must be fenced. */
static inline void packed_reverb_recover(ReverbStorage *s,unsigned bank)
{
    PackedReverb *src=&s->bank[bank];
    int32_t temp[128],mem[32];unsigned cursor=src->cursor,started=src->started;
    memcpy(temp,src->temp,sizeof temp);memcpy(mem,src->mem,sizeof mem);
#ifdef SOURCE_REVERB_PACKED
    memmove(s->cpu.ring,src->ring,sizeof s->cpu.ring);
#else
    if(bank)for(unsigned i=0;i<=SOURCE_REVERB_MASK;i++)s->cpu.ring[i]=source_reverb_unpack(src->ring[i]);
    else for(unsigned i=SOURCE_REVERB_MASK+1;i--;)s->cpu.ring[i]=source_reverb_unpack(src->ring[i]);
#endif
    memcpy(s->cpu.temp,temp,sizeof temp);memcpy(s->cpu.mem,mem,sizeof mem);
    s->cpu.cursor=cursor;s->cpu.started=started;
}
/* Resume from a recovered CPU state. Compression moves forwards so each
   source word is read before its space can be overwritten. Core fenced. */
static inline void packed_reverb_from_cpu(ReverbStorage *s)
{
    unsigned cursor=s->cpu.cursor,started=s->cpu.started;
    for(unsigned i=0;i<=SOURCE_REVERB_MASK;i++)
        s->bank[0].ring[i]=source_reverb_pack(source_reverb_read(&s->cpu,i));
    s->bank[0].cursor=cursor;s->bank[0].started=started;
}
#endif
