#ifndef PORT_REMIX_H
#define PORT_REMIX_H

/* The sound remastered (src/port/remix.c; docs/PORT.md, "The sound
 * remastered"; ROADMAP 32): beside the sound chip's own samples
 * (src/port/spu.c), which stay what they are, a second mix of the same
 * voices for the player's ears alone:
 *
 *   - each voice's waveform played at its pitch through a windowed sinc
 *     of 32 to 64 taps, in place of the chip's four-point table;
 *   - a reverb of the stage's own (a network of eight delay lines, its
 *     room chosen by the stage: the dojo, the cave, the open);
 *   - with `headphones`, each voice placed by its left and right volumes
 *     as a head would hear it: the far ear later and its highs shaded
 *     by the head, and the disc's music as two speakers in front.
 *
 * Nothing here is the game's state (config/port_state.txt: outside) and
 * nothing of the game reads it: the chip calls in with what it has
 * worked out, and the mix goes to the sound card and --wav in place of
 * the chip's. Whole numbers throughout, as the chip is: no floating
 * point, so no denormal and no NaN. */

#include "common.h"

/* The setting `sound` (src/port/window/settings_list.h; --sound): the
 * chip's own sound, or the sound remastered. */
#define PORT_SOUND_ORIGINAL 0
#define PORT_SOUND_REMASTERED 1
extern void port_SetSoundRemastered(s32 is_on);
extern s32 port_IsSoundRemastered(void);
/* The setting `headphones` (--headphones): the remastered sound placed
 * for headphones. It changes nothing of the original sound. */
extern void port_SetSoundHeadphones(s32 is_on);
extern s32 port_IsSoundHeadphones(void);

/* ---- what the chip calls (src/port/spu.c) ---- */

/* Set while port_RunSpuRemastered makes a remastered mix: the chip then
 * hands each sample its voices take from their waveforms to
 * port_RemixPush. */
extern s32 g_port_remix_live;
/* The mix's start for a run of samples: the room is chosen again by
 * where the game is, `stage` the fight's stage (enum StageIndex) or -1
 * out of a fight (sound.c). */
extern void port_RemixBegin(s32 stage);
/* Where sound.c has the chip make the remastered samples: room for a
 * blank's of them, and more. */
#define PORT_REMIX_MOST_FRAMES 1024
extern s16 g_port_remix_out[PORT_REMIX_MOST_FRAMES * 2];
/* A sample voice `voice` has just taken from its waveform, under its
 * envelope. */
extern void port_RemixPush(u32 voice, s32 sample);
/* Voice `voice`'s part of the next sample: where it is between its
 * waveform's samples (`counter`, 12 bits of fraction) and how fast it
 * goes (`step`, 0x1000 the waveform as recorded); if `noise` it plays the
 * chip's noise, whose sample under its envelope is `noise_sample`;
 * `volume` its left and right volumes as the chip has them now. */
extern void port_RemixVoice(u32 voice, u32 counter, u32 step, s32 noise,
                            s32 noise_sample, const s32 *volume);
/* The next sample, out of the voices given since the last: `cd` the
 * disc's sound (or a mod's music) at the chip's volume for it, and
 * whether the chip's reverb hears it; `chip_wet` the chip's own reverb,
 * heard where the room has none of its own; the main volume; whether the
 * chip is unmuted. Writes a left and a right into `out`. */
extern void port_RemixFrame(const s32 *cd, const s32 *chip_wet,
                            const s32 *main_volume, s32 unmuted, s16 *out);

/* A rollback's companion (netplay.c's saves, state.c's self-test): the
 * mix's running state (its copies of the voices, the head's, the
 * limiter's, the reverb's lines) kept beside the game's state saved at a
 * boundary, in place `place` of PORT_REMIX_KEEPS, and put back with it,
 * so that the steps run again from there are mixed from where the mix
 * was then, and the reverb does not hear them twice (a tail doubled).
 * Still not the game's state: nothing of the game reads it. Nothing
 * where the setting is off; a place never kept is not put back. */
#define PORT_REMIX_KEEPS 16
extern void port_RemixKeep(u32 place);
extern void port_RemixPutBack(u32 place);

/* What the remastered mix did, for --wav's summary. */
typedef struct {
    u32 samples;
    u32 clipped; /* samples beyond 16 bits, cut to them */
    u32 limited; /* samples the limiter turned down further */
    u32 room;    /* the room heard last (PORT_REMIX_ROOM_...) */
    u32 rooms;   /* how many times the room changed */
} PortRemixCounts;

extern const PortRemixCounts *port_GetRemixCounts(void);
/* The room's name, for a run's output. */
extern const char *port_RemixRoomName(u32 room);
/* The room a stage of the game has (enum StageIndex). */
extern u32 port_RemixStageRoom(s32 stage);

/* ---- for the tests (tests/port/remix.c) ---- */

/* The resampler's bands: the fastest step each serves and its taps. */
#define PORT_REMIX_BANDS 7
#define PORT_REMIX_PHASES 256
#define PORT_REMIX_COEF_ONE 16384
/* The kernel's row for `step` at `phase` (0 to PORT_REMIX_PHASES), and
 * its taps. */
extern const s16 *port_RemixKernel(u32 step, u32 phase, u32 *taps);
/* The reverb alone: room `room` set at once, the lines cleared; then one
 * sample in, a left and a right out (before the room's wet level, in
 * 256ths of a sample). */
extern void port_RemixReverbReset(u32 room);
extern void port_RemixReverbRun(s32 in_left, s32 in_right, s32 *out);
/* The room's decay at low frequencies, in milliseconds, and its wet level
 * (of 32768). */
extern u32 port_RemixRoomDecay(u32 room);
extern u32 port_RemixRoomWet(u32 room);
/* The head alone: a source placed by its volumes (`volume`, a left and a
 * right), one sample in, the two ears out. Source 0 to 23 are the
 * voices, 24 and 25 the disc's left and right. */
#define PORT_REMIX_SOURCES 26
extern void port_RemixPlaceReset(void);
extern void port_RemixPlaceRun(
    u32 source, s32 sample, const s32 *volume, s32 *ears);
/* The far ear's delay for a source panned all the way, in 256ths of a
 * sample. */
extern s32 port_RemixMostDelay(void);
/* The resampler's kernels and the head's table made, once, in memory
 * the system gives: as the setting is turned on, or by the tests. 0 if
 * there was no memory (the setting then stays off). */
extern s32 port_RemixPrepare(void);

/* The rooms (remix.c's table, docs/PORT.md). */
enum {
    PORT_REMIX_ROOM_NONE,
    PORT_REMIX_ROOM_OPEN,
    PORT_REMIX_ROOM_FIELD,
    PORT_REMIX_ROOM_FOREST,
    PORT_REMIX_ROOM_BAMBOO,
    PORT_REMIX_ROOM_COURTYARD,
    PORT_REMIX_ROOM_VALLEY,
    PORT_REMIX_ROOM_COAST,
    PORT_REMIX_ROOM_ROOFTOP,
    PORT_REMIX_ROOM_UNDERPASS,
    PORT_REMIX_ROOM_YARD,
    PORT_REMIX_ROOM_DOJO,
    PORT_REMIX_ROOM_HALL,
    PORT_REMIX_ROOM_THEATRE,
    PORT_REMIX_ROOM_SHIP,
    PORT_REMIX_ROOM_STATION,
    PORT_REMIX_ROOM_GARAGE,
    PORT_REMIX_ROOM_DAM,
    PORT_REMIX_ROOM_TUNNEL,
    PORT_REMIX_ROOM_CAVE,
    PORT_REMIX_ROOMS
};

#endif /* PORT_REMIX_H */
