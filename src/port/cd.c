/* Sony's libcd in the native port (docs/PORT.md, "The disc"): the
 * drive the game talks to, with the sectors coming from the player's
 * disc image (port_disc.h). The game's own disc code (cdrom.c) is
 * unchanged above this: it sends the drive commands, looks whether each
 * is done, and takes a file's sectors one at a time in a callback.
 *
 * There is no drive here to wait for, so:
 *
 *   - a command is done as it is given, and CdSync says so from the next
 *     time it is asked;
 *   - a read's sectors all arrive, one call of the game's callback each,
 *     the next time the game asks the drive anything (CdSync, CdReady),
 *     until the callback stops the read;
 *   - CdRead, Sony's own reader that the game waits on, has read
 *     everything when it returns.
 *
 * So a load takes as many of the game's frames as its commands take to
 * send, and none for the sectors: fewer than on the console, and always
 * the same number.
 *
 * Sound off the disc (docs/PORT.md, "Sound"): while a stream of XA
 * sound plays, the drive's position moves at the disc's speed by the
 * sound's clock, which is the port's clock counted in samples
 * (port_GetSampleCount), because the game finds the end of a piece of
 * music by asking where the drive is; and each sector that passes under
 * the drive, if it is sound of the file and channel the game chose,
 * goes to the decoder (xa.c). A CD audio track is still a stub that is
 * counted, and only the position moves: the game's disc has none, and
 * the game never asks for one. */
#include "common.h"
#include "port_disc.h"
#include "port_movie.h"
#include "port_spu.h"
#include "bb2.h"

#define RESULT_BYTES 8
/* With CdlModeSize1 a sector is handed over from its own position on:
 * 2340 bytes. Without it, the 2048 of its data. */
#define SIZE1_OFFSET PORT_DISC_SYNC_BYTES
/* Sectors a second at the drive's single speed. */
#define SECTORS_PER_SECOND 75
/* A sector's own file and channel, in its subheader. */
#define SUBHEADER_FILE (PORT_DISC_SYNC_BYTES + PORT_DISC_HEADER_BYTES)
#define SUBHEADER_CHANNEL (SUBHEADER_FILE + 1)
/* The answers of CdlGetlocP and CdlGetTN, by byte. */
#define GETLOCP_TRACK 0
#define GETLOCP_INDEX 1
#define GETLOCP_RELATIVE 2
#define GETLOCP_ABSOLUTE 5
#define GETTN_FIRST 1
#define GETTN_LAST 2
/* How many times an unreadable sector is reported before the port stops
 * saying so. */
#define COMPLAINTS_MAX 5

typedef struct {
    PortDisc *disc;
    u8 mode;   /* CdlMode... */
    u8 status; /* CdlStat... */
    CdlFILTER filter;
    s32 position; /* the sector the drive was last sent to */
    s32 sync;     /* what CdSync answers */
    u8 result[RESULT_BYTES];
    CdlCB ready_callback;
    /* a read is under way: the sectors from next_sector on go to the
     * callback */
    s32 reading;
    s32 next_sector;
    /* sound is playing from stream_sector since the sample
     * stream_sample of the sound's clock; if it is an XA stream, the
     * sectors before next_sound have been given to the decoder */
    s32 streaming;
    s32 stream_sector;
    u32 stream_sample;
    s32 stream_is_xa;
    s32 next_sound;
    /* the sector CdGetSector hands out, and how far it has */
    u8 sector[PORT_DISC_SECTOR_BYTES];
    u32 sector_cursor;
    /* what CdReadSync answers: 0, or -1 if CdRead failed */
    s32 read_left;
    u32 complaints;
} PortCd;

static PortCd g_port_cd;

void port_InsertDisc(struct PortDisc *disc) { g_port_cd.disc = disc; }

/* A raw sector of the disc in the drive (port_movie.h): what the movie's
 * stream reads as each sector passes under the head. */
s32 port_DriveReadSector(s32 sector, u8 *raw) {
    if (g_port_cd.disc == NULL || sector < 0) {
        return 0;
    }
    return port_ReadDiscSector(g_port_cd.disc, (u32)sector, raw) ==
           PORT_DISC_OK;
}

/* ---- positions ---- */

CdlLOC *CdIntToPos(s32 sector, CdlLOC *loc) {
    port_DiscSectorToPosition((u32)sector, &loc->minute);
    return loc;
}

/* Sony's takes any byte for two digits, and so does this: the game gives
 * it bytes out of a sector's header. */
s32 CdPosToInt(CdlLOC *loc) {
    s32 minute = btoi(loc->minute);
    s32 second = btoi(loc->second);
    s32 frame = btoi(loc->sector);

    return (minute * SECONDS_PER_MINUTE + second) * SECTORS_PER_SECOND + frame -
           PORT_DISC_LEAD_IN;
}

/* Where the drive is, or will be `ahead` samples from now. */
static s32 port_CdAtSample(u32 ahead) {
    u32 samples;
    u32 speed;

    if (g_port_cd.reading) {
        return g_port_cd.next_sector;
    }
    if (!g_port_cd.streaming) {
        return g_port_cd.position;
    }
    samples = port_GetSampleCount() + ahead - g_port_cd.stream_sample;
    speed = (g_port_cd.mode & CdlModeSpeed) ? 2 * SECTORS_PER_SECOND
                                            : SECTORS_PER_SECOND;
    /* whole seconds apart, so that an hour's samples fit the product */
    return g_port_cd.stream_sector +
           (s32)(samples / PORT_SPU_RATE * speed +
                 samples % PORT_SPU_RATE * speed / PORT_SPU_RATE);
}

/* Where the drive is now. */
static s32 port_CdNowAt(void) { return port_CdAtSample(0); }

/* ---- commands ---- */

/* The window's log of what is read (port_platform.h): nothing in the
 * build without one. */
void (*g_port_disc_hook)(s32 kind, u32 sector);

static void port_CdStopMoving(void) {
    if (g_port_cd.reading && g_port_disc_hook != NULL) {
        g_port_disc_hook(0, (u32)g_port_cd.next_sector);
    }
    g_port_cd.position = port_CdNowAt();
    g_port_cd.reading = 0;
    g_port_cd.streaming = 0;
    g_port_cd.stream_is_xa = 0;
    port_StopDiscStream();
    g_port_cd.status &= (u8) ~(CdlStatRead | CdlStatPlay | CdlStatSeek);
}

static void port_CdStartStream(s32 is_xa) {
    g_port_cd.streaming = 1;
    g_port_cd.stream_sector = g_port_cd.position;
    g_port_cd.stream_sample = port_GetSampleCount();
    g_port_cd.stream_is_xa = is_xa;
    g_port_cd.next_sound = g_port_cd.position;
    if (is_xa) {
        port_ResetCdAudio();
    }
}

/* The drive goes on for `frames` samples (port_spu.h): the sectors of
 * an XA stream that pass under it in that time go to the decoder, each
 * if it is sound and, where the mode says to choose, of the file and
 * channel chosen. One that cannot be read is passed over, as the drive
 * passes over a sector it cannot make out. */
void port_PlayDisc(u32 frames) {
    u8 raw[PORT_DISC_SECTOR_BYTES];
    s32 end;

    /* a movie's stream turns the drive itself, by the scan line the
     * clock has reached, and takes the picture as well as the sound */
    if (port_PumpStream()) {
        return;
    }
    if (!g_port_cd.streaming || !g_port_cd.stream_is_xa ||
        g_port_cd.disc == NULL) {
        return;
    }
    end = port_CdAtSample(frames);
    for (; g_port_cd.next_sound < end; g_port_cd.next_sound++) {
        if (g_port_cd.next_sound < 0 ||
            port_ReadDiscSector(g_port_cd.disc, (u32)g_port_cd.next_sound,
                                raw) != PORT_DISC_OK) {
            continue;
        }
        if ((g_port_cd.mode & CdlModeSF) &&
            (raw[SUBHEADER_FILE] != g_port_cd.filter.file ||
             raw[SUBHEADER_CHANNEL] != g_port_cd.filter.chan)) {
            continue;
        }
        port_FeedXaSector(raw);
    }
}

/* The stream of sound playing (port_spu.h). */
s32 port_GetCdStream(u32 *sector, s32 *channel, u32 *since) {
    if (!g_port_cd.streaming || !g_port_cd.stream_is_xa) {
        return 0;
    }
    *sector = (u32)g_port_cd.stream_sector;
    *channel = (g_port_cd.mode & CdlModeSF) ? g_port_cd.filter.chan : -1;
    *since = g_port_cd.stream_sample;
    return 1;
}

/* Does a command; the drive's answer is left in g_port_cd.result. Returns
 * 1 if the drive took it. */
static s32 port_CdCommand(u8 com, const u8 *argument) {
    u32 i;

    for (i = 0; i < RESULT_BYTES; i++) {
        g_port_cd.result[i] = 0;
    }
    g_port_cd.sync = CdlComplete;
    switch (com) {
    case CdlNop:
    case CdlStandby:
        break;
    case CdlMute:
        port_MuteCdAudio(1);
        break;
    case CdlDemute:
        port_MuteCdAudio(0);
        break;
    case CdlSetloc:
        if (argument != NULL) {
            CdlLOC loc;

            loc.minute = argument[0];
            loc.second = argument[1];
            loc.sector = argument[2];
            loc.track = 0;
            port_CdStopMoving();
            g_port_cd.position = CdPosToInt(&loc);
        }
        break;
    case CdlSeekL:
    case CdlSeekP:
        port_CdStopMoving();
        break;
    case CdlSetmode:
        if (argument != NULL) {
            g_port_cd.mode = argument[0];
        }
        break;
    case CdlSetfilter:
        if (argument != NULL) {
            g_port_cd.filter.file = argument[0];
            g_port_cd.filter.chan = argument[1];
        }
        break;
    case CdlReadN:
    case CdlReadS:
        port_CdStopMoving();
        g_port_cd.status |= CdlStatRead;
        if (port_StreamArmed()) {
            /* a movie: its sectors pass under the head at the disc's
             * own speed, the picture into the streaming library's ring
             * and the sound to the drive's decoder (stream.c) */
            port_CdStartStream(1);
            port_StartDiscStream(
                g_port_cd.position, (g_port_cd.mode & CdlModeSpeed) != 0);
        } else if (g_port_cd.mode & CdlModeRT) {
            /* the drive plays these sectors itself (port_PlayDisc) */
            port_CdStartStream(1);
        } else {
            g_port_cd.reading = 1;
            g_port_cd.next_sector = g_port_cd.position;
        }
        if (g_port_disc_hook != NULL) {
            g_port_disc_hook(
                (g_port_cd.mode & CdlModeRT) ? 2 : 1, (u32)g_port_cd.position);
        }
        break;
    case CdlPlay:
        PORT_STUB("CdControl: a CD audio track (CdlPlay, sound)");
        port_CdStopMoving();
        g_port_cd.status |= CdlStatPlay;
        port_CdStartStream(0);
        break;
    case CdlPause:
    case CdlStop:
        port_CdStopMoving();
        break;
    case CdlGetlocP: {
        u32 now = (u32)port_CdNowAt();

        g_port_cd.result[GETLOCP_TRACK] = 1;
        g_port_cd.result[GETLOCP_INDEX] = 1;
        /* the disc is one track, which begins at its first sector */
        port_DiscSectorToPosition(
            now >= PORT_DISC_LEAD_IN ? now - PORT_DISC_LEAD_IN : 0,
            &g_port_cd.result[GETLOCP_RELATIVE]);
        port_DiscSectorToPosition(now, &g_port_cd.result[GETLOCP_ABSOLUTE]);
        return 1;
    }
    case CdlGetTN:
        g_port_cd.result[GETTN_FIRST] = 1;
        g_port_cd.result[GETTN_LAST] = 1;
        break;
    default:
        PORT_STUB("CdControl: a command the port does not know");
        port_Log("rebushido: the drive was sent command 0x%02X, which the "
                 "port does not know\n",
                 com);
        break;
    }
    g_port_cd.result[0] = g_port_cd.status;
    return 1;
}

static void port_CdCopyResult(u8 *result) {
    u32 i;

    if (result != NULL) {
        for (i = 0; i < RESULT_BYTES; i++) {
            result[i] = g_port_cd.result[i];
        }
    }
}

/* Says that a sector could not be read, and why (enum PortDiscResult):
 * the first few times. */
static void port_CdComplain(s32 sector, s32 result) {
    static const char * const reasons[] = {
        "",
        "the image has no such sector",
        "the image's file could not be read",
        "what is there is not that sector",
        "it is not a sector of data",
    };

    if (g_port_cd.complaints < COMPLAINTS_MAX) {
        g_port_cd.complaints++;
        port_Log("rebushido: sector %d of the disc cannot be read: %s\n",
                 sector, reasons[result]);
    }
}

/* The sectors of the read under way, each to the game's callback, until
 * the callback has stopped the read. */
static void port_CdDeliverSectors(void) {
    while (g_port_cd.reading && g_port_cd.ready_callback != NULL) {
        s32 result = PORT_DISC_OUT_OF_RANGE;

        if (g_port_cd.disc != NULL && g_port_cd.next_sector >= 0) {
            result = port_ReadDiscSector(
                g_port_cd.disc, (u32)g_port_cd.next_sector, g_port_cd.sector);
        }
        if (result != PORT_DISC_OK) {
            port_CdComplain(g_port_cd.next_sector, result);
            port_CdStopMoving();
            g_port_cd.status |= CdlStatError;
            g_port_cd.result[0] = g_port_cd.status;
            g_port_cd.ready_callback(CdlDiskError, g_port_cd.result);
            g_port_cd.status &= (u8)~CdlStatError;
            return;
        }
        g_port_cd.sector_cursor =
            (g_port_cd.mode & CdlModeSize1) ? SIZE1_OFFSET
                                            : PORT_DISC_DATA_OFFSET;
        g_port_cd.next_sector++;
        g_port_cd.result[0] = g_port_cd.status;
        g_port_cd.ready_callback(CdlDataReady, g_port_cd.result);
    }
}

/* ---- Sony's names ---- */

s32 CdInit(void) {
    PortDisc *disc = g_port_cd.disc;
    u32 complaints = g_port_cd.complaints;
    u8 *bytes = (u8 *)&g_port_cd;
    u32 i;

    for (i = 0; i < sizeof(g_port_cd); i++) {
        bytes[i] = 0;
    }
    g_port_cd.disc = disc;
    g_port_cd.complaints = complaints;
    g_port_cd.status = CdlStatStandby;
    g_port_cd.result[0] = g_port_cd.status;
    g_port_cd.sync = CdlComplete;
    /* Sony's takes the library's callbacks off the drive as well, which
     * is what ends a stream the movie player left running when the
     * cross button skipped it (stream.c). */
    CdDataCallback(NULL);
    /* and Sony's sets the sound chip's volumes and takes its CD input
     * (CD_initvol, through CdReset(1)), without which nothing off the
     * disc is heard until the game turns it on at the main menu: the
     * opening movie would be silent, and on the console it is not
     * (port_spu.h, port_InitCdSpuVolume). */
    port_InitCdSpuVolume();
    port_ResetCdAudio();
    port_MuteCdAudio(0);
    return 1;
}

s32 CdSetDebug(s32 level) {
    (void)level;
    return 0;
}

void CdFlush(void) { g_port_cd.sync = CdlComplete; }

s32 CdMix(CdlATV *vol) {
    port_SetCdMix(vol->val0, vol->val1, vol->val2, vol->val3);
    return 1;
}

s32 CdControl(u8 com, u8 *argument, u8 *result) {
    s32 taken = port_CdCommand(com, argument);

    port_CdCopyResult(result);
    return taken;
}

s32 CdControlB(u8 com, u8 *argument, u8 *result) {
    return CdControl(com, argument, result);
}

s32 CdControlF(u8 com, u8 *argument) { return port_CdCommand(com, argument); }

s32 CdSync(s32 mode, u8 *result) {
    (void)mode;
    port_CdDeliverSectors();
    port_CdCopyResult(result);
    return g_port_cd.sync;
}

/* Only a drive playing CD audio with its reports on has anything to say
 * here, and the port plays none: nothing is ever ready. */
s32 CdReady(s32 mode, u8 *result) {
    (void)mode;
    port_CdDeliverSectors();
    port_CdCopyResult(result);
    return CdlNoIntr;
}

CdlCB CdReadyCallback(CdlCB func) {
    CdlCB before = g_port_cd.ready_callback;

    g_port_cd.ready_callback = func;
    return before;
}

/* The next `size` words of the sector that has arrived. */
s32 CdGetSector(void *madr, s32 size) {
    u8 *dest = madr;
    u32 count = (u32)size * 4;
    u32 i;

    if (g_port_cd.sector_cursor + count > PORT_DISC_SECTOR_BYTES) {
        return 0;
    }
    for (i = 0; i < count; i++) {
        dest[i] = g_port_cd.sector[g_port_cd.sector_cursor + i];
    }
    g_port_cd.sector_cursor += count;
    return 1;
}

/* Sony's reader of whole sectors of data, from where the drive was last
 * sent. The game waits on it with CdReadSync, with no frame between, so a
 * sector that cannot be read stops the program here: the game would ask
 * again for ever. */
s32 CdRead(s32 sectors, u32 *buf, s32 mode) {
    u32 done = 0;
    s32 result = PORT_DISC_OUT_OF_RANGE;

    g_port_cd.mode = (u8)mode;
    port_CdStopMoving();
    if (g_port_cd.disc != NULL && g_port_cd.position >= 0 && sectors >= 0) {
        result = port_ReadDiscData(g_port_cd.disc, (u32)g_port_cd.position,
                                   (u32)sectors, (u8 *)buf, &done);
    }
    if (result != PORT_DISC_OK) {
        port_CdComplain(g_port_cd.position + (s32)done, result);
        port_Fail("rebushido: the game cannot go on without it\n");
    }
    g_port_cd.position += sectors;
    g_port_cd.read_left = 0;
    return 1;
}

s32 CdReadSync(s32 mode, u8 *result) {
    (void)mode;
    port_CdCopyResult(result);
    return g_port_cd.read_left;
}
