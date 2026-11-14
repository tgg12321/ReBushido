/* The size the points and the team scores return (hud.c,
 * RB_FIX_HUD_DRAW_MODE): whether it holds both their drawing modes. */
#include "src/main/hud.c"
#include "src/main/hud_data.c"
#include "src/rebushido/settings.c"
#include "runtime/runtime.h"

/* What the two functions touch of the game, stood in for. */
u32 *g_disp_ot;
GameSettings g_game_settings;
static u32 g_test_ot[4];
/* the drawing modes set, in order, and how many were linked */
static s32 g_test_modes[4];
static s32 g_test_mode_count;
static s32 g_test_modes_linked;

s32 sprite_Draw(SpriteDraw *draw) { return draw->sprt_out + sizeof(SPRT); }
s32 sprite_DrawScaled(SpriteDraw *draw, s32 mode) {
    return draw->ft4_out + sizeof(POLY_FT4);
}
s32 sprite_GetTPage(SpriteSheet *sheet, s32 abr_bits) { return 0; }
void SetTile(TILE *p) {}
void SetPolyFT4(POLY_FT4 *p) {}
void SetSemiTrans(void *p, s32 abe) {}
void SetShadeTex(void *p, s32 tge) {}

void SetDrawMode(DR_MODE *p, s32 dfe, s32 dtd, s32 tpage, RECT *tw) {
    if (g_test_mode_count < 4) {
        g_test_modes[g_test_mode_count] = (s32)p;
    }
    g_test_mode_count++;
}

void AddPrim(void *ot, void *p) {
    if (g_test_mode_count > 0 && g_test_mode_count <= 4 &&
        (s32)p == g_test_modes[g_test_mode_count - 1]) {
        g_test_modes_linked++;
    }
}

/* One call's result: two drawing modes were set and linked, the first the
 * block's own and the second straight after the block; `size` is what the
 * function returned. */
static void test_Check(s32 prims, s32 block_size, s32 size, s32 bugfixes) {
    CHECK_EQ(g_test_mode_count, 2);
    CHECK_EQ(g_test_modes_linked, 2);
    CHECK_EQ(g_test_modes[0], prims + block_size - sizeof(DR_MODE));
    CHECK_EQ(g_test_modes[1], prims + block_size);
    if (bugfixes) {
        /* the second is inside what the caller is told was built */
        CHECK_EQ(size, block_size + sizeof(DR_MODE));
        CHECK(g_test_modes[1] + (s32)sizeof(DR_MODE) <= prims + size);
    } else {
        /* the original: the caller builds its next primitive on it */
        CHECK_EQ(size, block_size);
        CHECK_EQ(g_test_modes[1], prims + size);
    }
    /* the caller moves its cursor on by words */
    CHECK_EQ(size % 4, 0);
}

void test_Main(void) {
    static ClockTime time = {0, 42, 50};
    s32 prims = (s32)test_Alloc(0x4000);
    s32 bugfixes;
    s32 size;

    CHECK_EQ(RB_FIX_HUD_DRAW_MODE, 1);
    g_disp_ot = g_test_ot;

    for (bugfixes = 0; bugfixes < 2; bugfixes++) {
        g_rb_settings.bugfixes = bugfixes;

        /* two points and a warning a side */
        g_test_mode_count = 0;
        g_test_modes_linked = 0;
        size = hud_DrawPointsAndTime(&time, 0x1122, prims, 1);
        test_Check(prims, sizeof(HudCounterPrims), size, bugfixes);

        /* a team battle of three matches, each side one point */
        g_test_mode_count = 0;
        g_test_modes_linked = 0;
        size = hud_DrawTeamScores(0x5, prims, 1);
        test_Check(prims, sizeof(HudTeamScorePrims), size, bugfixes);
    }
}
