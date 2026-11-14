/* Starting a mode, and the debug menu that starts one directly. One file of
 * the original's: the debug menu's two long format strings lie before
 * fight_StartMode's jump table, and the debug menu's jump tables a padding
 * word after it. Compiled with small data: strings of eight bytes or fewer
 * are small data, and all of a file's other strings come before its jump
 * tables. */
#include "common.h"
#include "include_asm.h"
#include "bb2.h"

/* ---- starting a mode (fight.h) ----
 * What the menus left in g_fight_config becomes the mode's first fight (or,
 * for the story, its script). */

/* Makes what was asked for the fight's own (mode, view, stage) and clears
 * what is counted over a run of fights: the continues, the wins, the
 * wounds a story's fighter shows. */
void fight_ApplyConfig(void) {
    s8 mode = g_fight_config.mode;
    s8 first_person = g_fight_config.first_person;
    s8 stage = g_fight_config.stage;
    s32 every_hit_kills = g_fight_config.every_hit_kills;

    g_fight_viewer = 0;
    g_fight_mode = mode;
    g_fight_first_person = first_person;
    g_fight_every_hit_kills = every_hit_kills;
    g_fight_stage = stage;
    model_RequestTextures(0, 0);
    model_RequestTextures(1, 0);
    g_fight_story_wounds = 0;
    g_fight_story_wounds_p2 = 0;
    g_fight_continues = 0;
    g_fight_rematch_offered = 2;
    g_fight_wins_p2 = 0;
    g_fight_wins_p1 = 0;
}

/* The names of the modes (enum FightMode), which nothing reads:
 * g_fight_mode_names points at them. The short ones are small data.
 * FAKE: "TEAM", "VS" and so on, without the padding. */
char g_str_network[8] = "NETWORK";
char g_str_team[8] = "TEAM\0\0\0";
char g_str_vs[4] = "VS\0";
char g_str_vscpu[8] = "VSCPU\0\0";
char g_str_story[8] = "STORY\0\0";
const char g_str_chanbara[] = "CHANBARA";
const char g_str_practice[] = "PRACTICE";

/* Starts the mode in g_fight_config. Every mode ends by choosing the next
 * screen, here or in what it calls: most go straight to the fight, story
 * mode runs its script up to the first thing to show. */
void fight_StartMode(void) {
    fight_ApplyConfig();

    switch (g_fight_mode) {
    case FIGHT_MODE_NETWORK:
        /* each console follows its own fighter */
        g_fight_config.first_person = 1;
        g_disp_loading_screen = LOADING_SCREEN_PLAIN;
        g_sys_screen = SCREEN_FIGHT_INIT;
        g_fight_viewer = (g_link_side != 0);
        break;
    case FIGHT_MODE_STORY:
        story_Start(g_fighter_roster_characters[g_fight_config.roster[0]]);
        g_fight_config.roster[1] = 0;
        story_RunScript();
        cpu_ForgetAttacks(1);
        break;
    case FIGHT_MODE_VSCPU:
        g_fight_config.is_cpu[1] = 1;
        fight_BuildVsCpuOrder();
        g_disp_loading_screen = LOADING_SCREEN_PLAIN;
        fight_NextVsCpuOpponent();
        break;
    case FIGHT_MODE_CHANBARA:
        g_fight_config.is_cpu[1] = 1;
        g_fight_config.weapon[1] = WEAPON_ID_KATANA;
        g_fight_config.weapon[0] = WEAPON_ID_KATANA;
        g_chanbara_count = 0;
        g_fight_start_pair = 0;
        g_chanbara_time = 0;
        g_chanbara_area_changed = 0;
        g_fight_stage = STAGE_CHANBARA;
        fight_NextChanbaraOpponent();
        g_disp_loading_screen = LOADING_SCREEN_PLAIN;
        g_sys_screen = SCREEN_FIGHT_INIT;
        break;
    case FIGHT_MODE_TEAM:
        g_fight_stage = (g_team_clan_bits & 1) ? STAGE_TEAM_2 : STAGE_TEAM_1;
        g_team_match = 0;
        disp_Reset();
        eff_Init();
        fight_NextTeamMatch();
        break;
    case FIGHT_MODE_PRACTICE:
        /* practice against the partner opens on the pause menu, two
         * frames in */
        g_fight_auto_pause_countdown = (g_fight_practice_round == 0) << 1;
        g_fight_stage =
            (g_fight_practice_round != 0) ? STAGE_PRACTICE_2 : STAGE_PRACTICE_1;
        g_fight_config.is_cpu[1] = 1;
        if (g_fight_practice_round == 0) {
            /* the partner is the henchman of the player's clan */
            g_fight_config.roster[1] =
                g_fighter_clans
                        [g_fighter_roster_characters[g_fight_config.roster[0]]]
                    ? ROSTER_SIDEARM_1
                    : ROSTER_SIDEARM_2;
        }
        g_sys_screen = SCREEN_FIGHT_INIT;
        break;
    case FIGHT_MODE_VS:
        g_disp_loading_screen = LOADING_SCREEN_PLAIN;
        g_sys_screen = SCREEN_FIGHT_INIT;
        break;
    default:
        g_sys_screen = SCREEN_FIGHT_INIT;
        break;
    }
    if (g_fight_config.is_cpu[1]) {
        cpu_ForgetAttacks(1);
    }
}

/* ---- the debug menu (SCREEN_DEBUG_MENU) ----
 * A page of numbers in the built-in font from which a fight is set up
 * directly, with no character select. Each pad has a cursor; up and down
 * move it, left and right change the number it is on, Start begins the
 * mode. The first pad's column is the whole setup; the second pad's has
 * only the second fighter's rows. */

/* The rows. The first four are in both columns, one fighter each. The
 * letters are the row's label on the screen. */
enum MenuDebugRow {
    DEBUG_ROW_ROSTER = 0,
    DEBUG_ROW_WEAPON = 1,
    DEBUG_ROW_CPU = 2,       /* "C": played by the CPU */
    DEBUG_ROW_CPU_LEVEL = 3, /* "CL" */
    DEBUG_ROW_STAGE = 4,
    DEBUG_ROW_MODE = 5,         /* enum FightMode */
    DEBUG_ROW_FIRST_PERSON = 6, /* "O" */
    DEBUG_ROW_INNER_HITS = 7,   /* "CR": every hit counts as on the bone */
    DEBUG_ROW_GRAYSCALE = 8,    /* "CO": the saved "Monitor" option */
    DEBUG_ROW_EASY_CPU = 9,     /* "L": the saved "Difficulty" option */
    DEBUG_ROW_M = 10,           /* "M": 0 to 3; nothing reads it */
    DEBUG_ROW_FIGHT_VIEW = 11,  /* "D": g_fight_debug_view */
    DEBUG_ROWS,
};

/* The second pad's last row. */
#define DEBUG_ROW_LAST_2 DEBUG_ROW_CPU_LEVEL

/* How many there are of what the rows count through. */
#define DEBUG_WEAPONS 8
#define DEBUG_STAGES 38
#define DEBUG_MODES 7
/* A step of the CPU's level, which is shown in whole levels (the high
 * byte). */
#define DEBUG_CPU_LEVEL_STEP 0x80

/* The sound of a number changing (the one after the four MENU_SOUND_...),
 * and how loud it is played. */
#define DEBUG_SOUND_CHANGE 4
#define DEBUG_SOUND_VOLUME 0x3F

/* Each pad's row. */
static s16 g_menu_debug_cursors[2] = {0, 0};

/* SCREEN_DEBUG_MENU_UPDATE: prints the page, each row in the cursor's colour
 * if a cursor is on it, then moves the cursors and changes the numbers,
 * keeping each in its range. */
void menu_DebugUpdate(void) {
    s32 i;
    u8 *plain;
    u8 *lit;

    g_sys_screen_frames++;
    rand();
    plain = "~c777";
    lit = "~c737";
    font_Printf(
        "%s%5d",
        (s32)(g_menu_debug_cursors[0] == DEBUG_ROW_ROSTER ? lit : plain),
        g_fight_config.roster[0]);
    font_Printf(
        "%s%3d\n",
        (s32)(g_menu_debug_cursors[1] == DEBUG_ROW_ROSTER ? lit : plain),
        g_fight_config.roster[1]);
    font_Printf(
        "%s%5d",
        (s32)(g_menu_debug_cursors[0] == DEBUG_ROW_WEAPON ? lit : plain),
        g_fight_config.weapon[0]);
    font_Printf(
        "%s%3d\n",
        (s32)(g_menu_debug_cursors[1] == DEBUG_ROW_WEAPON ? lit : plain),
        g_fight_config.weapon[1]);
    font_Printf(
        "%sC%4d", (s32)(g_menu_debug_cursors[0] == DEBUG_ROW_CPU ? lit : plain),
        g_fight_config.is_cpu[0]);
    font_Printf("%s%3d\n",
                (s32)(g_menu_debug_cursors[1] == DEBUG_ROW_CPU ? lit : plain),
                g_fight_config.is_cpu[1]);
    font_Printf(
        "%sCL%3x",
        (s32)(g_menu_debug_cursors[0] == DEBUG_ROW_CPU_LEVEL ? lit : plain),
        g_fight_config.cpu_levels[0] >> 8);
    font_Printf(
        "%s%3x\n",
        (s32)(g_menu_debug_cursors[1] == DEBUG_ROW_CPU_LEVEL ? lit : plain),
        g_fight_config.cpu_levels[1] >> 8);
    font_Printf("%s%5d\n",
                (s32)(g_menu_debug_cursors[0] == DEBUG_ROW_STAGE ? lit : plain),
                g_fight_config.stage);
    font_Printf("%s%5d\n",
                (s32)(g_menu_debug_cursors[0] == DEBUG_ROW_MODE ? lit : plain),
                g_fight_config.mode);
    font_Printf(
        "%sO%4d\n",
        (s32)(g_menu_debug_cursors[0] == DEBUG_ROW_FIRST_PERSON ? lit : plain),
        g_fight_config.first_person);
    font_Printf(
        "%sCR%3d\n",
        (s32)(g_menu_debug_cursors[0] == DEBUG_ROW_INNER_HITS ? lit : plain),
        g_fight_config.every_hit_kills);
    font_Printf(
        "%sCO%3d\n",
        (s32)(g_menu_debug_cursors[0] == DEBUG_ROW_GRAYSCALE ? lit : plain),
        g_saved_record.flags & SAVE_FLAG_GRAYSCALE);
    font_Printf(
        "%sL%4d\n",
        (s32)(g_menu_debug_cursors[0] == DEBUG_ROW_EASY_CPU ? lit : plain),
        (g_saved_record.flags >> 1) & 1);
    font_Printf(
        "%sM%4d\n", (s32)(g_menu_debug_cursors[0] == DEBUG_ROW_M ? lit : plain),
        g_menu_debug_m_option);
    font_Printf(
        "%sD%4d\n",
        (s32)(g_menu_debug_cursors[0] == DEBUG_ROW_FIGHT_VIEW ? lit : plain),
        g_fight_debug_view);

    for (i = 0; i < 2; i++) {
        if (g_pad_state.pressed & PAD_OF(PADLup, i)) {
            snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
            if (g_menu_debug_cursors[i] > 0) {
                g_menu_debug_cursors[i]--;
            } else {
                g_menu_debug_cursors[i] =
                    (i != 0) ? DEBUG_ROW_LAST_2 : DEBUG_ROWS - 1;
            }
        } else if (g_pad_state.pressed & PAD_OF(PADLdown, i)) {
            snd_Play(MENU_SOUND_CURSOR, SND_VOLUME_MAX, SND_VOLUME_MAX);
            if (g_menu_debug_cursors[i] <
                ((i != 0) ? DEBUG_ROW_LAST_2 : DEBUG_ROWS - 1)) {
                g_menu_debug_cursors[i]++;
            } else {
                g_menu_debug_cursors[i] = 0;
            }
        } else if (g_pad_state.pressed & PAD_OF(PADLleft, i)) {
            snd_Play(
                DEBUG_SOUND_CHANGE, DEBUG_SOUND_VOLUME, DEBUG_SOUND_VOLUME);
            switch (g_menu_debug_cursors[i]) {
            case DEBUG_ROW_ROSTER:
                g_fight_config.roster[i]--;
                break;
            case DEBUG_ROW_WEAPON:
                g_fight_config.weapon[i]--;
                break;
            case DEBUG_ROW_CPU:
                g_fight_config.is_cpu[i]--;
                break;
            case DEBUG_ROW_CPU_LEVEL:
                g_fight_config.cpu_levels[i] -= DEBUG_CPU_LEVEL_STEP;
                break;
            case DEBUG_ROW_STAGE:
                g_fight_config.stage--;
                break;
            case DEBUG_ROW_MODE:
                g_fight_config.mode--;
                break;
            case DEBUG_ROW_FIRST_PERSON:
                g_fight_config.first_person--;
                break;
            case DEBUG_ROW_INNER_HITS:
                g_fight_config.every_hit_kills--;
                break;
            case DEBUG_ROW_GRAYSCALE:
                g_saved_record.flags ^= SAVE_FLAG_GRAYSCALE;
                break;
            case DEBUG_ROW_EASY_CPU:
                g_saved_record.flags ^= SAVE_FLAG_EASY_CPU;
                break;
            case DEBUG_ROW_M:
                g_menu_debug_m_option--;
                break;
            case DEBUG_ROW_FIGHT_VIEW:
                g_fight_debug_view--;
                break;
            }
        } else if (g_pad_state.pressed & PAD_OF(PADLright, i)) {
            snd_Play(
                DEBUG_SOUND_CHANGE, DEBUG_SOUND_VOLUME, DEBUG_SOUND_VOLUME);
            switch (g_menu_debug_cursors[i]) {
            case DEBUG_ROW_ROSTER:
                g_fight_config.roster[i]++;
                break;
            case DEBUG_ROW_WEAPON:
                g_fight_config.weapon[i]++;
                break;
            case DEBUG_ROW_CPU:
                g_fight_config.is_cpu[i]++;
                break;
            case DEBUG_ROW_CPU_LEVEL:
                g_fight_config.cpu_levels[i] += DEBUG_CPU_LEVEL_STEP;
                break;
            case DEBUG_ROW_STAGE:
                g_fight_config.stage++;
                break;
            case DEBUG_ROW_MODE:
                g_fight_config.mode++;
                break;
            case DEBUG_ROW_FIRST_PERSON:
                g_fight_config.first_person++;
                break;
            case DEBUG_ROW_INNER_HITS:
                g_fight_config.every_hit_kills++;
                break;
            case DEBUG_ROW_GRAYSCALE:
                g_saved_record.flags ^= SAVE_FLAG_GRAYSCALE;
                break;
            case DEBUG_ROW_EASY_CPU:
                g_saved_record.flags ^= SAVE_FLAG_EASY_CPU;
                break;
            case DEBUG_ROW_M:
                g_menu_debug_m_option++;
                break;
            case DEBUG_ROW_FIGHT_VIEW:
                g_fight_debug_view++;
                break;
            }
        }
        g_fight_config.roster[i] =
            (g_fight_config.roster[i] + ROSTER_COUNT) % ROSTER_COUNT;
        g_fight_config.weapon[i] =
            (g_fight_config.weapon[i] + DEBUG_WEAPONS) % DEBUG_WEAPONS;
        g_fight_config.is_cpu[i] = (g_fight_config.is_cpu[i] + 2) % 2;
    }
    g_fight_config.stage = (g_fight_config.stage + DEBUG_STAGES) % DEBUG_STAGES;
    g_fight_config.mode = (g_fight_config.mode + DEBUG_MODES) % DEBUG_MODES;
    g_fight_config.first_person &= 1;
    g_fight_config.every_hit_kills &= 1;
    g_menu_debug_m_option = (g_menu_debug_m_option + 4) % 4;
    g_fight_debug_view &= 1;
    if (g_pad_state.pressed & PAD_ANY(PADstart)) {
        snd_Play(MENU_SOUND_CONFIRM, SND_VOLUME_MAX, SND_VOLUME_MAX);
        fight_StartMode();
    }
}
