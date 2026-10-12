#include <stdint.h>
#include "ge_submit.h"
#ifdef __PSP__
#include "ge_list.h"
#include "ge_matrix.h"
void ge_submit_bone(unsigned bone,const float *m)
{ge_matrix_words(gu_list->current,0x2a,bone*12,m);gu_list->current+=13;}
void ge_submit_model(const float *m)
{ge_matrix_words(gu_list->current,0x3a,0,m);gu_list->current+=13;}
#endif
#if defined(__PSP__) || defined(JGR_TEST_GE_SUBMIT)
static unsigned pending,draws,kicks;
static int aux_batched=1;
void ge_submit_aux_mode(int enabled){aux_batched=!!enabled;}
int ge_submit_aux_batched(void){return aux_batched;}
void ge_submit_aux(int primitive,int format,int count,const void *indices,const void *vertices)
{
    if(aux_batched)ge_submit_draw(primitive,format,count,indices,vertices);
    else sceGuDrawArray(primitive,format,count,indices,vertices);
}
void ge_submit_reset(void){pending=0;}
void ge_submit_profile(unsigned out[2])
{out[0]=draws;out[1]=kicks;draws=kicks=0;}
void ge_submit_call(const void *list)
{
    draws++;
#ifndef JGR_IMMEDIATE_GE_SUBMIT
    if(++pending<32) {
        uintptr_t address=(uintptr_t)list;
        sceGuSendCommandi(0x10,(address>>8)&0x000f0000u);
        sceGuSendCommandi(0x0a,address&0x00ffffffu);return;
    }
#endif
    pending=0;kicks++;sceGuCallList(list);
}
void ge_submit_draw(int primitive,int format,int count,const void *indices,const void *vertices)
{
    draws++;
#ifndef JGR_IMMEDIATE_GE_SUBMIT
    /* Same VTYPE/BASE/IADDR/VADDR/PRIM words as this SDK's DrawArray.
       Updating the kernel stall address for each tiny draw is expensive.
       Keep the direct list and CPU/GE overlap, publishing every 32 draws.
       GetMemory and the ordinary SDK draws may publish sooner; Finish
       always publishes the tail before the existing GE fence/list reuse. */
    if(++pending<32) {
        if(format)sceGuSendCommandi(0x12,format);
        if(indices) {
            uintptr_t address=(uintptr_t)indices;
            sceGuSendCommandi(0x10,(address>>8)&0x000f0000u);
            sceGuSendCommandi(0x02,address&0x00ffffffu);
        }
        if(vertices) {
            uintptr_t address=(uintptr_t)vertices;
            sceGuSendCommandi(0x10,(address>>8)&0x000f0000u);
            sceGuSendCommandi(0x01,address&0x00ffffffu);
        }
        sceGuSendCommandi(0x04,((unsigned)primitive<<16)|(unsigned)count);
        return;
    }
#endif
    pending=0;kicks++;
    sceGuDrawArray(primitive,format,count,indices,vertices);
}
#endif
