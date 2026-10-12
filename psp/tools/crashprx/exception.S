/* Allegrex exception entry, following the PSPSDK exception dispatcher ABI:
 * v0/v1 are preserved in COP0 control registers 4/5. The first two words
 * are reserved for ExceptionManager's handler chain. Capture uses NO stack.
 * Unlike the SDK default trap, our C entry uses a dedicated kernel stack.
 * Reference: pspsdk/src/debug/exception_asm.S (James Forshaw, BSD).
 */
    .set noreorder
    .set noat
    .text
    .align 2
    .global jgr_crash_exception
    .ent jgr_crash_exception
jgr_crash_exception:
    nop
    nop
    la $v0, jgr_crash_latched
    lw $v1, 0($v0)
    bnez $v1, secondary
    nop
    li $v1, 1
    sw $v1, 0($v0)
    la $v0, jgr_crash_regs
    sw $0, 0($v0)
    sw $1, 4($v0)
    sw $4, 16($v0)
    sw $5, 20($v0)
    sw $6, 24($v0)
    sw $7, 28($v0)
    sw $8, 32($v0)
    sw $9, 36($v0)
    sw $10, 40($v0)
    sw $11, 44($v0)
    sw $12, 48($v0)
    sw $13, 52($v0)
    sw $14, 56($v0)
    sw $15, 60($v0)
    sw $16, 64($v0)
    sw $17, 68($v0)
    sw $18, 72($v0)
    sw $19, 76($v0)
    sw $20, 80($v0)
    sw $21, 84($v0)
    sw $22, 88($v0)
    sw $23, 92($v0)
    sw $24, 96($v0)
    sw $25, 100($v0)
    sw $26, 104($v0)
    sw $27, 108($v0)
    sw $28, 112($v0)
    sw $29, 116($v0)
    sw $30, 120($v0)
    sw $31, 124($v0)
    cfc0 $v1, $4
    sw $v1, 8($v0)
    cfc0 $v1, $5
    sw $v1, 12($v0)
    mfc0 $v1, $12
    sw $v1, 128($v0)
    mflo $v1
    sw $v1, 132($v0)
    mfhi $v1
    sw $v1, 136($v0)
    mfc0 $v1, $8
    sw $v1, 140($v0)
    mfc0 $v1, $13
    sw $v1, 144($v0)
    mfc0 $v1, $14
    sw $v1, 148($v0)
    lw $v1, 128($v0)
    lui $a0, 0x2000
    and $a0, $a0, $v1
    beqz $a0, no_fpu
    nop
    swc1 $f0, 152($v0)
    swc1 $f1, 156($v0)
    swc1 $f2, 160($v0)
    swc1 $f3, 164($v0)
    swc1 $f4, 168($v0)
    swc1 $f5, 172($v0)
    swc1 $f6, 176($v0)
    swc1 $f7, 180($v0)
    swc1 $f8, 184($v0)
    swc1 $f9, 188($v0)
    swc1 $f10, 192($v0)
    swc1 $f11, 196($v0)
    swc1 $f12, 200($v0)
    swc1 $f13, 204($v0)
    swc1 $f14, 208($v0)
    swc1 $f15, 212($v0)
    swc1 $f16, 216($v0)
    swc1 $f17, 220($v0)
    swc1 $f18, 224($v0)
    swc1 $f19, 228($v0)
    swc1 $f20, 232($v0)
    swc1 $f21, 236($v0)
    swc1 $f22, 240($v0)
    swc1 $f23, 244($v0)
    swc1 $f24, 248($v0)
    swc1 $f25, 252($v0)
    swc1 $f26, 256($v0)
    swc1 $f27, 260($v0)
    swc1 $f28, 264($v0)
    swc1 $f29, 268($v0)
    swc1 $f30, 272($v0)
    swc1 $f31, 276($v0)
    cfc1 $v1, $31
    sw $v1, 280($v0)
    cfc1 $v1, $0
    sw $v1, 284($v0)
    ctc1 $zero, $31
no_fpu:
    la $sp, jgr_crash_stack + 4096 - 16
    la $gp, _gp
    move $k1, $zero
    la $v0, jgr_crash_trap
    b enter_trap
    nop
secondary:
    /* Keep the first record intact. A second fault cannot reuse its stack.
       Interrupts remain enabled so the already-woken reporter can run. */
    la $v0, secondary_park
enter_trap:
    mtc0 $v0, $14
    mfc0 $v1, $12
    li $v0, 0xffffffe3
    and $v1, $v1, $v0
    ori $v1, $v1, 3
    mtc0 $v1, $12
    sync
    nop
    eret
    nop
secondary_park:
    b secondary_park
    nop
    .end jgr_crash_exception
