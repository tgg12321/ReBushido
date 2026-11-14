#ifndef FIGHTER_H
#define FIGHTER_H

/* The fighter: one of the two characters in a fight.
 *
 * Everything a fighter does is a move (motion.h). Each frame fighter_Update
 * takes the fighter's pad, lets the move's commands or something that
 * happened to the fighter (an event: a wound, a clash, a fall) begin another
 * move, advances the move, decodes its motion into a pose, moves the fighter
 * through the stage and poses its model. It leaves the places of the weapon
 * and of 22 points of the body on the scratchpad, and fight_UpdateFighterPair
 * then tests the two fighters against each other: blade against blade,
 * blade and bullet against body, body against body.
 *
 * There is no health. A blow that lands on the core of the head or the body
 * kills; one that only reaches a limb or grazes wounds it: a wounded leg
 * slows every move, a wounded sword arm slows attacks, and the other arm,
 * once hit, hangs useless.
 *
 * fighter.c has the set-up and the frame; fighter_pose.c what lies between
 * a move and the model (aiming the pose, turning, the body's points);
 * fighter_hit.c the two fighters against each other; hit_test.c the
 * geometry of the hit tests; fight_event.c a move's marks and sounds. The
 * fighter's pad is fighter_pad.c. The CPU
 * opponent is cpu.h. */

#include "common.h"
#include <psxsdk/libetc.h>
#include <psxsdk/libgte.h>
#include "pad.h"
#include "collision.h"
#include "motion.h"
#include "weapon.h"
#include "cpu.h"

struct LooseObjectKind;

/* ---- who a fighter is ---- */

/* A fighter is a character (Fighter.character, 27 of them: the row of every
 * per-character table) picked from a roster (Fighter.roster_id), in which
 * some characters appear more than once.
 *
 * The names are the game's own. Twenty characters have one, written as a
 * picture: on the grid of character select (SEL.BIN's fighter sheets, one
 * for each place of the grid, which g_select_grid ties to a character) and
 * in the team battle's scores and the best times (g_hud_name_cells, by
 * character, cut from GAMEDAN.BNK and GAMECHAN.BNK). The other seven have none
 * anywhere on the disc and are named here for what the code does with them;
 * docs/STATUS.md, "Open questions", says what each looks like. */
enum CharacterId {
    /* the Narukagami clan */
    CHARACTER_KANNUKI = 0,
    CHARACTER_TATSUMI = 1,
    CHARACTER_MIKADO = 2,
    CHARACTER_UTSUSEMI = 3,
    CHARACTER_RED_SHADOW = 4,
    CHARACTER_TSUBAME = 5, /* won in chanbara; fights with the long gun */
    CHARACTER_SAZANKA = 6,
    CHARACTER_SUMINAGASHI = 7,
    CHARACTER_NIGHTSTALKER = 8,
    CHARACTER_MATSUMUSHI = 9,
    CHARACTER_VANISHER = 10,  /* no name: ROSTER_VANISHER */
    CHARACTER_SIDEARM_1 = 11, /* no name: ROSTER_SIDEARM_1 */
    /* the Shainto */
    CHARACTER_GENGORO = 12,
    CHARACTER_KAUN = 13,
    CHARACTER_JO = 14, /* his off-hand weapon strikes in a paired move */
    CHARACTER_HONGOU = 15,
    CHARACTER_KATZE = 16, /* won in chanbara; fights with the short gun */
    CHARACTER_HIGHWAYMAN = 17,
    CHARACTER_CHIHIRO = 18,
    CHARACTER_UTAMARU = 19,
    CHARACTER_TONY_UMEDA = 20,
    CHARACTER_ISOHACHI = 21,
    CHARACTER_ARMORED = 22,   /* no name: ROSTER_ARMORED */
    CHARACTER_SIDEARM_2 = 23, /* no name: ROSTER_SIDEARM_2 */
    /* no name: the crest beside the Narukagami's half of the grid, a
     * figure in kabuki dress; won in chanbara without a continue, and
     * last in VSCPU */
    CHARACTER_LAST_1 = 24,
    CHARACTER_VICTIM = 25, /* no name: ROSTER_VICTIM */
    /* no name: the same for the Shainto */
    CHARACTER_LAST_2 = 26,
    CHARACTER_COUNT = 27,
};

/* The roster. The three fighters each clan starts the game with have two
 * entries: the first is the one in the dress they wear in story mode (so
 * the running game shows: the pictures do not tell the two apart), and the
 * story's scripts set them against each other in it. */
enum RosterId {
    ROSTER_KANNUKI_STORY = 0,
    ROSTER_TATSUMI_STORY = 1,
    ROSTER_MIKADO_STORY = 2,
    ROSTER_KANNUKI = 3,
    ROSTER_TATSUMI = 4,
    ROSTER_MIKADO = 5,
    ROSTER_UTSUSEMI = 6,
    ROSTER_RED_SHADOW = 7,
    ROSTER_TSUBAME = 8,
    ROSTER_SAZANKA = 9,
    ROSTER_SUMINAGASHI = 10,
    ROSTER_NIGHTSTALKER = 11,
    ROSTER_MATSUMUSHI = 12,
    /* in a white robe; struck, it vanishes and kneels somewhere else */
    ROSTER_VANISHER = 13,
    /* the two clans' ninja, the story's henchmen and the partner of
     * practice: they draw a pistol with the off hand */
    ROSTER_SIDEARM_1 = 14,
    ROSTER_GENGORO_STORY = 15,
    ROSTER_KAUN_STORY = 16,
    ROSTER_JO_STORY = 17,
    ROSTER_GENGORO = 18,
    ROSTER_KAUN = 19,
    ROSTER_JO = 20,
    ROSTER_HONGOU = 21,
    ROSTER_KATZE = 22,
    ROSTER_HIGHWAYMAN = 23,
    ROSTER_CHIHIRO = 24,
    ROSTER_UTAMARU = 25,
    ROSTER_TONY_UMEDA = 26,
    /* his sub-weapon move throws nothing. He stamps, and an opponent in
     * reach staggers */
    ROSTER_ISOHACHI = 27,
    /* wears armour that turns every blow but one down onto the chest */
    ROSTER_ARMORED = 28,
    ROSTER_SIDEARM_2 = 29,
    ROSTER_LAST_1 = 30,
    /* in white, unarmed, kneeling with its back turned: it waits to be
     * struck */
    ROSTER_VICTIM = 31,
    ROSTER_LAST_2 = 32,
    ROSTER_COUNT = 33,
};

/* The roster (fight_data.c): each entry's character, model and class, and
 * each character's entry. */
extern u8 g_fighter_roster_characters[36];
extern u8 g_fighter_roster_models[36];
extern u8 g_fighter_roster_classes[36];
extern u8 g_fighter_character_rosters[CHARACTER_COUNT + 1];

/* Each character's clan, 0 or 1 (by Fighter.character). The two clans have
 * their own look of every weapon. */
extern u8 g_fighter_clans[CHARACTER_COUNT + 1];
/* A fighter's weapon model (enum WeaponModel): [weapon][clan]. */
extern u8 g_fighter_clan_weapon_models[WEAPON_ID_COUNT][2];
/* The same in team battles and in practice against the ninja. */
extern u8 g_fighter_practice_weapon_models[WEAPON_ID_COUNT];
/* The stance kinds of [character][weapon]: a motion set (motion.h) less its
 * weapon. */
extern u16 g_fighter_stance_kinds[CHARACTER_COUNT][WEAPON_ID_COUNT];
/* The stance [character][weapon] starts a fight in. */
extern u8 g_fighter_default_stances[CHARACTER_COUNT][WEAPON_ID_COUNT];

/* The five characters whose sub-weapon is a second sword. */
#define CHARACTER_HAS_SECOND_SWORD(c)                                          \
    ((c) == CHARACTER_TATSUMI || (c) == CHARACTER_UTSUSEMI ||                  \
     (c) == CHARACTER_RED_SHADOW || (c) == CHARACTER_MATSUMUSHI ||             \
     (c) == CHARACTER_HIGHWAYMAN)

/* ---- the pad ---- */

/* A fighter's pad (Fighter.pad) is the player's with the face buttons
 * replaced by the three actions of the button configuration, and with the
 * directions turned so that right is always towards the opponent. The
 * shoulder buttons keep Sony's bits (PADL1 runs; PADR1 with a direction
 * climbs). The CPU builds the same word. */
/* The names are the key configuration screen's: "Frontal Attack",
 * "Reverse Attack" and "Change Stance" (which also reloads a gun). */
#define FIGHTER_PAD_FRONTAL_ATTACK PADRright
#define FIGHTER_PAD_REVERSE_ATTACK PADRdown
#define FIGHTER_PAD_STANCE PADRleft
#define FIGHTER_PAD_ACTIONS (PADRup | PADRright | PADRdown | PADRleft)
#define FIGHTER_PAD_TOWARD PADLright
#define FIGHTER_PAD_AWAY PADLleft
#define FIGHTER_PAD_SIDE_A PADLup
#define FIGHTER_PAD_SIDE_B PADLdown
#define FIGHTER_PAD_DIRECTIONS (PADLup | PADLright | PADLdown | PADLleft)
/* The bits of a pad's word below the four directions. */
#define FIGHTER_PAD_BELOW_DIRECTIONS 0xFFF

/* The heading of each direction of the pad from straight at the opponent:
 * [towards / neither / away][side A / neither / side B]. */
extern s16 g_fighter_pad_headings[3][3];

/* ---- moves ---- */

/* The speed at which a move runs a frame of its motion every frame
 * (Fighter.base_speed, Fighter.speed). */
#define FIGHTER_SPEED_ONE 0x1000

/* Fighter.move_started */
enum MoveStartState {
    MOVE_START_NONE = 0,
    MOVE_START_SCRIPT = 1, /* a move began this frame */
    MOVE_START_PAIRED = 2, /* a paired move began this frame, for both */
};

/* What can happen to a fighter (Fighter.pending_event). A move has a table
 * of the moves that answer each event (MoveScript.event_table_id), and
 * the fighter goes into that move at the start of its next frame. The moves
 * of a blade lock and of a paired move have tables of their own, in which
 * the first few numbers mean other things. */
enum MoveEvent {
    MOVE_EVENT_NONE = -1,
    /* its attack met the opponent's: the three outcomes, from holding its
     * own to being beaten back */
    MOVE_EVENT_CLASH_EVEN = 0,
    MOVE_EVENT_CLASH_PUSHED = 1,
    MOVE_EVENT_CLASH_BEATEN = 2,
    MOVE_EVENT_LEG_WOUND = 3,
    MOVE_EVENT_ARM_WOUND = 4,
    /* a wound to the body or the sword arm, and whatever else staggers:
     * thrown dirt, a runner's shove */
    MOVE_EVENT_STAGGER = 5,
    /* killed, from in front: by where the blow landed */
    MOVE_EVENT_KILLED_HEAD = 6,
    MOVE_EVENT_KILLED_CHEST = 7,
    MOVE_EVENT_KILLED_HIP = 8,
    MOVE_EVENT_KILLED_FROM_BEHIND = 9,
    MOVE_EVENT_LOCK = 10, /* the blades lock */
    /* its weapon struck something hard; also given to the one whose blade
     * beats the other's aside (it answers with MOVE_KIND_DEFLECT, the other is
     * PARRIED) */
    MOVE_EVENT_BLADE_STOPPED = 11,
    MOVE_EVENT_CLIMB = 13,
    MOVE_EVENT_RUNNERS_COLLIDE = 15,
    MOVE_EVENT_RUN_DOWN = 16, /* run into from behind */
    MOVE_EVENT_FALL_BACKWARD = 17,
    MOVE_EVENT_FALL_FORWARD = 18,
    /* its weapon was beaten aside: upwards, downwards and to either side */
    MOVE_EVENT_PARRIED_UP = 19,
    MOVE_EVENT_PARRIED_DOWN = 20,
    MOVE_EVENT_PARRIED_ACROSS = 21,
    MOVE_EVENT_PARRIED_BACK = 22,
    MOVE_EVENT_LAND_ON_BACK = 23, /* after a long fall */
    MOVE_EVENT_LAND_ON_FRONT = 24,
    /* BLADE_STOPPED and PARRIED for a fighter with a weapon in each hand */
    MOVE_EVENT_BLADE_STOPPED_TWO = 25,
    MOVE_EVENT_PARRIED_TWO = 26,
    MOVE_EVENT_BITTEN = 27, /* four characters, by the frog */
    MOVE_EVENT_STUNNED = 28,
    MOVE_EVENT_REAPPEAR = 29,
    MOVE_EVENT_ARMOR_HIT = 30,
    MOVE_EVENT_NO_STUN_LEFT = 31,
};

/* How far an attack has got, for each blade (Fighter.attack_phases): when
 * two blades meet the further one wins, and two that are both striking
 * lock. */
enum AttackPhase {
    ATTACK_PHASE_NONE = 0,
    ATTACK_PHASE_SPENT = 1,    /* past the frames in which it hits */
    ATTACK_PHASE_WINDING = 3,  /* before them */
    ATTACK_PHASE_STRIKING = 5, /* in them */
};

/* Fighter.weapon_live_state, sub_weapon_live_state: a weapon that is out
 * and can hit. Its first frame has no "last frame" to sweep from. */
enum WeaponLiveState {
    WEAPON_LIVE_OFF = 0,
    WEAPON_LIVE_ON = 1,
    WEAPON_LIVE_FIRST_FRAME = 2,
};

/* Fighter.frame_flags: what of the fighter is drawn this frame, which the
 * replay records. */
enum FighterFrameFlag {
    FIGHTER_DRAW_WEAPON = 1,      /* in the hand */
    FIGHTER_DRAW_SCABBARD = 2,    /* at the side */
    FIGHTER_DRAW_SUB_WEAPON = 4,  /* in the off hand */
    FIGHTER_DRAW_WORN_SUB = 8,    /* at the side */
    FIGHTER_SCABBARD_FULL = 0x10, /* the weapon is in its scabbard */
    FIGHTER_WORN_SUB_FULL = 0x20, /* the worn sub-weapon is in its own */
    FIGHTER_FRAME_EVENTS = 0x40,  /* the move's frame events were run */
    FIGHTER_OFF_ARM_OK = 0x80,
};

/* ---- the body ---- */

/* The 22 points of the body that can be hit, in five groups. */
enum BodyPointId {
    BODY_HEAD = 0,
    BODY_CHEST = 1, /* three */
    BODY_HIP = 4,   /* two */
    BODY_OFF_ARM = 6,
    BODY_SWORD_ARM = 10,
    BODY_LEGS = 14, /* eight: four a leg */
    BODY_SECOND_LEG = 18,
    BODY_POINT_COUNT = 22,
};

/* The bones the fighter's code looks at (model_GetBoneMatrices). */
enum FighterBone {
    FIGHTER_BONE_TORSO = 11,
    FIGHTER_BONE_FOOT_1 = 14,
    FIGHTER_BONE_FOOT_0 = 17,
    FIGHTER_BONE_WEAPON_HAND = 18,
    FIGHTER_BONE_OFF_HAND = 19,
};

/* One point of the body: a sphere on a bone. A blade that comes within
 * `radius` of it has hit; one that also reaches the smaller core, if the
 * point has one, has hit home. */
typedef struct BodyPoint {
    s16 has_core;
    s16 bone;
    SVECTOR ofs; /* from the bone */
    u16 radius;
    u16 reach_sq; /* the limit of a test that works on squared distances */
    u16 core_radius;
    u16 core_reach_sq;
} BodyPoint; /* 0x14 bytes */

/* The points for a fighter of full size, and each fighter's own. */
extern BodyPoint g_fighter_body_point_table[BODY_POINT_COUNT];
extern BodyPoint g_fighter_body_points[2][BODY_POINT_COUNT];

/* ---- the fighter ---- */

/* A pad_NN is the fourth of four where three are used (a point's, a set of
 * three angles' or factors'), which no code reads or writes. An unk_NN is a
 * place for something of which nothing is known: no code reads or writes
 * it, but for unk_144, unk_3C0 to unk_3C4 and unk_3EE, which fight_Init's
 * and cpu_Init's set-up clears and nothing reads. */
typedef struct Fighter {
    /* -- who it is -- */
    struct Fighter *other; /* the opponent */
    s16 index;             /* 0 or 1 */
    s16 is_cpu;
    s16 cpu_base_level;
    s16 character;
    s16 roster_id;
    s16 weapon_id; /* enum WeaponId */
    u8 unk_10[0x12 - 0x10];
    s16 weapon_model; /* enum WeaponModel: the weapon as it is drawn */
    /* The kind of sub-weapon the fighter has (enum ObjectKindId), or -1:
     * what is worn at its side or held in its off hand. */
    s16 sub_weapon_kind;
    u8 unk_16[0x1A - 0x16];
    s16 size_scale; /* 0x1000: full size */
    s16 base_speed; /* how fast its moves run, 0x1000 a frame */
    s16 base_power;
    s16 power; /* base_power less what its body wounds take */

    /* -- the move it is in -- */
    PadState pad;        /* this frame's (FIGHTER_PAD_...) */
    s32 frames_alive;    /* since fighter_Init */
    s16 move_frame;      /* current frame of the move */
    s16 move_frame_frac; /* and how far into it, 0x1000 a frame */
    s16 speed;           /* what the frame advances by: base_speed, wounded */
    s16 same_frame;      /* the frame did not advance this time */
    /* the motion set (motion.h): the weapon class and the three stances */
    u16 motion_set;
    s16 motion_slot; /* which of the two fighter files it is in */
    /* the next move of its own file is taken from the other fighter's */
    s16 borrow_other_motion;
    MoveScript *cur_move;
    u16 *motion;      /* the move's motion: its entry in the file */
    u8 *motion_head;  /* that motion's head and marks (MOTION_HEAD_...) */
    u16 motion_index; /* which motion of the file it is */
    /* the move last looked up is in the common file */
    s16 move_is_common;
    u8 cur_move_is_common;
    u8 cur_move_slot;
    u8 frame_flags; /* enum FighterFrameFlag */
    /* the two frames the pose is between (the decoder in the top two bits)
     * and how far from the first to the second */
    u16 pose_from_frame;
    u16 pose_to_frame;
    s16 pose_weight;
    u16 move_kind; /* enum MoveKind, from the motion's head */
    s16 prev_move_kind;
    s16 move_start_class; /* never read */
    s16 move_turn_mode;   /* 1: it keeps turning to face the opponent */
    s16 airborne;         /* off the ground until it lands */
    s32 fall_start_y;     /* where a jump or a fall began */
    s16 blend_into_move;  /* ease into the move from the pose before */
    s16 move_started;     /* enum MoveStartState */
    /* a move asked for, to begin when this one lets it */
    MoveScript *queued_move;
    s16 queued_move_is_common;
    u16 queued_move_command;
    s16 default_stance; /* the stance a fight begins in */
    s16 stance;         /* the stance it is in, 0 to 2 */
    /* which stance is the two-sword one (-1: none), and whether it can be
     * taken: the second sword is in hand and the arm can be used */
    s16 two_sword_stance;
    s16 two_sword_ready;
    /* the second sword or off-hand gun can hit (enum WeaponLiveState) */
    s16 sub_weapon_live_state;
    /* the same for the sheathed stance */
    s16 sheathed_stance;
    s16 sheathed_ready;
    s16 weapon_live_state;
    s16 falling_backward;
    s16 is_defeated;
    SVECTOR wall_normal; /* the wall it climbs */

    /* -- the frames of the move at which things happen (its marks) -- */
    /* an attack hit in its first frames meets the blow as a clash */
    u8 clash_frames;
    u8 hit_start_frames[2]; /* for each blade: it can hit from ... */
    u8 hit_end_frames[2];   /* ... to */
    u8 off_hand_start;      /* the off hand's weapon is out from ... */
    u8 off_hand_end;        /* ... to */
    u8 release_frame;       /* what is thrown leaves the hand */
    u8 pickup_frame;        /* the frame of a crouch at which it picks up */
    u8 face_frame;          /* getting up: it turns to face the opponent */
    u8 paired_hit_frame;    /* a paired move's blow lands */
    u8 counter_frame;       /* from here the opponent may answer */
    u8 reload_frame;
    /* an attack that has not yet landed or been stopped */
    u8 attack_armed;
    u8 hit_window_open; /* and is in the frames in which it hits */
    /* which of the two attack buttons the attack was begun with */
    u8 attack_kind;
    u8 command_taken; /* the flags of the command that began the move */
    /* what it stands on: the kind of the last surface it met (enum Surface)
     * and that surface's class (collision.h) */
    u8 surface;
    u8 surface_class;
    u8 face; /* MODEL_FACE_... */
    /* in a clash: both attacks were of the same kind */
    u8 attack_kinds_matched;

    /* -- where it is --
     * base_pos is where the fighter stands as its moves see it; a pose puts
     * the body root_offset from there, at world_pos. foot_pos is the ground
     * under the body, the point that is moved through the stage. */
    VECTOR foot_pos;
    VECTOR prev_foot_pos;
    Vec3 base_pos;
    s32 pad_E4;
    Vec3 root_offset;
    Vec3 world_pos;
    s32 pad_100;
    VECTOR velocity;        /* its own: jumps, falls */
    VECTOR blade_motion[2]; /* how far each blade's point moved this frame */
    VECTOR push;            /* from being pushed; dies away quickly */
    s32 unk_144;            /* cleared with `push`; never read */
    s32 ground_y;  /* the ground under it, also while it is in the air */
    s16 turn_left; /* how far it has still to turn */
    s16 run_speed;
    s16 run_drag; /* always 0 */
    /* the heading root_offset is measured from, when that is not the way
     * the fighter faces */
    s16 use_root_heading;
    s16 root_heading;
    s16 velocity_damping_x; /* what is kept each frame, of 0x1000 */
    s16 velocity_damping_y;
    s16 velocity_damping_z;
    s16 pad_15C;
    s16 push_damping_x;
    s16 push_damping_y;
    s16 push_damping_z;
    s16 pad_164;
    Vec3 camera_target; /* what the camera looks at: above its feet */
    /* the middle of each part of the body, where its dust rises and what
     * the first-person view looks from */
    Vec3 hip_pos;
    Vec3 head_pos;
    Vec3 chest_pos;
    Vec3 foot_points[2];   /* the feet */
    s32 foot_ground_ys[2]; /* and the ground under each */
    /* Which way each foot points: only vy, the heading, is written and
     * read (a footprint is laid along it). That the two bytes before it
     * and the four after are the other angles of an SVECTOR is a guess. */
    SVECTOR foot_angles[2];
    SVECTOR orientation; /* vy: the way it faces */
    SVECTOR initial_orientation;
    s16 opponent_bearing; /* the heading from it to the opponent */
    s16 lying_face_up;    /* a guess: it may be face down */
    s16 on_ground;
    u8 unk_1DE[0x1E6 - 0x1DE];
    /* how far the upper body is turned and leant towards the opponent, and
     * how far a gun is raised (motion_AimPose) */
    s16 aim_turn;
    s16 aim_lean;
    s16 gun_aim;
    Vec3 torso_axis; /* which way the top of the torso points */
    Vec3 prev_root_offset;
    u8 unk_204[0x210 - 0x204];
    /* where the weapons were last frame: a blade hits what lies between
     * there and where it is now */
    Vec3 prev_weapon_points[3];
    Vec3 prev_second_weapon_points[2];
    VECTOR prev_velocity;
    Vec3 gun_pos; /* a gun's muzzle, which it is aimed from */
    s32 gun_pos_valid;

    /* -- wounds -- */
    s16 off_arm_ok; /* 0: the off arm hangs */
    s16 leg_wounds;
    s16 arm_wounds;  /* to the sword arm */
    s16 body_wounds; /* and whatever else weakens it */
    /* how much slower each kind of wound makes it, of 0x1000 */
    s16 lost_arm_speed;
    s16 arm_wound_speeds[4];
    s16 wound_speeds[4];

    s16 pending_event;    /* enum MoveEvent */
    s16 attack_phases[2]; /* enum AttackPhase, for each blade */
    s32 lock_score;       /* in a blade lock: its button presses */

    /* -- easing from one move's pose into the next -- */
    MotionFrame prev_pose;
    u16 prev_pose_heading;
    u16 prev_pose_frame;
    s16 blend_weight; /* how far into the new pose, of 0x1000 */
    s16 blending;
    s16 blend_step;
    Vec3 blend_shift;
    s32 pad_32C;

    /* -- what it carries -- */
    /* The sub-weapons carried, by kind (object.h): thrown from the front,
     * picked up to the back; a second sword is kept first. */
    s16 sub_weapon_count;
    s16 sub_weapons[12];
    u8 ammo; /* shots left in a gun */
    u8 reloads_left;
    u8 vanish_count;   /* ROSTER_VANISHER: times it has vanished */
    u8 stun_charges;   /* ROSTER_ISOHACHI: uses left */
    u8 select_pressed; /* written, never read */
    /* a mortal wound bleeds: from which bone and which way */
    s16 bleed_timer;
    s16 bleed_bone;
    Vec3 bleed_dir;

    /* -- the CPU's (cpu.h) -- */
    CpuRoute cpu_route;
    s32 cpu_action; /* enum CpuAction */
    s16 cpu_action_timer;
    s16 cpu_action_frames;   /* how long an action is held */
    u8 cpu_mirrors_opponent; /* the action is whatever the opponent does */
    u8 cpu_circling;
    s16 cpu_goal_kind; /* where a circling is going */
    s16 cpu_goal_x;
    s16 cpu_goal_z;
    /* the attacks it knows: offsets from the list's start, in a run for
     * each stance, each run ended by a zero */
    u16 *cpu_scripts;
    u16 *cpu_stance_scripts[3];
    u8 *cpu_script; /* the pad script being played, or NULL */
    u8 cpu_script_wait;
    u8 unk_3B9[0x3BC - 0x3B9];
    u8 cpu_script_kind;    /* enum CpuScriptKind */
    u8 cpu_reaction_delay; /* frames before it strikes a fallen opponent */
    u8 unk_3BE[0x3C0 - 0x3BE];
    /* four that cpu_Init clears with the action's timer; never read */
    u8 unk_3C0;
    u8 unk_3C1;
    s16 unk_3C2;
    s32 unk_3C4;
    s32 cpu_script_buttons; /* what the script holds down */
    s32 cpu_buttons;        /* what this frame's decision holds down */
    PadState cpu_pad;       /* the pad it built last */
    u16 cpu_frames;         /* frames it has been playing */
    u8 unk_3EA[0x3EE - 0x3EA];
    s16 unk_3EE; /* cleared by cpu_Init; never read */
    /* frames it has been too far (positive) or too near */
    s16 cpu_range_error_frames;
    u8 cpu_attacks_begun;
    u8 cpu_threats_met;
    u8 cpu_setbacks; /* times hurt, parried or beaten back */
    u8 cpu_idle_frames;
    u8 cpu_idle_limit;
    /* For each stance, the nearest and the farthest its attacks reach, and
     * the farthest of the plain ones. Worked out for the opponent of a CPU
     * fighter too. */
    s16 attack_min_reach[3];
    s16 attack_max_reach[3];
    s16 attack_reach[3];
    s16 reach_bonus; /* what its size adds to a reach */
    s8 cpu_last_script;
    s8 cpu_last_script_stance;
    /* where the opponent was when an attack began, and how far it may move
     * before the attack is given up */
    s16 cpu_script_target_x;
    s16 cpu_script_target_z;
    s16 cpu_script_range;
    /* the opponent's attacks it has met: which motion, and how often */
    u8 cpu_known_attacks[CPU_KNOWN_ATTACKS][2];
    u8 cpu_known_threshold;   /* how often is often enough to know one */
    u8 cpu_projectile_threat; /* enum CpuProjectile */
    u8 cpu_attack_threat;
    u8 cpu_threat_hit_frame; /* when the attack coming will hit */
    s16 cpu_incoming_attack; /* which motion it is */
    s16 cpu_threat_x;        /* where it came from, and how far it reaches */
    s16 cpu_threat_z;
    s16 cpu_threat_range;
    s32 cpu_flags; /* enum CpuFlag */
    s32 cpu_obstacle_distance;
    s16 cpu_level;
    /* how far round from straight ahead the opponent is, and that without
     * its sign */
    s16 opponent_angle;
    s16 opponent_angle_abs;
    s16 cpu_preferred_distance;
    /* MOTION_HEAD_CLASS of the motion it is in, and of the stance it last
     * stood in */
    u8 stance_class;
    u8 move_class;
    u8 cpu_opponent_height; /* enum CpuOpponentHeight */
    u8 cpu_profile;
    u8 cpu_probes[CPU_PROBE_COUNT]; /* enum CpuProbeResult */
} Fighter;                          /* 0x44C bytes */

extern Fighter g_fighters[2];

/* ---- per-character tables (fight_data.c) ---- */

/* [character][weapon]: how fast its moves run and how hard it hits. */
extern u16 g_fighter_speeds[CHARACTER_COUNT][6];
extern u16 g_fighter_powers[CHARACTER_COUNT][6];
/* [character][body wounds, 3 at most]: what is left of its power. */
extern s16 g_fighter_wound_powers[CHARACTER_COUNT][4];
/* How much slower wounds make a character: a lost off arm, [wounds] to the
 * sword arm, [wounds] to the legs and body together. */
extern u16 g_fighter_lost_arm_speeds[CHARACTER_COUNT + 1];
extern u16 g_fighter_arm_wound_speeds[CHARACTER_COUNT][4];
extern u16 g_fighter_wound_speeds[CHARACTER_COUNT][4];
/* [character]: how fast it can run (a class, and the classes' limits) and
 * how it jumps. */
extern u8 g_fighter_run_classes[72];
extern s16 g_fighter_run_speed_limits[4];
extern s16 g_fighter_long_jump_lifts[ROSTER_COUNT + 1];
extern s16 g_fighter_long_jump_speeds[ROSTER_COUNT + 1];
extern s16 g_fighter_jump_lifts[ROSTER_COUNT + 1];

/* Where the points of a weapon are from the hand that holds it: for each
 * weapon (two points, three for the pole arms), for the weapon of story
 * scenes, and for each kind of sub-weapon (two). */
extern u8 g_fighter_weapon_point_counts[WEAPON_ID_COUNT];
extern SVECTOR *g_fighter_weapon_point_sets[WEAPON_ID_COUNT];
extern SVECTOR *g_fighter_sub_weapon_points[32]; /* OBJECT_KIND_COUNT */
extern SVECTOR g_fighter_story_weapon_points[2];
extern SVECTOR g_unit_y_vector;

/* ---- the scratchpad during a fight ---- */

/* Up to six scratch points for the weapon and object hit tests: the two ends
 * of a segment, or the two ends of a blade this frame and last plus their
 * midpoints, from which fight_TestBladesOnBodies makes the triangles a swing
 * sweeps out. raw sizes the union. */
typedef union {
    u8 raw[0x60];
    Vec3 work_points[6];
} HitScratchPoints;

/* Sixteen bytes with two users: the hit tests keep a vector here; a
 * SegmentCast laid over the same memory keeps its step counter and an axis
 * term. */
typedef union {
    Vec3 vector;

    struct {
        s16 walk_steps_left;    /* SegmentCast +0x90 */
        u8 unk02[0xA];          /* SegmentCast.unk92 */
        s32 scalar_proj_axis_x; /* SegmentCast +0x9C, proj_axis_x */
    } cast_view;
} HitScratchVec;

/* The hit tests' scratch, on the last 0x148 bytes of the scratchpad. A test
 * is of a shape (a blade's swept triangle, a shot's segment) against
 * spheres or against another triangle. The shape is first given a frame of
 * its own, in which the test is simple: its edges, its normal, the angles
 * and the matrix that turn the world into that frame. Other code uses this
 * memory at other times with its own layout: the stage-collision cast
 * (SegmentCast) and the renderer. */
typedef struct {
    HitScratchPoints point_slots;
    Vec3 *shape_points[3];       /* the shape's corners */
    Vec3 *other_shape_points[3]; /* a second triangle's */
    Vec3 bbox_min;               /* the box round the shape */
    Vec3 bbox_max;
    Vec3 other_bbox_min;
    Vec3 other_bbox_max;
    Vec3 edge_a;          /* from the first corner to the second */
    s32 body_hits;        /* a bit for each body point the shape reached */
    Vec3 edge_b;          /* from the first corner to the third */
    s32 core_hits;        /* and for each whose core it reached */
    HitScratchVec normal; /* edge_a x edge_b; or a point of the test's */
    MATRIX rot_matrix;
    SVECTOR angles;
    /* a point a test found: where a shot met the stage, where a blade met
     * an obstacle */
    Vec3 hit_points[2];
    Vec3 relative_points[3]; /* the other shape in the frame */
    Vec3 contact_point;      /* where two blades met */
} WeaponHitScratch;

/* The same scratch as loose objects and thrown dirt use it: the end point,
 * hit point and hit normal of a stage-collision cast, then the cast's work
 * area. */
typedef struct {
    /* +0x00..+0x0F, two views. cast_end: the to point;
     * collision_CastMoveWith copies 16 bytes from it,
     * so this layout reaches +0x0C only as that copy's 4th word. point_prefix:
     * the same bytes as WeaponHitScratch.point_slots.work_points[0] and
     * work_points[1].x, which the hit tests store and read. */
    union {
        Vec3 cast_end;

        struct {
            Vec3 work_point; /* point_slots.work_points[0] */
            /* point_slots.work_points[1].x */
            s32 second_work_point_x;
        } point_prefix;
    } end_slot;

    /* where the cast met the stage (before the cast, object_Update also
     * turns the curving kind's speed here) */
    Vec3 hit_point;
    s32 pad1C; /* the fourth word of a point; not touched */
    /* the curving kind's speed, turned and pitched (object_Update) */
    Vec3 pitched_vel;
    s32 pad2C;          /* not touched */
    s16 cast_normal[3]; /* the hit normal */
    s16 pad36;          /* not touched */
    /* collision_CastMoveWith's work area,
     * +0x38..+0x123 */
    SegmentCast cast_work;
} ObjectCastScratch;

/* The end of the fight scratchpad, under its two layouts. */
typedef union {
    WeaponHitScratch weapon_hit;
    ObjectCastScratch object_cast;
} FightScratchTail;

/* The scratchpad during a fight, indexed [fighter][point]: where, this
 * frame, each fighter's weapon points are (three along the main weapon, two
 * for a second blade or held object), two body points used to keep the
 * fighters from overlapping, and the 22 points of the body; then the hit
 * tests' scratch. The effects code lays the same memory out its own way
 * (EffScratch). */
typedef struct {
    Vec3 weapon_points[2][3];
    Vec3 second_weapon_points[2][2];
    Vec3 body_overlap_points[2][2];
    Vec3 body_points[2][BODY_POINT_COUNT];
    FightScratchTail tail;
} FightScratch;

#define SPAD ((FightScratch *)SCRATCHPAD)
/* The room the stage-collision casts of the fighter's code work in: where
 * the hit tests' scratch is. */
#define FIGHT_CAST_WORK ((SegmentCast *)(SCRATCHPAD + 0x2B8))

/* ---- fighter.c ---- */

/* How high the body's root is above the feet of a fighter standing. */
#define FIGHTER_WAIST_HEIGHT 900

/* A surface's normal is a vector of length ONE, and up is -y: ground is
 * level where the normal's y is -ONE, and a floor (something to stand on,
 * not a wall) where it is below this: a slope of less than 60 degrees. */
#define FIGHTER_FLOOR_NORMAL_Y (-ONE / 2)

/* Sets a fighter up for a fight: who it is, what it carries, where it
 * stands. `place_near_other`: a new opponent comes on beside the fighter
 * that is already there. */
extern void fighter_Init(s32 fighter_index, s32 is_cpu, s32 roster_id,
                         s32 weapon, s32 place_near_other);
extern void fighter_HealWounds(Fighter *fighter);
/* A frame of one fighter. */
extern void fighter_Update(s32 fighter_index, PadState *pad);
/* Puts the fighter into a move. */
extern void fighter_StartMove(
    s32 fighter_index, MoveScript *move, s32 is_common);
/* Looks for a ledge the fighter can climb in the direction its pad is held;
 * gives the heading to face it. */
extern s32 fighter_FindLedge(Fighter *fighter, s32 *heading_out);

/* ---- fighter_pad.c ---- */

/* The fighter's pad for this frame: the player's, through the button
 * configuration, or the CPU's. */
extern void fighter_ReadPad(s32 fighter_index, PadState *pad_out);

/* ---- fighter_pose.c ---- */

/* Begins a turn that will leave the fighter facing `angle`. */
extern void fighter_TurnTo(Fighter *fighter, s32 angle);
extern void fighter_StartBleeding(
    Fighter *fighter, s32 body_point, s16 *blow_dir);
extern void fighter_UpdateBleeding(Fighter *fighter);
extern void fighter_ScaleBodyPoints(s32 fighter_index, s32 size_scale);
/* Works out where the fighter's body and weapon points are this frame. */
extern void fighter_PlacePoints(Fighter *fighter, Vec3 *body_points,
                                Vec3 *weapon_points, Vec3 *second_points);

/* ---- what a hit does (fighter_hit.c; the tests are in fight.h) ---- */

/* What a hit does to a fighter. `by_object`: 1 for a thrown object, whose
 * kind is `object_kind`; for a blade that argument is 1 for the off hand's
 * weapon and 0 for the main one. `facing`: how far from facing the same way
 * the two are. `kills`: the blow reached the core. `by_sidearm`: it is a
 * sidearm's bullet. *clashed_out: an object met an attack and is turned
 * away. Returns 1 when the fighter is killed, 2 when it vanished. */
extern s32 fighter_TakeHit(
    s32 by_object, Fighter *fighter, s32 body_point, s32 facing, s32 kills,
    struct LooseObjectKind *object_kind, s32 by_sidearm, s32 *clashed_out);
extern s32 fighter_SecondSwordIsOut(Fighter *fighter);
extern s32 fighter_IsFaceUp(Fighter *fighter);
/* Returns nothing, though it is an int function: the blade lock's code is
 * that of a call to one. */
extern s32 fighter_TryDropSecondSword(Fighter *fighter);

/* ---- the geometry of the hit tests (hit_test.c) ---- */

/* Each shape is first given a frame of its own in the scratch; the tests
 * then work in that frame. A triangle too thin for hit_FrameTriangle to
 * frame (it returns 0) is tested as a sliver. `core`: test the sphere kept
 * from the last call against another radius. The file's helpers that need
 * no scratch are math.h's. */
extern s32 hit_FrameTriangle(WeaponHitScratch *scr);
extern s32 hit_FrameTriangleByNormal(WeaponHitScratch *scr);
extern s32 hit_SphereMeetsTriangle(
    s32 core, WeaponHitScratch *scr, s32 *center, s32 radius, s32 reach_sq);
extern s32 hit_SphereMeetsSliver(
    s32 core, WeaponHitScratch *scr, Vec3 *center, s32 radius, s32 reach_sq);
extern s32 hit_TrianglesMeet(
    WeaponHitScratch *scr, Vec3 *corner_0, Vec3 *corner_1, Vec3 *corner_2);
extern void hit_FrameSegment(WeaponHitScratch *scr);
extern s32 hit_SphereMeetsSegment(
    WeaponHitScratch *scr, Vec3 *center, s32 radius, s32 reach_sq);

/* ---- a move's marks and sounds (fight_event.c) ---- */

/* Reads the marks of the move the fighter has begun. */
extern void fighter_ReadMarks(Fighter *fighter);
/* Plays what the move's motion has for this frame: footsteps, voices. */
extern void fighter_RunFrameEvents(Fighter *fighter);

#endif /* FIGHTER_H */
