/* LIBCD EVENT: CdInit, and the callbacks it sets, which turn the CD-ROM's
 * interrupts into kernel events. */
#include "common.h"
#include "libcd_internal.h"
#include <psxsdk/libapi.h>

#include <psxsdk/libc.h>

void def_cbsync(u8 intr, u8 *result);
void def_cbready(u8 intr, u8 *result);
void def_cbread(u8 intr, u8 *result);

s32 CdInit(void) {
    s32 retries = 4;
    while (CdReset(1) != 1) {
        if (--retries == -1) {
            printf("CdInit: Init failed\n");
            return 0;
        }
    }
    CdSyncCallback(def_cbsync);
    CdReadyCallback(def_cbready);
    CdReadCallback(def_cbread);
    CdReadMode(0);
    return 1;
}

void def_cbsync(u8 intr, u8 *result) { DeliverEvent(HwCdRom, EvSpCOMP); }

void def_cbready(u8 intr, u8 *result) { DeliverEvent(HwCdRom, EvSpDR); }

void def_cbread(u8 intr, u8 *result) { DeliverEvent(HwCdRom, EvSpDR); }
