#ifndef JGR_HUD_TEXT_H
#define JGR_HUD_TEXT_H
#if JGR_PORTABLE_ASSETS
#include "source_data_ptr_hud_text.inc"
#else
#include "generated/source_data/hud_text.inc"
#endif
/* 02f394. Space for signed 32-bit input, ten full-width digits and NUL. */
static inline void hud_number(char out[24],int value)
{
    unsigned n=value<0 ? 0u-(unsigned)value:(unsigned)value;unsigned char rev[10];int k=0,at=0;
    do{rev[k++]=n%10;n/=10;}while(n);
    if(value<0){out[at++]=HUD_DIGITS[10][0];out[at++]=HUD_DIGITS[10][1];}
    while(k){unsigned d=rev[--k];out[at++]=HUD_DIGITS[d][0];out[at++]=HUD_DIGITS[d][1];}out[at]=0;
}
#endif
