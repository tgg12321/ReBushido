#ifndef PORT_TOURNAMENT_H
#define PORT_TOURNAMENT_H

/* A local tournament on the native port (ROADMAP 31; docs/PORT.md, "A
 * local tournament"; docs/PLAYING.md, "Tournament"): two to eight
 * entrants on one machine, each a player at one of the PC's four pads or
 * the CPU at one of its levels (port_cpu.h), in a bracket of single or
 * double elimination (port_bracket.h), every match a match of the rules
 * of port_match.h (one fight, Stock N or First to N).
 *
 * - The main menu's Tournament row opens the window's page of it (the
 *   entrants, the elimination, the seeding, the match), whose Start hands
 *   the setup to the game; between the matches the window shows the
 *   bracket (src/port/window/tournament_page.c), whose Play starts the
 *   next match and whose Leave ends it there. A tournament left is not
 *   kept: there is nothing to come back to.
 * - A match with a player in it goes to versus mode's select screen: each
 *   player picks his fighter and weapon with his own pad (the pads are
 *   given to the sides the match's entrants have: a player is always the
 *   first side where the other is the CPU), the CPU's are picked for it
 *   on the same screen (its cursor put on them, Cross pressed), and a
 *   player picks the stage. A match of two CPUs goes straight to its
 *   fight, on a stage drawn by the tournament's dice. A CPU's fighter and
 *   weapon are set, or drawn for each match (the six fighters an empty
 *   save has, the six blades).
 * - Each match is a match of versus with the tournament's rule (no rule
 *   page); its fights are the game's own, the CPU's level each CPU
 *   entrant's. "One fight" is First to 1, so that a drawn fight is fought
 *   again and every match has a winner. A fight of two CPUs that goes on
 *   three minutes is judged (PORT_TOURNAMENT_JUDGE_FRAMES): the CPUs can
 *   circle each other for ever, and nobody would be there to stop them. Where
 * the match is decided the game goes back to its main menu, the bracket is
 * told, and the window shows it.
 * - The record (port_stats.h): each fight a person plays is counted as
 *   any versus fight is, the tournament's date among its words; a fight
 *   of two CPUs, which counts nothing there, is still a match of the
 *   tournament. The tournament's own line, its entrants, every match's
 *   result and the places, is written to the record when it is over.
 *
 * What the tournament is (g_port_tournament: the setup, the bracket, the
 * match in play) is the game's state (src/port/tournament.c), so that a
 * rollback, run-ahead and a fight's replay see it as the fight did. The
 * window's asks (begin, play, leave) are kept outside it
 * (src/port/tournament_ask.c), each a count that only goes forward and
 * the number of the input it is for, and taken in as the game takes that
 * input (port_TakeInput), as the window's menu sounds are: a step run
 * again takes it at the same input, a frame run ahead takes it and gives
 * it back. The log's lines and the record are written at the host's
 * boundary between two steps (port_TournamentBoundary), once. Never in a
 * session of netplay, but as a room's tournament's one match
 * (PortTournamentForm, below). The PlayStation builds never see this
 * header. */

#include "common.h"
#include "port_bracket.h"

struct PortInput; /* port_platform.h */

#define PORT_TOURNAMENT_NAME_BYTES 16
/* who plays an entrant: a pad of the PC's (0 to 3), or the CPU */
#define PORT_TOURNAMENT_PADS 4
#define PORT_TOURNAMENT_CPU 0xFF
/* a CPU's fighter or weapon drawn for each match */
#define PORT_TOURNAMENT_RANDOM 0xFF

/* The window's asks, in the order they are taken. */
enum {
    PORT_TOURNAMENT_ASK_LEAVE = 0,
    PORT_TOURNAMENT_ASK_BEGIN = 1,
    PORT_TOURNAMENT_ASK_PLAY = 2,
    PORT_TOURNAMENT_ASKS = 3,
};

enum {
    PORT_TOURNAMENT_OFF = 0,
    PORT_TOURNAMENT_BETWEEN = 1,  /* at the main menu, the bracket shown */
    PORT_TOURNAMENT_STARTING = 2, /* the next match asked for */
    PORT_TOURNAMENT_PLAYING = 3,  /* its select screen or its fights */
    PORT_TOURNAMENT_DONE = 4,     /* over: the champion shown */
};

/* What the main menu does for the match asked for. */
enum {
    PORT_TOURNAMENT_STAY = 0,      /* nothing: the menu as it is */
    PORT_TOURNAMENT_TO_SELECT = 1, /* versus mode's select screen */
    PORT_TOURNAMENT_TO_FIGHT = 2,  /* two CPUs: the fight itself */
};

/* The seeding: drawn by the dice, or the entrants' order. */
enum {
    PORT_TOURNAMENT_SEED_RANDOM = 0,
    PORT_TOURNAMENT_SEED_ORDER = 1,
};

typedef struct {
    char name[PORT_TOURNAMENT_NAME_BYTES];
    u8 pad;     /* PORT_TOURNAMENT_CPU, or the pad that plays it */
    u8 cpu;     /* the CPU's level (PORT_CPU_...) */
    u8 fighter; /* a character (enum CharacterId), or RANDOM */
    u8 weapon;  /* a weapon (enum WeaponId), or RANDOM */
} PortTournamentEntrant;

/* What a tournament is begun with: the window's page, or --tournament. */
typedef struct {
    u8 count;
    u8 is_double;
    u8 seeding;
    u8 rule;       /* PORT_MATCH_FIRST_TO or PORT_MATCH_STOCK */
    u8 rule_count; /* its number */
    u32 seed;      /* the dice: the seeding, the CPUs' draws, the stages */
    PortTournamentEntrant entrant[PORT_BRACKET_MOST];
} PortTournamentSetup;

typedef struct {
    u8 phase;       /* PORT_TOURNAMENT_... */
    u8 ask_bracket; /* the bracket to be shown at the main menu */
    u8 match;       /* the bracket's match in play */
    u8 began;       /* its mode has begun (fight_ApplyConfig) */
    u8 side_entrant[2];
    /* each side's fighter and weapon in the match (a CPU's drawn as it
     * begins), and the stage of a match of two CPUs */
    u8 fighter[2];
    u8 weapon[2];
    u8 stage;
    u8 decided; /* the bracket's matches decided so far */
    u16 number; /* the tournaments begun in this run, for the record */
    u32 dice;
    PortTournamentSetup setup;
    PortBracket bracket;
    /* what each entrant last played, for the window */
    u8 last_fighter[PORT_BRACKET_MOST];
    u8 last_weapon[PORT_BRACKET_MOST];
    /* the bracket's matches decided by the judges, a bit each */
    u16 judged;
    /* the window's asks taken in so far, each kind's count */
    u16 taken[PORT_TOURNAMENT_ASKS];
    /* a room's tournament's match, played as a session (the form below):
     * no bracket of its own shown, no ask of the window's taken */
    u8 online;
} PortTournament;

/* A room's tournament's match in a session (docs/NETPLAY.md, section 22;
 * src/port/room_tournament.c runs the bracket, outside the state): the
 * match alone, as a tournament of two in the state of both machines, put
 * in as the session begins (port_ApplySession) from the host's welcome.
 * Its first entrant is the session's host's, its second the guest's; each
 * is the pad of the program that plays it (0 the host's, 1 the guest's)
 * or the CPU, at a level, with a fighter and weapon set or drawn by
 * `dice`. So the game's own tournament seats the sides (a player first
 * where the other is the CPU), picks for the CPU at the select screen,
 * judges two CPUs after three minutes, and both machines compute it. */
typedef struct PortTournamentForm {
    u8 on;
    u8 rule;       /* PORT_MATCH_FIRST_TO or PORT_MATCH_STOCK */
    u8 rule_count; /* its number */
    u8 pad[2];     /* 0, 1, or PORT_TOURNAMENT_CPU */
    u8 cpu[2];
    u8 fighter[2];
    u8 weapon[2];
    u32 dice;
} PortTournamentForm;

/* A fight of two CPUs that has gone on three minutes of the game's time
 * (thirty frames a second) with neither fallen is judged: the one less
 * wounded wins the match, or the tournament's dice where they are alike.
 * In a room's match (online) a fight of a player against the CPU too. */
#define PORT_TOURNAMENT_JUDGE_FRAMES 5400

extern PortTournament g_port_tournament;

/* ---- what the game calls (under RB_PORT) ---- */

extern s32 port_TournamentIsOn(void);
/* A match of the tournament is in play: its select screen or its fights. */
extern s32 port_TournamentIsPlaying(void);
/* A frame of the main menu's top page: the bracket asked for where it is
 * to be shown, and the match asked for begun (PORT_TOURNAMENT_...). */
extern s32 port_TournamentTopMenu(void);
/* For a match of two CPUs: each side's character and weapon, and the
 * stage, as the menus give them to the fight (menu_screen.c). */
extern void port_TournamentFightPicks(u8 *fighters, u8 *weapons, u8 *stage);
/* A mode begins (fight_ApplyConfig): each side the CPU's or a player's. */
extern void port_TournamentApplyConfig(void);
/* The match's rule and number, where a tournament's match is in play. */
extern s32 port_TournamentRule(s32 *rule, s32 *count);
/* The CPU's level of a place, where a tournament's CPU plays it. */
extern s32 port_TournamentCpuLevel(s32 place, s32 *level);
/* A side's pick on the select screen where the CPU plays it: its
 * character and weapon. FALSE for a player's side, or no tournament. */
extern s32 port_TournamentCpuPick(s32 side, s32 *fighter, s32 *weapon);
/* The match decided (port_MatchAfterFight), `winner` its side: the
 * bracket told, and back to the main menu. FALSE where it is no
 * tournament's. */
extern s32 port_TournamentMatchOver(s32 winner);
/* A frame of a fight of versus (fight_UpdateMode): a fight of two CPUs
 * past PORT_TOURNAMENT_JUDGE_FRAMES judged, and back to the main menu.
 * TRUE where it was. */
extern s32 port_TournamentJudge(void);
/* A mode chosen on the main menu: a tournament there is left. */
extern void port_TournamentModeChosen(void);
/* The input the game is about to take (pad.c, port_TakeInput): each
 * side's pad the pad of the entrant who plays it. */
extern void port_TournamentTakeInput(struct PortInput *input);

/* At the host's boundary (tournament_ask.c): a tournament begun from its
 * setup (its seeds, its bracket, the bracket shown at the main menu), and
 * one over or left (nothing of it the game's any more; the select
 * screen's rule the window's again). */
extern void port_TournamentBegin(const PortTournamentSetup *setup);
extern void port_TournamentEnd(void);

/* ---- a room's tournament's match in a session (session.c) ---- */

/* The match of `form` put in the state as the session begins: its two
 * entrants, its bracket of one match, asked for at the main menu. */
extern void port_TournamentSessionBegin(const PortTournamentForm *form);
/* The entrant (0 the session host's, 1 the guest's) of the game's side
 * `side`: the sides are seated a player first. `side` itself where no
 * room's match is in play. */
extern s32 port_TournamentSessionEntrant(s32 side);
/* Its match decided out of a fight (two CPUs judged): TRUE and the
 * entrant who won. */
extern s32 port_TournamentSessionDecided(s32 *entrant);

/* ---- the window's asks and the host's boundary (tournament_ask.c) ---- */

/* The fighters a CPU entrant may be set to (enum CharacterId): the select
 * screen's with blades. */
extern const u8 *port_TournamentFighters(s32 *count);
/* An ask of `kind` (PORT_TOURNAMENT_ASK_...) for this input or one before:
 * its count, to set against the state's; and the setup of the last
 * begin asked. */
extern s32 port_TournamentAsked(s32 kind, u16 *count);
extern const PortTournamentSetup *port_TournamentAskedSetup(void);

/* A tournament begun, the next match played, the tournament left: asked
 * now, done at the host's next boundary. */
extern void port_AskTournamentStart(const PortTournamentSetup *setup);
extern void port_AskTournamentPlay(void);
extern void port_AskTournamentLeave(void);
/* Between two steps (main.c): the log's lines, the record written when
 * it is over, a test's own play asked (--tournament-auto). */
extern void port_TournamentBoundary(void);
/* A test's (--tournament-auto N): the next match played N boundaries
 * after the bracket comes up at the main menu; and the program ended N
 * boundaries after it is over and kept (--tournament-quit). */
extern void port_SetTournamentAuto(s32 boundaries);
extern void port_SetTournamentQuit(void);
/* --tournament SPEC: a setup read from words (port_tournament.h's
 * port_TournamentParse), begun at the first boundary. FALSE, said in the
 * log, where it is not one. */
extern s32 port_AskTournamentWords(const char *spec);
/* The words of a setup, as --tournament takes them, read: FALSE where
 * they are not one. */
extern s32 port_TournamentParse(const char *spec, PortTournamentSetup *out);
/* The tournament's date as the record has it ("2026-11-27T08:07:01"),
 * where one is on; "" otherwise. */
extern const char *port_TournamentDate(void);
/* An entrant's name as shown: its own, or "Player N" / "CPU N". */
extern void port_TournamentName(
    const PortTournamentEntrant *entrant, s32 index, char *out, u32 room);

#endif /* PORT_TOURNAMENT_H */
