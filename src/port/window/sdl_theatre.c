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
} g_theatre;

/* ---- the view as words (--theatre-view; a clip's run) ---- */

/* free, the focus's three, the angles' three, the distance, the HUD hidden,
 * the time: ten numbers with commas */
static void port_TheatreViewWords(
    const PortTheatreView *view, char *out, u32 room) {
    snprintf(
        out, room, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", view->free,
        view->camera.focus[0], view->camera.focus[1], view->camera.focus[2],
        view->camera.angles[0], view->camera.angles[1], view->camera.angles[2],
        view->camera.distance, view->hide_hud, view->time);
}

static s32 port_TheatreReadView(const char *words, PortTheatreView *view) {
    int n[10];

    if (sscanf(words, "%d,%d,%d,%d,%d,%d,%d,%d,%d,%d", &n[0], &n[1], &n[2],
               &n[3], &n[4], &n[5], &n[6], &n[7], &n[8], &n[9]) != 10) {
        return FALSE;
    }
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
    g_theatre.view.free = TRUE;
    port_TheatreApply();
}

static double port_Radians(s32 angle) { return angle * 2.0 * PI / TURN; }

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
    zoom = held[HELD_FARTHER] - held[HELD_NEARER] +
           (g_theatre.triggers[1] - g_theatre.triggers[0]) / STICK_FULL;
    if (forward == 0 && right == 0 && up == 0 && yaw == 0 && pitch == 0 &&
        zoom == 0) {
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
    case SDL_MOUSEBUTTONUP:
        if (event->button.button == SDL_BUTTON_LEFT) {
            g_theatre.mouse_left = event->type == SDL_MOUSEBUTTONDOWN;
        } else if (event->button.button == SDL_BUTTON_RIGHT) {
            g_theatre.mouse_right = event->type == SDL_MOUSEBUTTONDOWN;
        }
        return TRUE;
    case SDL_MOUSEMOTION:
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
    case SDL_MOUSEWHEEL:
        port_TheatreFree();
        port_TheatreZoom(1.0 - event->wheel.y * WHEEL_ZOOM);
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
            g_theatre.has_in = g_theatre.has_out = FALSE;
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
    port_TheatreRunWords();
    port_TheatreLookAgain();
}

/* ---- the line ---- */

s32 port_TheatreWords(char *out, u32 room) {
    char view[128];

    out[0] = 0;
    if (g_theatre.line_hidden && port_TheatreActive()) {
        return FALSE;
    }
    if (!port_TheatreActive()) {
        return TRUE;
    }
    snprintf(view, sizeof(view), "%s%s%s%s",
             g_theatre.view.free ? "   free camera" : "",
             g_theatre.view.hide_hud ? "   no HUD" : "",
             g_theatre.view.time >= 0 ? "   " : "",
             g_theatre.view.time >= 0 ? g_times[g_theatre.view.time + 1] : "");
    if (g_theatre.photo || g_theatre.photo_asked) {
        snprintf(out, room,
                 "Photo mode%s   |   WASD QE move  arrows/mouse turn  Z/X "
                 "near/far  H HUD  T time  V still  Tab line  F4 back",
                 view);
    } else {
        snprintf(out, room,
                 "%s   |   C camera  WASD QE  arrows/mouse  Z/X  H HUD  T "
                 "time  V still  ,/PgUp/PgDn/0-9 seek  [ ] G clip  Tab",
                 view[0] != 0 ? view + 3 : "theatre");
    }
    return TRUE;
}
