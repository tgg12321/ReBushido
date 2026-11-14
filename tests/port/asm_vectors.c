/* The C beside the assembly on the probe's made-up cases. */
#include "src/main/math_asm.c"
#include "src/main/math_sin.c"
#include "src/main/render_asm.c"
#include "play/probe/cases.h"
#include "play/probe/routines.h"
#include "runtime/runtime.h"

/* sys_Trap's end of the line; no case runs it. */
void port_Trap(void) {}

/* One case, as the probe runs it on the emulator
 * (tests/play/probe/probe.c): the registers are loaded, the routine is
 * called, and `out` gets what it wrote and then all 64 registers. */
static void test_RunCase(s32 routine, s32 vector, u32 *out) {
    u32 in[PROBE_REGISTERS];
    s32 i;

    probe_MakeRoutineInput(routine, vector, in);
    for (i = 0; i < 32; i++) {
        port_GteWriteControl(i, in[PROBE_CONTROL + i]);
    }
    port_GteWriteData(PORT_GTE_IRGB, in[PROBE_DATA + PORT_GTE_IRGB]);
    port_GteWriteData(PORT_GTE_SXYP, in[PROBE_DATA + PORT_GTE_SXYP]);
    for (i = 0; i < 32; i++) {
        if (i != PORT_GTE_IRGB && i != PORT_GTE_SXYP) {
            port_GteWriteData(i, in[PROBE_DATA + i]);
        }
    }
    probe_RunRoutine(routine, vector, out);
    for (i = 0; i < 32; i++) {
        out[PROBE_RESULT_WORDS + i] = port_GteReadData(i);
        out[PROBE_RESULT_WORDS + 32 + i] = port_GteReadControl(i);
    }
}

/* With no argument: a line for each routine, its place in the list and
 * one number for all its cases, which the Python side holds to the
 * numbers written down when the emulator last agreed.
 *
 * `table`: the header the probe is built with, a number for every case.
 *
 * `case ROUTINE VECTOR`: what that case wrote and the registers after
 * it, for looking into a case the emulator made something else of. */
void test_Main(void) {
    static u32 hashes[PROBE_ROUTINE_VECTORS];
    u32 out[PROBE_RESULT_WORDS + PROBE_REGISTERS];
    s32 table = test_Arg(0) && test_StringsEqual(test_Arg(0), "table");
    s32 routine;
    s32 vector;
    s32 i;

    if (test_Arg(0) && test_StringsEqual(test_Arg(0), "case")) {
        routine = test_ParseInt(test_Arg(1));
        vector = test_ParseInt(test_Arg(2));
        test_RunCase(routine, vector, out);
        test_Print("routine %d vector %d\n", routine, vector);
        for (i = 0; i < PROBE_RESULT_WORDS; i++) {
            test_Print("result %2d  out %08X\n", i, out[i]);
        }
        for (i = 0; i < 32; i++) {
            test_Print("data %2d  in %08X  out %08X    control %2d  in %08X  "
                       "out %08X\n",
                       i, 0, out[PROBE_RESULT_WORDS + i], i, 0,
                       out[PROBE_RESULT_WORDS + 32 + i]);
        }
        CHECK(routine < PROBE_ROUTINE_COUNT);
        return;
    }

    if (table) {
        test_Print("static const u32 g_probe_routine_expected[] = {\n");
    }
    for (routine = 0; routine < PROBE_ROUTINE_COUNT; routine++) {
        for (vector = 0; vector < PROBE_ROUTINE_VECTORS; vector++) {
            test_RunCase(routine, vector, out);
            hashes[vector] =
                probe_Hash(out, PROBE_RESULT_WORDS + PROBE_REGISTERS);
            if (table) {
                test_Print("0x%08X,\n", hashes[vector]);
            }
        }
        if (!table) {
            test_Print("%d %08X\n", routine,
                       probe_Hash(hashes, PROBE_ROUTINE_VECTORS));
        }
    }
    if (table) {
        test_Print("};\n");
    }
    CHECK_EQ(routine, PROBE_ROUTINE_COUNT);
}
