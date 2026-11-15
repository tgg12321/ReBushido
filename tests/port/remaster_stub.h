/* The remastered renderer in a test that is not about it: the GTE, the
 * renderer and the GPU in C all hand it what they do (port_remaster.h),
 * and here that goes nowhere. tests/port/remaster.c is the test that
 * has the real one; a test that includes a file which calls any of
 * these includes this one too. */

#include "common.h"
#include "port_remaster.h"

void port_RemasterNotePoint(PortAddress at, const PortPrecisePoint *point) {
    (void)at;
    (void)point;
}

void port_RemasterNoteFace(PortAddress points, const s32 *index, s32 count) {
    (void)points;
    (void)index;
    (void)count;
}

void port_RemasterAttachFace(PortAddress prim) { (void)prim; }

void port_RemasterSetAnchor(s32 anchor) { (void)anchor; }

void port_RemasterNoteAnchored(PortAddress prim) { (void)prim; }

void port_RemasterNoteOtSlot(s32 index) { (void)index; }

void port_RemasterNoteCells(s32 tiles, s32 nodes, s32 queue_size) {
    (void)tiles;
    (void)nodes;
    (void)queue_size;
}

void port_RemasterNoteRoom(s32 prim_free, s32 prim_bytes) {
    (void)prim_free;
    (void)prim_bytes;
}

s32 port_RemasterDistance(void) { return PORT_DISTANCE_ORIGINAL; }

s32 port_RemasterFogFactor(s32 distance) {
    (void)distance;
    return PORT_FOG_FACTOR_ONE;
}

s32 port_RemasterNeedsFarShift(s32 distance) {
    (void)distance;
    return FALSE;
}

u32 *port_RemasterPrimBuffer(s32 which) {
    (void)which;
    return NULL;
}

u32 port_RemasterPrimBufferBytes(void) { return 0; }

void port_RemasterNewFrame(void) {}

s32 port_RemasterLeftMargin(void) { return 0; }

void port_RemasterSetPrim(PortAddress prim) { (void)prim; }

void port_RemasterCommand(const u32 *words, s32 count) {
    (void)words;
    (void)count;
}

void port_RemasterLoadedVram(s32 x, s32 y, s32 w, s32 h) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}
