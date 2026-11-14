/* LIBCD CDROM: StSetRing, which gives the streaming library its buffer. */
#include "libcd_st.h"
#include "libcd_reg.h"

StSECTOR *StRingAddr;
volatile u32 StRingSize;

void StSetRing(u32 *ring_addr, s32 ring_size) {
    StRingAddr = (StSECTOR *)ring_addr;
    StRingSize = ring_size;
    StClearRing();
}
