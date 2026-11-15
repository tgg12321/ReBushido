/* The pages of the main menu, drawn from MOD.BIN: the top menu with its
 * eight rows (seven modes and the options) and the choice of view; the
 * options; the memory card menu; the key configuration; training mode's
 * choice of practice; chanbara's question with the best times; and the
 * message box the memory-card screens write in. menu_screen.c decides which
 * page is up; menu_run.c holds the few steps between the two.
 *
 * Every page is drawn fresh each frame into the primitive pool of that
 * frame (MenuCtx), over the same frame: a backdrop in three pieces (a
 * column and a slanted piece each side of it), one or two see-through
 * panels and a title with a rule under it. A cursor does not jump: the
 * thing it is on is nudged six pixels in the direction pressed and the move
 * happens when the nudge is done (menu_StepNudge).
 *
 * At the end of the file are the pieces every 2D screen uses: relocating a
 * resource file and laying out the primitive pools. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include <psxsdk/inline_c.h>
#include <psxsdk/gtemac.h>
#include "bb2.h"

/* Defined further down. menu_DrawWhitePage has no prototype:
 * menu_UpdateWhitePage passes it an argument it does not take. */
static void menu_DrawWhitePage();
static s32 menu_RelocateFile(MenuFile *file);
static void menu_BeginDraw();
static void menu_DrawPageFrame(MenuCtx *ctx, s32 panels, GpuDb *db);
static void menu_DrawTopRows(MenuCtx *ctx, u16 *draw_env, s32 views);
static void menu_DrawPovChoice(MenuCtx *ctx, s32 views);
static void menu_DrawSideViewChoice(MenuCtx *ctx, s32 views);
static RECT g_menu_palette_rect;
static u32 g_menu_nudge_minus_buttons[2];
static u32 g_menu_nudge_plus_buttons[2];
static RECT g_menu_link_picture_rect;
static RECT g_menu_link_clear_rect;

/* A row of the top menu: the mode it starts (enum FightMode, or
 * MENU_ROW_OPTIONS) and which views the mode can be played in. */
typedef struct {
    u8 mode;
    u8 views;
} MenuTopRow;

#define MENU_VIEW_SIDE 1
#define MENU_VIEW_POV 2
#define MENU_TOP_ROWS 8
/* The last row is always shown; the seven above it scroll in a window six
 * rows high. rows_shown has a bit for each of five rows from the first in
 * view, and only the first is read. */
#define MENU_TOP_LAST_ROW 7
#define MENU_TOP_SHOWN_ROWS 5

/* The top menu: the rows that can be chosen and the rows now in view, a bit
 * each, then the rows. */
typedef struct {
    s32 rows_on;
    s32 rows_shown;
    MenuTopRow row[MENU_TOP_ROWS];
} MenuTop;

static MenuTop g_menu_top;

/* The menu's work area, kept after the primitive pools. `nudge` is how far
 * the thing under the cursor is pushed, across and down. key_cursors is each
 * player's row on the key configuration page; `word` is both at once.
 * grayscale_shown is the "Monitor" option as the colour tables in video
 * memory have it. The rest is not used. */
typedef struct {
    u8 pad00[0xC];
    s16 nudge[2];
    s16 unk_10; /* cleared when the menu opens; not read */
    s16 unk_12; /* cleared when the menu opens; not read */
    u8 pad14[0x10];
    PTR32(MenuFile *) file;

    union {
        s16 half[2];
        s32 word;
    } key_cursors;

    u8 pad2C[4];
    u8 grayscale_shown;
    u8 pad31[3];
} MenuWork;

/* The menu's files in the disc's data group: MOD.BIN and the picture shown
 * while the link cable connects, in colour and in grayscale. */
#define MENU_FILE CDROM_DATA_MOD_BIN
#define MENU_LINK_PICTURE_FILE CDROM_DATA_LINK_TIM
#define MENU_LINK_PICTURE_GRAY_FILE CDROM_DATA_LINK_M_TIM
#define MENU_FILE_LISTS 12 /* MOD.BIN's lists of sheets */

/* FAKE: the top menu's locals, laid out as its stack frame has them */
typedef struct {
    MenuCtx ctx;
    s32 unused[2];
    s32 pressed;
    s32 unused2;
    s32 last_row_shown;
} MenuTopFrame;

/* g_menu_cursors holds the cursors of three pages. */
#define CURSOR_TOP 0xF        /* the top menu's row */
#define CURSOR_OPTIONS 0x1C00 /* the options page's row */
#define CURSOR_OPTIONS_SHIFT 10
#define CURSOR_CARD 0xE000 /* the memory card menu's row */
#define CURSOR_CARD_SHIFT 13
#define CURSOR_ROW_MASK 7 /* a row of the last two, in its place */
#define TOP_ROW (g_menu_cursors & CURSOR_TOP)
#define OPTIONS_ROW                                                            \
    (((u32)g_menu_cursors >> CURSOR_OPTIONS_SHIFT) & CURSOR_ROW_MASK)
#define CARD_MENU_ROW                                                          \
    (((u32)g_menu_cursors >> CURSOR_CARD_SHIFT) & CURSOR_ROW_MASK)

enum OptionsRow {
    OPTIONS_ROW_MONITOR = 0,
    OPTIONS_ROW_DIFFICULTY = 1,
    OPTIONS_ROW_MUSIC = 2,
    OPTIONS_ROW_MEMCARD = 3,
    OPTIONS_ROW_KEY_CONFIG = 4,
    OPTIONS_ROW_EXIT = 5,
    OPTIONS_ROWS
};

enum CardMenuRow {
    CARD_MENU_ROW_LOAD = 0,
    CARD_MENU_ROW_SAVE = 1,
    CARD_MENU_ROW_BACK = 2,
    CARD_MENU_ROWS
};

/* The rows of the key configuration page: the three actions, then these. */
enum KeyConfigRow {
    KEY_ROW_DEFAULTS = 3, /* the mapping the game came with */
    KEY_ROW_RESET = 4,    /* the mapping as it was before the page opened */
    KEY_ROW_EXIT = 5,
    KEY_ROWS
};

/* MenuWork.key_cursors.word with both players' cursors on the last row. */
#define KEY_CURSORS_ON_EXIT (KEY_ROW_EXIT << 16 | KEY_ROW_EXIT)

/* How far a nudge goes before the cursor moves. */
#define NUDGE_DISTANCE 6

/* The brightness of the thing under the cursor swings with a sine, once
 * round in MENU_PULSE_FRAMES frames: `depth` either side of `center`
 * (SPRITE_GLOW), from the angle `start`. Most pulses start an eighth of a
 * turn round, less one. */
#define MENU_PULSE_FRAMES 32
#define MENU_PULSE_START (ANGLE_TURN / 8 - 1)
#define MENU_PULSE_FROM(start, depth, center)                                  \
    SPRITE_GLOW(                                                               \
        rsin(((g_menu_pulse & (MENU_PULSE_FRAMES - 1)) << 7) + (start)),       \
        depth, center)
#define MENU_PULSE(depth, center)                                              \
    MENU_PULSE_FROM(MENU_PULSE_START, depth, center)

/* ---- colours ---- */

/* A sheet's tint: this much leaves it as it is, less darkens it. A sheet
 * that glows swings this far either side of it. */
#define GLOW_DEPTH 47
#define GLOW_DEPTH_SMALL 32
#define TINT_PLAIN 0x80
#define TINT_DIM 0x40
#define TINT_OTHER_VIEW 0x70 /* the view not chosen */
#define TINT_ANSWER_OFF 0x30 /* a question's answer the cursor is not on */
#define TINT_BLINK 0xA0      /* the best time just set, every other frame */
/* A row of the top menu: one that can be chosen, one that cannot, and the
 * cursor's, whose red and green swing. */
#define ROW_R 0x80
#define ROW_G 0x6C
#define ROW_B 0x30
#define ROW_OFF 0x30
#define ROW_GLOW_R MENU_PULSE(63, 159)
#define ROW_GLOW_G MENU_PULSE(45, 131)
/* The line under a row the cursor is not on: its three lines, the lowest
 * first, and the pieces at its ends. */
#define UNDERLINE_GRAY_1 0x50
#define UNDERLINE_GRAY_2 0x20
#define UNDERLINE_GRAY_3 0x10
#define UNDERLINE_GRAY_ENDS 0x28
/* The far side of the frame round the key configuration's cursor row. */
#define KEY_FRAME_FAR_RED 0x80

/* A frame's drawing environment (DRAWENV) as menu_DrawTopRows is handed it,
 * in halfwords: the clip rectangle, then the offset. */
enum {
    ENV_CLIP_X,
    ENV_CLIP_Y,
    ENV_CLIP_W,
    ENV_CLIP_H,
    ENV_OFS_X,
    ENV_OFS_Y,
};

/* ---- the layers ----
 * Entries of the display's ordering table, the nearest first. Every page
 * has the last three; the top menu's rows are between the two entries that
 * set and unset their window. */
enum MenuLayer {
    /* the top menu: the mode's picture; its title and the field behind the
     * picture; the two views with their arrows */
    MENU_LAYER_TOP_PICTURE = 0,
    MENU_LAYER_TOP_TITLE = 1,
    MENU_LAYER_TOP_SIDE_VIEW = 2,
    MENU_LAYER_TOP_POV = 3,
    /* the message box; the key configuration's buttons and the frame round
     * a cursor's row */
    MENU_LAYER_FRONT = 8,
    MENU_LAYER_TOP_LAST_ROW = 9,
    /* a page's title and rows; the top menu's window ends here */
    MENU_LAYER_PAGE = 10,
    MENU_LAYER_TOP_ROWS = 11,
    /* the key configuration's bars; the top menu's window begins here */
    MENU_LAYER_BARS = 12,
    MENU_LAYER_WHITE_TEXT = 16, /* the white page's text */
    /* the see-through panels, and the rule under a title */
    MENU_LAYER_PANEL = 17,
    MENU_LAYER_FRAME = 18, /* the three sheets every page has */
    MENU_LAYER_BACKDROP = 19,
    MENU_LAYER_WHITE = 20, /* the white page's white */
};

/* the key configuration's buttons lit on the pad */
#define MENU_LAYER_KEY_PAD_BUTTONS MENU_LAYER_TOP_LAST_ROW

/* ---- where things are on the screen (640 x 240) ----
 * A sheet's cells have their places in them; these are the places the code
 * works out. */

/* The panels (menu_DrawPageFrame): the pair and the top menu's; the wide
 * one is in menu.h. */
#define PANEL_PAIR_X 78
#define PANEL_PAIR_WIDTH 204
#define PANEL_TOP_X 304
#define PANEL_TOP_Y 58
#define PANEL_TOP_WIDTH 294
#define PANEL_TOP_HEIGHT 171
/* The rule under a page's title, at MENU_TITLE_RULE_Y: its place and length
 * are the page's. */
#define OPTIONS_RULE_X 232 /* the options and the memory card menu */
#define OPTIONS_RULE_WIDTH 175
#define KEY_RULE_X 207
#define KEY_RULE_WIDTH 225
#define PRACTICE_RULE_X 218
#define PRACTICE_RULE_WIDTH 203
#define CHANBARA_RULE_X 245
#define CHANBARA_RULE_WIDTH 150

/* The top menu's choice of view. A mode's only view is at VIEW_X; of two,
 * the one chosen is life size and the other half size, a little lower (and
 * the side view further right). */
#define VIEW_X 78
#define VIEW_POV_X 156
#define VIEW_SIDE_X (-1)
#define VIEW_SMALL_Y 10
#define VIEW_SIDE_SMALL_X 50

/* The top menu's rows: seven in a window, and the last under it. */
#define TOP_ROWS_X 118
#define TOP_ROWS_Y 63
#define TOP_ROW_PITCH 24
#define TOP_LAST_ROW_Y 207
#define TOP_WINDOW_Y 58
#define TOP_WINDOW_WIDTH 246
#define TOP_WINDOW_HEIGHT 146
#define TOP_CAPTION_Y 65 /* the cursor row's name in English */
#define TOP_PICTURE_Y 25 /* the sprites over the mode's picture */
/* The black field behind the mode's picture. */
#define TOP_FIELD_X 322
#define TOP_FIELD_Y 81
#define TOP_FIELD_WIDTH 256
#define TOP_FIELD_HEIGHT 89
/* The line under a row: three lines, each a pixel higher and beginning
 * further right than the one under it. The lowest is from the left of the
 * rows to a little past the row's own left; the others are from the row's
 * left. */
#define UNDERLINE_X 95
#define UNDERLINE_Y 15
#define UNDERLINE_PAST_ROW 25
#define UNDERLINE_2_WIDTH 120
#define UNDERLINE_3_X 64
#define UNDERLINE_3_WIDTH 56

/* The message box, in the middle of the screen. Under a question the box
 * is taller by the answers' row, and the answer the cursor is on has three
 * red lines under it. */
#define MESSAGE_SPRT_BYTES 300 /* of g_menu_message_sprts, a frame */
#define MESSAGE_PRIMS 4        /* modes and tiles a frame */
#define MESSAGE_ANSWERS_HEIGHT 16
#define MESSAGE_ANSWER_CELLS 3 /* yes, no, and what is between them */
#define MESSAGE_YES_X 275
#define MESSAGE_YES_WIDTH 40
#define MESSAGE_NO_X_STEP 58 /* "no", from "yes" */
#define MESSAGE_NO_WIDTH 31
#define MESSAGE_UNDERLINE_Y 124 /* from the answers' row */

/* The key configuration. */
#define KEY_BARS 11         /* MenuFile.key_config_rects */
#define KEY_ACTION_PITCH 16 /* from one action's buttons to the next's */
#define KEY_BUTTON_PITCH 23 /* from one button of an action to the next */

/* A table of best times (menu_DrawBestTimes). Its rows are
 * BEST_TIMES_ROW_PITCH apart; a time is three pairs of digits; the time
 * just set is a fourth row, under the table and further left. */
#define BEST_DIGIT_WIDTH 24 /* on the screen, and in the digits' texture */
#define BEST_PAIR_PITCH 58
#define BEST_DIGITS_Y 2
#define BEST_NEW_ROW 3
#define BEST_NEW_NAME_SHIFT 50
#define BEST_NEW_NAME_Y 98
#define BEST_NEW_DIGITS_X 3
#define BEST_NEW_DIGITS_Y 100

/* MenuFile.option_sheets. */
enum OptionSheet {
    OPTION_SHEET_TITLE = 0,
    OPTION_SHEET_ROWS = 1,    /* six */
    OPTION_SHEET_MONITOR = 7, /* an option's two values */
    OPTION_SHEET_DIFFICULTY = 9,
    OPTION_SHEET_MUSIC = 11,
};

/* MenuFile.practice_sheets. */
enum PracticeSheet {
    PRACTICE_SHEET_TITLE = 0,
    PRACTICE_SHEET_ROWS = 1,         /* three */
    PRACTICE_SHEET_DESCRIPTIONS = 4, /* of the two kinds of practice */
    PRACTICE_SHEETS = 6
};

/* MenuFile.top_sheets. */
enum MenuTopSheet {
    TOP_SHEET_POV_PICTURE = 0,
    TOP_SHEET_POV_CAPTION = 1,
    TOP_SHEET_SIDE_PICTURE = 2,
    TOP_SHEET_SIDE_CAPTION = 3,
    TOP_SHEET_MODE_CAPTIONS = 4, /* not shown */
    TOP_SHEET_ARROWS = 6,        /* left, then right */
    TOP_SHEET_TITLE = 9,
    TOP_SHEET_UNDERLINE_LEFT = 10,
    TOP_SHEET_UNDERLINE_RIGHT = 11,
};

/* MenuFile.key_config_sheets. */
enum KeyConfigSheet {
    KEY_SHEET_TITLE = 0,
    KEY_SHEET_PAD = 1,          /* the picture of a pad */
    KEY_SHEET_ROWS = 2,         /* the six rows' names */
    KEY_SHEET_BUTTONS = 8,      /* the four buttons, as a row's mapping */
    KEY_SHEET_NO_BUTTON = 12,   /* an action with no button */
    KEY_SHEET_PAD_BUTTONS = 13, /* the four buttons lit on the pad */
    KEY_SHEET_BOTH_EXIT = 17,   /* the last row when both cursors are on it */
    KEY_SHEET_EXIT = 18,
    KEY_SHEET_NO_EXIT = 19, /* the last row while an action has no button */
};

/* The panels a page is drawn on (menu_DrawPageFrame). */
enum MenuPanels {
    MENU_PANEL_TOP = 0,  /* the top menu's, on the right */
    MENU_PANEL_WIDE = 1, /* one across the screen */
    MENU_PANEL_PAIR = 2, /* one for each player: the key configuration */
};

/* The rows of training mode's page: the two kinds of practice, and back. */
#define PRACTICE_ROWS 3
#define PRACTICE_ROW_BACK 2

/* The second pad's place on a page laid out for two. */
#define PLAYER_2_X 280

/* This file's variables, in the order the original has them. */
static s32 g_menu_cursors;
static MenuWork *g_menu_work;
static s32 g_menu_free_ram; /* the first free byte after what is loaded */
/* eight bytes that no code names: one variable or more */
static s32 g_menu_unused_words[2];
static s16 g_menu_nudge_steps[2]; /* how fast the nudge is moving */
/* cleared with the nudge's steps when the menu loads; nothing reads it */
static s16 g_menu_unused_load_pair[2];
static s32 g_menu_pulse; /* frames, for the cursor's glow */
static s32 g_menu_frame; /* frames drawn; the low bit picks the pool */
static PrimPool *g_menu_pools;
static PrimPool *g_menu_pool;
static GameSettings *g_menu_settings;
static s16 g_menu_practice_cursor;
static s32 g_menu_chanbara_cursor;

MenuFile *g_menu_file;
/* The message box's primitives: it is drawn outside a page's frame, so it
 * has its own. */
TILE *g_menu_message_tile;
DR_MODE *g_menu_message_mode;
u8 *g_menu_message_sprt;
s32 g_menu_message_choice_y;
static u8 g_menu_message_sprts[600];
static DR_MODE g_menu_message_modes[8];
static TILE g_menu_message_tiles[58];

/* ---- loading the menu ---- */

/* Which rows of the top menu can be chosen. `modes` has a bit for each mode
 * (enum FightMode) and bit 7 for the options; the rows are in another order
 * on the screen. */
void menu_SetEnabledRows(u32 modes) {
    s32 *rows_on = &g_menu_top.rows_on;
    s32 rows = *rows_on;
    /* a row's bit is cleared, then set from its mode's */
    rows &= ~0x1;
    rows |= modes & 0x1; /* rows 0 to 2 are modes 0 to 2 */
    rows &= ~0x2;        /* row 1 */
    rows |= modes & 0x2; /* mode 1 */
    rows &= ~0x4;        /* row 2 */
    rows |= modes & 0x4; /* mode 2 */
    rows &= ~0x8;        /* row 3 is mode 4 */
    rows |= ((modes >> 4) & 1) << 3;
    rows &= ~0x10; /* row 4 is mode 5 */
    rows |= ((modes >> 5) & 1) << 4;
    rows &= ~0x20; /* row 5 is mode 6 */
    rows |= ((modes >> 6) & 1) << 5;
    rows &= ~0x40;               /* row 6 is mode 3 */
    rows |= (modes << 3) & 0x40; /* mode 3 */
    rows &= ~0x80;               /* the last row is the options */
    rows |= modes & 0x80;        /* bit MENU_ROW_OPTIONS */
    *rows_on = rows;
}

/* Loads the menu to `memory`: the two frames' pool records, then MOD.BIN,
 * whose image goes to video memory and leaves room for the pools and the
 * work area. Puts every cursor where it starts; the top menu's goes to the
 * first row that can be chosen. */
s32 menu_Load(s32 memory, GameSettings *settings) {
    RECT palette_rect;
    s32 file;
    s32 free_ram;
    MenuWork *work;

    g_menu_free_ram = memory;
    g_menu_pools = (PrimPool *)ADDR(memory);
    file = memory + 2 * sizeof(PrimPool);
    g_menu_free_ram = file;
    resource_FreeAll();
    sprite_LoadFile(MENU_FILE, (SpriteFileHead *)ADDR(g_menu_free_ram));
    g_menu_file = (MenuFile *)ADDR(g_menu_free_ram);
    free_ram = menu_RelocateFile((MenuFile *)ADDR(g_menu_free_ram));
    g_menu_free_ram = free_ram;
    work = (MenuWork *)ADDR(sprite_InitPools(free_ram, g_menu_pools));
    SET_PTR(work->file, (MenuFile *)ADDR(file));
    g_menu_free_ram = (s32)work;
    g_menu_work = work;
    g_menu_free_ram = (s32)(work + 1);
    {
        s32 row_clear_mask = ~CURSOR_TOP;
        s32 cursors = g_menu_cursors;

        cursors &= row_clear_mask;
        g_menu_cursors = cursors;
        if (!(g_menu_top.rows_on & 1)) {
            u32 mask;
            u32 rows_on;
            mask = ~CURSOR_TOP;
            rows_on = g_menu_top.rows_on;
            do {
                s32 next_row;
                s32 row;
                cursors = g_menu_cursors;
                row = cursors;
                row &= CURSOR_TOP;
                if (row >= MENU_TOP_LAST_ROW) {
                    g_menu_cursors = cursors & mask;
                    break;
                }
                next_row = row + 1;
                cursors = (cursors & mask) | (next_row & CURSOR_TOP);
                g_menu_cursors = cursors;
                cursors &= CURSOR_TOP;
                cursors = rows_on >> cursors;
                cursors &= 1;
            } while (!cursors);
        }
        {
            /* FAKE: the work area's members, through pointers */
            s16 *key_cursor_1;
            s16 *key_cursor_0;
            s16 *nudge_y;
            s16 *nudge_x;
            s16 *unused_a; /* the two members nothing reads */
            s16 *unused_b;
            s32 last_row;

            last_row = KEY_ROW_EXIT;
            /* FAKE: an empty block; read it as not there */
            do {
            } while (0);
            key_cursor_1 = &g_menu_work->key_cursors.half[1];
            key_cursor_0 = &g_menu_work->key_cursors.half[0];
            nudge_y = &g_menu_work->nudge[1];
            nudge_x = &g_menu_work->nudge[0];
            unused_a = &g_menu_work->unk_10;
            unused_b = &g_menu_work->unk_12;
            g_menu_settings = settings;
            /* FAKE: an empty block; read it as not there */
            do {
            } while (0);
            g_menu_frame = 0;
            g_menu_practice_cursor = 0;
            *key_cursor_1 = last_row;
            *key_cursor_0 = last_row;
            cursors = g_menu_cursors & ~CURSOR_OPTIONS;
            g_menu_unused_load_pair[1] = 0;
            g_menu_unused_load_pair[0] = 0;
            g_menu_nudge_steps[1] = 0;
            g_menu_nudge_steps[0] = 0;
            cursors |= OPTIONS_ROW_KEY_CONFIG << CURSOR_OPTIONS_SHIFT;
            g_menu_cursors = cursors;
            *unused_b = 0;
            *unused_a = 0;
            *nudge_y = 0;
            *nudge_x = 0;
            palette_rect = g_menu_palette_rect;
            DrawSync(0);
            MoveImage(&palette_rect, SPRITE_STRIP_COPY_X, SPRITE_STRIP_COPY_Y);
            DrawSync(0);
        }
    }
    g_menu_work->grayscale_shown = g_menu_settings->grayscale;
    return 1;
}

/* The pool of frame `frame` (0 or 1). On the way, loads the colour tables
 * again if the "Monitor" option has changed. */
static PrimPool *menu_GetPrimPool(s32 frame) {
    GameSettings *settings = g_menu_settings;
    MenuWork *work = g_menu_work;
    if (work->grayscale_shown != settings->grayscale) {
        sprite_LoadBottomStrip((SpriteFileHead *)g_menu_file);
    }
    settings = g_menu_settings;
    work = g_menu_work;
    work->grayscale_shown = settings->grayscale;
    return &g_menu_pools[frame];
}

/* Turns the offsets in MOD.BIN's twelve lists into addresses and loads the
 * menu's sounds. Returns where the file's image began, which is free once it
 * is in video memory. */
static s32 menu_RelocateFile(MenuFile *file) {
    s32 i = 0;
    SpriteListSlot *list = &file->white_page_sheets;
    do {
        sprite_RelocateList(file, PTR(*list));
        list++;
        i++;
    } while (i < MENU_FILE_LISTS);
    snd_LoadVab(
        PTR(file->head.sounds), SND_VAB_MENU, file->head.free_ram_start);
    return file->head.free_ram_start;
}

/* Turns one of a file's lists of offsets (up to a 0) into addresses. An
 * entry of -1 is left: a place in the list with nothing in it. */
void sprite_RelocateList(void *file, void *list) {
    s32 *entry = list;
    if (!*entry) {
        return;
    }
    while (*entry) {
        if (*entry != -1) {
            *entry = *entry + (s32)file;
        }
        entry++;
    }
}

/* ---- the white page ---- */

/* A page of the menu's text on white, which nothing shows: left from making
 * the game, it seems. Returns 1 when the cross button is pressed. `held` is
 * not used. */
s32 menu_UpdateWhitePage(s32 held, s32 pressed) {
    MenuCtx ctx;
    g_menu_pulse = 0;
    menu_BeginDraw(&ctx, &g_menu_frame);
    menu_DrawWhitePage(&ctx, 0);
    if (pressed & PAD_ANY(PADRdown)) {
        snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
        return PAGE_CONFIRMED;
    }
    return PAGE_RUNNING;
}

/* ---- the cursor's nudge ---- */

/* Moves a page's nudge a frame. `nudge` is how far the thing under the
 * cursor is pushed, across and down; `steps` how fast it is moving. A
 * direction pressed while it is at rest starts it moving that way a pixel a
 * frame; when it has gone NUDGE_DISTANCE it snaps back, and that is when the
 * cursor moves: the result says which way (NUDGE_ACROSS, NUDGE_DOWN).
 * Pressing again on the way hurries it. `pressed` is the buttons pressed
 * this frame, of which `player`'s are looked at. */
s32 menu_StepNudge(u32 *pressed, s32 player, s16 *nudge, s16 *steps) {
    s32 one;
    s32 events;
    s32 axis;
    s32 step_offset;
    u32 plus_button;
    u32 minus_button;
    u32 buttons;
    s16 distance;
    s32 pad_shift;

    events = 0;
    axis = 0;
    pad_shift = player * PAD_BITS;
    /* FAKE: 1 in a local */
    one = 1;
    /* FAKE: the loop, wrapped in a do {} while (0) */
    do {
        step_offset = 0;
        do {
            /* the next axis's speed: the offset is 0 the first time */
            steps = (s16 *)((u8 *)steps + step_offset);
            minus_button = g_menu_nudge_minus_buttons[axis] << pad_shift;
            plus_button = g_menu_nudge_plus_buttons[axis] << pad_shift;
            if (*nudge == 0) {
                buttons = *pressed;
                if (buttons & plus_button) {
                    *steps = one;
                } else if (buttons & minus_button) {
                    *steps = -1;
                }
            } else {
                buttons = *pressed;
                if (buttons & plus_button) {
                    *nudge = NUDGE_DISTANCE;
                } else if (buttons & minus_button) {
                    *steps = -NUDGE_DISTANCE;
                }
                distance = *nudge;
                if (distance >= NUDGE_DISTANCE) {
                    events += one << (axis * NUDGE_DOWN_SHIFT);
                    *steps = 0;
                    *nudge = 0;
                } else if (distance < -(NUDGE_DISTANCE - 1)) {
                    events += NUDGE_MINUS << (axis * NUDGE_DOWN_SHIFT);
                    *steps = 0;
                    *nudge = 0;
                }
            }
            step_offset += 2;
            axis++;
            *nudge = *nudge + *steps;
            nudge++;
        } while (axis < 2);
    } while (0);
    return events;
}

/* ---- the top menu ---- */

/* The top menu: up and down move the cursor over the rows that can be
 * chosen, left and right pick the view where the mode has both, cross
 * chooses. Returns the mode chosen, MENU_TOP_CANCELED on triangle, or
 * MENU_TOP_RUNNING. The two-player modes need a second pad, and only they
 * can be chosen from it. `held` is not used. */
s32 menu_UpdateTop(s32 held, u32 pressed) {
    MenuTopFrame frame;
    s32 events;
    GpuDb *db;
    db = &g_disp_db[g_disp_frame_count & 1];
    menu_BeginDraw(&frame.ctx, &g_menu_frame);
    /* either pad moves the cursor */
    frame.pressed = (pressed & 0xFFFF) | (pressed >> PAD_BITS);
    events = menu_StepNudge(
        &frame.pressed, 0, g_menu_work->nudge, g_menu_nudge_steps);
    switch (NUDGE_DOWN(events)) {
    case NUDGE_PLUS:
        do {
            s32 cursors = g_menu_cursors;
            s32 row;
            g_menu_cursors = (cursors & ~CURSOR_TOP) |
                             (row = ((cursors & CURSOR_TOP) + 1) & CURSOR_TOP);
            if (row >= MENU_TOP_ROWS) {
                g_menu_cursors &= ~CURSOR_TOP;
            }
        } while (!(((u32)g_menu_top.rows_on >> TOP_ROW) & 1));
        g_menu_pulse = 0;
        g_menu_settings->first_person = 0;
        break;
    case NUDGE_MINUS:
        do {
            s32 cursors = g_menu_cursors;
            s32 row = cursors & CURSOR_TOP;
            if (row == 0) {
                g_menu_cursors = (cursors & ~CURSOR_TOP) | MENU_TOP_LAST_ROW;
            } else {
                g_menu_cursors =
                    (cursors & ~CURSOR_TOP) | ((row - 1) & CURSOR_TOP);
            }
        } while (!(((u32)g_menu_top.rows_on >> TOP_ROW) & 1));
        g_menu_pulse = 0;
        g_menu_settings->first_person = 0;
        break;
    }
    /* the view: chosen where the mode has both, else the one it has */
    if ((NUDGE_ACROSS(events) <= NUDGE_MINUS) && (NUDGE_ACROSS(events) != 0) &&
        (g_menu_top.row[TOP_ROW].views == (MENU_VIEW_SIDE | MENU_VIEW_POV))) {
        g_menu_settings->first_person ^= 1;
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
    }
    if (g_menu_top.row[TOP_ROW].views == MENU_VIEW_POV) {
        g_menu_settings->first_person = 1;
    } else if (g_menu_top.row[TOP_ROW].views == MENU_VIEW_SIDE) {
        g_menu_settings->first_person = 0;
    }
    /* scroll the rows so that the cursor's is in view: the window begins
     * at the first row, or five above the cursor's when that is the
     * seventh */
    if (TOP_ROW != MENU_TOP_LAST_ROW) {
        if ((TOP_ROW - (MENU_TOP_SHOWN_ROWS + 1)) >= 0) {
            frame.last_row_shown =
                g_menu_top.rows_shown & (1 << MENU_TOP_LAST_ROW);
            g_menu_top.rows_shown =
                frame.last_row_shown | (((1 << MENU_TOP_SHOWN_ROWS) - 1)
                                        << (TOP_ROW - MENU_TOP_SHOWN_ROWS));
        } else {
            g_menu_top.rows_shown =
                (1 << MENU_TOP_LAST_ROW) | ((1 << MENU_TOP_SHOWN_ROWS) - 1);
        }
    }
    menu_DrawPageFrame(&frame.ctx, MENU_PANEL_TOP, db);
    menu_DrawTopRows(&frame.ctx, (u16 *)db, g_menu_top.row[TOP_ROW].views);
    menu_DrawPovChoice(&frame.ctx, g_menu_top.row[TOP_ROW].views);
    menu_DrawSideViewChoice(&frame.ctx, g_menu_top.row[TOP_ROW].views);
    if (pressed & PADRdown) {
        switch (g_menu_top.row[TOP_ROW].mode) {
        case FIGHT_MODE_VS:
        case FIGHT_MODE_TEAM:
            if (!g_pad_state.valid[1]) {
                snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
                snd_FlushQueue();
                break;
            }
            /* fall through */
        default:
            snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
            if (g_menu_top.row[TOP_ROW].mode != MENU_ROW_OPTIONS) {
                snd_CloseMenuVab();
            }
            return g_menu_top.row[TOP_ROW].mode;
        }
    } else if (pressed & PAD_OF(PADRdown, 1)) {
        switch (g_menu_top.row[TOP_ROW].mode) {
        case FIGHT_MODE_STORY:
        case FIGHT_MODE_VSCPU:
        case FIGHT_MODE_PRACTICE:
        case FIGHT_MODE_CHANBARA:
        case FIGHT_MODE_NETWORK:
            snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
            break;
        default:
            if (!g_pad_state.valid[0]) {
                snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
                break;
            }
            snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
            if (g_menu_top.row[TOP_ROW].mode != MENU_ROW_OPTIONS) {
                snd_CloseMenuVab();
            }
            return g_menu_top.row[TOP_ROW].mode;
        }
    } else if (pressed & PAD_ANY(PADRup)) {
        snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
        return MENU_TOP_CANCELED;
    }
    if (pressed & PAD_ANY(PADLup | PADLdown)) {
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
    }
    return MENU_TOP_RUNNING;
}

/* ---- what the pages share ---- */

/* The line under a page's title: a gray line with two fainter ones above
 * it. `rule` is its x, y, width and height. */
void menu_DrawTitleRule(MenuCtx *ctx, u16 *rule, s32 ot_index) {
    TILE *tile = ctx->prims.tile;

    SetTile(tile);
    setRGB0(tile, 0xFF, 0xFF, 0xFF); /* white, which the next line undoes */
    setRGB0(tile, 0x80, 0x80, 0x80); /* gray */
    tile->x0 = rule[0];
    tile->y0 = rule[1];
    tile->w = rule[2];
    tile->h = rule[3];
    SetSemiTrans(tile, 0);
    AddPrim(g_disp_ot + ot_index, tile);
    tile++;

    SetTile(tile);
    setRGB0(tile, 0x80, 0x80, 0x80); /* the same gray, see-through */
    tile->x0 = rule[0];
    tile->y0 = rule[1] - 1;
    tile->w = rule[2];
    tile->h = rule[3];
    SetSemiTrans(tile, 1);
    AddPrim(g_disp_ot + ot_index, tile);
    tile++;

    SetTile(tile);
    setRGB0(tile, 0x40, 0x40, 0x40); /* a darker one, see-through */
    tile->x0 = rule[0];
    tile->y0 = rule[1] - 2;
    tile->w = rule[2];
    tile->h = rule[3];
    SetSemiTrans(tile, 1);
    AddPrim(g_disp_ot + ot_index, tile);
    tile++;

    ctx->prims.tile = tile;
}

/* The colour of a page's see-through panel: a slate blue, or gray when the
 * "Monitor" option is black and white. */
void menu_SetPanelColor(TILE *tile) {
    GameSettings *settings = menu_GetSettings();
    s32 level;
    if (settings->grayscale) {
        level = 0x22; /* gray */
        tile->r0 = level;
        tile->g0 = level;
    } else {
        level = 0x4C; /* slate blue: this much red and green */
        tile->r0 = level;
        tile->g0 = level;
        level = 0x6C; /* and this much blue */
    }
    tile->b0 = level;
}

/* The colour of the backdrop's bands: a dark blue, or nearly black. */
void menu_SetBackdropColor(POLY_F4 *quad) {
    GameSettings *settings = menu_GetSettings();
    s32 level;
    if (settings->grayscale) {
        level = 8; /* nearly black */
        quad->r0 = level;
        quad->g0 = level;
    } else {
        level = 0x31; /* dark blue: no red or green */
        quad->r0 = 0;
        quad->g0 = 0;
    }
    quad->b0 = level;
}

/* What every page is drawn over: the panel or panels the page's rows sit
 * on, see-through; the three sheets of the frame; and behind them the
 * backdrop, three pieces of one colour. `db` is not used. */
static void menu_DrawPageFrame(MenuCtx *ctx, s32 panels, GpuDb *db) {
    TILE *tile;
    POLY_F4 *poly;
    SpriteSheetSlot *sheets;
    SpriteSheetSlot *sheet;
    s32 i;
    SpriteDraw draw;

    tile = ctx->prims.tile;

    if (panels == MENU_PANEL_PAIR) {
        SetTile(tile);
        menu_SetPanelColor(tile);
        tile->x0 = PANEL_PAIR_X;
        tile->y0 = MENU_PANEL_Y;
        tile->w = PANEL_PAIR_WIDTH;
        tile->h = MENU_PANEL_HEIGHT;
        SetSemiTrans(tile, 1);
        AddPrim(g_disp_ot + MENU_LAYER_PANEL, tile);
        tile++;
        SetTile(tile);
        menu_SetPanelColor(tile);
        tile->x0 = PANEL_PAIR_X + PLAYER_2_X;
        tile->y0 = MENU_PANEL_Y;
        tile->w = PANEL_PAIR_WIDTH;
        tile->h = MENU_PANEL_HEIGHT;
        SetSemiTrans(tile, 1);
        AddPrim(g_disp_ot + MENU_LAYER_PANEL, tile);
        tile++;
    } else if (panels == MENU_PANEL_WIDE) {
        SetTile(tile);
        menu_SetPanelColor(tile);
        tile->x0 = MENU_PANEL_WIDE_X;
        tile->y0 = MENU_PANEL_Y;
        tile->w = MENU_PANEL_WIDE_WIDTH;
        tile->h = MENU_PANEL_HEIGHT;
        SetSemiTrans(tile, 1);
        AddPrim(g_disp_ot + MENU_LAYER_PANEL, tile);
        tile++;
    } else {
        SetTile(tile);
        menu_SetPanelColor(tile);
        tile->x0 = PANEL_TOP_X;
        tile->y0 = PANEL_TOP_Y;
        tile->w = PANEL_TOP_WIDTH;
        tile->h = PANEL_TOP_HEIGHT;
        SetSemiTrans(tile, 1);
        AddPrim(g_disp_ot + MENU_LAYER_PANEL, tile);
        tile++;
    }
    ctx->prims.tile = tile;
    draw.ot_index = MENU_LAYER_FRAME;
    draw.has_color = 0;
    draw.semi = 0;
    sheets = PTR(ctx->file->frame_sheets);
    draw.x = 0;
    draw.y = 0;
    sheet = sheets;
    for (i = 0; i < 3; i++) {
        draw.header = PTR(*sheet);
        draw.table = draw.header->cells;
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
        sheet++;
    }

    {
        draw.header = PTR(sheets[0]);
        SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    }
    AddPrim(g_disp_ot + MENU_LAYER_FRAME, ctx->prims.mode);

    poly = ctx->prims.f4;
    ctx->prims.mode++;
    SetPolyF4(poly);
    menu_SetBackdropColor(poly);
    /* left of the column */
    setXY4(poly, 0, MENU_BACKDROP_SLANT_LEFT_Y, MENU_BACKDROP_COLUMN_LEFT, 0, 0,
           DISP_HEIGHT - 1, MENU_BACKDROP_COLUMN_LEFT, DISP_HEIGHT - 1);
    SetSemiTrans(poly, 0);
    AddPrim(g_disp_ot + MENU_LAYER_BACKDROP, poly);
    poly++;

    SetPolyF4(poly);
    menu_SetBackdropColor(poly);
    /* right of it */
    poly->x0 = MENU_BACKDROP_COLUMN_RIGHT;
    poly->x1 = DISP_WIDTH - 1;
    poly->y0 = 0;
    poly->y1 = 0;
    poly->x2 = MENU_BACKDROP_COLUMN_RIGHT;
    poly->y2 = DISP_HEIGHT - 1;
    poly->x3 = DISP_WIDTH - 1;
    poly->y3 = MENU_BACKDROP_SLANT_RIGHT_Y;
    SetSemiTrans(poly, 0);
    AddPrim(g_disp_ot + MENU_LAYER_BACKDROP, poly);
    poly++;

    SetPolyF4(poly);
    menu_SetBackdropColor(poly);
    /* the column */
    setXY4(poly, MENU_BACKDROP_COLUMN_LEFT, 0, MENU_BACKDROP_COLUMN_RIGHT, 0,
           MENU_BACKDROP_COLUMN_LEFT, DISP_HEIGHT - 1,
           MENU_BACKDROP_COLUMN_RIGHT, DISP_HEIGHT - 1);
    SetSemiTrans(poly, 0);
    AddPrim(g_disp_ot + MENU_LAYER_BACKDROP, poly);
    poly++;

    ctx->prims.f4 = poly;
}

/* The white page: the three sheets of the menu's text, on white. */
static void menu_DrawWhitePage(MenuCtx *ctx) {
    TILE *tile;
    SpriteSheetSlot *sheets;
    SpriteDraw draw;
    SpriteCell *cells;

    tile = ctx->prims.tile;
    SetTile(tile);
    setRGB0(tile, 0xFF, 0xFF, 0xFF); /* white */
    tile->w = DISP_WIDTH;
    tile->x0 = 0;
    tile->y0 = 0;
    tile->h = DISP_HEIGHT;
    SetSemiTrans(tile, 0);
    AddPrim(g_disp_ot + MENU_LAYER_WHITE, tile);
    ctx->prims.tile = tile + 1;

    sheets = PTR(ctx->file->white_page_sheets);
    draw.ot_index = MENU_LAYER_WHITE_TEXT;
    draw.semi = 0;
    draw.x = 0;
    draw.y = 0;
    draw.has_color = 0;

    draw.header = PTR(sheets[0]);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + MENU_LAYER_PANEL, ctx->prims.mode);
    ctx->prims.mode++;

    cells = draw.header->cells;
    draw.table = cells;
    draw.sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(&draw);

    draw.header = PTR(sheets[1]);
    cells = draw.header->cells;
    draw.table = cells;
    draw.sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(&draw);

    draw.header = PTR(sheets[2]);
    cells = draw.header->cells;
    draw.table = cells;
    draw.sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(&draw);
}

/* ---- the top menu's drawing ---- */

/* The "POV" half of the top menu's choice of view, for a mode that has that
 * view: a picture and its caption. Where the mode has both views the one
 * chosen is drawn full size and glows, with an arrow beside it and the nudge
 * moving it, and the other half size and dim. */
static void menu_DrawPovChoice(MenuCtx *ctx, s32 views) {
    SpriteDraw draw;
    s32 unused[4]; /* never used; the original keeps room for it */
    SpriteSheetSlot *sheets;
    s32 home_x; /* where the half is at rest */
    s32 glow;
    SpriteCell *cells;

    if (views & MENU_VIEW_POV) {
        sheets = PTR(ctx->file->top_sheets);
        draw.header = PTR(sheets[TOP_SHEET_POV_PICTURE]);
        if (views & MENU_VIEW_SIDE) {
            draw.x = VIEW_POV_X;
            draw.has_color = 1;
        } else {
            draw.x = VIEW_X;
            draw.has_color = 0;
        }
        home_x = draw.x;
        if (g_menu_settings->first_person) {
            if (views & MENU_VIEW_SIDE) {
                draw.x = home_x + g_menu_work->nudge[0];
                glow = MENU_PULSE_FROM(0, GLOW_DEPTH, TINT_PLAIN);
                draw.col_b = glow;
                draw.col_g = glow;
                draw.col_r = glow;
            }
            draw.y = 0;
            draw.scale_y = SPRITE_SCALE_ONE;
            draw.scale_x = SPRITE_SCALE_ONE;
        } else {
            draw.col_b = TINT_OTHER_VIEW;
            draw.col_g = TINT_OTHER_VIEW;
            draw.col_r = TINT_OTHER_VIEW;
            draw.scale_y = SPRITE_SCALE_ONE / 2;
            draw.scale_x = SPRITE_SCALE_ONE / 2;
            draw.y = VIEW_SMALL_Y;
        }
        draw.semi = 0;
        draw.ot_index = MENU_LAYER_TOP_POV;
        cells = draw.header->cells;
        draw.table = cells;
        draw.ft4_out = ctx->prims.ft4;
        ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);
        draw.y = 0;
        draw.x = home_x;
        draw.has_color = 0;
        draw.header = PTR(sheets[TOP_SHEET_POV_CAPTION]);
        cells = draw.header->cells;
        draw.table = cells;
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
        if (views & MENU_VIEW_SIDE) {
            draw.ot_index = MENU_LAYER_TOP_SIDE_VIEW;
            draw.semi = 0;
            draw.header = PTR(sheets[TOP_SHEET_ARROWS]);
            draw.x = 0;
            draw.y = 0;
            draw.has_color = 1;
            draw.table = &draw.header->cells[1];
            draw.sprt_out = ctx->prims.sprt;
            ctx->prims.sprt = sprite_Draw(&draw);
        }
        SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
        AddPrim(g_disp_ot + MENU_LAYER_TOP_POV, ctx->prims.mode);
        ctx->prims.mode++;
    }
}

/* The "Side View" half, the same way. */
static void menu_DrawSideViewChoice(MenuCtx *ctx, s32 views) {
    SpriteDraw draw;
    s32 unused[4]; /* never used; the original keeps room for it */
    SpriteSheetSlot *sheets;
    s32 home_x; /* where the half is at rest */
    s32 glow;
    SpriteCell *cells;

    if (views & MENU_VIEW_SIDE) {
        sheets = PTR(ctx->file->top_sheets);
        draw.header = PTR(sheets[TOP_SHEET_SIDE_PICTURE]);
        if (views & MENU_VIEW_POV) {
            draw.x = VIEW_SIDE_X;
            draw.has_color = 1;
        } else {
            draw.x = VIEW_X;
            draw.has_color = 0;
        }
        home_x = draw.x;
        if (!g_menu_settings->first_person) {
            if (views & MENU_VIEW_POV) {
                draw.x = home_x + g_menu_work->nudge[0];
                glow = MENU_PULSE_FROM(0, GLOW_DEPTH, TINT_PLAIN);
                draw.col_b = glow;
                draw.col_g = glow;
                draw.col_r = glow;
            }
            draw.y = 0;
            draw.scale_y = SPRITE_SCALE_ONE;
            draw.scale_x = SPRITE_SCALE_ONE;
        } else {
            draw.x = home_x + VIEW_SIDE_SMALL_X;
            draw.col_b = TINT_OTHER_VIEW;
            draw.col_g = TINT_OTHER_VIEW;
            draw.col_r = TINT_OTHER_VIEW;
            draw.scale_y = SPRITE_SCALE_ONE / 2;
            draw.scale_x = SPRITE_SCALE_ONE / 2;
            draw.y = VIEW_SMALL_Y;
        }
        draw.semi = 0;
        draw.ot_index = MENU_LAYER_TOP_SIDE_VIEW;
        cells = draw.header->cells;
        draw.table = cells;
        draw.ft4_out = ctx->prims.ft4;
        ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);
        draw.y = 0;
        draw.x = home_x;
        draw.has_color = 0;
        draw.header = PTR(sheets[TOP_SHEET_SIDE_CAPTION]);
        cells = draw.header->cells;
        draw.table = cells;
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
        if (views & MENU_VIEW_POV) {
            draw.ot_index = MENU_LAYER_TOP_SIDE_VIEW;
            draw.semi = 0;
            draw.header = PTR(sheets[TOP_SHEET_ARROWS]);
            draw.x = 0;
            draw.y = 0;
            draw.has_color = 1;
            cells = draw.header->cells;
            draw.table = cells;
            draw.sprt_out = ctx->prims.sprt;
            ctx->prims.sprt = sprite_Draw(&draw);
        }
        SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
        AddPrim(g_disp_ot + MENU_LAYER_TOP_SIDE_VIEW, ctx->prims.mode);
        ctx->prims.mode++;
    }
}

/* The modes' names in English, all at once. Nothing calls it. */
static void menu_DrawModeCaptions(MenuCtx *ctx, SpriteDraw *draw) {
    draw->header = PTR(PTR(ctx->file->top_sheets)[TOP_SHEET_MODE_CAPTIONS]);
    draw->x = 0;
    draw->y = 0;
    draw->has_color = 0;
    draw->semi = 0;
    draw->ot_index = MENU_LAYER_TOP_TITLE;
    draw->table = draw->header->cells;
    draw->sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(draw);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw->header, 0), 0);
    AddPrim(g_disp_ot + MENU_LAYER_TOP_TITLE, ctx->prims.mode);
    ctx->prims.mode++;
}

/* The top menu's title. */
static void menu_DrawTopTitle(MenuCtx *ctx, SpriteDraw *draw) {
    draw->header = PTR(PTR(ctx->file->top_sheets)[TOP_SHEET_TITLE]);
    draw->x = 0;
    draw->y = 0;
    draw->has_color = 0;
    draw->semi = 0;
    draw->ot_index = MENU_LAYER_TOP_TITLE;
    draw->scale_x = SPRITE_SCALE_ONE;
    draw->scale_y = SPRITE_SCALE_ONE;
    draw->table = draw->header->cells;
    draw->ft4_out = ctx->prims.ft4;
    ctx->prims.ft4 = sprite_DrawScaled(draw, SPRITE_QUAD_NORMAL);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw->header, 0), 0);
    AddPrim(g_disp_ot + MENU_LAYER_TOP_TITLE, ctx->prims.mode);
    ctx->prims.mode++;
}

/* The line under a row of the top menu: three thin lines that step up to
 * the right and two sheets that fade it out at the ends. It takes its
 * colour from the row's (in `draw`), and is gray under a row the cursor is not
 * on. */
static void menu_DrawRowUnderline(MenuCtx *ctx, SpriteDraw *draw, s32 row) {
    TILE *tile;
    SpriteSheetSlot *sheets;
    s32 abr_bits;

    tile = ctx->prims.tile;
    SetTile(tile);
    /* the lowest line, the longest */
    if (TOP_ROW == row) {
        setRGB0(tile, draw->col_r, 0, draw->col_b);
        abr_bits = 0;
    } else {
        setRGB0(tile, UNDERLINE_GRAY_1, UNDERLINE_GRAY_1, UNDERLINE_GRAY_1);
        abr_bits = SPRITE_ABR_ADD;
    }
    tile->x0 = UNDERLINE_X;
    tile->y0 = draw->y + UNDERLINE_Y;
    tile->w = draw->x + UNDERLINE_PAST_ROW;
    tile->h = 1;
    SetSemiTrans(tile, draw->semi);
    AddPrim(g_disp_ot + draw->ot_index, tile);
    tile++;

    SetTile(tile);
    if (TOP_ROW == row) {
        setRGB0(tile, draw->col_r, 0, draw->col_b);
    } else {
        setRGB0(tile, UNDERLINE_GRAY_2, UNDERLINE_GRAY_2, UNDERLINE_GRAY_2);
    }
    tile->x0 = draw->x;
    tile->y0 = draw->y + UNDERLINE_Y - 1;
    tile->w = UNDERLINE_2_WIDTH;
    tile->h = 1;
    SetSemiTrans(tile, 1);
    AddPrim(g_disp_ot + draw->ot_index, tile);
    tile++;

    SetTile(tile);
    if (TOP_ROW == row) {
        setRGB0(tile, draw->col_r >> 1, 0, draw->col_b >> 1);
    } else {
        setRGB0(tile, UNDERLINE_GRAY_3, UNDERLINE_GRAY_3, UNDERLINE_GRAY_3);
    }
    tile->x0 = draw->x + UNDERLINE_3_X;
    tile->y0 = draw->y + UNDERLINE_Y - 2;
    tile->w = UNDERLINE_3_WIDTH;
    tile->h = 1;
    SetSemiTrans(tile, 1);
    AddPrim(g_disp_ot + draw->ot_index, tile);

    tile = tile + 1;
    ctx->prims.tile = tile;
    sheets = PTR(ctx->file->top_sheets);
    {
        SpriteCell *cells;
        /* FAKE: draw->header, through a pointer */
        SpriteSheet **header = &draw->header;

        *header = PTR(sheets[TOP_SHEET_UNDERLINE_LEFT]);
        if (TOP_ROW == row) {
            draw->col_g = 0;
            draw->col_r = draw->col_r >> 1;
            draw->col_b = draw->col_b >> 1;
        } else {
            draw->col_b = UNDERLINE_GRAY_ENDS;
            draw->col_g = UNDERLINE_GRAY_ENDS;
            draw->col_r = UNDERLINE_GRAY_ENDS;
        }

        draw->x = 0;
        cells = draw->header->cells;
        draw->y += UNDERLINE_Y;
        draw->table = cells;

        draw->sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(draw);

        draw->header = PTR(sheets[TOP_SHEET_UNDERLINE_RIGHT]);
        cells = draw->header->cells;
        draw->table = cells;
        draw->sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(draw);
    }

    SetDrawMode(
        ctx->prims.mode, 1, 0, sprite_GetTPage(draw->header, abr_bits), 0);
    AddPrim(g_disp_ot + draw->ot_index, ctx->prims.mode);
    ctx->prims.mode++;
}

/* The top menu's rows. The seven modes scroll in a window six rows high
 * (the draw area and offset are set around them; draw_env is the frame's
 * drawing environment, for the values to put back); the cursor's row glows
 * and is nudged, a row that can be chosen is lit and one that cannot is
 * dark. Then, not scrolled: the name in English of the cursor's row, the
 * last row (the options), the title, and the cursor row's picture on a
 * black field. `views` is not used. */
static void menu_DrawTopRows(MenuCtx *ctx, u16 *draw_env, s32 views) {
    SpriteDraw draw;
    RECT rect;
    s16 ofs[2];
    /* one list of sheets after another */
    SpriteSheetSlot *sheets;
    /* the rows in view, then the rows that can be chosen */
    u32 row_mask;
    SpriteCell *cells;
    s32 scroll;
    s32 bit;
    s32 row;

    sheets = PTR(ctx->file->row_sheets);
    row_mask = g_menu_top.rows_shown;
    bit = 0;
    draw.header = PTR(sheets[0]);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + MENU_LAYER_BARS, ctx->prims.mode);
    ctx->prims.mode++;
    rect.x = draw_env[ENV_CLIP_X];
    rect.y = draw_env[ENV_CLIP_Y] + TOP_WINDOW_Y;
    rect.w = TOP_WINDOW_WIDTH;
    rect.h = TOP_WINDOW_HEIGHT;
    SetDrawArea(ctx->prims.area, &rect);
    AddPrim(g_disp_ot + MENU_LAYER_BARS, ctx->prims.area);
    ctx->prims.area++;
    /* up by the rows above the first in view */
    scroll = 0;
    for (; !(row_mask & (1 << bit)); bit++) {
        scroll -= TOP_ROW_PITCH;
    }
    ofs[0] = draw_env[ENV_OFS_X];
    ofs[1] = draw_env[ENV_OFS_Y] + scroll;
    SetDrawOffset(ctx->prims.offset, ofs);
    AddPrim(g_disp_ot + MENU_LAYER_BARS, ctx->prims.offset);
    ctx->prims.offset++;

    row_mask = g_menu_top.rows_on;
    draw.ot_index = MENU_LAYER_TOP_ROWS;
    for (row = 0; row < MENU_TOP_LAST_ROW; row++) {
        s32 y = row * TOP_ROW_PITCH + TOP_ROWS_Y;

        draw.x = TOP_ROWS_X;
        draw.has_color = 1;
        if (TOP_ROW == row) {
            draw.semi = 0;
            draw.col_r = ROW_GLOW_R;
            draw.col_g = ROW_GLOW_G;
            draw.col_b = ROW_B;
            draw.y = g_menu_work->nudge[1] + y;
            g_menu_pulse++;
        } else if ((row_mask >> row) & 1) {
            draw.col_r = ROW_R;
            draw.col_g = ROW_G;
            draw.col_b = ROW_B;
            draw.semi = 1;
            draw.y = y;
            if (g_menu_settings->grayscale) {
                draw.has_color = 0;
            }
        } else {
            draw.col_r = ROW_OFF;
            draw.col_g = ROW_OFF;
            draw.col_b = ROW_OFF;
            draw.semi = 1;
            draw.y = y;
        }
        draw.header = PTR(sheets[row]);
        cells = draw.header->cells;
        draw.table = cells;
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
        SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
        AddPrim(g_disp_ot + draw.ot_index, ctx->prims.mode);
        ctx->prims.mode++;
        draw.has_color = 1;
        menu_DrawRowUnderline(ctx, &draw, row);
    }

    sheets = PTR(ctx->file->mode_caption_sheets);
    draw.ot_index = MENU_LAYER_PAGE;
    draw.has_color = 0;
    draw.semi = 0;
    draw.x = 0;
    draw.y = TOP_CAPTION_Y;
    draw.header = PTR(sheets[TOP_ROW]);
    cells = draw.header->cells;
    draw.table = cells;
    draw.sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(&draw);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + draw.ot_index, ctx->prims.mode);
    ctx->prims.mode++;

    sheets = PTR(ctx->file->row_sheets);
    rect.x = draw_env[ENV_CLIP_X];
    rect.y = draw_env[ENV_CLIP_Y];
    rect.w = draw_env[ENV_CLIP_W];
    rect.h = draw_env[ENV_CLIP_H];
    SetDrawArea(ctx->prims.area, &rect);
    AddPrim(g_disp_ot + MENU_LAYER_PAGE, ctx->prims.area);
    ctx->prims.area++;
    ofs[0] = draw_env[ENV_OFS_X];
    ofs[1] = draw_env[ENV_OFS_Y];
    SetDrawOffset(ctx->prims.offset, ofs);
    AddPrim(g_disp_ot + MENU_LAYER_PAGE, ctx->prims.offset);
    /* FAKE: ctx->prims.offset++, not as a member */
    *(s32 *)&ctx->prims.offset += sizeof(DR_OFFSET);

    draw.x = TOP_ROWS_X;
    draw.has_color = 1;
    if (TOP_ROW == MENU_TOP_LAST_ROW) {
        draw.semi = 0;
        draw.col_r = ROW_GLOW_R;
        draw.col_g = ROW_GLOW_G;
        draw.col_b = ROW_B;
        draw.y = g_menu_work->nudge[1] + TOP_LAST_ROW_Y;
        g_menu_pulse++;
    } else if ((row_mask >> MENU_TOP_LAST_ROW) & 1) {
        draw.col_r = ROW_R;
        draw.col_g = ROW_G;
        draw.col_b = ROW_B;
        draw.semi = 1;
        draw.y = TOP_LAST_ROW_Y;
        if (g_menu_settings->grayscale) {
            draw.has_color = 0;
        }
    } else {
        draw.col_r = ROW_OFF;
        draw.col_g = ROW_OFF;
        draw.col_b = ROW_OFF;
        draw.semi = 1;
        draw.y = TOP_LAST_ROW_Y;
    }
    draw.ot_index = MENU_LAYER_TOP_LAST_ROW;
    draw.header = PTR(sheets[MENU_TOP_LAST_ROW]);
    cells = draw.header->cells;
    draw.table = cells;
    draw.sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(&draw);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + draw.ot_index, ctx->prims.mode);
    ctx->prims.mode++;
    draw.has_color = 1;
    menu_DrawRowUnderline(ctx, &draw, MENU_TOP_LAST_ROW);
    menu_DrawTopTitle(ctx, &draw);

    draw.has_color = 0;
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + MENU_LAYER_TOP_TITLE, ctx->prims.mode);
    ctx->prims.mode++;
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + MENU_LAYER_TOP_TITLE, ctx->prims.mode);
    ctx->prims.mode++;

    sheets = PTR(ctx->file->mode_picture_sheets);
    draw.header = PTR(sheets[TOP_ROW + MENU_TOP_ROWS]);
    draw.x = 0;
    draw.y = TOP_PICTURE_Y;
    draw.has_color = 0;
    draw.semi = 0;
    draw.ot_index = MENU_LAYER_TOP_PICTURE;
    cells = draw.header->cells;
    draw.table = cells;
    draw.sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(&draw);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot, ctx->prims.mode);
    ctx->prims.mode++;

    sheets = PTR(ctx->file->mode_picture_sheets);
    draw.header = PTR(sheets[TOP_ROW]);
    draw.scale_x = 2 * SPRITE_SCALE_ONE;
    draw.x = 0;
    draw.y = 0;
    draw.scale_y = SPRITE_SCALE_ONE;
    draw.has_color = 0;
    draw.semi = 0;
    draw.ot_index = MENU_LAYER_TOP_PICTURE;
    cells = draw.header->cells;
    draw.table = cells;
    draw.ft4_out = ctx->prims.ft4;
    ctx->prims.ft4 = sprite_DrawScaled(&draw, SPRITE_QUAD_NORMAL);

    SetTile(ctx->prims.tile);
    (ctx->prims.tile)->r0 = 0;
    (ctx->prims.tile)->g0 = 0;
    (ctx->prims.tile)->b0 = 0;
    (ctx->prims.tile)->x0 = TOP_FIELD_X;
    (ctx->prims.tile)->y0 = TOP_FIELD_Y;
    (ctx->prims.tile)->w = TOP_FIELD_WIDTH;
    (ctx->prims.tile)->h = TOP_FIELD_HEIGHT;
    SetSemiTrans(ctx->prims.tile, 0);
    AddPrim(g_disp_ot + MENU_LAYER_TOP_TITLE, ctx->prims.tile);
    ctx->prims.tile++;

    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot, ctx->prims.mode);
    ctx->prims.mode++;
}

/* ---- the options ---- */

/* The options page: its title, the six rows with the cursor's glowing, and
 * beside the first three the two values of each: the one set is solid, and
 * glows and is nudged when the cursor is on its row; the other is
 * see-through. */
static void menu_DrawOptions(MenuCtx *ctx) {
    SpriteDraw draw;
    u16 rule[4];
    SpriteSheetSlot *sheets;
    s32 i;
    SpriteCell *cells;

    draw.has_color = 0;
    draw.ot_index = MENU_LAYER_PAGE;
    sheets = PTR(ctx->file->option_sheets);
    draw.header = PTR(sheets[OPTION_SHEET_TITLE]);
    draw.y = 0;
    draw.x = 0;
    draw.semi = 0;
    cells = draw.header->cells;
    draw.table = cells;
    draw.sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(&draw);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + MENU_LAYER_PAGE, ctx->prims.mode);
    ctx->prims.mode++;
    draw.x = 0;
    for (i = 0; i < OPTIONS_ROWS; i++) {
        draw.header = PTR(sheets[i + OPTION_SHEET_ROWS]);
        if (OPTIONS_ROW == i) {
            draw.has_color = 1;
            draw.y = g_menu_work->nudge[1];
            draw.semi = 0;
            draw.col_r = draw.col_g = draw.col_b =
                MENU_PULSE(GLOW_DEPTH, TINT_PLAIN);
        } else {
            draw.has_color = 0;
            draw.y = 0;
            draw.semi = 1;
        }
        cells = draw.header->cells;
        draw.table = cells;
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
    }

    draw.y = 0;
    for (i = 0; i < 2; i++) {
        draw.header = PTR(sheets[i + OPTION_SHEET_MONITOR]);
        draw.x = 0;
        draw.has_color = 0;
        if (g_menu_settings->grayscale == i) {
            draw.semi = 0;
            if (!(g_menu_cursors & CURSOR_OPTIONS)) {
                s32 glow;

                draw.has_color = 1;
                draw.x = g_menu_work->nudge[0];
                glow = MENU_PULSE(GLOW_DEPTH_SMALL, TINT_PLAIN);
                draw.col_b = glow;
                draw.col_g = glow;
                draw.col_r = glow;
            }
        } else {
            draw.semi = 1;
        }
        cells = draw.header->cells;
        draw.table = cells;
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
    }

    for (i = 0; i < 2; i++) {
        draw.header = PTR(sheets[i + OPTION_SHEET_DIFFICULTY]);
        draw.x = 0;
        draw.has_color = 0;
        if (g_menu_settings->easy_cpu == i) {
            draw.semi = 0;
            if ((g_menu_cursors & CURSOR_OPTIONS) ==
                (OPTIONS_ROW_DIFFICULTY << CURSOR_OPTIONS_SHIFT)) {
                s32 glow;

                draw.has_color = 1;
                draw.x = g_menu_work->nudge[0];
                glow = MENU_PULSE(GLOW_DEPTH_SMALL, TINT_PLAIN);
                draw.col_b = glow;
                draw.col_g = glow;
                draw.col_r = glow;
            }
        } else {
            draw.semi = 1;
        }
        cells = draw.header->cells;
        draw.table = cells;
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
    }

    for (i = 0; i < 2; i++) {
        draw.header = PTR(sheets[i + OPTION_SHEET_MUSIC]);
        draw.x = 0;
        draw.has_color = 0;
        if (g_menu_settings->music_off == i) {
            draw.semi = 0;
            if ((g_menu_cursors & CURSOR_OPTIONS) ==
                (OPTIONS_ROW_MUSIC << CURSOR_OPTIONS_SHIFT)) {
                s32 glow;

                draw.has_color = 1;
                draw.x = g_menu_work->nudge[0];
                glow = MENU_PULSE(GLOW_DEPTH_SMALL, TINT_PLAIN);
                draw.col_b = glow;
                draw.col_g = glow;
                draw.col_r = glow;
            }
        } else {
            draw.semi = 1;
        }
        cells = draw.header->cells;
        draw.table = cells;
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
    }

    sheets = PTR(ctx->file->option_sheets);
    draw.header = PTR(sheets[OPTION_SHEET_ROWS]);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + MENU_LAYER_PAGE, ctx->prims.mode);
    ctx->prims.mode++;
    rule[2] = OPTIONS_RULE_WIDTH;
    rule[0] = OPTIONS_RULE_X;
    rule[1] = MENU_TITLE_RULE_Y;
    rule[3] = 1;
    menu_DrawTitleRule(ctx, rule, MENU_LAYER_PANEL);
}

/* The options page's pad: up and down move the cursor, left or right
 * changes the option it is on, cross goes on down the page from an option
 * or opens the row's page. Triangle leaves. `held` is not used. */
static s32 menu_HandleOptions(s32 *held, u32 *pressed) {
    u32 buttons;
    s32 either_pad;
    s32 events;
    s32 result = OPTION_MENU_RESULT_NONE;

    buttons = *pressed;
    either_pad = (buttons & 0xFFFF) | (buttons >> PAD_BITS);
    events =
        menu_StepNudge(&either_pad, 0, g_menu_work->nudge, g_menu_nudge_steps);
    switch (NUDGE_DOWN(events)) {
    case NUDGE_PLUS:
        if ((g_menu_cursors & CURSOR_OPTIONS) ==
            (OPTIONS_ROW_EXIT << CURSOR_OPTIONS_SHIFT)) {
            g_menu_cursors &= ~CURSOR_OPTIONS;
        } else {
            g_menu_cursors =
                (g_menu_cursors & ~CURSOR_OPTIONS) |
                ((OPTIONS_ROW + 1) & CURSOR_ROW_MASK) << CURSOR_OPTIONS_SHIFT;
        }
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
        g_menu_pulse = 0;
        break;
    case NUDGE_MINUS:
        if ((g_menu_cursors & CURSOR_OPTIONS) == 0) {
            g_menu_cursors = (g_menu_cursors & ~CURSOR_OPTIONS) |
                             (OPTIONS_ROW_EXIT << CURSOR_OPTIONS_SHIFT);
        } else {
            g_menu_cursors =
                (g_menu_cursors & ~CURSOR_OPTIONS) |
                ((OPTIONS_ROW - 1) & CURSOR_ROW_MASK) << CURSOR_OPTIONS_SHIFT;
        }
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
        g_menu_pulse = 0;
        break;
    }

    switch (OPTIONS_ROW) {
    case OPTIONS_ROW_MONITOR:
        if (NUDGE_ACROSS(events) != 0) {
            g_menu_settings->grayscale ^= 1;
            snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
        }
        if (*pressed & PAD_ANY(PADRdown)) {
            snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
            g_menu_cursors =
                (g_menu_cursors & ~CURSOR_OPTIONS) |
                ((OPTIONS_ROW + 1) & CURSOR_ROW_MASK) << CURSOR_OPTIONS_SHIFT;
        }
        break;
    case OPTIONS_ROW_DIFFICULTY:
        if (NUDGE_ACROSS(events) != 0) {
            g_menu_settings->easy_cpu ^= 1;
            snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
        }
        if (*pressed & PAD_ANY(PADRdown)) {
            snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
            g_menu_cursors =
                (g_menu_cursors & ~CURSOR_OPTIONS) |
                ((OPTIONS_ROW + 1) & CURSOR_ROW_MASK) << CURSOR_OPTIONS_SHIFT;
        }
        break;
    case OPTIONS_ROW_MUSIC:
        if (NUDGE_ACROSS(events) != 0) {
            g_menu_settings->music_off ^= 1;
            snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
        }
        if (*pressed & PAD_ANY(PADRdown)) {
            snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
            g_menu_cursors =
                (g_menu_cursors & ~CURSOR_OPTIONS) |
                ((OPTIONS_ROW + 1) & CURSOR_ROW_MASK) << CURSOR_OPTIONS_SHIFT;
        }
        break;
    case OPTIONS_ROW_MEMCARD:
        if (*pressed & PAD_ANY(PADRdown)) {
            snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
            /* the memory card menu opens with its cursor on "back" */
            g_menu_cursors = (g_menu_cursors & ~CURSOR_CARD) |
                             (CARD_MENU_ROW_BACK << CURSOR_CARD_SHIFT);
            result = OPTION_MENU_RESULT_MEMCARD;
        }
        break;
    case OPTIONS_ROW_KEY_CONFIG:
        if (*pressed & PAD_ANY(PADRdown)) {
            snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
            result = OPTION_MENU_RESULT_KEY_CONFIG;
        }
        break;
    case OPTIONS_ROW_EXIT:
        if (*pressed & PAD_ANY(PADRdown)) {
            snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
            result = OPTION_MENU_RESULT_EXIT;
        }
        break;
    }

    if (*pressed & PAD_ANY(PADRup)) {
        snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
        result = OPTION_MENU_RESULT_EXIT;
    }
    return result;
}

/* A frame of the options page. */
s32 menu_UpdateOptions(s32 held, u32 pressed) {
    MenuCtx ctx;
    GpuDb *db;
    g_menu_pulse += 1;
    db = &g_disp_db[g_disp_frame_count & 1];
    menu_BeginDraw(&ctx, &g_menu_frame);
    menu_DrawPageFrame(&ctx, MENU_PANEL_WIDE, db);
    menu_DrawOptions(&ctx);
    return menu_HandleOptions(&held, &pressed);
}

/* ---- the memory card menu ---- */

/* The memory card menu's pad: load, save or back. Going back puts the
 * options' cursor on the row after "Memory Card". `held` is not used. */
static s32 menu_HandleCardMenu(s32 *held, u32 *pressed) {
    u32 buttons;
    s32 either_pad;
    s32 events;
    s32 row;
    s32 result = MEMCARD_MENU_RESULT_NONE;

    buttons = *pressed;
    either_pad = (buttons & 0xFFFF) | (buttons >> PAD_BITS);
    events =
        menu_StepNudge(&either_pad, 0, g_menu_work->nudge, g_menu_nudge_steps);
    switch (NUDGE_DOWN(events)) {
    case NUDGE_PLUS:
        if ((g_menu_cursors & CURSOR_CARD) ==
            (CARD_MENU_ROW_BACK << CURSOR_CARD_SHIFT)) {
            g_menu_cursors &= ~CURSOR_CARD;
        } else {
            g_menu_cursors =
                (g_menu_cursors & ~CURSOR_CARD) |
                ((CARD_MENU_ROW + 1) & CURSOR_ROW_MASK) << CURSOR_CARD_SHIFT;
        }
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
        break;
    case NUDGE_MINUS:
        if ((g_menu_cursors & CURSOR_CARD) == 0) {
            g_menu_cursors = (g_menu_cursors & ~CURSOR_CARD) |
                             (CARD_MENU_ROW_BACK << CURSOR_CARD_SHIFT);
        } else {
            g_menu_cursors =
                (g_menu_cursors & ~CURSOR_CARD) |
                ((CARD_MENU_ROW - 1) & CURSOR_ROW_MASK) << CURSOR_CARD_SHIFT;
        }
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
        break;
    }

    row = CARD_MENU_ROW;
    switch (row) {
    case CARD_MENU_ROW_LOAD:
        if (*pressed & PAD_ANY(PADRdown)) {
            snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
            result = MEMCARD_MENU_RESULT_LOAD;
        }
        break;
    case CARD_MENU_ROW_SAVE:
        if (*pressed & PAD_ANY(PADRdown)) {
            snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
            result = MEMCARD_MENU_RESULT_SAVE;
        }
        break;
    case CARD_MENU_ROW_BACK:
        if (*pressed & PAD_ANY(PADRdown)) {
            snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
            result = MEMCARD_MENU_RESULT_BACK;
            g_menu_cursors =
                (g_menu_cursors & ~CURSOR_OPTIONS) |
                (((OPTIONS_ROW + 1) & CURSOR_ROW_MASK) << CURSOR_OPTIONS_SHIFT);
        }
        break;
    }

    if (*pressed & PAD_ANY(PADRup)) {
        snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
        result = MEMCARD_MENU_RESULT_BACK;
    }
    return result;
}

/* The memory card menu: its title and three rows. */
static void menu_DrawCardMenu(MenuCtx *ctx) {
    SpriteDraw draw;
    u16 rule[4];
    s32 row;
    SpriteSheetSlot *sheets;
    SpriteCell *cells;

    draw.y = 0;
    draw.x = 0;
    draw.ot_index = MENU_LAYER_PAGE;
    draw.has_color = 0;
    sheets = PTR(ctx->file->card_menu_sheets);
    {
        draw.header = PTR(sheets[0]);
        draw.semi = 0;
        cells = draw.header->cells;
        draw.table = cells;
    }
    draw.sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(&draw);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + MENU_LAYER_PAGE, ctx->prims.mode);
    ctx->prims.mode++;

    for (row = 0; row < CARD_MENU_ROWS; row++) {
        draw.header = PTR(sheets[1]);
        cells = draw.header->cells;
        draw.table = cells;
        if (CARD_MENU_ROW == row) {
            draw.y = g_menu_work->nudge[1];
            draw.semi = 0;
        } else {
            draw.y = 0;
            draw.semi = 1;
        }
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
        sheets++;
    }

    sheets = PTR(ctx->file->option_sheets);
    draw.header = PTR(sheets[OPTION_SHEET_ROWS]);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + MENU_LAYER_PAGE, ctx->prims.mode);
    ctx->prims.mode++;

    rule[2] = OPTIONS_RULE_WIDTH;
    rule[0] = OPTIONS_RULE_X;
    rule[1] = MENU_TITLE_RULE_Y;
    rule[3] = 1;
    menu_DrawTitleRule(ctx, rule, MENU_LAYER_PANEL);
}

/* ---- the message box ---- */

/* The words of message `message`, `y` down from the middle of the screen:
 * one or two sheets. The answers are three cells of one sheet, of which the
 * one not chosen is darkened. */
static void menu_DrawMessageText(
    s32 message, s32 y, SpriteDraw *draw, s32 answer) {
    SpriteSheetSlot *sheets;
    SpriteCell *cells;
    s32 i, cell_index, cell_count;

    sheets = PTR(PTR(g_menu_work->file)->message_sheets);

    draw->col_b = TINT_ANSWER_OFF;
    draw->col_g = TINT_ANSWER_OFF;
    draw->col_r = TINT_ANSWER_OFF;

    for (i = 0; i < 2; i++) {
        /* FAKE: sheets[message * 2 + i], added in another order */
        draw->header = PTR(*(sheets + i + message * 2));
        if (draw->header == (SpriteSheet *)ADDR(-1)) {
            return;
        }

        cell_count = (message == MSG_ANSWERS) ? MESSAGE_ANSWER_CELLS : 1;

        for (cell_index = 0; cell_index < cell_count; cell_index++) {
            draw->x = 0;
            draw->y = y;
            draw->ot_index = MENU_LAYER_FRONT;
            if (message != MSG_ANSWERS || cell_index == answer ||
                cell_index == 2) {
                draw->has_color = 0;
            } else {
                draw->has_color = 1;
            }
            draw->semi = 0;
            draw->sprt_out = (s32)g_menu_message_sprt;
            cells = draw->header->cells;
            draw->table = cells + cell_index;
            g_menu_message_sprt = (u8 *)ADDR(sprite_Draw(draw));
        }

        SetDrawMode(
            g_menu_message_mode, 1, 0, sprite_GetTPage(draw->header, 0), 0);
        AddPrim(g_disp_ot + MENU_LAYER_FRONT, g_menu_message_mode);
        g_menu_message_mode++;
    }
}

/* A message in a box in the middle of the screen, over whatever is there.
 * With an answer (0 yes, 1 no; -1 for a message that asks nothing) the box
 * is taller, the answers are under the words and the chosen one is
 * underlined in red. */
void menu_DrawMessageBox(s32 message, s32 answer) {
    SpriteDraw draw;
    s32 frame;
    ScreenSize *size;
    s16 i;
    s32 red;
    s32 w; /* the underline's */
    s32 h; /* the answers' row's */
    s32 x0;

    g_menu_message_choice_y = 0;
    frame = g_disp_frame_count & 1;
    g_menu_message_sprt = g_menu_message_sprts + frame * MESSAGE_SPRT_BYTES;
    g_menu_message_mode = g_menu_message_modes + frame * MESSAGE_PRIMS;
    g_menu_message_tile = g_menu_message_tiles + frame * MESSAGE_PRIMS;
    menu_DrawMessageText(message, 0, &draw, 0);
    h = 0;
    size = PTR(PTR(g_menu_work->file)->message_sizes);
    size += message;
    if (answer != -1) {
        w = answer ? MESSAGE_NO_WIDTH : MESSAGE_YES_WIDTH;
        h = MESSAGE_ANSWERS_HEIGHT;
        g_menu_message_choice_y = (size->h + MESSAGE_ANSWERS_HEIGHT) >> 1;
        menu_DrawMessageText(
            MSG_ANSWERS, g_menu_message_choice_y, &draw, answer);
        x0 = (answer & 1) * MESSAGE_NO_X_STEP + MESSAGE_YES_X;
        for (i = 0; i < 3; i++) {
            SetTile(g_menu_message_tile);
            /* a bright line, and over it a see-through one as bright and
             * one half as bright */
            if (i == 0) {
                red = 0xFF;
                SetSemiTrans(g_menu_message_tile, 0);
            } else {
                red = 0xFF - ((i - 1) << 7);
                SetSemiTrans(g_menu_message_tile, 1);
            }
            setRGB0(g_menu_message_tile, red, 0, 0);
            g_menu_message_tile->y0 =
                g_menu_message_choice_y + MESSAGE_UNDERLINE_Y - i;
            g_menu_message_tile->x0 = x0;
            g_menu_message_tile->w = w;
            g_menu_message_tile->h = 1;
            AddPrim(g_disp_ot + MENU_LAYER_FRONT, g_menu_message_tile);
            g_menu_message_tile++;
        }
    }
    SetTile(g_menu_message_tile);
    setRGB0(g_menu_message_tile, 0, 0, 0);
    g_menu_message_tile->x0 = DISP_WIDTH / 2 - (size->w >> 1);
    g_menu_message_tile->y0 = DISP_HEIGHT / 2 - (size->h >> 1);
    g_menu_message_tile->w = size->w;
    g_menu_message_tile->h = size->h + h;
    SetSemiTrans(g_menu_message_tile, 1);
    AddPrim(g_disp_ot + MENU_LAYER_FRONT, g_menu_message_tile);
    g_menu_message_tile++;
}

/* A frame of the memory card menu. */
static s32 menu_UpdateCardMenuPage(s32 held, s32 pressed) {
    MenuCtx ctx;
    SpriteDraw unused; /* never used; the original keeps room for it */
    GpuDb *db;
    g_menu_pulse += 1;
    db = &g_disp_db[g_disp_frame_count & 1];
    menu_BeginDraw(&ctx, &g_menu_frame);
    menu_DrawPageFrame(&ctx, MENU_PANEL_WIDE, db);
    menu_DrawCardMenu(&ctx);
    return menu_HandleCardMenu(&held, &pressed);
}

/* The same. The memory-card screens call it with no buttons, to have the
 * page drawn behind their messages. */
s32 menu_UpdateCardMenu(s32 held, s32 pressed) {
    return menu_UpdateCardMenuPage(held, pressed);
}

/* ---- the key configuration ---- */

/* The key configuration page, the same on the left for the first player
 * and on the right for the second: the title; on a picture of the pad, lit,
 * the buttons of the action the player's cursor is on; the rows' names; the
 * coloured bars behind the rows; and round the cursor's row a frame that
 * glows red, or white on the last row. */
static void menu_DrawKeyConfig(MenuCtx *ctx) {
    SpriteDraw draw;
    s32 player_index;
    SpriteSheetSlot *sheets;
    ColorRect *rects;
    ColorRect *rect;
    ColorRect *bar;
    TILE *tile;
    POLY_G4 *poly;
    s32 abr;
    /* a loop counter, and then the row the player's cursor is on */
    s32 i;
    /* FAKE: a constant kept in a local */
    s32 far_red;
    /* FAKE: zeroes kept in 16-bit locals */
    s16 zero_dither;
    s16 xpos;
    s16 ypos;
    s16 zero_window;
    s32 player;
    s32 x;
    s32 edge; /* 0: the top and the left; 1: the bottom and the right */
    s32 glow;
    SpriteCell *cells;

    abr = 0;
    zero_dither = 0;
    xpos = 0;
    ypos = 0;
    zero_window = 0;
    draw.ot_index = MENU_LAYER_PAGE;
    draw.has_color = 0;
    sheets = PTR(ctx->file->key_config_sheets);
    draw.y = ypos;
    draw.x = xpos;
    draw.header = PTR(sheets[KEY_SHEET_TITLE]);
    draw.semi = 0;
    cells = draw.header->cells;
    draw.table = cells;
    draw.sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(&draw);
    SetDrawMode(
        ctx->prims.mode, 1, zero_dither, sprite_GetTPage(draw.header, abr),
        (RECT *)ADDR((s32)zero_window));
    AddPrim(g_disp_ot + MENU_LAYER_PAGE, ctx->prims.mode);
    ctx->prims.mode++;

    draw.ot_index = MENU_LAYER_KEY_PAD_BUTTONS;
    draw.has_color = 0;
    sheets = PTR(ctx->file->key_config_sheets);
    draw.y = ypos;
    for (player = 0; player < 2; player++) {
        draw.x = player * PLAYER_2_X;
        if (g_menu_work->key_cursors.half[player] < MENU_ACTIONS) {
            for (i = 0; i < MENU_BUTTONS; i++) {
                if (g_menu_settings->button_mappings[g_menu_work->key_cursors
                                                         .half[player]] &
                    ((1 << i) << (player * MENU_BUTTONS))) {
                    draw.header = PTR(sheets[i + KEY_SHEET_PAD_BUTTONS]);
                    draw.semi = 0;
                    cells = draw.header->cells;
                    draw.table = cells;
                    draw.sprt_out = ctx->prims.sprt;
                    ctx->prims.sprt = sprite_Draw(&draw);
                }
            }
        }
    }
    draw.header = PTR(sheets[KEY_SHEET_PAD_BUTTONS]);
    SetDrawMode(
        ctx->prims.mode, 1, zero_dither, sprite_GetTPage(draw.header, abr),
        (RECT *)ADDR((s32)zero_window));
    AddPrim(g_disp_ot + MENU_LAYER_KEY_PAD_BUTTONS, ctx->prims.mode);
    ctx->prims.mode++;

    draw.x = xpos;
    draw.y = ypos;
    draw.ot_index = MENU_LAYER_PAGE;
    draw.has_color = 0;
    sheets = PTR(ctx->file->key_config_sheets);
    draw.header = PTR(sheets[KEY_SHEET_PAD]);
    draw.semi = 0;
    cells = draw.header->cells;
    draw.table = cells;
    draw.sprt_out = ctx->prims.sprt;
    ctx->prims.sprt = sprite_Draw(&draw);
    for (i = 0; i < KEY_ROWS; i++) {
        draw.header = PTR(sheets[i + KEY_SHEET_ROWS]);
        draw.ot_index = MENU_LAYER_PAGE;
        draw.has_color = 0;
        draw.semi = 0;
        draw.y = 0;
        cells = draw.header->cells;
        draw.table = cells;
        for (player_index = 0; player_index < 2; player_index++) {
            draw.x = player_index ? PLAYER_2_X : 0;
            draw.sprt_out = ctx->prims.sprt;
            ctx->prims.sprt = sprite_Draw(&draw);
        }
    }
    sheets = PTR(ctx->file->key_config_sheets);
    draw.header = PTR(sheets[KEY_SHEET_PAD]);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, abr), 0);
    AddPrim(g_disp_ot + MENU_LAYER_PAGE, ctx->prims.mode);
    ctx->prims.mode++;

    rects = PTR(PTR(g_menu_work->file)->key_config_rects);
    tile = ctx->prims.tile;
    for (i = 0; i < KEY_BARS; i++) {
        bar = &rects[i];
        for (player_index = 0; player_index < 2; player_index++) {
            SetTile(tile);
            setRGB0(tile, bar->r, bar->g, bar->b);
            tile->x0 = bar->x + player_index * PLAYER_2_X;
            tile->y0 = bar->y;
            tile->w = bar->w;
            tile->h = bar->h;
            SetSemiTrans(tile, 0);
            AddPrim(g_disp_ot + MENU_LAYER_BARS, tile);
            tile++;
        }
    }
    ctx->prims.tile = tile;

    /* 32 either side of 208, once round in 32 frames */
    glow = ((rcos((g_menu_frame << 7) & 0xF80) * 32) >> 12) + 0xD0;
    poly = ctx->prims.g4;
    x = 0;
    for (player_index = 0; player_index < 2; player_index++) {
        i = g_menu_work->key_cursors.half[player_index];
        rect = &rects[i + 1];
        for (edge = 0; edge < 2; edge++) {
            /* the top of the frame or its foot: bright outside, fading
             * inward; thicker and white on the last row */
            SetPolyG4(poly);
            if (i == KEY_ROW_EXIT) {
                SetSemiTrans(poly, 1);
                far_red = 0;
                setRGB0(poly, glow, glow, glow);
                setRGB1(poly, glow, glow, glow);
                setRGB2(poly, far_red, 0, 0);
                setRGB3(poly, far_red, 0, 0);
                setXYWH(poly, rect[edge].x + x, rect[edge].y + 1 - edge,
                        rect[edge].w, -4 * edge + 2);
            } else {
                SetSemiTrans(poly, 0);
                far_red = KEY_FRAME_FAR_RED;
                setRGB0(poly, glow, 0, 0);
                setRGB1(poly, glow, 0, 0);
                setRGB2(poly, far_red, 0, 0);
                setRGB3(poly, far_red, 0, 0);
                setXYWH(poly, rect[edge].x + x, rect[edge].y + 1 - edge,
                        rect[edge].w, -2 * edge + 1);
            }
            AddPrim(g_disp_ot + MENU_LAYER_FRONT, poly);
            poly++;
            /* its left side or its right, the same way */
            SetPolyG4(poly);
            if (i == KEY_ROW_EXIT) {
                SetSemiTrans(poly, 1);
                far_red = 0;
                setRGB0(poly, glow, glow, glow);
                setRGB2(poly, glow, glow, glow);
                setRGB1(poly, far_red, 0, 0);
                setRGB3(poly, far_red, 0, 0);
                poly->x0 = rect->x + rect->w * edge + x;
                poly->y0 = rect->y + 1;
                /* a side is 4 wide, inward */
                poly->x1 = rect->x + rect->w * edge + x + (4 + edge * -8);
                poly->y1 = rect->y + 1;
                poly->x2 = rect->x + rect->w * edge + x;
                poly->y2 = rect[1].y - 1;
                /* inward by the same */
                poly->x3 = rect->x + rect->w * edge + x + (4 + edge * -8);
                poly->y3 = rect[1].y - 1;
            } else {
                SetSemiTrans(poly, 0);
                far_red = KEY_FRAME_FAR_RED;
                setRGB0(poly, glow, 0, 0);
                setRGB2(poly, glow, 0, 0);
                setRGB1(poly, far_red, 0, 0);
                setRGB3(poly, far_red, 0, 0);
                poly->x0 = rect->x + rect->w * edge + x;
                poly->y0 = rect->y + 1;
                poly->x1 = rect->x + rect->w * edge + x + (-4 * edge + 2);
                poly->y1 = rect->y + 1;
                poly->x2 = rect->x + rect->w * edge + x;
                poly->y2 = rect[1].y - 1;
                poly->x3 = rect->x + rect->w * edge + x + (-4 * edge + 2);
                poly->y3 = rect[1].y - 1;
            }
            AddPrim(g_disp_ot + MENU_LAYER_FRONT, poly);
            poly++;
        }
        /* the GPU's blending mode 2: a see-through frame is taken from
         * what is under it */
        SetDrawMode(ctx->prims.mode, 1, 0, getTPage(0, 2, 0, 0), 0);
        AddPrim(g_disp_ot + MENU_LAYER_FRONT, ctx->prims.mode);
        ctx->prims.mode++;
        x += PLAYER_2_X;
    }
    ctx->prims.g4 = poly;
}

/* Gives the action `player`'s cursor is on the button just pressed, and
 * takes that button away from the player's other actions. */
static void menu_AssignButton(s32 player, s32 pressed) {
    s32 button;
    s16 action;
    s32 mask;

    /* the button's bit of a set: PAD_CONFIG_TRIANGLE's, and so on */
    if (pressed & PAD_OF(PADRup, player)) {
        button = 1;
    } else if (pressed & PAD_OF(PADRdown, player)) {
        button = 2;
    } else if (pressed & PAD_OF(PADRleft, player)) {
        button = 3;
    } else if (pressed & PAD_OF(PADRright, player)) {
        button = 0;
    }

    mask = (1 << button) << (player * MENU_BUTTONS);

    for (action = 0; action < MENU_ACTIONS; action++) {
        if (action == g_menu_work->key_cursors.half[player]) {
            g_menu_settings->button_mappings[action] |= mask;
        } else {
            g_menu_settings->button_mappings[action] &= ~mask;
        }
    }
}

/* The key configuration's pad. Each player has a cursor: on an action, one
 * of the four buttons gives the action that button; on the two rows below,
 * cross puts the player's half of the mapping back to the defaults, or to
 * what it was. The last row cannot be reached by a player with an action
 * that has no button (`unassigned` has a bit for each such player). Returns
 * 1 when cross is pressed with both cursors on the last row. Nothing uses
 * `held`. */
static s32 menu_HandleKeyConfig(s32 *held, s32 *pressed, s16 unassigned) {
    s32 player;
    s16 last_row;
    s32 pad_shift;
    s32 nibble;
    s32 nibble_shift;
    s32 action;
    s32 result;
    s16 row;

    result = PAGE_RUNNING;
    if ((g_menu_work->key_cursors.word == KEY_CURSORS_ON_EXIT) &&
        (*pressed & PAD_ANY(PADRdown))) {
        snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
        result = PAGE_CONFIRMED;
    }

    player = 0;
    nibble = PAD_CONFIG_SET;
    nibble_shift = 0;
    pad_shift = 0;
    for (; player < 2; pad_shift += PAD_BITS, player++) {
        last_row = ((unassigned >> player) & 1) ? KEY_ROW_RESET : KEY_ROW_EXIT;

        if (*pressed & (PADLup << pad_shift)) {
            snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
            if (g_menu_work->key_cursors.half[player] <= 0) {
                g_menu_work->key_cursors.half[player] = last_row;
            } else {
                g_menu_work->key_cursors.half[player] =
                    g_menu_work->key_cursors.half[player] - 1;
            }
        } else if (*pressed & (PADLdown << pad_shift)) {
            snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
            if (g_menu_work->key_cursors.half[player] >= last_row) {
                g_menu_work->key_cursors.half[player] = 0;
            } else {
                g_menu_work->key_cursors.half[player] =
                    g_menu_work->key_cursors.half[player] + 1;
            }
        }

        row = g_menu_work->key_cursors.half[player];
        switch (row) {
        case 0: /* the three actions */
        case 1:
        case 2:
            if (*pressed &
                ((PADRup | PADRright | PADRdown | PADRleft) << pad_shift)) {
                snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
                menu_AssignButton(player, *pressed);
            }
            break;
        case KEY_ROW_DEFAULTS:
            if (*pressed & (PADRdown << pad_shift)) {
                snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
                for (action = 0; action < MENU_ACTIONS; action++) {
                    g_menu_settings->button_mappings[action] =
                        (g_menu_settings->button_mappings[action] &
                         ((player == 0) ? PAD_CONFIG_SET << PAD_CONFIG_BITS
                                        : PAD_CONFIG_SET)) +
                        (g_menu_settings->default_button_mappings[action] &
                         (nibble << nibble_shift));
                }
            }
            break;
        case KEY_ROW_RESET:
            if (*pressed & (PADRdown << pad_shift)) {
                snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
                for (action = 0; action < MENU_ACTIONS; action++) {
                    g_menu_settings->button_mappings[action] =
                        (g_menu_settings->button_mappings[action] &
                         ((player == 0) ? PAD_CONFIG_SET << PAD_CONFIG_BITS
                                        : PAD_CONFIG_SET)) +
                        (g_menu_settings->committed_button_mappings[action] &
                         (nibble << nibble_shift));
                }
            }
            break;
        }
        nibble_shift += MENU_BUTTONS;
    }
    return result;
}

/* How many buttons each player has on a row so far; `word` is both at
 * once. */
typedef union {
    s16 count[2];
    s32 word;
} KeyConfigCounts;

/* The buttons each action has, in a row beside it, for both players, and
 * the last row, which says whether the page can be left. Returns a bit for
 * each player who has an action with no button. */
static s32 menu_DrawKeyConfigButtons(MenuCtx *ctx) {
    SpriteDraw draw;
    KeyConfigCounts counts;
    SpriteSheetSlot *sheets;
    s16 action;
    s16 button;
    s16 player_index;
    s16 unassigned;
    SpriteCell *cells;

    unassigned = 0;
    sheets = PTR(ctx->file->key_config_sheets);
    draw.ot_index = MENU_LAYER_FRONT;
    draw.has_color = 0;
    draw.semi = 0;

    for (action = 0; action < MENU_ACTIONS; action++) {
        counts.word = 0;
        draw.y = action * KEY_ACTION_PITCH;
        for (button = 0; button < MENU_BUTTONS; button++) {
            draw.header = PTR(sheets[button + KEY_SHEET_BUTTONS]);
            cells = draw.header->cells;
            draw.table = cells;
            for (player_index = 0; player_index < 2; player_index++) {
                if (g_menu_settings->button_mappings[action] &
                    ((1 << (player_index * MENU_BUTTONS)) << button)) {
                    draw.x = player_index * PLAYER_2_X +
                             counts.count[player_index] * KEY_BUTTON_PITCH;
                    draw.sprt_out = ctx->prims.sprt;
                    ctx->prims.sprt = sprite_Draw(&draw);
                    counts.count[player_index]++;
                }
            }
        }

        for (player_index = 0; player_index < 2; player_index++) {
            if (counts.count[player_index] == 0) {
                draw.x = player_index * PLAYER_2_X;
                unassigned |= 1 << player_index;
                draw.header = PTR(sheets[KEY_SHEET_NO_BUTTON]);
                cells = draw.header->cells;
                draw.table = cells;
                draw.sprt_out = ctx->prims.sprt;
                ctx->prims.sprt = sprite_Draw(&draw);
            }
        }
    }

    for (player_index = 0; player_index < 2; player_index++) {
        if (g_menu_work->key_cursors.word == KEY_CURSORS_ON_EXIT) {
            draw.header = PTR(sheets[KEY_SHEET_BOTH_EXIT]);
        } else if ((unassigned >> player_index) & 1) {
            draw.header = PTR(sheets[KEY_SHEET_NO_EXIT]);
        } else {
            draw.header = PTR(sheets[KEY_SHEET_EXIT]);
        }
        draw.x = player_index * PLAYER_2_X;
        draw.y = 0;
        cells = draw.header->cells;
        draw.table = cells;
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
    }

    draw.header = PTR(sheets[KEY_SHEET_PAD_BUTTONS + 1]);
    SetDrawMode(ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + MENU_LAYER_FRONT, ctx->prims.mode);
    ctx->prims.mode++;

    {
        u16 rule[4];
        rule[2] = KEY_RULE_WIDTH;
        rule[0] = KEY_RULE_X;
        rule[1] = MENU_TITLE_RULE_Y;
        rule[3] = 1;
        menu_DrawTitleRule(ctx, rule, MENU_LAYER_PANEL);
    }
    return unassigned;
}

/* Opens the key configuration with both cursors on the last row. */
void menu_ResetKeyConfig(void) {
    g_menu_work->key_cursors.half[1] = KEY_ROW_EXIT;
    g_menu_work->key_cursors.half[0] = KEY_ROW_EXIT;
}

/* A frame of the key configuration page. */
s32 menu_UpdateKeyConfig(s32 held, s32 pressed) {
    MenuCtx ctx;
    SpriteDraw unused; /* never used; the original keeps room for it */
    GpuDb *db;
    s32 unassigned;
    g_menu_pulse += 1;
    db = &g_disp_db[g_disp_frame_count & 1];
    menu_BeginDraw(&ctx, &g_menu_frame);
    menu_DrawPageFrame(&ctx, MENU_PANEL_PAIR, db);
    menu_DrawKeyConfig(&ctx);
    unassigned = menu_DrawKeyConfigButtons(&ctx);
    return menu_HandleKeyConfig(&held, &pressed, ((unassigned << 16) >> 16));
}

/* ---- training mode's page ---- */

/* Training mode's page: the title, the three rows (the cursor's glowing)
 * and under them what the kind of practice under the cursor is. */
static void menu_DrawPractice(MenuCtx *ctx) {
    SpriteDraw draw;
    u16 rule[4];
    SpriteSheetSlot *sheets;
    s32 abr = 0;
    s16 i = 0;
    u8 dim = TINT_DIM;

    draw.ot_index = MENU_LAYER_PAGE;
    sheets = PTR(ctx->file->practice_sheets);
    draw.x = 0;
    draw.has_color = 1;

    for (; i < PRACTICE_SHEETS; i++) {
        draw.has_color = 1;
        if (i == PRACTICE_SHEET_TITLE) {
            draw.y = 0;
            draw.has_color = 0;
            draw.semi = 0;
        } else if (i == g_menu_practice_cursor + PRACTICE_SHEET_ROWS) {
            draw.y = g_menu_work->nudge[1];
            draw.col_r = draw.col_g = draw.col_b =
                MENU_PULSE(GLOW_DEPTH_SMALL, TINT_PLAIN);
            draw.semi = 0;
        } else if (i != 1 && i != 2 && i != 3 &&
                   i == g_menu_practice_cursor + PRACTICE_SHEET_DESCRIPTIONS) {
            /* the description of the kind the cursor is on */
            draw.col_r = draw.col_g = draw.col_b = TINT_PLAIN;
            draw.y = 0;
            draw.semi = 0;
        } else {
            draw.col_r = draw.col_g = draw.col_b = dim;
            draw.y = 0;
            draw.semi = 1;
        }
        draw.header = PTR(sheets[i]);
        draw.table = draw.header->cells;
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
        SetDrawMode(
            ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, abr), 0);
        AddPrim(g_disp_ot + MENU_LAYER_PAGE, ctx->prims.mode);
        ctx->prims.mode++;
    }

    rule[0] = PRACTICE_RULE_X;
    rule[1] = MENU_TITLE_RULE_Y;
    rule[2] = PRACTICE_RULE_WIDTH;
    rule[3] = 1;
    menu_DrawTitleRule(ctx, rule, MENU_LAYER_PANEL);
}

/* Training mode's pad: the two kinds of practice, and back. Returns 1 when
 * a kind is chosen (it is in the settings), -1 to go back. Nothing uses
 * `held`. */
static s32 menu_HandlePractice(s32 held, u32 pressed) {
    s32 either_pad;
    s32 result = PAGE_RUNNING;
    s32 events;
    s32 row;

    either_pad = (pressed & 0xFFFF) | (pressed >> PAD_BITS);
    events =
        menu_StepNudge(&either_pad, 0, g_menu_work->nudge, g_menu_nudge_steps);
    if (NUDGE_DOWN(events) == NUDGE_PLUS) {
        g_menu_practice_cursor = g_menu_practice_cursor + 1;
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
    } else if (NUDGE_DOWN(events) == NUDGE_MINUS) {
        g_menu_practice_cursor = g_menu_practice_cursor - 1;
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
    }
    if (g_menu_practice_cursor < 0) {
        g_menu_practice_cursor = PRACTICE_ROWS - 1;
    }
    g_menu_practice_cursor = g_menu_practice_cursor % PRACTICE_ROWS;
    if (pressed & PAD_ANY(PADRup)) {
        snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
        result = PAGE_CANCELED;
    } else if (pressed & PAD_ANY(PADRdown)) {
        snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
        row = g_menu_practice_cursor;
        if (row == PRACTICE_ROW_BACK) {
            result = PAGE_CANCELED;
        } else {
            g_menu_settings->practice_round = row;
            result = PAGE_CONFIRMED;
        }
    }
    return result;
}

/* A frame of training mode's page. */
s32 menu_UpdatePractice(s32 held, s32 pressed) {
    MenuCtx ctx;
    SpriteDraw unused; /* never used; the original keeps room for it */
    s32 result;
    GpuDb *db;
    g_menu_pulse += 1;
    db = &g_disp_db[g_disp_frame_count & 1];
    menu_BeginDraw(&ctx, &g_menu_frame);
    menu_DrawPageFrame(&ctx, MENU_PANEL_WIDE, db);
    menu_DrawPractice(&ctx);
    result = menu_HandlePractice(held, pressed);
    snd_FlushQueue();
    return result;
}

/* ---- chanbara's page ---- */

/* Opens chanbara's page with the cursor on "yes". */
s32 menu_ResetChanbara(void) {
    g_menu_chanbara_cursor = 0;
    return 1;
}

/* A table of best times: the title and ranks, then for each row whose time
 * it is and the time as three pairs of digits. `rank` is the row to blink,
 * with a fourth row under the table for the time just set; -1 draws the
 * three rows alone. The menu draws it on chanbara's page and the fight
 * after a run (hud_DrawBestTimes), each from its own sheets. */
void menu_DrawBestTimes(
    s32 *sprt, s32 *mode, BestTimesSheets *sheets, s32 ot_index, s32 rank) {
    SpriteDraw draw;
    s16 digits[6]; /* room for a time's six digits; two are used */
    s16 i;
    s16 k;
    s16 who;
    s16 rows;

    draw.ot_index = ot_index;
    draw.y = 0;
    draw.x = 0;
    draw.semi = 0;
    draw.has_color = 0;
    draw.col_r = draw.col_g = draw.col_b = TINT_BLINK;
    draw.header = PTR(sheets->own_cell_sheets[2]);
    for (i = 0; i < 3; i++) {
        draw.header = PTR(sheets->own_cell_sheets[i]);
        draw.table = draw.header->cells;
        draw.sprt_out = *sprt;
        *sprt = sprite_Draw(&draw);
    }
    draw.header = PTR(sheets->own_cell_sheets[0]);
    SetDrawMode(
        (DR_MODE *)ADDR(*mode), 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + ot_index, (DR_MODE *)ADDR(*mode));
    *mode += sizeof(DR_MODE);

    rows = BEST_TIMES_ROWS + 1;
    if (rank == -1) {
        rows = BEST_TIMES_ROWS;
    }
    for (i = 0; i < rows; i++) {
        who = g_menu_settings->time_records[i].character_index;
        if (who >= FIGHTERS_FIRST_TWELVE && who < FIGHTERS_ON_SELECT) {
            who -= FIGHTERS_UNNAMED;
        }
        draw.header = PTR(sheets->character_sheet);
        /* a name's three cells, after the long name's three more */
        draw.table = PTR(sheets->character_cells) + 3 + who * 3;
        draw.x = PTR(sheets->character_x_offsets)[who];
        draw.y = i * BEST_TIMES_ROW_PITCH;
        if (i == BEST_NEW_ROW) {
            draw.x -= BEST_NEW_NAME_SHIFT;
            draw.y = BEST_NEW_NAME_Y;
        }
        draw.sprt_out = *sprt;
        *sprt = sprite_Draw(&draw);
        if (who == FIGHTER_LONG_NAME) {
            draw.table = PTR(sheets->character_cells);
            draw.sprt_out = *sprt;
            *sprt = sprite_Draw(&draw);
        }
    }

    draw.header = PTR(sheets->character_sheet);
    SetDrawMode(
        (DR_MODE *)ADDR(*mode), 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + ot_index, (DR_MODE *)ADDR(*mode));
    *mode += sizeof(DR_MODE);

    for (i = 0; i < 3; i++) {
        for (k = 0; k < rows; k++) {
            if ((g_disp_frame_count & 1) &&
                ((k == rank && k < BEST_TIMES_ROWS) || k == BEST_NEW_ROW)) {
                draw.has_color = 1;
            } else {
                draw.has_color = 0;
            }
            switch (i) {
            case 0:
                digits[0] = digits[1] =
                    g_menu_settings->time_records[k].minutes;
                break;
            case 1:
                digits[0] = digits[1] =
                    g_menu_settings->time_records[k].seconds;
                break;
            case 2:
                digits[0] = digits[1] =
                    g_menu_settings->time_records[k].hundredths;
                break;
            }
            digits[0] = digits[0] % 10;      /* the ones */
            digits[1] = digits[1] / 10 % 10; /* the tens */
            draw.header = PTR(sheets->digit_sheet);
            draw.header->ubase = digits[0] * BEST_DIGIT_WIDTH;
            draw.table = PTR(sheets->digit_cell);
            draw.x = i * BEST_PAIR_PITCH + BEST_DIGIT_WIDTH;
            draw.y = k * BEST_TIMES_ROW_PITCH + BEST_DIGITS_Y;
            if (k == BEST_NEW_ROW) {
                draw.x = i * BEST_PAIR_PITCH + BEST_NEW_DIGITS_X;
                draw.y = BEST_NEW_DIGITS_Y;
            }
            draw.sprt_out = *sprt;
            *sprt = sprite_Draw(&draw);
            draw.header->ubase = digits[1] * BEST_DIGIT_WIDTH;
            draw.x -= BEST_DIGIT_WIDTH;
            draw.sprt_out = *sprt;
            *sprt = sprite_Draw(&draw);
        }
    }

    draw.header = PTR(sheets->digit_sheet);
    SetDrawMode(
        (DR_MODE *)ADDR(*mode), 1, 0, sprite_GetTPage(draw.header, 0), 0);
    AddPrim(g_disp_ot + ot_index, (DR_MODE *)ADDR(*mode));
    *mode += sizeof(DR_MODE);
}

/* Chanbara's page: the question and its two answers, and the best
 * times. */
static void menu_DrawChanbara(MenuCtx *ctx) {
    SpriteDraw draw;
    s32 unused[2]; /* never used; the original keeps room for it */
    u16 rule[4];
    s16 i;
    MenuChanbaraSheets *sheets;
    s32 zero = 0;

    draw.ot_index = MENU_LAYER_PAGE;
    sheets = PTR(ctx->file->chanbara);
    draw.x = 0;
    draw.semi = zero;

    for (i = 0; i < 3; i++) {
        draw.has_color = 1;
        if (i == g_menu_chanbara_cursor + 1) {
            draw.y = g_menu_work->nudge[1];
            draw.col_r = draw.col_g = draw.col_b =
                MENU_PULSE(GLOW_DEPTH_SMALL, TINT_PLAIN);
        } else {
            if (i == 0) {
                draw.col_r = draw.col_g = draw.col_b = TINT_PLAIN;
            } else {
                draw.col_r = draw.col_g = draw.col_b = TINT_DIM;
            }
            draw.y = 0;
        }
        draw.header = PTR(sheets->choice_sheets[i]);
        draw.table = draw.header->cells;
        draw.sprt_out = ctx->prims.sprt;
        ctx->prims.sprt = sprite_Draw(&draw);
        SetDrawMode(
            ctx->prims.mode, 1, 0, sprite_GetTPage(draw.header, zero), 0);
        AddPrim(g_disp_ot + MENU_LAYER_PAGE, ctx->prims.mode);
        ctx->prims.mode++;
    }

    menu_DrawBestTimes(&ctx->prims.sprt, (s32 *)&ctx->prims.mode,
                       &sheets->best_times, draw.ot_index, -1);

    rule[2] = CHANBARA_RULE_WIDTH;
    rule[0] = CHANBARA_RULE_X;
    rule[1] = MENU_TITLE_RULE_Y;
    rule[3] = 1;
    menu_DrawTitleRule(ctx, rule, MENU_LAYER_PANEL);
}

/* Chanbara's pad: yes or no. Returns 1 for yes, -1 for no. Nothing uses
 * `held`. */
static s32 menu_HandleChanbara(s32 held, u32 pressed) {
    s32 either_pad;
    s32 events;
    s32 result = PAGE_RUNNING;

    either_pad = (pressed & 0xFFFF) | (pressed >> PAD_BITS);
    events =
        menu_StepNudge(&either_pad, 0, g_menu_work->nudge, g_menu_nudge_steps);
    /* up or down: there are two answers */
    if ((NUDGE_DOWN(events) & 0xFF) != 0) {
        g_menu_chanbara_cursor += 1;
        snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
    }
    g_menu_chanbara_cursor &= 1;
    if (pressed & PAD_ANY(PADRup)) {
        result = PAGE_CANCELED;
        snd_Play(MENU_SOUND_CANCEL, SND_VOLUME_MAX, SND_VOLUME_MAX);
    } else if (pressed & PAD_ANY(PADRdown)) {
        snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
        if (g_menu_chanbara_cursor != 0) {
            result = PAGE_CANCELED;
        } else {
            result = PAGE_CONFIRMED;
        }
    }
    snd_FlushQueue();
    return result;
}

/* A frame of chanbara's page. */
s32 menu_UpdateChanbara(s32 held, s32 pressed) {
    MenuCtx ctx;
    SpriteDraw unused; /* never used; the original keeps room for it */
    GpuDb *db;
    g_menu_pulse += 1;
    db = &g_disp_db[g_disp_frame_count & 1];
    menu_BeginDraw(&ctx, &g_menu_frame);
    menu_DrawPageFrame(&ctx, MENU_PANEL_WIDE, db);
    menu_DrawChanbara(&ctx);
    return menu_HandleChanbara(held, pressed);
}

/* ---- the link cable's picture ---- */

/* Shows the picture that is up while two consoles connect over the link
 * cable: reads it over the menu's free memory and puts it in both display
 * buffers' place. Nothing else is drawn until menu_ClearLinkPicture. */
s32 menu_LoadLinkPicture(void) {
    /* where the picture is read to */
    s32 memory = g_menu_free_ram;
    RECT rect;
    s32 file;

    rect = g_menu_link_picture_rect;
    if (g_menu_settings->grayscale) {
        file =
            cdrom_GetFileIndex(CDROM_GROUP_DATA, MENU_LINK_PICTURE_GRAY_FILE);
    } else {
        file = cdrom_GetFileIndex(CDROM_GROUP_DATA, MENU_LINK_PICTURE_FILE);
    }
    cdrom_StartRead(file, g_menu_free_ram);
    cdrom_WaitIdle();
    cdrom_GetFileSize(file);
    SetDispMask(0);
    SetDefDrawEnv(&g_disp_db[0].draw, 0, 0, DISP_WIDTH, DISP_HEIGHT);
    SetDefDrawEnv(&g_disp_db[1].draw, 0, DISP_HEIGHT, DISP_WIDTH, DISP_HEIGHT);
    SetDefDispEnv(&g_disp_db[0].disp, 0, DISP_HEIGHT, DISP_WIDTH, DISP_HEIGHT);
    SetDefDispEnv(&g_disp_db[1].disp, 0, 0, DISP_WIDTH, DISP_HEIGHT);
    g_disp_db[0].disp.isinter = 0;
    g_disp_db[1].disp.isinter = 0;
    g_disp_db[0].disp.isrgb24 = 0;
    g_disp_db[1].disp.isrgb24 = 0;
    DrawSync(0);
    ClearImage(&rect, 0, 0, 0);
    DrawSync(0);
    /* the pixels follow the picture's TIM header */
    LoadImage(&rect, (u32 *)ADDR(memory + TIM_PIXELS_NO_CLUT));
    DrawSync(0);
    PutDrawEnv(&g_disp_db[0].draw);
    PutDispEnv(&g_disp_db[1].disp);
    SetDispMask(1);
    return 1;
}

/* Takes the picture down: the display as the menu had it, and both buffers
 * black. */
s32 menu_ClearLinkPicture(void) {
    RECT rect;
    SetDispMask(0);
    PutDrawEnv(&g_disp_db[g_menu_frame & 1].draw);
    PutDispEnv(&g_disp_db[g_menu_frame & 1].disp);
    DrawSync(0);
    rect = g_menu_link_clear_rect;
    ClearImage(&rect, 0, 0, 0);
    DrawSync(0);
    disp_SetBackground(1, 0, 0, 0);
    SetDispMask(1);
    return 1;
}

/* ---- a frame of a page ---- */

/* Starts a frame of a page: takes the next frame's pool and points the
 * context's cursors at the start of it. */
static void menu_BeginDraw(MenuCtx *ctx, s32 *frame) {
    PrimPool *pool;
    frame[0]++;
    pool = menu_GetPrimPool(frame[0] & 1);
    ctx->file = PTR(g_menu_work->file);
    ctx->prims.f4 = PTR(pool->f4);
    ctx->prims.ft4 = pool->ft4;
    ctx->prims.g4 = PTR(pool->g4);
    ctx->prims.sprt = pool->sprt;
    ctx->prims.tile = PTR(pool->tile);
    ctx->prims.mode = PTR(pool->mode);
    ctx->prims.area = PTR(pool->area);
    g_menu_pool = pool;
    ctx->prims.offset = PTR(pool->offset);
}

/* ---- every 2D screen: resource files and primitive pools ---- */

/* Turns the offsets at the start of a resource file (up to a -1) into
 * addresses. */
void sprite_RelocateFile(s32 *file) {
    s32 *entry = file;
    if (*entry == -1) {
        return;
    }
    while (*entry != -1) {
        *entry = *entry + (s32)file;
        entry++;
    }
}

/* A sheet's texture page, with a blending mode (`abr_bits`, already in
 * place) added. */
s32 sprite_GetTPage(SpriteSheet *sheet, s32 abr_bits) {
    /* the page's place, without the bits of the blending mode and of the
     * depth of colour; the depth is tp1 */
    return (sheet->tp0 & 0xFE1F) + (sheet->tp1 << 7) + abr_bits;
}

/* Lays out the two frames' primitive pools from `memory` on: a region for
 * each kind of primitive, one frame's after the other's. Returns the first
 * byte past them. */
s32 sprite_InitPools(s32 memory, PrimPool *pools) {
    u8 *first_f4;
    u8 *first_sprt;
    u8 *second_f4;
    u8 *second_sprt;
    pools[0].ft4 = memory;
    first_f4 = (u8 *)ADDR(pools[0].ft4) + PRIM_POOL_FT4_SIZE;
    SET_PTR(pools[0].g4, first_f4 + PRIM_POOL_F4_SIZE);
    SET_PTR(pools[0].f4, first_f4);
    first_sprt = first_f4 +
                 (PRIM_POOL_F4_SIZE + PRIM_POOL_G4_SIZE + PRIM_POOL_TILE_SIZE);
    SET_PTR(pools[0].tile, first_f4 + (PRIM_POOL_F4_SIZE + PRIM_POOL_G4_SIZE));
    SET_PTR(pools[0].mode, first_sprt + PRIM_POOL_SPRT_SIZE);
    SET_PTR(pools[0].area,
            first_sprt + (PRIM_POOL_SPRT_SIZE + PRIM_POOL_MODE_SIZE));
    SET_PTR(pools[0].offset,
            first_sprt + (PRIM_POOL_SPRT_SIZE + PRIM_POOL_MODE_SIZE +
                          PRIM_POOL_AREA_SIZE));
    SET_PTR(pools[0].tail_region,
            first_sprt + (PRIM_POOL_SPRT_SIZE + PRIM_POOL_MODE_SIZE +
                          PRIM_POOL_AREA_SIZE + PRIM_POOL_OFFSET_SIZE));
    pools[0].sprt = (s32)first_sprt;
    pools[1].ft4 = (s32)(first_sprt + PRIM_POOL_REST_SIZE);
    second_f4 = (u8 *)ADDR(pools[1].ft4) + PRIM_POOL_FT4_SIZE;
    SET_PTR(pools[1].g4, second_f4 + PRIM_POOL_F4_SIZE);
    SET_PTR(pools[1].f4, second_f4);
    second_sprt = second_f4 +
                  (PRIM_POOL_F4_SIZE + PRIM_POOL_G4_SIZE + PRIM_POOL_TILE_SIZE);
    SET_PTR(pools[1].tile, second_f4 + (PRIM_POOL_F4_SIZE + PRIM_POOL_G4_SIZE));
    SET_PTR(pools[1].mode, second_sprt + PRIM_POOL_SPRT_SIZE);
    SET_PTR(pools[1].area,
            second_sprt + (PRIM_POOL_SPRT_SIZE + PRIM_POOL_MODE_SIZE));
    SET_PTR(pools[1].offset,
            second_sprt + (PRIM_POOL_SPRT_SIZE + PRIM_POOL_MODE_SIZE +
                           PRIM_POOL_AREA_SIZE));
    SET_PTR(pools[1].tail_region,
            second_sprt + (PRIM_POOL_SPRT_SIZE + PRIM_POOL_MODE_SIZE +
                           PRIM_POOL_AREA_SIZE + PRIM_POOL_OFFSET_SIZE));
    pools[1].sprt = (s32)second_sprt;
    return (s32)(second_sprt + PRIM_POOL_REST_SIZE);
}

/* The strip of the colour tables that is copied when the menu loads. */
static RECT g_menu_palette_rect = {112, 481, 48, 1};
/* The buttons that nudge a cursor, across and down. */
static u32 g_menu_nudge_plus_buttons[2] = {PADLright, PADLdown};
static u32 g_menu_nudge_minus_buttons[2] = {PADLleft, PADLup};
/* Where the link picture goes, and both display buffers. */
static RECT g_menu_link_picture_rect = {0, 0, DISP_WIDTH, DISP_HEIGHT};
static RECT g_menu_link_clear_rect = {0, 0, DISP_WIDTH, 2 * DISP_HEIGHT};

/* The top menu as it starts: every row can be chosen, and the rows in the
 * order of the screen. */
#define BOTH_VIEWS (MENU_VIEW_SIDE | MENU_VIEW_POV)
static MenuTop g_menu_top = {
    0xFF, /* every row can be chosen */
    0xBF, /* in view: the first six rows, and the last */
    {{FIGHT_MODE_STORY, BOTH_VIEWS},
     {FIGHT_MODE_VSCPU, BOTH_VIEWS},
     {FIGHT_MODE_PRACTICE, BOTH_VIEWS},
     {FIGHT_MODE_VS, MENU_VIEW_SIDE},
     {FIGHT_MODE_TEAM, MENU_VIEW_SIDE},
     {FIGHT_MODE_NETWORK, MENU_VIEW_POV},
     {FIGHT_MODE_CHANBARA, BOTH_VIEWS},
     {MENU_ROW_OPTIONS, 0}},
};
