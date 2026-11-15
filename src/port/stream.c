/* The disc's streaming half in the native port (docs/PORT.md, "The
 * movie"): what fills the ring buffer Sony's streaming library hands a
 * movie's frames out of.
 *
 * Sony's St... calls are plain C on that ring and are compiled into the
 * port as they are (movovl/sdk/libcd/). Two of them are not, by the rule
 * that Sony's code is replaced where it talks to the console: the
 * handler that takes a sector in, which reads the drive's registers and
 * moves the sector by the DMA controller, and StUnSetRing, which clears
 * the drive's registers. Their work is here.
 *
 * On the console the drive raises an interrupt as each sector passes
 * under the head, every 150th of a second at double speed, and the
 * handler copies it into the ring; the player waits in StGetNext until a
 * whole frame is there. The port has no interrupts, so the drive is
 * turned by the port's own clock: port_PumpStream gives the stream every
 * sector that is due by the scan line the clock has reached. It is
 * called at each vertical blank (cd.c, through port_PlayDisc) and from
 * StGetNext, which is where the player waits: there each try moves the
 * clock on a scan line, so that time passes in that wait as it does on
 * the console, and the sound and the pads go on with it.
 *
 * A sector of the stream is 2048 bytes: a 32-byte header (its frame, how
 * many sectors that frame has and which this is, the frame's size and
 * the picture's) and 2016 bytes of the frame's bitstream. Sectors of
 * sound are interleaved with them and are not the stream's: they go to
 * the drive's own decoder (xa.c), as on the console.
 *
 * The ring itself is the player's buffer: StRingSize headers, then as
 * many 2016-byte bodies (movovl/sdk/libcd/libcd_st.h). */
#include "common.h"
#include "port_disc.h"
#include "port_spu.h"
#include "port_movie.h"
/* Sony's own header for the ring and the streaming library's variables.
 * The overlay's sdk.h, which it includes, is not wanted here and is shut
 * out by its guard: it declares printf otherwise than Sony's libc.h
 * does, and nothing of it is needed that this header has not. */
#define SDK_H
#include "movovl/sdk/libcd/libcd_st.h"
#include "bb2.h"

/* Where the sector being taken in goes: the library's own variable,
 * which lives in the module this file stands in for. */
u32 *StRingBase;

/* What a sector of a stream says it is, and where its channel is in the
 * second halfword. */
#define STREAM_SECTOR_ID 0x160
#define STREAM_CHANNEL_SHIFT 10
#define STREAM_CHANNEL_MASK 0x1F
/* A sector's header and body. */
#define STREAM_HEADER_WORDS 8
#define STREAM_BODY_WORDS 0x1F8
#define STREAM_SLOT_BODIES 0x3F /* bodies, in headers: 2016 / 32 */

/* How far apart two sectors are in scan lines at double speed: the
 * drive gives 150 a second, and a second is 44,100 samples of the port's
 * clock, which is 20,979/200 scan lines a sector. The two numbers are
 * exact to a part in a hundred million of what the clock says
 * (sound.c). */
#define SECTORS_PER_LINE_TOP 200
#define SECTORS_PER_LINE_BOTTOM 20979
/* The count is brought back to its start every this many times the
 * second number, so that the product above stays inside a word. */
#define REBASE_STEPS 100

/* How many scan lines StGetNext will wait altogether before it gives up
 * and lets the player's own count run out: two seconds. */
#define WAIT_LINES_MAX (PORT_LINES_PER_BLANK * 120)

/* What a run says of a movie at its end. */
typedef struct {
    u32 sectors; /* sectors of the stream that passed under the head */
    u32 picture; /* of those, sectors of picture taken into the ring */
    u32 sound;   /* of those, sectors of sound the drive played */
    u32 dropped; /* picture the ring had no room for */
    u32 frames;  /* frames the player took out of the ring */
    u32 first_blank;
    u32 last_blank;
    u16 width;
    u16 height;
    u32 first_frame;
    u32 last_frame;
} PortMovieCounts;

static PortMovieCounts g_port_movie;

typedef struct {
    s32 reading;      /* a stream is being read */
    s32 armed;        /* the library has hung its callback on the drive */
    s32 double_speed; /* 150 sectors a second, not 75 */
    s32 base_sector;  /* where the drive was at base_line */
    u32 base_line;
    s32 next_sector; /* the next sector to pass under the head */
    u32 waited;      /* scan lines StGetNext has waited for this frame */
} PortStream;

static PortStream g_port_stream;

s32 port_StreamArmed(void) { return g_port_stream.armed; }

void port_StartDiscStream(s32 sector, s32 double_speed) {
    g_port_stream.reading = 1;
    g_port_stream.double_speed = double_speed;
    g_port_stream.base_sector = sector;
    g_port_stream.base_line = port_GetLineCount();
    g_port_stream.next_sector = sector;
    g_port_stream.waited = 0;
}

void port_StopDiscStream(void) { g_port_stream.reading = 0; }

/* ---- a sector into the ring ---- */

/* Sony's handler, without the drive and the DMA: the sector at `data`,
 * 2048 bytes, into the slot the ring is filling. A sector that is not of
 * the stream, or that comes when the ring is full or out of its frame's
 * order, is dropped, and a frame that was being filled is given back,
 * exactly as the library does it. */
static void port_StreamSector(const u8 *data, s32 sector) {
    volatile u16 *head;
    const u32 *words = (const u32 *)data;
    u32 i;

    if (StFinalSector == 1) {
        return;
    }
    head = (u16 *)&StRingAddr[StRingIdx1];
    if (head[0] != ST_SLOT_FREE) {
        g_port_movie.dropped++; /* nothing has taken the frames there */
        return;
    }
    for (i = 0; i < STREAM_HEADER_WORDS; i++) {
        ((u32 *)head)[i] = words[i];
    }
    /* the library reads the sector's own place off the drive only when
     * it is given whole sectors; here it is always known */
    port_DiscSectorToPosition((u32)sector, &((StSECTOR *)head)->loc.minute);
    ((StSECTOR *)head)->loc.track = 0;
    if (StSTART_FLAG == 1 && StStartFrame != 0) {
        if (StStartFrame != head[4]) {
            head[0] = ST_SLOT_FREE;
            return;
        }
        StSTART_FLAG = 0;
    }
    if (head[0] != STREAM_SECTOR_ID || (s32)((head[1] >> STREAM_CHANNEL_SHIFT) &
                                             STREAM_CHANNEL_MASK) != CChannel) {
        head[0] = ST_SLOT_FREE;
        return;
    }
    g_port_movie.picture++;
    g_port_movie.width = ((const StSECTOR *)data)->width;
    g_port_movie.height = ((const StSECTOR *)data)->height;
    g_port_movie.last_frame = ((const StSECTOR *)data)->frameCount;
    if (g_port_movie.first_frame == 0) {
        g_port_movie.first_frame = g_port_movie.last_frame;
    }
    if (Stsector_offset != (s16)head[2] ||
        (Stframe_no != 0 && Stframe_no != head[4])) {
        /* out of order: give the part-filled frame back */
        Stframe_no = 0;
        Stsector_offset = 0;
        init_ring_status(StRingIdx2, (u32)(StRingIdx1 - StRingIdx2));
        StRingIdx1 = StRingIdx2;
        head[0] = ST_SLOT_FREE;
        return;
    }
    if (head[2] == 0) {
        Stsector_offset = 0;
        Stframe_no = head[4];
        if (StEndFrame != 0 && (u32)Stframe_no >= StEndFrame) {
            Stframe_no = 0;
            Stsector_offset = 0;
            init_ring_status(StRingIdx2, (u32)(StRingIdx1 - StRingIdx2));
            StRingIdx1 = StRingIdx2;
            head[0] = ST_SLOT_FREE;
            StSTART_FLAG = 1;
            if (StFunc2 != NULL) {
                StFunc2();
            }
            return;
        }
        if ((u32)(StRingSize - (u32)StRingIdx1 - 1) < head[3]) {
            /* the frame will not fit before the ring's end */
            if (StEndFrame == 0) {
                head[0] = ST_SLOT_WRAP;
                StSTART_FLAG = 1;
                if (StFunc2 != NULL) {
                    StFunc2();
                }
                return;
            }
            if ((s16)StRingAddr->id != ST_SLOT_FREE) {
                head[0] = ST_SLOT_FREE;
                return;
            }
            head[0] = ST_SLOT_WRAP;
            StRingIdx1 = 0;
            for (i = 0; i < STREAM_HEADER_WORDS; i++) {
                ((u32 *)StRingAddr)[i] = ((u32 *)head)[i];
            }
            head = (u16 *)StRingAddr;
        }
        StRingIdx2 = StRingIdx1;
    }
    Stsector_offset++;
    StRingBase =
        (u32 *)(&StRingAddr[StRingSize] + StRingIdx1 * STREAM_SLOT_BODIES);
    for (i = 0; i < STREAM_BODY_WORDS; i++) {
        StRingBase[i] = words[STREAM_HEADER_WORDS + i];
    }
    if (head[3] - 1 == head[2]) {
        Stsector_offset = 0;
        Stframe_no = 0;
        CChannel = StCHANNEL;
        head[0] = ST_SLOT_LOADING;
        StRingIdx1++;
        /* the console runs this when the last sector's transfer ends */
        data_ready_callback();
        return;
    }
    head[0] = ST_SLOT_LOADING;
    StRingIdx1++;
}

/* ---- the drive turning ---- */

/* Where the drive is now, in sectors from where the stream began. */
static s32 port_StreamSectorsDue(void) {
    u32 bottom =
        (u32)SECTORS_PER_LINE_BOTTOM * (g_port_stream.double_speed ? 1U : 2U);
    u32 lines = port_GetLineCount() - g_port_stream.base_line;

    while (lines >= bottom * REBASE_STEPS) {
        lines -= bottom * REBASE_STEPS;
        g_port_stream.base_line += bottom * REBASE_STEPS;
        g_port_stream.base_sector += REBASE_STEPS * SECTORS_PER_LINE_TOP;
    }
    return g_port_stream.base_sector +
           (s32)(lines * SECTORS_PER_LINE_TOP / bottom);
}

/* Every sector of the stream that is due by now: sound to the drive's
 * own decoder, picture to the ring. 0 if no stream is being read
 * (port_movie.h), and then the drive's sound goes the ordinary way. */
s32 port_PumpStream(void) {
    static u8 raw[PORT_DISC_SECTOR_BYTES];
    s32 due;

    if (!g_port_stream.reading) {
        return 0;
    }
    due = port_StreamSectorsDue();
    while (g_port_stream.next_sector < due) {
        s32 sector = g_port_stream.next_sector++;

        g_port_movie.sectors++;
        if (g_port_movie.first_blank == 0) {
            g_port_movie.first_blank = port_GetBlankCount();
        }
        g_port_movie.last_blank = port_GetBlankCount();
        if (!port_DriveReadSector(sector, raw)) {
            continue; /* the drive passes over what it cannot make out */
        }
        if (port_FeedXaSector(raw) != 0) {
            g_port_movie.sound++;
            continue; /* a sector of sound: the drive plays it itself */
        }
        port_StreamSector(&raw[PORT_DISC_DATA_OFFSET], sector);
    }
    return 1;
}

/* What a run says of the movie it played (port_movie.h). */
void port_PrintMovieSummary(void) {
    if (g_port_movie.sectors == 0) {
        return;
    }
    port_Print("movie sectors %u picture %u sound %u dropped %u\n",
               g_port_movie.sectors, g_port_movie.picture, g_port_movie.sound,
               g_port_movie.dropped);
    port_Print(
        "movie frames %u first %u last %u size %ux%u\n", g_port_movie.frames,
        g_port_movie.first_frame, g_port_movie.last_frame, g_port_movie.width,
        g_port_movie.height);
    port_Print("movie blanks %u to %u\n", g_port_movie.first_blank,
               g_port_movie.last_blank);
    port_PrintMdecSummary();
}

/* ---- what the library calls ---- */

/* The console's handler for a sector that has arrived. The player calls
 * it when the flag says one came while the decoder's callback was
 * running, which cannot happen here; it is the drive turning either
 * way. */
void StCdInterrupt(void) { port_PumpStream(); }

/* Sony's takes the library's callbacks off the drive and clears its
 * registers. Here there are no registers, and nothing is left hanging on
 * the drive. */
void StUnSetRing(void) {
    CdDataCallback(NULL);
    CdReadyCallback(NULL);
}

/* The drive's data callback: the streaming library hangs
 * data_ready_callback here when it starts a stream (CdRead2), and that
 * is how the port knows that the read which follows is a stream and not
 * a file's sectors. */
static void (*g_port_stream_data_callback)();

void (*CdDataCallback(void (*func)()))() {
    void (*before)() = g_port_stream_data_callback;

    g_port_stream_data_callback = func;
    g_port_stream.armed = func != NULL;
    if (func == NULL) {
        port_StopDiscStream();
    }
    return before;
}

/* The next whole frame of the ring, or 1 if there is none yet. Sony's
 * says only whether one is there; the player asks over and over until it
 * is, so here each try that finds none lets a scan line pass, which is
 * what turns the drive and lets the rest of the machine go on. */
u32 StGetNext(u32 **addr, u32 **header) {
    if (port_StGetNextRing(addr, header) == 0) {
        g_port_stream.waited = 0;
        g_port_movie.frames++;
        return 0;
    }
    if (!g_port_stream.reading || g_port_stream.waited >= WAIT_LINES_MAX) {
        return 1; /* nothing is coming: let the player's own count end it */
    }
    g_port_stream.waited++;
    port_WaitOneLine();
    port_PumpStream();
    return 1;
}
