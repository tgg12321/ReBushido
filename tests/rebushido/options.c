/* The options page's table and stepping (options.c, settings.c): rows and
 * choices go round, and every row fits the page and the file. */
#include "src/rebushido/settings.c"
#include "src/rebushido/options.c"
#include "runtime/runtime.h"

static s32 test_Length(const char *text) {
    s32 length = 0;

    while (text[length] != 0) {
        length++;
    }
    return length;
}

void test_Main(void) {
    /* which places of the file and members of the settings are taken */
    u8 slot_taken[RB_SAVE_VALUES];
    u8 member_taken[sizeof(RbSettings)];
    s32 i;
    s32 j;

    CHECK_EQ(REBUSHIDO, 1);
    CHECK_EQ(RB_OPTIONS_PAGE, 1);

    /* the cursor: down from the last row is the first, up from the first
     * the last */
    CHECK_EQ(rb_StepRow(0, 1), 1);
    CHECK_EQ(rb_StepRow(RB_OPTION_COUNT - 1, 1), 0);
    CHECK_EQ(rb_StepRow(0, -1), RB_OPTION_COUNT - 1);
    CHECK_EQ(rb_StepRow(RB_OPTION_COUNT - 1, -1), RB_OPTION_COUNT - 2);
    for (i = 0; i < RB_OPTION_COUNT; i++) {
        CHECK_EQ(rb_StepRow(rb_StepRow(i, 1), -1), i);
    }

    /* the rows the page has room for above its lower rule */
    CHECK(ROWS_Y + RB_OPTION_COUNT * ROW_PITCH <= RULE_BOTTOM_Y);

    for (i = 0; i < RB_SAVE_VALUES; i++) {
        slot_taken[i] = FALSE;
    }
    for (i = 0; i < (s32)sizeof(RbSettings); i++) {
        member_taken[i] = FALSE;
    }
    for (i = 0; i < RB_OPTION_COUNT; i++) {
        const RbOption *option = &g_rb_options[i];
        s32 member = option->value - (u8 *)&g_rb_settings;

        /* a setting of g_rb_settings, and a place in the file, each its
         * own */
        CHECK(member >= 0 && member < (s32)sizeof(RbSettings));
        CHECK_EQ(member_taken[member], FALSE);
        member_taken[member] = TRUE;
        CHECK(option->save_slot < RB_SAVE_SLOT_COUNT);
        CHECK_EQ(slot_taken[option->save_slot], FALSE);
        slot_taken[option->save_slot] = TRUE;

        /* its name and its choices fit their columns */
        CHECK(test_Length(option->name) * FONT_CHAR_SIZE <
              ROW_VALUE_X - ROW_NAME_X);
        CHECK(option->choice_count >= 2);
        CHECK(option->choice_count <= RB_OPTION_CHOICES_MAX);
        for (j = 0; j < option->choice_count; j++) {
            /* with "< " and " >" round it */
            s32 width = (test_Length(option->choices[j]) + 4) * FONT_CHAR_SIZE;

            CHECK(option->choices[j] != NULL);
            CHECK(ROW_VALUE_X + width <= RULE_X + RULE_WIDTH);
            CHECK(test_Length(option->choices[j]) + 5 <= VALUE_TEXT_SIZE);
        }

        /* it starts as the original, and its choices go round both ways */
        CHECK_EQ(*option->value, 0);
        for (j = 1; j < option->choice_count; j++) {
            rb_StepOption(option, 1);
            CHECK_EQ(*option->value, j);
        }
        rb_StepOption(option, 1);
        CHECK_EQ(*option->value, 0);
        rb_StepOption(option, -1);
        CHECK_EQ(*option->value, option->choice_count - 1);
        rb_StepOption(option, -1);
        CHECK_EQ(*option->value, option->choice_count - 2);
        rb_ResetSettings();
        CHECK_EQ(*option->value, 0);
    }
    /* every member of the settings has a row */
    for (i = 0; i < (s32)sizeof(RbSettings); i++) {
        CHECK_EQ(member_taken[i], TRUE);
    }

    /* the lines at the bottom fit the screen, one for each state of the
     * card */
    CHECK_EQ(sizeof(g_rb_page_card_notices) / sizeof(g_rb_page_card_notices[0]),
             RB_CARD_FULL + 1);
    for (i = 0; i <= RB_CARD_FULL; i++) {
        CHECK(test_Length(g_rb_page_card_notices[i]) * FONT_CHAR_SIZE <=
              PAGE_WIDTH);
    }
}
