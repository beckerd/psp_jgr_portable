/* PSP backend adapter for original 0b1b68/0b209c channel ownership.
   Called only under scene_lock. Wrapper identities outlive their channel;
   a retired wrapper must never stop a newer occupant of the same channel.
   This does not replace the ARM driver's separate note-voice allocator. */
#include "audio_channels.h"
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_audio_channel_tables.inc"
#else
#include "generated/source_data/audio_channel_tables.inc"
#endif
typedef struct {
    OsbVoice *voice;Smsb *sequence;unsigned stream;
    int *finished;
    const unsigned char *header;unsigned *level,*total;
    SourceAudioRamp volume_ramp,pan_ramp;int volume,pan;
} AudioChannelBackend;
static SourceAudioChannels audio_channels;
static AudioChannelBackend audio_backends[17];
static void audio_channel_detach(unsigned slot)
{
    AudioChannelBackend *b=audio_backends+slot;
    if(b->voice)b->voice->active=0;
    if(b->sequence)smsb_release(b->sequence,b->stream);
    if(b->finished)*b->finished=1;
    source_audio_channel_release(&audio_channels,audio_channels.owner[slot]);
    memset(b,0,sizeof *b);
}
static void audio_channel_release(const void *owner)
{
    for(unsigned i=0;i<17;i++)if(audio_channels.owner[i]==(uintptr_t)owner && owner)audio_channel_detach(i);
}
static void audio_channel_reap(void)
{
    /* Sequence wrappers retain their original age450 lifetime; their
       controller, not this sample-end check, releases those channels. */
    for(unsigned i=0;i<8;i++)if(audio_channels.owner[i] && audio_backends[i].voice &&
                              !audio_backends[i].voice->active)audio_channel_detach(i);
}
static int audio_channel_acquire_class(const void *owner,unsigned cls,int protect)
{
    audio_channel_reap();
    uintptr_t displaced;int slot=source_audio_channel_acquire(&audio_channels,(uintptr_t)owner,
        cls,protect,&displaced);
    if(slot<0)return -1;
    /* Acquire has already installed the successor. Retire only the old
       backend; releasing its identity now would alias a reused address. */
    AudioChannelBackend *b=audio_backends+slot;
    if(displaced){
        if(b->voice)b->voice->active=0;
        if(b->sequence)smsb_release(b->sequence,b->stream);
        if(b->finished)*b->finished=1;
    }
    memset(b,0,sizeof *b);return slot;
}
static int audio_channel_acquire(const void *owner,unsigned bank,unsigned program)
{
    int cls=audio_source_class(bank,program);if(cls<0)return -1;
    return audio_channel_acquire_class(owner,cls,source_audio_protected(bank,program));
}
static void audio_channel_registers(AudioChannelBackend *b)
{source_osb_registers(b->header,b->volume,b->pan,b->level,b->total);}
static int audio_channel_fade(OsbVoice *voice,int pan,int value,int duration)
{
    for(unsigned i=0;i<8;i++){
        AudioChannelBackend *b=audio_backends+i;if(b->voice!=voice)continue;
        SourceAudioRamp *r=pan ? &b->pan_ramp:&b->volume_ramp;
        if(source_audio_ramp_set(r,value,duration))return -1;
        if(pan)b->pan=source_audio_ramp_value(r);else b->volume=source_audio_ramp_value(r);
        audio_channel_registers(b);return 0;
    }
    return -1;
}
static void audio_channel_levels(OsbVoice *voice,int volume,int pan)
{audio_channel_fade(voice,0,volume,0);audio_channel_fade(voice,1,pan,0);}
static void audio_channel_service(void)
{
    for(unsigned i=0;i<8;i++){
        AudioChannelBackend *b=audio_backends+i;if(!b->voice)continue;int changed=0;
        if(source_audio_ramp_tick(&b->volume_ramp)){b->volume=source_audio_ramp_value(&b->volume_ramp);changed=1;}
        if(source_audio_ramp_tick(&b->pan_ramp)){b->pan=source_audio_ramp_value(&b->pan_ramp);changed=1;}
        if(changed)audio_channel_registers(b);
    }
}
static int audio_managed_osb_header(OsbVoice *v,SmsbEnvelope *env,unsigned *level,unsigned *total,
    unsigned bank,unsigned program,const unsigned char *header,const OsbProgram *p,int *finished)
{
    int slot=audio_channel_acquire(v,bank,program);if(slot<0)return 0;
    AudioChannelBackend *b=audio_backends+slot;
    b->voice=v;b->finished=finished;b->level=level;b->total=total;
    b->header=header;
    if(finished)*finished=0;
    audio_osb_start_header(v,env,level,total,header,p);
    audio_channel_levels(v,127,0);return 1;
}
static int audio_managed_osb(OsbVoice *v,SmsbEnvelope *env,unsigned *level,unsigned *total,
    unsigned bank,unsigned program,const unsigned char *image,const OsbProgram *p,int *finished)
{
    unsigned offset;memcpy(&offset,image+16+program*4,4);
    return audio_managed_osb_header(v,env,level,total,bank,program,image+offset,p,finished);
}
