/* EVENT dependency I/O runs before playback. The callback reads immutable
   SOSP fragments / sequence banks; no file access or allocation while mixing. */
#include "event_audio.h"
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_event_audio_tables.inc"
#else
#include "generated/source_data/event_audio_tables.inc"
#endif
#include "smpb_storage.h"
typedef struct {
    OsbVoice voice;SmsbEnvelope envelope;unsigned level,total;
    const EventAudioCue *cue;Smsb *sequence;int finished;
} EventAudioHandle;
typedef struct {
    const EventAudioCue *cues;unsigned count;
    OsbSample **samples;
    unsigned char *sequence[2];unsigned sequence_size[2];SmsbPatchBank patch[2];
    EventAudioHandle handles[EVENT_AUDIO_HANDLES];
    Smsb *sequencers[8];
} EventAudio;
static EventAudio *event_audio;
static EventAudioLoadInfo event_load_info;
void audio_event_reset_load_info(int id){event_load_info=(EventAudioLoadInfo){.id=id};}
EventAudioLoadInfo audio_event_load_info(void){return event_load_info;}
static int event_audio_record(FILE *f,unsigned index,unsigned out[2])
{
    unsigned header[2];unsigned char row[32];
    rewind(f);
    if(fread(header,8,1,f)!=1 || memcmp(header,"CAS1",4) || header[1]>1024)return -1;
    for(unsigned i=0;i<header[1];i++) {
        if(fread(row,32,1,f)!=1)return -1;
        unsigned id;memcpy(&id,row+16,4);
        if(id==index && !memcmp(row,"E_SND_SE\0",9)){memcpy(out,row+20,8);return 0;}
    }
    return -1;
}
static unsigned char *event_audio_read(FILE *f,unsigned index,unsigned *size)
{
    unsigned r[2];if(event_audio_record(f,index,r) || r[1]<20 || r[1]>0x200000)return NULL;
    unsigned char *data=malloc(r[1]);if(!data)return NULL;
    if(fseek(f,r[0],SEEK_SET) || fread(data,1,r[1],f)!=r[1]){free(data);return NULL;}
    *size=r[1];return data;
}
static void event_audio_delete(EventAudio *p)
{
    if(!p)return;
    if(p->samples)for(unsigned i=0;i<p->count;i++)osb_parts_free(p->samples[i],1);
    for(unsigned i=0;i<2;i++){
        free(p->sequence[i]);
        /* Pending ME snapshots still point at p->patch. Retire its storage
           without clearing that immutable view before the audio fence. */
        SmsbPatchBank retired=p->patch[i];smpb_storage_free(&retired);
    }
    for(unsigned i=0;i<8;i++)free(p->sequencers[i]);
    free(p->samples);free(p);
}
void audio_free_event(void)
{
    lock_scene();EventAudio *old=event_audio;event_audio=NULL;
    if(old)for(unsigned i=0;i<EVENT_AUDIO_HANDLES;i++)audio_channel_release(old->handles+i);
    unlock_scene();event_audio_delete(old);
}
int audio_load_event(int id)
{
    audio_free_event();unsigned count;const EventAudioCue *cues=event_audio_cues(id,&count);
    audio_event_reset_load_info(id);
    if(!count)return 0;
    FILE *f=fopen("cutscene_sources.bin","rb");
    if(!f){event_load_info.error=-1;return -1;}
    event_load_info.stage=1;event_load_info.largest=sizeof(EventAudio);
    EventAudio *p=calloc(1,sizeof *p);int error=-4;
    if(!p)goto done;
    p->count=count;p->cues=cues;p->samples=calloc(count,sizeof *p->samples);
    if(!p->samples)goto done;
    /* Separate small cursors instead of one large EventAudio allocation.
       Preallocate all physical sequence slots; the callback never allocates. */
    for(unsigned i=0;i<8;i++) {
        p->sequencers[i]=calloc(1,sizeof(Smsb));if(!p->sequencers[i])goto done;
        if(sizeof(Smsb)>event_load_info.largest)event_load_info.largest=sizeof(Smsb);
    }
    for(unsigned i=0;i<count;i++) {
        const EventAudioCue *c=cues+i;unsigned r[2];
        if(c->backend==0) {
            event_load_info.file=c->file;event_load_info.stage=2;
            if(event_audio_record(f,c->file,r)){error=-3;goto done;}
            if((error=osb_read_range(f,r,c->program,1,p->samples+i)))goto done;
            if(p->samples[i]->bytes>event_load_info.largest)event_load_info.largest=p->samples[i]->bytes;
        } else if(c->backend==1) {
            unsigned bank=c->patch_bank-1;if(bank>=2){error=-5;goto done;}
            if(!p->sequence[bank]) {
                event_load_info.file=c->file;event_load_info.stage=3;
                p->sequence[bank]=event_audio_read(f,c->file,p->sequence_size+bank);
                if(!p->sequence[bank]){error=-5;goto done;}
                if(p->sequence_size[bank]>event_load_info.largest)event_load_info.largest=p->sequence_size[bank];
                event_load_info.file=c->patch_file;event_load_info.stage=4;
                unsigned largest=0;
                if(event_audio_record(f,c->patch_file,r)){error=-3;goto done;}
                error=smpb_storage_load(f,r,p->patch+bank,&largest);
                if(largest>event_load_info.largest)event_load_info.largest=largest;
                if(error)goto done;
                Smsb check;
                if(smsb_bind_parts(&check,p->sequence[bank],p->sequence_size[bank],p->patch+bank)){error=-5;goto done;}
            }
        } else {error=-6;goto done;}
    }
    lock_scene();event_audio=p;unlock_scene();p=NULL;error=0;
done:event_load_info.error=error;fclose(f);event_audio_delete(p);return error;
}
void audio_event_stop(unsigned slot)
{
    if(slot>=EVENT_AUDIO_HANDLES)return;
    lock_scene();if(event_audio){EventAudioHandle *h=event_audio->handles+slot;audio_channel_release(h);memset(h,0,sizeof *h);}unlock_scene();
}
int audio_event_voice(unsigned slot,unsigned kind,unsigned bank,unsigned reference)
{
    if(slot>=EVENT_AUDIO_HANDLES)return -1;
    lock_scene();EventAudio *p=event_audio;
    if(!p){unlock_scene();return -1;}
    EventAudioHandle *h=p->handles+slot;audio_channel_release(h);memset(h,0,sizeof *h);
    unsigned i=0;while(i<p->count && (p->cues[i].kind!=kind || p->cues[i].bank!=bank || p->cues[i].reference!=reference))i++;
    if(i==p->count){unlock_scene();return -2;}
    const EventAudioCue *c=p->cues+i;h->cue=c;
    int slot_id=audio_channel_acquire_class(h,c->channel,source_audio_protected(c->logical_bank,c->program));
    if(slot_id<0){h->finished=1;unlock_scene();return 0;}
    AudioChannelBackend *b=audio_backends+slot_id;b->finished=&h->finished;
    int error=0;
    if(c->backend==0) {
        OsbSample *sample=p->samples[i];b->voice=&h->voice;b->header=sample->header;b->level=&h->level;b->total=&h->total;
        audio_osb_start_header(&h->voice,&h->envelope,&h->level,&h->total,sample->header,&sample->program);
        audio_channel_levels(&h->voice,127,0);
    } else {
        unsigned bank=c->patch_bank-1;
        if(slot_id<8 || slot_id>15){audio_channel_release(h);unlock_scene();return -3;}
        Smsb *seq=p->sequencers[slot_id-8];h->sequence=seq;
        error=smsb_bind_parts(seq,p->sequence[bank],p->sequence_size[bank],p->patch+bank);
        if(!error){seq->patch_bank=c->patch_bank;seq->allowed_programs=1u<<c->program;error=smsb_start(seq,0,c->program);}
        b->sequence=seq;b->stream=0;
        if(error)audio_channel_release(h);
    }
    unlock_scene();return error;
}
const EventAudioCue *audio_event_cue(unsigned slot)
{
    return event_audio && slot<EVENT_AUDIO_HANDLES ? event_audio->handles[slot].cue:NULL;
}
void audio_event_levels(unsigned slot,int volume,int pan)
{
    if(slot>=EVENT_AUDIO_HANDLES)return;
    lock_scene();if(event_audio) {
        EventAudioHandle *h=event_audio->handles+slot;
        if(h->cue && !h->finished) {
            if(h->sequence){smsb_volume(h->sequence,0,volume);smsb_pan(h->sequence,0,pan);}
            else audio_channel_levels(&h->voice,volume,pan);
        }
    }unlock_scene();
}
