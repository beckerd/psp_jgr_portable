#ifndef JGR_PORTABLE_REPLAY_GDROM_H
#define JGR_PORTABLE_REPLAY_GDROM_H
#include "portable_disc.h"
#include "portable_replay_vm.h"
typedef struct {
    JgrDisc *disc;
    unsigned char *ram;
    uint32_t ram_bytes;
    uint32_t command,parameter[4],result[4],status,request,next_request;
    uint64_t sectors_read;
    unsigned commands;
} PortableReplayGdrom;
/* BIOS service adapter for bounded cooked-sector reads. The caller owns disc
 * and zero-initialized guest RAM; no original BIOS or initialized game bytes. */
void portable_replay_gdrom_init(PortableReplayGdrom *,JgrDisc *,void *,uint32_t);
/* Executes the r6/r7 GD-ROM BIOS request. 0 means unsupported/malformed. */
int portable_replay_gdrom_service(PortableReplayGdrom *,PortableReplayVM *);
#endif
