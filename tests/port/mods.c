/* The port's mods: a manifest read, a folder of mods in its order, the key
 * of a texture, and the PNG reader and writer. */
/* Everything is made up here: the mods' folders are the test's own
 * stand-ins for the host's (a tree in memory), the textures are numbers
 * that say where they are, and the PNGs are written by tests/port/mods.py
 * from the format as it is written down, with zlib's own compression.
 * What is printed is read by that file too: the keys, worked out a second
 * time from docs/MODS.md's description of them, and the pictures. */
#include "common.h"
#include "port_host.h"
#include "port_mods.h"
#include "port_png.h"
#include "src/port/text.c"
#include "src/port/png.c"
#include "src/port/mods.c"
#include "runtime/runtime.h"

/* ---- the host, in memory ---- */

typedef struct {
    const char *path;
    const char *text; /* NULL for a folder */
} MadeEntry;

/* A tree of mods: a folder is a path with no text, a file one with. */
static const MadeEntry g_made_tree[] = {
    {"mods", NULL},
    {"mods/b-plain", NULL},
    {"mods/b-plain/textures", NULL},
    {"mods/b-plain/textures/x.png", "b's x"},
    {"mods/b-plain/textures/w.png", "b's w"},
    {"mods/a-first", NULL},
    {"mods/a-first/mod.txt", "# the first by its folder, the last by order\n"
                             "name = Later\r\nversion = 2.0\norder = 5\n"},
    {"mods/a-first/textures", NULL},
    {"mods/a-first/textures/X.PNG", "a's x"},
    {"mods/a-first/textures/notes.txt", "not a texture"},
    {"mods/a-first/textures/sub", NULL},
    {"mods/a-first/textures/sub/y.png", "a's y"},
    {"mods/a-first/music", NULL},
    {"mods/a-first/music/theme.wav", "a's music"},
    {"mods/c-off", NULL},
    {"mods/c-off/mod.txt", "enabled = no\n"},
    {"mods/c-off/textures", NULL},
    {"mods/c-off/textures/z.png", "c's z"},
};
#define MADE_ENTRIES ((s32)(sizeof(g_made_tree) / sizeof(g_made_tree[0])))
#define MADE_FILES_MAX 8

static s32 g_made_open[MADE_FILES_MAX];

static s32 made_Find(const char *path) {
    s32 i;

    for (i = 0; i < MADE_ENTRIES; i++) {
        if (test_StringsEqual(g_made_tree[i].path, path)) {
            return i;
        }
    }
    return -1;
}

s32 port_HostOpen(const char *path) {
    s32 entry = made_Find(path);
    s32 i;

    if (entry < 0 || g_made_tree[entry].text == NULL) {
        return -1;
    }
    for (i = 0; i < MADE_FILES_MAX; i++) {
        if (g_made_open[i] == 0) {
            g_made_open[i] = entry + 1;
            return i;
        }
    }
    return -1;
}

void port_HostClose(s32 file) { g_made_open[file] = 0; }

u32 port_HostFileSize(s32 file) {
    return port_TextLength(g_made_tree[g_made_open[file] - 1].text);
}

u32 port_HostReadAt(s32 file, u32 offset, void *dest, u32 count) {
    const char *text = g_made_tree[g_made_open[file] - 1].text;
    u32 size = port_TextLength(text);
    u32 i;

    for (i = 0; i < count && offset + i < size; i++) {
        ((char *)dest)[i] = text[offset + i];
    }
    return i;
}

s32 port_HostListFolder(
    const char *path,
    void (*each)(void *context, const char *name, s32 is_folder),
    void *context) {
    u32 length = port_TextLength(path);
    s32 entry = made_Find(path);
    s32 i;

    if (entry < 0 || g_made_tree[entry].text != NULL) {
        return 0;
    }
    for (i = 0; i < MADE_ENTRIES; i++) {
        const char *other = g_made_tree[i].path;
        u32 j;

        if (port_TextLength(other) <= length + 1 || other[length] != '/') {
            continue;
        }
        for (j = 0; j < length && other[j] == path[j]; j++) {
        }
        if (j < length) {
            continue;
        }
        /* a child, and not a child's child */
        for (j = length + 1; other[j] != 0 && other[j] != '/'; j++) {
        }
        if (other[j] == 0) {
            each(context, other + length + 1, g_made_tree[i].text == NULL);
        }
    }
    return 1;
}

s32 port_HostMakeFolder(const char *path) { return path != NULL; }

void *port_HostAllocate(u32 size) { return test_Alloc(size); }

/* What the mods say goes to the log; the test keeps the lines quiet. */
void port_HostWrite(s32 stream, const char *bytes, u32 count) {
    (void)stream;
    (void)bytes;
    (void)count;
}

void port_Quit(s32 code) {
    test_Print("quit %d\n", code);
    for (;;) {
    }
}

/* ---- the manifest ---- */

static void test_Manifest(void) {
    static const char text[] =
        "# a comment, and a blank line\n"
        "\n"
        "  Name =  Better Menus  # said after\r\n"
        "version=1.0b\n"
        "order = -3\n"
        "author = someone\n"
        "enabled = Off\n"
        "colour = blue\n"
        "order = many\n"
        "just words\n";
    PortModManifest manifest;
    char problem[128];
    s32 problems;

    manifest.name[0] = 0;
    manifest.version[0] = 0;
    manifest.order = 0;
    manifest.is_enabled = TRUE;
    problems = port_ModParseManifest(
        text, sizeof(text) - 1, &manifest, problem, sizeof(problem));
    CHECK(test_StringsEqual(manifest.name, "Better Menus"));
    CHECK(test_StringsEqual(manifest.version, "1.0b"));
    CHECK_EQ(manifest.order, -3);
    CHECK_EQ(manifest.is_enabled, FALSE);
    /* `colour`, `order = many` and `just words` */
    CHECK_EQ(problems, 3);
    CHECK(test_StringsEqual(
        problem, "line 8: a name the manifest does not have"));
    /* an empty manifest says nothing and changes nothing */
    problems = port_ModParseManifest("", 0, &manifest, problem, 16);
    CHECK_EQ(problems, 0);
    CHECK(test_StringsEqual(manifest.name, "Better Menus"));
}

/* ---- the folder of mods ---- */

static void test_Folder(void) {
    port_ModsSetFolder("mods");
    port_ModsStart("ignored/when/a/folder/is/given");
    /* a-first is first by its folder's name, but its order of 5 puts it
     * after b-plain's 0: so its x is the one */
    CHECK(test_StringsEqual(
        port_ModFind(PORT_MOD_TEXTURES, "x"), "mods/a-first/textures/X.PNG"));
    CHECK(test_StringsEqual(port_ModFind(PORT_MOD_TEXTURES, "y"),
                            "mods/a-first/textures/sub/y.png"));
    CHECK(test_StringsEqual(
        port_ModFind(PORT_MOD_TEXTURES, "w"), "mods/b-plain/textures/w.png"));
    /* the mod its manifest turned off, and what is not a PNG */
    CHECK(port_ModFind(PORT_MOD_TEXTURES, "z") == NULL);
    CHECK(port_ModFind(PORT_MOD_TEXTURES, "notes") == NULL);
    CHECK(port_ModFind(PORT_MOD_MODELS, "x") == NULL);
    CHECK(test_StringsEqual(
        port_ModFind(PORT_MOD_MUSIC, "theme"), "mods/a-first/music/theme.wav"));
    CHECK_EQ(port_ModsCount(PORT_MOD_TEXTURES), 3);
    CHECK_EQ(port_ModsCount(PORT_MOD_MUSIC), 1);
    CHECK_EQ(g_port_mods.overridden, 1);
    CHECK_EQ(port_ModFileSize("mods/b-plain/textures/x.png"), 5);
}

/* ---- the key of a texture ---- */

#define VRAM_WORDS (PORT_VRAM_WIDTH * PORT_VRAM_HEIGHT)

static u16 *g_vram;

/* A texture of four bits, 13 by 6 texels, whose texel (u, v) is
 * (u * 3 + v * 5) % 16, at page corner (page_x, page_y) and texture point
 * (u0, v0) of it; and its 16 colours at (clut_x, clut_y), colour i being
 * 0x1000 + i * 0x111 (colour 0 left as nothing). */
static void made_Texture4(
    s32 page_x, s32 page_y, s32 u0, s32 v0, s32 clut_x, s32 clut_y) {
    s32 u;
    s32 v;
    s32 i;

    for (v = 0; v < 6; v++) {
        for (u = 0; u < 13; u++) {
            s32 at_u = u0 + u;
            u16 *word = &g_vram[(page_y + v0 + v) * PORT_VRAM_WIDTH + page_x +
                                at_u / 4];
            s32 shift = (at_u % 4) * 4;

            *word = (u16)((*word & ~(0xF << shift)) |
                          (((u * 3 + v * 5) % 16) << shift));
        }
    }
    for (i = 0; i < 16; i++) {
        g_vram[clut_y * PORT_VRAM_WIDTH + clut_x + i] =
            (u16)(i == 0 ? 0 : 0x1000 + i * 0x111);
    }
}

static void made_Region(
    PortTextureRegion *region, s32 bits, s32 page_x, s32 page_y, s32 u, s32 v,
    s32 w, s32 h, s32 clut_x, s32 clut_y) {
    region->bits = bits;
    region->page_x = page_x;
    region->page_y = page_y;
    region->clut_x = clut_x;
    region->clut_y = clut_y;
    region->u = u;
    region->v = v;
    region->w = w;
    region->h = h;
    region->and_u = 0xFF;
    region->or_u = 0;
    region->and_v = 0xFF;
    region->or_v = 0;
}

static void test_PrintKey(const char *what, const PortTextureRegion *region) {
    char name[PORT_MOD_KEY_SIZE];

    port_TextureKeyName(
        port_TextureKey(g_vram, region), region, name, sizeof(name));
    test_Print("key %s %s\n", what, name);
}

static void test_Keys(void) {
    PortTextureRegion first;
    PortTextureRegion second;
    u64 key;
    s32 i;

    g_vram = test_Alloc(VRAM_WORDS * sizeof(u16));
    for (i = 0; i < VRAM_WORDS; i++) {
        g_vram[i] = (u16)(i * 7919);
    }
    made_Texture4(640, 0, 5, 9, 0, 480);
    made_Region(&first, PORT_MOD_TEXELS_4, 640, 0, 5, 9, 13, 6, 0, 480);
    key = port_TextureKey(g_vram, &first);
    /* the same texture elsewhere: another page, another place in it that
     * does not begin on a word, another colour table's slot */
    made_Texture4(768, 256, 2, 100, 512, 300);
    made_Region(&second, PORT_MOD_TEXELS_4, 768, 256, 2, 100, 13, 6, 512, 300);
    CHECK(port_TextureKey(g_vram, &second) == key);
    /* what the GPU would draw: the colour, and nothing for colour 0 */
    CHECK_EQ(port_TextureTexel(g_vram, &first, 0, 0), 0);
    CHECK_EQ(port_TextureTexel(g_vram, &first, 1, 0), 0x1000 + 3 * 0x111);
    CHECK_EQ(port_TextureTexel(g_vram, &second, 4, 5),
             0x1000 + ((4 * 3 + 5 * 5) % 16) * 0x111);
    test_PrintKey("four", &first);
    /* a colour of the table changed, and a texel */
    g_vram[300 * PORT_VRAM_WIDTH + 512 + 9] ^= 1;
    CHECK(port_TextureKey(g_vram, &second) != key);
    g_vram[300 * PORT_VRAM_WIDTH + 512 + 9] ^= 1;
    CHECK(port_TextureKey(g_vram, &second) == key);
    g_vram[(256 + 101) * PORT_VRAM_WIDTH + 768 + 1] ^= 0x10;
    CHECK(port_TextureKey(g_vram, &second) != key);
    /* a size or a depth of its own is a key of its own */
    second.w = 12;
    CHECK(port_TextureKey(g_vram, &second) != key);
    /* the texture window: points anded and ored, as the GPU does */
    made_Region(&first, PORT_MOD_TEXELS_4, 640, 0, 0, 0, 4, 2, 0, 480);
    first.and_u = 0xF0;
    first.or_u = 5;
    first.and_v = 0xF8;
    first.or_v = 9;
    CHECK_EQ(port_TextureTexel(g_vram, &first, 3, 1),
             port_TextureTexel(g_vram, &first, 0, 0));
    test_PrintKey("window", &first);
    /* sixteen bits: the pixels themselves, and no table */
    made_Region(&first, PORT_MOD_TEXELS_16, 100, 200, 0, 0, 20, 3, 0, 0);
    first.and_u = PORT_VRAM_WIDTH - 1;
    first.and_v = PORT_VRAM_HEIGHT - 1;
    CHECK_EQ(port_TextureTexel(g_vram, &first, 7, 2),
             g_vram[202 * PORT_VRAM_WIDTH + 107]);
    test_PrintKey("sixteen", &first);
    /* eight bits, its 256 colours wherever they are */
    made_Region(&first, PORT_MOD_TEXELS_8, 320, 64, 3, 1, 9, 4, 16, 500);
    test_PrintKey("eight", &first);
}

/* ---- fades ---- */

/* A colour of 15 bits from its three parts and its top bit. */
static u16 made_Color(s32 r, s32 g, s32 b, s32 top) {
    return (u16)(r | (g << 5) | (b << 10) | (top ? 0x8000 : 0));
}

/* The game's fade of a colour table (tex_TintClut's TEX_TINT_SCALE): each
 * part as eight bits, times a multiplier of 12 bits of fraction, shifted
 * down by 15; colour 0 left alone. */
static u16 made_Faded(u16 color, s32 mr, s32 mg, s32 mb) {
    s32 r = ((color & 31) << 3) * mr >> 15;
    s32 g = (((color >> 5) & 31) << 3) * mg >> 15;
    s32 b = (((color >> 10) & 31) << 3) * mb >> 15;

    if (color == 0) {
        return 0;
    }
    return (
        u16)((color & 0x8000) | (r & 31) | ((g & 31) << 5) | ((b & 31) << 10));
}

static void test_Fades(void) {
    u16 base[16];
    u16 now[16];
    PortTextureRegion first;
    PortTextureRegion second;
    u32 tint = 0;
    s32 step;
    s32 i;

    for (i = 0; i < 16; i++) {
        base[i] =
            i == 0 ? 0 : made_Color(i * 2 - 1, 31 - i, (i * 7) % 32, i & 1);
    }
    /* the same table is no fade at all, and tints by one */
    CHECK(port_TextureFadeOf(base, base, 16, &tint));
    CHECK_EQ(tint, PORT_MOD_TINT_SAME);
    /* every step of a fade to black, as the game writes it, is found, and
     * its tint is the multiplier's, a little under where the game
     * rounds each part down */
    for (step = 0; step <= 4096; step += 256) {
        for (i = 0; i < 16; i++) {
            now[i] = made_Faded(base[i], step, step, step);
        }
        CHECK(port_TextureFadeOf(base, now, 16, &tint));
        for (i = 0; i < 3; i++) {
            s32 part = (s32)((tint >> (8 * i)) & 0xFF);
            s32 want = step * PORT_MOD_TINT_ONE / 4096;

            CHECK(part - want <= 2 && want - part <= 10);
        }
    }
    /* each part by a number of its own: a tint toward gold */
    for (i = 0; i < 16; i++) {
        now[i] = made_Faded(base[i], 4096, 3200, 1000);
    }
    CHECK(port_TextureFadeOf(base, now, 16, &tint));
    CHECK((tint & 0xFF) >= 124 && ((tint >> 16) & 0xFF) <= 34);
    /* not a fade: another table, a colour of nothing that is now drawn, a
     * top bit changed, one colour out of step */
    for (i = 0; i < 16; i++) {
        now[i] = base[15 - i];
    }
    CHECK(!port_TextureFadeOf(base, now, 16, &tint));
    for (i = 0; i < 16; i++) {
        now[i] = made_Faded(base[i], 2048, 2048, 2048);
    }
    now[0] = made_Color(1, 1, 1, 0);
    CHECK(!port_TextureFadeOf(base, now, 16, &tint));
    now[0] = 0;
    CHECK(port_TextureFadeOf(base, now, 16, &tint));
    now[4] ^= 0x8000;
    CHECK(!port_TextureFadeOf(base, now, 16, &tint));
    now[4] ^= 0x8000;
    now[7] = made_Color(31, 31, 31, base[7] & 0x8000);
    CHECK(!port_TextureFadeOf(base, now, 16, &tint));
    /* a colour faded all the way to nothing is a fade's last step */
    for (i = 0; i < 16; i++) {
        now[i] = made_Faded(base[i], 0, 0, 0) & 0x7FFF;
    }
    now[0] = 0;
    for (i = 1; i < 16; i++) {
        now[i] = (u16)(now[i] | (base[i] & 0x8000));
    }
    CHECK(port_TextureFadeOf(base, now, 16, &tint));
    CHECK_EQ(tint, 0);

    /* the texels' key is the same whatever the table holds, and the
     * whole key is not */
    made_Texture4(640, 0, 5, 9, 0, 480);
    made_Region(&first, PORT_MOD_TEXELS_4, 640, 0, 5, 9, 13, 6, 0, 480);
    made_Texture4(768, 256, 2, 100, 512, 300);
    made_Region(&second, PORT_MOD_TEXELS_4, 768, 256, 2, 100, 13, 6, 512, 300);
    for (i = 1; i < 16; i++) {
        g_vram[300 * PORT_VRAM_WIDTH + 512 + i] = made_Faded(
            g_vram[300 * PORT_VRAM_WIDTH + 512 + i], 1500, 1500, 1500);
    }
    CHECK(port_TextureKey(g_vram, &first) != port_TextureKey(g_vram, &second));
    CHECK(port_TextureTexelKey(g_vram, &first) ==
          port_TextureTexelKey(g_vram, &second));
    CHECK(port_TextureFadeOf(&g_vram[480 * PORT_VRAM_WIDTH],
                             &g_vram[300 * PORT_VRAM_WIDTH + 512], 16, &tint));
    g_vram[(256 + 101) * PORT_VRAM_WIDTH + 768 + 1] ^= 0x10;
    CHECK(port_TextureTexelKey(g_vram, &first) !=
          port_TextureTexelKey(g_vram, &second));
}

/* ---- PNG ---- */

/* FNV-1a of 32 bits over a picture's bytes, which tests/port/mods.py works
 * out from the pixels it wrote. */
static u32 test_Sum(const u8 *bytes, u32 count) {
    u32 sum = 0x811C9DC5U;
    u32 i;

    for (i = 0; i < count; i++) {
        sum = (sum ^ bytes[i]) * 0x01000193U;
    }
    return sum;
}

static void test_ReadPngs(void) {
    s32 i;

    for (i = 1; test_Arg(i) != NULL; i++) {
        char path[512];
        char problem[128];
        unsigned size;
        u8 *file;
        PortPngInfo info;

        /* the folder, then the name */
        {
            const char *folder = test_Arg(0);
            const char *name = test_Arg(i);
            u32 at = 0;
            u32 j;

            for (j = 0; folder[j] != 0; j++) {
                path[at++] = folder[j];
            }
            path[at++] = '/';
            for (j = 0; name[j] != 0; j++) {
                path[at++] = name[j];
            }
            path[at] = 0;
        }
        file = test_ReadFile(path, &size);
        if (file == NULL) {
            test_Print("png %s missing\n", test_Arg(i));
            continue;
        }
        if (!port_PngMeasure(file, size, &info, problem, sizeof(problem))) {
            test_Print("png %s refused %s\n", test_Arg(i), problem);
            continue;
        }
        {
            u8 *work = test_Alloc(info.work_bytes);
            u8 *rgba = test_Alloc((u32)(info.w * info.h * 4));

            if (!port_PngDecode(
                    file, size, work, rgba, problem, sizeof(problem))) {
                test_Print("png %s damaged %s\n", test_Arg(i), problem);
                continue;
            }
            test_Print("png %s %d %d %u\n", test_Arg(i), info.w, info.h,
                       test_Sum(rgba, (u32)(info.w * info.h * 4)));
        }
    }
}

/* A picture written and read back, and printed for mods.py to read with
 * zlib: 7 by 3, pixel (x, y) being (x * 30, y * 80, x + y, 255 - x). */
static void test_WritePng(void) {
    u8 rgba[7 * 3 * 4];
    u8 out[1024];
    u8 back[7 * 3 * 4];
    u8 work[1024];
    PortPngInfo info;
    char problem[64];
    u32 size;
    s32 x;
    s32 y;
    u32 i;

    for (y = 0; y < 3; y++) {
        for (x = 0; x < 7; x++) {
            u8 *at = rgba + (y * 7 + x) * 4;

            at[0] = (u8)(x * 30);
            at[1] = (u8)(y * 80);
            at[2] = (u8)(x + y);
            at[3] = (u8)(255 - x);
        }
    }
    size = port_PngEncode(rgba, 7, 3, out);
    CHECK_EQ(size, port_PngEncodedSize(7, 3));
    CHECK(port_PngMeasure(out, size, &info, problem, sizeof(problem)));
    CHECK_EQ(info.w, 7);
    CHECK_EQ(info.h, 3);
    CHECK(info.work_bytes <= sizeof(work));
    CHECK(port_PngDecode(out, size, work, back, problem, sizeof(problem)));
    CHECK(test_BytesEqual(back, rgba, sizeof(rgba)));
    test_Print("written");
    for (i = 0; i < size; i++) {
        test_Print(" %d", out[i]);
    }
    test_Print("\n");
    /* cut short, and a byte of its pixels changed (the sums are not
     * checked: a damaged texture shows as one) */
    CHECK(!port_PngMeasure(out, 20, &info, problem, sizeof(problem)));
    out[size - 30] ^= 0x55;
    CHECK(!port_PngDecode(out, size, work, back, problem, sizeof(problem)) ||
          !test_BytesEqual(back, rgba, sizeof(rgba)));
}

void test_Main(void) {
    test_Manifest();
    test_Folder();
    test_Keys();
    test_Fades();
    test_WritePng();
    test_ReadPngs();
}
