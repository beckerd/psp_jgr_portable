/* Called under scene_lock, returning under that lock. The worker never sees
   mutable game state. Main-thread controls only affect the next buffer. */
static void audio_commit_osb(AudioJobOsb *before,AudioJobOsb *after,
                             OsbVoice *v,SmsbEnvelope *e,unsigned index)
{
    if(!audio_job_voice_changed[index] && !memcmp(v,&before->voice,sizeof *v)) {
        *v=after->voice;
        if(!memcmp(e,&before->envelope,sizeof *e))*e=after->envelope;
    }
}
static int audio_finish_me(short *out,unsigned count,const AudioOsbMix *mix,unsigned voices)
{
    Smsb *seqs[12]={&final_sequence,&world_sequence,&menu_sequence,&enemy_sequence};
    unsigned nseq=4,needed=0;
    if(event_audio)for(unsigned i=0;i<8;i++)seqs[nseq++]=event_audio->sequencers[i];
    for(unsigned i=0;i<nseq;i++)needed+=seqs[i]->sequence!=NULL;
    if(!audio_me_reserve_mix(needed))return 0;
    AudioMixJob *j=audio_me_mix_buffer();
    memcpy(audio_job_simple_epoch,audio_simple_epoch,sizeof audio_simple_epoch);
    AudioJobState *s=&j->input;
    Smsb *sequence_input=s->sequences;memset(s,0,sizeof *s);s->sequences=sequence_input;
    memset(audio_job_voice,0,sizeof audio_job_voice);
    memset(audio_job_voice_changed,0,sizeof audio_job_voice_changed);
    memset(audio_job_sequence,0,sizeof audio_job_sequence);
    memset(audio_job_sequence_changed,0,sizeof audio_job_sequence_changed);
    memset(audio_seq_controls,0,sizeof audio_seq_controls);
    s->enabled=reverb_enabled;s->mono=output_mono;s->paused=music_paused;
    s->music_gain=music_gain;s->speech_volume=speech_volume;
    s->osb_count=voices;
    for(unsigned i=0;i<voices;i++) {
        s->osb[i]=(AudioJobOsb){*mix[i].voice,*mix[i].envelope,mix[i].level,mix[i].total};
        audio_job_voice[i]=mix[i].voice;
    }
    s->garage=(AudioJobOsb){garage_voice,garage_envelope,garage_level,garage_total};
    audio_job_voice[AUDIO_JOB_OSB_MAX]=&garage_voice;
    /* Unbound sequencers have no sample-clock work. Avoid copying 12 large
       voice arrays in ordinary levels with only world/menu banks loaded. */
    for(unsigned i=0;i<nseq;i++)if(seqs[i]->sequence) {
        unsigned slot=s->sequence_count++;s->sequences[slot]=*seqs[i];audio_job_sequence[slot]=seqs[i];
    }
    s->movie=movie_voice;s->countdown=countdown_audio.playing;s->message=message_playing;
    if(message_playing_image)memcpy(s->message_chunks,message_playing_image->chunk,sizeof s->message_chunks);
    s->final_pcm=final_pcm;s->final_position=final_position;s->final_samples=final_samples;s->final_active=final_active;
    memcpy(s->final_gain,final_gain,sizeof s->final_gain);
    memcpy(s->sfx,voice,sizeof voice);memcpy(s->sfx_pcm,sfx_pcm,sizeof s->sfx_pcm);memcpy(s->sfx_len,sfx_len,sizeof s->sfx_len);
    memset(j->music,0,count*sizeof *j->music);
    if(!music_paused)for(unsigned i=0;i<count;i++)if(stream_active()) {
        short *sample=j->music[i];int fetched=stream_fetch(sample,sample+1);
        if(music_gain>0.f) {
            if(!fetched)music_underruns++;
            else {music_frames++;music_nonzero+=sample[0]!=0 || sample[1]!=0;
                unsigned a=sample[0]<0 ? -(int)sample[0]:sample[0],b=sample[1]<0 ? -(int)sample[1]:sample[1];
                if(a>music_input_peak)music_input_peak=a;
                if(b>music_input_peak)music_input_peak=b;}
        }
    }
    audio_decode_take(&j->music_decode);
    memcpy(j->music_decode_history,j->music_decode.history,sizeof j->music_decode_history);
    unsigned reset_serial=reverb_reset_request;
    int reset=reset_serial!=reverb_reset_seen;
    audio_memory_borrowed=audio_mix_pending=1;
    unsigned started=JGR_PROFILE_CLOCK();
    unlock_scene();
    int result=audio_me_mix(count,reset);
    unsigned done=JGR_PROFILE_CLOCK();
    lock_scene();
    if(result>=0) {
        audio_decode_retire(&j->music_decode);
        AudioJobState *a=&j->output;
        for(unsigned i=0;i<voices;i++)audio_commit_osb(s->osb+i,a->osb+i,mix[i].voice,mix[i].envelope,i);
        audio_commit_osb(&s->garage,&a->garage,&garage_voice,&garage_envelope,AUDIO_JOB_OSB_MAX);
        for(unsigned i=0;i<s->sequence_count;i++) {
            Smsb *live=audio_job_sequence[i];
            if(audio_job_sequence_changed[i] || live->sequence!=s->sequences[i].sequence || live->patch!=s->sequences[i].patch)continue;
            *live=a->sequences[i];
            for(unsigned slot=0;slot<2;slot++)for(unsigned pan=0;pan<2;pan++) {
                if(!audio_seq_controls[i][slot][pan].set)continue;
                int value=audio_seq_controls[i][slot][pan].value,duration=audio_seq_controls[i][slot][pan].duration;
                if(pan)smsb_fade_pan(live,slot,value,duration);else smsb_fade_volume(live,slot,value,duration);
            }
        }
#define COMMIT_MOVIE(live,field,index) if(audio_job_simple_epoch[index]==audio_simple_epoch[index] && !memcmp(&(live),&s->field,sizeof(MovieVoice)))(live)=a->field
        COMMIT_MOVIE(movie_voice,movie,0);COMMIT_MOVIE(countdown_audio.playing,countdown,1);COMMIT_MOVIE(message_playing,message,2);
#undef COMMIT_MOVIE
        if(audio_job_simple_epoch[3]==audio_simple_epoch[3] && final_pcm==s->final_pcm && final_position==s->final_position && final_active==s->final_active){final_position=a->final_position;final_active=a->final_active;}
        for(unsigned i=0;i<NVOICE;i++)if(audio_job_simple_epoch[4+i]==audio_simple_epoch[4+i] && !memcmp(voice+i,s->sfx+i,sizeof *voice))memcpy(voice+i,a->sfx+i,sizeof *voice);
        memcpy(out,j->pcm,count*2*sizeof *out);reverb_reset_seen=reset_serial;
    } else memset(out,0,count*2*sizeof *out);
    /* A failed reset leaves the complete transaction and ALL old banks
       quarantined. Neither next callback nor a scene free can reuse them. */
    if(!audio_me_quarantined()){audio_mix_pending=0;audio_memory_release();}
    if(!audio_suspended && !audio_me_owns_state())audio_me_release_idle_mix();
#ifdef __PSP__
    if(mixer_detail_active)mixer_profile.worker_us+=done-started;
#else
    (void)started;(void)done;
#endif
    return 1;
}
