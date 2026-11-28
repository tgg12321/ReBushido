/* The replay theatre and photo mode in the window (port_theatre.h;
 * docs/PLAYING.md, "The replay theatre" and "Photo mode"; docs/PORT.md,
 * "The replay theatre and photo mode"): the free camera's keys, mouse and
 * sticks, the HUD hidden, the time of day, stills, clips, a seek, and
 * photo mode's wait in a fight. Only the build with a window has this
 * file.
 *
 * While a replay plays in the window every picture is a look (theatre.c):
 * the frame on show drawn again from the theatre's view, which is the
 * game's own until the player changes it. In a fight, F4 asks for photo
 * mode: the looks begin at once, and at the first boundary that has the
 * state before it the game waits there, stepping no frame, while the
 * player moves the camera, hides the HUD and turns the time of day, each
 * change a look again; F4 or Escape and the fight goes on from the very
 * state it was left in. Never online, nor over a link cable: there the
 * other machine would not wait. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "menu.h"
#include "port_net.h"
#include "port_match.h"
#include "port_replay.h"
#include "port_stage_choice.h"
#include "port_state.h"
#include "port_theatre.h"
#include "shared.h"
#include "sdl_window.h"
#include "theatre_path.h"

/* the inputs the game has taken (port_platform.h, which a file of the
 * host's side cannot include beside the C library) */
extern u32 g_port_input_count;
extern s32 port_LinkIsUp(void);

#define PATH_BYTES 1024
#define WORD_BYTES 512
#define VIEW_BYTES 160
/* the free camera: a turn is 4096; a second's moves, at most a tick of
 * TICK_MOST_MS counted */
#define TURN 4096
#define PI 3.14159265358979
#define ORBIT_A_SECOND 1024.0
#define MOVE_A_SECOND 0.9 /* of the distance looked from */
#define MOVE_LEAST 400.0  /* world units a second, however near */
#define ZOOM_A_SECOND 1.0 /* the distance's logarithm */
#define FAST 4.0          /* with Shift held */
#define PITCH_MOST 1000   /* a little short of straight down or up */
#define DISTANCE_LEAST 300
#define DISTANCE_MOST 60000
#define MOUSE_TURN 6.0   /* 4096ths a pixel */
#define MOUSE_PAN 0.0025 /* of the distance a pixel */
#define WHEEL_ZOOM 0.1   /* of the distance a notch */
#define STICK_DEAD 8000
#define STICK_FULL 32767.0
#define TICK_MOST_MS 50
#define FRAMES_PER_SECOND 30
#define SEEK_SECONDS 5
#define TENTHS 10
/* a blank, in milliseconds, of photo mode's wait */
#define BLANK_MS 16
/* a clip asked for with no marks: from the frame on show, so long */
#define CLIP_FRAMES_BUILT_IN 150
/* the field of view: the game's own 640 across (disp.h's DISP_WIDTH), the
 * screen's distance 320 / tan(half the angle), from 20 to 100 degrees
 * (the game's side view is 45, its first person 80); a second held moves
 * it 30 degrees, a notch of the wheel with Ctrl 2.5 */
#define SCREEN_HALF 320.0
#define FOV_LEAST 20.0
#define FOV_MOST 100.0
#define FOV_GAME 45.0
#define FOV_A_SECOND 30.0
#define FOV_A_NOTCH 2.5
/* the camera's path as --theatre-path words: a key's nine numbers */
#define KEY_WORD_BYTES 120
#define PATH_WORD_BYTES (PORT_PATH_KEYS * KEY_WORD_BYTES)
/* The seek bar, in the game's 640 by 240 (menu.h, PORT_LOOK_GAME_W): a
 * strip of the game's panel across the bottom, the time on show at the
 * left of its track and the replay's length at the right; the track's
 * line and the part played, the clip's marks and the camera's keys on
 * it, and the frame on show. Shown with the HUD, and for a while after
 * the mouse moves; the mouse's press on it seeks, a drag along it too. */
#define BAR_X PORT_LOOK_PANEL_X
#define BAR_W PORT_LOOK_PANEL_W
#define BAR_Y 205
#define BAR_H 26
#define TRACK_X (BAR_X + 60)
#define TRACK_W (BAR_W - 120)
#define TRACK_Y 217
#define TRACK_LINE 2
#define TRACK_TEXT_GAP 7
#define TEXT_BASE 221
#define HEAD_W 4
#define HEAD_UP 6
#define MARK_UP 5
#define KEY_W 5
#define KEY_DOWN 3
#define KEY_H 4
#define BAR_MOUSE_MS 2500

enum {
    HELD_FORWARD,
    HELD_BACK,
    HELD_LEFT,
    HELD_RIGHT,
    HELD_UP,
    HELD_DOWN,
    HELD_YAW_LEFT,
    HELD_YAW_RIGHT,
    HELD_PITCH_UP,
    HELD_PITCH_DOWN,
    HELD_NEARER,
    HELD_FARTHER,
    HELD_NARROWER,
    HELD_WIDER,
    HELD_COUNT
};

static const char * const g_times[PORT_TIME_COUNT + 1] = {
    "the fight's time", "the stage's own", "dawn", "day", "dusk", "night"};

static struct {
    s32 started;
    PortTheatreView view;
    s32 changed;     /* a look again, where the game waits */
    u32 looked_at;   /* the input count the last look was of, plus one */
    s32 photo;       /* photo mode: the fight waits */
    s32 photo_asked; /* ... from the next boundary that can look */
    s32 replay;      /* a replay plays in the theatre */
    s32 line_hidden;
    s32 held[HELD_COUNT];
    s32 shift;
    s32 mouse_left;
    s32 mouse_right;
    s32 stick[4]; /* the left stick's x and y, the right's */
    s32 triggers[2];
    /* a controller's L1 held: the triggers are the field of view, and
     * Cross, Square, Circle and Triangle the camera's keys */
    s32 l1;
    u32 ticked_at;
    u32 mark_in;
    u32 mark_out;
    s32 has_in;
    s32 has_out;
    char said[WORD_BYTES]; /* the last thing done, for the line */
    u32 said_at;
    /* a test's words (--theatre-keys) */
    const char *words;
    s32 wait;
    s32 export_sought; /* a clip's run went to its first frame */
    s32 plain;         /* a test's replay with no look (`plain`) */
    /* the camera's path: its keys, whether the free camera follows them,
     * and the frame it was last put on it at, plus one */
    PortPath path;
    s32 path_on;
    u32 path_frame;
    /* the seek bar: held by the mouse, the frame the drag last sought
     * (plus one), where the mouse is and when it last moved */
    s32 bar_held;
    u32 bar_sought;
    s32 mouse_x;
    s32 mouse_y;
    u32 mouse_at;
    PortLook bar;
} g_theatre;

/* ---- the view as words (--theatre-view; a clip's run) ---- */

/* free, the focus's three, the angles' three, the distance, the HUD hidden,
 * the time, and the screen's distance (the field of view; 0 the game's):
 * eleven numbers with commas, the last of which may be left out */
static void port_TheatreViewWords(
    const PortTheatreView *view, char *out, u32 room) {
    snprintf(
        out, room, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", view->free,
        view->camera.focus[0], view->camera.focus[1], view->camera.focus[2],
        view->camera.angles[0], view->camera.angles[1], view->camera.angles[2],
        view->camera.distance, view->hide_hud, view->time, view->camera.plane);
}

static s32 port_TheatreReadView(const char *words, PortTheatreView *view) {
    int n[11];
    s32 count =
        sscanf(words, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", &n[0], &n[1], &n[2],
               &n[3], &n[4], &n[5], &n[6], &n[7], &n[8], &n[9], &n[10]);

    if (count < 10) {
        return FALSE;
    }
    view->camera.plane = count == 11 && n[10] > 0 ? n[10] : 0;
    view->free = n[0] != 0;
    view->camera.focus[0] = n[1];
    view->camera.focus[1] = n[2];
    view->camera.focus[2] = n[3];
    view->camera.angles[0] = (s16)n[4];
    view->camera.angles[1] = (s16)n[5];
    view->camera.angles[2] = (s16)n[6];
    view->camera.distance = n[7];
    view->hide_hud = n[8] != 0;
    view->time = n[9] >= -1 && n[9] < PORT_TIME_COUNT ? n[9] : -1;
    return TRUE;
}

/* The camera's path (--theatre-path; a clip's run): each key's frame,
 * focus, angles, distance and screen's distance, nine numbers with
 * commas, the keys between semicolons. */
static void port_TheatrePathWords(const PortPath *path, char *out, u32 room) {
    u32 used = 0;
    s32 i;

    out[0] = 0;
    for (i = 0; i < path->count && used < room; i++) {
        const PortPathKey *key = &path->keys[i];
        s32 n = snprintf(
            out + used, room - used, "%s%u,%d,%d,%d,%d,%d,%d,%d,%d",
            i > 0 ? ";" : "", key->frame, key->camera.focus[0],
            key->camera.focus[1], key->camera.focus[2], key->camera.angles[0],
            key->camera.angles[1], key->camera.angles[2], key->camera.distance,
            key->camera.plane);

        if (n < 0) {
            break;
        }
        used += (u32)n;
    }
}

static s32 port_TheatreReadPath(const char *words, PortPath *path) {
    const char *at = words;

    path->count = 0;
    while (*at != 0) {
        PortTheatreCamera camera;
        unsigned frame;
        int n[8];

        if (sscanf(at, "%u,%d,%d,%d,%d,%d,%d,%d,%d", &frame, &n[0], &n[1],
                   &n[2], &n[3], &n[4], &n[5], &n[6], &n[7]) != 9) {
            return FALSE;
        }
        camera.focus[0] = n[0];
        camera.focus[1] = n[1];
        camera.focus[2] = n[2];
        camera.angles[0] = (s16)n[3];
        camera.angles[1] = (s16)n[4];
        camera.angles[2] = (s16)n[5];
        camera.distance = n[6];
        camera.plane = n[7];
        if (port_PathSet(path, frame, &camera) < 0) {
            return FALSE;
        }
        at = strchr(at, ';');
        if (at == NULL) {
            break;
        }
        at++;
    }
    return TRUE;
}

static void port_TheatreSays(const char *format, const char *what) {
    snprintf(g_theatre.said, sizeof(g_theatre.said), format, what);
    g_theatre.said_at = SDL_GetTicks();
    port_Say("theatre: %s", g_theatre.said);
}

void port_StartTheatre(void) {
    g_theatre.started = TRUE;
    g_theatre.view.time = -1;
    if (g_port_window_options.theatre_view != NULL &&
        !port_TheatreReadView(
            g_port_window_options.theatre_view, &g_theatre.view)) {
        port_Say("--theatre-view: not ten numbers: %s",
                 g_port_window_options.theatre_view);
        port_Quit(PORT_EXIT_FAILED);
    }
    if (g_port_window_options.theatre_path != NULL) {
        if (!port_TheatreReadPath(
                g_port_window_options.theatre_path, &g_theatre.path)) {
            port_Say("--theatre-path: not keys of nine numbers: %s",
                     g_port_window_options.theatre_path);
            port_Quit(PORT_EXIT_FAILED);
        }
        g_theatre.path_on = g_theatre.path.count > 0;
    }
    port_TheatreSetView(&g_theatre.view);
    g_theatre.words = g_port_window_options.theatre_keys;
}

/* ---- where it may be ---- */

/* Why photo mode cannot be had now, or NULL. */
static const char *port_PhotoRefused(void) {
    if (port_IsNetplayOn() || port_SessionIsOn() || port_LinkIsUp()) {
        return "not online: the other machine would not wait";
    }
    if (port_ReplayIsPlaying()) {
        return "a replay plays: the theatre's keys are the same";
    }
    if (!port_IsFightScreen()) {
        return "in a fight only";
    }
    if (port_ExportIsOn()) {
        return "not in a film's run";
    }
    return NULL;
}

static s32 port_TheatreActive(void) {
    return g_theatre.replay || g_theatre.photo || g_theatre.photo_asked;
}

s32 port_TheatreHidesOverlays(void) {
    return port_TheatreActive() && g_theatre.view.hide_hud;
}

/* ---- the free camera ---- */

static void port_TheatreApply(void) {
    port_TheatreSetView(&g_theatre.view);
    g_theatre.changed = TRUE;
}

static s32 port_TheatrePlane(double fov);
static s32 port_TheatreFollows(void);

/* The free camera on, from the game's own where it was off. */
static void port_TheatreFree(void) {
    PortTheatreCamera game;

    if (g_theatre.view.free) {
        return;
    }
    if (port_TheatreGameCamera(&game)) {
        g_theatre.view.camera = game;
    }
    if (g_theatre.view.camera.distance < DISTANCE_LEAST) {
        g_theatre.view.camera.distance = DISTANCE_LEAST;
    }
    if (g_theatre.view.camera.plane <= 0) {
        /* the game's field of view, where it was never seen */
        g_theatre.view.camera.plane = port_TheatrePlane(FOV_GAME);
    }
    g_theatre.view.free = TRUE;
    port_TheatreApply();
}

static double port_Radians(s32 angle) { return angle * 2.0 * PI / TURN; }

/* The field of view of a screen's distance, in degrees (the game's side
 * view's where it is not known), and the screen's distance of one. */
static double port_TheatreFov(s32 plane) {
    return plane > 0 ? 2.0 * SDL_atan(SCREEN_HALF / plane) * 180.0 / PI
                     : FOV_GAME;
}

static s32 port_TheatrePlane(double fov) {
    fov = fov < FOV_LEAST ? FOV_LEAST : fov > FOV_MOST ? FOV_MOST : fov;
    return (s32)(SCREEN_HALF / SDL_tan(fov * PI / 360.0) + 0.5);
}

/* The free camera's field of view widened by `degrees` (narrowed where
 * fewer than none), within FOV_LEAST and FOV_MOST. */
static void port_TheatreWiden(double degrees) {
    PortTheatreCamera *camera = &g_theatre.view.camera;

    camera->plane = port_TheatrePlane(port_TheatreFov(camera->plane) + degrees);
}

/* The camera moved `forward` and `right` along the ground the way it
 * looks, and `up`, in world units (y is down: up is less y). As
 * camera_CalcEye places the eye: the eye looks along (sin b cos a, -sin a,
 * cos b cos a), a and b the pitch and yaw turned back; its right is
 * (cos b, 0, -sin b). */
static void port_TheatreMove(double forward, double right, double up) {
    PortTheatreCamera *camera = &g_theatre.view.camera;
    double yaw = -port_Radians(camera->angles[1]);

    camera->focus[0] += (s32)(forward * SDL_sin(yaw) + right * SDL_cos(yaw));
    camera->focus[2] += (s32)(forward * SDL_cos(yaw) - right * SDL_sin(yaw));
    camera->focus[1] -= (s32)up;
}

static void port_TheatreTurn(double yaw, double pitch) {
    PortTheatreCamera *camera = &g_theatre.view.camera;
    s32 to = camera->angles[0] + (s32)pitch;

    camera->angles[1] = (s16)((camera->angles[1] + (s32)yaw) & (TURN - 1));
    camera->angles[0] = (s16)(to > PITCH_MOST    ? PITCH_MOST
                              : to < -PITCH_MOST ? -PITCH_MOST
                                                 : to);
}

static void port_TheatreZoom(double times) {
    PortTheatreCamera *camera = &g_theatre.view.camera;
    double to = camera->distance * times;

    camera->distance = (s32)(to < DISTANCE_LEAST  ? DISTANCE_LEAST
                             : to > DISTANCE_MOST ? DISTANCE_MOST
                                                  : to);
}

static double port_Stick(s32 value) {
    if (value > -STICK_DEAD && value < STICK_DEAD) {
        return 0.0;
    }
    return value / STICK_FULL;
}

/* What is held moves the camera for the time since the last tick. */
static void port_TheatreMotion(void) {
    u32 now = SDL_GetTicks();
    double seconds = (now - g_theatre.ticked_at) / 1000.0;
    double speed;
    double forward;
    double right;
    double up;
    double yaw;
    double pitch;
    double zoom;
    double wider;
    const s32 *held = g_theatre.held;

    if (g_theatre.ticked_at == 0 || now - g_theatre.ticked_at > TICK_MOST_MS) {
        seconds = TICK_MOST_MS / 1000.0;
    }
    g_theatre.ticked_at = now;
    speed = g_theatre.shift ? FAST : 1.0;
    forward =
        held[HELD_FORWARD] - held[HELD_BACK] - port_Stick(g_theatre.stick[1]);
    right = held[HELD_RIGHT] - held[HELD_LEFT] + port_Stick(g_theatre.stick[0]);
    up = held[HELD_UP] - held[HELD_DOWN];
    yaw = held[HELD_YAW_RIGHT] - held[HELD_YAW_LEFT] +
          port_Stick(g_theatre.stick[2]);
    pitch = held[HELD_PITCH_UP] - held[HELD_PITCH_DOWN] +
            port_Stick(g_theatre.stick[3]);
    zoom = held[HELD_FARTHER] - held[HELD_NEARER];
    wider = held[HELD_WIDER] - held[HELD_NARROWER];
    /* the triggers: nearer and farther, or with L1 held the field of
     * view narrower and wider */
    if (g_theatre.l1) {
        wider += (g_theatre.triggers[1] - g_theatre.triggers[0]) / STICK_FULL;
    } else {
        zoom += (g_theatre.triggers[1] - g_theatre.triggers[0]) / STICK_FULL;
    }
    if (forward == 0 && right == 0 && up == 0 && yaw == 0 && pitch == 0 &&
        zoom == 0 && wider == 0) {
        return;
    }
    port_TheatreFree();
    {
        double step = g_theatre.view.camera.distance * MOVE_A_SECOND;

        if (step < MOVE_LEAST) {
            step = MOVE_LEAST;
        }
        step *= speed * seconds;
        port_TheatreMove(forward * step, right * step, up * step);
    }
    port_TheatreTurn(yaw * ORBIT_A_SECOND * speed * seconds,
                     pitch * ORBIT_A_SECOND * speed * seconds);
    port_TheatreZoom(SDL_exp(zoom * ZOOM_A_SECOND * speed * seconds));
    if (wider != 0) {
        port_TheatreWiden(wider * FOV_A_SECOND * speed * seconds);
    }
    port_TheatreApply();
}

/* ---- what the player asks ---- */

static void port_TheatreToggleHud(void) {
    g_theatre.view.hide_hud = !g_theatre.view.hide_hud;
    port_TheatreApply();
    port_TheatreSays(
        "the HUD %s", g_theatre.view.hide_hud ? "hidden" : "shown");
}

static void port_TheatreNextTime(void) {
    s32 time = g_theatre.view.time + 1;

    g_theatre.view.time = time >= PORT_TIME_COUNT ? -1 : time;
    port_TheatreApply();
    port_TheatreSays("the time of day: %s", g_times[g_theatre.view.time + 1]);
    if (!port_StageIsDressed() && g_theatre.view.time >= 0) {
        port_TheatreSays("%s", "the time of day: a Remastered Classic stage "
                               "or an arena has one (the stage page's kind)");
    }
}

static void port_TheatreCameraKey(void) {
    if (g_theatre.view.free) {
        g_theatre.view.free = FALSE;
        port_TheatreApply();
        port_TheatreSays("%s", "the game's camera");
    } else {
        port_TheatreFree();
        port_TheatreSays("%s", "the free camera (the game's to begin with)");
    }
}

static void port_TheatreStill(const char *path) {
    char written[PATH_BYTES];
    s32 w = 0;
    s32 h = 0;
    s32 was = g_port_unseen;
    s32 ok;

    /* the picture on show: the last look's, wherever the still is asked
     * (a boundary's turn begins as if no look had drawn it) */
    g_port_unseen = port_TheatreShowsLook() || was;
    ok = port_WriteWindowStill(path, written, sizeof(written), &w, &h);
    g_port_unseen = was;
    if (ok) {
        char words[PATH_BYTES + 64];

        snprintf(words, sizeof(words), "a still, %d by %d: %s", w, h, written);
        port_TheatreSays("%s", words);
    } else {
        port_TheatreSays("%s", "no still: the window's pixels cannot be read");
    }
}

static void port_TheatreClip(u32 from, u32 to, s32 is_gif) {
    char view[VIEW_BYTES];
    char why[PORT_REPLAY_WHY_BYTES];
    const char *path = port_ReplayPlayingPath();
    const char *name;
    PortExportClip clip;

    if (path == NULL) {
        return;
    }
    name = strrchr(path, '/');
    if (name == NULL) {
        name = strrchr(path, '\\');
    }
    name = name != NULL ? name + 1 : path;
    port_TheatreViewWords(&g_theatre.view, view, sizeof(view));
    clip.from = from < to ? from : to;
    clip.to = from < to ? to : from;
    clip.view = view;
    clip.path = NULL;
    if (port_TheatreFollows()) {
        /* the camera's keys with it: the clip's run follows them */
        static char g_path_words[PATH_WORD_BYTES];

        port_TheatrePathWords(
            &g_theatre.path, g_path_words, sizeof(g_path_words));
        clip.path = g_path_words;
    }
    clip.is_gif = is_gif;
    if (!port_ExportClip(path, name, &clip, why, sizeof(why))) {
        port_TheatreSays("no clip: %s", why);
        return;
    }
    snprintf(why, sizeof(why), "frames %u to %u as a %s, made beside the game",
             clip.from, clip.to, is_gif ? "GIF" : "film");
    port_TheatreSays("a clip: %s", why);
}

/* The clip of the marks, or from the frame on show. */
static void port_TheatreClipMarked(void) {
    PortReplayStatus status;
    u32 from;
    u32 to;

    port_GetReplayStatus(&status);
    from = g_theatre.has_in ? g_theatre.mark_in : status.frame;
    to = g_theatre.has_out && g_theatre.mark_out != from
             ? g_theatre.mark_out
             : from + CLIP_FRAMES_BUILT_IN;
    if (to >= status.frames) {
        to = status.frames > 0 ? status.frames - 1 : 0;
    }
    port_TheatreClip(from, to, g_port_settings.clip_kind == 0);
}

static void port_TheatreMark(s32 is_out) {
    PortReplayStatus status;
    char words[64];

    port_GetReplayStatus(&status);
    if (is_out) {
        g_theatre.mark_out = status.frame;
        g_theatre.has_out = TRUE;
    } else {
        g_theatre.mark_in = status.frame;
        g_theatre.has_in = TRUE;
    }
    snprintf(words, sizeof(words), "the clip %s at frame %u",
             is_out ? "ends" : "begins", status.frame);
    port_TheatreSays("%s", words);
}

static void port_TheatreSeek(s32 frames) {
    PortReplayStatus status;
    s32 to;

    port_GetReplayStatus(&status);
    to = (s32)status.frame + frames;
    port_ReplaySeekTo(to < 0 ? 0 : (u32)to);
}

static void port_TheatrePhoto(void) {
    const char *refused;

    if (g_theatre.photo || g_theatre.photo_asked) {
        g_theatre.photo = FALSE;
        g_theatre.photo_asked = FALSE;
        port_TheatreSays("%s", "photo mode off: the fight goes on");
        return;
    }
    refused = port_PhotoRefused();
    if (refused != NULL) {
        port_TheatreSays("no photo mode: %s", refused);
        return;
    }
    g_theatre.photo_asked = TRUE;
    /* the free camera from the game's own, the HUD and time as they were */
    g_theatre.view.free = FALSE;
    port_TheatreFree();
}

/* ---- the camera's path ---- */

/* Whether the free camera follows its path: in a replay, with a key or
 * more, and not let go of it (O). */
static s32 port_TheatreFollows(void) {
    return g_theatre.replay && g_theatre.path_on && g_theatre.view.free &&
           g_theatre.path.count > 0;
}

/* The free camera put on its path at the frame on show, where it follows
 * one and was not put there at this frame already: a move of the player's
 * stays until the frame changes, so that a key can be set from it. */
static void port_TheatreFollow(void) {
    PortReplayStatus status;
    PortTheatreCamera camera;

    if (!port_TheatreFollows()) {
        return;
    }
    port_GetReplayStatus(&status);
    if (g_theatre.path_frame == status.frame + 1) {
        return;
    }
    g_theatre.path_frame = status.frame + 1;
    if (port_PathAt(&g_theatre.path, status.frame, &camera)) {
        g_theatre.view.camera = camera;
        port_TheatreApply();
    }
}

/* A key of the free camera as it is at the frame on show (J, Insert). */
static void port_TheatreKey(void) {
    PortReplayStatus status;
    char words[WORD_BYTES];

    port_GetReplayStatus(&status);
    port_TheatreFree();
    if (port_PathSet(&g_theatre.path, status.frame, &g_theatre.view.camera) <
        0) {
        snprintf(words, sizeof(words), "no key: the path has %d already",
                 PORT_PATH_KEYS);
        port_TheatreSays("%s", words);
        return;
    }
    g_theatre.path_on = TRUE;
    g_theatre.path_frame = status.frame + 1;
    snprintf(words, sizeof(words),
             "a key of the camera at frame %u (%d in all), %.0f degrees seen",
             status.frame, g_theatre.path.count,
             port_TheatreFov(g_theatre.view.camera.plane));
    port_TheatreSays("%s", words);
}

/* The key nearest the frame on show cleared (U, Delete), or all of them
 * (with Shift). */
static void port_TheatreUnkey(s32 all) {
    PortReplayStatus status;
    char words[WORD_BYTES];
    u32 cleared = 0;

    port_GetReplayStatus(&status);
    g_theatre.path_frame = 0;
    if (all) {
        snprintf(words, sizeof(words), "the camera's %d keys cleared",
                 g_theatre.path.count);
        g_theatre.path.count = 0;
    } else if (port_PathClear(&g_theatre.path, status.frame, &cleared)) {
        snprintf(words, sizeof(words), "the key at frame %u cleared (%d left)",
                 cleared, g_theatre.path.count);
    } else {
        snprintf(words, sizeof(words), "%s", "no key to clear");
    }
    port_TheatreSays("%s", words);
}

/* The camera on its path, or let go of it to be moved freely as it plays
 * (O). */
static void port_TheatrePathKey(void) {
    g_theatre.path_on = !g_theatre.path_on;
    g_theatre.path_frame = 0;
    port_TheatreSays(
        "%s", g_theatre.path.count == 0 ? "no keys: J sets one"
              : g_theatre.path_on       ? "the camera follows its keys"
                                        : "the camera let go of its keys");
}

/* ---- the seek bar ---- */

/* Where the bar is in the window's pixels: its strip, and its track. */
typedef struct {
    PortRect area; /* the game's 4:3 area */
    PortRect band;
    PortRect track;
} TheatreBarPlace;

static void port_TheatreBarPlace(TheatreBarPlace *place) {
    PortRect *area = &place->area;

    port_GetLookArea(area);
    place->band.x = area->x + BAR_X * area->w / PORT_LOOK_GAME_W;
    place->band.w =
        area->x + (BAR_X + BAR_W) * area->w / PORT_LOOK_GAME_W - place->band.x;
    place->band.y = area->y + BAR_Y * area->h / PORT_LOOK_GAME_H;
    place->band.h =
        area->y + (BAR_Y + BAR_H) * area->h / PORT_LOOK_GAME_H - place->band.y;
    place->track.x = area->x + TRACK_X * area->w / PORT_LOOK_GAME_W;
    place->track.w =
        area->x + (TRACK_X + TRACK_W) * area->w / PORT_LOOK_GAME_W -
        place->track.x;
    place->track.y = area->y + TRACK_Y * area->h / PORT_LOOK_GAME_H;
    place->track.h = TRACK_LINE * area->h / PORT_LOOK_GAME_H;
    if (place->track.h < 1) {
        place->track.h = 1;
    }
}

/* Whether the bar is shown: in a replay of the theatre's (not a film's
 * run), with the HUD and the line, or for a while after the mouse moved,
 * or while it is held. */
static s32 port_TheatreBarShown(void) {
    if (!g_theatre.replay || port_ExportIsOn()) {
        return FALSE;
    }
    if (g_theatre.bar_held ||
        (g_theatre.mouse_at != 0 &&
         SDL_GetTicks() - g_theatre.mouse_at < BAR_MOUSE_MS)) {
        return TRUE;
    }
    return !g_theatre.view.hide_hud && !g_theatre.line_hidden;
}

static s32 port_TheatreOnBar(s32 x, s32 y) {
    TheatreBarPlace place;

    if (!port_TheatreBarShown()) {
        return FALSE;
    }
    port_TheatreBarPlace(&place);
    return x >= place.band.x && x < place.band.x + place.band.w &&
           y >= place.band.y && y < place.band.y + place.band.h;
}

/* The frame at the mouse's `x` on the bar sought, where it is another
 * than the drag sought last. */
static void port_TheatreBarSeek(s32 x) {
    PortReplayStatus status;
    TheatreBarPlace place;
    u32 frame;

    port_GetReplayStatus(&status);
    port_TheatreBarPlace(&place);
    frame = port_BarFrameAt(x, place.track.x, place.track.w, status.frames);
    if (g_theatre.bar_sought == frame + 1) {
        return;
    }
    g_theatre.bar_sought = frame + 1;
    port_ReplaySeekTo(frame);
}

static void port_TheatreMouseAt(s32 x, s32 y) {
    g_theatre.mouse_x = x;
    g_theatre.mouse_y = y;
    g_theatre.mouse_at = SDL_GetTicks();
    if (g_theatre.mouse_at == 0) {
        g_theatre.mouse_at = 1;
    }
}

/* A time as the line says it: minutes and seconds. */
static void port_TheatreClock(u32 frame, char *out, u32 room) {
    u32 seconds = frame / FRAMES_PER_SECOND;

    snprintf(out, room, "%u:%02u", seconds / 60, seconds % 60);
}

void port_TheatreDrawBar(void) {
    PortReplayStatus status;
    TheatreBarPlace place;
    PortLook *look = &g_theatre.bar;
    char words[32];
    u32 need;
    s32 left;
    s32 top;
    s32 line;
    s32 x;
    s32 i;

    if (!port_TheatreBarShown()) {
        return;
    }
    port_GetReplayStatus(&status);
    port_TheatreBarPlace(&place);
    if (place.area.w <= 0 || place.area.h <= 0 || status.frames == 0) {
        return;
    }
    if (!look->font_ok) {
        look->font_ok = port_FontStart();
        if (!look->font_ok) {
            return;
        }
    }
    /* a page of the game's look the size of its area, of which only the
     * bar's strip is cleared, drawn and laid over the picture */
    need = (u32)(place.area.w * place.area.h);
    if (need > look->room) {
        free(look->pixels);
        look->pixels = malloc((size_t)need * sizeof(u32));
        look->room = look->pixels != NULL ? need : 0;
    }
    if (look->pixels == NULL) {
        return;
    }
    look->area_w = place.area.w;
    look->area_h = place.area.h;
    top = place.band.y - place.area.y;
    memset(look->pixels + top * look->area_w, 0,
           (size_t)(look->area_w * place.band.h) * sizeof(u32));
    /* the game's panel, and the track's line on it */
    port_LookBox(look, BAR_X, BAR_Y, BAR_W, BAR_H, PORT_LOOK_PANEL);
    left = place.track.x - place.area.x;
    line = place.track.h;
    top = place.track.y - place.area.y;
    port_FillRect(look->pixels, look->area_w, look->area_h, left,
                  top - line / 2, place.track.w, line, PORT_LOOK_OFF);
    /* the part played, in the rows' gold */
    x = port_BarXOf(status.frame, left, place.track.w, status.frames);
    port_FillRect(look->pixels, look->area_w, look->area_h, left,
                  top - line / 2, x - left, line, PORT_LOOK_ROW);
    /* the clip's frames marked, brighter and thicker, its ends in white */
    if (g_theatre.has_in || g_theatre.has_out) {
        u32 from = g_theatre.has_in ? g_theatre.mark_in : 0;
        u32 to = g_theatre.has_out ? g_theatre.mark_out : status.frames - 1;
        s32 a = port_BarXOf(
            from < to ? from : to, left, place.track.w, status.frames);
        s32 b = port_BarXOf(
            from < to ? to : from, left, place.track.w, status.frames);
        s32 up = MARK_UP * look->area_h / PORT_LOOK_GAME_H;

        port_FillRect(
            look->pixels, look->area_w, look->area_h, a,
            top - line / 2 - line / 2, b - a + 1, line + line, PORT_LOOK_GOLD);
        if (g_theatre.has_in) {
            port_FillRect(look->pixels, look->area_w, look->area_h, a, top - up,
                          line > 1 ? line / 2 + 1 : 1, 2 * up, PORT_LOOK_WHITE);
        }
        if (g_theatre.has_out) {
            port_FillRect(look->pixels, look->area_w, look->area_h, b, top - up,
                          line > 1 ? line / 2 + 1 : 1, 2 * up, PORT_LOOK_WHITE);
        }
    }
    /* the camera's keys, in green under the track */
    for (i = 0; i < g_theatre.path.count; i++) {
        s32 kx = port_BarXOf(
            g_theatre.path.keys[i].frame, left, place.track.w, status.frames);
        s32 w = KEY_W * look->area_w / PORT_LOOK_GAME_W;

        w = w < 2 ? 2 : w;
        port_FillRect(look->pixels, look->area_w, look->area_h, kx - w / 2,
                      top + KEY_DOWN * look->area_h / PORT_LOOK_GAME_H, w,
                      KEY_H * look->area_h / PORT_LOOK_GAME_H + 1,
                      port_TheatreFollows() ? PORT_LOOK_GOOD : PORT_LOOK_GREY);
    }
    /* where the mouse would seek to */
    if (!g_theatre.bar_held &&
        port_TheatreOnBar(g_theatre.mouse_x, g_theatre.mouse_y)) {
        s32 mx = g_theatre.mouse_x - place.area.x;
        s32 up = HEAD_UP * look->area_h / PORT_LOOK_GAME_H;

        if (mx >= left && mx <= left + place.track.w) {
            port_FillRect(
                look->pixels, look->area_w, look->area_h, mx, top - up,
                line > 1 ? line / 2 : 1, 2 * up, PORT_LOOK_GREY);
        }
    }
    /* the frame on show: the cursor's yellow, glowing as the game's */
    {
        s32 w = HEAD_W * look->area_w / PORT_LOOK_GAME_W;
        s32 up = HEAD_UP * look->area_h / PORT_LOOK_GAME_H;

        w = w < 2 ? 2 : w;
        port_FillRect(
            look->pixels, look->area_w, look->area_h, x - w / 2, top - up, w,
            2 * up, port_LookPulse((s32)(SDL_GetTicks() / BLANK_MS / 2)));
    }
    /* the time on show and the replay's length either side */
    port_TheatreClock(status.frame, words, sizeof(words));
    port_LookText(
        look, TRACK_X - TRACK_TEXT_GAP, TEXT_BASE, PORT_LOOK_SIZE_NOTE,
        PORT_FACE_BOLD, words, PORT_LOOK_WHITE, PORT_LOOK_RIGHT);
    port_TheatreClock(status.frames, words, sizeof(words));
    port_LookText(look, TRACK_X + TRACK_W + TRACK_TEXT_GAP, TEXT_BASE,
                  PORT_LOOK_SIZE_NOTE, PORT_FACE_TEXT, words, PORT_LOOK_GREY,
                  PORT_LOOK_LEFT);
    top = place.band.y - place.area.y;
    port_LayPixels(look->pixels + top * look->area_w, look->area_w,
                   place.band.h, place.area.x, place.band.y);
}

/* ---- the keys, the mouse and the sticks ---- */

static s32 port_TheatreHeldKey(SDL_Scancode code, s32 down) {
    s32 held = -1;

    switch (code) {
    case SDL_SCANCODE_W:
        held = HELD_FORWARD;
        break;
    case SDL_SCANCODE_S:
        held = HELD_BACK;
        break;
    case SDL_SCANCODE_A:
        held = HELD_LEFT;
        break;
    case SDL_SCANCODE_D:
        held = HELD_RIGHT;
        break;
    case SDL_SCANCODE_E:
        held = HELD_UP;
        break;
    case SDL_SCANCODE_Q:
        held = HELD_DOWN;
        break;
    case SDL_SCANCODE_Z:
        held = HELD_NEARER;
        break;
    case SDL_SCANCODE_X:
        held = HELD_FARTHER;
        break;
    case SDL_SCANCODE_N:
        held = HELD_NARROWER;
        break;
    case SDL_SCANCODE_M:
        held = HELD_WIDER;
        break;
    case SDL_SCANCODE_LEFT:
        held = g_theatre.view.free ? HELD_YAW_LEFT : -1;
        break;
    case SDL_SCANCODE_RIGHT:
        held = g_theatre.view.free ? HELD_YAW_RIGHT : -1;
        break;
    case SDL_SCANCODE_UP:
        held = g_theatre.view.free ? HELD_PITCH_UP : -1;
        break;
    case SDL_SCANCODE_DOWN:
        held = g_theatre.view.free ? HELD_PITCH_DOWN : -1;
        break;
    case SDL_SCANCODE_LSHIFT:
    case SDL_SCANCODE_RSHIFT:
        g_theatre.shift = down;
        return TRUE;
    default:
        break;
    }
    if (held < 0) {
        /* an arrow let go after the free camera went off is the camera's */
        if (!down && (code == SDL_SCANCODE_LEFT || code == SDL_SCANCODE_RIGHT ||
                      code == SDL_SCANCODE_UP || code == SDL_SCANCODE_DOWN)) {
            g_theatre.held[HELD_YAW_LEFT] = g_theatre.held[HELD_YAW_RIGHT] = 0;
            g_theatre.held[HELD_PITCH_UP] = g_theatre.held[HELD_PITCH_DOWN] = 0;
        }
        return FALSE;
    }
    g_theatre.held[held] = down;
    return TRUE;
}

/* A key of the theatre's, pressed: TRUE where it was one. */
static s32 port_TheatrePressed(SDL_Scancode code) {
    switch (code) {
    case SDL_SCANCODE_C:
        port_TheatreCameraKey();
        return TRUE;
    case SDL_SCANCODE_H:
        port_TheatreToggleHud();
        return TRUE;
    case SDL_SCANCODE_T:
        port_TheatreNextTime();
        return TRUE;
    case SDL_SCANCODE_V:
        port_TheatreStill(NULL);
        return TRUE;
    case SDL_SCANCODE_TAB:
        g_theatre.line_hidden = !g_theatre.line_hidden;
        return TRUE;
    default:
        break;
    }
    if (!g_theatre.replay) {
        return FALSE;
    }
    switch (code) {
    case SDL_SCANCODE_LEFT:
    case SDL_SCANCODE_COMMA:
        /* (the arrows are the free camera's while it is on) */
        port_ReplayControl(PORT_REPLAY_HOLD);
        port_TheatreSeek(-1);
        return TRUE;
    case SDL_SCANCODE_PAGEUP:
        port_TheatreSeek(-SEEK_SECONDS * FRAMES_PER_SECOND);
        return TRUE;
    case SDL_SCANCODE_PAGEDOWN:
        port_TheatreSeek(SEEK_SECONDS * FRAMES_PER_SECOND);
        return TRUE;
    case SDL_SCANCODE_LEFTBRACKET:
        port_TheatreMark(FALSE);
        return TRUE;
    case SDL_SCANCODE_RIGHTBRACKET:
        port_TheatreMark(TRUE);
        return TRUE;
    case SDL_SCANCODE_G:
        port_TheatreClipMarked();
        return TRUE;
    case SDL_SCANCODE_J:
    case SDL_SCANCODE_INSERT:
        port_TheatreKey();
        return TRUE;
    case SDL_SCANCODE_U:
    case SDL_SCANCODE_DELETE:
        port_TheatreUnkey(g_theatre.shift);
        return TRUE;
    case SDL_SCANCODE_O:
        port_TheatrePathKey();
        return TRUE;
    default:
        break;
    }
    if (code >= SDL_SCANCODE_1 && code <= SDL_SCANCODE_0) {
        /* 1 to 9 a tenth of the way on each, 0 the start */
        PortReplayStatus status;
        u32 tenth =
            code == SDL_SCANCODE_0 ? 0 : (u32)(code - SDL_SCANCODE_1) + 1;

        port_GetReplayStatus(&status);
        port_ReplaySeekTo(status.frames * tenth / TENTHS);
        return TRUE;
    }
    return FALSE;
}

/* A controller's button with L1 held, in a replay: Cross a key of the
 * camera (J), Square the nearest cleared (U), Circle every key cleared
 * (Shift and U), Triangle the camera on its keys or let go (O). TRUE
 * where it was one of them, which the replay's own controls then never
 * see (Cross pauses, Square and Triangle seek, Circle stops). */
static s32 port_TheatreKeysButton(u8 button) {
    if (!g_theatre.replay) {
        return FALSE;
    }
    switch (button) {
    case SDL_CONTROLLER_BUTTON_A:
        port_TheatreKey();
        return TRUE;
    case SDL_CONTROLLER_BUTTON_X:
        port_TheatreUnkey(FALSE);
        return TRUE;
    case SDL_CONTROLLER_BUTTON_B:
        port_TheatreUnkey(TRUE);
        return TRUE;
    case SDL_CONTROLLER_BUTTON_Y:
        port_TheatrePathKey();
        return TRUE;
    default:
        return FALSE;
    }
}

s32 port_TheatreWindowEvent(const union SDL_Event *event) {
    if (port_MenuIsOpen() || port_OnlinePagesAreOpen()) {
        /* a page over the game has every key */
        return FALSE;
    }
    if (event->type == SDL_KEYDOWN &&
        event->key.keysym.scancode == SDL_SCANCODE_F4) {
        if (event->key.repeat == 0) {
            port_TheatrePhoto();
        }
        return TRUE;
    }
    if (!port_TheatreActive()) {
        /* (an L1 held as the theatre ended is let go) */
        g_theatre.l1 = FALSE;
        return FALSE;
    }
    if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) {
        SDL_Scancode code = event->key.keysym.scancode;
        s32 down = event->type == SDL_KEYDOWN;

        if (code >= SDL_SCANCODE_F1 && code <= SDL_SCANCODE_F12) {
            /* the window's own, but while photo mode holds the game,
             * whose pages and pictures come at a blank there is none of,
             * only the full screen's (F11) */
            return g_theatre.photo && code != SDL_SCANCODE_F11;
        }
        if (g_theatre.photo && down && event->key.repeat == 0 &&
            (code == SDL_SCANCODE_ESCAPE || code == SDL_SCANCODE_BACKSPACE)) {
            port_TheatrePhoto();
            return TRUE;
        }
        if (port_TheatreHeldKey(code, down)) {
            return TRUE;
        }
        if (down && event->key.repeat == 0 && port_TheatrePressed(code)) {
            return TRUE;
        }
        /* photo mode keeps every other key from the game, which waits */
        return g_theatre.photo || g_theatre.photo_asked;
    }
    switch (event->type) {
    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
        s32 down = event->type == SDL_MOUSEBUTTONDOWN;
        s32 x = event->button.x;
        s32 y = event->button.y;

        port_MouseToPixels(&x, &y);
        port_TheatreMouseAt(x, y);
        if (event->button.button == SDL_BUTTON_LEFT) {
            if (down && port_TheatreOnBar(x, y)) {
                /* the seek bar's: a press seeks, and a drag along it */
                g_theatre.bar_held = TRUE;
                g_theatre.bar_sought = 0;
                port_TheatreBarSeek(x);
                return TRUE;
            }
            if (!down && g_theatre.bar_held) {
                g_theatre.bar_held = FALSE;
                return TRUE;
            }
            g_theatre.mouse_left = down;
        } else if (event->button.button == SDL_BUTTON_RIGHT) {
            g_theatre.mouse_right = down;
        }
        return TRUE;
    }
    case SDL_MOUSEMOTION: {
        s32 x = event->motion.x;
        s32 y = event->motion.y;

        port_MouseToPixels(&x, &y);
        port_TheatreMouseAt(x, y);
        if (g_theatre.bar_held) {
            port_TheatreBarSeek(x);
            return TRUE;
        }
        if (g_theatre.mouse_left || g_theatre.mouse_right) {
            port_TheatreFree();
            if (g_theatre.mouse_left) {
                port_TheatreTurn(event->motion.xrel * MOUSE_TURN,
                                 event->motion.yrel * MOUSE_TURN);
            } else {
                double step = g_theatre.view.camera.distance * MOUSE_PAN;

                port_TheatreMove(
                    0, -event->motion.xrel * step, event->motion.yrel * step);
            }
            port_TheatreApply();
        }
        return TRUE;
    }
    case SDL_MOUSEWHEEL:
        port_TheatreFree();
        if (SDL_GetModState() & KMOD_CTRL) {
            /* with Ctrl, the field of view: up narrower, as a zoom in */
            port_TheatreWiden(-event->wheel.y * FOV_A_NOTCH);
        } else {
            port_TheatreZoom(1.0 - event->wheel.y * WHEEL_ZOOM);
        }
        port_TheatreApply();
        return TRUE;
    case SDL_CONTROLLERAXISMOTION:
        if (event->caxis.axis <= SDL_CONTROLLER_AXIS_RIGHTY) {
            g_theatre.stick[event->caxis.axis] = event->caxis.value;
            return TRUE;
        }
        g_theatre
            .triggers[event->caxis.axis == SDL_CONTROLLER_AXIS_TRIGGERRIGHT] =
            event->caxis.value;
        return TRUE;
    case SDL_CONTROLLERBUTTONDOWN:
        if (event->cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
            /* L1 held: the triggers the field of view, and the camera's
             * keys under four buttons (the replay has no L1 of its own) */
            g_theatre.l1 = TRUE;
            return TRUE;
        }
        if (g_theatre.l1 && port_TheatreKeysButton(event->cbutton.button)) {
            return TRUE;
        }
        if (event->cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSTICK) {
            port_TheatreCameraKey();
            return TRUE;
        }
        if (event->cbutton.button == SDL_CONTROLLER_BUTTON_RIGHTSTICK) {
            port_TheatreToggleHud();
            return TRUE;
        }
        if (g_theatre.photo &&
            (event->cbutton.button == SDL_CONTROLLER_BUTTON_B ||
             event->cbutton.button == SDL_CONTROLLER_BUTTON_BACK)) {
            port_TheatrePhoto();
            return TRUE;
        }
        return g_theatre.photo;
    case SDL_CONTROLLERBUTTONUP:
        if (event->cbutton.button == SDL_CONTROLLER_BUTTON_LEFTSHOULDER) {
            g_theatre.l1 = FALSE;
            return TRUE;
        }
        return g_theatre.photo;
    default:
        return FALSE;
    }
}

/* ---- a test's words (--theatre-keys) ---- */

/* The next word into `word`, from the words left: FALSE when none. */
static s32 port_TheatreNextWord(char *word, u32 room) {
    const char *at = g_theatre.words;
    u32 length = 0;

    if (at == NULL) {
        return FALSE;
    }
    while (*at == ' ' || *at == ',') {
        at++;
    }
    if (*at == 0) {
        g_theatre.words = NULL;
        return FALSE;
    }
    while (at[length] != 0 && at[length] != ' ' && length + 1 < room) {
        word[length] = at[length];
        length++;
    }
    word[length] = 0;
    return TRUE;
}

static void port_TheatreUseWord(u32 length) {
    g_theatre.words += length;
    while (*g_theatre.words == ' ') {
        g_theatre.words++;
    }
}

static void port_TheatreHash(void) {
    PortReplayStatus status;
    u64 hash = port_StateHash();
    PortTheatreCounts counts;

    port_GetReplayStatus(&status);
    port_TheatreGetCounts(&counts);
    port_Say("theatre: frame %u input %u hash %08X%08X looks %u failed %u "
             "paused %d",
             status.frame, g_port_input_count, (u32)(hash >> 32), (u32)hash,
             counts.looks, counts.failed, status.is_paused);
}

/* The camera a look draws with now, for a test (`camera`). */
static void port_TheatreSayCamera(void) {
    PortReplayStatus status;
    const PortTheatreCamera *camera = &g_theatre.view.camera;

    port_GetReplayStatus(&status);
    port_Say("theatre: camera at frame %u free %d follows %d keys %d focus %d "
             "%d %d angles %d %d %d distance %d plane %d fov %.2f",
             status.frame, g_theatre.view.free, port_TheatreFollows(),
             g_theatre.path.count, camera->focus[0], camera->focus[1],
             camera->focus[2], camera->angles[0], camera->angles[1],
             camera->angles[2], camera->distance, camera->plane,
             port_TheatreFov(camera->plane));
}

/* Where the seek bar is, for a test (`bar`): its track in the window's
 * pixels, the strip a press on it seeks from, the replay's frames. */
static void port_TheatreSayBar(void) {
    PortReplayStatus status;
    TheatreBarPlace place;

    port_GetReplayStatus(&status);
    port_TheatreBarPlace(&place);
    port_Say("theatre: bar track %d %d band %d %d %d %d frames %u shown %d",
             place.track.x, place.track.w, place.band.x, place.band.y,
             place.band.w, place.band.h, status.frames, port_TheatreBarShown());
}

/* The mouse moved to (`from`, `y`), its left button pressed there, moved
 * along to `to` and let go, as SDL's events of them (`click:`, `drag:`),
 * in the window's own units. */
static void port_TheatreTestMouse(s32 from, s32 to, s32 y) {
    SDL_Event event;
    s32 i;

    SDL_zero(event);
    event.type = SDL_MOUSEMOTION;
    event.motion.x = from;
    event.motion.y = y;
    port_TheatreWindowEvent(&event);
    SDL_zero(event);
    event.type = SDL_MOUSEBUTTONDOWN;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = from;
    event.button.y = y;
    port_TheatreWindowEvent(&event);
    for (i = 1; i <= 4 && to != from; i++) {
        SDL_zero(event);
        event.type = SDL_MOUSEMOTION;
        event.motion.x = from + (to - from) * i / 4;
        event.motion.y = y;
        event.motion.xrel = (to - from) / 4;
        port_TheatreWindowEvent(&event);
    }
    SDL_zero(event);
    event.type = SDL_MOUSEBUTTONUP;
    event.button.button = SDL_BUTTON_LEFT;
    event.button.x = to;
    event.button.y = y;
    port_TheatreWindowEvent(&event);
}

/* One word, where its time has come: FALSE where it waits (at:, input:,
 * wait:, fight, photo's own wait). */
static s32 port_TheatreWord(const char *word) {
    PortReplayStatus status;
    int a = 0;
    int b = 0;
    int c = 0;
    char path[PATH_BYTES];

    port_GetReplayStatus(&status);
    if (sscanf(word, "at:%d", &a) == 1) {
        return status.is_playing && !status.in_lead && status.frame >= (u32)a;
    }
    if (sscanf(word, "input:%d", &a) == 1) {
        return g_port_input_count >= (u32)a;
    }
    if (strcmp(word, "fight") == 0) {
        return port_IsFightScreen();
    }
    if (strcmp(word, "clip-wait") == 0) {
        /* the clip asked for made, or not */
        PortExportView made;

        port_ExportTick();
        port_ExportGetView(&made);
        return made.state != PORT_EXPORT_RUNNING;
    }
    if (strcmp(word, "photo-on") == 0) {
        /* photo mode asked for and now waiting */
        return g_theatre.photo;
    }
    if (sscanf(word, "wait:%d", &a) == 1) {
        if (g_theatre.wait == 0) {
            g_theatre.wait = a + 1;
        }
        if (--g_theatre.wait > 0) {
            return FALSE;
        }
        return TRUE;
    }
    if (strcmp(word, "pause") == 0) {
        port_ReplayControl(PORT_REPLAY_PAUSE);
    } else if (strcmp(word, "step") == 0) {
        port_ReplayControl(PORT_REPLAY_STEP);
    } else if (strcmp(word, "back") == 0) {
        port_ReplayControl(PORT_REPLAY_HOLD);
        port_TheatreSeek(-1);
    } else if (strcmp(word, "faster") == 0) {
        port_ReplayControl(PORT_REPLAY_FASTER);
    } else if (strcmp(word, "slower") == 0) {
        port_ReplayControl(PORT_REPLAY_SLOWER);
    } else if (sscanf(word, "seek:%d", &a) == 1) {
        port_ReplaySeekTo((u32)a);
    } else if (strcmp(word, "free") == 0) {
        port_TheatreFree();
    } else if (strcmp(word, "game") == 0) {
        g_theatre.view.free = FALSE;
        port_TheatreApply();
    } else if (sscanf(word, "orbit:%d:%d", &a, &b) == 2) {
        port_TheatreFree();
        port_TheatreTurn(a, b);
        port_TheatreApply();
    } else if (sscanf(word, "move:%d:%d:%d", &a, &b, &c) == 3) {
        port_TheatreFree();
        port_TheatreMove(a, b, c);
        port_TheatreApply();
    } else if (sscanf(word, "zoom:%d", &a) == 1) {
        port_TheatreFree();
        port_TheatreZoom(a / 100.0);
        port_TheatreApply();
    } else if (strcmp(word, "hud") == 0) {
        port_TheatreToggleHud();
    } else if (sscanf(word, "time:%d", &a) == 1) {
        g_theatre.view.time = a >= -1 && a < PORT_TIME_COUNT ? a : -1;
        port_TheatreApply();
    } else if (sscanf(word, "still:%1000s", path) == 1) {
        port_TheatreStill(path);
    } else if (strcmp(word, "hash") == 0) {
        port_TheatreHash();
    } else if (strcmp(word, "photo") == 0) {
        port_TheatrePhoto();
    } else if (sscanf(word, "clip:%d:%d:%d", &a, &b, &c) == 3) {
        port_TheatreClip((u32)a, (u32)b, c != 0);
    } else if (strcmp(word, "plain") == 0) {
        /* for a test: a replay played as it was before the theatre, no
         * picture a look */
        g_theatre.plain = TRUE;
    } else if (strcmp(word, "hold") == 0) {
        port_ReplayControl(PORT_REPLAY_HOLD);
    } else if (sscanf(word, "fov:%d", &a) == 1) {
        port_TheatreFree();
        g_theatre.view.camera.plane = port_TheatrePlane(a);
        port_TheatreApply();
    } else if (strcmp(word, "key") == 0) {
        port_TheatreKey();
    } else if (strcmp(word, "unkey") == 0) {
        port_TheatreUnkey(FALSE);
    } else if (strcmp(word, "unkey-all") == 0) {
        port_TheatreUnkey(TRUE);
    } else if (strcmp(word, "path") == 0) {
        port_TheatrePathKey();
    } else if (strcmp(word, "in") == 0 || strcmp(word, "out") == 0) {
        port_TheatreMark(word[0] == 'o');
    } else if (strcmp(word, "camera") == 0) {
        port_TheatreSayCamera();
    } else if (strcmp(word, "bar") == 0) {
        port_TheatreSayBar();
    } else if (sscanf(word, "pad-down:%1000s", path) == 1 ||
               sscanf(word, "pad-up:%1000s", path) == 1) {
        /* a controller's button by SDL's name (`leftshoulder`, `a`), as
         * SDL's event of it */
        SDL_Event event;

        SDL_zero(event);
        event.type =
            word[4] == 'd' ? SDL_CONTROLLERBUTTONDOWN : SDL_CONTROLLERBUTTONUP;
        event.cbutton.button =
            (Uint8)SDL_GameControllerGetButtonFromString(path);
        /* whether the theatre took it, or the replay's own controls
         * would have it (sdl_replay.c) */
        port_Say("theatre: pad %s %s taken %d",
                 event.type == SDL_CONTROLLERBUTTONDOWN ? "down" : "up", path,
                 port_TheatreWindowEvent(&event));
    } else if (sscanf(word, "pad-axis:%d:%d", &a, &b) == 2) {
        /* a controller's axis (SDL's number: 4 the left trigger, 5 the
         * right) to a value */
        SDL_Event event;

        SDL_zero(event);
        event.type = SDL_CONTROLLERAXISMOTION;
        event.caxis.axis = (Uint8)a;
        event.caxis.value = (Sint16)b;
        port_TheatreWindowEvent(&event);
    } else if (sscanf(word, "click:%d:%d", &a, &b) == 2) {
        port_TheatreTestMouse(a, a, b);
    } else if (sscanf(word, "drag:%d:%d:%d", &a, &b, &c) == 3) {
        port_TheatreTestMouse(a, b, c);
    } else if (sscanf(word, "shown:%1000s", path) == 1) {
        s32 was = g_port_unseen;

        /* the picture on show (port_TheatreStill), with what is over it */
        g_port_unseen = port_TheatreShowsLook() || was;
        port_Say("theatre: the window shown%s: %s",
                 port_WriteWindowShown(path) ? "" : " not written", path);
        g_port_unseen = was;
    } else if (strcmp(word, "quit") == 0) {
        port_Quit(PORT_EXIT_OK);
    } else {
        port_Say("--theatre-keys: no such word: %s", word);
        port_Quit(PORT_EXIT_FAILED);
    }
    return TRUE;
}

/* The words whose time has come, one after another, until one waits. */
static void port_TheatreRunWords(void) {
    char word[WORD_BYTES];

    while (port_TheatreNextWord(word, sizeof(word))) {
        if (!port_TheatreWord(word)) {
            return;
        }
        g_theatre.wait = 0;
        port_TheatreUseWord((u32)strlen(word));
        if (g_theatre.changed && (g_theatre.photo || port_ReplayIsPlaying())) {
            /* one change a look: a still after it is of the new view */
            return;
        }
    }
}

/* ---- the boundaries ---- */

/* The frame on show looked at again where its view changed or it is
 * another frame (a seek, a step back), the game waiting at a boundary. */
static void port_TheatreLookAgain(void) {
    if (!port_TheatreIsOn()) {
        return;
    }
    if (g_theatre.changed || g_theatre.looked_at != g_port_input_count + 1) {
        g_theatre.changed = FALSE;
        g_theatre.looked_at = g_port_input_count + 1;
        g_port_unseen = port_TheatreLook();
    }
}

/* Photo mode's wait: the fight steps no frame until it is left. */
static void port_PhotoWait(void) {
    port_Say(
        "theatre: photo mode: the fight waits at input %u", g_port_input_count);
    g_theatre.changed = TRUE;
    while (g_theatre.photo) {
        u32 began = SDL_GetTicks();

        port_PollWindow();
        port_TheatreMotion();
        port_TheatreRunWords();
        port_TheatreLookAgain();
        port_ShowStill();
        if (SDL_GetTicks() - began < BLANK_MS) {
            SDL_Delay(BLANK_MS - (SDL_GetTicks() - began));
        }
    }
    /* the picture of the fight as it is: the game's camera, its HUD and
     * its time again */
    g_theatre.view.hide_hud = FALSE;
    g_theatre.view.time = -1;
    g_theatre.view.free = FALSE;
    port_TheatreSetView(&g_theatre.view);
    port_TheatreSetOn(FALSE);
    port_Say("theatre: photo mode left at input %u", g_port_input_count);
}

void port_TheatreBoundary(void) {
    PortReplayStatus status;
    s32 replay;

    if (!g_theatre.started) {
        return;
    }
    port_GetReplayStatus(&status);
    /* a replay in the theatre: every picture a look, but in a film's
     * run with no view of the theatre's asked */
    replay = status.is_playing && !status.in_lead && !g_theatre.plain &&
             (!port_ExportIsOn() || g_port_window_options.theatre_view != NULL);
    if (replay != g_theatre.replay) {
        g_theatre.replay = replay;
        if (!replay) {
            /* the marks and the camera's keys were of that replay */
            g_theatre.has_in = g_theatre.has_out = FALSE;
            g_theatre.path.count = 0;
            g_theatre.path_on = FALSE;
            g_theatre.bar_held = FALSE;
        }
    }
    if (status.is_playing && !status.in_lead && port_ExportIsOn() &&
        !g_theatre.export_sought) {
        /* a clip's run: to its first frame at once (--export-from) */
        g_theatre.export_sought = TRUE;
        if (port_ExportFrom() > 0) {
            port_ReplaySeekTo(port_ExportFrom());
        }
    }
    if ((g_theatre.photo || g_theatre.photo_asked) &&
        port_PhotoRefused() != NULL) {
        g_theatre.photo = g_theatre.photo_asked = FALSE;
    }
    port_TheatreSetOn(
        g_theatre.replay || g_theatre.photo || g_theatre.photo_asked);
    if (port_TheatreActive()) {
        port_TheatreMotion();
    }
    /* the camera on its path at this frame, before a word asks of it */
    port_TheatreFollow();
    port_TheatreRunWords();
    if (g_theatre.photo_asked && port_TheatreCanLook()) {
        g_theatre.photo_asked = FALSE;
        g_theatre.photo = TRUE;
        port_PhotoWait();
    }
}

void port_TheatrePausedTick(void) {
    if (!g_theatre.started) {
        return;
    }
    port_TheatreMotion();
    port_TheatreFollow();
    port_TheatreRunWords();
    /* (a seek among the words: the camera on its path at the frame) */
    port_TheatreFollow();
    port_TheatreLookAgain();
}

/* ---- the line ---- */

s32 port_TheatreWords(char *out, u32 room) {
    char view[160];
    char fov[24];
    char keys[40];

    out[0] = 0;
    if (g_theatre.line_hidden && port_TheatreActive()) {
        return FALSE;
    }
    if (!port_TheatreActive()) {
        return TRUE;
    }
    fov[0] = 0;
    keys[0] = 0;
    if (g_theatre.view.free) {
        snprintf(fov, sizeof(fov), " %.0f deg",
                 port_TheatreFov(g_theatre.view.camera.plane));
    }
    if (g_theatre.replay && g_theatre.path.count > 0) {
        snprintf(keys, sizeof(keys), "   %d key%s%s", g_theatre.path.count,
                 g_theatre.path.count == 1 ? "" : "s",
                 port_TheatreFollows() ? "" : " (let go)");
    }
    snprintf(view, sizeof(view), "%s%s%s%s%s%s",
             g_theatre.view.free ? "   free camera" : "", fov, keys,
             g_theatre.view.hide_hud ? "   no HUD" : "",
             g_theatre.view.time >= 0 ? "   " : "",
             g_theatre.view.time >= 0 ? g_times[g_theatre.view.time + 1] : "");
    if (g_theatre.photo || g_theatre.photo_asked) {
        snprintf(out, room,
                 "Photo mode%s   |   WASD QE move  arrows/mouse turn  Z/X "
                 "near/far  N/M view  H HUD  T time  V still  Tab line  F4 "
                 "back",
                 view);
    } else {
        snprintf(out, room,
                 "%s   |   C camera  WASD QE  arrows/mouse  Z/X  N/M view  J/U "
                 "key  O path  H HUD  T time  V still  ,/PgUp/PgDn/0-9 seek  "
                 "[ ] G clip  Tab",
                 view[0] != 0 ? view + 3 : "theatre");
    }
    return TRUE;
}
