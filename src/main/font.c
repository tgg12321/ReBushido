/* The built-in text font (font.h): 8x8 characters drawn as sprites, for
 * the debug menu and the messages of the development version. A frame can
 * show 32 characters; the rest are dropped. */
#include "common.h"
#include "bb2.h"

/* Defined further down. */
static RECT g_font_rect;
static u32 g_font_image[288];
static s32 g_font_buf_index;
static s32 g_font_enabled;

#define FONT_MAX_SPRITES 32
#define FONT_CHAR_SIZE 8 /* a character's width and height on the screen */
#define FONT_COLUMNS 76  /* of the text printf writes */
#define FONT_ROWS 26
#define FONT_MARGIN 16 /* from the screen's corner to printf's first one */
#define FONT_PRINTF_MAX 1024 /* the longest text a font_Printf can make */
#define FONT_FORMAT_MAX 256  /* and the longest piece of its format */

/* The picture in video memory: four colour tables, a row each, then four
 * rows of eight characters. */
#define FONT_VRAM_X 1008
#define FONT_VRAM_Y 476
#define FONT_VRAM_WIDTH 16
#define FONT_VRAM_HEIGHT 36
#define FONT_ROW_CHARS 8
#define FONT_CLUTS 4
/* Its texture page is the one at (960, 256), read four bits a texel, and
 * its characters begin 192 across and 224 down that: written as the bytes
 * a sprite has them in, less the first character row's 32 codes, which are
 * not in the picture. */
#define FONT_TPAGE 0x1F
#define FONT_TEX_U (192 - 256)
#define FONT_TEX_V (224 - 256)

/* The colour text starts a frame in. */
#define FONT_COLOR_WHITE 0xF0F0F0

/* A set for each frame's buffer. */
static SPRT_8 g_font_sprites[2][FONT_MAX_SPRITES];
static DR_TPAGE g_font_tpage[2];

static s32 g_font_sprite_count; /* characters drawn this frame */
static s32 g_font_col;          /* where font_Printf writes next */
static s32 g_font_row;
static s32 g_font_color;

void font_LoadImage(void) { LoadImage(&g_font_rect, g_font_image); }

/* Starts a frame's text. With the font switched off (g_font_enabled) the
 * frame is given no room. */
void font_BeginFrame(void) {
    s32 buf_index;
    s32 font_enabled;
    g_font_color = FONT_COLOR_WHITE;
    buf_index = g_font_buf_index;
    font_enabled = g_font_enabled;
    g_font_sprite_count = 0;
    g_font_row = 0;
    g_font_col = 0;
    g_font_buf_index = buf_index < 1U; /* the other frame's buffer */
    if (!font_enabled) {
        g_font_sprite_count = FONT_MAX_SPRITES; /* no room: nothing shows */
    }
}

/* Links the font's texture page in front of the frame's characters. */
void font_AddTpage(void) {
    DR_TPAGE *dr_tpage = &g_font_tpage[g_font_buf_index];
    P_TAG *ot;
    setDrawTPage(dr_tpage, 0, 0, FONT_TPAGE);
    ot = (P_TAG *)g_disp_ot;
    setaddr(dr_tpage, ot->addr);
    ot->addr = (u32)dr_tpage;
}

/* One character at (x, y). The picture has eight characters to a row, and
 * four characters share each place in it: bits 3 and 4 of the code pick the
 * colour table that shows the right one. `color` is 0xBBGGRR, and is drawn
 * at half its brightness. */
static void font_DrawChar(s32 x, s32 y, s32 character, s32 color) {
    s32 sprite_index = g_font_sprite_count;
    SPRT_8 *sprite;
    P_TAG *ot;

    if (sprite_index == FONT_MAX_SPRITES) {
        return;
    }
    g_font_sprite_count = sprite_index + 1;
    sprite = &g_font_sprites[g_font_buf_index][sprite_index];
    setSprt8(sprite);
    sprite->x0 = x;
    sprite->y0 = y;
    sprite->u0 =
        (character & (FONT_ROW_CHARS - 1)) * FONT_CHAR_SIZE + FONT_TEX_U;
    sprite->v0 = (character >> 5) * FONT_CHAR_SIZE + FONT_TEX_V;
    sprite->clut = ((character >> 3) & (FONT_CLUTS - 1)) << 6 |
                   getClut(FONT_VRAM_X, FONT_VRAM_Y);
    /* the colour, and over it the sprite's code again */
    *(u32 *)&sprite->r0 = (color >> 1) | 0x74000000;
    ot = (P_TAG *)g_disp_ot;
    setaddr(sprite, ot->addr);
    ot->addr = (u32)sprite;
}

/* A string from (x, y); a newline goes back to x on the next row. Nothing
 * calls it. */
static void font_DrawString(s32 x, s32 y, u8 *str, s32 color) {
    s32 character;
    s32 start_x = x;

    for (character = *str++; character != 0; character = *str++) {
        if (character == ' ') {
            x += FONT_CHAR_SIZE;
        } else if (character == '\n') {
            x = start_x;
            y += FONT_CHAR_SIZE;
        } else {
            font_DrawChar(x, y, character, color);
            x += FONT_CHAR_SIZE;
        }
    }
}

/* Formats like printf, one argument to each conversion, and writes the
 * result at the text cursor. In the result "~c" and three digits set the
 * colour: red, green and blue, 0 to 7 each. */
void font_Printf(u8 *format, s32 first_arg, ...) {
    char buf[FONT_PRINTF_MAX];
    char format_chunk[FONT_FORMAT_MAX];
    va_list ap;
    s32 cur_arg;
    s32 percent_seen;
    /* where the next character of the piece goes, then the next of the
     * result to draw */
    char *cursor;
    s32 ch;

    cur_arg = first_arg;
    percent_seen = 0;
    cursor = format_chunk;
    va_start(ap, first_arg);
    buf[0] = 0;

    /* The format a piece at a time, each with one conversion and its
     * argument: a piece ends where the next conversion begins. */
    while ((ch = *format++) != 0) {
        if (ch == '%') {
            if (!percent_seen) {
                percent_seen = 1;
            } else {
                *cursor = 0;
                sprintf(buf + strlen(buf), format_chunk, ADDR(cur_arg));
                cursor = format_chunk;
                cur_arg = va_arg(ap, s32);
            }
        }
        *cursor++ = ch;
    }

    *cursor = 0;
    sprintf(buf + strlen(buf), format_chunk, ADDR(cur_arg));

    cursor = buf;
    while ((ch = *cursor++) != 0) {
        s32 row = g_font_row;
        if (row >= FONT_ROWS) {
            break;
        }

        if (ch == ' ') {
            g_font_col++;
        } else if (ch == '\n') {
            g_font_col = 0;
            g_font_row = row + 1;
        } else if (ch == '~') {
            ch = *cursor++;
            if (ch == 0) {
                break;
            }
            if (ch == 'c' || ch == 'C') {
                s32 red, green, blue; /* the digits, as characters */
                red = *cursor++;
                if (red == 0) {
                    break;
                }
                green = *cursor++;
                if (green == 0) {
                    break;
                }
                blue = *cursor++;
                if (blue == 0) {
                    break;
                }
                /* each digit into the top three bits of its byte */
                g_font_color = ((red - '0') << 5) | ((green - '0') << 13) |
                               ((blue - '0') << 21);
            }
        } else {
            font_DrawChar(g_font_col * FONT_CHAR_SIZE + FONT_MARGIN,
                          row * FONT_CHAR_SIZE + FONT_MARGIN, ch, g_font_color);
            g_font_col++;
        }
        if (g_font_col >= FONT_COLUMNS) {
            g_font_col = 0;
            g_font_row++;
        }
    }
}

/* The font's picture: 16 x 36 pixels of 16 bits, which the sprites read as
 * 64 x 36 of four bits through one of four colour tables, which are its first
 * four rows. */
static u32 g_font_image[288] = {
    0xFFFF0000, 0xFFFF0000, 0xFFFF0000, 0xFFFF0000, 0xFFFF0000, 0xFFFF0000,
    0xFFFF0000, 0xFFFF0000, 0x00000000, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF,
    0x00000000, 0xFFFFFFFF, 0x00000000, 0xFFFFFFFF, 0x00000000, 0x00000000,
    0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0xFFFFFFFF, 0xFFFFFFFF,
    0xFFFFFFFF, 0xFFFFFFFF, 0x898D8901, 0x09D99999, 0x880333AA, 0x00027E62,
    0x03519113, 0x03A008AB, 0x008F66CC, 0x00089008, 0x88DCB810, 0x49DBFF99,
    0x08920896, 0x0007AF26, 0x4168CC21, 0x0298889E, 0x00AD98AC, 0x00CBBA88,
    0x85472B01, 0x0D573359, 0x018A8843, 0x000FEF66, 0x49CACA89, 0x00898988,
    0x039D9D8E, 0x00FD99B8, 0x84BEBAB8, 0x0B33333F, 0x0108A621, 0x009CAA34,
    0x45CCEC81, 0x0088B88C, 0x039DB99E, 0x01BDD9F8, 0x8BAFEFAB, 0x4D77733D,
    0x01888E01, 0x001C8A14, 0x410ACA41, 0x00098148, 0x0315990E, 0x00B15DF8,
    0x00967658, 0x0913371D, 0x08900E94, 0x01048A05, 0x01A808A5, 0x0210041A,
    0x0025102C, 0x0223162A, 0x01836709, 0x0999B9D9, 0x880111C8, 0x01115D51,
    0x4FD555DF, 0x0320046F, 0x0007664C, 0x0222122A, 0x00888888, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x008EEC80, 0x008DFB80, 0x00556550, 0x00554550, 0x00CC1100, 0x05544555,
    0x00445DC0, 0x06ECDD44, 0x08C426C8, 0x08877588, 0x0671A976, 0x0551B955,
    0x005DD111, 0x01110145, 0x04418954, 0x0EE219C4, 0x0CC022CC, 0x08867188,
    0x0462AA20, 0x0451A910, 0x0044DD01, 0x00999955, 0x00099144, 0x0CE23000,
    0x04C8AAC4, 0x088EF980, 0x00442000, 0x00776330, 0x00551DD1, 0x00677764,
    0x01CC5554, 0x00CE2300, 0x0CC022CC, 0x08866000, 0x0026EE20, 0x0451A910,
    0x00549B64, 0x05D99910, 0x05599055, 0x000CE220, 0x08C426C8, 0x08867188,
    0x0220AC62, 0x0551B155, 0x045DD775, 0x05410155, 0x04518877, 0x00004622,
    0x008EEC80, 0x00CCFF80, 0x04446444, 0x00554D50, 0x00CC1122, 0x01554451,
    0x01545DF2, 0x0008C422, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x0AF555FE, 0x00CF77E8,
    0x0AFFFDDD, 0x02FDDD72, 0x08D555FF, 0x077999F7, 0x07719177, 0x04733374,
    0x07E808E7, 0x04D923DC, 0x0DF20055, 0x05728877, 0x01984CB3, 0x066A8277,
    0x06688877, 0x07704077, 0x07699967, 0x05D822DD, 0x05FA0055, 0x0002A877,
    0x0118CC33, 0x066AA277, 0x06E80AF7, 0x06604077, 0x0763A367, 0x0558AAD5,
    0x047B9155, 0x0044EE73, 0x0198CCB3, 0x0679B177, 0x0EF133FF, 0x07715077,
    0x037DDD67, 0x0155FB55, 0x0176CC55, 0x0442AA33, 0x0110C433, 0x06688077,
    0x0E62207F, 0x07744477, 0x02A808E7, 0x0554AA55, 0x057608F7, 0x05728877,
    0x0198CCB3, 0x06688077, 0x02660473, 0x07740477, 0x0BB111FE, 0x0516EE71,
    0x0CDBBBFD, 0x02FDDD72, 0x0233F733, 0x037DDDF3, 0x02244433, 0x09FBBBF8,
    0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00000000,
    0x00000000, 0x00000000, 0x00110022, 0x00022000, 0x00220011, 0x00888222,
    0x0112AE40, 0x00008880, 0x00111880, 0x00000000, 0x00110022, 0x00000000,
    0x00000011, 0x00008A20, 0x0112AC40, 0x00088000, 0x09909188, 0x00000000,
    0x08E764EE, 0x0CDF7FD8, 0x08FFBDDD, 0x0675DF70, 0x0157FD54, 0x007FB176,
    0x04EA3166, 0x0DFBBBFC, 0x06F80EE2, 0x09D628C4, 0x05FA4451, 0x01320AF5,
    0x0112AC51, 0x03FC2277, 0x06711377, 0x0F70C07F, 0x06688C62, 0x09D739D4,
    0x013A8451, 0x0046EE71, 0x0112AC51, 0x037DB177, 0x02641562, 0x0F70407F,
    0x02EC4CE2, 0x09DEEC51, 0x01328C51, 0x05728A31, 0x0552AC51, 0x026CA077,
    0x02245522, 0x03F555F2, 0x0AA004EA, 0x09F73310, 0x08BBDDFF, 0x02FDD776,
    0x0177FF10, 0x0671FDF2, 0x02205122, 0x013EAE20, 0x00004444, 0x04CCC880,
    0x00022220, 0x00000000, 0x00000000, 0x00000000, 0x00000000, 0x00111110,
};

static s32 g_font_buf_index = 0; /* which frame's buffer */
static s32 g_font_enabled = 1;
static RECT g_font_rect = {
    FONT_VRAM_X, FONT_VRAM_Y, FONT_VRAM_WIDTH, FONT_VRAM_HEIGHT};
