#ifndef JGR_AUDIO_JOB_FIXTURE_H
#define JGR_AUDIO_JOB_FIXTURE_H
#include "audio_job.h"
#include <string.h>
/* Startup-only synthetic inputs exercise arithmetic/cache ownership without
   loading game banks. The same fixture is checked by the native PSP test. */
static short mix_verify_pcm[128];
static unsigned char mix_verify_adpcm[64],mix_verify_adx[18*64],mix_verify_sequence[4];
static void audio_job_fixture(AudioMixJob *j)
{
    Smsb *input=j->input.sequences,*output=j->output.sequences;unsigned capacity=j->sequence_capacity;
    memset(j,0,sizeof *j);j->input.sequences=input;j->output.sequences=output;j->sequence_capacity=capacity;
    memset(input,0,capacity*sizeof *input);memset(output,0,capacity*sizeof *output);
    for(unsigned i=0;i<128;i++)mix_verify_pcm[i]=(short)(i*509-32000);
    for(unsigned i=0;i<64;i++)mix_verify_adpcm[i]=(unsigned char)(i*37);
    for(unsigned i=0;i<64;i++) {
        unsigned char *p=mix_verify_adx+i*18;p[0]=0;p[1]=7;
        for(unsigned k=2;k<18;k++)p[k]=(unsigned char)(k*13+i);
    }
    AudioJobState *s=&j->input;s->enabled=1;s->music_gain=.7f;s->speech_volume=.8f;
    s->osb_count=2;
    for(unsigned i=0;i<2;i++) {
        OsbProgram p={0};p.samples=i ? mix_verify_adpcm:(const unsigned char *)mix_verify_pcm;
        p.control=512|(i?256:0);p.length=128;p.step=i?1567:1024;p.dsp=15<<4;
        osb_start(&s->osb[i].voice,&p);
        smsb_envelope_start(&s->osb[i].envelope,0x1f,0,p.pitch);
        s->osb[i].level=0x0f0f;s->osb[i].total=0;
    }
    s->movie=(MovieVoice){.blocks=mix_verify_adx,.count=64,.samples=2048,.active=1,.ready=1};
    s->countdown=s->movie;s->message=s->movie;s->message_chunks[0]=mix_verify_adx;
    s->sequence_count=1;Smsb *seq=s->sequences;seq->sequence=mix_verify_sequence;
    seq->voices[0].active=1;seq->voices[0].sample=s->osb[0].voice;
    seq->voices[0].envelope=s->osb[0].envelope;seq->voices[0].level=0x0f0f;seq->voices[0].off_clock=0x7fffffff;
    s->sfx_pcm[0]=mix_verify_pcm;s->sfx_len[0]=128;s->sfx[0]=(AudioJobSfx){0,0,.6f,1};
    s->final_pcm=mix_verify_pcm;s->final_samples=128;s->final_active=1;s->final_gain[0]=20000;s->final_gain[1]=30000;
    for(unsigned i=0;i<1024;i++){j->music[i][0]=(short)(i*83);j->music[i][1]=(short)(i*-53);}
    j->music_decode.blocks=32;j->music_decode.channels=2;j->music_decode.coefficient1=7334;j->music_decode.coefficient2=-3283;
    memcpy(j->music_decode.input,mix_verify_adx,sizeof mix_verify_adx);
}
#endif
