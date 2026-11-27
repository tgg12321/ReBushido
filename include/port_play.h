#ifndef PORT_PLAY_H
#define PORT_PLAY_H

/* Two gameplay switches of the native port (docs/PORT.md, "Every stage in
 * every mode, and every unlock"; docs/PLAYING.md):
 *
 * - stage_select_everywhere: versus the CPU and training go on from the
 *   crossed weapons to the game's own stage pages, as versus mode does,
 *   and fight on the stage chosen there (with its kind of stage and the
 *   arenas' page: port_stage_choice.h) in place of the stage the mode
 *   would have picked. The first pad drives the pages alone.
 * - unlock_all: wherever the game reads the progress its save keeps (the
 *   fighters won, the stories finished), it reads all of it. The save
 *   itself (g_saved_record) is never changed by it: a save writes the
 *   player's own progress, and what a run wins is still won.
 *
 * What the run asks for -- the window's two settings, or --stage-select
 * and --unlock-all -- is kept in src/port/play_switches.c, outside the state;
 * the game's step takes it in when it next asks, into g_port_play, which
 * is the game's state (src/port/play.c), so a change reaches the state at
 * a frame of the game and a rollback puts back what the game had taken.
 * In a session of netplay the host's two hold on both machines
 * (src/port/session.c). The PlayStation builds never see this header. */

#include "common.h"

typedef struct {
    u8 unlock_all;  /* the run's unlock_all, as the game last took it in */
    u8 stage_pages; /* the run's stage_select_everywhere, likewise */
    /* this mode's select screen went on to the stage pages: its fights
     * are on the stage chosen there (g_fight_config.stage) */
    u8 page_used;
    u8 is_started;
    /* the unlock mask the game was last handed (port_PlayUnlocks), for the
     * play tests */
    s32 unlocks;
} PortPlay;

extern PortPlay g_port_play;

/* Every unlock bit the save can hold (SaveRecord.unlock_mask): the twenty
 * fighters with a name and the two clans' last fighters. */
#define PORT_PLAY_ALL_UNLOCKS 0x053FF3FF
/* Every story finished (SaveRecord.staff_roll_character_mask): the slash
 * mode's row of the top menu. */
#define PORT_PLAY_ALL_STAFF_ROLLS 0x3F

/* ---- what the game calls (under RB_PORT) ---- */

/* The save's unlock mask as the game is to read it: `saved`, or with
 * unlock_all every unlock. */
extern s32 port_PlayUnlocks(s32 saved);
/* The save's stories finished as the game is to read them. */
extern s32 port_PlayStaffRolls(s32 saved);
/* At the crossed weapons of a one-player mode's select screen (select.c,
 * select_WeaponsOutStep): TRUE if it goes on to the stage pages, as
 * versus the CPU and training do with stage_select_everywhere (versus and
 * link mode always do, and do not ask). Notes that the mode's fights are
 * on the stage chosen there. */
extern s32 port_PlayGoesToStages(s32 mode);
/* The select screen loads (select_Load): no stage chosen yet. */
extern void port_PlayReset(void);
/* TRUE if `mode` is versus the CPU or training and its fights are on the
 * stage its stage pages chose, not the stage the mode picks itself
 * (fight_StartMode, fight_NextVsCpuOpponent, port_StageChoiceAtLoad). */
extern s32 port_PlayStageChosen(s32 mode);

/* ---- the run's switches, outside the state (src/port/play_switches.c)
 * ---- */

/* Set by the window's settings (as the program starts and as the settings
 * menu changes them) and by --unlock-all and --stage-select; read by the
 * game's step as it needs them. */
extern void port_SetUnlockAll(s32 on);
extern void port_SetStageSelectEverywhere(s32 on);
extern s32 port_GetUnlockAll(void);
extern s32 port_GetStageSelectEverywhere(void);
/* A session holds them all to the host's (src/port/session.c), these two
 * and port_top_menu.h's: the setters change nothing after. `bits` as
 * port_PlaySwitchBits makes. */
extern void port_HoldPlaySwitches(u32 bits);
/* The player's own switches and fight music kept before a session begun
 * in the running program holds the host's, and put back after it
 * (online.c). */
extern void port_KeepPlaySwitches(void);
extern void port_RestorePlaySwitches(void);
/* The switches as a word for the session's welcome: PORT_PLAY_BIT_...,
 * and the game's options (port_GetGameOptions) from
 * PORT_PLAY_OPTION_SHIFT up. */
extern u32 port_PlaySwitchBits(void);
/* The CPU that plays a CPU's fighter in each place, the first fighter's
 * and the second's (port_cpu.h): PORT_CPU_ORIGINAL, the game's own, or one
 * of the port's levels. The window's cpu setting gives both the same;
 * --cpu each its own. Taken in as each CPU is set up (cpu_Init). */
extern void port_SetCpuLevels(s32 first, s32 second);
extern s32 port_GetCpuLevel(s32 place);

#define PORT_PLAY_BIT_UNLOCK_ALL 1
#define PORT_PLAY_BIT_STAGE_PAGES 2
#define PORT_PLAY_BIT_TOP_MENU 4
#define PORT_PLAY_BIT_BUGFIXES 8
#define PORT_PLAY_OPTION_SHIFT 4
#define PORT_PLAY_OPTION_MASK 7
/* the port's CPU of each place (port_SetCpuLevels), three bits each */
#define PORT_PLAY_CPU_SHIFT 8
#define PORT_PLAY_CPU_BITS 3
#define PORT_PLAY_CPU_MASK 7

#endif /* PORT_PLAY_H */
