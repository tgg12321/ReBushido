/* The native port's reader of a disc image (port_disc.h). It knows
 * nothing of the game: sectors by number out of a file of raw sectors.
 * tests/port/disc.c runs it on a tiny made-up image. */
#include "common.h"
#include "port_disc.h"
#include "port_host.h"
#include "bb2.h"

#define SECTORS_PER_SECOND 75
/* Two decimal digits in a byte: the tens in its high half. */
#define BCD_TENS_SHIFT 4
#define BCD_UNITS_MASK 0xF
#define BCD_BASE 10
/* A cue sheet's line that names the image: this word, then the name in
 * double quotes. */
#define CUE_FILE_WORD "FILE"
#define CUE_FILE_WORD_LENGTH 4
#define POSITION_MINUTE 0
#define POSITION_SECOND 1
#define POSITION_FRAME 2
/* The fourth byte of a sector's header. */
#define HEADER_MODE 3
#define MODE_2 2

/* A cue sheet is a few lines; one this long is something else. */
#define CUE_BYTES_MAX 0x4000
#define PATH_MAX_BYTES 1024

/* Sectors read since the program started (port_disc.h). */
u32 g_port_disc_sectors_read;

/* ---- positions ---- */

static u8 port_ToBcd(u32 number) {
    return (u8)(((number / BCD_BASE) << BCD_TENS_SHIFT) | (number % BCD_BASE));
}

/* -1 if the byte is not two decimal digits. */
static s32 port_FromBcd(u8 byte) {
    s32 tens = byte >> BCD_TENS_SHIFT;
    s32 units = byte & BCD_UNITS_MASK;

    if (tens >= BCD_BASE || units >= BCD_BASE) {
        return -1;
    }
    return tens * BCD_BASE + units;
}

void port_DiscSectorToPosition(u32 sector, u8 *position) {
    sector += PORT_DISC_LEAD_IN;
    position[POSITION_FRAME] = port_ToBcd(sector % SECTORS_PER_SECOND);
    sector /= SECTORS_PER_SECOND;
    position[POSITION_SECOND] = port_ToBcd(sector % SECONDS_PER_MINUTE);
    position[POSITION_MINUTE] = port_ToBcd(sector / SECONDS_PER_MINUTE);
}

/* The sector a position names (port_disc.h). */
s32 port_DiscPositionToSector(const u8 *position) {
    s32 minute = port_FromBcd(position[POSITION_MINUTE]);
    s32 second = port_FromBcd(position[POSITION_SECOND]);
    s32 frame = port_FromBcd(position[POSITION_FRAME]);
    s32 sector;

    if (minute < 0 || second < 0 || frame < 0 || second >= SECONDS_PER_MINUTE ||
        frame >= SECTORS_PER_SECOND) {
        return -1;
    }
    sector = (minute * SECONDS_PER_MINUTE + second) * SECTORS_PER_SECOND +
             frame - PORT_DISC_LEAD_IN;
    return sector < 0 ? -1 : sector;
}

/* ---- opening ---- */

/* The name between the quotes of a cue sheet's first FILE line, put after
 * the cue sheet's own folder; 0 if there is no such line or no room. */
static s32 port_FindCueImage(
    const char *cue_path, const char *cue, char *image_path, u32 room) {
    const char *line = cue;
    u32 folder = 0;
    u32 length = 0;
    u32 i;

    for (i = 0; cue_path[i] != 0; i++) {
        if (cue_path[i] == '/' || cue_path[i] == '\\') {
            folder = i + 1;
        }
    }
    while (*line != 0) {
        while (
            *line == ' ' || *line == '\t' || *line == '\r' || *line == '\n') {
            line++;
        }
        for (i = 0; i < CUE_FILE_WORD_LENGTH; i++) {
            if (line[i] != CUE_FILE_WORD[i]) {
                break;
            }
        }
        if (i == CUE_FILE_WORD_LENGTH && (line[i] == ' ' || line[i] == '\t')) {
            const char *name = line + i + 1;

            while (*name == ' ' || *name == '\t') {
                name++;
            }
            if (*name != '"') {
                return 0;
            }
            name++;
            while (name[length] != 0 && name[length] != '"' &&
                   name[length] != '\n') {
                length++;
            }
            if (name[length] != '"' || folder + length + 1 > room) {
                return 0;
            }
            for (i = 0; i < folder; i++) {
                image_path[i] = cue_path[i];
            }
            for (i = 0; i < length; i++) {
                image_path[folder + i] = name[i];
            }
            image_path[folder + length] = 0;
            return 1;
        }
        while (*line != 0 && *line != '\n') {
            line++;
        }
    }
    return 0;
}

/* Opens an image by its own name or its cue sheet's (port_disc.h). */
const char *port_OpenDisc(PortDisc *disc, const char *path) {
    char image_path[PATH_MAX_BYTES];
    u32 size;

    disc->file = -1;
    disc->sector_count = 0;
    if (port_TextEndsWith(path, ".cue")) {
        static char cue[CUE_BYTES_MAX + 1];
        s32 file = port_HostOpen(path);
        u32 cue_size;

        if (file < 0) {
            return "the cue sheet cannot be opened";
        }
        cue_size = port_HostFileSize(file);
        if (cue_size == 0 || cue_size > CUE_BYTES_MAX ||
            port_HostReadAt(file, 0, cue, cue_size) != cue_size) {
            port_HostClose(file);
            return "the cue sheet cannot be read, or is no cue sheet";
        }
        port_HostClose(file);
        cue[cue_size] = 0;
        if (!port_FindCueImage(path, cue, image_path, sizeof(image_path))) {
            return "the cue sheet has no FILE \"...\" line";
        }
        path = image_path;
    }
    disc->file = port_HostOpen(path);
    if (disc->file < 0) {
        return "the image cannot be opened";
    }
    size = port_HostFileSize(disc->file);
    if (size == 0 || size % PORT_DISC_SECTOR_BYTES != 0) {
        port_CloseDisc(disc);
        return "the image is not whole raw sectors of 2352 bytes (a .bin)";
    }
    disc->sector_count = size / PORT_DISC_SECTOR_BYTES;
    return NULL;
}

void port_CloseDisc(PortDisc *disc) {
    if (disc->file >= 0) {
        port_HostClose(disc->file);
    }
    disc->file = -1;
    disc->sector_count = 0;
}

/* ---- reading ---- */

s32 (*g_port_disc_override)(u32 sector, u8 *raw);

/* A raw sector, checked for its sync and its position, and given to
 * the mods' stage files to put theirs in (port_disc.h). */
s32 port_ReadDiscSector(const PortDisc *disc, u32 sector, u8 *raw) {
    u8 position[PORT_DISC_HEADER_BYTES];
    u32 i;

    if (disc->file < 0 || sector >= disc->sector_count) {
        return PORT_DISC_OUT_OF_RANGE;
    }
    g_port_disc_sectors_read++;
    if (port_HostReadAt(disc->file, sector * PORT_DISC_SECTOR_BYTES, raw,
                        PORT_DISC_SECTOR_BYTES) != PORT_DISC_SECTOR_BYTES) {
        return PORT_DISC_READ_FAILED;
    }
    /* the sync: a zero, ten bytes of ones, a zero */
    if (raw[0] != 0 || raw[PORT_DISC_SYNC_BYTES - 1] != 0) {
        return PORT_DISC_NOT_A_SECTOR;
    }
    for (i = 1; i < PORT_DISC_SYNC_BYTES - 1; i++) {
        if (raw[i] != 0xFF) {
            return PORT_DISC_NOT_A_SECTOR;
        }
    }
    port_DiscSectorToPosition(sector, position);
    for (i = 0; i < HEADER_MODE; i++) {
        if (raw[PORT_DISC_SYNC_BYTES + i] != position[i]) {
            return PORT_DISC_NOT_A_SECTOR;
        }
    }
    if (g_port_disc_override != NULL) {
        g_port_disc_override(sector, raw);
    }
    return PORT_DISC_OK;
}

/* The data of a run of mode 2 form 1 sectors (port_disc.h). */
s32 port_ReadDiscData(
    const PortDisc *disc, u32 sector, u32 count, u8 *dest, u32 *done) {
    u8 raw[PORT_DISC_SECTOR_BYTES];
    u32 read = 0;
    s32 result = PORT_DISC_OK;

    for (; read < count; read++) {
        const u8 *subheader =
            raw + PORT_DISC_SYNC_BYTES + PORT_DISC_HEADER_BYTES;
        u32 i;

        result = port_ReadDiscSector(disc, sector + read, raw);
        if (result != PORT_DISC_OK) {
            break;
        }
        if (raw[PORT_DISC_SYNC_BYTES + HEADER_MODE] != MODE_2 ||
            (subheader[PORT_DISC_SUBMODE] & PORT_DISC_SUBMODE_FORM2)) {
            result = PORT_DISC_NOT_DATA;
            break;
        }
        for (i = 0; i < PORT_DISC_DATA_BYTES; i++) {
            dest[read * PORT_DISC_DATA_BYTES + i] =
                raw[PORT_DISC_DATA_OFFSET + i];
        }
    }
    if (done != NULL) {
        *done = read;
    }
    return result;
}
