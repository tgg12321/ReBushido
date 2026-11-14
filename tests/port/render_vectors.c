/* The renderer's C on the probe's made-up meshes. */
#include "src/main/math_asm.c"
#include "src/main/math_sin.c"
#include "src/main/render_asm.c"
#include "src/main/render_skin_asm.c"
#include "src/main/render.c"
#include "play/probe/cases.h"
#include "runtime/runtime.h"

/* What the renderer's routines reach that is not in the files above. */
u32 *g_disp_prim_cursor;
u32 *g_render_tiny_ot;
LightSetup g_light_setups[LIGHT_SETUP_COUNT];
s32 g_render_outline_color_set; /* how often the colour was set */

/* The game's own function is in model.c with the models; this is the
 * line the port has there. */
void render_SetOutlineColor(s32 color) {
    g_render_outline_color = color & RENDER_OUTLINE_COLOR_MASK;
    g_render_outline_color_set++;
}

/* sys_Trap's end of the line; no case runs it. */
void port_Trap(void){}

/* How this program calls the routines (render_cases.h): the C keeps the
 * mesh cursor, the node and the handler table in g_render_walk. */
#define PROBE_HOST 1
#define PROBE_WALK_IN()                                                        \
    (g_render_walk.mesh = g_probe_walk.mesh,                                   \
     g_render_walk.node = (SceneNode *)g_probe_walk.node,                      \
     g_render_walk.handlers = (void (**)())g_probe_walk.handlers)
#define PROBE_WALK_OUT() (g_probe_walk.mesh = g_render_walk.mesh)
#define PROBE_HANDLER(routine, count)                                          \
    (PROBE_WALK_IN(), (routine)(count), PROBE_WALK_OUT())
#define PROBE_PROJECT(routine, count, points, depths)                          \
    (PROBE_WALK_IN(), routine(count, points, depths), PROBE_WALK_OUT())
#define PROBE_DRAW_GROUPS()                                                    \
    (PROBE_WALK_IN(), render_DrawGroups(), PROBE_WALK_OUT())
#define PROBE_DRAW_SKINNED_MESH(matrices, mesh, view, depth_shift)             \
    (render_DrawSkinnedMesh(                                                   \
         (s32)(matrices), (s32)(mesh), (s32)(view), (s32)(depth_shift)),       \
     PROBE_WALK_OUT())

#include "play/probe/render_cases.h"

#define TEST_OUT_WORDS (PROBE_RENDER_RESULTS + PROBE_REGISTERS)

/* One case, as the probe runs it on the emulator
 * (tests/play/probe/probe.c): the registers are loaded, the memory is
 * made up and the routine called, and `out` gets what it left and then
 * all 64 registers. */
static void test_RunCase(s32 routine, s32 vector, u32 *out) {
    u32 in[PROBE_REGISTERS];
    s32 i;

    probe_MakeRenderInput(routine, vector, in);
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
    probe_RunRender(routine, vector, out);
    for (i = 0; i < 32; i++) {
        out[PROBE_RENDER_RESULTS + i] = port_GteReadData(i);
        out[PROBE_RENDER_RESULTS + 32 + i] = port_GteReadControl(i);
    }
}

/* With no argument: a line for each routine, its place in the list and
 * one number for all its cases, which the Python side holds to the
 * numbers written down when the emulator last agreed.
 *
 * `table`: the header the probe is built with, a number for every case.
 *
 * `case ROUTINE VECTOR`: what that case left and the registers after it;
 * `dump ROUTINE VECTOR`: and every word of the arena and the scratchpad
 * after it, for looking into a case the emulator made something else
 * of. */
void test_Main(void) {
    static u32 hashes[PROBE_RENDER_VECTORS];
    u32 out[TEST_OUT_WORDS];
    s32 table = test_Arg(0) && test_StringsEqual(test_Arg(0), "table");
    s32 dump = test_Arg(0) && test_StringsEqual(test_Arg(0), "dump");
    s32 routine;
    s32 vector;
    s32 i;

    test_AllocAt(SCRATCHPAD, 0x1000);
    test_AllocAt(PROBE_ARENA, PROBE_ARENA_WORDS * 4);

    if (dump || (test_Arg(0) && test_StringsEqual(test_Arg(0), "case"))) {
        routine = test_ParseInt(test_Arg(1));
        vector = test_ParseInt(test_Arg(2));
        test_RunCase(routine, vector, out);
        test_Print("routine %d vector %d\n", routine, vector);
        for (i = 0; i < PROBE_RENDER_RESULTS; i++) {
            test_Print("result %2d  out %08X\n", i, out[i]);
        }
        for (i = 0; i < 32; i++) {
            test_Print("data %2d  in %08X  out %08X    control %2d  in %08X  "
                       "out %08X\n",
                       i, 0, out[PROBE_RENDER_RESULTS + i], i, 0,
                       out[PROBE_RENDER_RESULTS + 32 + i]);
        }
        if (dump) {
            for (i = 0; i < PROBE_ARENA_WORDS; i++) {
                test_Print("arena %08X\n", ((u32 *)PROBE_ARENA)[i]);
            }
            for (i = 0; i < PROBE_SCRATCH_WORDS; i++) {
                test_Print("scratch %08X\n", PROBE_SCRATCH[i]);
            }
        }
        CHECK(routine < PROBE_RENDER_COUNT);
        return;
    }

    if (table) {
        test_Print("static const u32 g_probe_render_expected[] = {\n");
    }
    for (routine = 0; routine < PROBE_RENDER_COUNT; routine++) {
        for (vector = 0; vector < PROBE_RENDER_VECTORS; vector++) {
            test_RunCase(routine, vector, out);
            hashes[vector] = probe_Hash(out, TEST_OUT_WORDS);
            if (table) {
                test_Print("0x%08X,\n", hashes[vector]);
            }
        }
        if (!table) {
            test_Print(
                "%d %08X\n", routine, probe_Hash(hashes, PROBE_RENDER_VECTORS));
        }
    }
    if (table) {
        test_Print("};\n");
    }
    CHECK_EQ(routine, PROBE_RENDER_COUNT);
}
