#ifndef SDK_H
#define SDK_H

/* Sony library declarations the overlay needs and the main game's headers
 * (include/psxsdk/) do not have: the MDEC decoder (libpress), CD streaming
 * (libcd) and the old pad interface (libetc). */

#include "common.h"
#include <psxsdk/libcd.h>

/* A file on the disc: where it starts, its size and its name. */
typedef struct {
    CdlLOC pos;
    u32 size;
    char name[16];
} CdlFILE;

/* The header in front of every sector of a movie stream. */
typedef struct {
    u16 id;
    u16 type;
    u16 secCount;
    u16 nSectors;
    u32 frameCount;
    u32 frameSize;
    u16 width;
    u16 height;
    u32 headm;
    u32 headv;
} StHEADER;

extern void DecDCTReset(s32 mode);
extern s32 DecDCTvlc(u32 *bs, u32 *buf);
extern void DecDCTin(u32 *buf, s32 mode);
extern void DecDCTout(u32 *buf, s32 size);
extern void DecDCToutCallback(void (*func)(void));

extern void StSetRing(u32 *ring_addr, s32 ring_size);
extern void StClearRing(void);
extern void StUnSetRing(void);
extern void StSetStream(s32 mode, u32 start_frame, u32 end_frame,
                        void (*func1)(void), void (*func2)(void));
extern u32 StGetNext(u32 **addr, u32 **header);
extern u32 StFreeRing(u32 *base);
extern void StCdInterrupt(void);
extern s32 CdRead2(s32 mode);

/* Set by the CD interrupt when a stream sector arrived while the decoder's
 * callback was running. */
extern s32 StCdIntrFlag;

extern void PadInit(s32 mode);
extern u32 PadRead(s32 id);
extern void PadStop(void);

extern s32 printf(const char *fmt, ...);

/* Sony's start-up code: runs the program's constructors, of which there are
 * none. */
extern void __main(void);

#endif /* SDK_H */
