/* The replay theatre's camera path and seek bar, as arithmetic
 * (theatre_path.h; docs/PORT.md, "The replay theatre and photo mode").
 * The window's (sdl_theatre.c) keys, curves and bar are here, held by
 * tests/port/window.c; nothing here asks anything of SDL or of a C
 * library. */
#include "theatre_path.h"

/* the camera's numbers a curve is drawn through, as theatre_path.h has
 * them: the focus's three, the pitch, the yaw and the roll the short way
 * round, the inverse of the distance and the screen's distance */
enum {
    CHANNEL_FOCUS = 0,
    CHANNEL_PITCH = 3,
    CHANNEL_YAW = 4,
    CHANNEL_ROLL = 5,
    CHANNEL_NEARNESS = 6,
    CHANNEL_PLANE = 7,
    CHANNELS = 8
};

#define HALF_TURN (PORT_PATH_TURN / 2)
/* the inverse of a distance, in a unit that keeps it far from nothing */
#define NEARNESS_ONE 1000000.0

s32 port_PathSet(PortPath *path, u32 frame, const PortTheatreCamera *camera) {
    s32 at = 0;
    s32 i;

    while (at < path->count && path->keys[at].frame < frame) {
        at++;
    }
    if (at < path->count && path->keys[at].frame == frame) {
        path->keys[at].camera = *camera;
        return at;
    }
    if (path->count >= PORT_PATH_KEYS) {
        return -1;
    }
    for (i = path->count; i > at; i--) {
        path->keys[i] = path->keys[i - 1];
    }
    path->keys[at].frame = frame;
    path->keys[at].camera = *camera;
    path->count++;
    return at;
}

static u32 port_PathGap(u32 a, u32 b) { return a > b ? a - b : b - a; }

s32 port_PathClear(PortPath *path, u32 frame, u32 *cleared) {
    s32 nearest = 0;
    s32 i;

    if (path->count == 0) {
        return FALSE;
    }
    for (i = 1; i < path->count; i++) {
        if (port_PathGap(path->keys[i].frame, frame) <
            port_PathGap(path->keys[nearest].frame, frame)) {
            nearest = i;
        }
    }
    *cleared = path->keys[nearest].frame;
    for (i = nearest; i + 1 < path->count; i++) {
        path->keys[i] = path->keys[i + 1];
    }
    path->count--;
    return TRUE;
}

/* An angle's step from `from` to `to`, the short way round. */
static s32 port_PathTurn(s32 from, s32 to) {
    s32 step = (to - from) % PORT_PATH_TURN;

    if (step > HALF_TURN) {
        step -= PORT_PATH_TURN;
    } else if (step <= -HALF_TURN) {
        step += PORT_PATH_TURN;
    }
    return step;
}

/* Key `index`'s numbers to draw curves through; its yaw and roll the
 * short way round from the key before's, `before` (NULL for the first). */
static void port_PathChannels(
    const PortTheatreCamera *camera, const double *before, double *out) {
    s32 i;

    for (i = 0; i < 3; i++) {
        out[CHANNEL_FOCUS + i] = camera->focus[i];
    }
    out[CHANNEL_PITCH] = camera->angles[0];
    out[CHANNEL_YAW] = camera->angles[1];
    out[CHANNEL_ROLL] = camera->angles[2];
    if (before != NULL) {
        out[CHANNEL_YAW] =
            before[CHANNEL_YAW] +
            port_PathTurn((s32)before[CHANNEL_YAW], camera->angles[1]);
        out[CHANNEL_ROLL] =
            before[CHANNEL_ROLL] +
            port_PathTurn((s32)before[CHANNEL_ROLL], camera->angles[2]);
    }
    out[CHANNEL_NEARNESS] =
        NEARNESS_ONE / (camera->distance > 0 ? camera->distance : 1);
    out[CHANNEL_PLANE] = camera->plane;
}

static s32 port_PathRound(double value) {
    return value >= 0 ? (s32)(value + 0.5) : -(s32)(-value + 0.5);
}

s32 port_PathAt(const PortPath *path, u32 frame, PortTheatreCamera *camera) {
    double values[PORT_PATH_KEYS][CHANNELS];
    double least[CHANNELS];
    double most[CHANNELS];
    double out[CHANNELS];
    double gap;
    double s;
    double h00;
    double h10;
    double h01;
    double h11;
    s32 last = path->count - 1;
    s32 at = 0;
    s32 i;
    s32 c;

    if (path->count == 0) {
        return FALSE;
    }
    if (frame <= path->keys[0].frame || path->count == 1) {
        *camera = path->keys[0].camera;
        return TRUE;
    }
    if (frame >= path->keys[last].frame) {
        *camera = path->keys[last].camera;
        return TRUE;
    }
    for (i = 0; i <= last; i++) {
        port_PathChannels(
            &path->keys[i].camera, i > 0 ? values[i - 1] : NULL, values[i]);
        for (c = 0; c < CHANNELS; c++) {
            if (i == 0 || values[i][c] < least[c]) {
                least[c] = values[i][c];
            }
            if (i == 0 || values[i][c] > most[c]) {
                most[c] = values[i][c];
            }
        }
    }
    while (path->keys[at + 1].frame <= frame) {
        at++;
    }
    gap = (double)(path->keys[at + 1].frame - path->keys[at].frame);
    s = (frame - path->keys[at].frame) / gap;
    h00 = (1 + 2 * s) * (1 - s) * (1 - s);
    h10 = s * (1 - s) * (1 - s);
    h01 = s * s * (3 - 2 * s);
    h11 = s * s * (s - 1);
    for (c = 0; c < CHANNELS; c++) {
        double slope[2];
        s32 k;

        /* Catmull-Rom's slope at each end of the span, in a frame's
         * change; none at the path's first key and its last */
        for (k = 0; k < 2; k++) {
            s32 key = at + k;

            slope[k] = 0;
            if (key > 0 && key < last) {
                slope[k] = (values[key + 1][c] - values[key - 1][c]) /
                           (double)(path->keys[key + 1].frame -
                                    path->keys[key - 1].frame);
            }
        }
        out[c] = h00 * values[at][c] + h10 * gap * slope[0] +
                 h01 * values[at + 1][c] + h11 * gap * slope[1];
        if (c == CHANNEL_PITCH || c == CHANNEL_NEARNESS || c == CHANNEL_PLANE) {
            out[c] = out[c] < least[c]  ? least[c]
                     : out[c] > most[c] ? most[c]
                                        : out[c];
        }
    }
    for (i = 0; i < 3; i++) {
        camera->focus[i] = port_PathRound(out[CHANNEL_FOCUS + i]);
    }
    camera->angles[0] = (s16)port_PathRound(out[CHANNEL_PITCH]);
    camera->angles[1] =
        (s16)(port_PathRound(out[CHANNEL_YAW]) & (PORT_PATH_TURN - 1));
    camera->angles[2] =
        (s16)(port_PathRound(out[CHANNEL_ROLL]) & (PORT_PATH_TURN - 1));
    camera->distance = port_PathRound(NEARNESS_ONE / out[CHANNEL_NEARNESS]);
    camera->plane = port_PathRound(out[CHANNEL_PLANE]);
    return TRUE;
}

/* ---- the seek bar ---- */

u32 port_BarFrameAt(s32 x, s32 left, s32 width, u32 frames) {
    s32 along = x - left;

    if (frames <= 1 || width <= 0 || along <= 0) {
        return 0;
    }
    if (along >= width) {
        return frames - 1;
    }
    return (u32)port_PathRound((double)along * (frames - 1) / width);
}

s32 port_BarXOf(u32 frame, s32 left, s32 width, u32 frames) {
    if (frames <= 1) {
        return left;
    }
    if (frame >= frames) {
        frame = frames - 1;
    }
    return left + port_PathRound((double)frame * width / (frames - 1));
}
