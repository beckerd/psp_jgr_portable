/* Payload-free compiler ABI and BSS storage. */
#if JGR_PORTABLE_ASSETS
#include <stdint.h>
#include <stddef.h>
typedef struct {int parent,attribute;float sphere[4],pos[3];int32_t ang[3];float scale[3];} PlayerBodyNode;


#include "source_data_pointers.h"
PlayerBodyNode PLAYER_BODY[16][20] __asm__("jgr_ptr_player_body_PLAYER_BODY") __attribute__((aligned(16)));
static const SourceDataPointerSection sections[]={
    {964264450,64,19200,16,PLAYER_BODY},
};
int source_data_ptr_player_body_load(void){return source_data_pointers_load_file("source_data_ptr_player_body.jsd",sections,1u,19264u,0x6fb94532u,0u);}
#endif
