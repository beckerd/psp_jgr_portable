#ifndef JGR_RADIO_UI_H
#define JGR_RADIO_UI_H

/* Radio task 0fe75c: fade in, play a single turn of the dial, then show
   the caption. Motion length and all artwork/text come from prepared data. */
enum { RADIO_FADE_IN=1, RADIO_IDLE, RADIO_REVERSE, RADIO_FORWARD,
       RADIO_FADE_OUT, RADIO_DONE };
typedef struct {
    int phase,track,frame,nframes,fade;
    float remainder;
} RadioUi;

static inline void radio_ui_init(RadioUi *r,int nframes)
{
    *r=(RadioUi){.phase=RADIO_FADE_IN,.nframes=nframes>0 ? nframes:1,.fade=25};
}

/* Return the requested stream entry, or -1 when playback is unchanged.
   Start and B both map to back. Left has priority over right, as on DC. */
static inline int radio_ui_input(RadioUi *r,int left,int right,int back)
{
    if(r->phase!=RADIO_IDLE)return -1;
    if(back){r->phase=RADIO_FADE_OUT;return 1;}
    if(left){r->track=(r->track+25)%26;r->phase=RADIO_REVERSE;r->frame=r->nframes-1;}
    else if(right){r->track=(r->track+1)%26;r->phase=RADIO_FORWARD;r->frame=0;}
    else return -1;
    return 0x89+r->track;
}

/* One original 30 Hz tick. The first song starts after the entry fade. */
static inline int radio_ui_tick(RadioUi *r)
{
    if(r->phase==RADIO_FADE_IN){
        if(--r->fade==0){r->phase=RADIO_FORWARD;return 0x89+r->track;}
    }else if(r->phase==RADIO_FORWARD || r->phase==RADIO_REVERSE){
        r->frame+=r->phase==RADIO_FORWARD ? 1:-1;
        if(r->frame<0 || r->frame>=r->nframes){r->frame=0;r->phase=RADIO_IDLE;}
    }else if(r->phase==RADIO_FADE_OUT){
        if(++r->fade==25)r->phase=RADIO_DONE;
    }
    return -1;
}

static inline int radio_ui_update(RadioUi *r,float seconds)
{
    int song=-1;
    r->remainder+=seconds;
    while(r->remainder>=1.f/30.f){
        r->remainder-=1.f/30.f;
        int next=radio_ui_tick(r);if(next>=0)song=next;
    }
    return song;
}
#endif
