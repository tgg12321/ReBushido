#ifndef PORT_CPU_H
#define PORT_CPU_H

/* The port's own CPU (src/port/cpu.c; docs/CPU.md): levels beside the
 * game's CPU, which stays as it is and stays the default. A fighter the
 * CPU plays is the port's when the run asked for a level for its place
 * (port_SetCpuLevels, port_play.h) as the game set it up (cpu_Init); it
 * is then played through a pad of the fighter's own buttons, which it
 * presses as a player would, from what a player could see of the fight,
 * after a player's time to react. Everything it keeps is in the game's
 * state, its dice too, so a fight against it replays and rolls back as
 * any other. Its memory is each fighter's: in a fight of four (port_duo.h)
 * it plays any of the four, partners and takeovers alike. */

#include "common.h"
#include "pad.h"

/* The CPUs a place can have: the game's own, or a level of the port's. */
enum {
    PORT_CPU_ORIGINAL = 0,
    PORT_CPU_NOVICE = 1,
    PORT_CPU_ADEPT = 2,
    PORT_CPU_VETERAN = 3,
    PORT_CPU_MASTER = 4,
    PORT_CPU_LEVELS = 4, /* the highest */
};

/* From cpu_Init: the fighter `fighter_index` is the CPU's; the port's
 * takes it if its place asked for a level. */
extern void port_CpuInit(s32 fighter_index);
/* TRUE if the port's CPU plays that fighter (fighter_ReadPad). */
extern s32 port_CpuPlays(s32 fighter_index);
/* The kind of pad script it is playing (enum CpuScriptKind, 1 up), or 0:
 * for --cpu-log. */
extern s32 port_CpuScriptKind(s32 fighter_index);
/* Its pad for this frame, as fighter_ReadPad gives a player's. */
extern void port_CpuBuildPad(s32 fighter_index, PadState *pad_out);

/* ---- the run's own, outside the state (src/port/cpu_run.c) ---- */

/* --cpu A[,B]: each place's CPU, a name of g_port_cpu_names or its
 * number (one for both); FALSE if it is neither. */
extern const char * const g_port_cpu_names[PORT_CPU_LEVELS + 1];
extern s32 port_CpuParseLevels(const char *text);
/* --cpu-tune LEVEL:FIELD=VALUE,...: a level's numbers (g_port_cpu_levels)
 * changed for the run, to try them; FALSE for a field that is none.
 * For tuning only: a replay does not keep it. */
extern s32 port_CpuParseTune(const char *text);
extern s32 port_CpuTune(s32 level, const char *field, s32 value);
/* --cpu-seed: mixed into each CPU's dice as it is set up (0: none). */
extern void port_CpuSetAskedSeed(u32 seed);
extern u32 port_CpuAskedSeed(void);
/* --cpu-log FILE: a line for each fighter's pad of each frame of a fight
 * (tools/port_cpu_react.py reads it). */
extern void port_CpuSetLog(const char *path);
extern void port_CpuLogPad(s32 fighter_index, const PadState *pad);

#endif /* PORT_CPU_H */
