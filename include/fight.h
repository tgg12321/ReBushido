#ifndef FIGHT_H
#define FIGHT_H

/* The fight around the two fighters: what the next fight is to be, the
 * screens that set one up, run it and follow it, and what each mode makes of
 * a fight that has ended.
 *
 * The menus leave their choices in g_fight_config and fight_StartMode
 * (fight_start.c) starts the mode. SCREEN_FIGHT_INIT (fight_Init) loads what
 * the fight needs and SCREEN_FIGHT_UPDATE (fight_Update) runs a frame of it:
 * the camera, the two fighters (fighter.h), the tests between them, the
 * loose objects, the mode's part of the display, and, two seconds after a
 * fighter has fallen, the mode's decision about what comes next
 * (fight_UpdateDefeat). The modes, by the names the game has for them:
 *
 * - STORY: a script for the player's character (story.h) sets up each fight
 *   and plays cutscenes between them.
 * - VSCPU: the other characters one after another, in a shuffled order.
 * - PRACTICE (the menu's Training): against a partner that only stands
 *   there, or a "practice round" against a chosen opponent, first to more
 *   wins.
 * - CHANBARA (the menu's Slash): a hundred opponents one after another
 *   through four areas of one stage, against the clock.
 * - VS and NETWORK (the link cable): two players.
 * - TEAM (the menu's Group Battle): three to five matches under a judge,
 *   with points, warnings for leaving the court and a time limit.
 *
 * Team battle's variables are g_team_, chanbara's g_chanbara_, the
 * opponents of VSCPU g_vscpu_; the story's are in story.h.
 *
 * Files: fight.c (the fight's screens), fight_modes.c (the modes' progress),
 * fight_after.c (the screens that follow a fight), fighter.c (the blade
 * lock), fight_event.c (what is heard and seen), fight_start.c. */

#include "common.h"
#include <psxsdk/libgte.h>
#include "hud.h"
#include "motion.h"

/* ---- what the next fight is to be ---- */

/* The modes (the list at the head of this file says what each is). */
enum FightMode {
    FIGHT_MODE_STORY = 0,
    FIGHT_MODE_VSCPU = 1,
    FIGHT_MODE_PRACTICE = 2,
    FIGHT_MODE_CHANBARA = 3,
    FIGHT_MODE_VS = 4,
    FIGHT_MODE_TEAM = 5,
    FIGHT_MODE_NETWORK = 6,
};

/* What the menus, the debug menu and the modes' own progress ask for.
 * fight_StartMode and the fight's set-up read it; mode, stage, view and
 * every_hit_kills become the fight's own variables in fight_ApplyConfig. */
typedef struct {
    u16 cpu_levels[2]; /* how well the CPU plays each fighter */
    /* for each fighter: */
    s8 roster[2]; /* who: an index into the roster (fighter.h) */
    s8 weapon[2]; /* enum WeaponId; FIGHT_WEAPON_SAME: the other's */
    s8 is_cpu[2];
    u8 story_cpu_levels[2]; /* the story script's: a sixteenth of a level */
    s8 stage;
    s8 mode; /* enum FightMode */
    s8 first_person;
    s8 every_hit_kills; /* the debug menu's */
} FightConfig;

#define FIGHT_WEAPON_SAME 0xFF
#define FIGHT_CPU_LEVEL_DEFAULT 0x800

extern FightConfig g_fight_config;

/* ---- the fight ---- */

extern s16 g_fight_mode; /* enum FightMode */
extern s16 g_fight_stage;
extern s8 g_fight_loaded_stage; /* the stage in memory, or -1 */
extern s16 g_fight_first_person;
extern s16 g_fight_viewer; /* the fighter a first-person fight looks through */
/* The debug menu's "D": the side view follows the first fighter alone and
 * the stage is not drawn. */
extern u8 g_fight_debug_view;
extern s32 g_fight_every_hit_kills;
extern MATRIX *g_fight_squashed_view;
extern u16 g_fight_rng_seed; /* over the link cable both consoles use one */

/* Set when a fight is to start again with what is loaded: a rematch, the
 * next round of practice or of a team battle's match. */
extern u8 g_fight_restart;
/* Where the fighters stand when it does: the pair of start positions
 * nearest the winner. */
extern u8 g_fight_restart_pair;
/* The pair of start positions in use: chanbara's area. */
extern u8 g_fight_start_pair;

/* Frames since a fighter fell; FIGHT_DEFEAT_FRAMES later the mode decides
 * what follows. */
extern s16 g_fight_defeat_frames;
#define FIGHT_DEFEAT_FRAMES 60
/* g_fight_winner and g_fight_lock_runner when they name neither fighter. */
#define FIGHT_NOBODY (-1)
/* The fighter left standing when the other fell, or FIGHT_NOBODY: whom the
 * replay, the winner's view and the kaishaku follow. */
extern s8 g_fight_winner;

/* Fights won by each player since the mode was started: shown in versus
 * and in a practice round, and the CPU of a practice round gets better with
 * the player's.
 *
 * A variable for each player, not an array of two, here and for the story's
 * wounds, the team battle's points and warnings and the blow's direction: the
 * code that names one forms its address at every use, which the compiler
 * does for a variable and not for an array's first element (as an array,
 * cpu_Init keeps the address in a register). The original still reaches
 * the second from the first where it has a player's number,
 * `(&g_fight_wins_p1)[winner]`. That works because the original's linker
 * laid the zeroed variables out in the order of the sum of the letters of
 * their names, and two names that differ by one in their last letter come
 * out side by side, as Sony's _svm_okon1 and _svm_okon2 do. */
extern u8 g_fight_wins_p1;
extern u8 g_fight_wins_p2;
/* Times "again" was chosen: the story's and chanbara's rewards are for a
 * run without any. */
extern s32 g_fight_continues;

/* Practice: the pause menu's rows of options, a bit each, and the frames
 * until the first kind of practice pauses itself at its start. */
extern u8 g_fight_practice_options;
extern u8 g_fight_auto_pause_countdown;
/* 0: against the partner that stands there; 1: a practice round. */
extern u8 g_fight_practice_round;
/* The colour of the practice partner, for each clan of the player. */
extern u8 g_fight_practice_tints[3][4];

/* The texture groups of the wounds each fighter has taken
 * (fighter_MarkStoryWound). Only story mode marks them, and only the
 * player's, which it keeps through a chapter; the opponent's are cleared
 * with each new opponent and never set. */
extern u8 g_fight_story_wounds;
extern u8 g_fight_story_wounds_p2;

/* The fight's screens (enum Screen): the fight, the menu over the loser
 * that follows it, and the camera's swing round to the winner. */
extern void fight_Init(void);
extern void fight_Update(void);
extern void fight_AfterFightInit(void);
extern void fight_AfterFightUpdate(void);
extern void fight_WinnerViewInit(void);
extern void fight_WinnerViewUpdate(void);

extern void fight_InitConfig(void);
extern void fight_StartMode(void);
/* Loads the stage and the sounds that go with it, unless they are the ones
 * in memory. */
extern void fight_LoadStage(void);
/* Sets fighter `fighter_index` up from g_fight_config and loads its model. */
extern void fight_LoadFighter(s32 fighter_index);
/* Loads the weapons the two fighters hold and carry. */
extern void fight_LoadWeapons(void);
/* Loads one fighter's weapon and nothing else. */
extern void fight_LoadWeaponOf(s32 fighter_index);

/* The menu that follows a fight (SCREEN_AFTER_FIGHT, SCREEN_AFTER_MATCH). */
enum FightChoice {
    FIGHT_CHOICE_REMATCH = 0,
    FIGHT_CHOICE_SUB_MENU = 1, /* back to the mode's page of the menu */
    FIGHT_CHOICE_TOP_MENU = 2,
    FIGHT_CHOICE_COUNT = 3,
};

extern u8 g_fight_choice;       /* the row the cursor is on */
extern u8 g_fight_first_choice; /* the topmost row on offer */
/* Whether "again" is on offer. Nothing ever clears it. */
extern u8 g_fight_rematch_offered;
/* Frames since a choice was made; it takes effect at FIGHT_CHOICE_FRAMES. */
extern u8 g_fight_choice_timer;
#define FIGHT_CHOICE_FRAMES 60

/* ---- where the fighters start (fighter.c, scene_frame.c) ---- */

/* By stage: four pairs of places, a fighter's each (resource_data.c). */
#define FIGHT_START_PAIRS 4
/* The halfwords of a point, of a pair and of a stage. */
#define FIGHT_START_POINT_SIZE 3
#define FIGHT_START_PAIR_SIZE (2 * FIGHT_START_POINT_SIZE)
#define FIGHT_START_STAGE_SIZE (FIGHT_START_PAIRS * FIGHT_START_PAIR_SIZE)
extern s16 g_fight_start_positions[STAGE_COUNT][FIGHT_START_PAIRS][2]
                                  [FIGHT_START_POINT_SIZE];

/* The place of fighter `fighter_index` in pair `pair_index` of the stage's. */
extern void fight_GetStartPosition(
    s32 fighter_index, s32 *position_out, s32 pair_index);
/* The pair whose places are nearest `position`. */
extern s32 fight_FindNearestStartPair(Vec3 *position);
/* The stage's row of g_fight_start_positions. */
extern void *fight_GetStartPositions(void);

/* ---- what is heard and seen (fight_event.c) ---- */

/* Things that happen in a fight and are heard or seen: fight_RaiseEvent
 * plays the sound or starts the effect, and the replay records the event to
 * raise it again. The sounds from FIGHT_EVENT_HEAD_CUT on are a fighter's
 * own. Named from the code and the effect each one starts. */
enum FightEvent {
    FIGHT_EVENT_SPARK = 1,     /* metal on something hard */
    FIGHT_EVENT_CLASH = 2,     /* two attacks meet */
    FIGHT_EVENT_HIT = 3,       /* a blow lands */
    FIGHT_EVENT_BLOOD = 4,     /* a wound bleeds */
    FIGHT_EVENT_LOCK = 5,      /* locked blades, and three more ... */
    FIGHT_EVENT_LOCK_SLOW = 6, /* ... as the lock goes one way */
    FIGHT_EVENT_LOCK_MEDIUM = 7,
    FIGHT_EVENT_LOCK_FAST = 8,
    FIGHT_EVENT_SPLASH = 9, /* a foot in water */
    FIGHT_EVENT_DUST = 10,
    FIGHT_EVENT_SHOT = 11,          /* a gun fires */
    FIGHT_EVENT_THROWN_EARTH = 12,  /* dirt thrown up */
    FIGHT_EVENT_THROWN_WATER = 13,  /* water thrown up */
    FIGHT_EVENT_BULLET_IMPACT = 14, /* a bullet, or the bursting kind */
    FIGHT_EVENT_MATCHED_CLASH = 15, /* two attacks of one kind meet */
    FIGHT_EVENT_PAIRED_MOVE = 16,   /* one fighter catches the other */
    FIGHT_EVENT_PICK_UP = 17,
    FIGHT_EVENT_ARMOR_SPARK = 18,
    FIGHT_EVENT_COLLIDE = 33, /* a runner into the other fighter */
    FIGHT_EVENT_HEAD_CUT = 34,
    FIGHT_EVENT_BODY_CUT = 35,
    FIGHT_EVENT_LIMB_CUT = 36,
    FIGHT_EVENT_CLANG = 37, /* blade on blade */
    FIGHT_EVENT_HARD_CLANG = 38,
    FIGHT_EVENT_SECOND_SWORD_CUT = 39,
    FIGHT_EVENT_SHEATHED_HIT = 40,
    FIGHT_EVENT_WEAPON_LANDS = 41, /* a dropped weapon on the ground */
    FIGHT_EVENT_THROW = 42,        /* also a sidearm's shot */
    FIGHT_EVENT_OBJECT_HITS = 43,  /* a thrown object on a fighter */
    FIGHT_EVENT_OBJECT_LANDS = 44, /* a sub-weapon on the ground */
    FIGHT_EVENT_GRUNT = 45,        /* a fighter beaten back */
    FIGHT_EVENT_DEATH_CRY = 46,
    FIGHT_EVENT_HOP = 47, /* the hopping kind's own sound */
    FIGHT_EVENT_VANISH = 48,
    FIGHT_EVENT_RELOAD = 49,
    FIGHT_EVENT_EMPTY_GUN = 50,
};

/* The sounds a fighter has of its own (snd.h: 40 each, from
 * SND_FIGHTER_FIRST; the second fighter's are SND_FIGHTER_COUNT further
 * on), as far as the code plays them by name, and three of the sounds every
 * fight has. Named from what plays them; a motion's frame events play these
 * and the rest by number. */
enum {
    /* where a fighter's three banks begin (snd.h): its model's (the body
     * and the voice), its weapon's, and its class's (the footsteps) */
    SND_FIGHTER_MODEL_BANK = 0x21,
    SND_FIGHTER_WEAPON_BANK = 0x31,
    SND_FIGHTER_FOOTSTEP = 0x41, /* four for each class of ground */
    /* the voice: seven sounds, GRUNT the fifth and DEATH_CRY the seventh */
    SND_FIGHTER_VOICE = 0x25,

    SND_FIGHTER_THROW = 0x21,
    SND_FIGHTER_IMPACT = 0x22, /* its weapon or what it threw, landing */
    SND_FIGHTER_OBJECT_LANDS = 0x23,
    SND_FIGHTER_GRUNT = 0x29,
    SND_FIGHTER_DEATH_CRY = 0x2B,
    SND_FIGHTER_KAISHAKU_VOICE = 0x2D, /* the winner's, in the kaishaku */
    /* the story's henchmen: the first of a band, and one of two for each
     * of the rest */
    SND_FIGHTER_CHALLENGE = 0x2E,
    SND_FIGHTER_SHOUT_A = 0x2F,
    SND_FIGHTER_SHOUT_B = 0x30,
    SND_FIGHTER_SHOT = 0x31, /* the kaishaku plays it for any weapon */
    SND_FIGHTER_RELOAD = 0x32,
    SND_FIGHTER_EMPTY_GUN = 0x33,
    SND_FIGHTER_LIMB_CUT = 0x34,
    SND_FIGHTER_CUT = 0x35,
    SND_FIGHTER_KAISHAKU_CUT = 0x36,
    SND_FIGHTER_PICK_UP = 0x39,
    SND_FIGHTER_CLANG = 0x3A,
    SND_FIGHTER_HARD_CLANG = 0x3B,
    SND_FIGHTER_LOCK = 0x3C,
    SND_FIGHTER_SHEATHED_HIT = 0x3E,
    SND_FIGHTER_WEAPON_LANDS = 0x40,

    SND_FIGHT_THROWN_EARTH = 0x78,
    SND_FIGHT_THROWN_WATER = 0x79,
    SND_FIGHT_COLLIDE = 0x7A,
};

/* `fighter_index` picks whose sounds; `dir` is a direction for the effects
 * that have one, or NULL. */
extern void fight_RaiseEvent(s32 fighter_index, s32 event, s32 *pos, s16 *dir);

/* ---- the two fighters: their distance, paired moves, the blade lock ---- */

/* How far apart the fighters are, and how much further than a frame ago
 * (fighter_pose.c). */
extern s32 g_fight_distance;
extern s32 g_fight_distance_change;
extern void fight_UpdateDistance(void);

/* A frame of the two fighters against each other (fighter_hit.c): blade
 * against blade, blade and bullet against body, body against body. What a
 * hit then does to a fighter is fighter_TakeHit (fighter.h). */
extern void fight_UpdateFighterPair(void);
/* Outcomes of a clash (MOVE_EVENT_CLASH_...), by the gap in power, who is
 * the stronger, and whether the two attacks were of one kind. */
extern u8 g_fight_clash_events[3][2][2];
/* In a blade lock: keeps the two at the lock's distance; returns how far
 * off it they are. */
extern s32 fight_HoldLockDistance(void);
/* Puts back the stage's props that a blade has cut down. */
extern void fight_ResetObstacles(void);

/* The direction of the blow being dealt, for the blood: one vector, in
 * three variables. They are three by the code: fighter_TakeHit keeps the
 * first's address in a register and still forms it again to read it, which
 * the compiler does for a variable and not for a member or an element. */
extern s16 g_fight_blow_dir_x;
extern s16 g_fight_blow_dir_y;
extern s16 g_fight_blow_dir_z;
/* How near a fighter must be for ROSTER_ISOHACHI to stagger it. */
extern s32 g_fight_stun_reach;

/* A paired move is one motion for both fighters: one catches the other
 * (the "initiator") and both play it out, held `separation` apart; from
 * `break_frame` until its blow lands the move breaks off if they come
 * apart. A paired move can lead to another (`start_pending`, with the
 * move, its command's flags and where it is from); and two that fall off
 * something in one fall together (`falling_fighter`, or
 * FIGHT_NOBODY_FALLING). */
#define FIGHT_NOBODY_FALLING 0xFF
extern s16 g_fight_paired_move_initiator;
extern s32 g_fight_paired_move_separation;
extern u8 g_fight_paired_move_break_frame;
extern s16 g_fight_paired_move_start_pending;
extern MoveScript *g_fight_paired_move;
extern u16 g_fight_paired_move_flags;
extern s16 g_fight_paired_move_is_common;
extern u8 g_fight_falling_fighter;
extern u8 g_fight_falling_forward;

/* A paired move is a "karami" in the game's move names (MOVE_KIND_PAIRED).
 * What a paired move's event table answers, in place of enum MoveEvent's
 * first numbers: the two fall off something, and the same for a fighter
 * that is dead; or the move breaks off. */
enum PairedMoveEvent {
    PAIRED_EVENT_FALL_BACKWARD = 0,
    PAIRED_EVENT_FALL_FORWARD = 1,
    PAIRED_EVENT_DEAD = 2, /* added to either */
    /* the two have come apart before the blow: for the one that began the
     * move, and for the other */
    PAIRED_EVENT_BREAK = 4,
    PAIRED_EVENT_BROKEN = 5,
};

/* The blade lock, "seri" in the game's move names (MOVE_KIND_LOCK,
 * MOVE_KIND_LOCK_EXIT; fighter.c): two blades that meet striking lock, the
 * fighters push (lock_score: a press of an attack button adds the
 * fighter's power), and every LOCK_ROUND_FRAMES the stronger pushes the
 * other back. The lock goes two steps either way from even
 * (MOVE_KIND_LOCK_WINNING_1 and 2 for the one winning, MOVE_KIND_LOCK_LOSING_1
 * and 2 for the other); a round won at the second step ends it. `blades_met`
 * says which blades met this frame (a bit each; -1: none), `lock_requested`
 * that a lock is to begin, `lock_runner` the fighter that ran into the other to
 * begin it (FIGHT_NOBODY: two blades did), who wins its first exchange. */
extern s16 g_fight_blades_met;
extern s16 g_fight_lock_requested;
extern s16 g_fight_lock_runner;
extern s16 g_fight_lock_frames;
#define LOCK_ROUND_FRAMES 43

/* What a lock's event table answers, in place of enum MoveEvent's first
 * numbers. */
enum LockEvent {
    LOCK_EVENT_FALL_FORWARD = 0, /* off something, and the other breaks */
    LOCK_EVENT_FALL_BACKWARD = 1,
    LOCK_EVENT_BREAK = 2,  /* the two part */
    LOCK_EVENT_WIN = 3,    /* pushes the other back */
    LOCK_EVENT_LOSE = 4,   /* is pushed back */
    LOCK_EVENT_LET_GO = 5, /* the other stepped away from an even lock */
};

/* A lock's flashes: between the fighters, for each kind of lock move, a
 * place (how far towards the second fighter, how far up) and the event that
 * starts the flash; the next is not started until the last has run. */
typedef struct {
    s16 ahead;
    s16 up;
} LockFlashPlace;

extern LockFlashPlace g_fight_lock_flash_places[6];
extern u8 g_fight_lock_flash_events[6];
extern s16 g_fight_lock_flash_delay;

extern void fight_UpdateLock(void);

/* ---- the modes (fight_modes.c) ---- */

/* SaveRecord.unlock_mask has a bit for each character. */
#define UNLOCK_BIT(character) (1 << (character))

/* VSCPU: the characters to be fought, in order; how many, and how many
 * have been met. Each fights with the next of a list of weapons, on its own
 * stage. */
extern u8 g_vscpu_order[24];
extern u8 g_vscpu_count;
extern u8 g_vscpu_faced;
extern u8 g_vscpu_weapon_cursor;
extern u8 g_vscpu_weapons[20];                 /* by position in the run */
extern u8 g_vscpu_stages[CHARACTER_COUNT + 1]; /* by character */
/* A run that meets this many is rewarded with an ending. */
#define VSCPU_FULL_RUN 20

extern void fight_BuildVsCpuOrder(void);
extern void fight_NextVsCpuOpponent(void);

/* Chanbara: the hundred opponents (their kind and colour), how many have
 * come on, and the one that is on: its kind, model and colour. There are
 * five kinds, the last the single final opponent; the tables that go by
 * kind have a row for each clan of the player. */
#define CHANBARA_OPPONENTS 100
#define CHANBARA_LAST_TEN 91 /* the first of the last ten */
#define CHANBARA_KINDS 5
#define CHANBARA_LAST_KIND 4

extern u8 g_chanbara_opponents[CHANBARA_OPPONENTS][4];
extern u8 g_chanbara_count;
extern u8 g_chanbara_kind;
extern u8 g_chanbara_model;
extern CVECTOR g_chanbara_tint; /* the opponent's colour */
extern u8 g_chanbara_models[2][CHANBARA_KINDS];
extern u8 g_chanbara_rosters[2][CHANBARA_KINDS];
/* Where each kind's voice is among chanbara's sounds. */
extern s32 g_chanbara_voice_offsets[CHANBARA_KINDS];
/* Set when the opponent that has come on is in the stage's next area. */
extern u8 g_chanbara_area_changed;
/* The run's time in frames (30 a second), and where it came among the
 * three best times (3: nowhere). */
extern s32 g_chanbara_time;
extern u8 g_chanbara_rank;
/* the clock stops at 99'59"99 */
#define CHANBARA_TIME_MAX (100 * FRAMES_PER_MINUTE)
/* a run under a quarter of an hour wins a gunner */
#define CHANBARA_FAST_TIME (15 * FRAMES_PER_MINUTE)
/* half a minute a continue */
#define CHANBARA_CONTINUE_PENALTY (30 * FRAMES_PER_SECOND)
/* Set when the run has just won a character: for its speed, and for
 * needing no continue. */
extern u8 g_chanbara_fast_unlock;
extern u8 g_chanbara_clean_unlock;

/* Brings on chanbara's next opponent and returns 1; after the last, ends
 * the run and returns 0. */
extern s32 fight_NextChanbaraOpponent(void);
extern void fight_GetChanbaraTime(ClockTime *time);
extern void fight_LeaveBestTimes(void);

/* Team battle. A battle is `match_count` matches, each between the two
 * teams' fighters of one place. A match is won by `points_to_win` points
 * (a kill is a point; so are two warnings to the other side, given for
 * stepping over the court's line: "WARNING" is the word beside the marks),
 * or on points and then warnings when the time runs out. The judge's
 * calls are timed by a timer each, of which at most one runs; the fighters
 * cannot move while one does.
 *
 * The points and warnings of the match being fought are a variable for each
 * side, the second reached from the first where the code has a side
 * (see g_fight_wins_p1). */
extern u8 g_team_points;
extern u8 g_team_points_p2;
extern u8 g_team_warnings;
extern u8 g_team_warnings_p2;
extern u8 g_team_match_count;
extern u8 g_team_match; /* the match being fought, from 0 */
extern u8 g_team_points_to_win;
extern u8 g_team_time_limit; /* seconds, or 0 */
extern u8 g_team_time_limits[4];
extern s32 g_team_time_frames; /* the match's clock */
extern u8 g_team_clan_bits;    /* each side's clan: it picks the stage */
extern u8 g_team_rosters[5][2];
extern u8 g_team_weapons[5][2];
/* Who won each match (enum TeamResult), and its points. */
extern u8 g_team_results[];
extern u8 g_team_scores[8][2];
/* The scores two bits a side, for the table (hud_DrawTeamScores). */
extern s32 g_team_packed_scores;
extern u8 g_team_winner; /* the battle's, enum TeamResult */

/* Who won a match or the battle (g_team_results, g_team_winner). */
enum TeamResult {
    TEAM_RESULT_SIDE_1 = 0,
    TEAM_RESULT_SIDE_2 = 1,
    TEAM_RESULT_DRAW = 2,
};

/* The judge's calls (the sounds of the team bank, the disc's JADGE.SE,
 * from SND_JUDGE_FIRST). Named
 * from when each is played; what is said has not been heard. */
enum JudgeSound {
    SND_JUDGE_BEGIN = 0x9C,   /* a round starts */
    SND_JUDGE_BREAK = 0x9D,   /* both are down: the round is stopped */
    SND_JUDGE_TIME_UP = 0x9E, /* the clock has run out */
    SND_JUDGE_OUT = 0x9F,     /* a fighter has stepped off the ground */
    SND_JUDGE_WARNING = 0xA0,
    SND_JUDGE_IPPON = 0xA1, /* a point */
    /* said after it for a point by two warnings */
    SND_JUDGE_IPPON_BY_WARNINGS = 0xA2,
    SND_JUDGE_MATCH_OVER = 0xA3, /* said after TIME_UP */
    SND_JUDGE_DRAW = 0xA4,
    SND_JUDGE_WINNER = 0xA6, /* a match has a winner: then the side, ... */
    SND_JUDGE_CLAN_1 = 0xA7,
    SND_JUDGE_CLAN_2 = 0xA8,
    SND_JUDGE_BATTLE_WON = 0xA9, /* ... and, for the whole battle, this */
};

extern u8 g_team_begin_countdown; /* until the call that starts a round */
#define TEAM_BEGIN_FRAMES 80
extern u8 g_team_out_timer;     /* a fighter is off the ground */
extern u8 g_team_warning_timer; /* a warning is given */
extern u8 g_team_ippon_timer;   /* a point is given */
extern u8 g_team_break_timer;   /* both are down: the round is stopped */
extern u8 g_team_time_up_timer;
extern u8 g_team_result_timer; /* the match's result is called */
/* The fighter a call goes against: the one who fell or stepped out; 2 when
 * stepping out was nobody's fault. */
extern u8 g_team_penalized;

extern void fight_RecordTeamMatch(void);
extern void fight_NextTeamMatch(void);

/* ---- what follows a fight (fight_after.c) ---- */

/* The screens (enum Screen): the team battle's table of results, the menu
 * after a team battle or a kaishaku, the best times of chanbara, and the
 * count of a full run against the CPU. */
extern void fight_TeamResultInit(void);
extern void fight_TeamResultUpdate(void);
extern void fight_AfterMatchInit(void);
extern void fight_AfterMatchUpdate(void);
extern void fight_BestTimesInit(void);
extern void fight_BestTimesUpdate(void);
extern void fight_KillCountInit(void);
extern void fight_KillCountUpdate(void);

/* The endings SCREEN_ENDING shows: a cutscene under a picture of words
 * ("Victorious!" and the clan's title after the story, "20 Kills
 * Completed!", who a chanbara run has won and for which modes). Each comes
 * in two, for the clan of the fighter it is about. */
enum FightEnding {
    ENDING_STORY = 0,          /* the story's last scene */
    ENDING_VSCPU = 2,          /* a full run against the CPU */
    ENDING_TEAM = 4,           /* the team that won */
    ENDING_CHANBARA_FAST = 6,  /* the gunner a fast chanbara run has won */
    ENDING_CHANBARA_CLEAN = 8, /* the one a run without a continue has */
    ENDING_COUNT = 10,
};

extern u8 g_fight_ending;
/* Each ending's voice: an audio file (-1: none) and the channel of it. */
extern s8 g_ending_voices[40][2];

/* The kaishaku cutscene: the frame of the winner's voice, by character
 * (the table is as long as the roster and one more: the original's data). */
extern s16 g_kaishaku_voice_frames[ROSTER_COUNT + 1];

/* The frames at which its other sounds and its blood come, and where the
 * blood comes from. */
typedef struct KaishakuCues {
    s16 swing_frame;
    s16 cut_frame;
    s16 cry_frame;
    s16 fall_frame;
    s16 blood_frame;
    s32 blood_x; /* from the first fighter's start position */
    s32 blood_y;
    s32 blood_z;
} KaishakuCues;

extern KaishakuCues g_kaishaku_cues;
extern SVECTOR g_kaishaku_blood_dir; /* which way it goes */

/* The names of the modes, which g_fight_mode_names points at
 * (fight_start.c). */
extern const char g_str_chanbara[];
extern const char g_str_practice[];
extern char g_str_network[];
extern char g_str_team[];
extern char g_str_vs[];
extern char g_str_vscpu[];
extern char g_str_story[];

#endif /* FIGHT_H */
