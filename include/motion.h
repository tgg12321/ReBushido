#ifndef MOTION_H
#define MOTION_H

/* Motion: the fighters' animation.
 *
 * Everything a fighter does is a move (MoveScript): a stretch of recorded
 * motion with the frames at which things happen in it and the moves it can
 * lead to. The moves and the motion they play are in motion files (MOTION/
 * *.BBM): COMMON.BBM, which every fight loads, with what all fighters share
 * (falls, wounds, the paired moves of two fighters locked together), and one
 * file for each motion set, a weapon class with three stances.
 *
 * A fighter holds its weapon in one of three stances and steps through them;
 * most of a file's moves exist once for each stance. Which three stances a
 * fighter has depends on who it is and what it carries.
 *
 * The motion itself is one long stream of bit-packed frames per file
 * (motion_decode.c). A decoded frame (MotionFrame) is what a model is posed
 * from. Cutscenes (cutscene.h) are recorded in the same streams.
 *
 * fighter.c loads the files and finds moves in them; the functions that bend
 * a decoded pose (to aim at the opponent, to let a wounded arm hang) sit
 * with the fighter's code in fighter_pose.c, fighter.c and fighter_hit.c. */

#include "common.h"

struct Fighter;
struct ReplayFighterFrame;

/* ---- stances and motion sets ---- */

/* The kinds of stance are numbered 1 to 5; 1 to 3 are the ordinary ones. */
#define STANCE_KIND_TWO_SWORDS 4 /* the second weapon in the other hand */
#define STANCE_KIND_SHEATHED 5   /* the blade is not out until it strikes */
#define STANCE_COUNT 3           /* stances a fighter has */

/* A motion set names a motion file: the weapon class in the top digit and the
 * three stance kinds in the others, the fighter's first stance lowest. The
 * file's name says the same: U124.BBM is weapon class 0 with stances 1, 2
 * and 4. */
#define MOTION_SET(weapon, kinds) ((kinds) | ((weapon) << 12))
#define MOTION_SET_WEAPON(set) ((set) >> 12)
#define MOTION_SET_STANCE_KIND(set, stance) (((set) >> ((stance) << 2)) & 0xF)
/* Only the first two weapon classes have the two special stances. */
#define MOTION_SET_SPECIAL_WEAPONS 2
/* In g_motion_loaded_sets: the second file's room holds a chanbara pack. */
#define MOTION_SET_PACK 0xFFFF

/* A motion set and the file it is in: its place in g_motion_set_files is the
 * file's place on the disc, two files into the motion group. */
typedef struct {
    s32 set;
    const char *name; /* never read */
} MotionSetFile;

extern MotionSetFile g_motion_set_files[18];
/* Chanbara's opponents: for each column of the ladder, the two stance kinds
 * it would rather start in. */
extern u8 g_motion_chanbara_stance_kinds[6][2];

/* ---- moves ---- */

/* A move id names a move wherever a move leads to another. Bit 15 set: the
 * move at that halfword of the fighter's own file; clear: of the common
 * file. The ids from MOVE_ID_DEFAULT_STANCE up stand for a move that depends
 * on the fighter's stance. */
#define MOVE_ID_OWN_FILE 0x8000
#define MOVE_ID_INDEX 0x7FFF

/* The move ids that stand for a move of the fighter's stance. */
enum StanceMoveId {
    /* stand in the stance the fight began in */
    MOVE_ID_DEFAULT_STANCE = 0x7FF0,
    /* stand in the next stance */
    MOVE_ID_NEXT_STANCE = 0x7FF1,
    /* stand in this stance */
    MOVE_ID_STANCE = 0x7FF2,
    /* go from this stance to the next */
    MOVE_ID_STANCE_CHANGE = 0x7FF3,
    /* stand in this stance, easing into it */
    MOVE_ID_STANCE_BLEND = 0x7FF4,
    /* the first of the moves the file has once for each stance */
    MOVE_ID_PER_STANCE = 0x7FF5,
};

#define MOVE_ID_PER_STANCE_COUNT 11

/* A move, as it is in a file's moves section (halfwords throughout; a move
 * id indexes the section in halfwords). */
typedef struct MoveScript {
    /* the id of a table of move ids, one for each thing that can happen to
     * the fighter during the move */
    u16 event_table_id;
    /* the move that follows when nothing else was asked for */
    u16 next_move_id;
    /* which of the file's motions it plays (MotionSections.entries) */
    u16 motion_index;
    u8 first_frame;        /* the frame of the motion it begins at */
    u8 last_frame;         /* and the one it ends after */
    u8 last_command_frame; /* no command is taken after this frame */
    u8 flags;              /* MOVE_... */
    /* What the pad can turn the move into: pairs of (flags, move id), a
     * pair with a MOVE_COMMAND_WIDE flag followed by two more halfwords,
     * ended by a zero. */
    u16 commands[1];
} MoveScript;

/* MoveScript.flags */
#define MOVE_TURN_MODE 3 /* how the fighter may turn during the move */
#define MOVE_PAIRED 0x40 /* played from the file of whoever began it */

#define MOVE_NEXT_BORROWED 0x20 /* the move that follows is the opponent's */
/* a command taken during the move waits for its last command frame */
#define MOVE_QUEUES 0x80

/* The flags of a command of a move. */
#define MOVE_COMMAND_BUTTON 0xF /* which bit of the fighter's pad */
/* two of them the code names: PADR1 and the frontal attack */
#define MOVE_COMMAND_BUTTON_R1 3
#define MOVE_COMMAND_BUTTON_FRONTAL 5
/* pseudo-buttons: PADR1 once the opponent can be answered, and attack 1
 * with a usable off arm */
#define MOVE_COMMAND_BUTTON_COUNTER 9
#define MOVE_COMMAND_BUTTON_OFF_ARM 10
#define MOVE_COMMAND_WORD 0x30 /* which word of the pad: */
#define MOVE_COMMAND_HELD 0x00
#define MOVE_COMMAND_PRESSED 0x10
#define MOVE_COMMAND_UNHELD 0x20
#define MOVE_COMMAND_RELEASED 0x30
#define MOVE_COMMAND_BORROWED 0x1000 /* the move is in the opponent's file */
/* not for the pad: what the fighter a paired move is done to goes into
 * when the move ends */
#define MOVE_COMMAND_FOLLOW 0x2000
#define MOVE_COMMAND_PAIRED 0x4000 /* a PairedMoveEntry */
/* only for some characters: two more halfwords, a bit for each */
#define MOVE_COMMAND_FOR_SOME 0x8000
#define MOVE_COMMAND_WIDE 0xC000 /* four halfwords, not two */

/* The command of a move that begins a paired move: the move both fighters go
 * into when one catches the other. */
typedef struct {
    u16 flags;
    u16 id;
    /* how far apart the fighters may be (two bytes: least, most), how far
     * apart the move puts them (all three in units of 20), and a byte of
     * flags and a frame */
    u8 b[4];
} PairedMoveEntry;

/* A motion's head: four bytes, then its marks. */
enum {
    MOTION_HEAD_KIND = 0,   /* enum MoveKind */
    MOTION_HEAD_FRAMES = 1, /* how long it is */
    /* high four bits: how far an attack may turn towards the opponent, in
     * MOTION_TURN_UNITs; low four: the motion's class (MOTION_HEAD_CLASS) */
    MOTION_HEAD_TURN_AND_CLASS = 2,
    /* how far an attack reaches, in MOTION_REACH_UNITs */
    MOTION_HEAD_REACH = 3,
    MOTION_HEAD_SIZE = 4,
};

/* The unit of MOTION_HEAD_REACH, and of the distances of the CPU's attacks
 * (cpu.h). */
#define MOTION_REACH_UNIT 40
/* The unit of the turn in MOTION_HEAD_TURN_AND_CLASS: a thirtieth of a turn,
 * 12 degrees. */
#define MOTION_TURN_UNIT 0x88

/* The class of a motion says which of several it is: for a walk or a step
 * its direction, for standing in a stance the stance's kind less one (so
 * every motion file agrees: 0 to 2 the ordinary stances, then the two
 * special ones), for an attack what kind of blow. */
#define MOTION_HEAD_CLASS 0xF

/* The classes the code names. */
enum {
    MOVE_CLASS_TOWARD = 0, /* a walk or step: towards the opponent, ... */
    MOVE_CLASS_AWAY = 1,
    MOVE_CLASS_SIDE = 2,         /* 2 and 3: to either side */
    STANCE_CLASS_TWO_SWORDS = 3, /* STANCE_KIND_TWO_SWORDS */
    STANCE_CLASS_SHEATHED = 4,   /* STANCE_KIND_SHEATHED */
};

/* What a fighter is doing: every motion says which of these it is. The
 * game's own name for each, where it differs or is Japanese, is in the
 * comment: the table of them is in fight_data.c, its strings in fighter.c
 * (g_str_move_kind_*), and a
 * constant that is not remarked on has the game's name. */
enum MoveKind {
    MOVE_KIND_NULL = 0,
    MOVE_KIND_NORMAL = 1,
    MOVE_KIND_ATTACK = 2,
    MOVE_KIND_DAM = 3,       /* hurt */
    MOVE_KIND_END_GAME = 4,  /* beaten: lying dead */
    MOVE_KIND_SURRENDER = 5, /* "KAISYAKU": giving up, kneeling to be struck */
    MOVE_KIND_DOWN = 6,      /* "KOROGARI": lying or rolling on the ground */
    MOVE_KIND_STAGGER = 7,   /* "KZRE" (kuzure): guard broken */
    MOVE_KIND_CLIMB = 8,     /* "NOBORI": climbing */
    MOVE_KIND_GET_UP = 9,    /* "OKIAGARI" */
    MOVE_KIND_DROP = 10,     /* "ORI": dropping down from a height */
    MOVE_KIND_SUBWEP = 11,   /* throwing a sub-weapon */
    MOVE_KIND_CROUCH_STEP = 12, /* "SYASTEP" */
    MOVE_KIND_BLOCK = 13,       /* "UKE": taking a blow on the blade */
    MOVE_KIND_DEFLECT = 14,     /* "HAJI" (hajiki): beating a weapon aside */
    MOVE_KIND_LOCK = 15,        /* "SERI" (tsubazeriai): blades locked */
    MOVE_KIND_RELOAD = 16,
    MOVE_KIND_PAIRED = 17, /* "KARAMI": the two locked together */
    MOVE_KIND_DIRT = 18,   /* "SUNA", sand: throwing dirt */
    MOVE_KIND_RUN = 19,
    MOVE_KIND_DTH = 20,    /* dying */
    MOVE_KIND_STANCE = 21, /* "KAMAE": standing in a stance */
    MOVE_KIND_SJUMP = 22,
    MOVE_KIND_MJUMP = 23, /* a jump on the spot */
    MOVE_KIND_LJUMP = 24, /* a jump forward */
    MOVE_KIND_WALK = 25,
    MOVE_KIND_STEP = 26,           /* a quick step */
    MOVE_KIND_ARUN = 27,           /* an attack from a run */
    MOVE_KIND_START = 28,          /* a blade lock: beginning, ... */
    MOVE_KIND_LOCK_EVEN = 29,      /* "GOKAKU": ... even, ... */
    MOVE_KIND_LOCK_WINNING_1 = 30, /* "YURI1", "YURI2": ... winning, ... */
    MOVE_KIND_LOCK_WINNING_2 = 31,
    MOVE_KIND_LOCK_LOSING_1 = 32, /* "FURI1", "FURI2": ... losing */
    MOVE_KIND_LOCK_LOSING_2 = 33,
    MOVE_KIND_CLIMB_ST = 34, /* "NOBORI_S": a climb, taking hold, ... */
    MOVE_KIND_CLIMB_ED = 35, /* "NOBORI_E": ... and pulling up */
    MOVE_KIND_MOVE = 36,
    MOVE_KIND_STANCE_CHANGE = 37, /* "KAMAE_KA" */
    MOVE_KIND_CLIMB_ATTACK = 38,  /* "ANOBORI": an attack from a climb */
    MOVE_KIND_HOM_ST = 39,        /* the leaping attack: take-off, ... */
    MOVE_KIND_HOM_AT = 40,        /* ... flight, ... */
    MOVE_KIND_HOM_ED = 41,        /* ... landing */
    MOVE_KIND_CROUCH = 42,        /* "SYAGAMI" */
    MOVE_KIND_LOCK_EXIT = 43,     /* "SERIEXIT": leaving a blade lock */
    MOVE_KIND_DEFLECTED = 44,     /* "HAJIKARE": weapon beaten aside */
    MOVE_KIND_SURRENDER_ST = 45,  /* "KAISHAKU_ST": awaiting the last blow */
    MOVE_KIND_APPEAR = 46,
    MOVE_KIND_LAND = 47,      /* "CHAKUTI" (chakuchi): landing */
    MOVE_KIND_RUN_ST = 48,    /* breaking into a run */
    MOVE_KIND_RUN_ED = 49,    /* pulling up from one */
    MOVE_KIND_PAIRED_ED = 50, /* "KARAMI_ED": a paired move's end */
    MOVE_KIND_WIN = 51,       /* the win pose */
};

/* The four kinds in which a weapon strikes. */
#define MOVE_IS_ATTACK(kind)                                                   \
    ((kind) == MOVE_KIND_ATTACK || (kind) == MOVE_KIND_ARUN ||                 \
     (kind) == MOVE_KIND_HOM_AT || (kind) == MOVE_KIND_CLIMB_ATTACK)
/* The kinds of a blade lock. */
#define MOVE_IS_LOCK(kind)                                                     \
    ((kind) == MOVE_KIND_LOCK || (kind) == MOVE_KIND_START ||                  \
     (kind) == MOVE_KIND_LOCK_EVEN || (kind) == MOVE_KIND_LOCK_WINNING_1 ||    \
     (kind) == MOVE_KIND_LOCK_WINNING_2 ||                                     \
     (kind) == MOVE_KIND_LOCK_LOSING_1 || (kind) == MOVE_KIND_LOCK_LOSING_2)

/* A motion's marks: the frames of a motion at which something happens. After
 * the motion's head comes a list of them, ended by a zero: a mark of 0x80 up
 * is followed by its frame; one below 0x80 is a frame event (a sound or a
 * footprint, fighter_RunFrameEvents), followed by its frame; 0xFF is
 * followed by four bytes of a mask of characters, and the two bytes after
 * those are skipped for a character that is not in it. */
#define MOVE_NO_FRAME 0xFF      /* the frame of a mark the move has not */
#define MOVE_MARK_FIRST 0x80    /* below it: a frame event */
#define MOVE_MARK_FOR_SOME 0xFF /* a mask of characters follows */
#define MOVE_MARK_MASK_SIZE 4

/* The marks: each sets one of the Fighter's frames (fighter_ReadMarks). */
enum MoveMark {
    MOVE_MARK_HIT_START = 0x80, /* the weapon can hit from here ... */
    MOVE_MARK_HIT_END = 0x81,   /* ... to here */
    MOVE_MARK_RELEASE = 0x82,   /* what is thrown leaves the hand */
    MOVE_MARK_PICKUP = 0x83,    /* the hand reaches what lies on the ground */
    MOVE_MARK_FACE = 0x84,      /* getting up: turn to the opponent */
    MOVE_MARK_OFF_HAND_START = 0x85,    /* the off hand's weapon is out ... */
    MOVE_MARK_OFF_HAND_END = 0x86,      /* ... to here */
    MOVE_MARK_BLADE_2_HIT_START = 0x87, /* the same for a second sword */
    MOVE_MARK_BLADE_2_HIT_END = 0x88,
    MOVE_MARK_PAIRED_HIT = 0x89, /* a paired move's blow lands */
    MOVE_MARK_COUNTER = 0x8A,    /* the opponent may answer from here */
    MOVE_MARK_RELOAD = 0x8B,     /* the gun is loaded again */
};

/* ---- motion files ---- */

/* The head of a fighter's motion file. Its four words of offsets are where
 * the sections after the moves begin; the common file has the same four
 * words and its moves straight after them. */
typedef struct {
    u8 pad00[3];
    u8 stance_move_rows; /* rows of per_stance_moves in the file */
    /* the motions, their marks, the CPU's scripts, the stream */
    s32 section_offsets[4];
    s16 stance_count;
    /* Moves of the common file, by halfword: */
    u16 entrance_move; /* how the fighter comes on */
    u16 win_moves[27]; /* each character's pose when it has won */
    /* Moves of this file, by halfword: */
    u16 stance_moves[STANCE_COUNT]; /* standing in each stance */
    /* going from one stance [from] to another [to] */
    u16 stance_change_moves[STANCE_COUNT][STANCE_COUNT];
    /* the moves MOVE_ID_PER_STANCE up stand for */
    u16 per_stance_moves[MOVE_ID_PER_STANCE_COUNT][STANCE_COUNT];
} MotionFile;

/* Where the sections of a loaded file are. */
typedef struct MotionSections {
    u16 *moves; /* MoveScripts */
    /* a motion is four bytes: its first frame in the stream, and where its
     * marks are in `metadata` */
    u16 *entries;
    u8 *metadata;
    u16 *cpu_scripts; /* not in the common file */
    u32 *stream;      /* the frames (motion_decode.c) */
} MotionSections;     /* 0x14 bytes */

extern s32 g_motion_common_file;
extern MotionFile *g_motion_fighter_files[4];
extern MotionSections g_motion_common_sections;
extern MotionSections g_motion_fighter_sections[2]; /* per fighter file */
extern u8 g_motion_common_loaded;
/* The motion set of each of the two fighter files (0: none). Two fighters
 * with the same set share a file. */
extern u16 g_motion_loaded_sets[2];

/* ---- frames ---- */

/* A decoded frame: the pose of a model. 66 values of 16 bits; the angles are
 * 4096 to the turn. */
typedef struct MotionFrame {
    /* where the body is from where the fighter stands: how high, and how far
     * in which direction */
    s16 root_height;
    u16 root_angle;
    u16 root_distance;
    /* the rotation of the waist, which the rest hangs from */
    u16 waist_angle_x;
    u16 waist_angle_y;
    u16 waist_angle_z;
    /* three angles for each of the other 20 bones; two of the triples (the
     * 17th and the 19th) are signed 12-bit values instead */
    s16 bone_angles[60];
} MotionFrame; /* 0x84 bytes */

/* Places in bone_angles that the fight bends (motion_AimPose): */
#define MOTION_TORSO 21       /* the upper body: three angles */
#define MOTION_GUN_AIM 51     /* the arm of a fighter whose weapon is a gun */
#define MOTION_SIDEARM_AIM 57 /* the arm with a gun in the off hand */
#define MOTION_ARM 24         /* an arm's four bones (motion_HangArm) */

/* The values of a frame: the root's three, the waist's three, the 60. */
#define MOTION_FRAME_VALUES 66
/* the values after the root's three, which the stream packs in 12 bits */
#define MOTION_PACKED_VALUES 63

/* A frame a decoder has kept, with what is needed to decode the one after
 * it. */
typedef struct MotionDecoderCache {
    s32 frame;                     /* +0x000 which one; MOTION_NO_FRAME: none */
    u16 pose[MOTION_FRAME_VALUES]; /* +0x004 */
    /* +0x088 how fast each value was changing */
    u16 rate[MOTION_FRAME_VALUES];
    /* where the stream stands after the frame: */
    u32 *next;        /* +0x10C the next word */
    s32 bits;         /* +0x110 how many bits of `word` are unread */
    u32 word;         /* +0x114 the unread bits, at the top */
} MotionDecoderCache; /* 0x118 bytes */

#define MOTION_NO_FRAME (-2)

#define MOTION_DECODER_CACHES 4

/* The decoder of one stream. */
typedef struct MotionDecoder {
    u32 *stream; /* +0x000 */
    /* +0x004 the pose a value keeps when the stream leaves it out */
    u16 pose[MOTION_FRAME_VALUES];
    /* +0x088 how each value is packed (two bits each in the stream) */
    u16 code[MOTION_FRAME_VALUES];
    s32 turn; /* +0x10C counts the calls; the next cache to use */
    MotionDecoderCache cache[MOTION_DECODER_CACHES]; /* +0x110 */
} MotionDecoder;                                     /* 0x570 bytes */

/* Which decoder is which, in a fight. */
enum {
    MOTION_DECODER_COMMON = 0,
    MOTION_DECODER_FILE = 1, /* + the fighter's file */
};

/* Each fighter's win pose: the frames of its character's part of WIN.DAT,
 * which is not packed. */
extern MotionFrame *g_motion_win_frames[2];
/* Whose win pose each of them holds (MOTION_NO_WIN_FRAMES: nobody's). */
extern u8 g_motion_win_frames_loaded[2];
#define MOTION_NO_WIN_FRAMES 0xFF
#define MOTION_WIN_SECTORS 7 /* sectors of WIN.DAT a character has */

/* The files of the disc's motion folder. */
enum {
    MOTION_FILE_WIN = 0,    /* WIN.DAT */
    MOTION_FILE_COMMON = 1, /* COMMON.BBM */
    MOTION_FILE_SETS = 2,   /* the first of g_motion_set_files */
};

/* Where a fight keeps them, from the start of the load area: the two win
 * poses, the common file, the first fighter file. The second fighter file
 * is at SYS_STAGING_ADDR. */
#define MOTION_WIN_FRAMES_ADDR SYS_LOAD_ADDR
#define MOTION_WIN_FRAMES_2_ADDR 0x8011C400
#define MOTION_COMMON_FILE_ADDR 0x80120000
#define MOTION_FIGHTER_FILE_ADDR 0x80148800

/* Starts decoding the stream at `next` with decoder `decoder_index`. */
extern void motion_OpenStream(s32 decoder_index, u32 *next);
/* Decodes frame `frame_index` of the decoder's stream into `out`. `work` is
 * room for three frames' worth of halfwords (the scratchpad). */
extern void motion_DecodeFrame(
    s32 decoder_index, s32 frame_index, MotionFrame *out, u16 *work);

/* ---- fighter.c ---- */

/* Says where the files are loaded to; nothing is loaded yet. */
extern void motion_InitBuffers(void);
/* Nothing that was loaded is any longer. */
extern void motion_ForgetFiles(void);
/* Loads what the two fighters need and is not already there: the common
 * file, a file for each one's motion set, and the win poses. */
extern void motion_LoadFighterFiles(
    s32 character_1, s32 weapon_1, s32 character_2, s32 weapon_2);
extern void motion_OpenStreams(void);
/* Chanbara: the opponents' models are read as a pack into the room of the
 * second fighter file, which that mode does not need. */
extern void motion_DropChanbaraPack(void);

/* Finds which file the fighter's motion set is in, and which of its stances
 * are the special ones. */
extern void motion_AttachFighter(s32 fighter_index);
/* The move a move id stands for, for this fighter now. *is_common_out says
 * whether it is in the common file. The stance ids change the fighter's
 * stance. */
extern void *motion_GetMove(
    struct Fighter *fighter, s32 id, s16 *is_common_out);
extern void motion_ResetStance(s32 fighter_index);
extern MoveScript *motion_GetStanceMove(s32 fighter_index);
extern MoveScript *motion_GetDefaultStanceMove(s32 fighter_index);
extern MoveScript *motion_GetEntranceMove(s32 fighter_index);
extern MoveScript *motion_GetWinMove(s32 fighter_index, s32 character);

/* ---- with the fighter's code ---- */

/* How fast the fighter's move runs this frame: slower for its wounds. */
extern void motion_UpdateFrameStep(struct Fighter *fighter);
/* Bends the two poses a fighter is drawn between: towards the opponent, and
 * for its wounds. */
extern void motion_AimPose(
    struct Fighter *fighter, MotionFrame *from_pose, MotionFrame *to_pose);
/* The same from what the replay recorded. */
extern void motion_AimReplayPose(
    struct Fighter *fighter, struct ReplayFighterFrame *recorded,
    MotionFrame *from_pose, MotionFrame *to_pose);
/* Poses an arm that can no longer be used: it hangs. */
extern void motion_HangArm(MotionFrame *frame);

/* ---- bitstream.c ---- */

/* Both of the motions' bit readers (this one and motion_decode.c's) take
 * their stream a word of this many bits at a time. */
#define BITSTREAM_WORD_BITS 32

/* A stream of bits, read most significant bit first. */
typedef struct {
    u32 *next; /* the next word to take */
    u32 word;  /* the word being read */
    s32 avail; /* how many of its low bits are unread */
} BitStream;

/* A track: six values that a stream changes a little every frame. A
 * cutscene's camera is one (where it is and how it is turned). */
typedef struct {
    BitStream bits;
    s16 values[6];
} BitTrack;

/* Starts track `track_index` at the head of the stream at `stream_address`, all
 * its values 0. */
extern void bitstream_OpenTrack(s32 stream_address, s32 track_index);
/* Reads a frame; returns the track's six values. */
extern s16 *bitstream_StepTrack(s32 track_index);

/* The names of the move kinds and of the motion files, which the tables in
 * fight_data.c point at (the long ones are in fighter.c). */
extern const char g_str_move_kind_win[];
extern const char g_str_move_kind_paired_ed[];
extern const char g_str_move_kind_run_ed[];
extern const char g_str_move_kind_run_st[];
extern const char g_str_move_kind_land[];
extern const char g_str_move_kind_appear[];
extern const char g_str_move_kind_surrender_st[];
extern const char g_str_move_kind_deflected[];
extern const char g_str_move_kind_lock_exit[];
extern const char g_str_move_kind_crouch[];
extern const char g_str_move_kind_hom_ed[];
extern const char g_str_move_kind_hom_at[];
extern const char g_str_move_kind_hom_st[];
extern const char g_str_move_kind_climb_attack[];
extern const char g_str_move_kind_stance_change[];
extern const char g_str_move_kind_move[];
extern const char g_str_move_kind_climb_ed[];
extern const char g_str_move_kind_climb_st[];
extern const char g_str_move_kind_lock_losing_2[];
extern const char g_str_move_kind_lock_losing_1[];
extern const char g_str_move_kind_lock_winning_2[];
extern const char g_str_move_kind_lock_winning_1[];
extern const char g_str_move_kind_lock_even[];
extern const char g_str_move_kind_start[];
extern const char g_str_move_kind_arun[];
extern const char g_str_move_kind_step[];
extern const char g_str_move_kind_walk[];
extern const char g_str_move_kind_ljump[];
extern const char g_str_move_kind_mjump[];
extern const char g_str_move_kind_sjump[];
extern const char g_str_move_kind_stance[];
extern const char g_str_move_kind_dth[];
extern const char g_str_move_kind_run[];
extern const char g_str_move_kind_dirt[];
extern const char g_str_move_kind_paired[];
extern const char g_str_move_kind_reload[];
extern const char g_str_move_kind_lock[];
extern const char g_str_move_kind_deflect[];
extern const char g_str_move_kind_block[];
extern const char g_str_move_kind_crouch_step[];
extern const char g_str_move_kind_subwep[];
extern const char g_str_move_kind_drop[];
extern const char g_str_move_kind_get_up[];
extern const char g_str_move_kind_climb[];
extern const char g_str_move_kind_stagger[];
extern const char g_str_move_kind_down[];
extern const char g_str_move_kind_surrender[];
extern const char g_str_move_kind_end_game[];
extern const char g_str_move_kind_dam[];
extern const char g_str_move_kind_attack[];
extern const char g_str_move_kind_normal[];
extern const char g_str_move_kind_null[];
extern const char g_str_motion_file_y123[];
extern const char g_str_motion_file_n123[];
extern const char g_str_motion_file_k123[];
extern const char g_str_motion_file_t123[];
extern const char g_str_motion_file_s234[];
extern const char g_str_motion_file_s125[];
extern const char g_str_motion_file_s124[];
extern const char g_str_motion_file_s123[];
extern const char g_str_motion_file_u235[];
extern const char g_str_motion_file_u135[];
extern const char g_str_motion_file_u134[];
extern const char g_str_motion_file_u125[];
extern const char g_str_motion_file_u124[];
extern const char g_str_motion_file_u123[];
extern char g_str_motion_file_on[];
extern char g_str_motion_file_gs[];
extern char g_str_motion_file_gn[];

/* Used only inside motion_decode.c. */

/* In a fight: the common file's, then the two fighter files'. In a cutscene:
 * the two fighters'. */
extern MotionDecoder g_motion_decoders[3];

#endif /* MOTION_H */
