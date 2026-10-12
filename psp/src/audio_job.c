/* ME-safe sample computation: no PSP imports, allocation or mutable globals.
   Scalar FPU only (ME has no VFPU). Shared OSB/SMSB/ADX decoders use the same
   arithmetic as CPU playback. Keep -ffp-contract=off. */
#include "audio_job.h"
#include "audio_spatial.h"
#include "adx.h"
#include <string.h>
static void mix_osb(AudioJobOsb *m,float dry[][2],int *sends,unsigned n,int enabled)
{
    for(unsigned i=0;i<n && m->voice.active;i++) {
        unsigned tl=(m->total>>8)+smsb_envelope_tick(&m->envelope),gain[2];
        source_aica_gains(m->level,(tl>255?255:tl)<<8,gain);
        int sample=osb_next(&m->voice);
        dry[i][0]+=((sample*(int)gain[0])>>15)*(1.f/32768.f);
        dry[i][1]+=((sample*(int)gain[1])>>15)*(1.f/32768.f);
        if(enabled && !(m->voice.program.dsp&15))sends[i]+=source_aica_dsp(sample,tl,m->voice.program.dsp>>4);
        if(m->envelope.state==4)m->voice.active=0;
    }
}
static int message_next(AudioJobState *s)
{
    MovieVoice *v=&s->message;
    if(!v->active || !v->ready || v->error)return 0;
    unsigned index=v->position&31;
    if(!index) {
        unsigned block=v->position/32,chunk=block/512;
        const unsigned char *p=chunk<AUDIO_JOB_MESSAGE_CHUNKS ? s->message_chunks[chunk]:NULL;
        if(!p || adx_decode_continuous(p+(block%512)*18,18,1,v->decoded,32,v->history,7334,-3283)!=1){v->active=0;v->error=1;return 0;}
    }
    int result=v->decoded[index];if(++v->position>=v->samples)v->active=0;return result;
}
int audio_mix_job_execute(AudioMixJob *job)
{
    if(job->bank>1 || job->count>REVERB_JOB_FRAMES || job->input.osb_count>AUDIO_JOB_OSB_MAX || job->input.sequence_count>job->sequence_capacity || job->sequence_capacity>AUDIO_JOB_SEQUENCES)return -1;
    Smsb *sequences=job->output.sequences;
    job->output=job->input;job->output.sequences=sequences;AudioJobState *s=&job->output;
    if(s->sequence_count)memcpy(sequences,job->input.sequences,s->sequence_count*sizeof *sequences);
    PackedReverb *verb=&job->storage->bank[job->bank^1];
    if(job->reset)packed_reverb_reset(verb);
    else memcpy(verb,&job->storage->bank[job->bank],sizeof *verb);
    for(unsigned base=0;base<job->count;base+=AUDIO_JOB_BLOCK) {
        unsigned n=job->count-base;if(n>AUDIO_JOB_BLOCK)n=AUDIO_JOB_BLOCK;
        float dry[AUDIO_JOB_BLOCK][2]={{0}};int sends[AUDIO_JOB_BLOCK]={0},seqpcm[AUDIO_JOB_BLOCK][3];
        for(unsigned i=0;i<n;i++) {
            dry[i][0]=(float)job->music[base+i][0]*(1.f/32768.f)*s->music_gain;
            dry[i][1]=(float)job->music[base+i][1]*(1.f/32768.f)*s->music_gain;
        }
        for(unsigned i=0;i<n && s->movie.active;i++) {
            float v=movie_voice_next(&s->movie)*(1.f/32768.f);dry[i][0]+=v;dry[i][1]+=v;
        }
        if(!s->paused) {
            for(unsigned i=0;i<n && s->countdown.active;i++) {
                float v=movie_voice_next(&s->countdown)*(1.f/32768.f)*s->speech_volume;dry[i][0]+=v;dry[i][1]+=v;
            }
            for(unsigned i=0;i<n && s->message.active;i++) {
                float v=message_next(s)*(1.f/32768.f)*s->speech_volume;dry[i][0]+=v;dry[i][1]+=v;
            }
        }
        for(unsigned v=0;v<s->osb_count;v++)mix_osb(s->osb+v,dry,sends,n,s->enabled);
        for(unsigned v=0;v<s->sequence_count;v++) {
            Smsb *seq=s->sequences+v;
            if(!seq->sequence){seq->dsp_input=0;continue;}
            smsb_next_buffer(seq,seqpcm,n);
            for(unsigned i=0;i<n;i++){dry[i][0]+=seqpcm[i][0]*(1.f/32768.f);dry[i][1]+=seqpcm[i][1]*(1.f/32768.f);sends[i]+=seqpcm[i][2];}
        }
        mix_osb(&s->garage,dry,sends,n,s->enabled);
        for(unsigned i=0;i<n && s->final_active;i++) {
            int sample=s->final_pcm[s->final_position++];
            dry[i][0]+=((sample*(int)s->final_gain[0])>>15)*(1.f/32768.f);
            dry[i][1]+=((sample*(int)s->final_gain[1])>>15)*(1.f/32768.f);
            if(s->final_position>=s->final_samples)s->final_active=0;
        }
        for(unsigned v=0;v<AUDIO_JOB_SFX;v++)if(s->sfx[v].active) {
            AudioJobSfx *f=s->sfx+v;
            if((unsigned)f->id>=64)return -2;
            for(unsigned i=0;i<n;i++) {
                unsigned sp=f->pos>>1;if(sp>=s->sfx_len[f->id]){f->active=0;break;}
                float sample=(float)s->sfx_pcm[f->id][sp]*(1.f/32768.f)*f->vol;
                dry[i][0]+=sample;dry[i][1]+=sample;f->pos++;
            }
        }
        /* Keep the DSP out of the dry loop's instruction-cache footprint. */
        int wet[AUDIO_JOB_BLOCK][2];
        if(s->enabled)for(unsigned i=0;i<n;i++)packed_reverb_next(verb,sends[i],wet[i]);
        for(unsigned i=0;i<n;i++) {
            float l=dry[i][0],r=dry[i][1];
            if(s->enabled){l+=wet[i][0]*(1.f/32768.f);r+=wet[i][1]*(1.f/32768.f);}
            if(s->mono)l=r=(l+r)*.5f;
            if(l>1.f)l=1.f;
            if(l< -1.f)l=-1.f;
            if(r>1.f)r=1.f;
            if(r< -1.f)r=-1.f;
            job->pcm[base+i][0]=(short)(int)(l*28000.f);job->pcm[base+i][1]=(short)(int)(r*28000.f);
        }
    }
    memcpy(job->music_decode.history,job->music_decode_history,sizeof job->music_decode_history);
    stream_decode_execute(&job->music_decode);
    return (int)(0x4d49584au^job->serial^job->count);
}
int audio_mix_job_worker(int arg){return audio_mix_job_execute((AudioMixJob *)(uintptr_t)(unsigned)arg);}
