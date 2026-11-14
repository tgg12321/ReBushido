/* The port's GTE (src/port/gte.c) on the probe's made-up cases. */
#include "src/port/gte.c"
#include "play/probe/cases.h"
#include "runtime/runtime.h"

/* One case on the model, as the probe runs it on a coprocessor
 * (tests/play/probe/start.s): the control registers are loaded, then the
 * data registers, the operation runs, and all 64 are read back. */
static void test_RunCase(s32 command, s32 vector, u32 *out) {
    u32 in[PROBE_REGISTERS];
    s32 i;

    probe_MakeInput(command, vector, in);
    for (i = 0; i < 32; i++) {
        port_GteWriteControl(i, in[PROBE_CONTROL + i]);
    }
    if (!PROBE_LOADS_BY_NUMBER(vector)) {
        port_GteWriteData(PORT_GTE_IRGB, in[PROBE_DATA + PORT_GTE_IRGB]);
        port_GteWriteData(PORT_GTE_SXYP, in[PROBE_DATA + PORT_GTE_SXYP]);
    }
    for (i = 0; i < 32; i++) {
        if (PROBE_LOADS_BY_NUMBER(vector) ||
            (i != PORT_GTE_IRGB && i != PORT_GTE_SXYP)) {
            port_GteWriteData(i, in[PROBE_DATA + i]);
        }
    }
    if (g_probe_commands[command] != 0) {
        port_GteCommand(g_probe_commands[command]);
    }
    for (i = 0; i < 32; i++) {
        out[i] = port_GteReadData(i);
        out[32 + i] = port_GteReadControl(i);
    }
}

/* With no argument: a line for each operation, its instruction word and
 * one number for all its cases, which the Python side holds to the
 * numbers written down when the emulator last agreed.
 *
 * `table`: the header the probe is built with, a number for every case.
 *
 * `case WORD VECTOR`: that case's registers before and after, for looking
 * into a case the emulator made something else of. */
void test_Main(void) {
    static u32 hashes[PROBE_VECTORS];
    u32 out[PROBE_REGISTERS];
    s32 table = test_Arg(0) && test_StringsEqual(test_Arg(0), "table");
    s32 command;
    s32 vector;
    s32 i;

    if (test_Arg(0) && test_StringsEqual(test_Arg(0), "case")) {
        u32 in[PROBE_REGISTERS];

        command = test_ParseInt(test_Arg(1));
        vector = test_ParseInt(test_Arg(2));
        probe_MakeInput(command, vector, in);
        test_RunCase(command, vector, out);
        test_Print(
            "command %08X vector %d\n", g_probe_commands[command], vector);
        for (i = 0; i < 32; i++) {
            test_Print("data %2d  in %08X  out %08X    control %2d  in %08X  "
                       "out %08X\n",
                       i, in[PROBE_DATA + i], out[i], i, in[PROBE_CONTROL + i],
                       out[32 + i]);
        }
        CHECK(command < PROBE_COMMAND_COUNT);
        return;
    }

    if (table) {
        test_Print("static const u32 g_probe_expected[] = {\n");
    }
    for (command = 0; command < PROBE_COMMAND_COUNT; command++) {
        for (vector = 0; vector < PROBE_VECTORS; vector++) {
            test_RunCase(command, vector, out);
            hashes[vector] = probe_Hash(out, PROBE_REGISTERS);
            if (table) {
                test_Print("0x%08X,\n", hashes[vector]);
            }
        }
        if (!table) {
            test_Print("%08X %08X\n", g_probe_commands[command],
                       probe_Hash(hashes, PROBE_VECTORS));
        }
    }
    if (table) {
        test_Print("};\n");
    }
    CHECK_EQ(command, PROBE_COMMAND_COUNT);
}
