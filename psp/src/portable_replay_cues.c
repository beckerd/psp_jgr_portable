#include "portable_replay_cues.h"
int portable_replay_cues_observe(PortableReplayVM *v,void *context)
{
    unsigned kind;
    /* Function identities describe the four original initializer APIs. */
    switch(v->pc) {
    case 0x8c05d288u:kind=2;break;
    case 0x8c05d2c0u:kind=1;break;
    case 0x8c05d2f8u:kind=3;break;
    case 0x8c05d330u:kind=4;break;
    default:return 0;
    }
    PortableReplayCues *c=context;uint32_t parent,tick;
    if(!c || !c->events || c->count>=c->capacity ||
       !portable_replay_vm_read(v,v->r[4]+0x18,4,&parent) ||
       !portable_replay_vm_read(v,parent+0x98,4,&tick))return -1;
    PortableReplayCue *e=c->events+c->count++;
    e->tick=tick;e->kind=kind;e->bank=v->r[5]>>16;e->program=v->r[5]&65535;
    e->parent=parent;e->task=v->r[4];
    return 0; /* Preserve PC/registers; execute original initializer. */
}
