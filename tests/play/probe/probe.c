/* The GTE probe (tests/play/README.md, "The GTE probe"): a PlayStation
 * program of the tests' own, no part of the game. It runs every case of
 * cases.h on the coprocessor of whatever it is run on, which here is the
 * emulator's, and compares the registers it reads back with what the
 * port's GTE in C made of the same case; then every case of routines.h
 * on the game's and Sony's routines that are assembly, which are linked
 * into it as the game has them, and compares what they wrote with what
 * the C the port has in their place wrote (expected.h, written by the
 * two host tests when the probe is built). The emulator's script reads
 * the counts below out of memory when g_probe_done is set. */
#include "common.h"
#include "bb2.h"
#include "cases.h"
#include "routines.h"

/* What the model and the port's C made of each case: one number a case
 * (g_probe_expected, g_probe_routine_expected). */
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

/* A case that came out otherwise than on the host: which, the registers
 * as the coprocessor had them, and for a routine what it wrote. */
typedef struct {
    u32 command; /* the instruction word, or PROBE_BAD_ROUTINE + the routine */
    u32 vector;
    u32 out[PROBE_REGISTERS];
    u32 results[PROBE_RESULT_WORDS];
} ProbeBad;

#define PROBE_BAD_ROUTINE 0xA0000000

#define PROBE_BAD_MAX 16

/* What the emulator's script reads. */
u32 g_probe_done = 0;   /* 1 when every case has been run */
u32 g_probe_run = 0;    /* how many were */
u32 g_probe_failed = 0; /* how many differed from the model */
ProbeBad g_probe_bad[PROBE_BAD_MAX] = {{0}}; /* the first of those */

/* Writes a case down that differed, if there is room. */
static void probe_Differs(
    u32 command, s32 vector, const u32 *out, const u32 *results) {
    s32 i;

    if (g_probe_failed < PROBE_BAD_MAX) {
        ProbeBad *bad = &g_probe_bad[g_probe_failed];

        bad->command = command;
        bad->vector = vector;
        for (i = 0; i < PROBE_REGISTERS; i++) {
            bad->out[i] = out[i];
        }
        for (i = 0; results && i < PROBE_RESULT_WORDS; i++) {
            bad->results[i] = results[i];
        }
    }
    g_probe_failed++;
}

void probe_Main(void) {
    static u32 in[PROBE_REGISTERS];
    static u32 out[PROBE_RESULT_WORDS + PROBE_REGISTERS];
    s32 command;
    s32 routine;
    s32 vector;

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
                probe_Differs(g_probe_commands[command], vector, out, NULL);
            }
            g_probe_run++;
        }
    }
    /* The routines: what each wrote, then the registers after it. */
    for (routine = 0; routine < PROBE_ROUTINE_COUNT; routine++) {
        for (vector = 0; vector < PROBE_ROUTINE_VECTORS; vector++) {
            probe_MakeRoutineInput(routine, vector, in);
            probe_LoadSplit(in);
            probe_RunRoutine(routine, vector, out);
            probe_Store(out + PROBE_RESULT_WORDS);
            if (probe_Hash(out, PROBE_RESULT_WORDS + PROBE_REGISTERS) !=
                g_probe_routine_expected[routine * PROBE_ROUTINE_VECTORS +
                                         vector]) {
                probe_Differs(PROBE_BAD_ROUTINE + routine, vector,
                              out + PROBE_RESULT_WORDS, out);
            }
            g_probe_run++;
        }
    }
    g_probe_done = 1;
}
