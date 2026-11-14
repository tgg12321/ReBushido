#ifndef STORY_H
#define STORY_H

/* Story mode. Each character has a script, a string of commands (enum
 * StoryCommand) that story_RunScript reads until one of them has something
 * to show: a stage card, a cutscene, a fight, the ending. What was showing
 * calls it again when it is over.
 *
 * The story goes through stages, here "chapters". A chapter opens on the
 * stage card, a map on which the route so far is marked, and usually sets
 * the clan's henchmen on the player (the roster's two ninja,
 * ROSTER_SIDEARM_1 and _2): a number of them, one after another, in a
 * colour of their own. Then come the opponents the script names, with
 * cutscenes before and after. The player's fighter keeps its wounds through
 * a chapter.
 *
 * In an "interlude" the player plays another character, the guest, against
 * henchmen and then an opponent. If the guest falls the player's own
 * fighter takes its place; a guest that wins is added to the characters the
 * save data unlocks, and may turn up later as an opponent with the wounds
 * it took. The story's last chapter can end on the "victim", an unarmed
 * opponent that waits to be struck: the player strikes, or walks away.
 *
 * Named from the code and the scripts; the victim's three scenes have been
 * seen running, an interlude won by the guest has not (docs/STATUS.md). */

#include "common.h"

/* The commands, each followed by its operands. */
enum StoryCommand {
    /* the end: the ending, or the staff roll if a continue was used */
    STORY_END = 0x00,
    /* (stage) the next chapter: shows the stage card */
    STORY_STAGE = 0x01,
    /* (STORY_INTERLUDE_SIZE bytes) an interlude: starts its first fight */
    STORY_INTERLUDE = 0x02,
    /* (roster, weapon) who the second fighter is from here on: for the
     * cutscenes it is in; FIGHT_WEAPON_SAME gives it the player's weapon */
    STORY_SECOND_FIGHTER = 0x03,
    /* (STORY_HENCHMEN_SIZE bytes) the opponent is the henchmen */
    STORY_HENCHMEN = 0x10,
    /* (STORY_OPPONENT_SIZE bytes) the opponent is one the script names */
    STORY_OPPONENT = 0x11,
    /* the fight that has been set up */
    STORY_FIGHT = 0x12,
    /* (STORY_CUTSCENE_SIZE bytes) a cutscene */
    STORY_CUTSCENE = 0x13,
    /* (three cutscenes) the victim: plays the first, then the fight */
    STORY_VICTIM = 0x14,
};

/* A cutscene in a script. */
enum {
    STORY_CUTSCENE_FILE = 0,    /* which of the character's set of cutscenes */
    STORY_CUTSCENE_VOICE = 1,   /* its voice: an audio file, ... */
    STORY_CUTSCENE_CHANNEL = 2, /* ... and the channel of it */
    STORY_CUTSCENE_FLAGS = 3,
    STORY_CUTSCENE_SIZE = 4,
};

/* STORY_CUTSCENE_FLAGS. */
enum {
    STORY_CUTSCENE_WEAPON_1 = 0x01, /* the first fighter has its weapon, ... */
    STORY_CUTSCENE_WEAPON_2 = 0x02, /* ... the second its */
    /* a fighter holds the weapon that story scenes have instead */
    STORY_CUTSCENE_STORY_WEAPON_1 = 0x10,
    STORY_CUTSCENE_STORY_WEAPON_2 = 0x20,
    STORY_CUTSCENE_WHOLE_STAGE = 0x40, /* not one area of the stage */
    /* too long for the room below the motion files: it takes theirs */
    STORY_CUTSCENE_LONG = 0x80,
};

/* The operands of STORY_HENCHMEN. */
enum {
    STORY_HENCHMEN_NUMBER = 0,
    STORY_HENCHMEN_LEVEL = 1, /* how well the CPU plays them */
    STORY_HENCHMEN_RED = 2,   /* their colour */
    STORY_HENCHMEN_GREEN = 3,
    STORY_HENCHMEN_BLUE = 4,
    STORY_HENCHMEN_SIZE = 5,
};

/* The operands of STORY_OPPONENT. */
enum {
    STORY_OPPONENT_ROSTER = 0,
    STORY_OPPONENT_WEAPON = 1,
    STORY_OPPONENT_LEVEL = 2,
    STORY_OPPONENT_SIZE = 3,
};

/* The operands of STORY_INTERLUDE: the guest, its henchmen and its
 * opponent, then five cutscenes, numbered from 2 (enum InterludeScene). */
enum {
    STORY_INTERLUDE_GUEST = 0, /* a roster entry */
    STORY_INTERLUDE_HENCHMEN = 1,
    STORY_INTERLUDE_OPPONENT = 6,
    STORY_INTERLUDE_SCENES = 1, /* scene n is at SCENES + n * 4 */
    STORY_INTERLUDE_SIZE = 29,
};

/* The five cutscenes of an interlude. */
enum InterludeScene {
    INTERLUDE_GUEST_MEETS_OPPONENT = 2, /* the guest has beaten the henchmen */
    INTERLUDE_GUEST_WON = 3,
    INTERLUDE_GUEST_FELL = 4, /* the player's fighter takes over */
    INTERLUDE_PLAYER_MEETS_OPPONENT = 5,
    INTERLUDE_PLAYER_WON = 6, /* also when both fell */
};

/* The victim's three cutscenes, numbered from 1; scene n is at
 * (n - 1) * 4 of the command's operands. */
enum VictimScene {
    VICTIM_SCENE_MEET = 1,
    VICTIM_SCENE_STRUCK = 2,
    /* the player's fighter has walked onto the other class of ground, which
     * on the victim's stage lies round the edge of the floor: it has walked
     * away */
    VICTIM_SCENE_SPARED = 3,
    VICTIM_SCENES = 3,
};

/* Where the reading of the character's script has got to. */
extern u8 *g_story_script_cursor;

/* The chapter, counted from 1; the fights started in it; and the route:
 * the stages so far, which the stage card marks one by one while the next
 * chapter loads (`route_drawn` of them so far, `route_timer` calls since the
 * last). */
extern u8 g_story_chapter;
extern u8 g_story_chapter_fights;
extern u8 g_story_route[];
extern u8 g_story_route_drawn;
extern s32 g_story_route_timer;

/* The opponent: one the script names, or henchmen; and of those, how many
 * there were, how many are left, and their colour. `first_henchmen` is set
 * until the game's first band of them has been beaten, which ends in a
 * replay like any fight. */
extern u8 g_story_opponent_named;
extern u8 g_story_henchmen_count;
extern u8 g_story_henchmen_left;
extern u8 g_story_first_henchmen;
extern CVECTOR g_story_henchman_tint; /* the henchmen's colour */

/* The interlude being played (its operands), or NULL, and the scene of it
 * that is playing. While the guest is in, `player_roster` and
 * `player_wounds` keep the player's own fighter (STORY_NO_ROSTER: no guest
 * is in); afterwards `guest_roster` and `guest_wounds` keep the guest. */
extern u8 *g_story_interlude;
extern u8 g_story_interlude_scene;
extern u8 g_story_player_roster;
extern u8 g_story_player_wounds;
extern u8 g_story_guest_roster;
extern u8 g_story_guest_wounds;
#define STORY_NO_ROSTER 0xFF
/* The characters won as a guest: added to the save data after the staff
 * roll. */
extern s32 g_story_unlocks;

/* The victim's cutscenes while that fight is on, or NULL, and the one that
 * is playing. */
extern u8 *g_story_victim;
extern u8 g_story_victim_scene;

/* The cutscene SCREEN_CUTSCENE is to play (STORY_CUTSCENE_...), and the
 * character's set of them. */
extern u8 *g_story_cutscene;
extern u8 g_story_cutscene_set;

/* The stage card's screen (enum Screen). */
extern void story_StageCardInit(void);
extern void story_StageCardUpdate(void);

/* Starts the story of `character`. */
extern void story_Start(s32 character);
extern void story_RunScript(void);
/* Keeps the stage card up until the whole route has been marked. */
extern void story_FinishStageCard(void);
/* Makes the opponent the one named by STORY_OPPONENT's operands and loads
 * it; returns STORY_OPPONENT_SIZE. */
extern s32 story_SetOpponent(u8 *operands);
/* Takes the guest out and puts the player's own fighter back. */
extern void story_PutPlayerBack(void);
/* Goes to the cutscene screen with a scene of the interlude or of the
 * victim. */
extern void story_PlayInterludeScene(s32 scene);
extern void story_PlayVictimScene(s32 scene);

#endif /* STORY_H */
