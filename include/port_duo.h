#ifndef PORT_DUO_H
#define PORT_DUO_H

/* The native port's two on two (ROADMAP 24; docs/PORT.md, "Two on two";
 * docs/PLAYING.md, "Two on two"): four fighters in one fight, two teams.
 *
 * The game's fight is written for two: g_fighters[2], each fighter's
 * `other`, the scratchpad's points of two, the pair's globals (how far
 * apart, the paired move, the blade lock), two model slots, two motion
 * files and two characters' textures in video memory. Four fighters are
 * held by giving each fighter a place of its own here and putting the
 * ones a step is about into the game's two slots: a team is a slot (the
 * first team the first fighter's, the second the second's), so a fighter
 * is only ever put in its own team's slot, beside an enemy. What a
 * fighter has that is a slot's (its record, its model, its scratchpad
 * points and body points, its frames of the replay's ring) goes with it;
 * what a pair has (the game's pair globals) is kept for each of the four
 * pairs of enemies.
 *
 * Video memory holds two characters' textures and the RAM two motion
 * files, so a team is two of its first fighter's character with his
 * weapon: the second of each team is a copy of the first's model (its
 * own nodes, matrices and cloth, the meshes and textures shared).
 *
 * Each fighter targets an enemy (his `other` while he is stepped); a
 * press of the retarget button (R2, which the fight does not read) turns
 * him to the other enemy, and a fighter whose target falls turns to the
 * one left. The CPU turns to an enemy much nearer. A paired move or a
 * blade lock is between two: neither of them is struck by, or strikes,
 * anyone else while it lasts. Team mates' blades pass through each other
 * (no friendly fire) and their bodies are kept apart. A team whose two
 * fighters have fallen has lost: the game's own fall, replay and winner's
 * view follow, of the last to fall and the enemy who struck him.
 *
 * All of it is the port's and under RB_PORT; with two on two off the
 * fight is the game's own, frame for frame. src/port/duo.c is a state
 * file: rolled back, saved and hashed with the game. */

#include "common.h"

struct Fighter;
struct PadState;

#define PORT_DUO_FIGHTERS 4
/* the enemies' pairs: (0, 1), (0, 3), (2, 1), (2, 3) */
#define PORT_DUO_PAIRS 4
#define PORT_DUO_TEAMS 2

/* Who plays a team's second fighter (PortDuo.partner). */
enum {
    PORT_DUO_PARTNER_CPU = 0, /* the CPU */
    PORT_DUO_PARTNER_PAD = 1, /* the third pad (the first team), the fourth */
};

/* The window's choice, as the rule page's row has it (PortMatch.page_teams,
 * port_match.h): one on one, two on two with the CPU's partners, or with
 * four players. */
enum {
    PORT_TEAMS_ONE = 0,
    PORT_TEAMS_TWO_CPU = 1,
    PORT_TEAMS_TWO_PADS = 2,
    PORT_TEAMS_CHOICES = 3
};

/* A fighter's slot (its team) and his team mate. */
#define PORT_DUO_SLOT(who) ((who) & 1)
#define PORT_DUO_MATE(who) ((who) ^ 2)

/* ---- what the game calls (under RB_PORT) ---- */

/* The fight about to be set up (fight.c, fight_Init, before anything is
 * loaded): whether it is two on two (the match's choice, in a mode that
 * has matches), and the slots' own models back in them. */
extern void port_DuoFightBegin(void);
/* The fight set up (fight_Init, its two fighters in their first moves):
 * the second of each team made, his model copied, all four placed. */
extern void port_DuoFightSet(void);
/* A frame begins (main.c): the port's own primitive buffers while a fight
 * of two on two is on, where the run has the console's. */
extern void port_DuoPrimBuffers(void);
/* This fight is two on two: fight_Update hands its frame to the duo's. */
extern s32 port_DuoIsOn(void);
/* The models of the two slots back in their slots, before the arena may
 * move them (model.c, model_Rebase; a load). */
extern void port_DuoModelsHome(void);
/* The engine's state reset (model.c, model_ClearAll, at boot and after
 * the movie): the copies and their cloth are no longer anyone's. */
extern void port_DuoForget(void);
/* The address is in the room of a copy's cloth (arena.c). */
extern s32 port_DuoHoldsAddress(s32 address);
/* A slot's model moved in the arena (resource.c): its copy follows. */
extern void port_DuoModelMoved(s32 slot, s32 delta);

/* ---- a frame of two on two (fight.c, fight_UpdateDuo) ---- */

/* The two points and fighters the side view frames: the ends of all four
 * along the line from the first team to the second. */
extern void port_DuoCameraPoints(
    Vec3 *first_point, Vec3 *second_point, struct Fighter **first_fighter,
    struct Fighter **second_fighter);
/* Each fighter's target for the frame: the retarget presses, the fallen,
 * the CPU's choice, those held by a paired move or a lock. */
extern void port_DuoChooseTargets(void);
/* A pair of enemies (0 to 3) in the two slots with its own globals: TRUE
 * if it is to be measured (fight_UpdateDistance; `meet` 0) or to meet
 * (fight_UpdateFighterPair; `meet` 1) this frame. */
extern s32 port_DuoPutPair(s32 pair, s32 meet);
/* A fighter (0 to 3) in his slot, his target in the other, their pair's
 * globals: then his pad for the frame into `pad`. */
extern void port_DuoPutFighter(s32 who, struct PadState *pad);
/* After the fighter's step: his frame of the replay's ring kept. */
extern void port_DuoFighterDone(s32 who);
/* Before a pair of enemies meets: how hurt each is. */
extern void port_DuoBeforeMeet(void);
/* After a pair of enemies met (fight_UpdateFighterPair): a blow that
 * wounded or felled one of them was the other's. */
extern void port_DuoAfterMeet(void);
/* Where a fighter is (his world_pos), for the stage's cells to open. */
extern void port_DuoPlace(s32 who, Vec3 *at);
/* Team mates' bodies kept apart. */
extern void port_DuoKeepMatesApart(void);
/* The pair the fight rests on between steps put in the slots (the living,
 * or the last to fall and his enemy), the winner said once a team is out,
 * the replay's frame of the two written. */
extern void port_DuoEndFrame(void);
/* fight_UpdateDistance's paired moves may begin between the two in the
 * slots (fighter_pose.c): not when either is held by a third or fallen;
 * a paired move asked for then is let go. TRUE with two on two off. */
extern s32 port_DuoMayPair(void);

/* ---- what the window reads (outside the state's rules: read only) ---- */

typedef struct {
    s16 x;
    s16 y;
    u8 seen; /* in front of the camera */
} PortDuoTag;

typedef struct {
    u8 on;
    u8 alive[PORT_DUO_FIGHTERS];
    u8 target[PORT_DUO_FIGHTERS];
    u8 is_cpu[PORT_DUO_FIGHTERS];
    PortDuoTag tag[PORT_DUO_FIGHTERS];
} PortDuoShown;

/* What the window shows of the fight now (gamestate.c). */
extern void port_DuoShown(PortDuoShown *shown);

/* ---- the run's choice (src/port/play_switches.c, outside the state) ---- */

/* The window's `teams` (--teams one|cpu|pads): what the rule page starts
 * at, and what a mode with no rule page plays. In a session the host's. */
extern void port_SetTeamsDefault(s32 teams);
extern s32 port_GetTeamsDefault(void);

#endif /* PORT_DUO_H */
