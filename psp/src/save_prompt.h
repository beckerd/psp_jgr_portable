/* 0f40b8 / 0f40f0 / 0f40cc: task a00, requested main mode 15.
   This is SAVE?, not the garage record browser. */
#ifndef JGR_SAVE_PROMPT_H
#define JGR_SAVE_PROMPT_H
typedef struct {int yes,done;float previous_axis;} SavePrompt;
static inline void save_prompt_init(SavePrompt *s)
{s->yes=1;s->done=0;s->previous_axis=0;}
/* DC A or START accepts. B does nothing. Digital directions both toggle.
   Source analog comparison uses positive axis change > .5, not repeat. */
static inline int save_prompt_step(SavePrompt *s,unsigned pressed,unsigned analog_mask,float axis)
{
    if(!s->done) {
        if(pressed&12)s->done=1;
        else if((pressed&0xc0) || (s->previous_axis+.5f<axis && (analog_mask&12)))s->yes=1-s->yes;
    }
    s->previous_axis=axis;
    return s->done ? (s->yes ? 12:5):0;
}
#endif
