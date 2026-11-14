# The probe's assembly (tests/play/README.md, "The GTE probe"): where the
# program starts, and the three things C cannot say: loading all 64 of the
# GTE's registers, reading them back, and running one operation.

.set noat
.set noreorder

.section .text, "ax"

# Where the program starts. The coprocessor is switched on (bit 30 of the
# status register) and the global pointer set, which the game's C that is
# linked in reaches its small variables through; then the C side runs and
# the program idles: the emulator's script reads the results out of memory.
.globl _start
.type _start, @function
_start:
    mfc0    $t0, $12
    lui     $t1, 0x4000
    or      $t0, $t0, $t1
    mtc0    $t0, $12
    nop
    nop
    lui     $gp, %hi(_gp)
    addiu   $gp, $gp, %lo(_gp)
    jal     probe_Main
     nop
1:
    j       1b
     nop

# The control registers by number, from the words at $a0.
.macro load_controls
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    lw      $t0, 4*\n($a0)
    nop
    ctc2    $t0, $\n
    .endr
.endm

# One data register, from the word at $a0 + 128 + 4 * n.
.macro load_data n
    lw      $t0, 4*\n+128($a0)
    nop
    mtc2    $t0, $\n
.endm

# void probe_LoadByNumber(const u32 *in): the 32 control registers, then
# the 32 data registers, each in the order of its number.
.globl probe_LoadByNumber
.type probe_LoadByNumber, @function
probe_LoadByNumber:
    load_controls
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    load_data \n
    .endr
    jr      $ra
     nop

# void probe_LoadSplit(const u32 *in): the same, but the two data
# registers that write others, IRGB (28) and SXYP (15), go first.
.globl probe_LoadSplit
.type probe_LoadSplit, @function
probe_LoadSplit:
    load_controls
    load_data 28
    load_data 15
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,16,17,18,19,20,21,22,23,24,25,26,27,29,30,31
    load_data \n
    .endr
    jr      $ra
     nop

# void probe_Store(u32 *out): the 32 data registers, then the 32 control
# registers, read back to the words at $a0.
.globl probe_Store
.type probe_Store, @function
probe_Store:
    .irp n,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31
    mfc2    $t0, $\n
    nop
    sw      $t0, 4*\n($a0)
    cfc2    $t0, $\n
    nop
    sw      $t0, 4*\n+128($a0)
    .endr
    jr      $ra
     nop

# void probe_RunCommand(s32 index): operation `index` of the list. Each
# has three words at probe_command_stubs (probe.c): the operation, and a
# return.
.globl probe_RunCommand
.type probe_RunCommand, @function
probe_RunCommand:
    sll     $t0, $a0, 2
    sll     $t1, $a0, 3
    addu    $t0, $t0, $t1
    lui     $t1, %hi(probe_command_stubs)
    addiu   $t1, $t1, %lo(probe_command_stubs)
    addu    $t0, $t0, $t1
    jr      $t0
     nop

# u32 probe_CallRegs(u32 *regs, void *routine): calls one of the renderer's
# listings, which take what they work on in registers C does not pass
# anything in. `regs` is thirteen words that go into $a0 to $a3, $s0 to $s7
# and $fp before the call; after it $s0 to $s7 and $fp are put back there
# and $v0 is the fourteenth.
.globl probe_CallRegs
.type probe_CallRegs, @function
probe_CallRegs:
    addiu   $sp, $sp, -0x40
    sw      $ra, 0x3C($sp)
    sw      $fp, 0x38($sp)
    sw      $s7, 0x34($sp)
    sw      $s6, 0x30($sp)
    sw      $s5, 0x2C($sp)
    sw      $s4, 0x28($sp)
    sw      $s3, 0x24($sp)
    sw      $s2, 0x20($sp)
    sw      $s1, 0x1C($sp)
    sw      $s0, 0x18($sp)
    sw      $a0, 0x10($sp)
    addu    $t8, $a0, $zero
    addu    $t9, $a1, $zero
    lw      $s0, 16($t8)
    lw      $s1, 20($t8)
    lw      $s2, 24($t8)
    lw      $s3, 28($t8)
    lw      $s4, 32($t8)
    lw      $s5, 36($t8)
    lw      $s6, 40($t8)
    lw      $s7, 44($t8)
    lw      $fp, 48($t8)
    lw      $a0, 0($t8)
    lw      $a1, 4($t8)
    lw      $a2, 8($t8)
    lw      $a3, 12($t8)
    jalr    $t9
     nop
    lw      $t8, 0x10($sp)
    nop
    sw      $s0, 16($t8)
    sw      $s1, 20($t8)
    sw      $s2, 24($t8)
    sw      $s3, 28($t8)
    sw      $s4, 32($t8)
    sw      $s5, 36($t8)
    sw      $s6, 40($t8)
    sw      $s7, 44($t8)
    sw      $fp, 48($t8)
    sw      $v0, 52($t8)
    lw      $ra, 0x3C($sp)
    lw      $fp, 0x38($sp)
    lw      $s7, 0x34($sp)
    lw      $s6, 0x30($sp)
    lw      $s5, 0x2C($sp)
    lw      $s4, 0x28($sp)
    lw      $s3, 0x24($sp)
    lw      $s2, 0x20($sp)
    lw      $s1, 0x1C($sp)
    lw      $s0, 0x18($sp)
    jr      $ra
     addiu  $sp, $sp, 0x40
