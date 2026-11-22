/* Story mode (story.h): the script and what runs it, the fighters and
 * weapons it has loaded, and its two screens, the stage card and the
 * cutscene. The scripts themselves end the file. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include <psxsdk/inline_o.h>
#include <psxsdk/gtemac.h>
#include "bb2.h"

/* Defined further down. */
static u8 *g_story_scripts[32];

/* Looks ahead in the script, past whatever sets up fights and plays
 * cutscenes, for the next chapter, and loads the stage card that leads
 * into it; there is none before the end and before the victim. The stage
 * that is being left is marked on the route. */
static void story_LoadNextStageCard(u8 *script) {
    s32 *load_addr = (s32 *)SYS_STAGING_ADDR;
    s32 next_stage = -1;
    s32 done = 0;

    do {
        s32 command = *script++;
        switch (command) {
        case STORY_STAGE:
            next_stage = *script;
        case STORY_END:
        case STORY_VICTIM:
            done = 1;
            break;
        case STORY_HENCHMEN:
            script += STORY_HENCHMEN_SIZE;
            break;
        case STORY_OPPONENT:
            script += STORY_OPPONENT_SIZE;
            break;
        case STORY_SECOND_FIGHTER:
            script += 2;
            break;
        case STORY_CUTSCENE:
            script += STORY_CUTSCENE_SIZE;
            break;
        case STORY_INTERLUDE:
            script += STORY_INTERLUDE_SIZE;
            break;
        }
    } while (!done);

    if (next_stage >= 0) {
        g_story_route[g_story_chapter] = g_fight_stage;
        disp_Reset();
        motion_DropChanbaraPack();
        hud_LoadStageCard(next_stage, load_addr);
    }
}

/* ---- the fight (fight.h): loading a fighter and the weapons outside
 * SCREEN_FIGHT_INIT, for the story and the endings ---- */

/* Sets a fighter up from g_fight_config, giving it the other's weapon if
 * it is to have the same, and loads its model. */
void fight_LoadFighter(s32 fighter_index) {
    if ((u8)g_fight_config.weapon[fighter_index] == FIGHT_WEAPON_SAME) {
        g_fight_config.weapon[fighter_index] =
            g_fight_config.weapon[fighter_index == 0];
    }
    fighter_Init(fighter_index, g_fight_config.is_cpu[fighter_index],
                 g_fight_config.roster[fighter_index],
                 g_fight_config.weapon[fighter_index], 0);
    disp_Reset();
    motion_DropChanbaraPack();
    model_Load(fighter_index,
               g_fighter_roster_models[g_fight_config.roster[fighter_index]],
               SYS_STAGING_ADDR);
}

/* Loads the weapons and sub-weapons of both fighters, with the sounds
 * their weapons make. */
void fight_LoadWeapons(void) {
    s32 load_addr = SYS_STAGING_ADDR;
    s32 i;

    disp_Reset();
    motion_DropChanbaraPack();
    resource_ClearWeaponRequests();

    for (i = 0; i < 2; i++) {
        resource_RequestWeapon(g_fighters[i].weapon_model);
        resource_SetWeaponSounds(
            i, g_resource_weapon_sound_kinds[g_fighters[i].character]
                                            [g_fighters[i].weapon_id]);
        if (g_fighters[i].sub_weapon_kind != OBJECT_NONE) {
            resource_RequestWeapon(
                g_object_weapon_models[g_fighters[i].sub_weapon_kind]);
            if (g_fighters[i].sub_weapon_kind == OBJECT_FROG) {
                resource_RequestWeapon(g_object_weapon_models[OBJECT_FROG] +
                                       WEAPON_MODEL_FROG_IN_AIR);
            }
        }
    }

    resource_LoadWeapons(load_addr);
}

/* Loads one fighter's weapon and nothing else: for an ending, which shows
 * one fighter. */
void fight_LoadWeaponOf(s32 fighter_index) {
    s32 load_addr = SYS_STAGING_ADDR;

    disp_Reset();
    motion_DropChanbaraPack();
    resource_ClearWeaponRequests();

    resource_RequestWeapon(g_fighters[fighter_index].weapon_model);

    if (g_fight_mode == FIGHT_MODE_TEAM) {
        resource_SetWeaponSounds(
            fighter_index, g_resource_weapon_sound_kinds_alt
                               [g_fighters[fighter_index].character]
                               [g_fighters[fighter_index].weapon_id]);
    } else {
        resource_SetWeaponSounds(
            fighter_index,
            g_resource_weapon_sound_kinds[g_fighters[fighter_index].character]
                                         [g_fighters[fighter_index].weapon_id]);
    }
    resource_LoadWeapons(load_addr);
}

/* ---- the script ---- */

/* Starts the story of `character`: the player against the CPU, nothing
 * yet behind it, the script at its beginning. Loads the first stage card
 * and the player's fighter. */
void story_Start(s32 character) {
    g_fight_config.is_cpu[0] = 0;
    g_fight_config.is_cpu[1] = 1;
    g_story_interlude = NULL;
    g_story_victim = NULL;
    g_story_player_roster = STORY_NO_ROSTER;
    g_story_guest_roster = STORY_NO_ROSTER;
    g_story_first_henchmen = 1;
    g_story_chapter = 0;
    g_story_unlocks = 0;
    g_story_script_cursor = g_story_scripts[character];
    eff_Init();
    story_LoadNextStageCard(g_story_script_cursor);
    fight_LoadFighter(0);
    g_story_cutscene_set =
        g_fighter_roster_characters[g_fight_config.roster[0]];
}

/* An interlude begins: the guest takes the player's place, unwounded; the
 * player's own roster entry and wounds are kept. */
static void story_PutGuestIn(void) {
    g_story_player_roster = g_fight_config.roster[0];
    {
        u8 *interlude = g_story_interlude;
        u8 wounds = g_fight_story_wounds;
        u8 guest = *interlude;
        g_story_player_wounds = wounds;
        g_fight_story_wounds = 0;
        g_fight_config.roster[0] = guest;
    }
    model_RequestTextures(0, 0);
}

/* The guest's part is over: the player's own fighter comes back, with the
 * wounds it showed but healed; who the guest was and the wounds it took are
 * kept, for when it turns up as an opponent. */
void story_PutPlayerBack(void) {
    u8 guest = g_fight_config.roster[0];
    u8 guest_wounds = g_fight_story_wounds;
    u8 player = g_story_player_roster;
    u8 player_wounds = g_story_player_wounds;
    g_story_player_roster = STORY_NO_ROSTER;
    g_story_guest_roster = guest;
    g_story_guest_wounds = guest_wounds;
    g_fight_config.roster[0] = player;
    g_fight_story_wounds = player_wounds;
    model_RequestTextures(0, player_wounds);
    fighter_HealWounds(g_fighters);
}

/* STORY_HENCHMEN: the opponent is a band of the henchmen, those of the
 * player's clan (in the first chapter, always the first clan's); loads
 * one. Returns the size of the operands. */
static s32 story_SetHenchmen(u8 *operands) {
    u8 character;
    u8 clan;
    g_story_opponent_named = 0;
    character = g_fighter_roster_characters[g_fight_config.roster[0]];
    clan = g_fighter_clans[character];
    if (clan != 0 && g_story_chapter == 1) {
        clan = 0;
    }
    g_fight_config.roster[1] = clan != 0 ? ROSTER_SIDEARM_1 : ROSTER_SIDEARM_2;
    g_fight_config.weapon[1] = WEAPON_ID_KATANA;
    {
        u8 number = operands[STORY_HENCHMEN_NUMBER];
        g_story_henchmen_left = number;
        g_story_henchmen_count = number;
    }
    g_fight_config.story_cpu_levels[1] = operands[STORY_HENCHMEN_LEVEL];
    g_story_henchman_tint.r = operands[STORY_HENCHMEN_RED];
    g_story_henchman_tint.g = operands[STORY_HENCHMEN_GREEN];
    g_story_henchman_tint.b = operands[STORY_HENCHMEN_BLUE];
    fight_LoadFighter(1);
    fight_LoadWeapons();
    return STORY_HENCHMEN_SIZE;
}

/* STORY_OPPONENT: the opponent is the one the operands name; loads it.
 * Returns the size of the operands. */
s32 story_SetOpponent(u8 *operands) {
    g_story_opponent_named = 1;
    g_fight_config.roster[1] = operands[STORY_OPPONENT_ROSTER];
    g_fight_config.weapon[1] = operands[STORY_OPPONENT_WEAPON];
    g_fight_config.story_cpu_levels[1] = operands[STORY_OPPONENT_LEVEL];
    fight_LoadFighter(1);
    fight_LoadWeapons();
    return STORY_OPPONENT_SIZE;
}

/* STORY_VICTIM: the player against the victim, both with the katana. */
static void story_SetVictim(void) {
    g_story_opponent_named = 1;
    g_fight_config.weapon[0] = WEAPON_ID_KATANA;
    g_fight_config.roster[1] = ROSTER_VICTIM;
    g_fight_config.weapon[1] = WEAPON_ID_KATANA;
    fight_LoadFighter(0);
    fight_LoadFighter(1);
    fight_LoadWeapons();
}

/* Goes to the cutscene screen with scene `scene` of the interlude (enum
 * InterludeScene). */
void story_PlayInterludeScene(s32 scene) {
    g_story_interlude_scene = scene;
    g_sys_screen = SCREEN_CUTSCENE_INIT;
    g_story_cutscene = g_story_interlude +
                       (scene * STORY_CUTSCENE_SIZE + STORY_INTERLUDE_SCENES);
}

/* The same with a scene of the victim's (enum VictimScene). */
void story_PlayVictimScene(s32 scene) {
    g_story_victim_scene = scene;
    g_sys_screen = SCREEN_CUTSCENE_INIT;
    g_story_cutscene =
        g_story_victim + (scene * STORY_CUTSCENE_SIZE - STORY_CUTSCENE_SIZE);
}

/* Reads the script on from where it stopped, setting up what the commands
 * ask for, until one of them has chosen the next screen. */
void story_RunScript(void) {
    s32 screen_chosen;
    s8 *second_roster;

    snd_Reset();
    screen_chosen = 0;
    second_roster = &g_fight_config.roster[1];

    do {
        u8 *cursor = g_story_script_cursor;
        s32 command;
        g_story_script_cursor = cursor + 1;
        command = *cursor;

        switch (command) {
        case STORY_HENCHMEN: {
            s32 size = story_SetHenchmen(g_story_script_cursor);
            g_story_script_cursor += size;
            break;
        }

        case STORY_OPPONENT: {
            s32 size = story_SetOpponent(g_story_script_cursor);
            g_story_script_cursor += size;
            break;
        }

        case STORY_SECOND_FIGHTER: {
            u8 *operands = g_story_script_cursor;
            s8 roster;
            g_story_script_cursor = operands + 1;
            roster = operands[0];
            g_story_script_cursor = operands + 2;
            /* the second side's roster place, and its weapon two bytes on */
            second_roster[0] = roster;
            second_roster[2] = operands[1];
            /* the guest of an interlude comes back as it was left */
            if (roster == g_story_guest_roster) {
                model_RequestTextures(1, g_story_guest_wounds);
            }
            fight_LoadFighter(1);
            fight_LoadWeapons();
            break;
        }

        case STORY_STAGE: {
            u8 stage;
            u8 *operands = g_story_script_cursor;
            u8 chapter = g_story_chapter;
            g_story_script_cursor = operands + 1;
            stage = *operands;
            g_story_chapter = chapter + 1;
            g_fight_stage = stage;
            g_story_route[chapter] = stage;
            g_sys_screen = SCREEN_STAGE_CARD_INIT;
            screen_chosen = 1;
            fighter_HealWounds(g_fighters);
            g_story_chapter_fights = 0;
            break;
        }

        case STORY_END:
            if (g_fight_continues == 0) {
                g_fight_ending =
                    (g_fighter_clans[g_fighters[0].character] != 0);
                g_sys_screen = SCREEN_ENDING_INIT;
            } else {
                g_sys_screen = SCREEN_STAFF_ROLL_INIT;
            }
            screen_chosen = 1;
            break;

        case STORY_CUTSCENE:
            g_sys_screen = SCREEN_CUTSCENE_INIT;
            g_story_cutscene = g_story_script_cursor;
            g_story_script_cursor += STORY_CUTSCENE_SIZE;
            screen_chosen = 1;
            break;

        case STORY_INTERLUDE:
            g_story_interlude = g_story_script_cursor;
            g_story_script_cursor += STORY_INTERLUDE_SIZE;
            story_PutGuestIn();
            fight_LoadFighter(0);
            story_SetHenchmen(g_story_interlude + STORY_INTERLUDE_HENCHMEN);
        case STORY_FIGHT:
            g_sys_screen = SCREEN_FIGHT_INIT;
            g_story_chapter_fights++;
            screen_chosen = 1;
            break;

        case STORY_VICTIM:
            g_story_victim = g_story_script_cursor;
            g_story_script_cursor += VICTIM_SCENES * STORY_CUTSCENE_SIZE;
            screen_chosen = 1;
            story_SetVictim();
            story_PlayVictimScene(VICTIM_SCENE_MEET);
            g_fight_first_person = 0;
            break;
        }
    } while (!screen_chosen);
}

/* ---- the story's screens ---- */

/* SCREEN_STAGE_CARD_INIT: clears the screen for the stage card. The player's
 * fighter is to show the wounds it has, the opponent none. */
void story_StageCardInit(void) {
    model_RequestTextures(0, g_fight_story_wounds);
    model_RequestTextures(1, 0);
    snd_Reset();
    disp_Blank();
    disp_SetBackground(1, 0, 0, 0);
    g_sys_screen_frames = 0;
    g_sys_screen = SCREEN_STAGE_CARD_UPDATE;
    disp_Show();
}

/* SCREEN_STAGE_CARD_UPDATE: draws the card for two frames, so that it is
 * in both display buffers, and makes it the loading screen, on which the
 * route is then marked; behind it the next chapter's card and the stage
 * are loaded and the script goes on. */
void story_StageCardUpdate(void) {
    g_sys_screen_frames++;
    if (g_sys_screen_frames < 3) {
#if RB_PORT
        /* the card stays on show while the stage loads: the remastered
         * picture reads a mod's pictures of it at once */
        port_RemasterKeepsPicture(TRUE);
#endif
        g_disp_prim_cursor += hud_DrawStageCard((s32)g_disp_prim_cursor, 1) / 4;
    }
    if (g_sys_screen_frames == 3) {
        DrawSync(0);
#if RB_PORT
        port_RemasterKeepsPicture(FALSE);
#endif
        story_LoadNextStageCard(g_story_script_cursor);
        /* longer than any wait: the first mark is drawn at once */
        g_story_route_timer = 500;
        g_story_route_drawn = 0;
        g_disp_loading_screen = LOADING_SCREEN_STAGE_CARD;
        fight_LoadStage();
        story_RunScript();
        disp_SetBackground(1, 0, 0, 0);
        g_sys_skip_frames = 1;
    }
}

/* SCREEN_CUTSCENE_INIT: loads the cutscene the script asked for
 * (g_story_cutscene: its file, voice track and channel, and flags) and
 * starts motion and voice together. A fighter that is to hold the weapon of
 * the story's scenes has that loaded in place of its own. */
void cutscene_Init(void) {
    s32 weapon_model_1;
    s32 weapon_model_2;
    s32 shown_weapon_1;
    s32 shown_weapon_2;
    s32 load_addr;
    s32 voice_file;
    u8 flags;

    load_addr = SYS_STAGING_ADDR;
    snd_EndFight();
    eff_Stop();
    if (g_disp_loading_screen != LOADING_SCREEN_STAGE_CARD) {
        disp_Blank();
    }
    if (g_disp_loading_screen != LOADING_SCREEN_NONE) {
        disp_Show();
    }
    disp_DrawLoadingScreen();
    disp_Reset();
    motion_DropChanbaraPack();
    disp_SetBackground(1, 0, 0, 0);
    if (g_story_cutscene[STORY_CUTSCENE_FLAGS] & STORY_CUTSCENE_LONG) {
        motion_ForgetFiles();
        load_addr = SYS_LOAD_ADDR;
    }
    {
        u8 scene_flags = g_story_cutscene[STORY_CUTSCENE_FLAGS];
        if (scene_flags &
            (STORY_CUTSCENE_STORY_WEAPON_1 | STORY_CUTSCENE_STORY_WEAPON_2)) {
            weapon_model_1 = g_fighters[0].weapon_model;
            weapon_model_2 = g_fighters[1].weapon_model;
            if (scene_flags & STORY_CUTSCENE_STORY_WEAPON_1) {
                g_fighters[0].weapon_model = WEAPON_MODEL_STORY;
            }
            if (g_story_cutscene[STORY_CUTSCENE_FLAGS] &
                STORY_CUTSCENE_STORY_WEAPON_2) {
                g_fighters[1].weapon_model = WEAPON_MODEL_STORY;
            }
            fight_LoadWeapons();
            g_fighters[0].weapon_model = weapon_model_1;
            g_fighters[1].weapon_model = weapon_model_2;
        }
    }
    if (g_story_cutscene[STORY_CUTSCENE_FLAGS] & STORY_CUTSCENE_WEAPON_1) {
        shown_weapon_1 = g_fighters[0].weapon_model;
    } else {
        shown_weapon_1 = -1;
    }
    if (g_story_cutscene[STORY_CUTSCENE_FLAGS] & STORY_CUTSCENE_WEAPON_2) {
        shown_weapon_2 = g_fighters[1].weapon_model;
    } else {
        shown_weapon_2 = -1;
    }
    flags = g_story_cutscene[STORY_CUTSCENE_FLAGS];
    if (flags & STORY_CUTSCENE_STORY_WEAPON_1) {
        shown_weapon_1 = WEAPON_MODEL_STORY;
    }
    if (flags & STORY_CUTSCENE_STORY_WEAPON_2) {
        shown_weapon_2 = WEAPON_MODEL_STORY;
    }
    g_cutscene_unused_start_flag = 0;
    cutscene_StartFromSet(
        g_story_cutscene_set, g_story_cutscene[STORY_CUTSCENE_FILE], 0,
        shown_weapon_1, shown_weapon_2, -1, -1, load_addr);
    model_SetOutline(0, 0);
    model_SetOutline(1, 0);
    if (g_story_cutscene[STORY_CUTSCENE_FLAGS] & STORY_CUTSCENE_WHOLE_STAGE) {
        resource_SelectStageArea(-1);
    }
    if (g_fight_config.roster[1] == ROSTER_SIDEARM_1 ||
        g_fight_config.roster[1] == ROSTER_SIDEARM_2) {
        model_TintOpponent(g_story_henchman_tint.r, g_story_henchman_tint.g,
                           g_story_henchman_tint.b);
    }
    story_FinishStageCard();
    g_disp_loading_screen = LOADING_SCREEN_NONE;
    g_disp_loading_noise_burst = 0;
    snd_EnableCdAudio();
    voice_file = cdrom_GetFileIndex(
        CDROM_GROUP_AUDIO, g_story_cutscene[STORY_CUTSCENE_VOICE]);
    cdrom_StartAudio(voice_file, g_story_cutscene[STORY_CUTSCENE_CHANNEL]);
    cdrom_WaitAudioStarted();
    g_sys_screen_frames = 0;
    g_sys_screen = SCREEN_CUTSCENE_UPDATE;
    disp_Show();
}

/* SCREEN_CUTSCENE_UPDATE: plays the cutscene to its end, or until cross is
 * pressed. What follows depends on which scene it was: a fight for the
 * scenes that lead into one; after the last scene of an interlude or of
 * the victim, the script. */
void cutscene_Update(void) {
    g_sys_screen_frames++;

    if (cutscene_RunFrame() != 0 &&
        !(g_pad_state.pressed & PAD_ANY(PADRdown))) {
        return;
    }
    cdrom_StopAndWait();
    cutscene_End();
    if (g_fight_mode != FIGHT_MODE_STORY) {
        return;
    }
    if (g_story_interlude) {
        g_sys_screen = SCREEN_FIGHT_INIT;
        switch (g_story_interlude_scene) {
        case 1:
        case INTERLUDE_GUEST_MEETS_OPPONENT:
        case INTERLUDE_GUEST_FELL:
        case INTERLUDE_PLAYER_MEETS_OPPONENT:
            g_story_chapter_fights++;
            return;
        case INTERLUDE_GUEST_WON:
            /* the player's own fighter, back in the guest's place */
            fight_LoadFighter(0);
            fight_LoadWeapons();
            /* fall through */
        case INTERLUDE_PLAYER_WON:
            g_story_interlude = NULL;
            break;
        default:
            return;
        }
    } else if (g_story_victim) {
        s32 scene = g_story_victim_scene;

        if (scene == VICTIM_SCENE_MEET) {
            g_sys_screen = SCREEN_FIGHT_INIT;
            return;
        }
        if (scene == 0 || scene > VICTIM_SCENES) {
            return;
        }
        g_story_victim = NULL;
    }
    story_RunScript();
}

/* ---- the scripts ---- */

/* The commands as they are written below, a step to a line and a blank line
 * before each chapter:
 *
 *   S_STAGE(stage)                              the chapter's stage card
 *   S_HENCHMEN(number, level, red, green, blue) a band of henchmen to fight
 *   S_OPPONENT(roster, weapon, level)           an opponent to fight
 *   S_FIGHT                                     the fight that was set up
 *   S_CUTSCENE(file, voice, channel, flags)     a cutscene
 *   S_SECOND_FIGHTER(roster, weapon)            who else is in the cutscenes
 *   S_INTERLUDE(guest, band, foe, five scenes)  the player plays a guest
 *   S_VICTIM(three scenes)                      the story's last opponent
 *   S_END
 *
 * A stage is an enum StageIndex, a roster entry an enum RosterId, a
 * weapon an enum WeaponId. A level is how well the CPU plays, a sixteenth of a
 * fighter's level. A band's colour is the tint its henchmen wear. A
 * cutscene is its file among the character's cutscenes, its voice (an audio
 * file, MAR_XA(n) or END_XA(n), and the channel of it) and its flags. */

/* A cutscene's flags (STORY_CUTSCENE_...): which of the two fighters have
 * their weapons; the first holds the weapon of the story's scenes instead;
 * the whole stage is shown; the scene is a long one. */
#define S_UNARMED 0
#define S_ARMED_1 STORY_CUTSCENE_WEAPON_1
#define S_ARMED_2 STORY_CUTSCENE_WEAPON_2
#define S_ARMED (STORY_CUTSCENE_WEAPON_1 | STORY_CUTSCENE_WEAPON_2)
#define S_STORY_WEAPON_1 STORY_CUTSCENE_STORY_WEAPON_1
#define S_WHOLE_STAGE STORY_CUTSCENE_WHOLE_STAGE
#define S_LONG STORY_CUTSCENE_LONG
#define S_END STORY_END
#define S_STAGE(stage) STORY_STAGE, stage
#define S_SECOND_FIGHTER(roster, weapon) STORY_SECOND_FIGHTER, roster, weapon
#define S_BAND(number, level, red, green, blue) number, level, red, green, blue
#define S_HENCHMEN(number, level, red, green, blue)                            \
    STORY_HENCHMEN, S_BAND(number, level, red, green, blue)
#define S_FOE(roster, weapon, level) roster, weapon, level
#define S_OPPONENT(roster, weapon, level)                                      \
    STORY_OPPONENT, S_FOE(roster, weapon, level)
#define S_FIGHT STORY_FIGHT
/* A voice's audio file (enum CdromAudioFile): XA_0/MAR_XA00.XA to
 * MAR_XA28.XA, and for the long last scenes XA_1/END_XA1.XA to END_XA3.XA. */
#define MAR_XA(n) (CDROM_AUDIO_MAR_XA + (n))
#define END_XA(n) (CDROM_AUDIO_END_XA + (n) - 1)
#define S_SCENE(file, voice, channel, flags) file, voice, channel, flags
#define S_CUTSCENE(file, voice, channel, flags)                                \
    STORY_CUTSCENE, S_SCENE(file, voice, channel, flags)
/* The guest, its henchmen and its opponent; then the scenes: the guest
 * meets the opponent, the guest has won, the guest has fallen, the player
 * meets the opponent, the player has won. */
#define S_INTERLUDE(guest, band, foe, s2, s3, s4, s5, s6)                      \
    STORY_INTERLUDE, guest, band, foe, s2, s3, s4, s5, s6
/* The scenes: the meeting, the victim struck, the victim spared. */
#define S_VICTIM(meet, struck, spared) STORY_VICTIM, meet, struck, spared

/* clang-format off */
static u8 g_story_script_kannuki[] = {
    S_STAGE(STAGE_OUTSIDE_MEIKYOKAN),
    S_HENCHMEN(7, 0x10, 3, 51, 36),
    S_FIGHT,
    S_CUTSCENE(0, MAR_XA(25), 10, S_ARMED_1),

    S_STAGE(STAGE_TERRACED_FIELDS),
    S_HENCHMEN(5, 0x10, 120, 0, 255),
    S_FIGHT,
    S_OPPONENT(ROSTER_TONY_UMEDA, WEAPON_ID_NODACHI, 0x50),
    S_CUTSCENE(1, MAR_XA(14), 3, S_UNARMED),
    S_FIGHT,
    S_CUTSCENE(2, MAR_XA(6), 9, S_UNARMED),
    S_SECOND_FIGHTER(ROSTER_RED_SHADOW, FIGHT_WEAPON_SAME),
    S_CUTSCENE(3, MAR_XA(24), 7, S_ARMED),

    S_STAGE(STAGE_UNDER_RAILWAY),
    S_INTERLUDE(ROSTER_RED_SHADOW,
                S_BAND(6, 0x10, 255, 48, 3),
                S_FOE(ROSTER_ISOHACHI, WEAPON_ID_YARI, 0x60),
                S_SCENE(4, MAR_XA(18), 0, S_ARMED),
                S_SCENE(5, MAR_XA(2), 12, S_UNARMED),
                S_SCENE(6, MAR_XA(0), 5, S_ARMED_1),
                S_SCENE(7, MAR_XA(10), 0, S_ARMED_1),
                S_SCENE(8, MAR_XA(8), 7, S_UNARMED)),

    S_STAGE(STAGE_LUMBERYARD),
    S_HENCHMEN(5, 0x20, 200, 200, 200),
    S_FIGHT,
    S_OPPONENT(ROSTER_KAUN_STORY, WEAPON_ID_KATANA, 0x70),
    S_CUTSCENE(9, MAR_XA(18), 3, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(10, MAR_XA(20), 13, S_ARMED),

    S_STAGE(STAGE_LIGHTHOUSE),
    S_OPPONENT(ROSTER_KATZE, WEAPON_ID_SHORT_GUN, 0x80),
    S_CUTSCENE(11, MAR_XA(18), 7, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(12, MAR_XA(20), 6, S_ARMED),

    S_STAGE(STAGE_TATARA_SHRINE),
    S_SECOND_FIGHTER(ROSTER_MATSUMUSHI, FIGHT_WEAPON_SAME),
    S_CUTSCENE(13, MAR_XA(27), 5, S_ARMED),
    S_INTERLUDE(ROSTER_MATSUMUSHI,
                S_BAND(6, 0x20, 137, 0, 0),
                S_FOE(ROSTER_JO_STORY, WEAPON_ID_LONG_SWORD, 0xA0),
                S_SCENE(14, MAR_XA(17), 13, S_ARMED),
                S_SCENE(15, MAR_XA(5), 9, S_ARMED),
                S_SCENE(16, MAR_XA(0), 5, S_ARMED),
                S_SCENE(17, MAR_XA(8), 3, S_ARMED),
                S_SCENE(18, MAR_XA(18), 1, S_ARMED)),

    S_STAGE(STAGE_ORANGE_TREE_FIELDS),
    S_HENCHMEN(4, 0x30, 30, 30, 30),
    S_FIGHT,
    S_OPPONENT(ROSTER_GENGORO_STORY, WEAPON_ID_BROADSWORD, 0xB0),
    S_CUTSCENE(19, MAR_XA(20), 3, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(20, MAR_XA(9), 13, S_ARMED),

    S_STAGE(STAGE_SHAINTO_HQ),
    S_OPPONENT(ROSTER_ARMORED, WEAPON_ID_KATANA, 0xC0),
    S_CUTSCENE(21, MAR_XA(21), 4, S_ARMED),
    S_FIGHT,
    S_SECOND_FIGHTER(ROSTER_TATSUMI_STORY, FIGHT_WEAPON_SAME),
    S_CUTSCENE(22, END_XA(3), 0, S_ARMED | S_LONG),
    S_END,
};

static u8 g_story_script_tatsumi[] = {
    S_STAGE(STAGE_OUTSIDE_MEIKYOKAN),
    S_HENCHMEN(7, 0x10, 3, 51, 36),
    S_FIGHT,
    S_CUTSCENE(0, MAR_XA(24), 13, S_ARMED_1),

    S_STAGE(STAGE_TATARA_SHRINE_PATH),
    S_HENCHMEN(5, 0x10, 187, 89, 1),
    S_FIGHT,
    S_OPPONENT(ROSTER_HONGOU, WEAPON_ID_YARI, 0x50),
    S_CUTSCENE(1, MAR_XA(15), 2, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(2, MAR_XA(10), 11, S_ARMED),
    S_SECOND_FIGHTER(ROSTER_NIGHTSTALKER, FIGHT_WEAPON_SAME),
    S_CUTSCENE(3, MAR_XA(24), 9, S_UNARMED),

    S_STAGE(STAGE_BAMBOO_THICKET),
    S_INTERLUDE(ROSTER_NIGHTSTALKER,
                S_BAND(6, 0x10, 137, 0, 0),
                S_FOE(ROSTER_JO_STORY, WEAPON_ID_NODACHI, 0x60),
                S_SCENE(4, MAR_XA(14), 13, S_ARMED),
                S_SCENE(5, MAR_XA(6), 3, S_ARMED_1),
                S_SCENE(6, MAR_XA(1), 5, S_ARMED),
                S_SCENE(7, MAR_XA(8), 4, S_ARMED_2),
                S_SCENE(8, MAR_XA(7), 8, S_ARMED_1)),

    S_STAGE(STAGE_FUTURE_DUMP_SITE),
    S_OPPONENT(ROSTER_KATZE, WEAPON_ID_SHORT_GUN, 0x90),
    S_CUTSCENE(9, MAR_XA(18), 8, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(10, MAR_XA(19), 13, S_UNARMED),

    S_STAGE(STAGE_SNAKE_CLIFF),
    S_HENCHMEN(5, 0x20, 113, 1, 150),
    S_FIGHT,
    S_OPPONENT(ROSTER_CHIHIRO, WEAPON_ID_LONG_SWORD, 0x70),
    S_CUTSCENE(11, MAR_XA(12), 13, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(12, MAR_XA(11), 14, S_UNARMED),

    S_STAGE(STAGE_FERRY_DOCK),
    S_SECOND_FIGHTER(ROSTER_UTSUSEMI, FIGHT_WEAPON_SAME),
    S_CUTSCENE(13, MAR_XA(26), 13, S_ARMED),
    S_INTERLUDE(ROSTER_UTSUSEMI,
                S_BAND(6, 0x20, 200, 200, 200),
                S_FOE(ROSTER_KAUN_STORY, WEAPON_ID_KATANA, 0xA0),
                S_SCENE(14, MAR_XA(20), 2, S_ARMED),
                S_SCENE(15, MAR_XA(10), 13, S_ARMED_1),
                S_SCENE(16, MAR_XA(1), 5, S_ARMED_1),
                S_SCENE(17, MAR_XA(4), 4, S_ARMED),
                S_SCENE(18, MAR_XA(20), 4, S_ARMED_1)),

    S_STAGE(STAGE_SHAINTO_GATE),
    S_HENCHMEN(4, 0x30, 30, 30, 30),
    S_FIGHT,
    S_OPPONENT(ROSTER_GENGORO_STORY, WEAPON_ID_BROADSWORD, 0xB0),
    S_CUTSCENE(19, MAR_XA(16), 10, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(20, MAR_XA(11), 4, S_ARMED_1),

    S_STAGE(STAGE_SHAINTO_HQ),
    S_OPPONENT(ROSTER_ARMORED, WEAPON_ID_KATANA, 0xC0),
    S_CUTSCENE(21, MAR_XA(21), 9, S_ARMED_1),
    S_FIGHT,
    S_CUTSCENE(22, END_XA(3), 1, S_ARMED | S_LONG),
    S_END,
};

static u8 g_story_script_mikado[] = {
    S_STAGE(STAGE_OUTSIDE_MEIKYOKAN),
    S_HENCHMEN(7, 0x10, 3, 51, 36),
    S_FIGHT,
    S_CUTSCENE(0, MAR_XA(24), 14, S_ARMED),

    S_STAGE(STAGE_ABANDONED_STATION),
    S_HENCHMEN(5, 0x10, 107, 102, 27),
    S_FIGHT,
    S_OPPONENT(ROSTER_HIGHWAYMAN, WEAPON_ID_NODACHI, 0x50),
    S_CUTSCENE(1, MAR_XA(15), 11, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(2, MAR_XA(10), 3, S_UNARMED),
    S_SECOND_FIGHTER(ROSTER_SUMINAGASHI, FIGHT_WEAPON_SAME),
    S_CUTSCENE(3, MAR_XA(23), 10, S_ARMED_2),

    S_STAGE(STAGE_RAILWAY),
    S_INTERLUDE(ROSTER_SUMINAGASHI,
                S_BAND(6, 0x10, 200, 30, 30),
                S_FOE(ROSTER_UTAMARU, WEAPON_ID_LONG_SWORD, 0x60),
                S_SCENE(4, MAR_XA(17), 9, S_ARMED),
                S_SCENE(5, MAR_XA(7), 7, S_ARMED_1),
                S_SCENE(6, MAR_XA(5), 1, S_ARMED_1),
                S_SCENE(7, MAR_XA(5), 6, S_ARMED_2),
                S_SCENE(8, MAR_XA(5), 7, S_ARMED_2)),

    S_STAGE(STAGE_KABUKI_STAGE),
    S_HENCHMEN(5, 0x20, 200, 200, 200),
    S_FIGHT,
    S_OPPONENT(ROSTER_KAUN_STORY, WEAPON_ID_YARI, 0x70),
    S_CUTSCENE(9, MAR_XA(14), 1, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(10, MAR_XA(21), 3, S_ARMED),

    S_STAGE(STAGE_ABANDONED_SHIPYARD),
    S_OPPONENT(ROSTER_KATZE, WEAPON_ID_SHORT_GUN, 0x80),
    S_CUTSCENE(11, MAR_XA(17), 6, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(12, MAR_XA(21), 5, S_ARMED),

    S_STAGE(STAGE_TATARA_SHRINE_PATH),
    S_SECOND_FIGHTER(ROSTER_SAZANKA, FIGHT_WEAPON_SAME),
    S_CUTSCENE(13, MAR_XA(25), 13, S_ARMED),
    S_INTERLUDE(ROSTER_SAZANKA,
                S_BAND(6, 0x20, 137, 0, 0),
                S_FOE(ROSTER_JO_STORY, WEAPON_ID_LONG_SWORD, 0xA0),
                S_SCENE(14, MAR_XA(14), 5, S_ARMED),
                S_SCENE(15, MAR_XA(3), 3, S_ARMED),
                S_SCENE(16, MAR_XA(5), 2, S_ARMED),
                S_SCENE(17, MAR_XA(1), 3, S_ARMED),
                S_SCENE(18, MAR_XA(11), 3, S_ARMED)),

    S_STAGE(STAGE_SHAINTO_CEMETERY),
    S_HENCHMEN(4, 0x30, 30, 30, 30),
    S_FIGHT,
    S_OPPONENT(ROSTER_GENGORO_STORY, WEAPON_ID_BROADSWORD, 0xB0),
    S_CUTSCENE(19, MAR_XA(18), 6, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(20, MAR_XA(3), 6, S_ARMED),

    S_STAGE(STAGE_SHAINTO_HQ),
    S_OPPONENT(ROSTER_ARMORED, WEAPON_ID_KATANA, 0xC0),
    S_CUTSCENE(21, MAR_XA(22), 9, S_ARMED),
    S_FIGHT,
    S_SECOND_FIGHTER(ROSTER_TATSUMI_STORY, FIGHT_WEAPON_SAME),
    S_CUTSCENE(22, END_XA(3), 2, S_ARMED | S_LONG),
    S_END,
};

static u8 g_story_script_utsusemi[] = {
    S_STAGE(STAGE_OUTSIDE_MEIKYOKAN),
    S_HENCHMEN(7, 0x10, 3, 51, 36),
    S_FIGHT,
    S_CUTSCENE(0, MAR_XA(23), 3, S_ARMED),

    S_STAGE(STAGE_ABANDONED_STATION),
    S_HENCHMEN(5, 0x10, 255, 48, 3),
    S_FIGHT,
    S_OPPONENT(ROSTER_ISOHACHI, WEAPON_ID_NODACHI, 0x50),
    S_CUTSCENE(1, MAR_XA(19), 0, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(2, MAR_XA(5), 10, S_ARMED),

    S_STAGE(STAGE_RAILWAY),
    S_HENCHMEN(6, 0x10, 137, 0, 0),
    S_FIGHT,
    S_OPPONENT(ROSTER_JO_STORY, WEAPON_ID_YARI, 0x60),
    S_CUTSCENE(3, MAR_XA(17), 5, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(4, MAR_XA(10), 1, S_ARMED),

    S_STAGE(STAGE_ABANDONED_SHIPYARD),
    S_HENCHMEN(5, 0x20, 200, 30, 30),
    S_FIGHT,
    S_OPPONENT(ROSTER_UTAMARU, WEAPON_ID_LONG_SWORD, 0x70),
    S_CUTSCENE(5, MAR_XA(13), 2, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(6, MAR_XA(7), 10, S_ARMED),

    S_STAGE(STAGE_KIMIGAURA_COAST),
    S_OPPONENT(ROSTER_KATZE, WEAPON_ID_SHORT_GUN, 0x80),
    S_CUTSCENE(7, MAR_XA(12), 8, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(8, MAR_XA(19), 15, S_ARMED),
    S_SECOND_FIGHTER(ROSTER_TATSUMI_STORY, FIGHT_WEAPON_SAME),
    S_CUTSCENE(9, MAR_XA(26), 5, S_ARMED),

    S_STAGE(STAGE_FERRY_DOCK),
    S_INTERLUDE(ROSTER_TATSUMI_STORY,
                S_BAND(6, 0x20, 200, 200, 200),
                S_FOE(ROSTER_KAUN_STORY, WEAPON_ID_KATANA, 0xA0),
                S_SCENE(10, MAR_XA(15), 5, S_ARMED),
                S_SCENE(11, MAR_XA(10), 14, S_ARMED),
                S_SCENE(12, MAR_XA(2), 13, S_ARMED),
                S_SCENE(13, MAR_XA(4), 4, S_ARMED),
                S_SCENE(14, MAR_XA(4), 13, S_ARMED)),

    S_STAGE(STAGE_SHAINTO_GATE),
    S_HENCHMEN(4, 0x30, 30, 30, 30),
    S_FIGHT,
    S_OPPONENT(ROSTER_GENGORO_STORY, WEAPON_ID_BROADSWORD, 0xB0),
    S_CUTSCENE(15, MAR_XA(16), 2, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(16, MAR_XA(10), 2, S_ARMED),

    S_STAGE(STAGE_SHAINTO_HQ),
    S_OPPONENT(ROSTER_ARMORED, WEAPON_ID_KATANA, 0xC0),
    S_CUTSCENE(17, MAR_XA(21), 11, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(18, END_XA(2), 6, S_ARMED | S_LONG),
    S_END,
};

static u8 g_story_script_red_shadow[] = {
    S_STAGE(STAGE_OUTSIDE_MEIKYOKAN),
    S_HENCHMEN(7, 0x10, 3, 51, 36),
    S_FIGHT,
    S_CUTSCENE(0, MAR_XA(24), 6, S_ARMED),

    S_STAGE(STAGE_ABANDONED_STATION),
    S_HENCHMEN(5, 0x10, 107, 102, 27),
    S_FIGHT,
    S_OPPONENT(ROSTER_HIGHWAYMAN, WEAPON_ID_YARI, 0x50),
    S_CUTSCENE(1, MAR_XA(16), 12, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(2, MAR_XA(1), 9, S_ARMED),
    S_SECOND_FIGHTER(ROSTER_KANNUKI_STORY, FIGHT_WEAPON_SAME),
    S_CUTSCENE(3, MAR_XA(26), 1, S_ARMED),

    S_STAGE(STAGE_RAILWAY),
    S_INTERLUDE(ROSTER_KANNUKI_STORY,
                S_BAND(6, 0x10, 255, 48, 3),
                S_FOE(ROSTER_ISOHACHI, WEAPON_ID_NODACHI, 0x60),
                S_SCENE(4, MAR_XA(19), 1, S_ARMED),
                S_SCENE(5, MAR_XA(3), 5, S_ARMED),
                S_SCENE(6, MAR_XA(3), 0, S_ARMED),
                S_SCENE(7, MAR_XA(3), 2, S_ARMED),
                S_SCENE(8, MAR_XA(9), 9, S_ARMED)),

    S_STAGE(STAGE_ABANDONED_SHIPYARD),
    S_HENCHMEN(5, 0x20, 113, 1, 150),
    S_FIGHT,
    S_OPPONENT(ROSTER_CHIHIRO, WEAPON_ID_LONG_SWORD, 0x70),
    S_CUTSCENE(9, MAR_XA(19), 7, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(10, MAR_XA(7), 5, S_ARMED),

    S_STAGE(STAGE_FERRY_DOCK),
    S_OPPONENT(ROSTER_KATZE, WEAPON_ID_SHORT_GUN, 0x80),
    S_CUTSCENE(11, MAR_XA(15), 14, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(12, MAR_XA(2), 5, S_ARMED),
    S_SECOND_FIGHTER(ROSTER_NIGHTSTALKER, FIGHT_WEAPON_SAME),
    S_CUTSCENE(13, MAR_XA(23), 7, S_ARMED),

    S_STAGE(STAGE_ABANDONED_SHIP),
    S_INTERLUDE(ROSTER_NIGHTSTALKER,
                S_BAND(6, 0x20, 200, 200, 200),
                S_FOE(ROSTER_KAUN_STORY, WEAPON_ID_KATANA, 0xA0),
                S_SCENE(14, MAR_XA(16), 13, S_ARMED),
                S_SCENE(15, MAR_XA(7), 13, S_ARMED),
                S_SCENE(16, MAR_XA(2), 11, S_ARMED),
                S_SCENE(17, MAR_XA(4), 4, S_ARMED),
                S_SCENE(18, MAR_XA(6), 11, S_ARMED)),

    S_STAGE(STAGE_ORANGE_TREE_FIELDS),
    S_HENCHMEN(4, 0x30, 30, 30, 30),
    S_FIGHT,
    S_OPPONENT(ROSTER_GENGORO_STORY, WEAPON_ID_BROADSWORD, 0xB0),
    S_CUTSCENE(19, MAR_XA(17), 2, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(20, MAR_XA(6), 12, S_ARMED),

    S_STAGE(STAGE_SHAINTO_HQ),
    S_OPPONENT(ROSTER_ARMORED, WEAPON_ID_KATANA, 0xC0),
    S_CUTSCENE(21, MAR_XA(20), 0, S_ARMED),
    S_FIGHT,
    S_SECOND_FIGHTER(ROSTER_UTSUSEMI, FIGHT_WEAPON_SAME),
    S_CUTSCENE(22, END_XA(2), 1, S_ARMED_1 | S_LONG),
    S_END,
};

static u8 g_story_script_sazanka[] = {
    S_STAGE(STAGE_OUTSIDE_MEIKYOKAN),
    S_HENCHMEN(7, 0x10, 3, 51, 36),
    S_FIGHT,
    S_CUTSCENE(0, MAR_XA(24), 8, S_ARMED),

    S_STAGE(STAGE_TATARA_SHRINE_PATH),
    S_HENCHMEN(5, 0x10, 187, 89, 1),
    S_FIGHT,
    S_OPPONENT(ROSTER_HONGOU, WEAPON_ID_LONG_SWORD, 0x50),
    S_CUTSCENE(1, MAR_XA(16), 14, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(2, MAR_XA(7), 0, S_ARMED),

    S_STAGE(STAGE_BAMBOO_THICKET),
    S_HENCHMEN(6, 0x10, 255, 48, 3),
    S_FIGHT,
    S_OPPONENT(ROSTER_ISOHACHI, WEAPON_ID_NODACHI, 0x60),
    S_CUTSCENE(3, MAR_XA(19), 8, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(4, MAR_XA(7), 14, S_ARMED),

    S_STAGE(STAGE_FUTURE_DUMP_SITE),
    S_HENCHMEN(5, 0x20, 200, 200, 200),
    S_FIGHT,
    S_OPPONENT(ROSTER_KAUN_STORY, WEAPON_ID_YARI, 0x70),
    S_CUTSCENE(5, MAR_XA(13), 0, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(6, MAR_XA(0), 10, S_ARMED),

    S_STAGE(STAGE_BUILDING_ROOFTOP),
    S_OPPONENT(ROSTER_KATZE, WEAPON_ID_SHORT_GUN, 0x80),
    S_CUTSCENE(7, MAR_XA(17), 11, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(8, MAR_XA(20), 8, S_ARMED),

    S_STAGE(STAGE_TATARA_SHRINE),
    S_SECOND_FIGHTER(ROSTER_MIKADO_STORY, FIGHT_WEAPON_SAME),
    S_CUTSCENE(9, MAR_XA(26), 3, S_ARMED),
    S_INTERLUDE(ROSTER_MIKADO_STORY,
                S_BAND(6, 0x20, 137, 0, 0),
                S_FOE(ROSTER_JO_STORY, WEAPON_ID_LONG_SWORD, 0xA0),
                S_SCENE(10, MAR_XA(13), 7, S_ARMED),
                S_SCENE(11, MAR_XA(1), 13, S_ARMED),
                S_SCENE(12, MAR_XA(3), 8, S_ARMED),
                S_SCENE(13, MAR_XA(1), 14, S_ARMED),
                S_SCENE(14, MAR_XA(1), 4, S_ARMED)),

    S_STAGE(STAGE_SHAINTO_GATE),
    S_HENCHMEN(4, 0x30, 30, 30, 30),
    S_FIGHT,
    S_OPPONENT(ROSTER_GENGORO_STORY, WEAPON_ID_BROADSWORD, 0xB0),
    S_CUTSCENE(15, MAR_XA(17), 1, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(16, MAR_XA(2), 0, S_ARMED),

    S_STAGE(STAGE_SHAINTO_HQ),
    S_OPPONENT(ROSTER_ARMORED, WEAPON_ID_KATANA, 0xC0),
    S_CUTSCENE(17, MAR_XA(20), 12, S_ARMED),
    S_FIGHT,
    S_SECOND_FIGHTER(ROSTER_UTSUSEMI, FIGHT_WEAPON_SAME),
    S_CUTSCENE(18, END_XA(2), 5, S_ARMED | S_LONG),
    S_END,
};

static u8 g_story_script_suminagashi[] = {
    S_STAGE(STAGE_OUTSIDE_MEIKYOKAN),
    S_HENCHMEN(7, 0x10, 3, 51, 36),
    S_FIGHT,
    S_CUTSCENE(0, MAR_XA(23), 6, S_ARMED),

    S_STAGE(STAGE_PURPLE_CAVERN),
    S_HENCHMEN(5, 0x10, 255, 48, 3),
    S_FIGHT,
    S_OPPONENT(ROSTER_ISOHACHI, WEAPON_ID_LONG_SWORD, 0x50),
    S_CUTSCENE(1, MAR_XA(13), 8, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(2, MAR_XA(0), 12, S_ARMED),
    S_SECOND_FIGHTER(ROSTER_MATSUMUSHI, FIGHT_WEAPON_SAME),
    S_CUTSCENE(3, MAR_XA(27), 14, S_ARMED | S_LONG),

    S_STAGE(STAGE_UNDER_LOST_BRIDGE),
    S_INTERLUDE(ROSTER_MATSUMUSHI,
                S_BAND(6, 0x10, 113, 1, 150),
                S_FOE(ROSTER_CHIHIRO, WEAPON_ID_YARI, 0x60),
                S_SCENE(4, MAR_XA(14), 6, S_ARMED),
                S_SCENE(5, MAR_XA(4), 0, S_ARMED),
                S_SCENE(6, MAR_XA(5), 8, S_ARMED),
                S_SCENE(7, MAR_XA(2), 3, S_ARMED),
                S_SCENE(8, MAR_XA(4), 3, S_ARMED)),

    S_STAGE(STAGE_LOST_BRIDGE),
    S_HENCHMEN(5, 0x20, 187, 89, 1),
    S_FIGHT,
    S_OPPONENT(ROSTER_HONGOU, WEAPON_ID_BROADSWORD, 0x70),
    S_CUTSCENE(9, MAR_XA(13), 9, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(10, MAR_XA(9), 12, S_ARMED),

    S_STAGE(STAGE_TERRACED_FIELDS),
    S_OPPONENT(ROSTER_KATZE, WEAPON_ID_SHORT_GUN, 0x80),
    S_CUTSCENE(11, MAR_XA(15), 1, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(12, MAR_XA(3), 13, S_ARMED),
    S_SECOND_FIGHTER(ROSTER_MIKADO_STORY, FIGHT_WEAPON_SAME),
    S_CUTSCENE(13, MAR_XA(24), 0, S_ARMED),

    S_STAGE(STAGE_UNDER_RAILWAY),
    S_INTERLUDE(ROSTER_MIKADO_STORY,
                S_BAND(6, 0x20, 200, 30, 30),
                S_FOE(ROSTER_UTAMARU, WEAPON_ID_NODACHI, 0xA0),
                S_SCENE(14, MAR_XA(16), 1, S_ARMED),
                S_SCENE(15, MAR_XA(9), 5, S_ARMED),
                S_SCENE(16, MAR_XA(5), 0, S_ARMED),
                S_SCENE(17, MAR_XA(2), 8, S_ARMED),
                S_SCENE(18, MAR_XA(12), 4, S_ARMED)),

    S_STAGE(STAGE_ORANGE_TREE_FIELDS),
    S_HENCHMEN(5, 0x30, 30, 30, 30),
    S_FIGHT,
    S_OPPONENT(ROSTER_GENGORO_STORY, WEAPON_ID_BROADSWORD, 0xB0),
    S_CUTSCENE(19, MAR_XA(15), 12, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(20, MAR_XA(5), 14, S_ARMED),

    S_STAGE(STAGE_SHAINTO_HQ),
    S_OPPONENT(ROSTER_ARMORED, WEAPON_ID_KATANA, 0xC0),
    S_CUTSCENE(21, MAR_XA(20), 9, S_ARMED),
    S_FIGHT,
    S_SECOND_FIGHTER(ROSTER_MIKADO_STORY, FIGHT_WEAPON_SAME),
    S_CUTSCENE(22, END_XA(1), 4, S_ARMED | S_LONG),
    S_END,
};

static u8 g_story_script_nightstalker[] = {
    S_STAGE(STAGE_OUTSIDE_MEIKYOKAN),
    S_HENCHMEN(7, 0x10, 3, 51, 36),
    S_FIGHT,
    S_CUTSCENE(0, MAR_XA(24), 12, S_ARMED),

    S_STAGE(STAGE_TERRACED_FIELDS),
    S_HENCHMEN(5, 0x10, 120, 0, 255),
    S_FIGHT,
    S_OPPONENT(ROSTER_TONY_UMEDA, WEAPON_ID_YARI, 0x50),
    S_CUTSCENE(1, MAR_XA(14), 2, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(2, MAR_XA(7), 6, S_ARMED),
    S_SECOND_FIGHTER(ROSTER_TATSUMI_STORY, FIGHT_WEAPON_SAME),
    S_CUTSCENE(3, MAR_XA(25), 9, S_ARMED),

    S_STAGE(STAGE_BAMBOO_THICKET),
    S_INTERLUDE(ROSTER_TATSUMI_STORY,
                S_BAND(6, 0x10, 137, 0, 0),
                S_FOE(ROSTER_JO_STORY, WEAPON_ID_NODACHI, 0x60),
                S_SCENE(4, MAR_XA(16), 7, S_ARMED),
                S_SCENE(5, MAR_XA(4), 2, S_ARMED),
                S_SCENE(6, MAR_XA(0), 9, S_ARMED),
                S_SCENE(7, MAR_XA(0), 6, S_ARMED),
                S_SCENE(8, MAR_XA(4), 1, S_ARMED)),

    S_STAGE(STAGE_UNDER_RAILWAY),
    S_HENCHMEN(5, 0x20, 200, 30, 30),
    S_FIGHT,
    S_OPPONENT(ROSTER_UTAMARU, WEAPON_ID_LONG_SWORD, 0x70),
    S_CUTSCENE(9, MAR_XA(16), 8, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(10, MAR_XA(9), 14, S_ARMED),

    S_STAGE(STAGE_LUMBERYARD),
    S_OPPONENT(ROSTER_KATZE, WEAPON_ID_SHORT_GUN, 0x80),
    S_CUTSCENE(11, MAR_XA(17), 10, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(12, MAR_XA(3), 7, S_ARMED),
    S_SECOND_FIGHTER(ROSTER_RED_SHADOW, FIGHT_WEAPON_SAME),
    S_CUTSCENE(13, MAR_XA(25), 5, S_ARMED),

    S_STAGE(STAGE_ABANDONED_SHIP),
    S_INTERLUDE(ROSTER_RED_SHADOW,
                S_BAND(6, 0x20, 200, 200, 200),
                S_FOE(ROSTER_KAUN_STORY, WEAPON_ID_KATANA, 0xA0),
                S_SCENE(14, MAR_XA(15), 0, S_ARMED),
                S_SCENE(15, MAR_XA(1), 12, S_ARMED),
                S_SCENE(16, MAR_XA(0), 11, S_ARMED),
                S_SCENE(17, MAR_XA(4), 4, S_ARMED),
                S_SCENE(18, MAR_XA(0), 0, S_ARMED)),

    S_STAGE(STAGE_FERRY_DOCK),
    S_HENCHMEN(4, 0x30, 30, 30, 30),
    S_FIGHT,
    S_OPPONENT(ROSTER_GENGORO_STORY, WEAPON_ID_BROADSWORD, 0xB0),
    S_CUTSCENE(19, MAR_XA(16), 0, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(20, MAR_XA(4), 10, S_ARMED),

    S_STAGE(STAGE_SHAINTO_HQ),
    S_OPPONENT(ROSTER_ARMORED, WEAPON_ID_KATANA, 0xC0),
    S_CUTSCENE(21, MAR_XA(20), 1, S_ARMED),
    S_FIGHT,
    S_SECOND_FIGHTER(ROSTER_RED_SHADOW, FIGHT_WEAPON_SAME),
    S_CUTSCENE(22, END_XA(1), 3, S_ARMED | S_LONG),
    S_END,
};

static u8 g_story_script_matsumushi[] = {
    S_STAGE(STAGE_OUTSIDE_MEIKYOKAN),
    S_HENCHMEN(7, 0x10, 3, 51, 36),
    S_FIGHT,
    S_CUTSCENE(0, MAR_XA(25), 3, S_ARMED),

    S_STAGE(STAGE_PURPLE_CAVERN),
    S_HENCHMEN(5, 0x10, 107, 102, 27),
    S_FIGHT,
    S_OPPONENT(ROSTER_HIGHWAYMAN, WEAPON_ID_YARI, 0x50),
    S_CUTSCENE(1, MAR_XA(15), 3, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(2, MAR_XA(2), 7, S_ARMED),
    S_SECOND_FIGHTER(ROSTER_SUMINAGASHI, FIGHT_WEAPON_SAME),
    S_CUTSCENE(3, MAR_XA(28), 4, S_ARMED),

    S_STAGE(STAGE_LOST_BRIDGE),
    S_INTERLUDE(ROSTER_SUMINAGASHI,
                S_BAND(6, 0x10, 113, 1, 150),
                S_FOE(ROSTER_CHIHIRO, WEAPON_ID_NODACHI, 0x60),
                S_SCENE(4, MAR_XA(17), 7, S_ARMED),
                S_SCENE(5, MAR_XA(9), 1, S_ARMED),
                S_SCENE(6, MAR_XA(8), 11, S_ARMED),
                S_SCENE(7, MAR_XA(2), 3, S_ARMED),
                S_SCENE(8, MAR_XA(10), 12, S_ARMED)),

    S_STAGE(STAGE_WATER_TOWER_DAM),
    S_HENCHMEN(5, 0x20, 200, 30, 30),
    S_FIGHT,
    S_OPPONENT(ROSTER_UTAMARU, WEAPON_ID_BROADSWORD, 0x70),
    S_CUTSCENE(9, MAR_XA(14), 14, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(10, MAR_XA(10), 8, S_ARMED),

    S_STAGE(STAGE_SNAKE_CLIFF),
    S_OPPONENT(ROSTER_KATZE, WEAPON_ID_SHORT_GUN, 0x80),
    S_CUTSCENE(11, MAR_XA(18), 13, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(12, MAR_XA(22), 7, S_ARMED),

    S_STAGE(STAGE_TATARA_SHRINE),
    S_SECOND_FIGHTER(ROSTER_KANNUKI_STORY, FIGHT_WEAPON_SAME),
    S_CUTSCENE(13, MAR_XA(25), 11, S_ARMED),
    S_INTERLUDE(ROSTER_KANNUKI_STORY,
                S_BAND(6, 0x20, 137, 0, 0),
                S_FOE(ROSTER_JO_STORY, WEAPON_ID_LONG_SWORD, 0xA0),
                S_SCENE(14, MAR_XA(13), 14, S_ARMED),
                S_SCENE(15, MAR_XA(5), 13, S_ARMED),
                S_SCENE(16, MAR_XA(8), 12, S_ARMED),
                S_SCENE(17, MAR_XA(1), 1, S_ARMED),
                S_SCENE(18, MAR_XA(9), 2, S_ARMED)),

    S_STAGE(STAGE_SHAINTO_CEMETERY),
    S_HENCHMEN(4, 0x30, 30, 30, 30),
    S_FIGHT,
    S_OPPONENT(ROSTER_GENGORO_STORY, WEAPON_ID_BROADSWORD, 0xB0),
    S_CUTSCENE(19, MAR_XA(15), 4, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(20, MAR_XA(9), 3, S_ARMED),

    S_STAGE(STAGE_SHAINTO_HQ),
    S_OPPONENT(ROSTER_ARMORED, WEAPON_ID_KATANA, 0xC0),
    S_CUTSCENE(21, MAR_XA(19), 2, S_ARMED),
    S_FIGHT,
    S_SECOND_FIGHTER(ROSTER_KANNUKI_STORY, FIGHT_WEAPON_SAME),
    S_CUTSCENE(22, END_XA(2), 4, S_ARMED | S_LONG),
    S_END,
};

static u8 g_story_script_gengoro[] = {
    S_STAGE(STAGE_SHAINTO_HQ),
    S_HENCHMEN(5, 0x10, 80, 17, 45),
    S_CUTSCENE(0, MAR_XA(23), 11, S_UNARMED),
    S_FIGHT,
    S_CUTSCENE(1, MAR_XA(22), 11, S_ARMED),

    S_STAGE(STAGE_FERRY_DOCK),
    S_HENCHMEN(4, 0x10, 74, 0, 137),
    S_FIGHT,
    S_OPPONENT(ROSTER_MATSUMUSHI, WEAPON_ID_BROADSWORD, 0x50),
    S_CUTSCENE(2, MAR_XA(21), 1, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(3, MAR_XA(0), 3, S_UNARMED),
    S_SECOND_FIGHTER(ROSTER_ISOHACHI, FIGHT_WEAPON_SAME),
    S_CUTSCENE(4, MAR_XA(25), 4, S_UNARMED),

    S_STAGE(STAGE_ABANDONED_SHIP),
    S_INTERLUDE(ROSTER_ISOHACHI,
                S_BAND(5, 0x10, 113, 130, 2),
                S_FOE(ROSTER_TATSUMI_STORY, WEAPON_ID_LONG_SWORD, 0x60),
                S_SCENE(5, MAR_XA(17), 12, S_ARMED),
                S_SCENE(6, MAR_XA(8), 8, S_ARMED_2),
                S_SCENE(7, MAR_XA(2), 14, S_ARMED_1),
                S_SCENE(8, MAR_XA(2), 4, S_ARMED),
                S_SCENE(9, MAR_XA(6), 6, S_ARMED)),

    S_STAGE(STAGE_WATER_TOWER_DAM),
    S_OPPONENT(ROSTER_TSUBAME, WEAPON_ID_LONG_GUN, 0x70),
    S_CUTSCENE(10, MAR_XA(16), 11, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(11, MAR_XA(22), 6, S_ARMED),

    S_STAGE(STAGE_MEIKYOKAN_DOJO),
    S_HENCHMEN(3, 0x30, 1, 1, 177),
    S_FIGHT,
    S_OPPONENT(ROSTER_KANNUKI_STORY, WEAPON_ID_BROADSWORD, 0x80),
    S_CUTSCENE(12, MAR_XA(19), 10, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(13, MAR_XA(15), 13, S_ARMED | S_STORY_WEAPON_1),

    S_STAGE(STAGE_CASTLE_ROOFTOP),
    S_SECOND_FIGHTER(ROSTER_CHIHIRO, FIGHT_WEAPON_SAME),
    S_CUTSCENE(14, MAR_XA(28), 5, S_ARMED | S_STORY_WEAPON_1),
    S_INTERLUDE(ROSTER_CHIHIRO,
                S_BAND(5, 0x30, 137, 0, 71),
                S_FOE(ROSTER_MIKADO_STORY, WEAPON_ID_NAGINATA, 0xA0),
                S_SCENE(15, MAR_XA(21), 10, S_ARMED),
                S_SCENE(16, MAR_XA(9), 11, S_ARMED),
                S_SCENE(17, MAR_XA(2), 14, S_ARMED),
                S_SCENE(18, MAR_XA(4), 11, S_ARMED),
                S_SCENE(19, MAR_XA(10), 9, S_ARMED)),

    S_STAGE(STAGE_UNDER_LOST_BRIDGE),
    S_HENCHMEN(4, 0x30, 255, 130, 4),
    S_FIGHT,
    S_OPPONENT(ROSTER_UTSUSEMI, WEAPON_ID_KATANA, 0xB0),
    S_CUTSCENE(20, MAR_XA(17), 8, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(21, MAR_XA(3), 4, S_ARMED),

    S_STAGE(STAGE_TATARA_SHRINE),
    S_OPPONENT(ROSTER_VANISHER, WEAPON_ID_KATANA, 0xC0),
    S_CUTSCENE(22, MAR_XA(21), 8, S_ARMED | S_WHOLE_STAGE),
    S_FIGHT,
    S_CUTSCENE(23, MAR_XA(11), 9, S_ARMED_1),

    S_STAGE(STAGE_MAIN_TATARA_SHRINE),
    S_SECOND_FIGHTER(ROSTER_VICTIM, FIGHT_WEAPON_SAME),
    S_VICTIM(S_SCENE(24, END_XA(2), 2, S_ARMED | S_STORY_WEAPON_1),
             S_SCENE(25, MAR_XA(26), 8, S_ARMED | S_STORY_WEAPON_1 | S_LONG),
             S_SCENE(26, MAR_XA(25), 14, S_ARMED | S_STORY_WEAPON_1 | S_LONG)),
    S_END,
};

static u8 g_story_script_kaun[] = {
    S_STAGE(STAGE_SHAINTO_HQ),
    S_HENCHMEN(5, 0x10, 80, 17, 45),
    S_CUTSCENE(0, MAR_XA(24), 1, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(1, MAR_XA(23), 5, S_ARMED),

    S_STAGE(STAGE_FERRY_DOCK),
    S_HENCHMEN(4, 0x10, 44, 38, 173),
    S_FIGHT,
    S_OPPONENT(ROSTER_SUMINAGASHI, WEAPON_ID_LONG_SWORD, 0x50),
    S_CUTSCENE(2, MAR_XA(22), 1, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(3, MAR_XA(0), 1, S_ARMED_1),
    S_SECOND_FIGHTER(ROSTER_HIGHWAYMAN, FIGHT_WEAPON_SAME),
    S_CUTSCENE(4, MAR_XA(24), 5, S_ARMED_1),

    S_STAGE(STAGE_BUILDING_PARKING_LOT),
    S_INTERLUDE(ROSTER_HIGHWAYMAN,
                S_BAND(5, 0x10, 113, 130, 2),
                S_FOE(ROSTER_TATSUMI_STORY, WEAPON_ID_BROADSWORD, 0x60),
                S_SCENE(5, MAR_XA(14), 0, S_ARMED),
                S_SCENE(6, MAR_XA(1), 2, S_UNARMED),
                S_SCENE(7, MAR_XA(13), 13, S_ARMED_1),
                S_SCENE(8, MAR_XA(11), 2, S_ARMED),
                S_SCENE(9, MAR_XA(11), 12, S_ARMED)),

    S_STAGE(STAGE_ABANDONED_TUNNEL),
    S_OPPONENT(ROSTER_TSUBAME, WEAPON_ID_LONG_GUN, 0x70),
    S_CUTSCENE(10, MAR_XA(19), 3, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(11, MAR_XA(22), 3, S_ARMED),

    S_STAGE(STAGE_MEIKYOKAN_DOJO),
    S_HENCHMEN(3, 0x30, 1, 1, 177),
    S_FIGHT,
    S_OPPONENT(ROSTER_KANNUKI_STORY, WEAPON_ID_NODACHI, 0x80),
    S_CUTSCENE(12, MAR_XA(12), 7, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(13, MAR_XA(12), 12, S_ARMED | S_STORY_WEAPON_1),
    S_SECOND_FIGHTER(ROSTER_UTAMARU, FIGHT_WEAPON_SAME),
    S_CUTSCENE(14, MAR_XA(26), 7, S_ARMED | S_STORY_WEAPON_1),

    S_STAGE(STAGE_TATARA_SHRINE_PATH),
    S_INTERLUDE(ROSTER_UTAMARU,
                S_BAND(5, 0x30, 74, 0, 137),
                S_FOE(ROSTER_MATSUMUSHI, WEAPON_ID_KATANA, 0xA0),
                S_SCENE(15, MAR_XA(19), 5, S_ARMED),
                S_SCENE(16, MAR_XA(3), 12, S_ARMED),
                S_SCENE(17, MAR_XA(11), 13, S_ARMED),
                S_SCENE(18, MAR_XA(7), 12, S_ARMED),
                S_SCENE(19, MAR_XA(10), 7, S_ARMED)),

    S_STAGE(STAGE_LOST_BRIDGE),
    S_HENCHMEN(4, 0x30, 137, 0, 71),
    S_FIGHT,
    S_OPPONENT(ROSTER_MIKADO_STORY, WEAPON_ID_NAGINATA, 0xB0),
    S_CUTSCENE(20, MAR_XA(19), 11, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(21, MAR_XA(1), 10, S_ARMED),

    S_STAGE(STAGE_TATARA_SHRINE),
    S_OPPONENT(ROSTER_VANISHER, WEAPON_ID_KATANA, 0xC0),
    S_CUTSCENE(22, MAR_XA(18), 5, S_ARMED | S_WHOLE_STAGE),
    S_FIGHT,
    S_CUTSCENE(23, MAR_XA(14), 4, S_ARMED_1),

    S_STAGE(STAGE_MAIN_TATARA_SHRINE),
    S_SECOND_FIGHTER(ROSTER_VICTIM, FIGHT_WEAPON_SAME),
    S_VICTIM(S_SCENE(24, END_XA(1), 2, S_ARMED | S_STORY_WEAPON_1 | S_LONG),
             S_SCENE(25, MAR_XA(27), 13, S_ARMED | S_STORY_WEAPON_1 | S_LONG),
             S_SCENE(26, MAR_XA(24), 11, S_ARMED | S_STORY_WEAPON_1 | S_LONG)),
    S_END,
};

static u8 g_story_script_jo[] = {
    S_STAGE(STAGE_SHAINTO_HQ),
    S_HENCHMEN(5, 0x10, 80, 17, 45),
    S_CUTSCENE(0, MAR_XA(24), 10, S_UNARMED),
    S_FIGHT,
    S_CUTSCENE(1, MAR_XA(22), 14, S_ARMED_1),

    S_STAGE(STAGE_FERRY_DOCK),
    S_HENCHMEN(4, 0x10, 255, 30, 0),
    S_FIGHT,
    S_OPPONENT(ROSTER_RED_SHADOW, WEAPON_ID_BROADSWORD, 0x50),
    S_CUTSCENE(2, MAR_XA(17), 3, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(3, MAR_XA(2), 2, S_ARMED_1),
    S_SECOND_FIGHTER(ROSTER_TONY_UMEDA, FIGHT_WEAPON_SAME),
    S_CUTSCENE(4, MAR_XA(25), 7, S_ARMED_1),

    S_STAGE(STAGE_SNAKE_CLIFF),
    S_INTERLUDE(ROSTER_TONY_UMEDA,
                S_BAND(5, 0x10, 113, 130, 2),
                S_FOE(ROSTER_TATSUMI_STORY, WEAPON_ID_LONG_SWORD, 0x60),
                S_SCENE(5, MAR_XA(16), 6, S_ARMED),
                S_SCENE(6, MAR_XA(4), 7, S_ARMED_1),
                S_SCENE(7, MAR_XA(1), 1, S_ARMED_1),
                S_SCENE(8, MAR_XA(4), 6, S_ARMED),
                S_SCENE(9, MAR_XA(8), 2, S_UNARMED)),

    S_STAGE(STAGE_OUTSIDE_MEIKYOKAN),
    S_OPPONENT(ROSTER_TSUBAME, WEAPON_ID_LONG_GUN, 0x70),
    S_CUTSCENE(10, MAR_XA(17), 4, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(11, MAR_XA(21), 15, S_ARMED_1),

    S_STAGE(STAGE_MEIKYOKAN_DOJO),
    S_HENCHMEN(3, 0x30, 137, 0, 71),
    S_FIGHT,
    S_OPPONENT(ROSTER_MIKADO_STORY, WEAPON_ID_KATANA, 0x80),
    S_CUTSCENE(12, MAR_XA(18), 10, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(13, MAR_XA(1), 8, S_ARMED_1 | S_STORY_WEAPON_1),
    S_SECOND_FIGHTER(ROSTER_HONGOU, FIGHT_WEAPON_SAME),
    S_CUTSCENE(14, MAR_XA(27), 8, S_ARMED | S_STORY_WEAPON_1),

    S_STAGE(STAGE_PURPLE_CAVERN),
    S_INTERLUDE(ROSTER_HONGOU,
                S_BAND(5, 0x30, 21, 128, 34),
                S_FOE(ROSTER_SAZANKA, WEAPON_ID_NAGINATA, 0xA0),
                S_SCENE(15, MAR_XA(11), 0, S_ARMED),
                S_SCENE(16, MAR_XA(3), 11, S_ARMED_1),
                S_SCENE(17, MAR_XA(1), 0, S_ARMED_1),
                S_SCENE(18, MAR_XA(11), 1, S_ARMED),
                S_SCENE(19, MAR_XA(11), 5, S_ARMED_1)),

    S_STAGE(STAGE_UNDER_LOST_BRIDGE),
    S_HENCHMEN(4, 0x30, 1, 1, 177),
    S_FIGHT,
    S_OPPONENT(ROSTER_KANNUKI_STORY, WEAPON_ID_NODACHI, 0xB0),
    S_CUTSCENE(20, MAR_XA(21), 14, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(21, MAR_XA(4), 8, S_ARMED_1),

    S_STAGE(STAGE_TATARA_SHRINE),
    S_OPPONENT(ROSTER_VANISHER, WEAPON_ID_KATANA, 0xC0),
    S_CUTSCENE(22, MAR_XA(18), 14, S_ARMED | S_WHOLE_STAGE),
    S_FIGHT,
    S_CUTSCENE(23, MAR_XA(12), 3, S_ARMED_1),

    S_STAGE(STAGE_MAIN_TATARA_SHRINE),
    S_SECOND_FIGHTER(ROSTER_VICTIM, FIGHT_WEAPON_SAME),
    S_VICTIM(S_SCENE(24, END_XA(2), 0, S_ARMED_1 | S_STORY_WEAPON_1 | S_LONG),
             S_SCENE(25, MAR_XA(28), 8, S_UNARMED | S_STORY_WEAPON_1 | S_LONG),
             S_SCENE(26, MAR_XA(22), 0, S_UNARMED | S_STORY_WEAPON_1 | S_LONG)),
    S_END,
};

static u8 g_story_script_hongou[] = {
    S_STAGE(STAGE_SHAINTO_HQ),
    S_HENCHMEN(5, 0x10, 80, 17, 45),
    S_CUTSCENE(0, MAR_XA(23), 13, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(1, MAR_XA(22), 13, S_ARMED),

    S_STAGE(STAGE_FERRY_DOCK),
    S_HENCHMEN(4, 0x10, 75, 11, 11),
    S_FIGHT,
    S_OPPONENT(ROSTER_NIGHTSTALKER, WEAPON_ID_NAGINATA, 0x50),
    S_CUTSCENE(2, MAR_XA(19), 9, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(3, MAR_XA(3), 1, S_ARMED),
    S_SECOND_FIGHTER(ROSTER_ISOHACHI, FIGHT_WEAPON_SAME),
    S_CUTSCENE(4, MAR_XA(26), 11, S_ARMED),

    S_STAGE(STAGE_BUILDING_ROOFTOP),
    S_INTERLUDE(ROSTER_ISOHACHI,
                S_BAND(5, 0x10, 137, 0, 71),
                S_FOE(ROSTER_MIKADO_STORY, WEAPON_ID_LONG_SWORD, 0x60),
                S_SCENE(5, MAR_XA(13), 12, S_ARMED),
                S_SCENE(6, MAR_XA(3), 9, S_ARMED),
                S_SCENE(7, MAR_XA(2), 9, S_ARMED),
                S_SCENE(8, MAR_XA(5), 1, S_ARMED),
                S_SCENE(9, MAR_XA(11), 8, S_ARMED)),

    S_STAGE(STAGE_RAILWAY),
    S_OPPONENT(ROSTER_TSUBAME, WEAPON_ID_LONG_GUN, 0x70),
    S_CUTSCENE(10, MAR_XA(17), 14, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(11, MAR_XA(20), 10, S_ARMED),

    S_STAGE(STAGE_MEIKYOKAN_DOJO),
    S_HENCHMEN(3, 0x30, 1, 1, 177),
    S_FIGHT,
    S_OPPONENT(ROSTER_KANNUKI_STORY, WEAPON_ID_BROADSWORD, 0x80),
    S_CUTSCENE(12, MAR_XA(18), 4, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(13, MAR_XA(6), 7, S_ARMED | S_STORY_WEAPON_1),

    S_STAGE(STAGE_CASTLE_ROOFTOP),
    S_SECOND_FIGHTER(ROSTER_JO_STORY, FIGHT_WEAPON_SAME),
    S_CUTSCENE(14, MAR_XA(26), 9, S_ARMED | S_STORY_WEAPON_1),
    S_INTERLUDE(ROSTER_JO_STORY,
                S_BAND(5, 0x30, 21, 128, 34),
                S_FOE(ROSTER_SAZANKA, WEAPON_ID_NODACHI, 0xA0),
                S_SCENE(15, MAR_XA(15), 6, S_ARMED),
                S_SCENE(16, MAR_XA(0), 4, S_ARMED),
                S_SCENE(17, MAR_XA(2), 10, S_ARMED),
                S_SCENE(18, MAR_XA(3), 8, S_ARMED),
                S_SCENE(19, MAR_XA(7), 11, S_ARMED)),

    S_STAGE(STAGE_PURPLE_CAVERN),
    S_HENCHMEN(4, 0x30, 113, 130, 2),
    S_FIGHT,
    S_OPPONENT(ROSTER_TATSUMI_STORY, WEAPON_ID_KATANA, 0xB0),
    S_CUTSCENE(20, MAR_XA(14), 10, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(21, MAR_XA(11), 11, S_ARMED),

    S_STAGE(STAGE_TATARA_SHRINE),
    S_OPPONENT(ROSTER_VANISHER, WEAPON_ID_KATANA, 0xC0),
    S_CUTSCENE(22, MAR_XA(22), 2, S_ARMED | S_WHOLE_STAGE),
    S_FIGHT,
    S_CUTSCENE(23, MAR_XA(13), 4, S_ARMED_1),

    S_STAGE(STAGE_MAIN_TATARA_SHRINE),
    S_SECOND_FIGHTER(ROSTER_VICTIM, FIGHT_WEAPON_SAME),
    S_VICTIM(S_SCENE(24, END_XA(1), 1, S_ARMED | S_STORY_WEAPON_1 | S_LONG),
             S_SCENE(25, MAR_XA(26), 6, S_ARMED | S_STORY_WEAPON_1 | S_LONG),
             S_SCENE(26, MAR_XA(23), 0, S_ARMED | S_STORY_WEAPON_1 | S_LONG)),
    S_END,
};

static u8 g_story_script_highwayman[] = {
    S_STAGE(STAGE_SHAINTO_HQ),
    S_HENCHMEN(5, 0x10, 80, 17, 45),
    S_CUTSCENE(0, MAR_XA(23), 12, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(1, MAR_XA(22), 12, S_ARMED),

    S_STAGE(STAGE_FERRY_DOCK),
    S_HENCHMEN(4, 0x10, 137, 0, 71),
    S_FIGHT,
    S_OPPONENT(ROSTER_MIKADO_STORY, WEAPON_ID_BROADSWORD, 0x50),
    S_CUTSCENE(2, MAR_XA(11), 10, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(3, MAR_XA(0), 8, S_ARMED),
    S_SECOND_FIGHTER(ROSTER_KAUN_STORY, FIGHT_WEAPON_SAME),
    S_CUTSCENE(4, MAR_XA(25), 0, S_ARMED),

    S_STAGE(STAGE_BUILDING_PARKING_LOT),
    S_INTERLUDE(ROSTER_KAUN_STORY,
                S_BAND(5, 0x10, 113, 130, 2),
                S_FOE(ROSTER_TATSUMI_STORY, WEAPON_ID_KATANA, 0x60),
                S_SCENE(5, MAR_XA(16), 5, S_ARMED),
                S_SCENE(6, MAR_XA(12), 1, S_ARMED),
                S_SCENE(7, MAR_XA(3), 10, S_ARMED),
                S_SCENE(8, MAR_XA(11), 15, S_ARMED),
                S_SCENE(9, MAR_XA(8), 1, S_ARMED)),

    S_STAGE(STAGE_OUTSIDE_MEIKYOKAN),
    S_OPPONENT(ROSTER_TSUBAME, WEAPON_ID_LONG_GUN, 0x70),
    S_CUTSCENE(10, MAR_XA(14), 8, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(11, MAR_XA(22), 8, S_ARMED),

    S_STAGE(STAGE_MEIKYOKAN_DOJO),
    S_HENCHMEN(3, 0x30, 1, 1, 177),
    S_FIGHT,
    S_OPPONENT(ROSTER_KANNUKI_STORY, WEAPON_ID_BROADSWORD, 0x80),
    S_CUTSCENE(12, MAR_XA(21), 6, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(13, MAR_XA(21), 12, S_ARMED | S_STORY_WEAPON_1),
    S_SECOND_FIGHTER(ROSTER_TONY_UMEDA, FIGHT_WEAPON_SAME),
    S_CUTSCENE(14, MAR_XA(25), 2, S_ARMED | S_STORY_WEAPON_1),

    S_STAGE(STAGE_LOST_BRIDGE),
    S_INTERLUDE(ROSTER_TONY_UMEDA,
                S_BAND(5, 0x30, 75, 11, 11),
                S_FOE(ROSTER_NIGHTSTALKER, WEAPON_ID_LONG_SWORD, 0xA0),
                S_SCENE(15, MAR_XA(13), 3, S_ARMED),
                S_SCENE(16, MAR_XA(4), 14, S_ARMED),
                S_SCENE(17, MAR_XA(2), 1, S_ARMED),
                S_SCENE(18, MAR_XA(0), 9, S_ARMED),
                S_SCENE(19, MAR_XA(10), 6, S_ARMED)),

    S_STAGE(STAGE_TATARA_SHRINE_PATH),
    S_HENCHMEN(4, 0x30, 255, 130, 4),
    S_FIGHT,
    S_OPPONENT(ROSTER_UTSUSEMI, WEAPON_ID_NODACHI, 0xB0),
    S_CUTSCENE(20, MAR_XA(18), 11, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(21, MAR_XA(1), 7, S_ARMED),

    S_STAGE(STAGE_TATARA_SHRINE),
    S_OPPONENT(ROSTER_VANISHER, WEAPON_ID_KATANA, 0xC0),
    S_CUTSCENE(22, MAR_XA(20), 7, S_ARMED | S_WHOLE_STAGE),
    S_FIGHT,
    S_CUTSCENE(23, MAR_XA(12), 5, S_ARMED_1),

    S_STAGE(STAGE_MAIN_TATARA_SHRINE),
    S_SECOND_FIGHTER(ROSTER_VICTIM, FIGHT_WEAPON_SAME),
    S_VICTIM(S_SCENE(24, END_XA(1), 0, S_ARMED | S_STORY_WEAPON_1 | S_LONG),
             S_SCENE(25, MAR_XA(27), 3, S_ARMED | S_STORY_WEAPON_1 | S_LONG),
             S_SCENE(26, MAR_XA(22), 5, S_ARMED | S_STORY_WEAPON_1 | S_LONG)),
    S_END,
};

static u8 g_story_script_chihiro[] = {
    S_STAGE(STAGE_SHAINTO_HQ),
    S_HENCHMEN(5, 0x10, 80, 17, 45),
    S_CUTSCENE(0, MAR_XA(24), 3, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(1, MAR_XA(23), 2, S_ARMED),

    S_STAGE(STAGE_KIMIGAURA_COAST),
    S_HENCHMEN(4, 0x10, 255, 30, 0),
    S_FIGHT,
    S_OPPONENT(ROSTER_RED_SHADOW, WEAPON_ID_BROADSWORD, 0x50),
    S_CUTSCENE(2, MAR_XA(19), 6, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(3, MAR_XA(7), 4, S_ARMED),

    S_STAGE(STAGE_SNAKE_CLIFF),
    S_HENCHMEN(5, 0x10, 21, 128, 34),
    S_FIGHT,
    S_OPPONENT(ROSTER_SAZANKA, WEAPON_ID_LONG_SWORD, 0x60),
    S_CUTSCENE(4, MAR_XA(17), 0, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(5, MAR_XA(0), 14, S_ARMED),

    S_STAGE(STAGE_FUTURE_DUMP_SITE),
    S_OPPONENT(ROSTER_TSUBAME, WEAPON_ID_LONG_GUN, 0x70),
    S_CUTSCENE(6, MAR_XA(16), 4, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(7, MAR_XA(21), 0, S_ARMED),

    S_STAGE(STAGE_MEIKYOKAN_DOJO),
    S_HENCHMEN(3, 0x30, 1, 1, 177),
    S_FIGHT,
    S_OPPONENT(ROSTER_KANNUKI_STORY, WEAPON_ID_NODACHI, 0x80),
    S_CUTSCENE(8, MAR_XA(16), 9, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(9, MAR_XA(13), 11, S_ARMED | S_STORY_WEAPON_1),
    S_SECOND_FIGHTER(ROSTER_GENGORO_STORY, FIGHT_WEAPON_SAME),
    S_CUTSCENE(10, MAR_XA(26), 12, S_ARMED | S_STORY_WEAPON_1),

    S_STAGE(STAGE_LOST_BRIDGE),
    S_INTERLUDE(ROSTER_GENGORO_STORY,
                S_BAND(5, 0x30, 137, 0, 71),
                S_FOE(ROSTER_MIKADO_STORY, WEAPON_ID_NAGINATA, 0xA0),
                S_SCENE(11, MAR_XA(14), 7, S_ARMED),
                S_SCENE(12, MAR_XA(10), 10, S_ARMED),
                S_SCENE(13, MAR_XA(2), 3, S_ARMED),
                S_SCENE(14, MAR_XA(5), 2, S_ARMED),
                S_SCENE(15, MAR_XA(12), 9, S_ARMED)),

    S_STAGE(STAGE_TATARA_SHRINE_PATH),
    S_HENCHMEN(4, 0x30, 113, 130, 2),
    S_FIGHT,
    S_OPPONENT(ROSTER_TATSUMI_STORY, WEAPON_ID_KATANA, 0xB0),
    S_CUTSCENE(16, MAR_XA(21), 2, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(17, MAR_XA(15), 8, S_ARMED),

    S_STAGE(STAGE_TATARA_SHRINE),
    S_OPPONENT(ROSTER_VANISHER, WEAPON_ID_KATANA, 0xC0),
    S_CUTSCENE(18, MAR_XA(15), 9, S_ARMED | S_WHOLE_STAGE),
    S_FIGHT,
    S_CUTSCENE(19, MAR_XA(8), 13, S_ARMED_1),

    S_STAGE(STAGE_MAIN_TATARA_SHRINE),
    S_SECOND_FIGHTER(ROSTER_VICTIM, FIGHT_WEAPON_SAME),
    S_VICTIM(S_SCENE(20, END_XA(1), 6, S_ARMED | S_STORY_WEAPON_1 | S_LONG),
             S_SCENE(21, MAR_XA(27), 0, S_ARMED | S_STORY_WEAPON_1 | S_LONG),
             S_SCENE(22, MAR_XA(25), 1, S_ARMED | S_STORY_WEAPON_1 | S_LONG)),
    S_END,
};

static u8 g_story_script_utamaru[] = {
    S_STAGE(STAGE_SHAINTO_HQ),
    S_HENCHMEN(5, 0x10, 80, 17, 45),
    S_CUTSCENE(0, MAR_XA(24), 4, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(1, MAR_XA(23), 1, S_ARMED),

    S_STAGE(STAGE_BUILDING_ROOFTOP),
    S_HENCHMEN(4, 0x10, 75, 11, 11),
    S_FIGHT,
    S_OPPONENT(ROSTER_NIGHTSTALKER, WEAPON_ID_NAGINATA, 0x50),
    S_CUTSCENE(2, MAR_XA(8), 5, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(3, MAR_XA(10), 5, S_ARMED),

    S_STAGE(STAGE_ABANDONED_SHIPYARD),
    S_HENCHMEN(5, 0x10, 255, 30, 0),
    S_FIGHT,
    S_OPPONENT(ROSTER_RED_SHADOW, WEAPON_ID_BROADSWORD, 0x60),
    S_CUTSCENE(4, MAR_XA(14), 12, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(5, MAR_XA(7), 9, S_ARMED),

    S_STAGE(STAGE_UNDER_RAILWAY),
    S_OPPONENT(ROSTER_TSUBAME, WEAPON_ID_LONG_GUN, 0x70),
    S_CUTSCENE(6, MAR_XA(15), 10, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(7, MAR_XA(18), 12, S_ARMED),

    S_STAGE(STAGE_MEIKYOKAN_DOJO),
    S_HENCHMEN(3, 0x30, 137, 0, 71),
    S_FIGHT,
    S_OPPONENT(ROSTER_MIKADO_STORY, WEAPON_ID_KATANA, 0x80),
    S_CUTSCENE(8, MAR_XA(16), 3, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(9, MAR_XA(4), 5, S_ARMED | S_STORY_WEAPON_1),
    S_SECOND_FIGHTER(ROSTER_KAUN_STORY, FIGHT_WEAPON_SAME),
    S_CUTSCENE(10, MAR_XA(25), 8, S_ARMED | S_STORY_WEAPON_1),

    S_STAGE(STAGE_TATARA_SHRINE_PATH),
    S_INTERLUDE(ROSTER_KAUN_STORY,
                S_BAND(5, 0x30, 74, 0, 137),
                S_FOE(ROSTER_MATSUMUSHI, WEAPON_ID_LONG_SWORD, 0xA0),
                S_SCENE(11, MAR_XA(13), 1, S_ARMED),
                S_SCENE(12, MAR_XA(8), 14, S_ARMED),
                S_SCENE(13, MAR_XA(2), 8, S_ARMED),
                S_SCENE(14, MAR_XA(8), 12, S_ARMED),
                S_SCENE(15, MAR_XA(8), 6, S_ARMED)),

    S_STAGE(STAGE_PURPLE_CAVERN),
    S_HENCHMEN(4, 0x30, 1, 1, 177),
    S_FIGHT,
    S_OPPONENT(ROSTER_KANNUKI_STORY, WEAPON_ID_NODACHI, 0xB0),
    S_CUTSCENE(16, MAR_XA(18), 9, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(17, MAR_XA(6), 2, S_ARMED),

    S_STAGE(STAGE_TATARA_SHRINE),
    S_OPPONENT(ROSTER_VANISHER, WEAPON_ID_KATANA, 0xC0),
    S_CUTSCENE(18, MAR_XA(18), 2, S_ARMED | S_WHOLE_STAGE),
    S_FIGHT,
    S_CUTSCENE(19, MAR_XA(0), 2, S_ARMED_1),

    S_STAGE(STAGE_MAIN_TATARA_SHRINE),
    S_SECOND_FIGHTER(ROSTER_VICTIM, FIGHT_WEAPON_SAME),
    S_VICTIM(S_SCENE(20, END_XA(1), 7, S_ARMED | S_STORY_WEAPON_1 | S_LONG),
             S_SCENE(21, MAR_XA(26), 4, S_ARMED | S_STORY_WEAPON_1 | S_LONG),
             S_SCENE(22, MAR_XA(22), 10, S_ARMED | S_STORY_WEAPON_1 | S_LONG)),
    S_END,
};

static u8 g_story_script_tony_umeda[] = {
    S_STAGE(STAGE_SHAINTO_HQ),
    S_HENCHMEN(5, 0x10, 80, 17, 45),
    S_CUTSCENE(0, MAR_XA(24), 2, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(1, MAR_XA(23), 9, S_ARMED),

    S_STAGE(STAGE_KIMIGAURA_COAST),
    S_HENCHMEN(4, 0x10, 44, 38, 173),
    S_FIGHT,
    S_OPPONENT(ROSTER_SUMINAGASHI, WEAPON_ID_LONG_SWORD, 0x50),
    S_CUTSCENE(2, MAR_XA(11), 6, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(3, MAR_XA(3), 14, S_ARMED),
    S_SECOND_FIGHTER(ROSTER_JO_STORY, FIGHT_WEAPON_SAME),
    S_CUTSCENE(4, MAR_XA(25), 12, S_ARMED),

    S_STAGE(STAGE_LIGHTHOUSE),
    S_INTERLUDE(ROSTER_JO_STORY,
                S_BAND(5, 0x10, 113, 130, 2),
                S_FOE(ROSTER_TATSUMI_STORY, WEAPON_ID_KATANA, 0x60),
                S_SCENE(5, MAR_XA(12), 11, S_ARMED),
                S_SCENE(6, MAR_XA(11), 7, S_ARMED),
                S_SCENE(7, MAR_XA(1), 6, S_ARMED),
                S_SCENE(8, MAR_XA(4), 6, S_ARMED),
                S_SCENE(9, MAR_XA(7), 1, S_ARMED)),

    S_STAGE(STAGE_UNDER_RAILWAY),
    S_OPPONENT(ROSTER_TSUBAME, WEAPON_ID_LONG_GUN, 0x70),
    S_CUTSCENE(10, MAR_XA(20), 5, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(11, MAR_XA(22), 4, S_ARMED),

    S_STAGE(STAGE_MEIKYOKAN_DOJO),
    S_HENCHMEN(3, 0x30, 137, 0, 71),
    S_FIGHT,
    S_OPPONENT(ROSTER_MIKADO_STORY, WEAPON_ID_NAGINATA, 0x80),
    S_CUTSCENE(12, MAR_XA(14), 9, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(13, MAR_XA(9), 10, S_ARMED | S_STORY_WEAPON_1),

    S_STAGE(STAGE_CASTLE_ROOFTOP),
    S_SECOND_FIGHTER(ROSTER_HIGHWAYMAN, FIGHT_WEAPON_SAME),
    S_CUTSCENE(14, MAR_XA(25), 6, S_ARMED | S_STORY_WEAPON_1),
    S_INTERLUDE(ROSTER_HIGHWAYMAN,
                S_BAND(5, 0x30, 1, 1, 177),
                S_FOE(ROSTER_KANNUKI_STORY, WEAPON_ID_BROADSWORD, 0xA0),
                S_SCENE(15, MAR_XA(13), 10, S_ARMED),
                S_SCENE(16, MAR_XA(6), 10, S_ARMED),
                S_SCENE(17, MAR_XA(1), 6, S_ARMED),
                S_SCENE(18, MAR_XA(0), 5, S_ARMED),
                S_SCENE(19, MAR_XA(12), 14, S_ARMED)),

    S_STAGE(STAGE_LOST_BRIDGE),
    S_HENCHMEN(4, 0x30, 255, 130, 4),
    S_FIGHT,
    S_OPPONENT(ROSTER_UTSUSEMI, WEAPON_ID_NODACHI, 0xB0),
    S_CUTSCENE(20, MAR_XA(15), 7, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(21, MAR_XA(9), 8, S_ARMED),

    S_STAGE(STAGE_TATARA_SHRINE),
    S_OPPONENT(ROSTER_VANISHER, WEAPON_ID_KATANA, 0xC0),
    S_CUTSCENE(22, MAR_XA(21), 13, S_ARMED | S_WHOLE_STAGE),
    S_FIGHT,
    S_CUTSCENE(23, MAR_XA(13), 6, S_ARMED_1),

    S_STAGE(STAGE_MAIN_TATARA_SHRINE),
    S_SECOND_FIGHTER(ROSTER_VICTIM, FIGHT_WEAPON_SAME),
    S_VICTIM(S_SCENE(24, END_XA(1), 5, S_ARMED | S_STORY_WEAPON_1 | S_LONG),
             S_SCENE(25, MAR_XA(26), 10, S_ARMED | S_STORY_WEAPON_1 | S_LONG),
             S_SCENE(26, MAR_XA(21), 7, S_ARMED | S_STORY_WEAPON_1 | S_LONG)),
    S_END,
};

static u8 g_story_script_isohachi[] = {
    S_STAGE(STAGE_SHAINTO_HQ),
    S_HENCHMEN(5, 0x10, 80, 17, 45),
    S_CUTSCENE(0, MAR_XA(23), 14, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(1, MAR_XA(23), 4, S_ARMED),

    S_STAGE(STAGE_FERRY_DOCK),
    S_HENCHMEN(4, 0x10, 44, 38, 173),
    S_FIGHT,
    S_OPPONENT(ROSTER_SUMINAGASHI, WEAPON_ID_LONG_SWORD, 0x50),
    S_CUTSCENE(2, MAR_XA(19), 14, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(3, MAR_XA(5), 11, S_ARMED),
    S_SECOND_FIGHTER(ROSTER_HONGOU, FIGHT_WEAPON_SAME),
    S_CUTSCENE(4, MAR_XA(27), 4, S_ARMED),

    S_STAGE(STAGE_BUILDING_ROOFTOP),
    S_INTERLUDE(ROSTER_HONGOU,
                S_BAND(5, 0x10, 137, 0, 71),
                S_FOE(ROSTER_MIKADO_STORY, WEAPON_ID_NAGINATA, 0x60),
                S_SCENE(5, MAR_XA(8), 9, S_ARMED),
                S_SCENE(6, MAR_XA(2), 6, S_ARMED),
                S_SCENE(7, MAR_XA(5), 3, S_ARMED),
                S_SCENE(8, MAR_XA(5), 1, S_ARMED),
                S_SCENE(9, MAR_XA(6), 13, S_ARMED)),

    S_STAGE(STAGE_FUTURE_DUMP_SITE),
    S_OPPONENT(ROSTER_TSUBAME, WEAPON_ID_LONG_GUN, 0x70),
    S_CUTSCENE(10, MAR_XA(19), 4, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(11, MAR_XA(20), 15, S_ARMED),

    S_STAGE(STAGE_MEIKYOKAN_DOJO),
    S_HENCHMEN(3, 0x30, 1, 1, 177),
    S_FIGHT,
    S_OPPONENT(ROSTER_KANNUKI_STORY, WEAPON_ID_BROADSWORD, 0x80),
    S_CUTSCENE(12, MAR_XA(13), 5, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(13, MAR_XA(5), 12, S_ARMED | S_STORY_WEAPON_1),
    S_SECOND_FIGHTER(ROSTER_GENGORO_STORY, FIGHT_WEAPON_SAME),
    S_CUTSCENE(14, MAR_XA(26), 2, S_ARMED | S_STORY_WEAPON_1),

    S_STAGE(STAGE_LOST_BRIDGE),
    S_INTERLUDE(ROSTER_GENGORO_STORY,
                S_BAND(5, 0x30, 113, 130, 2),
                S_FOE(ROSTER_TATSUMI_STORY, WEAPON_ID_KATANA, 0xA0),
                S_SCENE(15, MAR_XA(14), 11, S_ARMED),
                S_SCENE(16, MAR_XA(12), 6, S_ARMED),
                S_SCENE(17, MAR_XA(3), 2, S_ARMED),
                S_SCENE(18, MAR_XA(4), 6, S_ARMED),
                S_SCENE(19, MAR_XA(6), 14, S_ARMED)),

    S_STAGE(STAGE_TATARA_SHRINE_PATH),
    S_HENCHMEN(4, 0x30, 255, 130, 4),
    S_FIGHT,
    S_OPPONENT(ROSTER_UTSUSEMI, WEAPON_ID_NODACHI, 0xB0),
    S_CUTSCENE(20, MAR_XA(19), 12, S_ARMED),
    S_FIGHT,
    S_CUTSCENE(21, MAR_XA(5), 4, S_ARMED),

    S_STAGE(STAGE_TATARA_SHRINE),
    S_OPPONENT(ROSTER_VANISHER, WEAPON_ID_KATANA, 0xC0),
    S_CUTSCENE(22, MAR_XA(20), 11, S_ARMED | S_WHOLE_STAGE),
    S_FIGHT,
    S_CUTSCENE(23, MAR_XA(12), 2, S_ARMED_1),

    S_STAGE(STAGE_MAIN_TATARA_SHRINE),
    S_SECOND_FIGHTER(ROSTER_VICTIM, FIGHT_WEAPON_SAME),
    S_VICTIM(S_SCENE(24, END_XA(2), 3, S_ARMED | S_STORY_WEAPON_1 | S_LONG),
             S_SCENE(25, MAR_XA(27), 2, S_ARMED | S_STORY_WEAPON_1 | S_LONG),
             S_SCENE(26, MAR_XA(23), 8, S_ARMED | S_STORY_WEAPON_1 | S_LONG)),
    S_END,
};
/* clang-format on */

/* Each character's script. The characters the story cannot be played with
 * have the first. */
static u8 *g_story_scripts[32] = {
    g_story_script_kannuki,
    g_story_script_tatsumi,
    g_story_script_mikado,
    g_story_script_utsusemi,
    g_story_script_red_shadow,
    g_story_script_kannuki, /* 5: TSUBAME */
    g_story_script_sazanka,
    g_story_script_suminagashi,
    g_story_script_nightstalker,
    g_story_script_matsumushi,
    g_story_script_kannuki, /* 10: the vanisher */
    g_story_script_kannuki, /* 11: the Narukagami's ninja */
    g_story_script_gengoro,
    g_story_script_kaun,
    g_story_script_jo,
    g_story_script_hongou,
    g_story_script_kannuki, /* 16: KATZE */
    g_story_script_highwayman,
    g_story_script_chihiro,
    g_story_script_utamaru,
    g_story_script_tony_umeda,
    g_story_script_isohachi,
    g_story_script_kannuki, /* 22: the armoured one */
    g_story_script_kannuki, /* 23: the Shainto's ninja */
    g_story_script_kannuki, /* 24: the Narukagami's crest */
    g_story_script_kannuki, /* 25: the victim */
    g_story_script_kannuki, /* 26: the Shainto's crest */
    g_story_script_kannuki, /* 27: spare */
    g_story_script_kannuki, /* 28: spare */
    g_story_script_kannuki, /* 29: spare */
    g_story_script_kannuki, /* 30: spare */
    g_story_script_kannuki, /* 31: spare */
};
