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

void port_RemasterNewFrame(void) {}

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
