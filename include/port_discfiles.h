#ifndef PORT_DISCFILES_H
#define PORT_DISCFILES_H

/* The native port's view of the disc's files (src/port/discfiles.c;
 * docs/MODS.md, "Stages"). The game asks the drive for sectors, never for
 * a file by name, but the disc has a directory (ISO 9660) all the same:
 * it is read once, as the program starts, so that a piece of music can be
 * named by its file (port_music.h) and a mod's stage file can be read in
 * place of the disc's sectors of the file it replaces. None of it is the
 * game's state. */

#include "common.h"

struct PortDisc;

/* A path on the disc, `LOADSE/BB2_M1.XA`: the ISO 9660 names have at
 * most eight letters, a dot and three, and the game's folders are one
 * deep. */
#define PORT_DISC_PATH_SIZE 64

/* A file of the disc's directory. */
typedef struct {
    char path[PORT_DISC_PATH_SIZE]; /* without its version, `;1` */
    u32 name_at;                    /* where its name begins in `path` */
    u32 sector;                     /* its first */
    u32 size;                       /* in bytes */
} PortDiscFile;

/* ---- the directory ---- */

/* Reads the directory of the disc in the drive: how many files it has,
 * 0 if it has none that can be read. */
extern s32 port_ReadDiscFiles(const struct PortDisc *disc);
/* The file of this path (letters of either case), or NULL. */
extern const PortDiscFile *port_FindDiscFile(const char *path);
/* The file `sector` is in, or NULL. */
extern const PortDiscFile *port_DiscFileAt(u32 sector);

/* The files of a directory as it is on the disc: `count` sectors of it
 * from `data`, 2048 bytes each, added to `files` (room for `room`) with
 * `folder` (`LOADSE/`, or empty for the root) before each name. Each
 * folder in it is added as well, its size and sector in a file whose path
 * ends with `/`. How many there are in `files` after. */
extern u32 port_ReadDiscFolder(const u8 *data, u32 count, const char *folder,
                               PortDiscFile *files, u32 have, u32 room);

/* ---- the mods' stage files over the disc's ---- */

/* The keys of a stage's files, for stage `stage` (0 to 37): `s07` is its
 * geometry and textures (NDATA's `S07`), `stage07` its sound banks and
 * billboards (`STAGE07.BIN`), `stg07` the card story mode shows (0 to
 * 31). Needs 16 bytes. 0 if the stage has no such file. */
enum {
    PORT_STAGE_FILE_GROUND, /* s07: NDATA.DAT's S07 */
    PORT_STAGE_FILE_SOUND,  /* stage07: LOADSE(1)/STAGE07.BIN */
    PORT_STAGE_FILE_CARD,   /* stg07: U_PIC/STG07.BIN or TIM2D's */
    PORT_STAGE_FILE_KINDS
};

#define PORT_STAGES 38
#define PORT_STAGE_CARDS 32
#define PORT_STAGE_KEY_SIZE 16
extern s32 port_StageFileKey(s32 stage, s32 kind, char *key, u32 room);

/* `--dump-stages DIR`: every stage's files are written there from the
 * disc, under their keys (`s07.dat`, `stage07.bin`, `stg07.bin`), for a
 * mod to begin from. NULL for none. */
extern void port_SetStageDumpFolder(const char *folder);
/* As the mods have been read (port_ModsStart): each stage file of theirs
 * that fits where the disc's is will be read in its place, and the dump
 * is written. */
extern void port_StartStageMods(const struct PortDisc *disc);
/* A sector as the disc reader hands it out (`raw`, 2352 bytes): its data
 * replaced by a mod's, where a mod's file covers it. 1 if it was. */
extern s32 port_OverrideDiscSector(u32 sector, u8 *raw);
/* An arena's ground (port_arenas.h), `bytes` of `data`, read in place of
 * stage `stage`'s ground on the disc, after port_StartStageMods: FALSE if
 * the stage has none or it is longer than the disc's file, whose length
 * (whole sectors) is put in `room` either way. */
extern s32 port_SetArenaGround(s32 stage, const u8 *data, u32 bytes, u32 *room);

#endif /* PORT_DISCFILES_H */
