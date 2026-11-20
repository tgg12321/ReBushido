#ifndef PORT_DISC_H
#define PORT_DISC_H

/* The native port's reader of a disc image (src/port/disc.c; docs/
 * PORT.md, "The disc"): the player's own image of the game's disc, a
 * `.bin` of raw sectors, 2352 bytes each, with or without the `.cue`
 * that names it. The game never asks for a file by name: it asks the
 * drive for sectors by their position, and this is where they come
 * from. */

#include "common.h"

/* A raw sector: twelve bytes of sync, the sector's own position and mode
 * (four), and then, on the game's disc, which is all mode 2: a subheader
 * (eight) and the data, 2048 bytes of it in form 1. */
#define PORT_DISC_SECTOR_BYTES 2352
#define PORT_DISC_SYNC_BYTES 12
#define PORT_DISC_HEADER_BYTES 4
#define PORT_DISC_SUBHEADER_BYTES 8
#define PORT_DISC_DATA_OFFSET 24
#define PORT_DISC_DATA_BYTES 2048
/* The subheader's third byte, the submode: this bit says form 2, whose
 * data is 2324 bytes of sound or picture and not a file's. */
#define PORT_DISC_SUBMODE 2
#define PORT_DISC_SUBMODE_FORM2 0x20
/* A disc's first sector is two seconds in, 75 sectors a second: sector 0
 * is at 00:02:00. */
#define PORT_DISC_LEAD_IN 150

/* How a read ended. */
enum PortDiscResult {
    PORT_DISC_OK = 0,
    PORT_DISC_OUT_OF_RANGE = 1, /* the image has no such sector */
    PORT_DISC_READ_FAILED = 2,  /* the file could not be read */
    PORT_DISC_NOT_A_SECTOR = 3, /* no sync, or not the position asked for */
    PORT_DISC_NOT_DATA = 4,     /* not mode 2 form 1 */
};

typedef struct PortDisc {
    s32 file;         /* the image, open (port_host.h) */
    u32 sector_count; /* whole sectors in it, from the first of the data */
    u32 first; /* the file's sectors before the data's first: a pregap the
                  cue sheet's INDEX 01 says the file has (0 for most) */
} PortDisc;

/* Opens the image `path` names: a `.bin`, or a `.cue` whose first FILE
 * line names one beside it. Returns NULL if it is one, or what is wrong
 * with it, in words. */
extern const char *port_OpenDisc(PortDisc *disc, const char *path);
extern void port_CloseDisc(PortDisc *disc);

/* The raw sector `sector` (0 is the disc's first, at 00:02:00) into
 * `raw`, PORT_DISC_SECTOR_BYTES of it, checked for its sync and its own
 * position. */
extern s32 port_ReadDiscSector(const PortDisc *disc, u32 sector, u8 *raw);
/* If not NULL, each sector read is given to this as well, which may put
 * other data in it: a mod's stage file over the disc's
 * (port_discfiles.h). Set once, as the program starts. */
extern s32 (*g_port_disc_override)(u32 sector, u8 *raw);
/* The data of `count` sectors from `sector` on, 2048 bytes each, one after
 * another into `dest`. Stops at the first that cannot be read or is not
 * mode 2 form 1, with that sector's result; `*done`, if not NULL, is how
 * many were read. */
extern s32 port_ReadDiscData(
    const PortDisc *disc, u32 sector, u32 count, u8 *dest, u32 *done);

/* How many sectors have been read: a step that read many loaded something,
 * and netplay makes the frame after it a point both programs wait at
 * (netplay.c). It is part of the state, and goes back with a load. */
extern u32 g_port_disc_sectors_read;

/* A position as the drive is given it and as a sector says its own: the
 * minute, second and frame, two decimal digits in a byte each. */
extern void port_DiscSectorToPosition(u32 sector, u8 *position);
/* -1 if a byte is not two decimal digits or the position is before the
 * disc's first sector. */
extern s32 port_DiscPositionToSector(const u8 *position);

#endif /* PORT_DISC_H */
