/* Audio API thread updates remain live while the callback waits for ME.
   Cursor snapshots commit only to the same voice. Sequence mix controls
   are replayed on its advanced state, so traffic pan/volume updates cannot
   repeatedly rewind the sequencer. Starts/stops/rebinds supersede old jobs. */
static unsigned audio_simple_epoch[10],audio_job_simple_epoch[10];
#define AUDIO_TOUCH_SIMPLE(i) (++audio_simple_epoch[i])
static int audio_mix_pending;
static OsbVoice *audio_job_voice[AUDIO_JOB_OSB_MAX+1];
static unsigned char audio_job_voice_changed[AUDIO_JOB_OSB_MAX+1];
static Smsb *audio_job_sequence[AUDIO_JOB_SEQUENCES];
static unsigned char audio_job_sequence_changed[AUDIO_JOB_SEQUENCES];
static struct {int set,value,duration;} audio_seq_controls[AUDIO_JOB_SEQUENCES][2][2];
static void audio_touch_voice(OsbVoice *voice)
{
    if(audio_mix_pending)for(unsigned i=0;i<AUDIO_JOB_OSB_MAX+1;i++)
        if(audio_job_voice[i]==voice)audio_job_voice_changed[i]=1;
}
static int audio_sequence_index(Smsb *s)
{
    if(audio_mix_pending)for(unsigned i=0;i<AUDIO_JOB_SEQUENCES;i++)if(audio_job_sequence[i]==s)return (int)i;
    return -1;
}
static void audio_sequence_touch(Smsb *s)
{int i=audio_sequence_index(s);if(i>=0)audio_job_sequence_changed[i]=1;}
static int audio_smsb_start(Smsb *s,unsigned slot,unsigned program)
{audio_sequence_touch(s);return smsb_start(s,slot,program);}
static void audio_smsb_stop(Smsb *s,unsigned slot)
{audio_sequence_touch(s);smsb_stop(s,slot);}
static void audio_smsb_release(Smsb *s,unsigned slot)
{audio_sequence_touch(s);smsb_release(s,slot);}
static void audio_sequence_control(Smsb *s,unsigned slot,int pan,int value,int duration)
{
    int i=audio_sequence_index(s);
    if(i>=0 && slot<2){audio_seq_controls[i][slot][pan].set=1;audio_seq_controls[i][slot][pan].value=value;audio_seq_controls[i][slot][pan].duration=duration;}
}
static void audio_smsb_volume(Smsb *s,unsigned slot,int value)
{audio_sequence_control(s,slot,0,value,0);smsb_volume(s,slot,value);}
static void audio_smsb_pan(Smsb *s,unsigned slot,int value)
{audio_sequence_control(s,slot,1,value,0);smsb_pan(s,slot,value);}
static int audio_smsb_fade_volume(Smsb *s,unsigned slot,int value,int duration)
{int r=smsb_fade_volume(s,slot,value,duration);if(!r)audio_sequence_control(s,slot,0,value,duration);return r;}
static int audio_smsb_fade_pan(Smsb *s,unsigned slot,int value,int duration)
{int r=smsb_fade_pan(s,slot,value,duration);if(!r)audio_sequence_control(s,slot,1,value,duration);return r;}
#define smsb_start audio_smsb_start
#define smsb_stop audio_smsb_stop
#define smsb_release audio_smsb_release
#define smsb_volume audio_smsb_volume
#define smsb_pan audio_smsb_pan
#define smsb_fade_volume audio_smsb_fade_volume
#define smsb_fade_pan audio_smsb_fade_pan
