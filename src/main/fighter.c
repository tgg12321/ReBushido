/* The fighter and what it moves by. One file of the original's, from
 * motion_GetMove at the latest to fight_UpdateLock: the jump tables of
 * those two are a padding word apart, which only one file's alignment
 * gives, and everything between them is the fighter. Three parts: the
 * motion files and stances, the fighter, and the blade lock. Where the
 * original's file began (somewhere after
 * snd_StartFightMusic in fight.c) is not known. */
#define INCLUDE_ASM_USE_MACRO_INC 1
#include "common.h"
#include "include_asm.h"
#include <psxsdk/inline_o.h>
#include <psxsdk/gtemac.h>
#include "bb2.h"

/* ---- motion ---- */

/* Motion files and stances (motion.h): loading the common file and the two
 * fighters' files, and finding a move in them, which for many moves depends
 * on the stance the fighter is in. */

MotionFile *g_motion_fighter_files[4];
MotionSections g_motion_common_sections;
MotionSections g_motion_fighter_sections[2];

/* Nothing that was loaded is any longer: the next fight reads its files
 * again. */
void motion_ForgetFiles(void) {
    /* FAKE: g_motion_loaded_sets[1], through a pointer */
    u16 *sets = g_motion_loaded_sets;

    if (sets[1] == MOTION_SET_PACK) {
        ndata_InvalidatePack();
    }
    g_motion_common_loaded = 0;
    sets[1] = 0;
    sets[0] = 0;
    g_motion_win_frames_loaded[1] = MOTION_NO_WIN_FRAMES;
    g_motion_win_frames_loaded[0] = MOTION_NO_WIN_FRAMES;
}

/* The room of the second fighter file is about to be used for something
 * else. */
void motion_DropChanbaraPack(void) {
    /* FAKE: g_motion_loaded_sets[1], through a pointer */
    u16 *sets = g_motion_loaded_sets;

    if (sets[1] == MOTION_SET_PACK) {
        ndata_InvalidatePack();
    }
    sets[1] = 0;
}

void motion_InitBuffers(void) {
    g_motion_win_frames[0] = (MotionFrame *)MOTION_WIN_FRAMES_ADDR;
    g_motion_win_frames[1] = (MotionFrame *)MOTION_WIN_FRAMES_2_ADDR;
    g_motion_common_file = MOTION_COMMON_FILE_ADDR;
    g_motion_fighter_files[0] = (MotionFile *)MOTION_FIGHTER_FILE_ADDR;
    g_motion_fighter_files[1] = (MotionFile *)SYS_STAGING_ADDR;
    motion_ForgetFiles();
}

/* Loads the file of the moves and motions every fighter has (COMMON.BBM)
 * and finds its parts. */
static void motion_LoadCommonFile(void) {
    s32 file;

    cdrom_StartRead(cdrom_GetFileIndex(CDROM_GROUP_MOTION, MOTION_FILE_COMMON),
                    g_motion_common_file);
    cdrom_WaitIdle();
    file = g_motion_common_file;
    /* four words of offsets after the first, then the moves */
    g_motion_common_sections.moves = (u16 *)ADDR(file + 0x14);
    g_motion_common_sections.entries =
        (u16 *)ADDR(file + *(s32 *)ADDR(file + 4));
    /* the third word */
    g_motion_common_sections.metadata =
        (u8 *)ADDR(file + *(s32 *)ADDR(file + 8));
    /* the fifth word; nothing reads the fourth */
    g_motion_common_sections.stream =
        (u32 *)ADDR(file + *(s32 *)ADDR(file + 0x10));
    g_motion_common_loaded = 1;
}

/* Loads what a fight between these two needs. The modes that can end on a
 * win pose get each character's; then each fighter's motion set is worked
 * out and its file loaded, unless a file already in memory is the one: a
 * file that is still wanted stays where it is, and two fighters with the
 * same set share one. */
void motion_LoadFighterFiles(
    s32 character_1, s32 weapon_1, s32 character_2, s32 weapon_2) {
    u16 sets_to_load[2];
    char unused[256]; /* nothing uses it */
    s32 i;
    /* FAKE: two locals: a character, then a place in g_motion_set_files */
    s32 work;

    if (g_motion_common_loaded == 0) {
        motion_LoadCommonFile();
    }

    if (g_fight_mode == FIGHT_MODE_VSCPU || g_fight_mode == FIGHT_MODE_VS ||
        g_fight_mode == FIGHT_MODE_NETWORK) {
        for (i = 0; i < 2; i++) {
            work = character_1;
            if (i != 0) {
                work = character_2;
            }
            if (g_motion_win_frames_loaded[i] != work) {
                g_motion_win_frames_loaded[i] = work;
                cdrom_StartReadAt(
                    cdrom_GetFileIndex(CDROM_GROUP_MOTION, MOTION_FILE_WIN),
                    (s32)g_motion_win_frames[i], work * MOTION_WIN_SECTORS,
                    MOTION_WIN_SECTORS);
                cdrom_WaitIdle();
            }
        }
    }

    sets_to_load[1] = 0;
    sets_to_load[0] = 0;
    {
        u16 set_1 =
            MOTION_SET(weapon_1, g_fighter_stance_kinds[character_1][weapon_1]);
        u16 set_2;

        if (g_fight_mode == FIGHT_MODE_CHANBARA) {
            /* the opponents fight with the player's motion */
            set_2 = set_1;
        } else {
            set_2 = MOTION_SET(
                weapon_2, g_fighter_stance_kinds[character_2][weapon_2]);
        }
#if RB_PORT
        /* a mod's weapon with a motion file of its own has a set of its
         * own (port_weapons.h) */
        if (g_fight_mode != FIGHT_MODE_CHANBARA) {
            set_2 = (u16)port_WeaponMotionSet(1, weapon_2, set_2);
        }
        set_1 = (u16)port_WeaponMotionSet(0, weapon_1, set_1);
        if (g_fight_mode == FIGHT_MODE_CHANBARA) {
            set_2 = set_1;
        }
#endif
        g_fighters[0].motion_set = set_1;
        g_fighters[1].motion_set = set_2;

        if (set_1 == set_2) {
            if (g_motion_loaded_sets[0] == set_2 ||
                g_motion_loaded_sets[1] == set_2) {
                return;
            }
            sets_to_load[0] = set_1;
        } else if (g_motion_loaded_sets[0] == set_1) {
            if (g_motion_loaded_sets[1] == set_2) {
                return;
            }
            sets_to_load[1] = set_2;
        } else if (g_motion_loaded_sets[0] == set_2) {
            if (g_motion_loaded_sets[1] == set_1) {
                return;
            }
            sets_to_load[1] = set_1;
        } else if (g_motion_loaded_sets[1] == set_2) {
            sets_to_load[0] = set_1;
        } else {
            sets_to_load[0] = set_1;
            sets_to_load[1] = set_2;
        }
    }

    for (i = 0; i < 2; i++) {
        if (sets_to_load[i] != 0) {
#if RB_PORT
            /* a mod's weapon's own file, read from the mod: none of the
             * disc's files is its */
            if (!port_WeaponReadMotions(
                    sets_to_load[i], g_motion_fighter_files[i]))
#endif
            {
                for (work = 0; g_motion_set_files[work].set != 0; work++) {
                    if (g_motion_set_files[work].set == sets_to_load[i]) {
                        break;
                    }
                }
                cdrom_StartRead(cdrom_GetFileIndex(CDROM_GROUP_MOTION,
                                                   work + MOTION_FILE_SETS),
                                (s32)g_motion_fighter_files[i]);
                cdrom_WaitIdle();
            }
            /* the moves follow the file's rows of per-stance moves */
            g_motion_fighter_sections[i].moves = (u16 *)ADDR(
                (s32)g_motion_fighter_files[i] + 0x6C +
                (g_motion_fighter_files[i]->stance_move_rows - 1) * 6);
            g_motion_fighter_sections[i].entries =
                (u16 *)ADDR((s32)g_motion_fighter_files[i] +
                            g_motion_fighter_files[i]->section_offsets[0]);
            g_motion_fighter_sections[i].metadata =
                (u8 *)ADDR((s32)g_motion_fighter_files[i] +
                           g_motion_fighter_files[i]->section_offsets[1]);
            g_motion_fighter_sections[i].cpu_scripts =
                (u16 *)ADDR((s32)g_motion_fighter_files[i] +
                            g_motion_fighter_files[i]->section_offsets[2]);
            g_motion_fighter_sections[i].stream =
                (u32 *)ADDR((s32)g_motion_fighter_files[i] +
                            g_motion_fighter_files[i]->section_offsets[3]);
            g_motion_loaded_sets[i] = sets_to_load[i];
        }
    }
}

/* Gives each decoder the stream of the file it reads: the common file's,
 * and each of the two motion-file slots that holds a file. */
void motion_OpenStreams(void) {
    motion_OpenStream(MOTION_DECODER_COMMON, g_motion_common_sections.stream);
    if (g_motion_loaded_sets[0]) {
        motion_OpenStream(
            MOTION_DECODER_FILE, g_motion_fighter_sections[0].stream);
    }
    /* In chanbara the room of the second fighter's motion file holds the
     * pack of opponent models instead, and g_motion_loaded_sets[1] is
     * MOTION_SET_PACK to say so (fight.c, motion_LoadChanbaraPack): there
     * is no stream, and g_motion_fighter_sections[1].stream is still the
     * null it was at power on. The console reads its kernel's memory at
     * address 0 and fills the second file's decoder with nonsense, which
     * nothing then reads: both fighters fight with the one motion set, so
     * motion_AttachFighter puts them both on the first file's decoder. A
     * PC has nothing at address 0 and stops the program (docs/PORT.md,
     * "Memory"), so the port leaves the decoder alone. */
#if RB_PORT
    if (g_motion_loaded_sets[1] != 0 &&
        g_motion_loaded_sets[1] != MOTION_SET_PACK) {
        motion_OpenStream(
            MOTION_DECODER_FILE + 1, g_motion_fighter_sections[1].stream);
    }
#else
    if (g_motion_loaded_sets[1]) {
        motion_OpenStream(
            MOTION_DECODER_FILE + 1, g_motion_fighter_sections[1].stream);
    }
#endif
}

/* The weapon class whose rules a set's fighter has: the set's own, or on
 * the port a mod's weapon's base where the set is its own motion file's
 * (port_weapons.h). */
#if RB_PORT
#define MOTION_SET_CLASS(set) port_WeaponMotionClass(set)
#else
#define MOTION_SET_CLASS(set) MOTION_SET_WEAPON(set)
#endif

/* Finds the file the fighter's motion set is in, and which of its three
 * stances are the two-sword and the sheathed one. Either can be used while
 * the fighter has the use of its arm, but not by chanbara's opponents short
 * of the last column of the ladder (two swords), nor by a fighter the story
 * has put in the player's place (sheathed). */
void motion_AttachFighter(s32 fighter_index) {
    s32 i = 0;
    Fighter *fighter = &g_fighters[fighter_index];
    s32 set = fighter->motion_set;
    u16 *loaded = g_motion_loaded_sets;

    for (; i < 2; i++, loaded++) {
        if (set == *loaded) {
            break;
        }
    }
    fighter->motion_slot = i;
    fighter->borrow_other_motion = 0;
    if (MOTION_SET_CLASS(fighter->motion_set) < MOTION_SET_SPECIAL_WEAPONS) {
        for (i = 0; i < STANCE_COUNT; i++) {
            s32 kind = MOTION_SET_STANCE_KIND(fighter->motion_set, i);

            if (kind == STANCE_KIND_TWO_SWORDS) {
                fighter->two_sword_stance = i;
                if (g_fight_mode != FIGHT_MODE_CHANBARA || fighter_index != 1 ||
                    g_chanbara_kind == kind) {
                    fighter->two_sword_ready = fighter->off_arm_ok;
                }
            } else if (kind == STANCE_KIND_SHEATHED) {
                fighter->sheathed_stance = i;
                if (g_fight_mode != FIGHT_MODE_STORY || g_story_victim == 0 ||
                    fighter_index != 0) {
                    fighter->sheathed_ready = fighter->off_arm_ok;
                }
            }
        }
    }
}

/* Moves a fighter on from a special stance to the next one. */
static void motion_SkipSpecialStance(Fighter *fighter) {
    s16 stance = fighter->stance;

    if (stance != fighter->two_sword_stance) {
        if (stance != fighter->sheathed_stance) {
            return;
        }
    }
    fighter->stance =
        (stance + 1) %
        g_motion_fighter_files[fighter->motion_slot]->stance_count;
}

/* The game's names of the move kinds (enum MoveKind, last to first; the
 * symbols carry the enum's English names) and of the motion files. Nothing
 * reads them: g_fighter_move_kind_names and g_motion_set_files point at
 * them. Each is padded to twelve bytes. */
const char g_str_move_kind_win[] = "WIN     ";
const char g_str_move_kind_paired_ed[] = "KARAMI_ED";
const char g_str_move_kind_run_ed[] = "RUN_ED  ";
const char g_str_move_kind_run_st[] = "RUN_ST  ";
const char g_str_move_kind_land[] = "CHAKUTI ";
const char g_str_move_kind_appear[] = "APPEAR  ";
const char g_str_move_kind_surrender_st[] = "KAISHAKU_ST";
const char g_str_move_kind_deflected[] = "HAJIKARE";
const char g_str_move_kind_lock_exit[] = "SERIEXIT";
const char g_str_move_kind_crouch[] = "SYAGAMI ";
const char g_str_move_kind_hom_ed[] = "HOM_ED  ";
const char g_str_move_kind_hom_at[] = "HOM_AT  ";
const char g_str_move_kind_hom_st[] = "HOM_ST  ";
const char g_str_move_kind_climb_attack[] = "ANOBORI ";
const char g_str_move_kind_stance_change[] = "KAMAE_KA";
const char g_str_move_kind_move[] = "MOVE    ";
const char g_str_move_kind_climb_ed[] = "NOBORI_E";
const char g_str_move_kind_climb_st[] = "NOBORI_S";
const char g_str_move_kind_lock_losing_2[] = "FURI2   ";
const char g_str_move_kind_lock_losing_1[] = "FURI1   ";
const char g_str_move_kind_lock_winning_2[] = "YURI2   ";
const char g_str_move_kind_lock_winning_1[] = "YURI1   ";
const char g_str_move_kind_lock_even[] = "GOKAKU  ";
const char g_str_move_kind_start[] = "START   ";
const char g_str_move_kind_arun[] = "ARUN    ";
const char g_str_move_kind_step[] = "STEP    ";
const char g_str_move_kind_walk[] = "WALK    ";
const char g_str_move_kind_ljump[] = "LJUMP   ";
const char g_str_move_kind_mjump[] = "MJUMP   ";
const char g_str_move_kind_sjump[] = "SJUMP   ";
const char g_str_move_kind_stance[] = "KAMAE   ";
const char g_str_move_kind_dth[] = "DTH     ";
const char g_str_move_kind_run[] = "RUN     ";
const char g_str_move_kind_dirt[] = "SUNA    ";
const char g_str_move_kind_paired[] = "KARAMI  ";
const char g_str_move_kind_reload[] = "RELOAD  ";
const char g_str_move_kind_lock[] = "SERI    ";
const char g_str_move_kind_deflect[] = "HAJI    ";
const char g_str_move_kind_block[] = "UKE     ";
const char g_str_move_kind_crouch_step[] = "SYASTEP ";
const char g_str_move_kind_subwep[] = "SUBWEP  ";
const char g_str_move_kind_drop[] = "ORI     ";
const char g_str_move_kind_get_up[] = "OKIAGARI";
const char g_str_move_kind_climb[] = "NOBORI  ";
const char g_str_move_kind_stagger[] = "KZRE    ";
const char g_str_move_kind_down[] = "KOROGARI";
const char g_str_move_kind_surrender[] = "KAISYAKU";
const char g_str_move_kind_end_game[] = "END_GAME";
const char g_str_move_kind_dam[] = "DAM     ";
const char g_str_move_kind_attack[] = "ATTACK  ";
const char g_str_move_kind_normal[] = "NORMAL  ";
const char g_str_move_kind_null[] = "NULL    ";
const char g_str_motion_file_y123[] = "Y123.BBM";
const char g_str_motion_file_n123[] = "N123.BBM";
const char g_str_motion_file_k123[] = "K123.BBM";
const char g_str_motion_file_t123[] = "T123.BBM";
const char g_str_motion_file_s234[] = "S234.BBM";
const char g_str_motion_file_s125[] = "S125.BBM";
const char g_str_motion_file_s124[] = "S124.BBM";
const char g_str_motion_file_s123[] = "S123.BBM";
const char g_str_motion_file_u235[] = "U235.BBM";
const char g_str_motion_file_u135[] = "U135.BBM";
const char g_str_motion_file_u134[] = "U134.BBM";
const char g_str_motion_file_u125[] = "U125.BBM";
const char g_str_motion_file_u124[] = "U124.BBM";
const char g_str_motion_file_u123[] = "U123.BBM";

/* The move a move id stands for (motion.h). A stance that is not ready (the
 * second sword is gone, the arm is hurt) is passed over; chanbara's
 * opponents never stay in a special stance. */
void *motion_GetMove(Fighter *fighter, s32 id, s16 *is_common_out) {
    s16 stance;
    s32 slot;

    fighter->blend_into_move = 0;
    *is_common_out = 0;
    if (id >= MOVE_ID_PER_STANCE &&
        id < MOVE_ID_PER_STANCE + MOVE_ID_PER_STANCE_COUNT) {
        return &g_motion_fighter_sections[fighter->motion_slot]
                    .moves[g_motion_fighter_files[fighter->motion_slot]
                               ->per_stance_moves[id - MOVE_ID_PER_STANCE]
                                                 [fighter->stance]];
    }
    switch (id) {
    case MOVE_ID_DEFAULT_STANCE:
        fighter->blend_into_move = 1;
        fighter->stance = fighter->default_stance;
        return &g_motion_fighter_sections[fighter->motion_slot]
                    .moves[g_motion_fighter_files[fighter->motion_slot]
                               ->stance_moves[fighter->default_stance]];
    case MOVE_ID_NEXT_STANCE:
        fighter->stance =
            (fighter->stance + 1) %
            g_motion_fighter_files[fighter->motion_slot]->stance_count;
    case MOVE_ID_STANCE:
    case MOVE_ID_STANCE_BLEND:
        if (id == MOVE_ID_STANCE_BLEND) {
            fighter->blend_into_move = 1;
        }
        stance = fighter->stance;
        if ((stance == fighter->two_sword_stance &&
             fighter->two_sword_ready == 0) ||
            (stance == fighter->sheathed_stance &&
             fighter->sheathed_ready == 0)) {
            fighter->stance =
                (fighter->stance + 1) %
                g_motion_fighter_files[fighter->motion_slot]->stance_count;
        } else if (
            g_fight_mode == FIGHT_MODE_CHANBARA && fighter->is_cpu != 0) {
            motion_SkipSpecialStance(fighter);
        }
        return &g_motion_fighter_sections[fighter->motion_slot]
                    .moves[g_motion_fighter_files[fighter->motion_slot]
                               ->stance_moves[fighter->stance]];
    case MOVE_ID_STANCE_CHANGE:
        /* the stance is not changed here: the move leads to
         * MOVE_ID_NEXT_STANCE */
        stance = (fighter->stance + 1) %
                 g_motion_fighter_files[fighter->motion_slot]->stance_count;
        if ((stance == fighter->two_sword_stance &&
             fighter->two_sword_ready == 0) ||
            (stance == fighter->sheathed_stance &&
             fighter->sheathed_ready == 0)) {
            stance = (stance + 1) %
                     g_motion_fighter_files[fighter->motion_slot]->stance_count;
        } else if (
            g_fight_mode == FIGHT_MODE_CHANBARA && fighter->is_cpu != 0 &&
            (stance == fighter->two_sword_stance ||
             stance == fighter->sheathed_stance)) {
            stance = (stance + 1) %
                     g_motion_fighter_files[fighter->motion_slot]->stance_count;
        }
        return &g_motion_fighter_sections[fighter->motion_slot]
                    .moves[g_motion_fighter_files[fighter->motion_slot]
                               ->stance_change_moves[fighter->stance][stance]];
    }
    if (id & MOVE_ID_OWN_FILE) {
        /* during a paired move the fighter that was caught plays from the
         * other's file */
        if (fighter->borrow_other_motion != 0) {
            slot = fighter->other->motion_slot;
        } else {
            slot = fighter->motion_slot;
        }
        return &g_motion_fighter_sections[slot].moves[id & MOVE_ID_INDEX];
    }
    *is_common_out = 1;
    return &g_motion_common_sections.moves[id];
}

void motion_ResetStance(s32 fighter_index) {
    g_fighters[fighter_index].stance = g_fighters[fighter_index].default_stance;
}

/* Standing in the stance the fighter is in. */
MoveScript *motion_GetStanceMove(s32 fighter_index) {
    s16 slot = g_fighters[fighter_index].motion_slot;
    s16 stance = g_fighters[fighter_index].stance;

    return (MoveScript *)&g_motion_fighter_sections[slot]
        .moves[g_motion_fighter_files[slot]->stance_moves[stance]];
}

MoveScript *motion_GetDefaultStanceMove(s32 fighter_index) {
    s16 slot = g_fighters[fighter_index].motion_slot;
    s16 stance = g_fighters[fighter_index].default_stance;

    return (MoveScript *)&g_motion_fighter_sections[slot]
        .moves[g_motion_fighter_files[slot]->stance_moves[stance]];
}

/* How the fighter comes on at the start of a practice or a team fight. */
MoveScript *motion_GetEntranceMove(s32 fighter_index) {
    return (MoveScript *)&g_motion_common_sections
        .moves[g_motion_fighter_files[g_fighters[fighter_index].motion_slot]
                   ->entrance_move];
}

/* The pose `character` strikes when it has won. */
MoveScript *motion_GetWinMove(s32 fighter_index, s32 character) {
    return (MoveScript *)&g_motion_common_sections
        .moves[g_motion_fighter_files[g_fighters[fighter_index].motion_slot]
                   ->win_moves[character]];
}

/* ---- fighter ---- */

/* The fighter (fighter.h): setting one up for a fight, and its frame: the
 * pad, the move it is in, where the move carries it, its model, and what
 * the frame leaves for the hit tests. Also where on a stage a fighter
 * starts. */

BodyPoint g_fighter_body_points[2][BODY_POINT_COUNT];
Fighter g_fighters[2];

/* How far a fighter can fall and land on its feet. */
#define FIGHTER_SAFE_FALL 4000
/* How much higher than its feet a ledge can be and still be climbed. */
#define FIGHTER_CLIMB_HEIGHT 2200
#define FIGHTER_GRAVITY 21
/* The most a run's drag may be (it is always 0). */
#define RUN_DRAG_MAX 0x400
/* A speed this small or smaller is halved each frame. */
#define SMALL_SPEED 15
/* The leaping attack: its lift, and the fall its aim is worked out for. */
#define LEAP_LIFT 250
#define LEAP_AIM_GRAVITY 31

/* fighter_CheckFooting: the ground is looked for from this far above the
 * feet to this far below; a fighter with none is moved this far a frame
 * at most, the way it was going or down the slope. */
#define FOOTING_CAST_ABOVE 200
#define FOOTING_CAST_BELOW 1300
#define FOOTING_STEP_MAX 64
/* fighter_CheckGround: how near under the feet the ground must be. */
#define GROUND_NEAR 500
/* fighter_FindGroundY: how far down the ground under a jump is looked
 * for. */
#define SHADOW_DEPTH 8000
/* An attack hit in its first frames meets the blow as a clash. */
#define FIGHTER_CLASH_FRAMES 8
#define FIGHTER_GUN_SHOTS 10
#define FIGHTER_GUN_RELOADS 9

/* Chanbara's opponents come in tens, each ten a rank (1 to 10) and each
 * rank a little faster, stronger and better played; the ninth ten are
 * apart. */
#define CHANBARA_RANK_SIZE 10
#define CHANBARA_ELITE_RANK 9
#define CHANBARA_SPEED_STEP 96
#define CHANBARA_POWER_STEP 64
#define CHANBARA_LEVEL_STEP 80
#define FIGHTER_SIDEARM_SHOTS 2
#define FIGHTER_STUN_CHARGES 2

/* Puts the fighter into a move: finds the move's motion, starts it at the
 * move's first frame and reads its marks. Where the pose's root is measured
 * from depends on the kind of move: a walk or a step keeps the heading to
 * the opponent it began with, so that the fighter circles. */
void fighter_StartMove(s32 fighter_index, MoveScript *move, s32 is_common) {
    Fighter *fighter = &g_fighters[fighter_index];
    s32 slot;
    if ((fighter->borrow_other_motion) != 0) {
        slot = fighter->other->motion_slot;
    } else {
        slot = fighter->motion_slot;
    }
    fighter->borrow_other_motion = 0;
    fighter->cur_move = move;
    {
        u16 motion_index = move->motion_index;
        fighter->motion_index = motion_index;
        if (is_common != 0) {
            u16 *motion = &g_motion_common_sections.entries[motion_index * 2];
            fighter->motion = motion;
            fighter->motion_head =
                g_motion_common_sections.metadata + motion[1];
        } else {
            u16 *motion =
                &g_motion_fighter_sections[slot].entries[motion_index * 2];
            fighter->motion = motion;
            fighter->motion_head =
                g_motion_fighter_sections[slot].metadata + motion[1];
        }
    }
    {
        MoveScript *cur_move = fighter->cur_move;
        u16 old_kind = fighter->move_kind;
        u8 *head = fighter->motion_head;
        fighter->cur_move_is_common = is_common;
        /* FAKE: an empty statement */
        do {
        } while (0);
        fighter->cur_move_slot = slot;
        {
            u8 first_frame = cur_move->first_frame;
            fighter->prev_move_kind = old_kind;
            {
                u8 *head_again = fighter->motion_head;
                fighter->move_frame_frac = 0;
                fighter->move_started = MOVE_START_SCRIPT;
                fighter->queued_move = NULL;
                fighter->same_frame = 0;
                fighter->move_frame = first_frame;
                /* FAKE: fighter->move_kind = head[MOTION_HEAD_KIND] */
                do {
                    fighter->move_kind = *head;
                } while (0);
                fighter->move_start_class =
                    head_again[MOTION_HEAD_TURN_AND_CLASS];
            }
        }
        {
            MoveScript *move_again = fighter->cur_move;
            s32 kind = fighter->move_kind;
            fighter->move_turn_mode = move_again->flags & MOVE_TURN_MODE;
            {
                s32 armed = 0;
                if (MOVE_IS_ATTACK(kind)) {
                    armed = 1;
                }
                fighter->attack_armed = armed;
            }
            fighter_ReadMarks(fighter);
            {
                u16 new_kind = fighter->move_kind;

                if (new_kind == MOVE_KIND_GET_UP) {
                    fighter->use_root_heading = 1;
                    fighter->root_heading = fighter->orientation.vy;
                } else if (new_kind == MOVE_KIND_ATTACK) {
                    if (fighter->use_root_heading == 0 &&
                        fighter->prev_move_kind != MOVE_KIND_RUN) {
                        fighter->root_heading = fighter->opponent_bearing;
                    }
                    fighter->use_root_heading = 0;
                } else if (
                    new_kind == MOVE_KIND_WALK || new_kind == MOVE_KIND_STEP) {
                    if (fighter->use_root_heading == 0 ||
                        (new_kind == MOVE_KIND_WALK &&
                         fighter->prev_move_kind == new_kind)) {
                        fighter->root_heading = fighter->opponent_bearing;
                    }
                    fighter->use_root_heading = 1;
                } else if (new_kind == MOVE_KIND_PAIRED) {
                    fighter->use_root_heading = 1;
                } else {
                    fighter->use_root_heading = 0;
                }
            }
            {
                u16 kind_again = fighter->move_kind;
                if (MOVE_IS_ATTACK(kind_again)) {
                    /* which attack button the command was for: 0 for the
                     * frontal attack */
                    fighter->attack_kind =
                        (fighter->command_taken & MOVE_COMMAND_BUTTON) !=
                        MOVE_COMMAND_BUTTON_FRONTAL;
                }
            }
        }
    }
}

#if RB_PORT
/* (The native port.) fighter_Update below starts a move as
 *
 *     fighter_StartMove(index, motion_GetMove(fighter, id,
 *                                             &fighter->move_is_common),
 *                       fighter->move_is_common);
 *
 * and means the third argument to be what motion_GetMove has just
 * written: whether the move it found is in the common file. C does not
 * say which argument is worked out first. The PlayStation's compiler
 * makes the call first and reads the member after it; a PC's reads the
 * member first, and then starts a fighter's own move with the common
 * file's motion, or the other way about (docs/PORT.md, "What the
 * PlayStation's compiler decided"). So in the port fighter_StartMove is
 * given where the flag is, and reads it when every argument has been
 * worked out: from here to the end of this file, without a line of
 * fighter_Update changed. */
static void fighter_StartMoveAfterLookup(
    s32 fighter_index, MoveScript *move, const s16 *is_common) {
    fighter_StartMove(fighter_index, move, *is_common);
}

#define fighter_StartMove(fighter_index, move, is_common)                      \
    fighter_StartMoveAfterLookup(fighter_index, move, &(is_common))
#endif

/* ---- where the fighters start ----
 *
 * A stage has four pairs of start positions, a point for each fighter: 24
 * halfwords a stage in the table fight_GetStartPositions returns
 * (g_fight_start_positions, as halfwords). */

/* The start position of one fighter in one of the stage's pairs. */
void fight_GetStartPosition(
    s32 fighter_index, s32 *position_out, s32 pair_index) {
    s16 *start_position;
    start_position = (s16 *)fight_GetStartPositions() +
                     ((g_fight_stage * FIGHT_START_STAGE_SIZE) +
                      (pair_index * FIGHT_START_PAIR_SIZE) +
                      (fighter_index * FIGHT_START_POINT_SIZE));
    position_out[0] = start_position[0];
    position_out[1] = start_position[1];
    position_out[2] = start_position[2];
}

/* fight_FindPlaceNear: how far from the fighter the place is, in how many
 * directions it is looked for, and how far the ground may be below. */
#define PLACE_DISTANCE 4000
#define PLACE_DIRECTIONS 8
#define PLACE_STEPS 40
#define PLACE_DROP 4000
/* the casts are made from this far above the feet */
#define PLACE_CAST_HEIGHT 500

/* A place for a new opponent to come on, some way from the fighter that is
 * already there (practice and chanbara): 4000 units from it in one of
 * eight directions, tried in a random order, that can be walked to without
 * meeting a wall and has level ground under it. If none will do, the
 * second point of a start pair: chanbara's own, or the pair nearest
 * it. `fighter_index` is not used. */
static void fight_FindPlaceNear(
    s32 fighter_index, Vec3 *place_out, s32 *other_position) {
    Vec3 start;
    Vec3 reached;
    Vec3 segment_end;
    Vec3 hit_point;
    s16 hit_normal[4];
    s16 *start_position_cursor;
    s32 first_direction;
    s32 start_angle;
    s32 i;
    /* a step on the way out, a step on the way down, then a start pair */
    s32 step_or_pair;
    s32 angle;
    s32 dx;
    s32 dz;
    s32 best_dist_sq;
    s32 dist_sq;

    start_position_cursor = fight_GetStartPositions();
    start_angle = rand();
    first_direction = rand() & (PLACE_DIRECTIONS - 1);
    start.x = other_position[0];
    start.y = other_position[1] - PLACE_CAST_HEIGHT;
    start.z = other_position[2];
    for (i = 0; i < PLACE_DIRECTIONS; i++) {
        /* eighths of a turn, three at a time: every direction once */
        angle = (start_angle + ((first_direction + i * 3) << 9)) & ANGLE_MASK;
        dx = (g_math_sin_table[(angle + ANGLE_TURN / 4) & ANGLE_MASK] *
              PLACE_DISTANCE) /
             ONE;
        dz = (g_math_sin_table[angle] * PLACE_DISTANCE) / ONE;
        segment_end.x = start.x + dx;
        segment_end.y = start.y;
        segment_end.z = start.z + dz;
        if (collision_CastWith(&start.x, &segment_end.x, &hit_point.x,
                               hit_normal, FIGHT_CAST_WORK) != 0) {
            continue;
        }
        reached = start;
        /* out, in forty steps: as far as the way is clear */
        for (step_or_pair = 1; step_or_pair <= PLACE_STEPS; step_or_pair++) {
            segment_end.y = start.y;
            segment_end.x = start.x + (dx * step_or_pair) / PLACE_STEPS;
            segment_end.z = start.z + (dz * step_or_pair) / PLACE_STEPS;
            if (collision_CastMoveWith(&reached.x, &segment_end.x, &hit_point.x,
                                       hit_normal, FIGHT_CAST_WORK) != 0) {
                break;
            }
            *place_out = segment_end;
            reached = *place_out;
        }
        segment_end = *place_out;
        /* level ground within PLACE_DROP below */
        segment_end.y += PLACE_DROP;
        if (collision_CastWith(&place_out->x, &segment_end.x, &hit_point.x,
                               hit_normal, FIGHT_CAST_WORK) == 0) {
            continue;
        }
        if (hit_normal[1] != -ONE) {
            continue;
        }
        reached = *place_out;
        /* down, in forty steps, to stand on it */
        for (step_or_pair = 1; step_or_pair <= PLACE_STEPS; step_or_pair++) {
            segment_end.y =
                place_out->y + step_or_pair * (PLACE_DROP / PLACE_STEPS);
            if (collision_CastMoveWith(&reached.x, &segment_end.x, &hit_point.x,
                                       hit_normal, FIGHT_CAST_WORK) != 0 &&
                hit_normal[1] == -ONE) {
                place_out->y = hit_point.y;
                return;
            }
            reached.y = segment_end.y;
        }
    }
    if (g_fight_mode == FIGHT_MODE_CHANBARA) {
        step_or_pair = g_fight_start_pair;
    } else {
        best_dist_sq = 0x7FFFFFFF; /* farther than any */
        start_position_cursor += g_fight_stage * FIGHT_START_STAGE_SIZE;
        for (i = 0, step_or_pair = 0; i < FIGHT_START_PAIRS; i++) {
            /* the pair's second point: its x and z */
            dx = start_position_cursor[i * FIGHT_START_PAIR_SIZE +
                                       FIGHT_START_POINT_SIZE] -
                 other_position[0];
            dz = start_position_cursor[i * FIGHT_START_PAIR_SIZE +
                                       FIGHT_START_POINT_SIZE + 2] -
                 other_position[2];
            dist_sq = dx * dx + dz * dz;
            if (dist_sq < best_dist_sq) {
                best_dist_sq = dist_sq;
                step_or_pair = i;
            }
        }
    }
    start_position_cursor +=
        step_or_pair * FIGHT_START_PAIR_SIZE + FIGHT_START_POINT_SIZE;
    place_out->x = start_position_cursor[0];
    place_out->y = start_position_cursor[1];
    place_out->z = start_position_cursor[2];
}

/* A start position for story mode's next opponent: one of the two pairs
 * that are neither the nearest to the fighter already there nor the
 * farthest. `fighter_index` is not used. */
static void fight_PickStartAwayFrom(
    s32 fighter_index, s32 *position_out, s32 *other_position) {
    s32 dist_sq[4];
    s32 picks[2];
    s16 *starts;
    s32 dx;
    s32 dz;
    s32 i;
    s32 best;
    s32 j;

    /* (the stage's 24 halfwords, as three times eight) */
    starts = (s16 *)fight_GetStartPositions() + (g_fight_stage * 3) * 8;
    for (i = 0; i < FIGHT_START_PAIRS; i++) {
        /* the pair's second point: its x and z */
        dx = starts[i * FIGHT_START_PAIR_SIZE + FIGHT_START_POINT_SIZE] -
             other_position[0];
        dz = starts[i * FIGHT_START_PAIR_SIZE + FIGHT_START_POINT_SIZE + 2] -
             other_position[2];
        dist_sq[i] = dx * dx + dz * dz;
    }

    best = 0;
    for (i = 1; i < FIGHT_START_PAIRS; i++) {
        if (dist_sq[i] < dist_sq[best]) {
            best = i;
        }
    }
    dist_sq[best] = -1;

    best = 0;
    for (i = 1; i < FIGHT_START_PAIRS; i++) {
        if (dist_sq[i] > dist_sq[best]) {
            best = i;
        }
    }
    dist_sq[best] = -1;

    for (i = 0, j = 0; i < FIGHT_START_PAIRS; i++) {
        if (dist_sq[i] != -1) {
            picks[j++] = i;
        }
    }

    starts +=
        picks[rand() & 1] * FIGHT_START_PAIR_SIZE + FIGHT_START_POINT_SIZE;
    position_out[0] = starts[0];
    position_out[1] = starts[1];
    position_out[2] = starts[2];
}

/* The start pair whose middle is nearest a place. */
s32 fight_FindNearestStartPair(Vec3 *position) {
    s16 *pair_table;
    s32 i;
    s32 best_dist_sq;
    s32 best;
    s32 x;
    s32 z;
    s32 dx;
    s32 dz;
    s32 dist_sq;

    pair_table = fight_GetStartPositions();
    best_dist_sq = 0x7FFFFFFF; /* farther than any */
    x = position->x;
    z = position->z;
    pair_table += g_fight_stage * FIGHT_START_STAGE_SIZE;
    for (i = 0; i < FIGHT_START_PAIRS; i++) {
        /* the x and the z of its two points */
        dx = (pair_table[i * FIGHT_START_PAIR_SIZE + 0] +
              pair_table[i * FIGHT_START_PAIR_SIZE + FIGHT_START_POINT_SIZE]) /
                 2 -
             x;
        dz = (pair_table[i * FIGHT_START_PAIR_SIZE + 2] +
              pair_table[i * FIGHT_START_PAIR_SIZE + FIGHT_START_POINT_SIZE +
                         2]) /
                 2 -
             z;
        dist_sq = dx * dx + dz * dz;
        if (dist_sq < best_dist_sq) {
            best_dist_sq = dist_sq;
            best = i;
        }
    }
    return best;
}

/* The stance chanbara's opponent begins in (motion.h): the first of its
 * three that is of a kind its column of the ladder would rather have. */
static s32 motion_PickChanbaraStance(Fighter *fighter) {
    u8 *wanted;
    s32 kinds;
    s32 i;
    s32 j;

    wanted = g_motion_chanbara_stance_kinds[g_chanbara_kind];
    kinds = g_fighter_stance_kinds[fighter->other->character]
                                  [fighter->other->weapon_id];
    for (j = 0; j < 2; j++) {
        for (i = 0; i < STANCE_COUNT; i++) {
            if (wanted[j] == MOTION_SET_STANCE_KIND(kinds, i)) {
                return i;
            }
        }
    }
    return 0;
}

/* ---- set-up ---- */

void fighter_HealWounds(Fighter *fighter) {
    fighter->off_arm_ok = 1;
    fighter->leg_wounds = 0;
    fighter->arm_wounds = 0;
    fighter->body_wounds = 0;
}

/* Sets a fighter up for a fight (fighter.h): who it is and what it holds,
 * how fast and strong it is, where it stands, facing the other; unwounded,
 * unless it is story mode's player coming to a later stage. Chanbara's
 * opponents get faster, stronger and cleverer with every ten beaten. */
void fighter_Init(s32 fighter_index, s32 is_cpu, s32 roster_id, s32 weapon,
                  s32 place_near_other) {
    Fighter *fighter;
    Vec3 other_position;
    s32 rank; /* chanbara: 1 for the first ten opponents, to 10 */
    s32 bearing;
    s32 i;
    s32 pair_index;

    fighter = &g_fighters[fighter_index];
    g_fight_falling_fighter = FIGHT_NOBODY_FALLING;
    g_fight_paired_move_start_pending = 0;
    fighter->frames_alive = 0;
    fighter->other = (fighter_index != 0) ? &g_fighters[0] : &g_fighters[1];
    fighter->index = fighter_index;
    fighter->is_cpu = is_cpu;
    fighter->roster_id = roster_id;
    fighter->character = g_fighter_roster_characters[roster_id];
    fighter->weapon_id = weapon;

    if (g_fight_mode == FIGHT_MODE_TEAM ||
        (g_fight_mode == FIGHT_MODE_PRACTICE && g_fight_practice_round == 0)) {
        fighter->weapon_model =
            g_fighter_practice_weapon_models[fighter->weapon_id];
    } else if (fighter_index == 1 && g_fight_mode == FIGHT_MODE_CHANBARA) {
        fighter->weapon_model = g_fighter_clan_weapon_models
            [fighter->weapon_id]
            [g_fighter_clans[fighter->other->character] == 0];
    } else if (g_fight_mode == FIGHT_MODE_STORY && g_story_victim != 0 &&
               fighter_index == 0) {
        fighter->weapon_model = WEAPON_MODEL_STORY;
    } else {
        fighter->weapon_model =
            g_fighter_clan_weapon_models[fighter->weapon_id]
                                        [g_fighter_clans[fighter->character]];
    }
#if RB_PORT
    /* a mod's weapon (port_weapons.h): the game's own with none */
    fighter->weapon_model = port_WeaponModel(
        fighter_index, fighter->weapon_id, fighter->weapon_model);
#endif

    if (fighter_index == 1 && g_fight_mode == FIGHT_MODE_CHANBARA) {
        fighter->size_scale = g_model_scales[g_chanbara_model];
    } else {
        fighter->size_scale =
            g_model_scales[g_fighter_roster_models[fighter->roster_id]];
    }
    fighter->base_speed =
        g_fighter_speeds[fighter->character][fighter->weapon_id];
    fighter->base_power =
        g_fighter_powers[fighter->character][fighter->weapon_id];
#if RB_PORT
    fighter->base_speed = port_WeaponSpeed(
        fighter_index, fighter->weapon_id, fighter->base_speed);
    fighter->base_power = port_WeaponPower(
        fighter_index, fighter->weapon_id, fighter->base_power);
#endif
    fighter->power = fighter->base_power;
    fighter->cpu_base_level = CPU_LEVEL_MAX;

    if (g_fight_mode == FIGHT_MODE_CHANBARA && fighter_index == 1) {
        fighter->default_stance = motion_PickChanbaraStance(fighter);
        rank = (g_chanbara_count - 1) / CHANBARA_RANK_SIZE + 1;
        /* From half speed, three quarters of full power and a low level,
         * each a little more with every rank. The ninth ten have values of
         * their own, above those of the last ten. */
        fighter->base_speed =
            rank * CHANBARA_SPEED_STEP +
            ((rank == CHANBARA_ELITE_RANK) ? 5 * FIGHTER_SPEED_ONE / 8
                                           : FIGHTER_SPEED_ONE / 2);
        if (rank == CHANBARA_ELITE_RANK) {
            fighter->power = 0x1080; /* a little over full power */
        } else {
            fighter->power = rank * CHANBARA_POWER_STEP + 3 * ONE / 4;
        }
        if (rank == CHANBARA_ELITE_RANK) {
            /* seven eighths of the highest */
            fighter->cpu_base_level = 7 * CPU_LEVEL_MAX / 8;
        } else {
            /* from a low level */
            fighter->cpu_base_level = rank * CHANBARA_LEVEL_STEP + 0x150;
        }
    } else {
        fighter->default_stance =
            g_fighter_default_stances[fighter->character][fighter->weapon_id];
#if RB_PORT
        fighter->default_stance = port_WeaponDefaultStance(
            fighter_index, fighter->weapon_id, fighter->default_stance);
#endif
    }

    fighter->two_sword_stance = -1;
    fighter->two_sword_ready = 0;
    fighter->sheathed_stance = -1;
    fighter->sheathed_ready = 0;
    pair_index = (g_fight_restart != 0) ? g_fight_restart_pair : 0;

    switch (g_fight_mode) {
    case FIGHT_MODE_STORY:
        if (place_near_other != 0) {
            other_position = fighter->other->base_pos;
            fight_PickStartAwayFrom(
                fighter_index, &fighter->base_pos.x, &other_position.x);
        } else {
            fight_GetStartPosition(
                fighter_index, &fighter->base_pos.x, pair_index);
            fight_GetStartPosition(
                fighter_index == 0, &other_position.x, pair_index);
        }
        break;
    case FIGHT_MODE_PRACTICE:
    case FIGHT_MODE_CHANBARA:
        if (place_near_other != 0) {
            other_position = fighter->other->base_pos;
            fight_FindPlaceNear(
                fighter_index, &fighter->base_pos, &other_position.x);
        } else {
            fight_GetStartPosition(
                fighter_index, &fighter->base_pos.x, g_fight_start_pair);
            fight_GetStartPosition(
                fighter_index == 0, &other_position.x, g_fight_start_pair);
        }
        break;
    default:
        fight_GetStartPosition(fighter_index, &fighter->base_pos.x, pair_index);
        fight_GetStartPosition(
            fighter_index == 0, &other_position.x, pair_index);
        break;
    }

    fighter->root_offset.x = 0;
    fighter->root_offset.y = -FIGHTER_WAIST_HEIGHT;
    fighter->root_offset.z = 0;
    fighter->world_pos.x = fighter->base_pos.x + fighter->root_offset.x;
    fighter->world_pos.y = fighter->base_pos.y + fighter->root_offset.y;
    fighter->world_pos.z = fighter->base_pos.z + fighter->root_offset.z;
    other_position.x += fighter->root_offset.x;
    other_position.y += fighter->root_offset.y;
    other_position.z += fighter->root_offset.z;
    fighter->foot_pos.vx = fighter->world_pos.x;
    fighter->foot_pos.vy = fighter->base_pos.y;
    fighter->foot_pos.vz = fighter->world_pos.z;
    fighter->prev_foot_pos = fighter->foot_pos;
    fighter->prev_root_offset = fighter->root_offset;
    fighter->velocity.vx = 0;
    fighter->velocity.vy = 0;
    fighter->velocity.vz = 0;
    fighter->prev_velocity = fighter->velocity;
    fighter->blade_motion[0].vx = 0;
    fighter->blade_motion[0].vy = 0;
    fighter->blade_motion[0].vz = 0;
    fighter->blade_motion[1].vx = 0;
    fighter->blade_motion[1].vy = 0;
    fighter->blade_motion[1].vz = 0;
    fighter->push.vx = 0;
    fighter->push.vy = 0;
    fighter->push.vz = 0;
    fighter->unk_144 = 0;
    fighter->ground_y = fighter->foot_pos.vy;
    fighter->turn_left = 0;
    fighter->run_drag = 0;
    fighter->use_root_heading = 0;
    fighter->run_speed = 0;
    /* three quarters of a speed is kept each frame; of a fall, nearly all */
    fighter->velocity_damping_x = 3 * ONE / 4;
    fighter->velocity_damping_y = 31 * ONE / 32;
    fighter->velocity_damping_z = 3 * ONE / 4;
    fighter->push_damping_x = 3 * ONE / 4;
    fighter->push_damping_y = 3 * ONE / 4;
    fighter->push_damping_z = 3 * ONE / 4;
    fighter->aim_lean = 0;
    fighter->aim_turn = 0;
    fighter->gun_aim = 0;
    fighter->gun_pos_valid = 0;
    fighter->camera_target = fighter->world_pos;
    fighter->hip_pos = fighter->world_pos;
    fighter->head_pos = fighter->world_pos;
    fighter->chest_pos = fighter->world_pos;
    fighter->orientation.vx = 0;
    fighter->orientation.vy = 0;
    fighter->orientation.vz = 0;
    bearing = ratan2(other_position.x - fighter->world_pos.x,
                     other_position.z - fighter->world_pos.z);
    fighter->orientation.vy = fighter->opponent_bearing = bearing;
    if (fighter->roster_id == ROSTER_VICTIM) {
        /* its back to the other */
        fighter->orientation.vy = bearing + ANGLE_TURN / 2;
    }
    fighter->initial_orientation = fighter->orientation;
    fighter_ScaleBodyPoints(fighter_index, fighter->size_scale);
    if (g_fight_mode != FIGHT_MODE_STORY || fighter_index != 0 ||
        g_story_chapter_fights < 2 || g_fight_restart != 0) {
        fighter_HealWounds(fighter);
    }
#if RB_PORT
    /* a Stock or Crew match's fight won before: the winner comes in with
     * its wounds (port_match.h) */
    port_MatchPutWounds(
        fighter_index, &fighter->off_arm_ok, &fighter->leg_wounds,
        &fighter->arm_wounds, &fighter->body_wounds);
#endif
    fighter->lost_arm_speed = g_fighter_lost_arm_speeds[fighter->character];
    for (i = 0; i < 4; i++) {
        fighter->arm_wound_speeds[i] =
            g_fighter_arm_wound_speeds[fighter->character][i];
        fighter->wound_speeds[i] =
            g_fighter_wound_speeds[fighter->character][i];
    }
    fighter->clash_frames = FIGHTER_CLASH_FRAMES;
    fighter->queued_move = NULL;
    fighter->pending_event = MOVE_EVENT_NONE;
    fighter->blending = 0;
    fighter->on_ground = 0;
    fighter->airborne = 0;
    fighter->is_defeated = 0;
    fighter->surface = SURFACE_BARRIER;
    fighter->surface_class = 0;
    fighter->vanish_count = 0;
    object_InitSubWeapons(fighter);
    fighter->sub_weapon_kind = fighter->sub_weapons[0];
    if (g_fight_mode == FIGHT_MODE_TEAM ||
        (g_fight_mode == FIGHT_MODE_PRACTICE && g_fight_practice_round == 0)) {
        if (CHARACTER_HAS_SECOND_SWORD(fighter->character)) {
            fighter->sub_weapons[0] = fighter->sub_weapon_kind =
                OBJECT_PRACTICE_SWORD;
        } else {
            fighter->sub_weapon_kind = OBJECT_NONE;
            fighter->sub_weapon_count = 0;
        }
    } else if (g_fight_mode == FIGHT_MODE_CHANBARA && fighter_index == 1 &&
               g_chanbara_kind != CHANBARA_LAST_KIND) {
        fighter->sub_weapon_kind = OBJECT_NONE;
        fighter->sub_weapon_count = 0;
    } else if (g_fight_mode == FIGHT_MODE_CHANBARA && fighter_index == 1 &&
               g_chanbara_kind == CHANBARA_LAST_KIND) {
        /* chanbara's last opponent carries what the player does */
        if (CHARACTER_HAS_SECOND_SWORD(fighter->other->character)) {
            fighter->sub_weapons[0] = fighter->sub_weapon_kind =
                fighter->other->sub_weapon_kind;
            fighter->sub_weapon_count = 1;
        }
    } else {
        if (fighter->roster_id == ROSTER_SIDEARM_2) {
            fighter->sub_weapon_kind = OBJECT_OFF_HAND_GUN_2;
            fighter->ammo = FIGHTER_SIDEARM_SHOTS;
        } else if (fighter->roster_id == ROSTER_SIDEARM_1) {
            fighter->sub_weapon_kind = OBJECT_OFF_HAND_GUN_1;
            fighter->ammo = FIGHTER_SIDEARM_SHOTS;
        } else {
            fighter->ammo = FIGHTER_GUN_SHOTS;
        }
        fighter->reloads_left = FIGHTER_GUN_RELOADS;
    }
    if (g_fight_mode == FIGHT_MODE_TEAM ||
        g_fight_mode == FIGHT_MODE_PRACTICE ||
        g_fight_mode == FIGHT_MODE_CHANBARA ||
        (g_fight_mode == FIGHT_MODE_STORY && g_story_victim != 0)) {
        fighter->stun_charges = 0;
    } else {
        fighter->stun_charges = FIGHTER_STUN_CHARGES;
    }
    fighter->bleed_timer = 0;
}

/* Sets up the fighters the CPU plays: how good each is (from the story
 * script, or from the fight's settings; chanbara's was set with the
 * fighter), and its scripts and the opponent's, from their motion files. */
void cpu_InitFighters(void) {
    s32 i;
    u16 *level;

    for (i = 0, level = g_fight_config.cpu_levels; i < 2; level++, i++) {
        Fighter *fighter = &g_fighters[i];

        if (fighter->is_cpu != 0) {
            switch (g_fight_mode) {
            case FIGHT_MODE_STORY:
                /* the script's level is in 256ths */
                fighter->cpu_base_level =
                    g_fight_config.story_cpu_levels[i] << 4;
                break;
            case FIGHT_MODE_VSCPU:
            case FIGHT_MODE_PRACTICE:
            default:
                fighter->cpu_base_level = *level;
                break;
            case FIGHT_MODE_CHANBARA:
                break;
            }
            cpu_Init(
                i, g_motion_fighter_sections[fighter->motion_slot].cpu_scripts,
                g_motion_fighter_sections[fighter->other->motion_slot]
                    .cpu_scripts);
        }
    }
}

/* ---- moving through the stage ---- */

/* What the fighter's casts against the stage keep on the scratchpad, below
 * the casts' own work area: where a cast hit and the surface's normal
 * there, and the cast's far end. Moving the fighter also keeps the normal
 * of its second, short cast (never read) and what is left of the move. */
typedef struct {
    VECTOR hit;            /* 0x00 */
    SVECTOR normal;        /* 0x10 */
    SVECTOR step_normal;   /* 0x18 */
    VECTOR remaining_move; /* 0x20 */
    VECTOR segment_end;    /* 0x30 */
} FighterCastScratch;

#define FIGHTER_CAST_SCRATCH ((FighterCastScratch *)(SCRATCHPAD + 0x1B0))

/* Moves the fighter's feet from `feet` towards `target` and leaves them where
 * they get to. When the way meets a surface the fighter stops there, loses
 * the speed it had into the surface, steps a little way off it, and slides
 * what is left of the move along it (half as freely while standing in a
 * stance); a wall does not lift it unless it is climbing or in the air. Up
 * to four surfaces a frame. */
static void fighter_MoveThroughStage(
    Fighter *fighter, VECTOR *feet, VECTOR *target, s32 *velocity) {
    FighterCastScratch *scratch = FIGHTER_CAST_SCRATCH;
    s32 surfaces_met = 0;
    do {
        if (((feet->vx != target->vx) || (feet->vy != target->vy)) ||
            (feet->vz != target->vz)) {
            if (collision_CastMoveWith(
                    &feet->vx, &target->vx, &scratch->hit.vx,
                    &scratch->normal.vx, FIGHT_CAST_WORK) == 0) {
                *feet = *target;
                break;
            }
            fighter->surface = collision_GetSurface();
            *feet = scratch->hit;
            /* into the surface, a quarter of the speed comes back; along
             * it, nine tenths are kept */
            math_ScaleAlong(velocity, &scratch->normal.vx, velocity,
                            -SCALE_ALONG_ONE / 4, 9 * SCALE_ALONG_ONE / 10);
            /* a step off the surface: four units along its normal */
            scratch->segment_end.vx =
                feet->vx + (scratch->normal.vx / (ONE / 4));
            scratch->segment_end.vy =
                feet->vy + (scratch->normal.vy / (ONE / 4));
            scratch->segment_end.vz =
                feet->vz + (scratch->normal.vz / (ONE / 4));
            target->vx += scratch->normal.vx / (ONE / 4);
            target->vy += scratch->normal.vy / (ONE / 4);
            target->vz += scratch->normal.vz / (ONE / 4);
            if (collision_CastMoveWith(
                    &feet->vx, &scratch->segment_end.vx, &scratch->hit.vx,
                    &scratch->step_normal.vx, FIGHT_CAST_WORK) == 0) {
                *feet = scratch->segment_end;
                /* what is left of the move, along the surface */
                scratch->remaining_move.vx = target->vx - feet->vx;
                scratch->remaining_move.vy = target->vy - feet->vy;
                scratch->remaining_move.vz = target->vz - feet->vz;
                math_ScaleAlong(
                    &scratch->remaining_move.vx, &scratch->normal.vx,
                    &scratch->remaining_move.vx, 0,
                    (fighter->move_kind == MOVE_KIND_STANCE)
                        ? SCALE_ALONG_ONE / 2
                        : SCALE_ALONG_ONE);
                /* a wall */
                if (scratch->normal.vy > FIGHTER_FLOOR_NORMAL_Y) {
                    u16 kind = fighter->move_kind;
                    if ((kind != MOVE_KIND_CLIMB) &&
                        (kind != MOVE_KIND_CLIMB_ST) &&
                        (kind != MOVE_KIND_MJUMP) &&
                        (kind != MOVE_KIND_LJUMP) && (kind != MOVE_KIND_DROP) &&
                        (fighter->airborne == 0)) {
                        scratch->remaining_move.vy = 0;
                    }
                }
                target->vx = feet->vx + scratch->remaining_move.vx;
                target->vy = feet->vy + scratch->remaining_move.vy;
                surfaces_met++;
                target->vz = feet->vz + scratch->remaining_move.vz;
            } else {
                break;
            }
        } else {
            break;
        }
    } while (surfaces_met < 4);
}

/* fighter_FindLedge: where the wall is looked for, how far below the
 * height a fighter pulls itself up to the top may be, and how far in over
 * the wall the top is looked for. */
#define LEDGE_KNEE_HEIGHT 100
#define LEDGE_TOP_DEPTH 400
#define LEDGE_TOP_REACH 512

/* Looks for a ledge to climb in the direction the pad is held: a wall
 * within reach at knee height, nothing in the way above it at the height a
 * fighter can pull itself up to, and ground on top. Gives the heading that
 * faces the wall. */
s32 fighter_FindLedge(Fighter *fighter, s32 *heading_out) {
    s32 from[3];
    s32 to[3];
    SVECTOR normal;
    s32 hit[4];
    u32 held;
    /* the pad's direction as a place in g_fighter_pad_headings */
    s32 input_column;
    s32 input_row;

    held = fighter->pad.held;
    /* side A: 0, neither: 1, side B: 2 (bit 14 is FIGHTER_PAD_SIDE_B) */
    input_column = (held >> 14) & 1;
    if (!(held & FIGHTER_PAD_SIDE_A)) {
        input_column++;
    }
    /* towards: 0, neither: 1, away: 2 (bit 15 is FIGHTER_PAD_AWAY) */
    input_row = (held >> 15) & 1;
    if (!(held & FIGHTER_PAD_TOWARD)) {
        input_row++;
    }

    {
        s32 x;
        s32 bearing_offset;

        x = fighter->foot_pos.vx;
        bearing_offset = g_fighter_pad_headings[input_row][input_column];

        from[0] = x;
        from[1] = fighter->foot_pos.vy - LEDGE_KNEE_HEIGHT;
        from[2] = fighter->foot_pos.vz;

        {
            s32 angle =
                (fighter->opponent_bearing + bearing_offset) & ANGLE_MASK;
            s16 probe_bearing_sin = g_math_sin_table[angle];

            /* 1024 units that way */
            to[0] = x + probe_bearing_sin / 4;
            to[1] = from[1];
        }

        {
            s32 angle2 =
                (fighter->opponent_bearing + bearing_offset + ANGLE_TURN / 4) &
                ANGLE_MASK;
            s16 probe_bearing_cos = g_math_sin_table[angle2];

            to[2] = from[2] + probe_bearing_cos / 4;
        }
    }

    if (collision_CastWith(from, to, hit, &normal.vx, FIGHT_CAST_WORK) == 0) {
        return 0;
    }

    fighter->wall_normal = normal;

    {
        s32 normal_heading = ratan2(normal.vx, normal.vz);
        s32 reversed_normal_heading;

        from[0] = fighter->foot_pos.vx;
        from[1] = fighter->foot_pos.vy - FIGHTER_CLIMB_HEIGHT;
        from[2] = fighter->foot_pos.vz;

        reversed_normal_heading = normal_heading + ANGLE_TURN / 2;

        {
            s32 angle3 = reversed_normal_heading & ANGLE_MASK;
            s16 reversed_normal_sin = g_math_sin_table[angle3];
            to[0] = from[0] + reversed_normal_sin / (ONE / LEDGE_TOP_REACH);
        }

        to[1] = from[1];

        {
            s32 angle4 = (normal_heading + 3 * ANGLE_TURN / 4) & ANGLE_MASK;
            s16 reversed_normal_cos = g_math_sin_table[angle4];
            to[2] = from[2] + reversed_normal_cos / (ONE / LEDGE_TOP_REACH);
        }

        if (collision_CastWith(from, to, hit, &normal.vx, FIGHT_CAST_WORK) !=
            0) {
            return 0;
        }

        from[0] = to[0];
        from[1] = to[1] + LEDGE_TOP_DEPTH;
        from[2] = to[2];

        if (collision_CastWith(to, from, hit, &normal.vx, FIGHT_CAST_WORK) ==
            0) {
            return 0;
        }

        *heading_out = reversed_normal_heading;
        return 1;
    }
}

/* Running. A run is steered: the fighter turns towards wherever the pad
 * points, measured from the opponent (or, for a player in the first-person
 * view, from the way it faces), and is carried forward at a speed that
 * builds up to its character's limit. Out of a run the speed is lost. */
static void fighter_UpdateRun(Fighter *fighter) {
    u16 kind = fighter->move_kind;

    if (kind == MOVE_KIND_RUN || kind == MOVE_KIND_ARUN ||
        kind == MOVE_KIND_RUN_ST) {
        s32 work;
        u32 held = fighter->pad.held;
        if (held & FIGHTER_PAD_DIRECTIONS) {
            /* the pad's direction, as fighter_FindLedge has it */
            s32 input_column = (held >> 14) & 1;
            s32 input_row;

            if (!(held & FIGHTER_PAD_SIDE_A)) {
                input_column++;
            }
            input_row = (held >> 15) & 1;
            if (!(held & FIGHTER_PAD_TOWARD)) {
                input_row++;
            }

            work = g_fighter_pad_headings[input_row][input_column];

            if (g_fight_first_person != 0 && fighter->is_cpu == 0) {
                /* a quarter of the way round to where the pad points */
                fighter_TurnTo(fighter, fighter->orientation.vy + work / 4);
            } else {
                fighter_TurnTo(fighter, fighter->opponent_bearing + work);
            }
        } else {
            if (g_fight_first_person != 0 && fighter->is_cpu == 0) {
                fighter->turn_left = 0;
            }
        }

        {
            /* The speed builds up by a sixteenth of the fighter's size a
             * frame, less a drag that is always 0, up to the limit of its
             * character's class.
             * FAKE: work and speed, reused for the drag, the size, the step */
            work = fighter->run_drag;
            if (work < 0) {
                work = -work;
            }
            if (work > RUN_DRAG_MAX) {
                work = RUN_DRAG_MAX;
            }

            {
                s32 speed = (u16)fighter->run_speed - work;
                s32 gain = fighter->size_scale;
                s16 new_speed;
                s32 class_limit;
                s32 scaled_limit;
                s32 limit;

                fighter->run_speed = speed;
                /* a sixteenth, rounded towards nothing */
                if (gain < 0) {
                    gain += 15; /* (the rounding) */
                }
                gain >>= 4;
                new_speed = speed + gain;
                fighter->run_speed = new_speed;

                class_limit = g_fighter_run_speed_limits
                    [g_fighter_run_classes[fighter->character]];
                speed = fighter->size_scale;
                scaled_limit = speed * class_limit;
                limit = (scaled_limit << 4) >> 12;

                if (limit < new_speed) {
                    fighter->run_speed = limit;
                } else if (new_speed < 0) {
                    fighter->run_speed = 0;
                }

                {
                    s32 distance = fighter->run_speed * fighter->speed;
                    s16 facing_sin = g_math_sin_table[(
                        fighter->orientation.vy & ANGLE_MASK)];

                    work = distance >> 12;

                    /* (a run's speed is in sixteenths of a unit) */
                    fighter->base_pos.x += (facing_sin * work) >> 16;

                    {
                        s16 facing_cos = g_math_sin_table[(
                            (fighter->orientation.vy + ANGLE_TURN / 4) &
                            ANGLE_MASK)];
                        fighter->base_pos.z += (facing_cos * work) >> 16;
                    }
                }
            }
        }
    } else {
        if (fighter->run_speed > 0) {
            if (kind != MOVE_KIND_CLIMB_ST) {
                fighter->turn_left = 0;
            }
            fighter->run_speed = 0;
        }
    }
}

/* Makes a fighter that has walked off an edge, or stands on ground too
 * steep to stand on, fall: forwards or backwards by whether it faces the
 * way it is going, with a push that way. In a blade lock both fighters go
 * over; in a paired move the fall waits for the fight to end the move. */
static void fighter_CheckFooting(Fighter *fighter) {
    s32 from[4];
    s32 to[4];
    s32 hit[4];
    /* the normal of the ground; with none, the way the fighter is moving */
    s16 slope[4];
    s16 second_hit_normal[4];
    /* whether a cast hit, then how far from facing down the slope */
    s32 work;
    s32 dx_delta;
    s32 dz_delta;
    SegmentCast *cast_work;
    s32 falls;
    u16 kind;
    if (fighter->velocity.vy <= 0) {
        return;
    }
    kind = fighter->move_kind;
    if (kind == MOVE_KIND_MJUMP || kind == MOVE_KIND_LJUMP ||
        kind == MOVE_KIND_DROP || fighter->airborne != 0 ||
        kind == MOVE_KIND_NORMAL || kind == MOVE_KIND_HOM_AT) {
        return;
    }
    cast_work = FIGHT_CAST_WORK;
    from[0] = fighter->foot_pos.vx;
    from[1] = fighter->foot_pos.vy - FOOTING_CAST_ABOVE;
    from[2] = fighter->foot_pos.vz;
    to[0] = fighter->foot_pos.vx;
    to[1] = fighter->foot_pos.vy + FOOTING_CAST_BELOW;
    to[2] = fighter->foot_pos.vz;
    work = collision_CastWith(from, to, hit, slope, cast_work);
    falls = 1;
    if (work != 0) {
        /* the ground is more than as far again below its feet */
        if (hit[1] - from[1] > 2 * FOOTING_CAST_ABOVE) {
            falls = fighter->move_kind != MOVE_KIND_CLIMB_ST;
        } else if (slope[1] > FIGHTER_FLOOR_NORMAL_Y) {
            /* too steep: is there ground a little way down the slope */
            /* 512 units along it */
            to[0] += slope[0] / (ONE / 512);
            to[2] += slope[2] / (ONE / 512); /* (and along z) */
            falls = collision_CastWith(
                        from, to, hit, second_hit_normal, cast_work) == 0;
        } else {
            falls = 0;
        }
    }
    if (!falls) {
        return;
    }
    if (work == 0) {
        /* the way it moved this frame, FOOTING_STEP_MAX at most */
        slope[0] = fighter->foot_pos.vx - fighter->prev_foot_pos.vx;
        slope[2] = fighter->foot_pos.vz - fighter->prev_foot_pos.vz;
        if (slope[0] < -FOOTING_STEP_MAX) {
            slope[0] = -FOOTING_STEP_MAX;
        } else if (slope[0] > FOOTING_STEP_MAX) {
            slope[0] = FOOTING_STEP_MAX;
        }
        if (slope[2] < -FOOTING_STEP_MAX) {
            slope[2] = -FOOTING_STEP_MAX;
        } else if (slope[2] > FOOTING_STEP_MAX) {
            slope[2] = FOOTING_STEP_MAX;
        }
        dx_delta = slope[0];
        dz_delta = slope[2];
    } else {
        /* the same at most, down the slope */
        dx_delta = slope[0] / (ONE / FOOTING_STEP_MAX);
        dz_delta = slope[2] / (ONE / FOOTING_STEP_MAX);
    }
    work = (fighter->orientation.vy - ratan2(slope[0], slope[2])) & ANGLE_MASK;
    if (work >= ANGLE_TURN / 2) {
        work = ANGLE_TURN - work;
    }
    kind = fighter->move_kind;
    if (MOVE_IS_LOCK(kind)) {
        if (work < ANGLE_TURN / 4) {
            fighter->pending_event = LOCK_EVENT_FALL_FORWARD;
            fighter->falling_backward = 0;
        } else {
            fighter->pending_event = LOCK_EVENT_FALL_BACKWARD;
            fighter->falling_backward = 1;
        }
        fighter->other->pending_event = LOCK_EVENT_BREAK;
        fighter->fall_start_y = fighter->foot_pos.vy;
    } else if (kind == MOVE_KIND_PAIRED) {
        g_fight_falling_forward = work < ANGLE_TURN / 4;
        g_fight_falling_fighter = fighter->index;
    } else {
        if (work < ANGLE_TURN / 4) {
            fighter->pending_event = MOVE_EVENT_FALL_FORWARD;
            fighter->falling_backward = 0;
        } else {
            fighter->pending_event = MOVE_EVENT_FALL_BACKWARD;
            fighter->falling_backward = 1;
        }
        fighter->fall_start_y = fighter->foot_pos.vy;
    }
    fighter->velocity.vx += dx_delta;
    fighter->velocity.vz += dz_delta;
}

/* The rotation about x, then y, then z. */
static void math_AnglesToMatrix(
    s32 x_angle, s32 y_angle, s32 z_angle, MATRIX *matrix_out) {
    matrix_out->m[0][0] = ONE;
    matrix_out->m[0][1] = 0;
    matrix_out->m[0][2] = 0;
    matrix_out->m[1][0] = 0;
    matrix_out->m[1][1] = ONE;
    matrix_out->m[1][2] = 0;
    matrix_out->m[2][0] = 0;
    matrix_out->m[2][1] = 0;
    matrix_out->m[2][2] = ONE;
    RotMatrixX(x_angle, matrix_out);
    RotMatrixY(y_angle, matrix_out);
    RotMatrixZ(z_angle, matrix_out);
}

/* ---- the pose ---- */

/* Begins easing from the pose the fighter is in to the move it has begun,
 * `blend_step` of the way (of 0x1000) a frame. A blend already under way
 * keeps where it has got to. */
static void fighter_BlendPose(Fighter *fighter, s16 blend_step) {
    s16 blends;
    fighter->blending += 1;
    blends = fighter->blending;
    if (blends == 1) {
        fighter->blend_weight = blend_step;
        fighter->blend_shift.x = 0;
        fighter->blend_shift.z = 0;
    }
    fighter->blend_step = blend_step;
    if (fighter->use_root_heading == 0) {
        fighter->use_root_heading = 1;
        fighter->root_heading = fighter->opponent_bearing;
    }
}

/* Over eight frames. */
static void fighter_BlendPoseSlowly(Fighter *fighter) {
    fighter_BlendPose(fighter, ONE / 8);
}

/* Whether there is ground within GROUND_NEAR under a fighter that is not
 * on its way up. */
static void fighter_CheckGround(Fighter *fighter) {
    if (fighter->velocity.vy < 0) {
        fighter->on_ground = 0;
        return;
    }
    FIGHTER_CAST_SCRATCH->segment_end.vx = fighter->foot_pos.vx;
    FIGHTER_CAST_SCRATCH->segment_end.vy = fighter->foot_pos.vy + GROUND_NEAR;
    FIGHTER_CAST_SCRATCH->segment_end.vz = fighter->foot_pos.vz;
    fighter->on_ground = collision_CastMoveWith(
        &fighter->foot_pos.vx, &FIGHTER_CAST_SCRATCH->segment_end.vx,
        &FIGHTER_CAST_SCRATCH->hit.vx, &FIGHTER_CAST_SCRATCH->normal.vx,
        FIGHT_CAST_WORK);
}

/* Whether the fighter's feet are on ground flat enough to land on: a floor
 * within 5 units under them. */
static s32 fighter_IsOnFloor(Fighter *fighter) {
    s32 result;
    FIGHTER_CAST_SCRATCH->segment_end.vx = fighter->foot_pos.vx;
    /* five units under the feet */
    FIGHTER_CAST_SCRATCH->segment_end.vy = fighter->foot_pos.vy + 5;
    FIGHTER_CAST_SCRATCH->segment_end.vz = fighter->foot_pos.vz;
    if (collision_CastMoveWith(
            &fighter->foot_pos.vx, &FIGHTER_CAST_SCRATCH->segment_end.vx,
            &FIGHTER_CAST_SCRATCH->hit.vx, &FIGHTER_CAST_SCRATCH->normal.vx,
            FIGHT_CAST_WORK) != 0) {
        result = FIGHTER_CAST_SCRATCH->normal.vy < FIGHTER_FLOOR_NORMAL_Y;
    } else {
        result = 0;
    }
    return result;
}

/* Where the ground under the fighter is, for its shadow: looked for
 * below it, SHADOW_DEPTH down, while it jumps or drops, left alone while
 * it climbs. */
static void fighter_FindGroundY(Fighter *fighter) {
    FighterCastScratch *scratch = FIGHTER_CAST_SCRATCH;
    u16 kind = fighter->move_kind;

    if (kind == MOVE_KIND_CLIMB || kind == MOVE_KIND_CLIMB_ST) {
        return;
    }
    if (kind == MOVE_KIND_MJUMP || kind == MOVE_KIND_LJUMP ||
        kind == MOVE_KIND_HOM_AT || kind == MOVE_KIND_DROP) {
        scratch->segment_end.vx = fighter->foot_pos.vx;
        scratch->segment_end.vy = fighter->foot_pos.vy + SHADOW_DEPTH;
        scratch->segment_end.vz = fighter->foot_pos.vz;
        if (collision_CastWith(
                &fighter->foot_pos.vx, &scratch->segment_end.vx,
                &scratch->hit.vx, &scratch->normal.vx, FIGHT_CAST_WORK) != 0) {
            fighter->ground_y = scratch->hit.vy;
        }
    } else {
        fighter->ground_y = fighter->foot_pos.vy;
    }
}

/* Marks the scabbard as holding its sword and the worn second weapon as
 * in its place, when neither is in the hand. */
static inline void fighter_MarkSheathed(Fighter *fighter) {
    if (fighter->move_kind != MOVE_KIND_END_GAME &&
        fighter->move_kind != MOVE_KIND_DTH &&
        fighter->weapon_live_state == WEAPON_LIVE_OFF) {
        fighter->frame_flags |= FIGHTER_SCABBARD_FULL;
    }
    if (fighter->sub_weapon_count > 0 &&
        fighter->sub_weapons[0] == fighter->sub_weapon_kind &&
        fighter->sub_weapon_live_state == WEAPON_LIVE_OFF) {
        fighter->frame_flags |= FIGHTER_WORN_SUB_FULL;
    }
}

/* ---- the frame ---- */

/* The pairs of kinds (from, to) between which a move is eased into. */
#define FROM_TO(from, to) (((from) << 8) | (to))

/* A frame of one fighter.
 *
 * The pad first: turned a quarter for a player in the first-person view,
 * and without Select in the modes that have no use for it. Then the move.
 * Its frame counter advances at the fighter's speed. Something that
 * happened to the fighter last frame (its pending event) picks the next
 * move from the move's table of events; so does a ledge it is pressed
 * against, a landing, or the move running out; otherwise the pad is tried
 * against the move's commands, which may begin a move at once, queue one,
 * or, in a paired move, ask the fight to begin one for both.
 *
 * The kind of move then decides how the fighter turns and what throws it
 * into the air. The two frames of motion the move is between are decoded
 * and bent towards the opponent (motion_AimPose); a move that has just
 * begun is eased into from the pose before. The pose's root says how far
 * the body is from where the fighter stands; that place moves with the
 * fighter's speed and with whatever pushes it, is kept on the stage, and
 * the model is posed there.
 *
 * Last comes what the frame leaves behind: which of the fighter's weapons
 * are drawn and which can hit, where their points and the body's are
 * (fighter_PlacePoints), whether the attack is in its hitting frames, and
 * what the move's marks do at this frame: let go of something thrown, pick
 * something up, reload. A fighter that has just been beaten drops its
 * weapon. */
void fighter_Update(s32 fighter_index, PadState *pad) {
    MotionFrame poses[2];
    Vec3 roots[2];
    VECTOR new_foot_pos;
    s16 root_headings[2];
    s32 ledge_heading;
    s16 is_common;
    Fighter *fighter;
    u16 command;
    u16 buttons;
    u16 *commands;
    MoveScript *move;
    /* FAKE: four locals: a turn limit, an angle, a turn, the pad's side */
    s32 work;
    s32 root_heading;
    s32 root_heading_plus_quarter;
    s32 weight;
    s32 from_frame;
    s32 next_move_frame;
    s32 to_frame;
    s32 decoder;
    s32 frame_offset;
    s32 hitting;
    s16 frac;
    s32 rest;
    s32 old_heading_plus_quarter;

    fighter = &g_fighters[fighter_index];
    fighter->frames_alive++;
    fighter->pad = *pad;
    /* in the first-person view the pad turns with the fighter: left is
     * away from the screen */
    if (g_fight_first_person != 0 &&
        (fighter_index == 0 || (fighter_index == 1 && fighter->is_cpu == 0))) {
        fighter->pad.held =
            (fighter->pad.held & FIGHTER_PAD_BELOW_DIRECTIONS) |
            ((fighter->pad.held & PADLleft) >> 3) |
            ((fighter->pad.held & (PADLup | PADLright | PADLdown)) << 1);
        fighter->pad.pressed =
            (fighter->pad.pressed & FIGHTER_PAD_BELOW_DIRECTIONS) |
            ((fighter->pad.pressed & PADLleft) >> 3) |
            ((fighter->pad.pressed & (PADLup | PADLright | PADLdown)) << 1);
        fighter->pad.released =
            (fighter->pad.released & FIGHTER_PAD_BELOW_DIRECTIONS) |
            ((fighter->pad.released & PADLleft) >> 3) |
            ((fighter->pad.released & (PADLup | PADLright | PADLdown)) << 1);
        fighter->pad.unheld =
            (fighter->pad.unheld & FIGHTER_PAD_BELOW_DIRECTIONS) |
            ((fighter->pad.unheld & PADLleft) >> 3) |
            ((fighter->pad.unheld & (PADLup | PADLright | PADLdown)) << 1);
    }
    if (g_fight_mode == FIGHT_MODE_PRACTICE ||
        g_fight_mode == FIGHT_MODE_TEAM ||
        g_fight_mode == FIGHT_MODE_CHANBARA ||
        (g_fight_mode == FIGHT_MODE_STORY && g_story_victim != 0)) {
        fighter->pad.pressed &= ~PADselect;
    }
    /* no pad on its first frame */
    if (fighter->frames_alive < 2) {
        fighter->pad.held = fighter->pad.pressed = fighter->pad.released = 0;
        fighter->pad.unheld = 0xFFFF;
    }
    /* body wounds weaken it (what is left, of 0x1000) */
    fighter->power =
        (fighter->base_power *
         g_fighter_wound_powers
             [fighter->character]
             [fighter->body_wounds >= 4 ? 3 : fighter->body_wounds]) >>
        12;
    /* ---- the move ---- */
    if (fighter->move_started == MOVE_START_SCRIPT) {
        fighter->move_started = MOVE_START_NONE;
    }
    motion_UpdateFrameStep(fighter);
    frac = fighter->move_frame_frac + fighter->speed;
    fighter->move_frame_frac = frac;
    if (frac >= FIGHTER_SPEED_ONE) {
        fighter->same_frame = 0;
        fighter->move_frame += frac >> 12;
        fighter->move_frame_frac &= FIGHTER_SPEED_ONE - 1;
    } else {
        fighter->same_frame = 1;
    }
    /* something happened to it: the move that answers the event */
    if (fighter->pending_event >= 0) {
        /* the move's table of the moves that answer each event */
        u16 *table;

        if (fighter->move_kind == MOVE_KIND_DROP ||
            fighter->move_kind == MOVE_KIND_MJUMP ||
            fighter->move_kind == MOVE_KIND_LJUMP) {
            fighter->airborne = 1;
        }
        fighter->blending = 0;
        table = motion_GetMove(fighter, fighter->cur_move->event_table_id,
                               &fighter->move_is_common);
        fighter_StartMove(fighter_index,
                          motion_GetMove(fighter, table[fighter->pending_event],
                                         &fighter->move_is_common),
                          fighter->move_is_common);
        fighter->pending_event = MOVE_EVENT_NONE;
    }
    /* pressed against a ledge with R1 held: climb it */
    if ((fighter->pad.held & PADR1) &&
        (fighter->pad.held & FIGHTER_PAD_DIRECTIONS)) {
        if ((fighter->move_kind == MOVE_KIND_STANCE ||
             fighter->move_kind == MOVE_KIND_WALK ||
             fighter->move_kind == MOVE_KIND_STEP ||
             fighter->move_kind == MOVE_KIND_RUN ||
             fighter->move_kind == MOVE_KIND_RUN_ST ||
             fighter->move_kind == MOVE_KIND_RUN_ED) &&
            fighter_FindLedge(fighter, &ledge_heading) != 0) {
            u16 *table;

            fighter_TurnTo(fighter, ledge_heading);
            fighter->blending = 0;
            table = motion_GetMove(fighter, fighter->cur_move->event_table_id,
                                   &fighter->move_is_common);
            fighter_StartMove(fighter_index,
                              motion_GetMove(fighter, table[MOVE_EVENT_CLIMB],
                                             &fighter->move_is_common),
                              fighter->move_is_common);
            fighter->velocity.vx = 0;
            fighter->velocity.vy = 0;
            fighter->velocity.vz = 0;
        }
    }
    /* the move has run out: the move asked for, or the one that follows */
    if (fighter->cur_move->last_frame < fighter->move_frame) {
        if (fighter->queued_move != NULL) {
            fighter->move_is_common = fighter->queued_move_is_common;
            if (fighter->queued_move_command & MOVE_COMMAND_BORROWED) {
                fighter->borrow_other_motion = 1;
            }
            fighter_StartMove(fighter_index, fighter->queued_move,
                              fighter->queued_move_is_common);
        } else {
            if ((fighter->cur_move->commands[0] & MOVE_COMMAND_FOLLOW) &&
                g_fight_paired_move_initiator != fighter_index) {
                if (fighter->cur_move->commands[0] & MOVE_COMMAND_BORROWED) {
                    fighter->borrow_other_motion = 1;
                }
                fighter_StartMove(
                    fighter_index,
                    motion_GetMove(fighter, fighter->cur_move->commands[1],
                                   &fighter->move_is_common),
                    fighter->move_is_common);
            } else {
                if (fighter->cur_move->flags & MOVE_NEXT_BORROWED) {
                    fighter->borrow_other_motion = 1;
                }
                fighter_StartMove(
                    fighter_index,
                    motion_GetMove(fighter, fighter->cur_move->next_move_id,
                                   &fighter->move_is_common),
                    fighter->move_is_common);
            }
        }
    }
    /* A queued move begins once the move takes no more commands. With
     * none queued, the pad is tried against the move's commands, the
     * first that fits winning. */
    if (fighter->queued_move != NULL) {
        if (fighter->cur_move->last_command_frame < fighter->move_frame) {
            if (fighter->move_kind == MOVE_KIND_PAIRED) {
                g_fight_paired_move_start_pending = 1;
                g_fight_paired_move = fighter->queued_move;
                g_fight_paired_move_is_common = fighter->queued_move_is_common;
                g_fight_paired_move_flags = fighter->queued_move_command;
            } else {
                fighter->move_is_common = fighter->queued_move_is_common;
                if (fighter->queued_move_command & MOVE_COMMAND_BORROWED) {
                    fighter->borrow_other_motion = 1;
                }
                fighter_StartMove(fighter_index, fighter->queued_move,
                                  fighter->queued_move_is_common);
            }
        }
    } else if (fighter->cur_move->last_command_frame >= fighter->move_frame) {
        commands = fighter->cur_move->commands;
        while ((command = commands[0]) != 0) {
            if (!(command & MOVE_COMMAND_FOR_SOME) ||
                ((commands[2] | ((u32)commands[3] << 16)) &
                 (1 << fighter->character))) {
                switch (command & MOVE_COMMAND_WORD) {
                case MOVE_COMMAND_HELD:
                    buttons = fighter->pad.held;
                    break;
                case MOVE_COMMAND_PRESSED:
                    buttons = fighter->pad.pressed;
                    break;
                case MOVE_COMMAND_UNHELD:
                    buttons = fighter->pad.unheld;
                    break;
                case MOVE_COMMAND_RELEASED:
                    buttons = fighter->pad.released;
                    break;
                }
                if (command & MOVE_COMMAND_FOLLOW) {
                    buttons = 0;
                }
                if (fighter->move_kind == MOVE_KIND_PAIRED &&
                    g_fight_paired_move_initiator == fighter_index) {
                    buttons = 0;
                }
                if ((command & MOVE_COMMAND_BUTTON) ==
                    MOVE_COMMAND_BUTTON_COUNTER) {
                    command = (command & ~MOVE_COMMAND_BUTTON) |
                              MOVE_COMMAND_BUTTON_R1;
                    if (fighter->other->move_kind != MOVE_KIND_DOWN &&
                        fighter->other->move_frame <
                            fighter->other->counter_frame) {
                        buttons = 0;
                    }
                } else if ((command & MOVE_COMMAND_BUTTON) ==
                           MOVE_COMMAND_BUTTON_OFF_ARM) {
                    command = (command & ~MOVE_COMMAND_BUTTON) |
                              MOVE_COMMAND_BUTTON_FRONTAL;
                    if (fighter->off_arm_ok == 0) {
                        buttons = 0;
                    }
                }
                if ((buttons >> (command & MOVE_COMMAND_BUTTON)) & 1) {
                    fighter->command_taken = command;
                    if (command & MOVE_COMMAND_BORROWED) {
                        fighter->borrow_other_motion = 1;
                    }
                    move = motion_GetMove(fighter, commands[1], &is_common);
                    if (fighter->cur_move->flags & MOVE_QUEUES) {
                        fighter->queued_move = move;
                        fighter->queued_move_command = command;
                        fighter->borrow_other_motion = 0;
                        fighter->queued_move_is_common = is_common;
                    } else if (fighter->move_kind == MOVE_KIND_PAIRED) {
                        g_fight_paired_move_start_pending = 1;
                        g_fight_paired_move = move;
                        g_fight_paired_move_flags = command;
                        g_fight_paired_move_is_common = is_common;
                    } else {
                        fighter->move_is_common = is_common;
                        fighter_StartMove(fighter_index, move, is_common);
                    }
                    break;
                }
            }
            if (command & MOVE_COMMAND_WIDE) {
                commands += 4;
            } else {
                commands += 2;
            }
        }
    }
    /* landing: after too long a fall the fighter goes down */
    if (fighter->move_kind == MOVE_KIND_DROP || fighter->airborne != 0) {
        if (fighter_IsOnFloor(fighter) != 0) {
            if (fighter->fall_start_y + FIGHTER_SAFE_FALL <
                fighter->foot_pos.vy) {
                u16 *table;

                table =
                    motion_GetMove(fighter, fighter->cur_move->event_table_id,
                                   &fighter->move_is_common);
                fighter_StartMove(
                    fighter_index,
                    motion_GetMove(fighter,
                                   table[fighter->falling_backward != 0
                                             ? MOVE_EVENT_LAND_ON_BACK
                                             : MOVE_EVENT_LAND_ON_FRONT],
                                   &fighter->move_is_common),
                    fighter->move_is_common);
            } else if (fighter->airborne == 0) {
                fighter_StartMove(
                    fighter_index,
                    motion_GetMove(fighter, fighter->cur_move->next_move_id,
                                   &fighter->move_is_common),
                    fighter->move_is_common);
            }
            fighter->airborne = 0;
        }
    } else if (
        fighter->move_started != MOVE_START_NONE &&
        (fighter->prev_move_kind == MOVE_KIND_MJUMP ||
         fighter->prev_move_kind == MOVE_KIND_LJUMP) &&
        !(fighter->move_kind == MOVE_KIND_MJUMP ||
          fighter->move_kind == MOVE_KIND_LJUMP) &&
        fighter->fall_start_y + FIGHTER_SAFE_FALL < fighter->foot_pos.vy) {
        u16 *table;

        table = motion_GetMove(fighter, fighter->cur_move->event_table_id,
                               &fighter->move_is_common);
        fighter_StartMove(
            fighter_index,
            motion_GetMove(fighter, table[MOVE_EVENT_LAND_ON_FRONT],
                           &fighter->move_is_common),
            fighter->move_is_common);
    }
    /* ---- turning, and leaving the ground ---- */
    if (fighter->move_turn_mode == 1) {
        fighter_TurnTo(fighter, fighter->opponent_bearing);
    }
    if (fighter->move_started != MOVE_START_NONE) {
        if (MOVE_IS_ATTACK(fighter->move_kind) ||
            fighter->move_kind == MOVE_KIND_MOVE) {
            fighter_TurnTo(fighter, fighter->opponent_bearing);
            if (fighter->move_kind == MOVE_KIND_ATTACK ||
                fighter->move_kind == MOVE_KIND_MOVE) {
                work = (fighter->motion_head[MOTION_HEAD_TURN_AND_CLASS] >> 4) *
                       MOTION_TURN_UNIT;
                if (fighter->turn_left > work) {
                    fighter->turn_left = work;
                } else if (fighter->turn_left < -work) {
                    fighter->turn_left = -work;
                }
            }
        }
    }
    if (fighter->same_frame == 0 && fighter->move_kind == MOVE_KIND_GET_UP &&
        fighter->face_frame == fighter->move_frame) {
        fighter_TurnTo(fighter, fighter->opponent_bearing);
    }
    if (fighter->move_started != MOVE_START_NONE &&
        fighter->prev_move_kind != MOVE_KIND_LJUMP &&
        fighter->move_kind == MOVE_KIND_LJUMP) {
        fighter->velocity.vx +=
            (g_math_sin_table[fighter->orientation.vy & ANGLE_MASK] *
             g_fighter_long_jump_speeds[fighter->character]) >>
            12;
        fighter->velocity.vz +=
            (g_math_sin_table[(fighter->orientation.vy + ANGLE_TURN / 4) &
                              ANGLE_MASK] *
             g_fighter_long_jump_speeds[fighter->character]) >>
            12;
        fighter->velocity.vy += g_fighter_long_jump_lifts[fighter->character];
        fighter->fall_start_y = fighter->foot_pos.vy;
    }
    if (fighter->move_started != MOVE_START_NONE &&
        fighter->prev_move_kind != MOVE_KIND_MJUMP &&
        fighter->move_kind == MOVE_KIND_MJUMP) {
        fighter->velocity.vy += g_fighter_jump_lifts[fighter->character];
        fighter->fall_start_y = fighter->foot_pos.vy;
    }
    if (fighter->move_started != MOVE_START_NONE &&
        fighter->move_kind == MOVE_KIND_HOM_AT) {
        s32 horizontal_velocity[3];
        s32 target_point[3];

        /* aimed short of the opponent by how far it has to turn: by 512
         * units when the opponent is behind it */
        work =
            (fighter->opponent_bearing - fighter->orientation.vy) & ANGLE_MASK;
        if (work >= ANGLE_TURN / 2) {
            work = ANGLE_TURN - work;
        }
        target_point[0] =
            fighter->other->chest_pos.x -
            ((g_math_sin_table[fighter->opponent_bearing & ANGLE_MASK] *
              work) >>
             14);
        target_point[1] = fighter->other->chest_pos.y;
        target_point[2] =
            fighter->other->chest_pos.z -
            ((g_math_sin_table[(fighter->opponent_bearing + ANGLE_TURN / 4) &
                               ANGLE_MASK] *
              work) >>
             14);
        /* the leaping attack: up, and across to where the opponent is
         * (worked out for a fall of LEAP_AIM_GRAVITY a frame, where
         * FIGHTER_GRAVITY is 21: the original's behaviour) */
        fighter->velocity.vy -= LEAP_LIFT;
        math_LobVelocity(
            (s32 *)&fighter->head_pos, target_point, fighter->velocity.vy,
            LEAP_AIM_GRAVITY, horizontal_velocity);
        fighter->velocity.vx += horizontal_velocity[0];
        fighter->velocity.vz += horizontal_velocity[2];
        fighter->airborne = 1;
        fighter->fall_start_y = fighter->foot_pos.vy;
    }
    if (fighter->move_kind == MOVE_KIND_MJUMP ||
        fighter->move_kind == MOVE_KIND_LJUMP ||
        fighter->move_kind == MOVE_KIND_HOM_AT ||
        fighter->move_kind == MOVE_KIND_DROP) {
        if (fighter->move_frame == fighter->cur_move->last_frame &&
            fighter->velocity.vy > 0) {
            fighter->move_frame = fighter->cur_move->last_frame - 1;
        }
    }
    if (fighter->move_started != MOVE_START_NONE &&
        fighter->move_kind == MOVE_KIND_CLIMB_ED) {
        fighter->foot_pos.vy -= FIGHTER_CLIMB_HEIGHT;
        fighter->base_pos.y -= FIGHTER_CLIMB_HEIGHT;
        fighter->prev_root_offset.y += FIGHTER_CLIMB_HEIGHT;
    }
    {
        /* FAKE: the kind, read ahead of the test */
        s32 kind = fighter->move_kind;

        if (fighter->move_started != MOVE_START_NONE &&
            kind == MOVE_KIND_DOWN) {
            fighter->stance = fighter->default_stance;
        }
    }
    /* a part of the turn it has to make: a sixth, at full speed */
    work = (fighter->turn_left * fighter->speed) / (6 * FIGHTER_SPEED_ONE);
    fighter->orientation.vy += work;
    fighter->turn_left -= work;
    if (fighter->move_started != MOVE_START_NONE) {
        /* which moves are eased into */
        if (fighter->blend_into_move != 0) {
            fighter_BlendPoseSlowly(fighter);
        } else if (fighter->move_kind == MOVE_KIND_START) {
            fighter_BlendPose(fighter, ONE / 4); /* over four frames */
        } else {
            if (fighter->prev_move_kind == MOVE_KIND_START ||
                fighter->move_kind == MOVE_KIND_PAIRED ||
                fighter->prev_move_kind == MOVE_KIND_PAIRED ||
                fighter->move_kind == MOVE_KIND_STANCE ||
                fighter->move_kind == MOVE_KIND_DROP ||
                fighter->move_kind == MOVE_KIND_SURRENDER_ST) {
                fighter_BlendPoseSlowly(fighter);
            } else {
                switch (FROM_TO(fighter->prev_move_kind, fighter->move_kind)) {
                case FROM_TO(MOVE_KIND_STANCE, MOVE_KIND_STANCE_CHANGE):
                case FROM_TO(MOVE_KIND_STANCE, MOVE_KIND_ATTACK):
                case FROM_TO(MOVE_KIND_RUN, MOVE_KIND_CLIMB_ST):
                case FROM_TO(MOVE_KIND_STANCE, MOVE_KIND_WALK):
                case FROM_TO(MOVE_KIND_RUN_ST, MOVE_KIND_CLIMB_ST):
                case FROM_TO(MOVE_KIND_WALK, MOVE_KIND_RUN):
                case FROM_TO(MOVE_KIND_RUN_ED, MOVE_KIND_CLIMB_ST):
                    fighter_BlendPoseSlowly(fighter);
                    break;
                }
            }
        }
    }
    /* ---- the pose ----
     * The two frames of motion it is between. A paired move's are in the
     * file of whoever began it, the other fighter's half after the
     * first's; one clan stands in the second half of a stance's motion. */
    next_move_frame = fighter->move_frame + 1;
    if (next_move_frame >= fighter->motion_head[MOTION_HEAD_FRAMES]) {
        next_move_frame = fighter->motion_head[MOTION_HEAD_FRAMES] - 1;
    }
    if (fighter->cur_move->flags & MOVE_PAIRED) {
        decoder =
            fighter->move_is_common == 0
                ? g_fighters[g_fight_paired_move_initiator].motion_slot + 1
                : 0;
        if (fighter->move_kind == MOVE_KIND_PAIRED &&
            g_fight_paired_move_initiator != fighter_index) {
            frame_offset = fighter->motion_head[MOTION_HEAD_FRAMES];
        } else {
            frame_offset = 0;
        }
    } else {
        decoder = fighter->move_is_common == 0 ? fighter->motion_slot + 1 : 0;
        if (fighter->move_kind == MOVE_KIND_STANCE &&
            g_fighter_clans[fighter->character] != 0) {
            frame_offset = fighter->motion_head[MOTION_HEAD_FRAMES];
        } else {
            frame_offset = 0;
        }
    }
    from_frame = frame_offset + *fighter->motion + fighter->move_frame;
    to_frame = frame_offset + *fighter->motion + next_move_frame;
    fighter->pose_from_frame = (decoder << 14) | from_frame;
    fighter->pose_to_frame = (decoder << 14) | to_frame;
    if (fighter->move_kind == MOVE_KIND_WIN) {
        poses[1] = g_motion_win_frames[fighter_index][fighter->move_frame];
        poses[0] = poses[1];
    } else {
        motion_DecodeFrame(
            decoder, from_frame, &poses[0], (u16 *)FIGHTER_CAST_SCRATCH);
        motion_DecodeFrame(
            decoder, to_frame, &poses[1], (u16 *)FIGHTER_CAST_SCRATCH);
    }
    if (fighter->move_started != MOVE_START_NONE &&
        (fighter->move_kind == MOVE_KIND_DOWN ||
         fighter->move_kind == MOVE_KIND_DTH) &&
        fighter->prev_move_kind == MOVE_KIND_DOWN) {
        MATRIX saved_pose_matrix;
        MATRIX current_pose_matrix;
        s32 old_heading;
        s32 new_heading;
        s32 heading_delta;

        /* rolling on the ground: the fighter turns as the body has turned */
        math_AnglesToMatrix(
            fighter->prev_pose.waist_angle_x, fighter->prev_pose.waist_angle_y,
            fighter->prev_pose.waist_angle_z, &saved_pose_matrix);
        math_AnglesToMatrix(poses[0].waist_angle_x, poses[0].waist_angle_y,
                            poses[0].waist_angle_z, &current_pose_matrix);
        old_heading =
            ratan2(saved_pose_matrix.m[0][0], saved_pose_matrix.m[2][0]);
        new_heading =
            ratan2(current_pose_matrix.m[0][0], current_pose_matrix.m[2][0]);
        heading_delta = new_heading - old_heading;

        fighter->orientation.vy += heading_delta;
        camera_EaseTurn(fighter_index, heading_delta);
    }
    motion_AimPose(fighter, &poses[0], &poses[1]);
    if (fighter->move_kind == MOVE_KIND_PAIRED) {
        fighter->root_heading = fighter->orientation.vy;
    }
    /* where each pose puts the body from where the fighter stands */
    if (fighter->use_root_heading != 0) {
        root_heading = fighter->root_heading;
    } else {
        root_heading = fighter->orientation.vy;
    }
    root_headings[0] = root_headings[1] = root_heading;
    root_heading_plus_quarter = root_heading + ANGLE_TURN / 4;
    roots[0].x =
        (poses[0].root_distance *
         g_math_sin_table[(poses[0].root_angle - root_heading_plus_quarter +
                           ANGLE_TURN / 4) &
                          ANGLE_MASK]) >>
        12;
    roots[0].y = -poses[0].root_height;
    roots[0].z =
        (poses[0].root_distance *
         g_math_sin_table[(poses[0].root_angle - root_heading_plus_quarter) &
                          ANGLE_MASK]) >>
        12;
    roots[1].x =
        (poses[1].root_distance *
         g_math_sin_table[(poses[1].root_angle - root_heading_plus_quarter +
                           ANGLE_TURN / 4) &
                          ANGLE_MASK]) >>
        12;
    roots[1].y = -poses[1].root_height;
    roots[1].z =
        (poses[1].root_distance *
         g_math_sin_table[(poses[1].root_angle - root_heading_plus_quarter) &
                          ANGLE_MASK]) >>
        12;
    roots[0].x = (roots[0].x * fighter->size_scale) >> 12;
    roots[0].z = (roots[0].z * fighter->size_scale) >> 12;
    roots[1].x = (roots[1].x * fighter->size_scale) >> 12;
    roots[1].z = (roots[1].z * fighter->size_scale) >> 12;
    if (fighter->move_kind != MOVE_KIND_CLIMB) {
        roots[0].y = (roots[0].y * fighter->size_scale) >> 12;
        roots[1].y = (roots[1].y * fighter->size_scale) >> 12;
    }
    if (fighter->blending != 0) {
        /* easing: from the pose it was in to the first of the two */
        poses[1] = poses[0];
        root_headings[1] = root_headings[0];
        poses[0] = fighter->prev_pose;
        root_headings[0] = fighter->prev_pose_heading;
        weight = fighter->blend_weight;
        fighter->pose_to_frame = fighter->pose_from_frame;
        fighter->pose_from_frame = fighter->prev_pose_frame;
    } else {
        weight = fighter->move_frame_frac;
    }
    rest = 0x1000 - weight; /* the weights are of 0x1000 */
    fighter->pose_weight = weight;
    if (fighter->blending != 0) {
        Vec3 old_root;

        if (fighter->move_started != MOVE_START_NONE) {
            fighter->base_pos.x += fighter->prev_root_offset.x - roots[0].x;
            fighter->base_pos.z += fighter->prev_root_offset.z - roots[0].z;
            fighter->blend_shift.x += fighter->prev_root_offset.x - roots[0].x;
            fighter->blend_shift.z += fighter->prev_root_offset.z - roots[0].z;
        }
        old_heading_plus_quarter = root_headings[0] + ANGLE_TURN / 4;
        old_root.x =
            (poses[0].root_distance *
             g_math_sin_table[(poses[0].root_angle - old_heading_plus_quarter +
                               ANGLE_TURN / 4) &
                              ANGLE_MASK]) >>
            12;
        old_root.y = -poses[0].root_height;
        old_root.z =
            (poses[0].root_distance *
             g_math_sin_table[(poses[0].root_angle - old_heading_plus_quarter) &
                              ANGLE_MASK]) >>
            12;
        old_root.x = (old_root.x * fighter->size_scale) >> 12;
        old_root.y = (old_root.y * fighter->size_scale) >> 12;
        old_root.z = (old_root.z * fighter->size_scale) >> 12;
        old_root.x -= fighter->blend_shift.x;
        old_root.z -= fighter->blend_shift.z;
        fighter->root_offset.x =
            (old_root.x * rest + roots[0].x * weight) >> 12;
        fighter->root_offset.y =
            (old_root.y * rest + roots[0].y * weight) >> 12;
        fighter->root_offset.z =
            (old_root.z * rest + roots[0].z * weight) >> 12;
    } else {
        fighter->root_offset.x =
            (roots[0].x * rest + roots[1].x * weight) >> 12;
        fighter->root_offset.y =
            (roots[0].y * rest + roots[1].y * weight) >> 12;
        fighter->root_offset.z =
            (roots[0].z * rest + roots[1].z * weight) >> 12;
    }
    if (fighter->move_started != MOVE_START_NONE && fighter->blending == 0) {
        fighter->base_pos.x +=
            fighter->prev_root_offset.x - fighter->root_offset.x;
        fighter->base_pos.z +=
            fighter->prev_root_offset.z - fighter->root_offset.z;
        if (fighter->move_kind == MOVE_KIND_CLIMB_ED ||
            fighter->move_kind == MOVE_KIND_DROP) {
            fighter->base_pos.y +=
                fighter->prev_root_offset.y - fighter->root_offset.y;
        }
    }
    /* ---- moving ---- */
    fighter_UpdateRun(fighter);
    fighter_CheckFooting(fighter);
    if (fighter->move_kind != MOVE_KIND_CLIMB &&
        fighter->move_kind != MOVE_KIND_CLIMB_ST) {
        fighter->velocity.vy += FIGHTER_GRAVITY;
    }
    {
        s32 kind = fighter->move_kind;

        if (kind == MOVE_KIND_CLIMB || kind == MOVE_KIND_CLIMB_ST) {
            /* held to the wall: 16 units a frame */
            fighter->push.vx -= fighter->wall_normal.vx / (ONE / 16);
            fighter->push.vz -= fighter->wall_normal.vz / (ONE / 16);
        }
    }
    if (fighter->move_started == MOVE_START_NONE ||
        fighter->move_kind != MOVE_KIND_HOM_AT) {
        fighter->velocity.vx =
            (fighter->velocity.vx * fighter->velocity_damping_x) >> 12;
        fighter->velocity.vy =
            (fighter->velocity.vy * fighter->velocity_damping_y) >> 12;
        fighter->velocity.vz =
            (fighter->velocity.vz * fighter->velocity_damping_z) >> 12;
    }
    /* a small speed dies quickly */
    if (fighter->velocity.vx >= -SMALL_SPEED &&
        fighter->velocity.vx <= SMALL_SPEED) {
        fighter->velocity.vx /= 2;
    }
    if (fighter->velocity.vy >= -SMALL_SPEED &&
        fighter->velocity.vy <= SMALL_SPEED) {
        fighter->velocity.vy /= 2;
    }
    if (fighter->velocity.vz >= -SMALL_SPEED &&
        fighter->velocity.vz <= SMALL_SPEED) {
        fighter->velocity.vz /= 2;
    }
    fighter->base_pos.x += fighter->velocity.vx;
    fighter->base_pos.y += fighter->velocity.vy;
    fighter->base_pos.z += fighter->velocity.vz;
    if ((fighter->move_kind == MOVE_KIND_STAGGER ||
         fighter->move_kind == MOVE_KIND_BLOCK) &&
        /* staggering after a clash of unlike attacks, it can be steered
         * to one side */
        fighter->attack_kinds_matched == 0) {
        work = (fighter->pad.held & FIGHTER_PAD_SIDE_A)
                   ? 1
                   : -((fighter->pad.held & FIGHTER_PAD_SIDE_B) != 0);
        if (work != 0) {
            /* half the push it has, and a new one of 32 units a frame
             * towards the opponent and 96 to the side */
            fighter->push.vx /= 2;
            fighter->push.vz /= 2;
            fighter->push.vx +=
                (g_math_sin_table[fighter->opponent_bearing & ANGLE_MASK] -
                 g_math_sin_table[(fighter->opponent_bearing + ANGLE_TURN / 4) &
                                  ANGLE_MASK] *
                     work * 3) /
                (ONE / 32);
            fighter->push.vz +=
                (g_math_sin_table[(fighter->opponent_bearing + ANGLE_TURN / 4) &
                                  ANGLE_MASK] +
                 g_math_sin_table[fighter->opponent_bearing & ANGLE_MASK] *
                     work * 3) /
                (ONE / 32);
        }
    }
    /* a push of 3 or less is over */
    fighter->push.vx = (fighter->push.vx * fighter->push_damping_x) >> 12;
    if (fighter->push.vx >= -3 && fighter->push.vx <= 3) {
        fighter->push.vx = 0;
    }
    fighter->push.vy = (fighter->push.vy * fighter->push_damping_y) >> 12;
    if (fighter->push.vy >= -3 && fighter->push.vy <= 3) {
        fighter->push.vy = 0;
    }
    fighter->push.vz = (fighter->push.vz * fighter->push_damping_z) >> 12;
    if (fighter->push.vz >= -3 && fighter->push.vz <= 3) {
        fighter->push.vz = 0;
    }
    if (fighter->move_kind == MOVE_KIND_END_GAME ||
        fighter->move_kind == MOVE_KIND_DTH ||
        fighter->roster_id == ROSTER_VICTIM) {
        fighter->push.vx = 0;
        fighter->push.vy = 0;
        fighter->push.vz = 0;
    } else {
        fighter->base_pos.x += fighter->push.vx;
        fighter->base_pos.y += fighter->push.vy;
        fighter->base_pos.z += fighter->push.vz;
    }
    /* through the stage; a climber's height is its pose's */
    new_foot_pos.vx = fighter->base_pos.x + fighter->root_offset.x;
    new_foot_pos.vy = fighter->base_pos.y;
    if (fighter->move_kind == MOVE_KIND_CLIMB ||
        fighter->move_kind == MOVE_KIND_CLIMB_ST) {
        new_foot_pos.vy += fighter->root_offset.y;
    }
    new_foot_pos.vz = fighter->base_pos.z + fighter->root_offset.z;
    fighter->prev_foot_pos = fighter->foot_pos;
    fighter_MoveThroughStage(
        fighter, &fighter->foot_pos, &new_foot_pos, &fighter->velocity.vx);
    if (fighter->surface != SURFACE_BARRIER &&
        fighter->surface != SURFACE_PLAIN) {
        fighter->surface_class = SURFACE_CLASS(fighter->surface);
    }
    fighter->base_pos.x = fighter->foot_pos.vx - fighter->root_offset.x;
    fighter->base_pos.y = fighter->foot_pos.vy;
    if (fighter->move_kind == MOVE_KIND_CLIMB ||
        fighter->move_kind == MOVE_KIND_CLIMB_ST) {
        fighter->base_pos.y -= fighter->root_offset.y;
    }
    fighter->base_pos.z = fighter->foot_pos.vz - fighter->root_offset.z;
    fighter->world_pos.x = fighter->base_pos.x + fighter->root_offset.x;
    fighter->world_pos.y = fighter->base_pos.y + fighter->root_offset.y;
    fighter->world_pos.z = fighter->base_pos.z + fighter->root_offset.z;
    fighter->prev_root_offset = fighter->root_offset;
    fighter->camera_target.x = fighter->world_pos.x;
    fighter->camera_target.y = fighter->base_pos.y - FIGHTER_WAIST_HEIGHT;
    fighter->camera_target.z = fighter->world_pos.z;
    fighter_FindGroundY(fighter);
    /* ---- the model ---- */
    model_BlendPoses(fighter_index, (u8 *)&poses[0], (u8 *)&poses[1],
                     fighter->pose_weight, (MATRIX *)FIGHTER_CAST_SCRATCH);
    cloth_SaveScratch();
    model_Update(fighter_index, MODEL_POSE_BLENDED, (s32 *)&fighter->world_pos,
                 (s16 *)&fighter->orientation, 0, fighter->ground_y);
    cloth_RestoreScratch();
    /* ---- the weapons: which can hit and what is drawn ----
     * A sheathed blade is not out while standing or moving about. */
    if (fighter->stance == fighter->sheathed_stance &&
        (fighter->move_kind == MOVE_KIND_STANCE ||
         fighter->move_kind == MOVE_KIND_WALK ||
         fighter->move_kind == MOVE_KIND_STEP ||
         fighter->move_kind == MOVE_KIND_SJUMP ||
         fighter->move_kind == MOVE_KIND_RUN_ST ||
         fighter->move_kind == MOVE_KIND_RUN ||
         fighter->move_kind == MOVE_KIND_RUN_ED)) {
        fighter->weapon_live_state = WEAPON_LIVE_OFF;
    } else {
        fighter->weapon_live_state =
            fighter->weapon_live_state != WEAPON_LIVE_OFF
                ? WEAPON_LIVE_ON
                : WEAPON_LIVE_FIRST_FRAME;
    }
    fighter->frame_flags = 0;
    if (fighter->roster_id != ROSTER_VICTIM && fighter->is_defeated == 0 &&
        fighter->weapon_live_state != WEAPON_LIVE_OFF) {
        fighter->frame_flags = FIGHTER_DRAW_WEAPON;
    }
    if ((g_fight_mode != FIGHT_MODE_CHANBARA || fighter_index != 1 ||
         g_chanbara_kind == CHANBARA_LAST_KIND) &&
        (fighter->weapon_id == WEAPON_ID_KATANA ||
         fighter->weapon_id == WEAPON_ID_LONG_SWORD) &&
        (g_fight_mode != FIGHT_MODE_PRACTICE || g_fight_practice_round != 0) &&
        g_fight_mode != FIGHT_MODE_TEAM) {
        fighter->frame_flags |= FIGHTER_DRAW_SCABBARD;
    }
    /* the off hand: a second sword in the two-sword stance, a sidearm
     * while its attack has it out */
    if (fighter->stance == fighter->two_sword_stance &&
        fighter->two_sword_ready != 0) {
        if (fighter_SecondSwordIsOut(fighter) != 0) {
            fighter->sub_weapon_live_state =
                fighter->sub_weapon_live_state != WEAPON_LIVE_OFF
                    ? WEAPON_LIVE_ON
                    : WEAPON_LIVE_FIRST_FRAME;
            fighter->frame_flags |= FIGHTER_DRAW_SUB_WEAPON;
        } else {
            fighter->sub_weapon_live_state = WEAPON_LIVE_OFF;
        }
    } else if (
        ((fighter->roster_id == ROSTER_SIDEARM_2 ||
          fighter->roster_id == ROSTER_SIDEARM_1) &&
         (MOVE_IS_ATTACK(fighter->move_kind)) &&
         fighter->hit_start_frames[1] < MOVE_NO_FRAME &&
         fighter->off_arm_ok != 0 &&
         fighter->move_frame >= fighter->off_hand_start &&
         fighter->move_frame <= fighter->off_hand_end) ||
        (fighter->character == CHARACTER_JO &&
         fighter->move_kind == MOVE_KIND_PAIRED &&
         g_fight_paired_move_initiator == fighter_index &&
         fighter->move_frame >= fighter->off_hand_start &&
         fighter->off_hand_end >= fighter->move_frame)) {
        fighter->sub_weapon_live_state =
            fighter->sub_weapon_live_state != WEAPON_LIVE_OFF
                ? WEAPON_LIVE_ON
                : WEAPON_LIVE_FIRST_FRAME;
        fighter->frame_flags |= FIGHTER_DRAW_SUB_WEAPON;
    } else {
        fighter->sub_weapon_live_state = WEAPON_LIVE_OFF;
    }
    if (g_fight_mode != FIGHT_MODE_CHANBARA || fighter_index != 1 ||
        g_chanbara_kind == CHANBARA_LAST_KIND) {
        if (g_fight_mode == FIGHT_MODE_CHANBARA && fighter_index == 1 &&
            g_chanbara_kind == CHANBARA_LAST_KIND) {
            if (CHARACTER_HAS_SECOND_SWORD(fighter->other->character)) {
                fighter->frame_flags |= FIGHTER_DRAW_WORN_SUB;
            }
            fighter_MarkSheathed(fighter);
        } else if ((g_fight_mode != FIGHT_MODE_PRACTICE ||
                    g_fight_practice_round != 0) &&
                   g_fight_mode != FIGHT_MODE_TEAM) {
            if (CHARACTER_HAS_SECOND_SWORD(fighter->character)) {
                fighter->frame_flags |= FIGHTER_DRAW_WORN_SUB;
            }
            fighter_MarkSheathed(fighter);
        }
    }
    if ((g_fight_mode == FIGHT_MODE_PRACTICE && g_fight_practice_round == 0) ||
        g_fight_mode == FIGHT_MODE_TEAM) {
        if ((fighter->weapon_id == WEAPON_ID_KATANA ||
             fighter->weapon_id == WEAPON_ID_LONG_SWORD) &&
            fighter->move_kind != MOVE_KIND_END_GAME &&
            fighter->move_kind != MOVE_KIND_DTH &&
            fighter->weapon_live_state == WEAPON_LIVE_OFF) {
            fighter->frame_flags =
                (fighter->frame_flags | FIGHTER_DRAW_SCABBARD) &
                ~FIGHTER_SCABBARD_FULL;
        }
        if (fighter->sub_weapon_count > 0 &&
            fighter->sub_weapons[0] == fighter->sub_weapon_kind &&
            fighter->sub_weapon_live_state == WEAPON_LIVE_OFF) {
            fighter->frame_flags =
                (fighter->frame_flags | FIGHTER_DRAW_WORN_SUB) &
                ~FIGHTER_WORN_SUB_FULL;
            weapon_QueueWorn(g_object_weapon_models[fighter->sub_weapon_kind],
                             (fighter_index * 2) | 1, 0);
        }
    }
    if (fighter->frame_flags & FIGHTER_DRAW_WEAPON) {
        weapon_Queue(
            fighter->weapon_model, fighter_index * 2 + WEAPON_DRAW_HELD, 0, 0);
    }
    if (fighter->frame_flags & FIGHTER_DRAW_SUB_WEAPON) {
        weapon_Queue(g_object_weapon_models[fighter->sub_weapon_kind],
                     fighter_index * 2 + WEAPON_DRAW_HELD + 1, 0, 0);
    }
    if (fighter->frame_flags & FIGHTER_DRAW_SCABBARD) {
        weapon_QueueWorn(fighter->weapon_model, fighter_index * 2,
                         (fighter->frame_flags & FIGHTER_SCABBARD_FULL) != 0);
    }
    if (fighter->frame_flags & FIGHTER_DRAW_WORN_SUB) {
        weapon_QueueWorn(g_object_weapon_models[fighter->sub_weapon_kind],
                         (fighter_index * 2) | 1,
                         (fighter->frame_flags & FIGHTER_WORN_SUB_FULL) != 0);
    }
    /* ---- for the next frame ---- */
    fighter->prev_pose = poses[0];
    fighter->prev_pose_heading = root_headings[0];
    fighter->prev_pose_frame = fighter->pose_from_frame;
    if (fighter->blending != 0) {
        if (fighter->blending >= 2) {
            fighter->blending = 1;
        }
        fighter->blend_weight += fighter->blend_step;
        if (fighter->blend_weight >= ONE) {
            fighter->blending = 0;
            fighter->prev_pose = poses[1];
            fighter->prev_pose_heading = root_headings[1];
            fighter->prev_pose_frame = fighter->pose_to_frame;
            fighter->prev_root_offset = roots[1];
        }
    }
    fighter->opponent_bearing =
        ratan2(fighter->other->world_pos.x - fighter->world_pos.x,
               fighter->other->world_pos.z - fighter->world_pos.z);
    fighter->prev_velocity = fighter->velocity;
    fighter_CheckGround(fighter);
    /* ---- for the hit tests ---- */
    fighter_PlacePoints(fighter, SPAD->body_points[fighter_index],
                        SPAD->weapon_points[fighter_index],
                        SPAD->second_weapon_points[fighter_index]);
    /* A fighter's first frame: the weapon's first point has no last place
     * yet. Only the first is given one (the original's behaviour): the
     * second and third keep what the last fight left, and for a pole arm
     * that follows a two-point weapon the third is whatever lay on the
     * scratchpad (see fight_UpdateFighterPair). */
    if (fighter->frames_alive == 1) {
        fighter->prev_weapon_points[0] = SPAD->weapon_points[fighter_index][0];
#if RB_FIX_FIRST_FRAME_SWEEP
        /* the second and the third have no last place either: this
         * frame's sweeps were made from stale ones */
        if (g_rb_settings.bugfixes) {
            fighter->prev_weapon_points[1] =
                SPAD->weapon_points[fighter_index][1];
            fighter->prev_weapon_points[2] =
                SPAD->weapon_points[fighter_index][2];
        }
#endif
    }
    if (fighter->sub_weapon_live_state == WEAPON_LIVE_FIRST_FRAME) {
        fighter->sub_weapon_live_state = WEAPON_LIVE_ON;
        fighter->prev_second_weapon_points[0] =
            SPAD->second_weapon_points[fighter_index][0];
        fighter->prev_second_weapon_points[1] =
            SPAD->second_weapon_points[fighter_index][1];
    }
    if (fighter->weapon_live_state == WEAPON_LIVE_FIRST_FRAME) {
        fighter->weapon_live_state = WEAPON_LIVE_ON;
        /* the second weapon's last places again, not the main weapon's
         * own (the original's behaviour) */
        fighter->prev_second_weapon_points[0] =
            SPAD->second_weapon_points[fighter_index][0];
        fighter->prev_second_weapon_points[1] =
            SPAD->second_weapon_points[fighter_index][1];
    }
    if ((WEAPON_IS_GUN(fighter->weapon_id)) &&
        (fighter->frame_flags & FIGHTER_DRAW_WEAPON)) {
        fighter->gun_pos = SPAD->weapon_points[fighter_index][0];
        fighter->gun_pos_valid = 1;
    } else {
        fighter->gun_pos_valid = 0;
    }
    fighter->blade_motion[0].vx = SPAD->weapon_points[fighter_index][0].x -
                                  fighter->prev_weapon_points[0].x;
    fighter->blade_motion[0].vy = SPAD->weapon_points[fighter_index][0].y -
                                  fighter->prev_weapon_points[0].y;
    fighter->blade_motion[0].vz = SPAD->weapon_points[fighter_index][0].z -
                                  fighter->prev_weapon_points[0].z;
    fighter->blade_motion[1].vx =
        SPAD->second_weapon_points[fighter_index][0].x -
        fighter->prev_second_weapon_points[0].x;
    fighter->blade_motion[1].vy =
        SPAD->second_weapon_points[fighter_index][0].y -
        fighter->prev_second_weapon_points[0].y;
    fighter->blade_motion[1].vz =
        SPAD->second_weapon_points[fighter_index][0].z -
        fighter->prev_second_weapon_points[0].z;
    fighter->lying_face_up = fighter_IsFaceUp(fighter);
    /* is the attack in the frames in which it hits */
    hitting = 0;
    if ((MOVE_IS_ATTACK(fighter->move_kind)) && fighter->attack_armed != 0) {
        if ((fighter->move_frame >= fighter->hit_start_frames[0] &&
             fighter->move_frame <= fighter->hit_end_frames[0]) ||
            (fighter->move_frame >= fighter->hit_start_frames[1] &&
             fighter->move_frame <= fighter->hit_end_frames[1])) {
            hitting = 1;
        }
    }
    fighter->hit_window_open = hitting;
    /* each blade's phase (enum AttackPhase), added up: 1 for an attack, 2
     * more before its hitting frames, 4 more in them */
    fighter->attack_phases[0] = ATTACK_PHASE_NONE;
    fighter->attack_phases[1] = ATTACK_PHASE_NONE;
    if (fighter->attack_armed != 0) {
        if (MOVE_IS_ATTACK(fighter->move_kind)) {
            fighter->attack_phases[0] += 1;
            fighter->attack_phases[1] += 1;
            if (fighter->move_frame < fighter->hit_start_frames[0]) {
                fighter->attack_phases[0] += 2;
            } else if (fighter->move_frame <= fighter->hit_end_frames[0]) {
                fighter->attack_phases[0] += 4;
            }
            if (fighter->move_frame < fighter->hit_start_frames[1]) {
                fighter->attack_phases[1] += 2;
            } else if (fighter->move_frame <= fighter->hit_end_frames[1]) {
                fighter->attack_phases[1] += 4;
            }
        }
    }
    if (fighter->move_kind == MOVE_KIND_DOWN) {
        /* What is kept each frame of a push: three eighths lying on the
         * ground, a sixteenth on a wall, otherwise three quarters; and
         * (below) of its own speed: all but a sixty-fourth, less across
         * the ground while it stands on it. */
        fighter->push_damping_x = 3 * ONE / 8;
        fighter->push_damping_y = 3 * ONE / 8;
        fighter->push_damping_z = 3 * ONE / 8;
    } else if (fighter->move_kind == MOVE_KIND_CLIMB) {
        fighter->push_damping_x = ONE / 16;
        fighter->push_damping_y = ONE / 16;
        fighter->push_damping_z = ONE / 16;
    } else {
        fighter->push_damping_x = 3 * ONE / 4;
        fighter->push_damping_y = 3 * ONE / 4;
        fighter->push_damping_z = 3 * ONE / 4;
    }
    if (fighter->on_ground != 0) {
        fighter->velocity_damping_x = 13 * ONE / 16;
        fighter->velocity_damping_y = 63 * ONE / 64;
        fighter->velocity_damping_z = 13 * ONE / 16;
    } else {
        fighter->velocity_damping_x = 63 * ONE / 64;
        fighter->velocity_damping_y = 63 * ONE / 64;
        fighter->velocity_damping_z = 63 * ONE / 64;
    }
    /* beaten this frame: the death cry, and the weapon falls */
    if (fighter->is_defeated == 0 &&
        ((fighter->move_started != MOVE_START_NONE &&
          fighter->prev_move_kind != MOVE_KIND_END_GAME &&
          fighter->prev_move_kind != MOVE_KIND_DTH &&
          (fighter->move_kind == MOVE_KIND_END_GAME ||
           fighter->move_kind == MOVE_KIND_DTH)) ||
         (fighter->move_kind == MOVE_KIND_PAIRED &&
          fighter_index != g_fight_paired_move_initiator &&
          fighter->paired_hit_frame == fighter->move_frame))) {
        MatrixSlot *bone_matrices = model_GetBoneMatrices(fighter_index);
        if (g_fight_mode != FIGHT_MODE_STORY ||
            g_fighter_clans[g_fighters[0].character] == 0 ||
            g_story_chapter != 1 || fighter_index != g_story_chapter) {
            fight_RaiseEvent(
                fighter_index, FIGHT_EVENT_DEATH_CRY, &fighter->world_pos.x, 0);
        }
        if (fighter->roster_id != ROSTER_VICTIM) {
#if RB_PORT
            /* a mod's weapon may fall as another weapon does (`dropped =`,
             * port_weapons.h) */
            s32 clan = g_fighter_clans[fighter->character];

            object_DropWeapon(
                fighter,
                port_WeaponDropped(
                    fighter_index, fighter->weapon_id, clan,
                    g_object_dropped_weapon_kinds[clan][fighter->weapon_id]),
                (Vec3 *)PTR(bone_matrices[FIGHTER_BONE_WEAPON_HAND])->t);
#else
            object_DropWeapon(
                fighter,
                g_object_dropped_weapon_kinds
                    [g_fighter_clans[fighter->character]][fighter->weapon_id],
                (Vec3 *)PTR(bone_matrices[FIGHTER_BONE_WEAPON_HAND])->t);
#endif
        }
        fighter->is_defeated = 1;
        if (g_fight_winner == FIGHT_NOBODY) {
            g_fight_winner = fighter_index == 0;
        }
    }
    if (fighter->move_started != MOVE_START_NONE &&
        fighter->roster_id == ROSTER_ISOHACHI &&
        fighter->move_kind == MOVE_KIND_SUBWEP && fighter->stun_charges == 0) {
        fighter->pending_event = MOVE_EVENT_NO_STUN_LEFT;
    }
    /* ---- what the move does at this frame ---- */
    if (fighter->same_frame == 0) {
        if (g_fight_mode != FIGHT_MODE_TEAM &&
            g_fight_mode != FIGHT_MODE_PRACTICE &&
            g_fight_mode != FIGHT_MODE_CHANBARA &&
            (g_fight_mode != FIGHT_MODE_STORY || g_story_victim == 0)) {
            if (fighter->move_kind == MOVE_KIND_SUBWEP &&
                fighter->move_frame == fighter->release_frame) {
                if (fighter->roster_id == ROSTER_ISOHACHI) {
                    if (fighter->stun_charges != 0) {
                        fighter->stun_charges--;
                    }
                } else if (fighter->off_arm_ok != 0 &&
                           object_ThrowSubWeapon(fighter) ==
                               fighter->sub_weapon_kind) {
                    fighter->two_sword_ready = 0;
                }
            }
        }
        if (fighter->move_kind == MOVE_KIND_DOWN) {
            if (object_PickUp(fighter) == fighter->sub_weapon_kind) {
                fighter->two_sword_ready = fighter->off_arm_ok;
            }
        } else if (fighter->off_arm_ok != 0 &&
                   (fighter->move_kind == MOVE_KIND_CROUCH_STEP ||
                    fighter->move_kind == MOVE_KIND_CROUCH) &&
                   fighter->move_frame == fighter->pickup_frame &&
                   object_PickUp(fighter) == fighter->sub_weapon_kind) {
            fighter->two_sword_ready = 1;
        }
        if (fighter->move_kind == MOVE_KIND_DIRT &&
            fighter->move_frame == fighter->release_frame &&
            SURFACE_IN(fighter->surface, SURFACES_LOOSE) &&
            fighter->off_arm_ok != 0) {
            object_ThrowDirt(
                fighter, SURFACE_IN(fighter->surface, SURFACES_SAND)
                             ? DIRT_EARTH
                             : DIRT_WATER);
        }
        if (fighter->move_kind == MOVE_KIND_RELOAD &&
            fighter->move_frame == fighter->reload_frame &&
            fighter->off_arm_ok != 0 && fighter->reloads_left != 0) {
            fighter->ammo = FIGHTER_GUN_SHOTS;
            fighter->reloads_left--;
            fight_RaiseEvent(
                fighter_index, FIGHT_EVENT_RELOAD, &fighter->world_pos.x, 0);
        }
        if (fighter->same_frame == 0 &&
            (fighter->move_kind != MOVE_KIND_PAIRED ||
             fighter_index == g_fight_paired_move_initiator)) {
            fighter->frame_flags |= FIGHTER_FRAME_EVENTS;
            fighter_RunFrameEvents(fighter);
        }
    }
    if (fighter->off_arm_ok != 0) {
        fighter->frame_flags |= FIGHTER_OFF_ARM_OK;
    }
    if (fighter->is_defeated != 0) {
        if (g_sys_screen != SCREEN_AFTER_FIGHT_UPDATE) {
            fighter->face = MODEL_FACE_DEAD;
        }
    } else {
        fighter->face = MODEL_FACE_NORMAL;
    }
    if (fighter->roster_id == ROSTER_VICTIM) {
        fighter->face = MODEL_FACE_DEAD;
    }
    model_SetFace(fighter_index, fighter->face);
    fighter_UpdateBleeding(fighter);
    if (fighter->move_started == MOVE_START_PAIRED) {
        fighter->move_started = MOVE_START_NONE;
    }
    replay_RecordFighter(fighter);
}

/* ---- fight lock ---- */

/* The blade lock (fight.h): a frame of two fighters with their blades
 * locked. */

/* How far out of the lock's distance, and how far above one another, the
 * two can get before it breaks. */
#define LOCK_BREAK_DISTANCE 200
#define LOCK_BREAK_HEIGHT 1000

/* A frame of the lock, in place of the tests between the two fighters. The
 * two are held at the lock's distance. A lock that one of them ran into
 * goes that one's way at once. Otherwise each press of an attack button
 * adds the fighter's power to its score, and every LOCK_ROUND_FRAMES the
 * one with the higher score pushes the other back (a second sword that is
 * out may be lost by it) and the scores start again. While the lock is
 * even, or going a fighter's way by one step, it can be dragged sideways
 * with the pad; holding away from the opponent in an even lock lets go.
 * The lock breaks if the two come too far apart. Last, the lock's flash is
 * started between the two, the next when the one before has run. */
void fight_UpdateLock(void) {
    Fighter *first;
    Fighter *second;
    Fighter *fighter;
    s32 distance_error;
    s32 i;
    s32 runner;
    s32 drag;
    s32 flash;
    s32 flash_pos[3];

    distance_error = fight_HoldLockDistance();
    first = g_fighters;
    g_fight_blades_met = -1;
    if (g_fighters[0].move_kind == MOVE_KIND_START) {
        /* on the fifth frame of the lock's beginning */
        if (g_fighters[0].move_frame == 4 &&
            (runner = g_fight_lock_runner) != FIGHT_NOBODY) {
            /* FAKE: locals of their own for the runner and the other */
            first = &g_fighters[runner];
            second = g_fighters;
            if (runner == 0) {
                second++;
            }
            first->pending_event = LOCK_EVENT_WIN;
            second->pending_event = LOCK_EVENT_LOSE;
        }
    } else if (g_fighters[0].move_kind != MOVE_KIND_LOCK) {
        for (i = 0; i < 2; i++) {
            fighter = first + i;
            if (fighter->pad.pressed & FIGHTER_PAD_FRONTAL_ATTACK) {
                fighter->lock_score += fighter->power;
            }
            if (fighter->pad.pressed & FIGHTER_PAD_REVERSE_ATTACK) {
                fighter->lock_score += fighter->power;
            }
            if (fighter->move_kind == MOVE_KIND_LOCK_EVEN ||
                fighter->move_kind == MOVE_KIND_LOCK_WINNING_1 ||
                fighter->move_kind == MOVE_KIND_LOCK_LOSING_1) {
                if (fighter->pad.held & FIGHTER_PAD_SIDE_A) {
                    drag = 1;
                } else if (fighter->pad.held & FIGHTER_PAD_SIDE_B) {
                    drag = -1;
                } else {
                    drag = 0;
                }
                /* across the way it faces, 16 units a frame */
                fighter->push.vx -=
                    (g_math_sin_table[(fighter->orientation.vy +
                                       ANGLE_TURN / 4) &
                                      ANGLE_MASK] *
                     drag) /
                    (ONE / 16);
                fighter->push.vz +=
                    (g_math_sin_table[fighter->orientation.vy & ANGLE_MASK] *
                     drag) /
                    (ONE / 16);
            }
        }
        first = g_fighters;
        second = first + 1;
        g_fight_lock_frames++;
        if (g_fight_lock_frames >= LOCK_ROUND_FRAMES) {
            if (g_fighters[0].lock_score > g_fighters[1].lock_score) {
                g_fighters[0].pending_event = LOCK_EVENT_WIN;
                g_fighters[1].pending_event = LOCK_EVENT_LOSE;
                if (g_fighters[1].move_kind == MOVE_KIND_LOCK_LOSING_2) {
                    fighter_TryDropSecondSword(second);
                }
                fight_RaiseEvent(1, FIGHT_EVENT_GRUNT, &second->world_pos.x, 0);
            } else if (g_fighters[1].lock_score > g_fighters[0].lock_score) {
                g_fighters[1].pending_event = LOCK_EVENT_WIN;
                g_fighters[0].pending_event = LOCK_EVENT_LOSE;
                if (g_fighters[0].move_kind == MOVE_KIND_LOCK_LOSING_2) {
                    fighter_TryDropSecondSword(first);
                }
                fight_RaiseEvent(0, FIGHT_EVENT_GRUNT, &first->world_pos.x, 0);
            }
            second->lock_score = 0;
            first->lock_score = 0;
            g_fight_lock_frames = 0;
        }
        if (distance_error > LOCK_BREAK_DISTANCE) {
            first->pending_event = LOCK_EVENT_BREAK;
            second->pending_event = LOCK_EVENT_BREAK;
        } else {
            s32 height = first->world_pos.y - second->world_pos.y;
            if (height < 0) {
                height = -height;
            }
            if (height >= LOCK_BREAK_HEIGHT) {
                first->pending_event = LOCK_EVENT_BREAK;
                second->pending_event = LOCK_EVENT_BREAK;
            }
        }
        if (first->move_kind == MOVE_KIND_LOCK_EVEN) {
            if (first->pad.held & FIGHTER_PAD_AWAY) {
                if (second->pad.held & FIGHTER_PAD_AWAY) {
                    first->pending_event = LOCK_EVENT_BREAK;
                    second->pending_event = LOCK_EVENT_BREAK;
                } else {
                    first->pending_event = LOCK_EVENT_BREAK;
                    second->pending_event = LOCK_EVENT_LET_GO;
                }
            } else if (second->pad.held & FIGHTER_PAD_AWAY) {
                first->pending_event = LOCK_EVENT_LET_GO;
                second->pending_event = LOCK_EVENT_BREAK;
            }
        }
    }
    first = g_fighters;
    second = &g_fighters[1];
    if (g_fight_lock_flash_delay == 0) {
        /* which flash, by how the lock stands for the first fighter */
        switch (g_fighters[0].move_kind) {
        case MOVE_KIND_LOCK:
            flash = -1;
            break;
        case MOVE_KIND_START:
            flash = 0;
            break;
        case MOVE_KIND_LOCK_LOSING_2:
            flash = 1;
            break;
        case MOVE_KIND_LOCK_LOSING_1:
            flash = 2;
            break;
        case MOVE_KIND_LOCK_EVEN:
            flash = 3;
            break;
        case MOVE_KIND_LOCK_WINNING_1:
            flash = 4;
            break;
        case MOVE_KIND_LOCK_WINNING_2:
            flash = 5; /* the last of the six */
            break;
        }
        if (flash >= 0) {
            flash_pos[0] = (first->world_pos.x + second->world_pos.x) / 2;
            flash_pos[1] = (first->base_pos.y + second->base_pos.y) / 2;
            flash_pos[2] = (first->world_pos.z + second->world_pos.z) / 2;
            flash_pos[0] +=
                (g_math_sin_table[first->opponent_bearing & ANGLE_MASK] *
                 g_fight_lock_flash_places[flash].ahead) >>
                12;
            flash_pos[1] += g_fight_lock_flash_places[flash].up;
            flash_pos[2] +=
                (g_math_sin_table[(first->opponent_bearing + ANGLE_TURN / 4) &
                                  ANGLE_MASK] *
                 g_fight_lock_flash_places[flash].ahead) >>
                12;
            fight_RaiseEvent(
                0, g_fight_lock_flash_events[flash], flash_pos, NULL);
        }
    } else {
        g_fight_lock_flash_delay--;
    }
}
