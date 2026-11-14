/* The GTE probe (tests/play/README.md, "The GTE probe"): a PlayStation
 * program of the tests' own, no part of the game. It runs every case of
 * cases.h on the coprocessor of whatever it is run on, which here is the
 * emulator's, and compares the registers it reads back with what the
 * port's GTE in C made of the same case (expected.h, written by the host
 * test when the probe is built). The emulator's script reads the counts
 * below out of memory when g_probe_done is set. */
#include "common.h"
#include "cases.h"

/* What the model made of each case: one number a case
 * (g_probe_expected). */
#include "expected.h"

/* start.s. */
extern void probe_LoadByNumber(const u32 *in);
extern void probe_LoadSplit(const u32 *in);
extern void probe_Store(u32 *out);
extern void probe_RunCommand(s32 index);

/* The operations, three words each: the instruction and a return. */
#define PROBE_COMMAND_STUB(word) "    .word " #word "\n    jr $31\n    nop\n"
__asm__(".section .text\n"
        "    .set noreorder\n"
        "    .globl probe_command_stubs\n"
        "probe_command_stubs:\n" PROBE_COMMANDS(
            PROBE_COMMAND_STUB) "    .set reorder\n");

/* A case whose registers were not the model's: which, and the registers
 * as the coprocessor had them. */
typedef struct {
    u32 command; /* the instruction word */
    u32 vector;
    u32 out[PROBE_REGISTERS];
} ProbeBad;

#define PROBE_BAD_MAX 16

/* What the emulator's script reads. */
u32 g_probe_done = 0;   /* 1 when every case has been run */
u32 g_probe_run = 0;    /* how many were */
u32 g_probe_failed = 0; /* how many differed from the model */
ProbeBad g_probe_bad[PROBE_BAD_MAX] = {{0}}; /* the first of those */

void probe_Main(void) {
    static u32 in[PROBE_REGISTERS];
    static u32 out[PROBE_REGISTERS];
    s32 command;
    s32 vector;
    s32 i;

    for (command = 0; command < PROBE_COMMAND_COUNT; command++) {
        for (vector = 0; vector < PROBE_VECTORS; vector++) {
            probe_MakeInput(command, vector, in);
            if (PROBE_LOADS_BY_NUMBER(vector)) {
                probe_LoadByNumber(in);
            } else {
                probe_LoadSplit(in);
            }
            probe_RunCommand(command);
            probe_Store(out);
            if (probe_Hash(out, PROBE_REGISTERS) !=
                g_probe_expected[command * PROBE_VECTORS + vector]) {
                if (g_probe_failed < PROBE_BAD_MAX) {
                    ProbeBad *bad = &g_probe_bad[g_probe_failed];

                    bad->command = g_probe_commands[command];
                    bad->vector = vector;
                    for (i = 0; i < PROBE_REGISTERS; i++) {
                        bad->out[i] = out[i];
                    }
                }
                g_probe_failed++;
            }
            g_probe_run++;
        }
    }
    g_probe_done = 1;
}
