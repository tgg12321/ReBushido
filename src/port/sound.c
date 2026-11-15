/* Sony's libsnd and libspu in the native port: nothing sounds until stage
 * 6 of docs/PORT.md. Every function the game calls is a stub that is
 * counted. Each answers as the library does when all goes well, since
 * the game stops if a sound bank will not open: a bank opens under the
 * number asked for, a transfer is done at once, no voice is sounding.
 * The one thing remembered is where in sound memory each bank was said
 * to be, which the game asks back.
 *
 * Two of these would have changed the game's memory and do not: Sony's
 * SsVabOpenHeadSticky rewrites parts of a bank's header as it opens it,
 * and SpuRead copies sound memory into the game's. */
#include "common.h"
#include "bb2.h"

#define VABS_MAX 16
#define VOICES 24

/* Where in sound memory each open bank's samples are. */
static u32 g_port_vab_addrs[VABS_MAX];

static s16 port_OpenVab(s16 vabid, u32 sbaddr) {
    if (vabid < 0 || vabid >= VABS_MAX) {
        return -1;
    }
    g_port_vab_addrs[vabid] = sbaddr;
    return vabid;
}

/* ---- libsnd ---- */

void SsInit(void) { PORT_STUB("SsInit (sound)"); }

void SsStart(void) { PORT_STUB("SsStart (sound)"); }

void SsEnd(void) { PORT_STUB("SsEnd (sound)"); }

void SsQuit(void) { PORT_STUB("SsQuit (sound)"); }

void SsSetTickMode(s32 tick_mode) {
    PORT_STUB("SsSetTickMode (sound)");
    (void)tick_mode;
}

void SsSetStereo(void) { PORT_STUB("SsSetStereo (sound)"); }

void SsSetMVol(s16 voll, s16 volr) {
    PORT_STUB("SsSetMVol (sound)");
    (void)voll;
    (void)volr;
}

void SsSetSerialAttr(s32 s_num, s32 attr, s32 mode) {
    PORT_STUB("SsSetSerialAttr (sound)");
    (void)s_num;
    (void)attr;
    (void)mode;
}

void SsSetSerialVol(s16 s_num, s16 voll, s16 volr) {
    PORT_STUB("SsSetSerialVol (sound)");
    (void)s_num;
    (void)voll;
    (void)volr;
}

void SsSetAutoKeyOffMode(s16 mode) {
    PORT_STUB("SsSetAutoKeyOffMode (sound)");
    (void)mode;
}

s32 SsSetReservedVoice(s32 voices) {
    PORT_STUB("SsSetReservedVoice (sound)");
    return voices;
}

void SsUtAllKeyOff(s16 mode) {
    PORT_STUB("SsUtAllKeyOff (sound)");
    (void)mode;
}

s16 SsUtKeyOnV(s16 voice, s16 vabId, s16 prog, s16 tone, s16 note, s16 fine,
               s16 voll, s16 volr) {
    PORT_STUB("SsUtKeyOnV (sound)");
    (void)vabId;
    (void)prog;
    (void)tone;
    (void)note;
    (void)fine;
    (void)voll;
    (void)volr;
    return voice;
}

s32 SsUtSetVVol(s16 voice, s16 voll, s16 volr) {
    PORT_STUB("SsUtSetVVol (sound)");
    (void)voice;
    (void)voll;
    (void)volr;
    return 0;
}

void SsUtReverbOn(void) { PORT_STUB("SsUtReverbOn (sound)"); }

void SsUtReverbOff(void) { PORT_STUB("SsUtReverbOff (sound)"); }

s16 SsUtSetReverbType(s16 type) {
    PORT_STUB("SsUtSetReverbType (sound)");
    return type;
}

void SsUtSetReverbDepth(s16 ldepth, s16 rdepth) {
    PORT_STUB("SsUtSetReverbDepth (sound)");
    (void)ldepth;
    (void)rdepth;
}

s16 SsVabOpenHeadSticky(u8 *addr, s16 vabid, u32 sbaddr) {
    PORT_STUB("SsVabOpenHeadSticky (sound)");
    (void)addr;
    return port_OpenVab(vabid, sbaddr);
}

s16 SsVabFakeHead(u8 *addr, s16 vabid, u32 sbaddr) {
    PORT_STUB("SsVabFakeHead (sound)");
    (void)addr;
    return port_OpenVab(vabid, sbaddr);
}

s16 SsVabTransBody(u8 *addr, s16 vabid) {
    PORT_STUB("SsVabTransBody (sound)");
    (void)addr;
    return vabid;
}

s16 SsVabFakeBody(s16 vabid) {
    PORT_STUB("SsVabFakeBody (sound)");
    return vabid;
}

s16 SsVabTransCompleted(s16 immediateFlag) {
    PORT_STUB("SsVabTransCompleted (sound)");
    (void)immediateFlag;
    return 1;
}

void SsVabClose(s16 vabid) {
    PORT_STUB("SsVabClose (sound)");
    (void)vabid;
}

u32 SsUtGetVBaddrInSB(s16 vabid) {
    PORT_STUB("SsUtGetVBaddrInSB (sound)");
    if (vabid < 0 || vabid >= VABS_MAX) {
        return 0;
    }
    return g_port_vab_addrs[vabid];
}

/* ---- libspu ---- */

s32 SpuSetTransferStartAddr(s32 addr) {
    PORT_STUB("SpuSetTransferStartAddr (sound)");
    return addr;
}

s32 SpuWrite(s32 addr, s32 size) {
    PORT_STUB("SpuWrite (sound)");
    (void)addr;
    return size;
}

s32 SpuRead(s32 addr, s32 size) {
    PORT_STUB("SpuRead (sound: the game's memory is left as it was)");
    (void)addr;
    return size;
}

s32 SpuIsTransferCompleted(s32 wait) {
    PORT_STUB("SpuIsTransferCompleted (sound)");
    (void)wait;
    return 1;
}

s32 SpuClearReverbWorkArea(u32 mode) {
    PORT_STUB("SpuClearReverbWorkArea (sound)");
    (void)mode;
    return 0;
}

s32 SpuGetKeyStatus(u32 voice_bit) {
    PORT_STUB("SpuGetKeyStatus (sound)");
    (void)voice_bit;
    return SPU_OFF;
}

void SpuGetAllKeysStatus(u8 *status) {
    s32 voice;

    PORT_STUB("SpuGetAllKeysStatus (sound)");
    for (voice = 0; voice < VOICES; voice++) {
        status[voice] = SPU_OFF;
    }
}
