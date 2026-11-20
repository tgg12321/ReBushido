#ifndef PORT_UPDATE_H
#define PORT_UPDATE_H

/* The PC package's updater (docs/PORT.md, "Updates"; docs/PLAYING.md,
 * "Updates"): the newest release of the repository asked for at a start,
 * at most once a day, its manifest held to the release key built in
 * (config/release_public_key.txt), its zip downloaded beside the program,
 * every file of it held to the manifest's SHA-256, and the files put in
 * place by a copy of the program that waits for this one to end, swaps
 * them, starts the new one and puts the old back if it does not start.
 * Only the build with a window has these files, and only the build for
 * Windows asks anything of the network. */

#include "common.h"

/* ---- what is worked out and nothing else (update_core.c) ---- */

/* The updater's own version: a release whose manifest asks for a later
 * one (`updater = N`) is shown and not installed. */
#define PORT_UPDATER_VERSION 1

#define PORT_UPDATE_HASH_BYTES 32
#define PORT_UPDATE_KEY_BYTES 32
#define PORT_UPDATE_SIGNATURE_BYTES 64
#define PORT_UPDATE_TEXT_BYTES 128
#define PORT_UPDATE_PATH_BYTES 260
#define PORT_UPDATE_NOTES_MAX 24

/* SHA-256, a piece at a time. */
typedef struct {
    u32 state[8];
    u8 block[64];
    u32 used;
    unsigned long long length;
} PortSha256;

extern void port_Sha256Begin(PortSha256 *sha);
extern void port_Sha256Add(PortSha256 *sha, const void *data, u32 size);
extern void port_Sha256End(PortSha256 *sha, u8 *out);
extern void port_Sha256(const void *data, u32 size, u8 *out);

/* `count` bytes of hex (either case) into `out`: FALSE if `text` is not
 * exactly that, a newline after it allowed. */
extern s32 port_UpdateReadHex(const char *text, u8 *out, s32 count);

/* -1, 0 or 1 as version `a` is before, the same as or after `b`, by
 * their numbers between the dots ("0.10" is after "0.9"); what follows
 * the numbers (-dev) is not counted. */
extern s32 port_UpdateCompareVersions(const char *a, const char *b);
/* The number of ReBushido's version text ("ReBushido 0.1-dev (abc)" is
 * "0.1") into `out`; whether the text says a trial's -dev. */
extern s32 port_UpdateOwnVersion(const char *text, char *out, u32 room);

/* One file of a release. */
typedef struct {
    char path[PORT_UPDATE_PATH_BYTES]; /* below the package's folder, `/` */
    u8 hash[PORT_UPDATE_HASH_BYTES];
    u32 size;
} PortUpdateFile;

/* A release's manifest (update-manifest.txt), as tools/release_publish.py
 * writes it. */
typedef struct {
    char version[PORT_UPDATE_TEXT_BYTES];
    char commit[PORT_UPDATE_TEXT_BYTES];
    char channel[PORT_UPDATE_TEXT_BYTES];
    s32 updater;
    char zip[PORT_UPDATE_TEXT_BYTES];
    u32 zip_size;
    u8 zip_hash[PORT_UPDATE_HASH_BYTES];
    char folder[PORT_UPDATE_TEXT_BYTES];
    char notes[PORT_UPDATE_NOTES_MAX][PORT_UPDATE_TEXT_BYTES];
    s32 note_count;
    PortUpdateFile *files; /* the caller's room for `room` of them */
    s32 room;
    s32 file_count;
} PortUpdateManifest;

/* The manifest's text read into `manifest` (its `files` and `room` set
 * beforehand): FALSE and why in `why` if it is not a manifest, a line is
 * not one it knows, a value is missing, or a file's path is not one a
 * package may have (port_UpdatePathIsOurs). */
extern s32 port_UpdateParseManifest(
    const char *text, u32 size, PortUpdateManifest *manifest, char *why,
    u32 why_room);
/* A file's line, `HASH SIZE PATH`, as package-files.txt has it and a
 * manifest's `file = ` lines after the `=`: FALSE if it is not one. */
extern s32 port_UpdateReadFileLine(const char *line, PortUpdateFile *file);
/* Whether `signature` (64 bytes) is the release key's of the `size`
 * bytes of `text` (Ed25519, RFC 8032; Monocypher's). */
extern s32 port_UpdateCheckSignature(
    const u8 *key, const u8 *signature, const void *text, u32 size);
/* Whether a path of a manifest is one a package may have: below the
 * folder, no `..`, no drive, and none of a player's own files (the
 * settings, the cards, the logs, the pictures, the disc, the updater's
 * own folder). */
extern s32 port_UpdatePathIsOurs(const char *path);
/* The address of the asset of a release whose name is `name` in the
 * GitHub API's answer `json` (`"browser_download_url": "..."`), the
 * first of them, into `out`: FALSE if there is none. */
extern s32 port_UpdateFindAsset(
    const char *json, u32 size, const char *name, char *out, u32 room);

/* ---- the updater (update.c) ---- */

/* What the updater is doing, for the settings menu and the top menu. */
enum {
    PORT_UPDATE_OFF = 0,     /* no key built in, a session, not Windows */
    PORT_UPDATE_IDLE,        /* not asked yet */
    PORT_UPDATE_CHECKING,    /* asking */
    PORT_UPDATE_UP_TO_DATE,  /* the newest release is this one, or older */
    PORT_UPDATE_AVAILABLE,   /* a newer one, not downloaded */
    PORT_UPDATE_DOWNLOADING, /* ... being downloaded and checked */
    PORT_UPDATE_READY,       /* ... downloaded: installed at a restart */
    PORT_UPDATE_TOO_NEW,     /* ... which needs a newer updater */
    PORT_UPDATE_FAILED,      /* the last try did not work: `text` says */
};

typedef struct {
    s32 status;                       /* PORT_UPDATE_... */
    s32 generation;                   /* one more at every change */
    s32 permille;                     /* of the download */
    s32 is_dismissed;                 /* Later was chosen for this version */
    char own[PORT_UPDATE_TEXT_BYTES]; /* this program's version */
    char version[PORT_UPDATE_TEXT_BYTES]; /* the newer one's */
    char text[PORT_UPDATE_TEXT_BYTES];    /* what went wrong, or a note */
    /* a version that was installed and did not start, so was taken out */
    char rolled_back[PORT_UPDATE_TEXT_BYTES];
    char notes[PORT_UPDATE_NOTES_MAX][PORT_UPDATE_TEXT_BYTES];
    s32 note_count;
} PortUpdateView;

/* Where the program is started as the step that puts an update in place
 * (`--update-apply PID KEY -- ...`, which only the program itself gives):
 * what it ends with; -1 where it was started otherwise. Called first of
 * all, before anything is set up. */
extern s32 port_UpdateApplyMain(void);

/* The updater set up, after the settings are read: `beside` the
 * program's folder with its last separator; `url` and `key` the test's
 * (--update-url, --update-key: NULL for the release's own, which a
 * release build alone has); `is_active` FALSE where nothing is to be
 * asked (a session of online play, a run of a test's). */
extern void port_UpdateSetUp(
    const char *beside, const char *url, const char *key, s32 is_active);
/* A downloaded update put in place, at the start: TRUE where the program
 * is to end now, the step that installs it having been started. */
extern s32 port_UpdateAtStart(void);
/* The program was started by that step (--update-started): it says it
 * has started, so that it is kept. */
extern void port_UpdateSayStarted(void);
/* That step put the old version back (--update-rolled-back VERSION). */
extern void port_UpdateRolledBack(const char *version);
/* The check in the background, if it is a day since the last. */
extern void port_UpdateBegin(void);
/* At each blank, by the window's thread: whether a fight is on, when
 * nothing is downloaded. */
extern void port_UpdateWatch(s32 in_fight);
/* Online play's pages or a session of it are up (on) or left (off): while
 * they are, nothing is downloaded, no notice is shown and nothing is put
 * in place. A program started into a session asks nothing at all. */
extern void port_UpdateSetOnline(s32 on);
extern void port_UpdateGetView(PortUpdateView *view);
/* The top menu's notice, two lines, where there is one to show; and a
 * number that changes when it does. */
extern s32 port_UpdateNotice(
    char *first, char *second, u32 room, s32 *generation);
/* The settings menu's: ask now; download the newer one; Later (the
 * notice is not shown again for this version); end the program and put
 * the downloaded one in place. */
extern void port_UpdateCheckNow(void);
extern void port_UpdateDownloadNow(void);
extern void port_UpdateLater(void);
extern void port_UpdateRestartNow(void);

#endif /* PORT_UPDATE_H */
