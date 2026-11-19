/* The native port's mods (port_mods.h; docs/MODS.md): the folder of mods
 * read as the program starts, each mod's manifest, the one table of their
 * files by kind and key, and the key of a texture, which is how a
 * replacement texture is found whatever the game did with video memory.
 *
 * Nothing here is the game's state (config/port_state.txt):
 * the table is made once and only read after. Its memory is asked for
 * only where there are mods, so a run without any costs nothing. */
#include "common.h"
#include "port_gpu.h"
#include "port_host.h"
#include "port_mods.h"
#include "bb2.h"

#define MODS_MAX 64
/* how many mods the settings can leave out (port_ModsLeaveOut) */
#define LEFT_OUT_MAX 8
#define FILES_MAX 16384
/* the table's slots: a power of two, twice the files */
#define SLOTS 32768
#define SLOT_MASK (SLOTS - 1)
/* the files' keys and paths, end to end */
#define TEXT_BYTES 0x400000
#define PATH_SIZE 1024
#define FOLDER_NAME_SIZE 128
/* how deep the folders under a kind's own may go, and how many a folder
 * may have that are read */
#define DEPTH_MAX 4
#define SUBFOLDERS_MAX 32
#define MANIFEST_BYTES_MAX 0x4000
#define LINE_VALUE_SIZE 128
#define MODS_FOLDER "mods"
#define TEXT_END 0
/* the longest ending a kind names */
#define ENDING_SIZE 16
/* a gameplay file is hashed so many bytes at a time */
#define HASH_CHUNK_BYTES 4096

/* FNV-1a: of 32 bits for the table, of 64 for a texture's key */
#define HASH_START 0x811C9DC5U
#define HASH_PRIME 0x01000193U
#define KEY_START 0xCBF29CE484222325ULL
#define KEY_PRIME 0x00000100000001B3ULL

#define BITS_PER_BYTE 8
#define BYTE_MASK 0xFF
#define HALF_SHIFT 32
/* a texel of four bits: four to a word of video memory, and the table's
 * sixteen colours; of eight, two to a word and 256 colours */
#define NIBBLE_BITS 4
#define NIBBLE_MASK 0x0F
#define NIBBLE_SHIFT 2
#define NIBBLE_LOW 3
#define BYTE_SHIFT 1
#define BYTE_LOW 1
#define COLORS_4 16
#define COLORS_8 256
#define VRAM_X_MASK (PORT_VRAM_WIDTH - 1)
#define VRAM_Y_MASK (PORT_VRAM_HEIGHT - 1)

static const char * const g_port_mod_kind_folders[PORT_MOD_KINDS] = {
    "textures", "models",  "music",   "stages",
    "skies",    "weapons", "motions", "arenas"};
/* the ending a kind's file must have to be read, or one of several
 * separated by `|`; NULL for any */
static const char * const g_port_mod_kind_endings[PORT_MOD_KINDS] = {
    ".png", ".glb", ".ogg|.wav", NULL, ".png", ".txt", ".bbm", ".arena"};

typedef struct {
    PortModManifest manifest;
    char folder[FOLDER_NAME_SIZE];
    s32 counts[PORT_MOD_KINDS];
    s32 is_left_out; /* by the settings (port_ModsLeaveOut) */
} PortMod;

typedef struct {
    u32 hash;
    s32 kind;
    s32 mod;
    u32 key_at; /* in the text */
    u32 path_at;
} PortModFile;

static struct {
    s32 is_off;
    const char *folder;
    const char *dump_folder;
    /* the mods' folders the settings turned off */
    const char *left_out[LEFT_OUT_MAX];
    s32 left_out_count;
    char root[PATH_SIZE];
    s32 mod_count;
    PortMod mods[MODS_MAX];
    s32 order[MODS_MAX];
    PortModFile *files;
    s32 *slots;
    char *text;
    u32 text_used;
    s32 file_count;
    s32 counts[PORT_MOD_KINDS];
    s32 overridden;
    s32 lost; /* files left out: no room */
} g_port_mods;

/* ---- text ---- */

static void port_ModsSay(char *out, u32 room, const char *format, ...) {
    PortArgs args;

    if (out == NULL || room == 0) {
        return;
    }
    PORT_ARGS_START(args, format);
    port_FormatText(out, room, format, args);
    PORT_ARGS_END(args);
}

static char port_ModsLower(char character) {
    return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a')
                                                : character;
}

static s32 port_ModsIsSpace(char character) {
    return character == ' ' || character == '\t' || character == '\r' ||
           character == '\n';
}

/* Whether the `length` characters at `text` are `word`, letters of
 * either case. */
static s32 port_ModsWordIs(const char *text, u32 length, const char *word) {
    u32 i;

    for (i = 0; i < length; i++) {
        if (word[i] == 0 || port_ModsLower(text[i]) != word[i]) {
            return FALSE;
        }
    }
    return word[length] == 0;
}

static void port_ModsCopy(char *out, u32 room, const char *text, u32 length) {
    u32 i;

    for (i = 0; i < length && i + 1 < room; i++) {
        out[i] = text[i];
    }
    out[i] = TEXT_END;
}

/* ---- the manifest ---- */

/* A value that says yes or no: 1, 0, or -1 for neither. */
static s32 port_ModsYesOrNo(const char *value, u32 length) {
    if (port_ModsWordIs(value, length, "yes") ||
        port_ModsWordIs(value, length, "on") ||
        port_ModsWordIs(value, length, "true") ||
        port_ModsWordIs(value, length, "1")) {
        return TRUE;
    }
    if (port_ModsWordIs(value, length, "no") ||
        port_ModsWordIs(value, length, "off") ||
        port_ModsWordIs(value, length, "false") ||
        port_ModsWordIs(value, length, "0")) {
        return FALSE;
    }
    return -1;
}

/* One line's name and value, trimmed: what it says is put in the
 * manifest, or NULL is returned for a line that is understood, and a
 * reason for one that is not. */
static const char *port_ModsTakeLine(
    PortModManifest *manifest, const char *name, u32 name_length,
    const char *value, u32 value_length) {
    char number[LINE_VALUE_SIZE];
    s32 parsed;

    if (port_ModsWordIs(name, name_length, "name")) {
        port_ModsCopy(
            manifest->name, sizeof(manifest->name), value, value_length);
    } else if (port_ModsWordIs(name, name_length, "version")) {
        port_ModsCopy(
            manifest->version, sizeof(manifest->version), value, value_length);
    } else if (port_ModsWordIs(name, name_length, "order")) {
        port_ModsCopy(number, sizeof(number), value, value_length);
        if (!port_ParseNumber(number, &parsed)) {
            return "order takes a whole number";
        }
        manifest->order = parsed;
    } else if (port_ModsWordIs(name, name_length, "enabled")) {
        parsed = port_ModsYesOrNo(value, value_length);
        if (parsed < 0) {
            return "enabled is yes or no";
        }
        manifest->is_enabled = parsed;
    } else if (!port_ModsWordIs(name, name_length, "author") &&
               !port_ModsWordIs(name, name_length, "about") &&
               !port_ModsWordIs(name, name_length, "url")) {
        return "a name the manifest does not have";
    }
    return NULL;
}

/* The manifest a line at a time (port_mods.h). */
s32 port_ModParseManifest(const char *text, u32 length,
                          PortModManifest *manifest, char *problem, u32 room) {
    s32 problems = 0;
    s32 line = 0;
    u32 at = 0;

    if (problem != NULL && room > 0) {
        problem[0] = TEXT_END;
    }
    while (at < length) {
        u32 start = at;
        u32 end;
        u32 equals;
        u32 name_end;
        u32 value_start;
        const char *why;

        line++;
        while (at < length && text[at] != '\n') {
            at++;
        }
        end = at;
        at++;
        /* a comment runs to the line's end */
        for (equals = start; equals < end; equals++) {
            if (text[equals] == '#') {
                end = equals;
                break;
            }
        }
        while (start < end && port_ModsIsSpace(text[start])) {
            start++;
        }
        while (end > start && port_ModsIsSpace(text[end - 1])) {
            end--;
        }
        if (start == end) {
            continue;
        }
        for (equals = start; equals < end && text[equals] != '='; equals++) {
        }
        name_end = equals;
        while (name_end > start && port_ModsIsSpace(text[name_end - 1])) {
            name_end--;
        }
        value_start = equals + 1;
        while (value_start < end && port_ModsIsSpace(text[value_start])) {
            value_start++;
        }
        if (equals == end || name_end == start) {
            why = "not NAME = VALUE";
        } else {
            why = port_ModsTakeLine(
                manifest, text + start, name_end - start, text + value_start,
                value_start < end ? end - value_start : 0);
        }
        if (why != NULL) {
            if (problems == 0) {
                port_ModsSay(problem, room, "line %d: %s", line, why);
            }
            problems++;
        }
    }
    return problems;
}

/* ---- the table ---- */

static u32 port_ModsHash(s32 kind, const char *key) {
    u32 hash = HASH_START ^ (u32)kind;

    hash *= HASH_PRIME;
    while (*key != TEXT_END) {
        hash = (hash ^ (u8)*key++) * HASH_PRIME;
    }
    return hash;
}

/* `text` kept in the table's text: where, or -1 if there is no room. */
static s32 port_ModsKeep(const char *text, u32 length) {
    u32 at = g_port_mods.text_used;
    u32 i;

    if (TEXT_BYTES - at < length + 1) {
        return -1;
    }
    for (i = 0; i < length; i++) {
        g_port_mods.text[at + i] = text[i];
    }
    g_port_mods.text[at + length] = TEXT_END;
    g_port_mods.text_used += length + 1;
    return (s32)at;
}

static s32 port_ModsTextsEqual(const char *first, const char *second) {
    while (*first != TEXT_END && *first == *second) {
        first++;
        second++;
    }
    return *first == *second;
}

/* The slot of a kind's key: the file's own, or the empty one it would
 * go in. */
static s32 *port_ModsSlotOf(s32 kind, const char *key, u32 hash) {
    u32 at = hash & SLOT_MASK;

    for (;;) {
        s32 *slot = &g_port_mods.slots[at];
        const PortModFile *file;

        if (*slot < 0) {
            return slot;
        }
        file = &g_port_mods.files[*slot];
        if (file->hash == hash && file->kind == kind &&
            port_ModsTextsEqual(g_port_mods.text + file->key_at, key)) {
            return slot;
        }
        at = (at + 1) & SLOT_MASK;
    }
}

/* A mod's file: `path` is the whole of it, `name` its last part. */
static void port_ModsAdd(
    s32 kind, s32 mod, const char *path, u32 path_length, const char *name) {
    char key[PORT_MOD_KEY_SIZE];
    u32 length = 0;
    u32 hash;
    s32 *slot;
    s32 key_at;
    s32 path_at;
    PortModFile *file;

    while (name[length] != TEXT_END && length + 1 < sizeof(key)) {
        key[length] = port_ModsLower(name[length]);
        length++;
    }
    key[length] = TEXT_END;
    /* the key is the name without its ending */
    while (length > 0 && key[length - 1] != '.') {
        length--;
    }
    if (length > 1) {
        key[length - 1] = TEXT_END;
    }
    hash = port_ModsHash(kind, key);
    slot = port_ModsSlotOf(kind, key, hash);
    path_at = port_ModsKeep(path, path_length);
    if (path_at < 0) {
        g_port_mods.lost++;
        return;
    }
    if (*slot >= 0) {
        /* a mod earlier in the order had it: this one's is the one */
        file = &g_port_mods.files[*slot];
        g_port_mods.mods[file->mod].counts[kind]--;
        file->mod = mod;
        file->path_at = (u32)path_at;
        g_port_mods.mods[mod].counts[kind]++;
        g_port_mods.overridden++;
        return;
    }
    key_at = port_ModsKeep(key, port_TextLength(key));
    if (key_at < 0 || g_port_mods.file_count >= FILES_MAX) {
        g_port_mods.lost++;
        return;
    }
    file = &g_port_mods.files[g_port_mods.file_count];
    file->hash = hash;
    file->kind = kind;
    file->mod = mod;
    file->key_at = (u32)key_at;
    file->path_at = (u32)path_at;
    *slot = g_port_mods.file_count++;
    g_port_mods.mods[mod].counts[kind]++;
    g_port_mods.counts[kind]++;
}

/* ---- reading the folders ---- */

/* What a folder's listing is gathered into: its files are added as they
 * come, its folders kept to be read after it (a listing is not read
 * inside another). */
typedef struct {
    s32 kind;
    s32 mod;
    char *path;
    u32 path_length;
    s32 subfolder_count;
    char subfolders[SUBFOLDERS_MAX][FOLDER_NAME_SIZE];
} PortModsListing;

/* Whether `name` ends with one of `endings`, which are separated by `|`
 * (letters of either case). */
static s32 port_ModsHasEnding(const char *name, const char *endings) {
    char ending[ENDING_SIZE];

    while (*endings != TEXT_END) {
        u32 length = 0;

        while (endings[length] != TEXT_END && endings[length] != '|' &&
               length + 1 < sizeof(ending)) {
            ending[length] = endings[length];
            length++;
        }
        ending[length] = TEXT_END;
        if (port_TextEndsWith(name, ending)) {
            return TRUE;
        }
        endings += length;
        if (*endings == '|') {
            endings++;
        }
    }
    return FALSE;
}

/* A name in a kind's folder: a folder kept for later, a file of the
 * kind's ending added to the table under its whole path. */
static void port_ModsEachFile(void *context, const char *name, s32 is_folder) {
    PortModsListing *listing = (PortModsListing *)context;
    const char *ending = g_port_mod_kind_endings[listing->kind];
    u32 length = port_TextLength(name);

    if (is_folder) {
        if (listing->subfolder_count < SUBFOLDERS_MAX &&
            length < FOLDER_NAME_SIZE) {
            port_ModsCopy(listing->subfolders[listing->subfolder_count++],
                          FOLDER_NAME_SIZE, name, length);
        }
        return;
    }
    if (ending != NULL && !port_ModsHasEnding(name, ending)) {
        return;
    }
    if (listing->path_length + 1 + length + 1 > PATH_SIZE) {
        g_port_mods.lost++;
        return;
    }
    listing->path[listing->path_length] = '/';
    port_ModsCopy(listing->path + listing->path_length + 1,
                  PATH_SIZE - listing->path_length - 1, name, length);
    port_ModsAdd(listing->kind, listing->mod, listing->path,
                 listing->path_length + 1 + length, name);
    listing->path[listing->path_length] = TEXT_END;
}

/* Every file of a kind under `path` (PATH_SIZE bytes, changed and put
 * back), its folders' too. */
static void port_ModsReadKind(s32 kind, s32 mod, char *path, s32 depth) {
    PortModsListing listing;
    s32 i;

    listing.kind = kind;
    listing.mod = mod;
    listing.path = path;
    listing.path_length = port_TextLength(path);
    listing.subfolder_count = 0;
    if (!port_HostListFolder(path, port_ModsEachFile, &listing) ||
        depth >= DEPTH_MAX) {
        return;
    }
    for (i = 0; i < listing.subfolder_count; i++) {
        u32 length = port_TextLength(listing.subfolders[i]);

        if (listing.path_length + 1 + length + 1 > PATH_SIZE) {
            continue;
        }
        path[listing.path_length] = '/';
        port_ModsCopy(
            path + listing.path_length + 1, PATH_SIZE - listing.path_length - 1,
            listing.subfolders[i], length);
        port_ModsReadKind(kind, mod, path, depth + 1);
        path[listing.path_length] = TEXT_END;
    }
}

/* A folder of the mods' folder: a mod, its manifest read. */
static void port_ModsEachMod(void *context, const char *name, s32 is_folder) {
    PortMod *mod;
    char path[PATH_SIZE];
    char text[MANIFEST_BYTES_MAX];
    char problem[PATH_SIZE / 4];
    u32 size;
    s32 i;

    (void)context;
    if (!is_folder || port_TextLength(name) >= FOLDER_NAME_SIZE) {
        return;
    }
    if (g_port_mods.mod_count >= MODS_MAX) {
        port_Log("mods: more than %d mods; %s is left out\n", MODS_MAX, name);
        return;
    }
    mod = &g_port_mods.mods[g_port_mods.mod_count];
    memset(mod, 0, sizeof(*mod));
    port_ModsCopy(
        mod->folder, sizeof(mod->folder), name, port_TextLength(name));
    port_ModsCopy(mod->manifest.name, sizeof(mod->manifest.name), name,
                  port_TextLength(name));
    mod->manifest.is_enabled = TRUE;
    mod->is_left_out = FALSE;
    for (i = 0; i < g_port_mods.left_out_count; i++) {
        if (port_ModsTextsEqual(name, g_port_mods.left_out[i])) {
            mod->is_left_out = TRUE;
        }
    }
    port_ModsSay(path, sizeof(path), "%s/%s/%s", g_port_mods.root, name,
                 PORT_MOD_MANIFEST);
    size = port_ModReadFile(path, text, sizeof(text));
    if (size > 0) {
        s32 problems = port_ModParseManifest(
            text, size, &mod->manifest, problem, sizeof(problem));

        if (problems > 0) {
            port_Log("mods: %s: %d line%s of %s not understood (%s)\n", name,
                     problems, problems == 1 ? "" : "s", PORT_MOD_MANIFEST,
                     problem);
        }
    }
    if (mod->is_left_out) {
        mod->manifest.is_enabled = FALSE;
    }
    g_port_mods.mod_count++;
}

/* The mods in their order: by the manifests' `order`, then by folder. */
static s32 port_ModsComesBefore(const PortMod *first, const PortMod *second) {
    const char *mine = first->folder;
    const char *other = second->folder;

    if (first->manifest.order != second->manifest.order) {
        return first->manifest.order < second->manifest.order;
    }
    while (
        *mine != TEXT_END && port_ModsLower(*mine) == port_ModsLower(*other)) {
        mine++;
        other++;
    }
    return port_ModsLower(*mine) < port_ModsLower(*other);
}

/* The mods' order, an insertion sort into `order`: there are few. */
static void port_ModsSort(void) {
    s32 i;

    for (i = 0; i < g_port_mods.mod_count; i++) {
        s32 j = i;

        while (j > 0 && port_ModsComesBefore(
                            &g_port_mods.mods[i],
                            &g_port_mods.mods[g_port_mods.order[j - 1]])) {
            g_port_mods.order[j] = g_port_mods.order[j - 1];
            j--;
        }
        g_port_mods.order[j] = i;
    }
}

/* The table's memory: asked for once, where there is a mod. */
static s32 port_ModsMakeTable(void) {
    s32 i;

    if (g_port_mods.files != NULL) {
        return TRUE;
    }
    g_port_mods.files = (PortModFile *)port_HostAllocate(
        (u32)(FILES_MAX * sizeof(PortModFile)));
    g_port_mods.slots = (s32 *)port_HostAllocate((u32)(SLOTS * sizeof(s32)));
    g_port_mods.text = (char *)port_HostAllocate(TEXT_BYTES);
    if (g_port_mods.files == NULL || g_port_mods.slots == NULL ||
        g_port_mods.text == NULL) {
        port_Log("mods: no memory for the mods' table\n");
        return FALSE;
    }
    for (i = 0; i < SLOTS; i++) {
        g_port_mods.slots[i] = -1;
    }
    return TRUE;
}

/* The mods' folder: the one asked for, or `mods` beside the program. */
static void port_ModsFindRoot(const char *program) {
    u32 length;

    if (g_port_mods.folder != NULL) {
        port_ModsCopy(g_port_mods.root, sizeof(g_port_mods.root),
                      g_port_mods.folder, port_TextLength(g_port_mods.folder));
        return;
    }
    length = program != NULL ? port_TextLength(program) : 0;
    while (length > 0 && program[length - 1] != '/' &&
           program[length - 1] != '\\') {
        length--;
    }
    if (length == 0) {
        port_ModsCopy(g_port_mods.root, sizeof(g_port_mods.root), MODS_FOLDER,
                      port_TextLength(MODS_FOLDER));
        return;
    }
    port_ModsCopy(g_port_mods.root, sizeof(g_port_mods.root), program, length);
    port_ModsCopy(g_port_mods.root + length, sizeof(g_port_mods.root) - length,
                  MODS_FOLDER, port_TextLength(MODS_FOLDER));
}

void port_ModsSetFolder(const char *folder) {
    if (port_TextsEqual(folder, "off")) {
        g_port_mods.is_off = TRUE;
        return;
    }
    g_port_mods.is_off = FALSE;
    g_port_mods.folder = folder;
}

void port_ModsLeaveOut(const char *folder) {
    if (g_port_mods.left_out_count < LEFT_OUT_MAX) {
        g_port_mods.left_out[g_port_mods.left_out_count++] = folder;
    }
}

void port_ModsSetDumpFolder(const char *folder) {
    g_port_mods.dump_folder = folder;
}

const char *port_ModsGetDumpFolder(void) { return g_port_mods.dump_folder; }

/* The dump folder made, the mods' folder listed, each mod's files read
 * in its order, and all of it said in the log (port_mods.h). */
void port_ModsStart(const char *program) {
    char path[PATH_SIZE];
    s32 i;

    if (g_port_mods.dump_folder != NULL) {
        if (port_HostMakeFolder(g_port_mods.dump_folder)) {
            port_Log("mods: the game's textures are written to %s as they "
                     "are first drawn\n",
                     g_port_mods.dump_folder);
        } else {
            port_Log("mods: %s cannot be made; no texture is written\n",
                     g_port_mods.dump_folder);
            g_port_mods.dump_folder = NULL;
        }
    }
    if (g_port_mods.is_off) {
        port_Log("mods: off\n");
        return;
    }
    port_ModsFindRoot(program);
    g_port_mods.mod_count = 0;
    if (!port_HostListFolder(g_port_mods.root, port_ModsEachMod, NULL)) {
        port_Log("mods: none (no folder %s)\n", g_port_mods.root);
        return;
    }
    if (g_port_mods.mod_count == 0) {
        port_Log("mods: none in %s\n", g_port_mods.root);
        return;
    }
    if (!port_ModsMakeTable()) {
        return;
    }
    port_ModsSort();
    for (i = 0; i < g_port_mods.mod_count; i++) {
        s32 which = g_port_mods.order[i];
        PortMod *mod = &g_port_mods.mods[which];
        s32 kind;

        if (!mod->manifest.is_enabled) {
            continue;
        }
        for (kind = 0; kind < PORT_MOD_KINDS; kind++) {
            port_ModsSay(path, sizeof(path), "%s/%s/%s", g_port_mods.root,
                         mod->folder, g_port_mod_kind_folders[kind]);
            port_ModsReadKind(kind, which, path, 0);
        }
    }
    port_Log("mods: %d in %s, in this order:\n", g_port_mods.mod_count,
             g_port_mods.root);
    for (i = 0; i < g_port_mods.mod_count; i++) {
        const PortMod *mod = &g_port_mods.mods[g_port_mods.order[i]];

        if (!mod->manifest.is_enabled) {
            port_Log("mods:   %s (%s): turned off by %s\n", mod->manifest.name,
                     mod->folder,
                     mod->is_left_out ? "the settings" : "its manifest");
            continue;
        }
        port_Log("mods:   %s%s%s (%s, order %d): %d textures, %d models, "
                 "%d music, %d stages, %d skies, %d weapons, %d motions\n",
                 mod->manifest.name, mod->manifest.version[0] != 0 ? " " : "",
                 mod->manifest.version, mod->folder, mod->manifest.order,
                 mod->counts[PORT_MOD_TEXTURES], mod->counts[PORT_MOD_MODELS],
                 mod->counts[PORT_MOD_MUSIC], mod->counts[PORT_MOD_STAGES],
                 mod->counts[PORT_MOD_SKIES], mod->counts[PORT_MOD_WEAPONS],
                 mod->counts[PORT_MOD_MOTIONS]);
    }
    if (g_port_mods.overridden > 0) {
        port_Log("mods: %d files of one mod overridden by a later one's\n",
                 g_port_mods.overridden);
    }
    if (g_port_mods.lost > 0) {
        port_Log("mods: %d files left out: more than the table holds\n",
                 g_port_mods.lost);
    }
}

s32 port_ModsCount(s32 kind) {
    return kind >= 0 && kind < PORT_MOD_KINDS ? g_port_mods.counts[kind] : 0;
}

const char *port_ModFind(s32 kind, const char *key) {
    s32 *slot;

    if (g_port_mods.files == NULL || g_port_mods.file_count == 0) {
        return NULL;
    }
    slot = port_ModsSlotOf(kind, key, port_ModsHash(kind, key));
    return *slot >= 0 ? g_port_mods.text + g_port_mods.files[*slot].path_at
                      : NULL;
}

/* The files of a kind, one by one (port_mods.h). */
s32 port_ModFileOf(s32 kind, s32 place, const char **key, const char **path) {
    s32 i;

    for (i = 0; i < g_port_mods.file_count; i++) {
        const PortModFile *file = &g_port_mods.files[i];

        if (file->kind == kind && place-- == 0) {
            *key = g_port_mods.text + file->key_at;
            *path = g_port_mods.text + file->path_at;
            return TRUE;
        }
    }
    return FALSE;
}

/* One file's part of the gameplay hash: FNV-1a over its kind, its key and
 * its bytes. */
static u64 port_ModsFileHash(s32 kind, const char *key, const char *path) {
    u8 chunk[HASH_CHUNK_BYTES];
    u64 hash = (KEY_START ^ (u64)kind) * KEY_PRIME;
    s32 file;
    u32 size;
    u32 at;

    while (*key != TEXT_END) {
        hash = (hash ^ (u8)*key++) * KEY_PRIME;
    }
    file = port_HostOpen(path);
    if (file < 0) {
        return hash;
    }
    size = port_HostFileSize(file);
    for (at = 0; at < size; at += sizeof(chunk)) {
        u32 want = size - at < sizeof(chunk) ? size - at : sizeof(chunk);
        u32 got = port_HostReadAt(file, at, chunk, want);
        u32 i;

        for (i = 0; i < got; i++) {
            hash = (hash ^ chunk[i]) * KEY_PRIME;
        }
        if (got != want) {
            break;
        }
    }
    port_HostClose(file);
    return hash;
}

/* Every gameplay file's hash, summed (port_mods.h). */
u64 port_ModsGameplayHash(void) {
    static const s32 kinds[] = {
        PORT_MOD_STAGES, PORT_MOD_WEAPONS, PORT_MOD_MOTIONS};
    u64 hash = 0;
    u32 k;

    for (k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++) {
        const char *key;
        const char *path;
        s32 place;

        /* a sum, so that the order the folders were listed in does not
         * count */
        for (place = 0; port_ModFileOf(kinds[k], place, &key, &path); place++) {
            hash += port_ModsFileHash(kinds[k], key, path);
        }
    }
    return hash;
}

u32 port_ModFileSize(const char *path) {
    s32 file = port_HostOpen(path);
    u32 size;

    if (file < 0) {
        return 0;
    }
    size = port_HostFileSize(file);
    port_HostClose(file);
    return size;
}

/* As much of a file as there is room for (port_mods.h). */
u32 port_ModReadFile(const char *path, void *dest, u32 room) {
    s32 file = port_HostOpen(path);
    u32 size;
    u32 got;

    if (file < 0) {
        return 0;
    }
    size = port_HostFileSize(file);
    got = port_HostReadAt(file, 0, dest, size < room ? size : room);
    port_HostClose(file);
    return got;
}

/* ---- the key of a texture ---- */

/* The value at texel (u, v) of the region, before the colour table: an
 * index of four or eight bits, or the pixel of sixteen. */
static u32 port_TextureValue(
    const u16 *vram, const PortTextureRegion *region, s32 u, s32 v) {
    s32 at_u = ((region->u + u) & region->and_u) | region->or_u;
    s32 at_v = ((region->v + v) & region->and_v) | region->or_v;
    s32 y = (region->page_y + at_v) & VRAM_Y_MASK;
    const u16 *row = vram + y * PORT_VRAM_WIDTH;

    if (region->bits == PORT_MOD_TEXELS_4) {
        u32 word = row[(region->page_x + (at_u >> NIBBLE_SHIFT)) & VRAM_X_MASK];

        return (word >> ((at_u & NIBBLE_LOW) * NIBBLE_BITS)) & NIBBLE_MASK;
    }
    if (region->bits == PORT_MOD_TEXELS_8) {
        u32 word = row[(region->page_x + (at_u >> BYTE_SHIFT)) & VRAM_X_MASK];

        return (word >> ((at_u & BYTE_LOW) * BITS_PER_BYTE)) & BYTE_MASK;
    }
    return row[(region->page_x + at_u) & VRAM_X_MASK];
}

static u16 port_TextureClut(
    const u16 *vram, const PortTextureRegion *region, u32 index) {
    return vram[(region->clut_y & VRAM_Y_MASK) * PORT_VRAM_WIDTH +
                ((region->clut_x + (s32)index) & VRAM_X_MASK)];
}

u16 port_TextureTexel(
    const u16 *vram, const PortTextureRegion *region, s32 u, s32 v) {
    u32 value = port_TextureValue(vram, region, u, v);

    if (region->bits == PORT_MOD_TEXELS_16) {
        return (u16)value;
    }
    return port_TextureClut(vram, region, value);
}

static u64 port_KeyByte(u64 key, u32 byte) {
    return (key ^ (byte & BYTE_MASK)) * KEY_PRIME;
}

static u64 port_KeyHalf(u64 key, u32 half) {
    return port_KeyByte(port_KeyByte(key, half), half >> BITS_PER_BYTE);
}

/* FNV-1a over the depth, the size, the texels row by row and, where
 * `with_table` says, the colour table, each number of two bytes low byte
 * first (port_mods.h). */
static u64 port_TextureKeyOf(
    const u16 *vram, const PortTextureRegion *region, s32 with_table) {
    u64 key = KEY_START;
    s32 colors = region->bits == PORT_MOD_TEXELS_4   ? COLORS_4
                 : region->bits == PORT_MOD_TEXELS_8 ? COLORS_8
                                                     : 0;
    s32 u;
    s32 v;
    s32 i;

    key = port_KeyByte(key, (u32)region->bits);
    key = port_KeyHalf(key, (u32)region->w);
    key = port_KeyHalf(key, (u32)region->h);
    for (v = 0; v < region->h; v++) {
        for (u = 0; u < region->w; u++) {
            u32 value = port_TextureValue(vram, region, u, v);

            key = colors != 0 ? port_KeyByte(key, value)
                              : port_KeyHalf(key, value);
        }
    }
    for (i = 0; with_table && i < colors; i++) {
        key = port_KeyHalf(key, port_TextureClut(vram, region, (u32)i));
    }
    return key;
}

u64 port_TextureKey(const u16 *vram, const PortTextureRegion *region) {
    return port_TextureKeyOf(vram, region, TRUE);
}

u64 port_TextureTexelKey(const u16 *vram, const PortTextureRegion *region) {
    return port_TextureKeyOf(vram, region, FALSE);
}

/* ---- a fade ---- */

/* One of a colour's three parts of five bits. */
static s32 port_FadePart(u16 color, s32 part) {
    return (color >> (part * PORT_MOD_PART_BITS)) & PORT_MOD_PART_MAX;
}

/* Each part's number from what the base's colours and the new ones add up
 * to, then every colour held to its base's times it (port_mods.h). */
s32 port_TextureFadeOf(const u16 *base, const u16 *now, u32 count, u32 *tint) {
    u32 sum_base[PORT_MOD_PARTS] = {0, 0, 0};
    u32 sum_now[PORT_MOD_PARTS] = {0, 0, 0};
    u32 scale[PORT_MOD_PARTS];
    u32 i;
    s32 part;

    /* each part's scale: what the colours of the base add up to against
     * what they add up to now */
    for (i = 0; i < count; i++) {
        if (base[i] == 0) {
            /* the colour of nothing is left alone by a fade */
            if (now[i] != 0) {
                return FALSE;
            }
            continue;
        }
        if (now[i] != 0 && ((base[i] ^ now[i]) & PORT_MOD_MIX_BIT) != 0) {
            return FALSE;
        }
        for (part = 0; part < PORT_MOD_PARTS; part++) {
            sum_base[part] += (u32)port_FadePart(base[i], part);
            sum_now[part] += (u32)port_FadePart(now[i], part);
        }
    }
    for (part = 0; part < PORT_MOD_PARTS; part++) {
        if (sum_base[part] == 0) {
            if (sum_now[part] != 0) {
                return FALSE;
            }
            scale[part] = PORT_MOD_TINT_ONE;
            continue;
        }
        scale[part] = (sum_now[part] * PORT_MOD_TINT_ONE + sum_base[part] / 2) /
                      sum_base[part];
        if (scale[part] > PORT_MOD_TINT_MAX) {
            return FALSE;
        }
    }
    /* and every colour that scale of its base, to within the rounding of
     * a fade's steps */
    for (i = 0; i < count; i++) {
        for (part = 0; part < PORT_MOD_PARTS; part++) {
            s32 want = (s32)(((u32)port_FadePart(base[i], part) * scale[part] +
                              PORT_MOD_TINT_ONE / 2) /
                             PORT_MOD_TINT_ONE);
            s32 got = port_FadePart(now[i], part);

            if (got - want > PORT_MOD_FADE_SLACK ||
                want - got > PORT_MOD_FADE_SLACK) {
                return FALSE;
            }
        }
    }
    *tint = scale[0] | (scale[1] << PORT_MOD_TINT_SHIFT) |
            (scale[2] << (2 * PORT_MOD_TINT_SHIFT));
    return TRUE;
}

/* ---- the key of a page of the scene ---- */

s32 port_TexturePagePixel(const PortTexturePage *page, s32 u) {
    if (page->bits == PORT_MOD_TEXELS_4) {
        return u >> NIBBLE_SHIFT;
    }
    if (page->bits == PORT_MOD_TEXELS_8) {
        return u >> BYTE_SHIFT;
    }
    return u;
}

s32 port_TexturePageFits(const PortTexturePage *page) {
    return page->page_x >= 0 && page->page_y >= 0 &&
           page->page_x + port_TexturePagePixel(page, PORT_MOD_PAGE_SIDE) <=
               PORT_VRAM_WIDTH &&
           page->page_y + PORT_MOD_PAGE_SIDE <= PORT_VRAM_HEIGHT;
}

/* FNV-1a over the page as port_mods.h describes it: the region of the
 * whole page read through port_TextureValue, a texel that is not the
 * stage's own counted as 0. */
u64 port_TexturePageKey(const u16 *vram, const PortTexturePage *page) {
    PortTextureRegion region;
    u64 key = KEY_START;
    s32 colors = page->bits == PORT_MOD_TEXELS_4   ? COLORS_4
                 : page->bits == PORT_MOD_TEXELS_8 ? COLORS_8
                                                   : 0;
    s32 u;
    s32 v;
    s32 i;

    region.bits = page->bits;
    region.page_x = page->page_x;
    region.page_y = page->page_y;
    region.clut_x = page->clut_x;
    region.clut_y = page->clut_y;
    region.u = 0;
    region.v = 0;
    region.w = PORT_MOD_PAGE_SIDE;
    region.h = PORT_MOD_PAGE_SIDE;
    region.and_u = PORT_MOD_PAGE_SIDE - 1;
    region.or_u = 0;
    region.and_v = PORT_MOD_PAGE_SIDE - 1;
    region.or_v = 0;
    key = port_KeyByte(key, (u32)page->bits);
    key = port_KeyHalf(key, (u32)PORT_MOD_PAGE_SIDE);
    key = port_KeyHalf(key, (u32)PORT_MOD_PAGE_SIDE);
    for (v = 0; v < PORT_MOD_PAGE_SIDE; v++) {
        const u8 *own = page->own == NULL
                            ? NULL
                            : page->own + (page->page_y + v) * PORT_VRAM_WIDTH +
                                  page->page_x;

        for (u = 0; u < PORT_MOD_PAGE_SIDE; u++) {
            u32 value = 0;

            if (own == NULL || own[port_TexturePagePixel(page, u)] != 0) {
                value = port_TextureValue(vram, &region, u, v);
            }
            key = colors != 0 ? port_KeyByte(key, value)
                              : port_KeyHalf(key, value);
        }
    }
    for (i = 0; i < colors; i++) {
        key = port_KeyHalf(key, port_TextureClut(vram, &region, (u32)i));
    }
    return key;
}

void port_TexturePageKeyName(
    u64 key, const PortTexturePage *page, char *out, u32 room) {
    s32 depth = page->bits == PORT_MOD_TEXELS_4   ? NIBBLE_BITS
                : page->bits == PORT_MOD_TEXELS_8 ? BITS_PER_BYTE
                                                  : 2 * BITS_PER_BYTE;

    port_ModsSay(out, room, "page-%dx%d-%d-%08x%08x", PORT_MOD_PAGE_SIDE,
                 PORT_MOD_PAGE_SIDE, depth, (u32)(key >> HALF_SHIFT), (u32)key);
}

void port_TextureKeyName(
    u64 key, const PortTextureRegion *region, char *out, u32 room) {
    s32 depth = region->bits == PORT_MOD_TEXELS_4   ? NIBBLE_BITS
                : region->bits == PORT_MOD_TEXELS_8 ? BITS_PER_BYTE
                                                    : 2 * BITS_PER_BYTE;

    port_ModsSay(out, room, "%dx%d-%d-%08x%08x", region->w, region->h, depth,
                 (u32)(key >> HALF_SHIFT), (u32)key);
}
