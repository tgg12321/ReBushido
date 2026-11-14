# The probe's assembly (tests/play/README.md, "The GTE probe"): where the
# program starts, and the three things C cannot say: loading all 64 of the
# GTE's registers, reading them back, and running one operation.

.set noat
.set noreorder

.section .text, "ax"

# Where the program starts. The coprocessor is switched on (bit 30 of the
# status register), then the C side runs and the program idles: the
# emulator's script reads the results out of memory.
.globl _start
.type _start, @function
_start:
    mfc0    $t0, $12
    lui     $t1, 0x4000
    or      $t0, $t0, $t1
    mtc0    $t0, $12
    nop
    nop
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
