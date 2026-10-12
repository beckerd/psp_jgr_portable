#ifndef JGR_PORTABLE_REPLAY_VM_H
#define JGR_PORTABLE_REPLAY_VM_H

#include <stddef.h>
#include <stdint.h>

/* A bounded extraction interpreter, not a complete Dreamcast emulator.
 * Instructions and game data must come from the user's original files.
 * Unsupported instructions, devices and mappings fail closed. */
enum { PRVM_READ=1, PRVM_WRITE=2, PRVM_EXEC=4, PRVM_MAX_REGIONS=24 };
enum {
    PRVM_OK=0, PRVM_MEMORY=1, PRVM_ALIGNMENT=2, PRVM_OPCODE=3,
    PRVM_DELAY_SLOT=4, PRVM_BUDGET=5, PRVM_FPU_MODE=6, PRVM_HOOK=7
};
typedef struct {
    uint32_t address, bytes, permissions;
    unsigned char *data;
} PortableReplayRegion;
typedef struct PortableReplayVM PortableReplayVM;
/* Called at instruction boundaries, never in a delay slot. Return 0 to
 * execute normally, 1 after supplying an explicit service, -1 on failure. */
typedef int (*PortableReplayHook)(PortableReplayVM *, void *);
/* Explicit device service: 0 means ordinary mapped memory, 1 supplies a
 * value, -1 rejects the access. No default values for missing devices. */
typedef int (*PortableReplayReadHook)(PortableReplayVM *,uint32_t,unsigned,uint32_t *,void *);
typedef int (*PortableReplayWriteHook)(PortableReplayVM *,uint32_t,unsigned,uint32_t,void *);
struct PortableReplayVM {
    uint32_t r[16], fr[16], xf[16], pc, pr, sr, fpul, fpscr, mach, macl;
    uint32_t bank[8], gbr, vbr, ssr, spc, sgr, dbr;
    uint32_t delay, delayed, fault, fault_pc, fault_address, fault_opcode;
    uint64_t instructions;
    unsigned nregions;
    PortableReplayRegion region[PRVM_MAX_REGIONS];
    /* Explicit ordinary RAM fast path; never used for device-backed ranges. */
    PortableReplayRegion *direct_memory;
    PortableReplayHook hook;
    void *context;
    PortableReplayReadHook read_hook;
    void *read_context;
    PortableReplayWriteHook write_hook;
    void *write_context;
    int (*store_queue_hook)(PortableReplayVM *,uint32_t,void *);
    void *store_queue_context;
    /* Explicit bulk write-only presentation sink. Return 1 only for a range
     * already discarded by ordinary writes; 0 leaves VM/device state intact. */
    int (*copy_output_hook)(PortableReplayVM *,uint32_t,unsigned,void *);
    void *copy_output_context;
    /* Explicit external CPU hardware-model table, not an embedded game asset.
     * Required only when the original program executes FSCA. */
    const uint32_t *fsca_positive;
    unsigned fsca_words;
};
void portable_replay_vm_init(PortableReplayVM *vm);
int portable_replay_vm_map(PortableReplayVM *vm,uint32_t address,void *data,
                           uint32_t bytes,unsigned permissions);
/* Bypass device hooks only for the already mapped region at this address. */
int portable_replay_vm_direct_memory(PortableReplayVM *vm,uint32_t address);
/* Decode and batch an ordinary-RAM copy loop from the supplied program.
 * No instructions, device events, registers or emulated time are discarded.
 * Returns the number of instructions executed, bounded by the next service
 * deadline supplied by the caller. Unsupported loops/accesses are unchanged. */
unsigned portable_replay_vm_copy_loop(PortableReplayVM *vm,unsigned budget);
/* The same bounded optimization for a decoded two-word table-fill loop. */
unsigned portable_replay_vm_fill_loop(PortableReplayVM *vm,unsigned budget);
int portable_replay_vm_read(PortableReplayVM *vm,uint32_t address,unsigned size,uint32_t *value);
int portable_replay_vm_write(PortableReplayVM *vm,uint32_t address,unsigned size,uint32_t value);
int portable_replay_vm_interrupt(PortableReplayVM *vm,unsigned priority);
int portable_replay_vm_run(PortableReplayVM *vm,uint32_t entry,uint32_t return_pc,uint32_t budget);
/* Resume an instruction-budget yield without changing PR or a delay slot. */
int portable_replay_vm_continue(PortableReplayVM *vm,uint32_t return_pc,uint32_t budget);
const char *portable_replay_vm_fault_name(unsigned fault);
#endif
