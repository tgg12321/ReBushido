#ifndef THEATRE_PATH_H
#define THEATRE_PATH_H

/* The arithmetic of the replay theatre's camera path and seek bar
 * (theatre_path.c; docs/PORT.md, "The replay theatre and photo mode"):
 * no SDL, no C library, held by tests/port/window.c.
 *
 * A path is the free camera's keys, each the camera at a frame of the
 * replay; between them the camera is a curve through them, and before the
 * first and after the last it is that key's. */

#include "common.h"
#include "port_theatre.h"

#define PORT_PATH_KEYS 64
/* a turn of the game's angles */
#define PORT_PATH_TURN 4096

typedef struct {
    u32 frame;
    PortTheatreCamera camera;
} PortPathKey;

typedef struct {
    s32 count;
    PortPathKey keys[PORT_PATH_KEYS]; /* by their frames, each once */
} PortPath;

/* A key at `frame`, the one there already replaced: its place, or -1
 * where the path is full. */
extern s32 port_PathSet(
    PortPath *path, u32 frame, const PortTheatreCamera *camera);
/* The key nearest `frame` (the earlier of two as near) taken away, its
 * frame into `cleared`: FALSE where there is none. */
extern s32 port_PathClear(PortPath *path, u32 frame, u32 *cleared);
/* The camera at `frame`: FALSE where the path has no key.
 *
 * Each of the camera's numbers is a cubic Hermite curve through the keys
 * with Catmull-Rom's slopes for keys unevenly spaced (at a key, the
 * slope of the line through the keys either side), and none at the first
 * and the last, so that a path of two keys eases out of the one and into
 * the other. The focus is so as it is. The yaw and the roll are taken
 * the short way round from one key to the next. The distance is the
 * curve of its inverse, and the screen's distance (the field of view)
 * the curve of itself: what a thing's size on the screen goes as, so
 * that a zoom is even. The pitch, the distance and the field of view are
 * kept within the keys' own, never past them where the curve overshoots;
 * so too the pitch short of straight up and down, where a yaw has no
 * meaning: the camera is an orbit's, its horizon level. */
extern s32 port_PathAt(
    const PortPath *path, u32 frame, PortTheatreCamera *camera);

/* The seek bar: a track `width` pixels from `left` stands for the frames
 * 0 to `frames` - 1. The frame at pixel `x` (the nearest, the track's
 * ends past them), and the pixel of a frame. */
extern u32 port_BarFrameAt(s32 x, s32 left, s32 width, u32 frames);
extern s32 port_BarXOf(u32 frame, s32 left, s32 width, u32 frames);

#endif /* THEATRE_PATH_H */
