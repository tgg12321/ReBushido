/* The GTE probe (tests/play/README.md, "The GTE probe"): a PlayStation
 * program of the tests' own, no part of the game. It runs every case of
 * cases.h on the coprocessor of whatever it is run on, which here is the
 * emulator's, and compares the registers it reads back with what the
 * port's GTE in C made of the same case; then every case of routines.h
 * on the game's and Sony's routines that are assembly, which are linked
 * into it as the game has them, and compares what they wrote with what
 * the C the port has in their place wrote; then every case of
 * render_cases.h on the renderer's routines, the same way (expected.h,
 * written by the three host tests when the probe is built). The emulator's
 * script reads the counts below out of memory when g_probe_done is set. */
#include "common.h"
#include "bb2.h"
#include "cases.h"
#include "routines.h"

/* start.s. */
extern u32 probe_CallRegs(u32 *regs, void *routine);

/* How this program calls the renderer's routines (render_cases.h): the
 * listings take the mesh cursor in $s0, the scratchpad in $s1, the node
 * in $s2 and the handler table in $s4, and leave the cursor in $s0. */
enum {
    PROBE_REG_A0,
    PROBE_REG_A1,
    PROBE_REG_A2,
    PROBE_REG_A3,
    PROBE_REG_S0,
    PROBE_REG_S1,
    PROBE_REG_S2,
    PROBE_REG_S3,
    PROBE_REG_S4,
    PROBE_REG_S5,
    PROBE_REG_S6,
    PROBE_REG_S7,
    PROBE_REG_FP,
    PROBE_REG_V0,
    PROBE_REG_COUNT
};

static u32 probe_CallWalk(void *routine, u32 a0, u32 a1, u32 a2, u32 a3);
#define PROBE_HANDLER(routine, count) probe_CallWalk(routine, count, 0, 0, 0)
#define PROBE_PROJECT(routine, count, points, depths)                          \
    probe_CallWalk(routine, count, (u32)(points), (u32)(depths), 0)
#define PROBE_DRAW_GROUPS() probe_CallWalk(render_DrawGroups, 0, 0, 0, 0)
/* The body's walk is called as C calls it, and returns the cursor. */
#define PROBE_DRAW_SKINNED_MESH(bones, start, view, depth_shift)               \
    (g_probe_walk.mesh = (u8 *)probe_CallWalk(                                 \
         render_DrawSkinnedMesh, (u32)(bones), (u32)(start), (u32)(view),      \
         (u32)(depth_shift)))
/* The cloth's walk and the tiles' take their arguments as C functions
 * do, but leave the mesh cursor's register changed, which a C function
 * may not: they are called like the others. */
#define PROBE_DRAW_CLOTH(node, scale_shift)                                    \
    probe_CallWalk(render_DrawCloth, (u32)(node), (u32)(scale_shift), 0, 0)
#define PROBE_DRAW_TILES(queue, tile, view)                                    \
    probe_CallWalk(                                                            \
        render_DrawStageTiles, (u32)(queue), (u32)(tile), (u32)(view), 0)

#include "render_cases.h"

static u32 probe_CallWalk(void *routine, u32 a0, u32 a1, u32 a2, u32 a3) {
    static u32 regs[PROBE_REG_COUNT];
    s32 i;

    for (i = 0; i < PROBE_REG_COUNT; i++) {
        regs[i] = 0x5A5A0000 + i;
    }
    regs[PROBE_REG_A0] = a0;
    regs[PROBE_REG_A1] = a1;
    regs[PROBE_REG_A2] = a2;
    regs[PROBE_REG_A3] = a3;
    regs[PROBE_REG_S0] = (u32)g_probe_walk.mesh;
    regs[PROBE_REG_S1] = SCRATCHPAD;
    regs[PROBE_REG_S2] = (u32)g_probe_walk.node;
    regs[PROBE_REG_S4] = (u32)g_probe_walk.handlers;
    probe_CallRegs(regs, routine);
    g_probe_walk.mesh = (u8 *)regs[PROBE_REG_S0];
    return regs[PROBE_REG_V0];
}

/* The game's function that sets the outline's colour is in model.c with
 * the models, which the probe does not link. This does what it does: the
 * colour goes into the instructions that load it, at the eight places
 * the game's own table names, in turn a pair of each kind. */
void render_SetOutlineColor(s32 color) {
    u32 high = ((u32)color >> 16) & 0xFF;
    u32 low = (u32)color & 0xFFFF;
    s32 i;

    for (i = 0; i < 8; i++) {
        u32 *site = (u32 *)g_render_outline_color_sites[i];

        if (i & 1) {
            site[0] = ((u32)g_render_outline_color_lui2 & ~0xFFU) | high;
            site[1] = ((u32)g_render_outline_color_ori2 & ~0xFFFFU) | low;
        } else {
            site[0] = ((u32)g_render_outline_color_lui & ~0xFFU) | high;
            site[1] = ((u32)g_render_outline_color_ori & ~0xFFFFU) | low;
        }
    }
}

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
/* And one of the renderer's cases: the routine's place in its list. */
#define PROBE_BAD_RENDER 0xB0000000

#define PROBE_BAD_MAX 16

/* What the emulator's script reads. */
u32 g_probe_done = 0;   /* 1 when every case has been run */
u32 g_probe_run = 0;    /* how many were */
u32 g_probe_failed = 0; /* how many differed from the model */
ProbeBad g_probe_bad[PROBE_BAD_MAX] = {{0}}; /* the first of those */
/* The arena and the scratchpad as the first of the renderer's cases that
 * differed left them. */
u32 g_probe_render_dump[PROBE_ARENA_WORDS + PROBE_SCRATCH_WORDS] = {0};
/* How many cases of each of the renderer's routines differed. */
#define PROBE_RENDER_MAX 64
u32 g_probe_render_failed[PROBE_RENDER_MAX] = {0};

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
    s32 render_dumped = FALSE;

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
    /* The renderer: what each routine left in memory, then the
     * registers. */
    for (routine = 0; routine < PROBE_RENDER_COUNT; routine++) {
        for (vector = 0; vector < PROBE_RENDER_VECTORS; vector++) {
            probe_MakeRenderInput(routine, vector, in);
            probe_LoadSplit(in);
            probe_RunRender(routine, vector, out);
            probe_Store(out + PROBE_RENDER_RESULTS);
            if (probe_Hash(out, PROBE_RENDER_RESULTS + PROBE_REGISTERS) !=
                g_probe_render_expected[routine * PROBE_RENDER_VECTORS +
                                        vector]) {
                if (!render_dumped) {
                    s32 i;

                    for (i = 0; i < PROBE_ARENA_WORDS; i++) {
                        g_probe_render_dump[i] = ((u32 *)PROBE_ARENA)[i];
                    }
                    for (i = 0; i < PROBE_SCRATCH_WORDS; i++) {
                        g_probe_render_dump[PROBE_ARENA_WORDS + i] =
                            PROBE_SCRATCH[i];
                    }
                    render_dumped = TRUE;
                }
                g_probe_render_failed[routine]++;
                probe_Differs(PROBE_BAD_RENDER + routine, vector,
                              out + PROBE_RENDER_RESULTS, out);
            }
            g_probe_run++;
        }
    }
    g_probe_done = 1;
}
