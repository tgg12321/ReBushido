/* The options page (rb_options.h): ReBushido's name and version, a row for
 * each setting of g_rb_options, what the buttons do, and a line about the
 * memory card. Up and down choose a row, left and right change it, Start
 * or cross goes on into the game.
 *
 * It runs before the game's first frame, with a loop of its own like the
 * pause menu's: a 320 by 240 screen (the boot image's), drawn with the
 * game's built-in font and plain rectangles. When it is done video memory
 * is black again, as the boot image expects it. Without RB_OPTIONS_PAGE,
 * and in the quick-boot build, this file is empty. */
#include "common.h"
#include "bb2.h"

#if RB_OPTIONS_PAGE && !RB_QUICK_BOOT

#define PAGE_WIDTH 320
#define PAGE_HEIGHT 240

/* The built-in font as font.c loads it into video memory: its colour
 * tables' place, its texture page, and where in that page a character's
 * 8 by 8 picture is. A character is seven pixels wide and, but for the
 * tails of g j p q y, seven high. */
#define FONT_CLUT_X 1008
#define FONT_CLUT_Y 476
#define FONT_TPAGE 0x1F
#define FONT_TEX_U 192
#define FONT_TEX_V 224
#define FONT_CHAR_SIZE 8
#define FONT_GLYPH_SIZE 7
#define FONT_ROW_CHARS 8
/* Where a character's picture is in the texture page: eight to a row, a
 * row for every 32 codes. */
#define FONT_CHAR_U(ch)                                                        \
    (FONT_TEX_U + ((ch) & (FONT_ROW_CHARS - 1)) * FONT_CHAR_SIZE)
#define FONT_CHAR_V(ch) (FONT_TEX_V + ((ch) >> 5) * FONT_CHAR_SIZE)
/* Text drawn twice the size: the character's own seven pixels doubled,
 * and the next one sixteen further on. */
#define BIG_GLYPH_SIZE (2 * FONT_GLYPH_SIZE)
#define BIG_CHAR_SIZE (2 * FONT_CHAR_SIZE)

/* Colours, 0xBBGGRR. A character's is doubled by the GPU: 0x80 is the
 * font's own white. */
#define COLOR_TEXT 0x808080
#define COLOR_TEXT_DIM 0x505050
#define COLOR_TEXT_FAINT 0x383838
#define COLOR_TEXT_NOTICE 0x306890
#define COLOR_RULE 0x18188C
#define COLOR_CURSOR_BAR 0x0C0C50

/* Where things are on the page. */
#define TITLE_Y 28
#define VERSION_Y 48
#define RULE_X 40
#define RULE_WIDTH 240
#define RULE_TOP_Y 62
#define ROWS_Y 80
#define ROW_PITCH 18
#define ROW_NAME_X 56
#define ROW_VALUE_X 160
#define ROW_BAR_ABOVE 5 /* the cursor's bar begins this far above its text */
#define RULE_BOTTOM_Y 158
#define HELP_Y 168
#define HELP_PITCH 12
#define HELP_KEYS_X 56
#define HELP_WHAT_X 176
#define NOTICE_Y 208

/* The rectangles are drawn first, the text over them. */
enum {
    LAYER_TEXT = 0,
    LAYER_BARS = 1,
    LAYER_COUNT = 2,
};

/* How long the page stays up to say that the settings were not saved. */
#define FAILED_NOTICE_FRAMES 150
/* The longest a setting's value is, with the cursor's marks round it. */
#define VALUE_TEXT_SIZE 32

u8 g_rb_page_state;
u8 g_rb_page_row;

/* The two buffers the page is drawn in by turns, each on show while the
 * other is drawn, and an ordering table for each. */
static DRAWENV g_rb_page_draw[2];
static DISPENV g_rb_page_disp[2];
static u32 g_rb_page_ots[2][LAYER_COUNT];
static s32 g_rb_page_frames;
/* The frame being built: its ordering table and its next primitive. */
static u32 *g_rb_page_ot;
static u32 *g_rb_page_prim;

/* Both buffers, for clearing them when the page is done. */
static RECT g_rb_page_rect = {0, 0, DISP_WIDTH, 2 * DISP_HEIGHT};

#define HELP_LINES 3
static const char * const g_rb_page_help[HELP_LINES][2] = {
    {"Up / Down", "choose"},
    {"Left / Right", "change"},
    {"Start / Cross", "continue"},
};

/* What the page says about the card, by RbCardStatus. */
static const char * const g_rb_page_card_notices[] = {
    "",
    "Saved on the memory card in slot 1",
    "No memory card: will not be saved",
    "Unformatted card: will not be saved",
    "Memory card full: will not be saved",
};

s32 rb_StepRow(s32 row, s32 step) {
    row += step;
    if (row < 0) {
        row = RB_OPTION_COUNT - 1;
    } else if (row >= RB_OPTION_COUNT) {
        row = 0;
    }
    return row;
}

/* ---- drawing ---- */

/* The colour table a character is drawn through (font.c: four characters
 * share each place in the font's picture). */
static s32 rb_GetCharClut(s32 ch) {
    return getClut(FONT_CLUT_X, FONT_CLUT_Y + ((ch >> 3) & 3));
}

/* Text from (x, y) in the built-in font, a sprite a character. */
static void rb_DrawText(s32 x, s32 y, const char *text, s32 color) {
    s32 ch;

    for (; (ch = (u8)*text) != 0; text++, x += FONT_CHAR_SIZE) {
        SPRT_8 *sprite = (SPRT_8 *)g_rb_page_prim;

        if (ch == ' ') {
            continue;
        }
        setSprt8(sprite);
        setRGB0(
            sprite, color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF);
        sprite->x0 = x;
        sprite->y0 = y;
        sprite->u0 = FONT_CHAR_U(ch);
        sprite->v0 = FONT_CHAR_V(ch);
        sprite->clut = rb_GetCharClut(ch);
        addPrim(&g_rb_page_ot[LAYER_TEXT], sprite);
        g_rb_page_prim = (u32 *)(sprite + 1);
    }
}

/* The same at twice the size, for text without the tails of g j p q y. */
static void rb_DrawBigText(s32 x, s32 y, const char *text, s32 color) {
    s32 ch;

    for (; (ch = (u8)*text) != 0; text++, x += BIG_CHAR_SIZE) {
        POLY_FT4 *poly = (POLY_FT4 *)g_rb_page_prim;

        if (ch == ' ') {
            continue;
        }
        setPolyFT4(poly);
        setRGB0(poly, color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF);
        setXYWH(poly, x, y, BIG_GLYPH_SIZE, BIG_GLYPH_SIZE);
        setUVWH(poly, FONT_CHAR_U(ch), FONT_CHAR_V(ch), FONT_GLYPH_SIZE,
                FONT_GLYPH_SIZE);
        poly->tpage = FONT_TPAGE;
        poly->clut = rb_GetCharClut(ch);
        addPrim(&g_rb_page_ot[LAYER_TEXT], poly);
        g_rb_page_prim = (u32 *)(poly + 1);
    }
}

/* Text with its middle at the middle of the screen. */
static void rb_DrawCenteredText(s32 y, const char *text, s32 color) {
    rb_DrawText((PAGE_WIDTH - (s32)strlen((char *)text) * FONT_CHAR_SIZE) / 2,
                y, text, color);
}

/* A filled rectangle, under the text. */
static void rb_DrawBar(s32 x, s32 y, s32 width, s32 height, s32 color) {
    TILE *tile = (TILE *)g_rb_page_prim;

    setTile(tile);
    setRGB0(tile, color & 0xFF, (color >> 8) & 0xFF, (color >> 16) & 0xFF);
    tile->x0 = x;
    tile->y0 = y;
    tile->w = width;
    tile->h = height;
    addPrim(&g_rb_page_ot[LAYER_BARS], tile);
    g_rb_page_prim = (u32 *)(tile + 1);
}

/* Draws the page into the buffer that is not on show, waits for the
 * vertical blank and shows it. `notice` is the line at the bottom. */
static void rb_DrawPage(const char *notice) {
    char value[VALUE_TEXT_SIZE];
    s32 index = g_rb_page_frames & 1;
    s32 i;
    s32 y;

    g_rb_page_ot = g_rb_page_ots[index];
    g_rb_page_prim = (u32 *)ADDR(g_disp_prim_buffers[index]);
    ClearOTagR(g_rb_page_ot, LAYER_COUNT);

    rb_DrawBigText(
        (PAGE_WIDTH - (s32)(sizeof("ReBushido") - 1) * BIG_CHAR_SIZE) / 2,
        TITLE_Y, "ReBushido", COLOR_TEXT);
    /* g_rb_version is "ReBushido <version> (<commit>)" */
    rb_DrawCenteredText(
        VERSION_Y, g_rb_version + sizeof("ReBushido ") - 1, COLOR_TEXT_DIM);
    rb_DrawBar(RULE_X, RULE_TOP_Y, RULE_WIDTH, 1, COLOR_RULE);

    for (i = 0; i < RB_OPTION_COUNT; i++) {
        const RbOption *option = &g_rb_options[i];
        const char *choice = option->choices[*option->value];

        y = ROWS_Y + i * ROW_PITCH;
        if (i == g_rb_page_row) {
            rb_DrawBar(RULE_X, y - ROW_BAR_ABOVE, RULE_WIDTH, ROW_PITCH,
                       COLOR_CURSOR_BAR);
            sprintf(value, "< %s >", choice);
            rb_DrawText(ROW_NAME_X, y, option->name, COLOR_TEXT);
            rb_DrawText(ROW_VALUE_X, y, value, COLOR_TEXT);
        } else {
            rb_DrawText(ROW_NAME_X, y, option->name, COLOR_TEXT_DIM);
            rb_DrawText(
                ROW_VALUE_X + 2 * FONT_CHAR_SIZE, y, choice, COLOR_TEXT_DIM);
        }
    }

    rb_DrawBar(RULE_X, RULE_BOTTOM_Y, RULE_WIDTH, 1, COLOR_RULE);
    for (i = 0; i < HELP_LINES; i++) {
        y = HELP_Y + i * HELP_PITCH;
        rb_DrawText(HELP_KEYS_X, y, g_rb_page_help[i][0], COLOR_TEXT_DIM);
        rb_DrawText(HELP_WHAT_X, y, g_rb_page_help[i][1], COLOR_TEXT_FAINT);
    }
    rb_DrawCenteredText(NOTICE_Y, notice, COLOR_TEXT_NOTICE);

    DrawSync(0);
    VSync(0);
    PutDispEnv(&g_rb_page_disp[index]);
    PutDrawEnv(&g_rb_page_draw[index]);
    DrawOTag(&g_rb_page_ot[LAYER_COUNT - 1]);
    g_rb_page_frames++;
}

/* The same picture for `frames` frames: twice at least, since a frame is
 * on show only once the next is drawn. */
static void rb_HoldPage(const char *notice, s32 frames) {
    s32 i;

    for (i = 0; i < frames; i++) {
        rb_DrawPage(notice);
    }
    DrawSync(0);
}

/* ---- the pads ---- */

/* The buttons held on either pad, a bit each as in Sony's PADLup and the
 * rest, read from the packets the pad driver fills (pad.c). The game's
 * own g_pad_state is left as it was. While no pad has answered every
 * button counts as held, so that none is taken for newly pressed when
 * one does. */
static s32 rb_ReadPads(void) {
    s32 held = 0;
    s32 answered = FALSE;
    s32 i;

#if RB_PORT
    /* as in pad_Update: the port's one input point (src/port/pad.c) */
    port_TakeInput();
#endif
    for (i = 0; i < 2; i++) {
        const u8 *packet = (const u8 *)g_pad_packets[i];
        s32 type = packet[1] >> 4;

        if (packet[0] == 0 &&
            (type == PAD_TYPE_DIGITAL || type == PAD_TYPE_ANALOG_STICK ||
             type == PAD_TYPE_ANALOG)) {
            held |= ~((packet[2] << 8) | packet[3]) & 0xFFFF;
            answered = TRUE;
        }
    }
    return answered ? held : -1;
}

/* ---- the page ---- */

static void rb_OpenPage(void) {
    s32 i;

    for (i = 0; i < 2; i++) {
        /* each buffer is on show while the other is drawn */
        SetDefDrawEnv(
            &g_rb_page_draw[i], 0, i * PAGE_HEIGHT, PAGE_WIDTH, PAGE_HEIGHT);
        SetDefDispEnv(&g_rb_page_disp[i], 0, (1 - i) * PAGE_HEIGHT, PAGE_WIDTH,
                      PAGE_HEIGHT);
        g_rb_page_draw[i].isbg = 1;
        setRGB0(&g_rb_page_draw[i], 0, 0, 0);
        g_rb_page_draw[i].tpage = FONT_TPAGE;
    }
    g_rb_page_frames = 0;
    g_rb_page_row = 0;
}

/* Leaves video memory black and nothing being drawn. */
static void rb_ClosePage(void) {
    DrawSync(0);
    ClearImage(&g_rb_page_rect, 0, 0, 0);
    DrawSync(0);
}

/* The page from the moment it comes up to the moment the player has left
 * it and the settings are written: the card is read with the page on
 * show, then the pads move the cursor and change the settings, a frame at
 * a time, until Start or cross. */
void rb_RunOptionsPage(void) {
    u8 loaded[RB_OPTION_COUNT];
    /* everything counts as held before the first look, so that a button
     * down as the page comes up does nothing until it is let go */
    s32 held = -1;
    s32 pressed;
    s32 changed;
    s32 i;

    /* the page comes up at once and waits for the card on show */
#if RB_PORT
    /* the port's --fight goes straight to its fight, with the settings the
     * game starts with */
    if (port_QuickFight() != NULL) {
        g_rb_page_state = RB_PAGE_DONE;
        return;
    }
#endif
    g_rb_page_state = RB_PAGE_LOADING;
    rb_OpenPage();
    rb_HoldPage("Reading the memory card", 2);
    rb_LoadSave();
    for (i = 0; i < RB_OPTION_COUNT; i++) {
        loaded[i] = *g_rb_options[i].value;
    }

    g_rb_page_state = RB_PAGE_UP;
    for (;;) {
        pressed = held;
        held = rb_ReadPads();
        pressed = held & ~pressed;

        if (pressed & (PADstart | PADRdown)) {
            snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
            break;
        }
        if (pressed & (PADLup | PADLdown)) {
            snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
            g_rb_page_row =
                rb_StepRow(g_rb_page_row, (pressed & PADLup) ? -1 : 1);
        } else if (pressed & (PADLleft | PADLright)) {
            snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
            rb_StepOption(
                &g_rb_options[g_rb_page_row], (pressed & PADLleft) ? -1 : 1);
        }

        rb_PollCard();
        snd_FlushQueue();
        rb_DrawPage(g_rb_page_card_notices[g_rb_card_status]);
    }
    snd_FlushQueue();

    /* The file is written only if the player changed something: a card
     * that had no settings, or settings this version cannot read, is
     * otherwise left as it was. */
    g_rb_page_state = RB_PAGE_SAVING;
    changed = FALSE;
    for (i = 0; i < RB_OPTION_COUNT; i++) {
        if (loaded[i] != *g_rb_options[i].value) {
            changed = TRUE;
        }
    }
    rb_QuietCard();
    /* with no card to write to, the page has said so already */
    if (changed && g_rb_card_status == RB_CARD_READY) {
        rb_HoldPage("Saving: leave the memory card in", 2);
        if (!rb_WriteSave()) {
            rb_HoldPage("The settings were not saved", FAILED_NOTICE_FRAMES);
        }
    } else if (changed) {
        g_rb_save_result = RB_SAVE_FAILED;
    }

    rb_ClosePage();
    g_rb_page_state = RB_PAGE_DONE;
}

#endif /* RB_OPTIONS_PAGE && !RB_QUICK_BOOT */
