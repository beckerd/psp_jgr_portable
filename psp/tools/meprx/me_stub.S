/* SPDX-License-Identifier: MIT
 * Boot setup adapted from psp-media-engine-custom-core v2.0.1:
 * Copyright (c) 2025 m-c/d
 * https://github.com/mcidclan/psp-media-engine-custom-core/tree/a49526f3821f0c85c089a38a61e81ff3a453174d
 * Sources: meLibHandler in me-core.h, meLibUnlockMemory in me-core-custom.h,
 * and the register definitions in hw-registers.h. Full MIT terms are in
 * tools/portable/notices/libmecore-2.0.1.txt.
 *
 * This port runs a standalone ME CPU worker: no firmware function mappings,
 * VME setup or user-register access is needed. Use the first 2 MiB of local
 * memory for the stack on every model. main.c copies this position-independent
 * code to 0xbfc00040 and supplies entry/argument words at 0xbfc00600/604.
 *
 * Cache initialization is written here from the PSP cache-operation notes:
 * https://forums.ps2dev.org/viewtopic.php?t=7426
 * These identify I/D index-store-tag operations 0x01/0x11, 128 indices and
 * 64-byte lines. Clear both tag registers and visit every index while still
 * executing uncached reset code. These are PSP-specific operation numbers.
 */
    .set noreorder
    .set noat
    .text
    .globl me_stub
    .globl me_stub_end
    .ent me_stub
me_stub:
    /* MIT handler's bus, Tachyon and NMI setup; retain other config bits. */
    li      $t0, 0xbc100000
    li      $t1, 0x0f
    sw      $t1, 0x50($t0)
    lw      $t1, 0x40($t0)
    ori     $t1, $t1, 2
    sw      $t1, 0x40($t0)
    li      $t1, -1
    sw      $t1, 0x04($t0)
    sync

    /* Grant the worker access through the memory-protection registers. */
    li      $t0, 0xbc000000
    li      $t2, 0xbc000030
.Lmemory_access:
    sw      $t1, 0($t0)
    addiu   $t0, $t0, 4
    bne     $t0, $t2, .Lmemory_access
    nop
    sync

    /* MIT handler's Status: CU0/CU1 enabled, interrupts/EXL/ERL disabled.
     * This polling worker also clears the writable software-pending bits. */
    li      $t0, 0x30000000
    mtc0    $t0, $12
    mtc0    $zero, $13
    sync
    li      $sp, 0x80200000

    mtc0    $zero, $28
    mtc0    $zero, $29
    li      $t0, 0x80000000
    li      $t1, 0x80002000
.Lcache_tags:
    cache   0x01, 0($t0)
    cache   0x11, 0($t0)
    addiu   $t0, $t0, 64
    bne     $t0, $t1, .Lcache_tags
    nop
    sync

    /* Port-owned handoff: no firmware lookup and no link-time addresses. */
    li      $t0, 0xbfc00600
    lw      $t9, 0($t0)
    lw      $a0, 4($t0)
    jr      $t9
    nop
me_stub_end:
    .end me_stub
    .size me_stub, me_stub_end-me_stub
    .if (me_stub_end-me_stub) > (0x600-0x40)
    .error "ME bootstrap overlaps its entry/argument words"
    .endif
