/* The native port's view of the disc's files (port_discfiles.h;
 * docs/MODS.md, "Stages"): the ISO 9660 directory of the player's image,
 * read once as the program starts, and the mods' stage files, each read
 * in place of the sectors of the disc's file it stands for.
 *
 * A stage file of a mod is the whole of the disc's file, changed: it is
 * read where the drive would read the disc's, sector for sector, so the
 * game loads it as it loads its own and nothing of the game's code knows.
 * It may be shorter than the disc's (the rest of the last sectors read as
 * zeros), never longer: the game reads as many sectors as its tables
 * say. */
#include "common.h"
#include "port_disc.h"
#include "port_discfiles.h"
#include "port_host.h"
#include "port_mods.h"
#include "bb2.h"

/* The Primary Volume Descriptor's sector, and where in it the root
 * directory's record is. */
#define VOLUME_SECTOR 16
#define VOLUME_ROOT 156
#define VOLUME_KIND_PRIMARY 1
/* after the kind, the standard's name */
#define VOLUME_NAME_AT 1
#define PATH_SIZE 512
/* A directory record: its length, the file's first sector and size (each
 * written both ways; the first, the little end first, is read), its
 * flags, its name's length and its name. */
#define RECORD_LENGTH 0
#define RECORD_SECTOR 2
#define RECORD_SIZE 10
#define RECORD_FLAGS 25
#define RECORD_NAME_LENGTH 32
#define RECORD_NAME 33
#define RECORD_FLAG_FOLDER 0x02
#define RECORD_HEAD_BYTES 34
/* The names of a folder's own record and its parent's: one byte each. */
#define NAME_SELF 0
#define NAME_PARENT 1

#define FILES_MAX 2048
/* the most sectors a folder of the disc is read in */
#define FOLDER_SECTORS_MAX 16
#define TEXT_END '\0'

/* NDATA's directory (docs/formats/NDATA.md): a start and a length, in
 * sectors of NDATA.DAT, for each file; the stages are from file 0x27, and
 * a start of all ones is a file the build left out. */
#define NDATA_ENTRY_BYTES 4
#define NDATA_STAGES 0x27
#define NDATA_LEFT_OUT 0xFFFF
#define NDATA_INF_SECTORS 2

/* a stage file is not larger than this */
#define STAGE_FILE_MAX (1024 * 1024)
/* and the motion files (the .BBM files of MOTION/: 18 on the disc) */
#define MOTION_FILES_MAX 32
#define OVERRIDES_MAX (PORT_STAGES * PORT_STAGE_FILE_KINDS + MOTION_FILES_MAX)

typedef struct {
    u32 sector; /* the disc's first sector the file covers */
    u32 count;  /* sectors */
    u8 *data;   /* count * 2048 bytes */
} PortDiscOverride;

static struct {
    PortDiscFile *files;
    u32 count;
    const char *dump_folder;
    PortDiscOverride *overrides; /* OVERRIDES_MAX of them */
    u32 override_count;
    u32 lowest;          /* the first sector any covers */
    u32 highest;         /* and the one after the last */
    const u8 *ndata_inf; /* NDATA's directory as the disc has it, or NULL */
} g_port_disc_files;

static void port_DiscSay(char *out, u32 room, const char *format, ...) {
    PortArgs args;

    PORT_ARGS_START(args, format);
    port_FormatText(out, room, format, args);
    PORT_ARGS_END(args);
}

static char port_DiscUpper(char letter) {
    return letter >= 'a' && letter <= 'z' ? (char)(letter - 'a' + 'A') : letter;
}

static u32 port_DiscWord(const u8 *bytes) {
    return bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) |
           ((u32)bytes[3] << 24);
}

/* Whether the bytes at `bytes` are the letters of `name`. */
static s32 port_DiscHasName(const u8 *bytes, const char *name) {
    u32 i;

    for (i = 0; name[i] != TEXT_END; i++) {
        if (bytes[i] != (u8)name[i]) {
            return FALSE;
        }
    }
    return TRUE;
}

/* ---- the directory ---- */

u32 port_ReadDiscFolder(const u8 *data, u32 count, const char *folder,
                        PortDiscFile *files, u32 have, u32 room) {
    u32 sector;

    for (sector = 0; sector < count; sector++) {
        const u8 *bytes = data + sector * PORT_DISC_DATA_BYTES;
        u32 at = 0;

        while (at + RECORD_HEAD_BYTES <= PORT_DISC_DATA_BYTES) {
            const u8 *record = bytes + at;
            u32 length = record[RECORD_LENGTH];
            u32 name_length = record[RECORD_NAME_LENGTH];
            PortDiscFile *file;
            u32 out = 0;
            u32 i;

            /* the rest of the sector is empty: records do not cross into
             * the next */
            if (length == 0) {
                break;
            }
            if (length < RECORD_HEAD_BYTES ||
                at + length > PORT_DISC_DATA_BYTES ||
                RECORD_NAME + name_length > length) {
                break;
            }
            at += length;
            if (name_length == 1 && (record[RECORD_NAME] == NAME_SELF ||
                                     record[RECORD_NAME] == NAME_PARENT)) {
                continue;
            }
            if (have >= room) {
                return have;
            }
            file = &files[have];
            for (i = 0; folder[i] != TEXT_END && out + 1 < PORT_DISC_PATH_SIZE;
                 i++) {
                file->path[out++] = folder[i];
            }
            file->name_at = out;
            for (i = 0; i < name_length && out + 2 < PORT_DISC_PATH_SIZE; i++) {
                char letter = (char)record[RECORD_NAME + i];

                if (letter == ';') {
                    break;
                }
                file->path[out++] = letter;
            }
            /* a name with no ending keeps a dot of its own ("NAME.") */
            if (out > file->name_at && file->path[out - 1] == '.') {
                out--;
            }
            if (record[RECORD_FLAGS] & RECORD_FLAG_FOLDER) {
                file->path[out++] = '/';
            }
            file->path[out] = TEXT_END;
            file->sector = port_DiscWord(record + RECORD_SECTOR);
            file->size = port_DiscWord(record + RECORD_SIZE);
            have++;
        }
    }
    return have;
}

/* The disc's directory, folder by folder from the root
 * (port_discfiles.h). */
s32 port_ReadDiscFiles(const PortDisc *disc) {
    u8 *data = port_HostAllocate(FOLDER_SECTORS_MAX * PORT_DISC_DATA_BYTES);
    u32 next;

    if (g_port_disc_files.files == NULL) {
        g_port_disc_files.files =
            port_HostAllocate(FILES_MAX * sizeof(PortDiscFile));
    }
    if (g_port_disc_files.files == NULL || data == NULL) {
        return 0;
    }
    g_port_disc_files.count = 0;
    if (port_ReadDiscData(disc, VOLUME_SECTOR, 1, data, NULL) != PORT_DISC_OK ||
        data[0] != VOLUME_KIND_PRIMARY ||
        !port_DiscHasName(data + VOLUME_NAME_AT, "CD001")) {
        port_Log("rebushido: the disc has no directory that can be read\n");
        return 0;
    }
    /* the root: a folder of no name, read like the others below */
    g_port_disc_files.files[0].path[0] = '/';
    g_port_disc_files.files[0].path[1] = TEXT_END;
    g_port_disc_files.files[0].name_at = 0;
    g_port_disc_files.files[0].sector =
        port_DiscWord(data + VOLUME_ROOT + RECORD_SECTOR);
    g_port_disc_files.files[0].size =
        port_DiscWord(data + VOLUME_ROOT + RECORD_SIZE);
    g_port_disc_files.count = 1;
    for (next = 0; next < g_port_disc_files.count; next++) {
        PortDiscFile *folder = &g_port_disc_files.files[next];
        u32 length = port_TextLength(folder->path);
        u32 sectors =
            (folder->size + PORT_DISC_DATA_BYTES - 1) / PORT_DISC_DATA_BYTES;
        u32 got = 0;

        if (length == 0 || folder->path[length - 1] != '/') {
            continue;
        }
        if (sectors > FOLDER_SECTORS_MAX) {
            sectors = FOLDER_SECTORS_MAX;
        }
        port_ReadDiscData(disc, folder->sector, sectors, data, &got);
        g_port_disc_files.count = port_ReadDiscFolder(
            data, got, next == 0 ? "" : folder->path, g_port_disc_files.files,
            g_port_disc_files.count, FILES_MAX);
    }
    return (s32)g_port_disc_files.count;
}

/* A file by its path (port_discfiles.h). */
const PortDiscFile *port_FindDiscFile(const char *path) {
    u32 i;

    for (i = 0; i < g_port_disc_files.count; i++) {
        const char *have = g_port_disc_files.files[i].path;
        u32 at = 0;

        while (have[at] != TEXT_END &&
               port_DiscUpper(have[at]) == port_DiscUpper(path[at])) {
            at++;
        }
        if (have[at] == TEXT_END && path[at] == TEXT_END) {
            return &g_port_disc_files.files[i];
        }
    }
    return NULL;
}

/* The file a sector is in (port_discfiles.h). */
const PortDiscFile *port_DiscFileAt(u32 sector) {
    u32 i;

    for (i = 0; i < g_port_disc_files.count; i++) {
        const PortDiscFile *file = &g_port_disc_files.files[i];
        u32 sectors =
            (file->size + PORT_DISC_DATA_BYTES - 1) / PORT_DISC_DATA_BYTES;
        u32 length = port_TextLength(file->path);

        if (length > 0 && file->path[length - 1] == '/') {
            continue;
        }
        if (sector >= file->sector && sector - file->sector < sectors) {
            return file;
        }
    }
    return NULL;
}

/* ---- the stages' files ---- */

s32 port_StageFileKey(s32 stage, s32 kind, char *key, u32 room) {
    static const char * const prefixes[PORT_STAGE_FILE_KINDS] = {
        "s", "stage", "stg"};

    if (stage < 0 || stage >= PORT_STAGES || kind < 0 ||
        kind >= PORT_STAGE_FILE_KINDS ||
        (kind == PORT_STAGE_FILE_CARD && stage >= PORT_STAGE_CARDS)) {
        return 0;
    }
    port_DiscSay(key, room, "%s%02d", prefixes[kind], stage);
    return 1;
}

/* Where the disc has a stage's file: its first sector, and how many
 * bytes of it the game reads (whole sectors for one of NDATA's, which
 * keeps only the count). 0 if the disc has none. */
static s32 port_FindStageFile(
    s32 stage, s32 kind, const u8 *ndata_inf, u32 *sector, u32 *bytes) {
    static const char * const folders[PORT_STAGE_FILE_KINDS][2] = {
        {"NDATA/", "NDATA/"},
        {"LOADSE/", "LOADSE1/"},
        {"U_PIC/", "TIM2D/"},
    };
    static const char * const names[PORT_STAGE_FILE_KINDS] = {
        "", "STAGE%02d.BIN", "STG%02d.BIN"};
    char path[PORT_DISC_PATH_SIZE];
    char name[PORT_DISC_PATH_SIZE];
    const PortDiscFile *file;
    u32 i;

    if (kind == PORT_STAGE_FILE_GROUND) {
        const u8 *entry =
            ndata_inf + (NDATA_STAGES + stage) * NDATA_ENTRY_BYTES;
        u32 start = entry[0] | (entry[1] << 8);
        u32 length = entry[2] | (entry[3] << 8);

        file = port_FindDiscFile("NDATA/NDATA.DAT");
        if (ndata_inf == NULL || file == NULL || start == NDATA_LEFT_OUT) {
            return 0;
        }
        *sector = file->sector + start;
        *bytes = length * PORT_DISC_DATA_BYTES;
        return 1;
    }
    port_DiscSay(name, sizeof(name), names[kind], stage);
    for (i = 0; i < 2; i++) {
        port_DiscSay(path, sizeof(path), "%s%s", folders[kind][i], name);
        file = port_FindDiscFile(path);
        if (file != NULL) {
            *sector = file->sector;
            *bytes = file->size;
            return 1;
        }
    }
    return 0;
}

/* The disc's file into the dump's folder, as KEY.dat or KEY.bin. */
static void port_DumpStageFile(const PortDisc *disc, const char *key, s32 kind,
                               u32 sector, u32 bytes, u8 *room) {
    char path[PATH_SIZE];
    u32 sectors = (bytes + PORT_DISC_DATA_BYTES - 1) / PORT_DISC_DATA_BYTES;
    s32 file;

    if (bytes > STAGE_FILE_MAX ||
        port_ReadDiscData(disc, sector, sectors, room, NULL) != PORT_DISC_OK) {
        port_Log("stages: %s cannot be read from the disc\n", key);
        return;
    }
    port_DiscSay(path, sizeof(path), "%s/%s.%s", g_port_disc_files.dump_folder,
                 key, kind == PORT_STAGE_FILE_GROUND ? "dat" : "bin");
    file = port_HostCreate(path);
    if (file < 0 || !port_HostAppend(file, room, bytes)) {
        port_Log("stages: %s cannot be written\n", path);
    }
    if (file >= 0) {
        port_HostClose(file);
    }
}

/* A mod's file for a stage's file (`what` is "stages") or a motion file
 * ("motions"): read, and kept to be read in place of the disc's sectors. */
static void port_AddStageOverride(const char *what, const char *key,
                                  const char *path, u32 sector, u32 bytes) {
    u32 sectors = (bytes + PORT_DISC_DATA_BYTES - 1) / PORT_DISC_DATA_BYTES;
    u32 size = port_ModFileSize(path);
    PortDiscOverride *override;

    if (size == 0 || size > sectors * PORT_DISC_DATA_BYTES) {
        port_Log("%s: %s (%s) is %u bytes, where the disc's file has "
                 "room for %u: left out\n",
                 what, key, path, size, sectors * PORT_DISC_DATA_BYTES);
        return;
    }
    if (g_port_disc_files.overrides == NULL) {
        g_port_disc_files.overrides =
            port_HostAllocate(OVERRIDES_MAX * sizeof(PortDiscOverride));
    }
    if (g_port_disc_files.overrides == NULL ||
        g_port_disc_files.override_count >= OVERRIDES_MAX) {
        return;
    }
    override = &g_port_disc_files.overrides[g_port_disc_files.override_count];
    override->data = port_HostAllocate(sectors * PORT_DISC_DATA_BYTES);
    if (override->data == NULL ||
        port_ModReadFile(path, override->data, size) != size) {
        port_Log("%s: %s (%s) cannot be read: left out\n", what, key, path);
        return;
    }
    override->sector = sector;
    override->count = sectors;
    if (g_port_disc_files.override_count == 0 ||
        sector < g_port_disc_files.lowest) {
        g_port_disc_files.lowest = sector;
    }
    if (sector + sectors > g_port_disc_files.highest) {
        g_port_disc_files.highest = sector + sectors;
    }
    g_port_disc_files.override_count++;
    port_Log("%s: %s from %s, %u bytes over the disc's sectors %u to %u\n",
             what, key, path, size, sector, sector + sectors - 1);
}

/* The motion files of the disc's MOTION/ (docs/formats/BBM.md): a mod's
 * `motions/u123.bbm` stands in for U123.BBM, its key the disc's name in
 * small letters without its ending, `common` for COMMON.BBM. */
static void port_StartMotionMods(void) {
    static const char motion_folder[] = "MOTION/";
    u32 i;

    for (i = 0; i < g_port_disc_files.count; i++) {
        const PortDiscFile *file = &g_port_disc_files.files[i];
        const char *name = file->path + file->name_at;
        char key[PORT_STAGE_KEY_SIZE];
        const char *path;
        u32 length = 0;

        u32 at;

        for (at = 0; at < file->name_at && motion_folder[at] != TEXT_END &&
                     port_DiscUpper(file->path[at]) == motion_folder[at];
             at++) {
        }
        if (at != file->name_at || motion_folder[at] != TEXT_END ||
            !port_TextEndsWith(name, ".BBM")) {
            continue;
        }
        while (name[length] != '.' && length + 1 < sizeof(key)) {
            key[length] = (char)(name[length] >= 'A' && name[length] <= 'Z'
                                     ? name[length] - 'A' + 'a'
                                     : name[length]);
            length++;
        }
        key[length] = TEXT_END;
        path = port_ModFind(PORT_MOD_MOTIONS, key);
        if (path != NULL) {
            port_AddStageOverride(
                "motions", key, path, file->sector, file->size);
        }
    }
}

void port_SetStageDumpFolder(const char *folder) {
    g_port_disc_files.dump_folder = folder;
}

/* Each stage's files found on the disc: written out for --dump-stages,
 * and the mods' own read to stand in for them (port_discfiles.h). */
void port_StartStageMods(const PortDisc *disc) {
    u8 *ndata_inf = port_HostAllocate(NDATA_INF_SECTORS * PORT_DISC_DATA_BYTES);
    const u8 *inf = NULL;
    const PortDiscFile *inf_file = port_FindDiscFile("NDATA/NDATA.INF");
    u8 *room = NULL;
    s32 stage;
    s32 kind;

    if (inf_file != NULL && ndata_inf != NULL &&
        port_ReadDiscData(disc, inf_file->sector, NDATA_INF_SECTORS, ndata_inf,
                          NULL) == PORT_DISC_OK) {
        inf = ndata_inf;
        g_port_disc_files.ndata_inf = ndata_inf;
    }
    if (g_port_disc_files.dump_folder != NULL) {
        port_HostMakeFolder(g_port_disc_files.dump_folder);
        room = port_HostAllocate(STAGE_FILE_MAX);
    }
    for (stage = 0; stage < PORT_STAGES; stage++) {
        for (kind = 0; kind < PORT_STAGE_FILE_KINDS; kind++) {
            char key[PORT_STAGE_KEY_SIZE];
            const char *path;
            u32 sector;
            u32 bytes;

            if (!port_StageFileKey(stage, kind, key, sizeof(key)) ||
                !port_FindStageFile(stage, kind, inf, &sector, &bytes)) {
                continue;
            }
            if (room != NULL) {
                port_DumpStageFile(disc, key, kind, sector, bytes, room);
            }
            path = port_ModFind(PORT_MOD_STAGES, key);
            if (path != NULL) {
                port_AddStageOverride("stages", key, path, sector, bytes);
            }
        }
    }
    if (room != NULL) {
        port_Log("stages: the stages' files written to %s\n",
                 g_port_disc_files.dump_folder);
    }
    port_StartMotionMods();
    if (g_port_disc_files.override_count > 0) {
        g_port_disc_override = port_OverrideDiscSector;
    }
}

/* An arena's ground over its host's sectors (port_discfiles.h). The
 * overrides are looked through in order, so it goes in front of the
 * list: it wins over a stage mod's file of the same sectors. */
s32 port_SetArenaGround(s32 stage, const u8 *data, u32 bytes, u32 *room) {
    PortDiscOverride *override;
    u32 sector;
    u32 length;
    u32 sectors;
    u32 i;

    *room = 0;
    if (!port_FindStageFile(stage, PORT_STAGE_FILE_GROUND,
                            g_port_disc_files.ndata_inf, &sector, &length)) {
        return FALSE;
    }
    *room = length;
    sectors = length / PORT_DISC_DATA_BYTES;
    if (bytes == 0 || bytes > length) {
        return FALSE;
    }
    if (g_port_disc_files.overrides == NULL) {
        g_port_disc_files.overrides =
            port_HostAllocate(OVERRIDES_MAX * sizeof(PortDiscOverride));
    }
    if (g_port_disc_files.overrides == NULL ||
        g_port_disc_files.override_count >= OVERRIDES_MAX) {
        return FALSE;
    }
    override = &g_port_disc_files.overrides[0];
    /* the others one place on, the arena's first */
    for (i = g_port_disc_files.override_count; i > 0; i--) {
        override[i] = override[i - 1];
    }
    override->data = port_HostAllocate(sectors * PORT_DISC_DATA_BYTES);
    if (override->data == NULL) {
        for (i = 0; i < g_port_disc_files.override_count; i++) {
            override[i] = override[i + 1];
        }
        return FALSE;
    }
    /* the rest of the host's sectors read as zeros, as a shorter mod's */
    for (i = 0; i < sectors * PORT_DISC_DATA_BYTES; i++) {
        override->data[i] = i < bytes ? data[i] : 0;
    }
    override->sector = sector;
    override->count = sectors;
    if (g_port_disc_files.override_count == 0 ||
        sector < g_port_disc_files.lowest) {
        g_port_disc_files.lowest = sector;
    }
    if (sector + sectors > g_port_disc_files.highest) {
        g_port_disc_files.highest = sector + sectors;
    }
    g_port_disc_files.override_count++;
    g_port_disc_override = port_OverrideDiscSector;
    return TRUE;
}

/* A mod's data in place of the disc's, in a sector it covers
 * (port_discfiles.h). */
s32 port_OverrideDiscSector(u32 sector, u8 *raw) {
    u32 i;

    if (sector < g_port_disc_files.lowest ||
        sector >= g_port_disc_files.highest) {
        return 0;
    }
    for (i = 0; i < g_port_disc_files.override_count; i++) {
        const PortDiscOverride *override = &g_port_disc_files.overrides[i];
        const u8 *from;
        u32 j;

        if (sector < override->sector ||
            sector - override->sector >= override->count) {
            continue;
        }
        from =
            override->data + (sector - override->sector) * PORT_DISC_DATA_BYTES;
        for (j = 0; j < PORT_DISC_DATA_BYTES; j++) {
            raw[PORT_DISC_DATA_OFFSET + j] = from[j];
        }
        return 1;
    }
    return 0;
}
