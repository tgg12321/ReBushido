/* LIBCD's streaming half (the St functions): what its modules share. The
 * variables have Sony's names, except st_read_slot and st_frame_callback,
 * whose names are not known (StRingIdx3 and StFunc1 would continue the
 * pattern and fall where the linker put them, but nothing at hand has
 * them). */
#ifndef LIBCD_ST_H
#define LIBCD_ST_H

#include "common.h"
#include <psxsdk/libapi.h>
#include <psxsdk/libcd.h>
#include <psxsdk/libetc.h>
#include "sdk.h"

/* A sector's header in the ring as the library sees it: StHEADER with the
 * sector's place on the disc where the last word is. `id` doubles as the
 * slot's state. */
typedef struct {
    u16 id;
    u16 type;
    u16 secCount;
    u16 nSectors;
    u32 frameCount;
    u32 frameSize;
    u16 width;
    u16 height;
    u32 dummy1;
    u32 dummy2;
    CdlLOC loc;
} StSECTOR;

#define ST_SLOT_FREE 0
#define ST_SLOT_WRAP 1  /* the ring goes on from its first slot */
#define ST_SLOT_READY 2 /* first sector of a complete frame */
#define ST_SLOT_LOADING 3
#define ST_SLOT_TAKEN 4 /* handed out by StGetNext */

/* The ring: `StRingSize` headers, then as many 2016-byte sector bodies. */
extern StSECTOR *StRingAddr;
extern volatile u32 StRingSize;
/* Slot StGetNext hands out next, slot the frame being loaded began at, and
 * slot the next sector goes to. */
extern s32 st_read_slot;
extern s32 StRingIdx2;
extern s32 StRingIdx1;
/* Set while a whole frame waits for data_ready_callback. */
extern s32 StFinalSector;
/* Sector within the frame and the frame's number, as expected next. */
extern s16 Stsector_offset;
extern s32 Stframe_no;
/* StSetMask's three arguments. */
extern u32 StSTART_FLAG;
extern u32 StStartFrame;
extern u32 StEndFrame;
/* StSetStream: 24-bit mode, the two callbacks, the channel playing and the
 * one to switch to, and the address stream data is copied from when it does
 * not come from the disc. */
extern s32 StRgb24;
extern void (*st_frame_callback)(void);
extern void (*StFunc2)(void);
extern s32 CChannel;
extern s32 StCHANNEL;
extern s32 StEmu_Addr;
/* Set by CdRead2 when sectors arrive without their 12-byte header. */
extern s32 StMode;

void StSetMask(u32 mask, u32 start, u32 end);
void data_ready_callback(void);
s32 init_ring_status(s32 first, u32 count);

#endif
