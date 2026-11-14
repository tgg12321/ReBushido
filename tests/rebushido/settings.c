/* ReBushido's settings (settings.c): as the original until changed. */
#include "src/rebushido/settings.c"
#include "runtime/runtime.h"

void test_Main(void) {
    /* this test is built with the master switch on, and only then */
    CHECK_EQ(REBUSHIDO, 1);

    /* as the program starts */
    CHECK_EQ(g_rb_settings.bugfixes, FALSE);
    CHECK_EQ(g_rb_settings.graphics, RB_GRAPHICS_ORIGINAL);

    /* and after a reset, whatever was chosen */
    g_rb_settings.bugfixes = TRUE;
    g_rb_settings.graphics = RB_GRAPHICS_ENHANCED;
    rb_ResetSettings();
    CHECK_EQ(g_rb_settings.bugfixes, FALSE);
    CHECK_EQ(g_rb_settings.graphics, RB_GRAPHICS_ORIGINAL);

    /* "original" is what memory that was never set says */
    CHECK_EQ(RB_GRAPHICS_ORIGINAL, 0);
}
