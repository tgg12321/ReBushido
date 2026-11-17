#ifndef PORT_MUSIC_H
#define PORT_MUSIC_H

/* The native port's music mods (src/port/music.c; docs/MODS.md, "Music").
 * All the game's music is XA sound the drive plays off the disc: a piece
 * is a stream (a channel) of a file. A mod's `music/KEY.ogg` or `.wav`
 * stands in for the piece whose key is KEY, the file's name and the
 * stream's number (`bb2_m1-7`, the top menu's). It is heard where the
 * game's would be: started, stopped, faded and set loud as the game
 * starts, stops, fades and sets the disc's sound, which the game goes on
 * playing as before. Only what is heard changes: at the mix, the disc's
 * sound gives way to the mod's. Nothing here is the game's state; a step
 * run again quietly hears nothing and moves nothing here. */

#include "common.h"

/* What a piece of music is, as it is opened. */
typedef struct {
    u32 rate;     /* samples a second */
    u32 channels; /* 1 or 2 are used; a third and on are left out */
    u32 frames;   /* how long, in samples of each channel */
    /* It loops: from loop_end it goes on at loop_start (in samples of
     * each channel, loop_end the first not played before it goes back). */
    s32 loops;
    u32 loop_start;
    u32 loop_end;
    s32 problem; /* the decoder's number for what was wrong */
} PortMusicFormat;

/* ---- the run's music ---- */

/* The setting `music_mods`: 0 plays the game's own music even where a mod
 * has some, from the next sample on. On if nothing says. */
extern void port_MusicSetOn(s32 is_on);
/* `--dump-music DIR`: each piece the drive plays is written there as it
 * plays, `KEY.wav`, the drive's own sound before its volumes, at the
 * chip's 44,100 samples a second. NULL for none. */
extern void port_MusicSetDumpFolder(const char *folder);
/* As the mods have been read (port_ModsStart): says what music they
 * give, and starts the dump. */
extern void port_MusicStart(void);
/* The next `frames` samples of the mod's music in place of the disc's
 * sound, at the drive's volumes as the disc's sound gets them, a left
 * and a right each: NULL where the disc's sound is heard as it is (no
 * mod's piece is playing, the setting is off, or the step is run again).
 * Where it is not NULL, `*heard` is room for the samples heard with it
 * (port_RunSpu's `heard_out`), which is none of the game's state.
 * Called before the samples are mixed (src/port/sound.c). */
extern const s16 *port_PlayMusic(u32 frames, s16 **heard);

/* ---- what is worked out (tests/port/music.c) ---- */

/* The key of stream `channel` of the disc's file `name` (`BB2_M1.XA`, its
 * version `;1` too): the name in small letters without its ending, a
 * dash and the channel (`bb2_m1-7`); the name alone for a channel of -1,
 * a stream that is not chosen by channel. Needs PORT_MOD_KEY_SIZE bytes. */
extern void port_MusicKey(const char *name, s32 channel, char *key, u32 room);
/* A tag of an Ogg file, `NAME=VALUE`: LOOPSTART, LOOPLENGTH and LOOPEND
 * (the sample the loop goes back from) set `format`'s loop. 1 if it was
 * one of them. */
extern s32 port_MusicReadTag(const char *tag, PortMusicFormat *format);
/* A WAV file's first `size` bytes (the whole of its header): its format,
 * and where its samples are and how many bytes each frame of them is.
 * Plain whole numbers of 8, 16, 24 or 32 bits; the first loop of its
 * `smpl` chunk is its loop. 0 if it is not such a file. */
extern s32 port_MusicReadWav(const u8 *bytes, u32 size, PortMusicFormat *format,
                             u32 *data_at, u32 *frame_bytes, u32 *bits);
/* Where sample `frame` of the chip's output falls in a piece of `rate`
 * samples a second: the piece's sample, and how far past it in 44,100ths
 * of one. Up to 96,000 a second. */
extern u32 port_MusicSourceAt(u32 frame, u32 rate, u32 *fraction);
/* A sample of the piece, `frame`, as its loop takes it: where it is read
 * from, or `format->frames` (or more) past the end of a piece that does
 * not loop. */
extern u32 port_MusicLoopFrame(const PortMusicFormat *format, u32 frame);

/* ---- the Ogg decoder (src/port/vorbis.c, stb_vorbis) ---- */

/* Opens the Ogg Vorbis file of `size` bytes at `data`, which must stay
 * while it is open; `work` is all the memory the decoder has. NULL, with
 * `format->problem` set, if it cannot. */
extern void *port_OggOpen(
    const u8 *data, u32 size, u8 *work, u32 work_size, PortMusicFormat *format);
/* The file's tag `index` (`NAME=VALUE`), NULL past the last. */
extern const char *port_OggTag(void *ogg, u32 index);
/* Up to `frames` samples, a left and a right each (one channel is given
 * to both): how many there were, 0 at the end. */
extern u32 port_OggRead(void *ogg, s16 *out, u32 frames);
/* The next read begins at sample `frame`: 0 if it cannot. */
extern s32 port_OggSeek(void *ogg, u32 frame);
extern void port_OggClose(void *ogg);

#endif /* PORT_MUSIC_H */
