#ifndef PORT_FILM_H
#define PORT_FILM_H

/* A replay as a film (docs/PORT.md, "A replay as a film"): the window's
 * pictures and the game's sound written as a video file. Only the build
 * with a window has these files: film.c (what a film is, and the AVI),
 * film_jpeg.c (the AVI's pictures), film_mf.c (the MP4, by Windows' own
 * Media Foundation) and sdl_export.c (a replay played into one).
 *
 * A film's frame is a vertical blank of the console's, 4,500,000 / 75,218
 * a second (59.826: NTSC's 263 lines of 286 cycles of 4.5 MHz), against
 * which the sound chip's 44,100 samples a second are counted (sound.c):
 * every blank is a frame and its sound is the samples of its time, so the
 * two never drift apart. Two kinds:
 *
 *   MP4   H.264 (High) and AAC, by the encoders every Windows 10 and 11
 *         has (Media Foundation); what Windows, the browsers and Discord
 *         play as they are
 *   AVI   Motion JPEG and the samples as they are (PCM), written by the
 *         port itself: on Linux, and where Windows has no encoder (its
 *         N editions); VLC, mpv and ffmpeg play it */

#include "common.h"

#define PORT_FILM_RATE_NUMERATOR 4500000
#define PORT_FILM_RATE_DENOMINATOR 75218
#define PORT_FILM_SAMPLE_RATE 44100
#define PORT_FILM_CHANNELS 2
#define PORT_FILM_WHY_BYTES 200
/* a film's size where nothing says another */
#define PORT_FILM_WIDTH 1920
#define PORT_FILM_HEIGHT 1080

enum {
    PORT_FILM_NONE = -1,
    PORT_FILM_MP4 = 0,
    PORT_FILM_AVI = 1,
};

typedef struct PortFilm PortFilm;

/* The kind a path's ending names (.mp4, .avi), or PORT_FILM_NONE. */
extern s32 port_FilmKindOf(const char *path);
/* Whether this program can make a film of `kind` here: the MP4 needs
 * Media Foundation. */
extern s32 port_FilmCanMake(s32 kind);
/* A new film of `w` by `h` pictures (both even) at `path`: NULL, with the
 * reason in `why`, where it cannot be made. */
extern PortFilm *port_FilmOpen(
    const char *path, s32 kind, s32 w, s32 h, char *why, u32 room);
/* The next frame: `w` by `h` pixels 0xFFRRGGBB, a row after a row from
 * the top. FALSE where it could not be written. */
extern s32 port_FilmPicture(PortFilm *film, const u32 *pixels);
/* The next samples, two channels of 16 bits a frame. */
extern s32 port_FilmSound(PortFilm *film, const s16 *samples, u32 frames);
/* The film's pictures and samples so far. */
extern u32 port_FilmPictures(const PortFilm *film);
extern u64 port_FilmSamples(const PortFilm *film);
/* The film finished and closed: FALSE, with the reason, where it could
 * not be. `keep` FALSE: closed and taken away (an export stopped). */
extern s32 port_FilmClose(PortFilm *film, s32 keep, char *why, u32 room);

/* For a test (tools/port_film.py): a film read back, each of the frames
 * `at` lists ("10,200,1500") into `folder` as frame-NNNNNN.raw (the
 * form of --tap-window's files) and its sound as sound.wav, and a line
 * a frame of when it is, in frames.txt. TRUE where it could be read. */
extern s32 port_FilmRead(
    const char *path, const char *folder, const char *at, char *why, u32 room);

/* ---- between the files ---- */

/* film_jpeg.c: a picture as a baseline JPEG of `quality` (1 to 100), into
 * `out`: its bytes, or 0 where `room` is too small. */
extern u32 port_JpegEncode(
    const u32 *pixels, s32 width, s32 height, s32 quality, u8 *out, u32 room);

/* film_mf.c: the MP4 by Media Foundation, on Windows; elsewhere none. */
typedef struct PortMf PortMf;
extern s32 port_MfCan(void);
extern PortMf *port_MfOpen(const char *path, s32 w, s32 h, char *why, u32 room);
extern s32 port_MfPicture(PortMf *mf, const u32 *pixels, u32 index);
extern s32 port_MfSound(PortMf *mf, const s16 *samples, u32 frames, u64 at);
extern s32 port_MfClose(PortMf *mf, char *why, u32 room);
/* ... and read back for a test, as port_FilmRead reads */
extern s32 port_MfRead(const char *path, const char *folder, const u8 *wanted,
                       u32 wanted_count, char *why, u32 room);
/* film.c's, which port_MfRead writes with */
extern s32 port_FilmWriteRaw(
    const char *folder, u32 index, const u32 *pixels, s32 w, s32 h);
extern s32 port_FilmWriteWav(const char *path, const s16 *samples, u64 frames);

#endif /* PORT_FILM_H */
