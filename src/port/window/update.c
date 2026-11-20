/* The PC package's updater (update.h; docs/PORT.md, "Updates").
 *
 * Beside the program it keeps update/:
 *
 *   state.txt            when it last asked, the version Later was chosen
 *                        for, the version that did not start
 *   download/            the zip as it comes
 *   staged/              its files, each held to the manifest
 *   ready.txt, ready.sig the manifest and its signature, once staged/ is
 *                        whole: what the next start puts in place
 *   apply/               the copy of this program (and SDL2.dll) that
 *                        puts it in place
 *   old/                 the files it set aside, until the new program
 *                        has started
 *   apply.log            what that step did
 *
 * The check and the download run on a thread of their own, at a low
 * priority; the download waits while a fight is on. Nothing here is the
 * game's state, and none of it runs in a session of online play. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#include <winhttp.h>
#endif

#include "port_png.h"
#include "sdl_window.h"
#include "shared.h"
#include "update.h"

/* the release key built in (config/release_public_key.txt, written into
 * the build by tools/port_sdl_build.py): its hex, or nothing */
extern const char g_port_update_key[];

#define REPOSITORY_API "https://api.github.com/repos/tgg12321/ReBushido"
#define MANIFEST_NAME "update-manifest.txt"
#define SIGNATURE_NAME "update-manifest.txt.sig"
#define DAY_SECONDS (24 * 60 * 60)
#define FILES_MAX 32768
#define ANSWER_MAX (2 * 1024 * 1024)
#define MANIFEST_MAX (8 * 1024 * 1024)
#define SIGNATURE_MAX 1024
#define URL_BYTES 1024
#define LINE_BYTES 1024
/* how long the check waits on the network, and a download on a piece */
#define CHECK_TIMEOUT_MS 5000
#define DOWNLOAD_TIMEOUT_MS 30000

enum { JOB_NONE, JOB_CHECK, JOB_DOWNLOAD };

static struct {
    s32 is_set_up;
    s32 is_active; /* asks anything at all */
    s32 is_test;   /* --update-url */
    s32 is_trial;  /* -dev */
    s32 has_key;
    u8 key[PORT_UPDATE_KEY_BYTES];
    char key_hex[PORT_UPDATE_KEY_BYTES * 2 + 1];
    char beside[PORT_UPDATE_PATH_BYTES * 2];
    char api[URL_BYTES];
    char own[PORT_UPDATE_TEXT_BYTES];
    /* update/state.txt */
    long long checked;
    char dismissed[PORT_UPDATE_TEXT_BYTES];
    char failed[PORT_UPDATE_TEXT_BYTES];
    /* what the check found: the manifest's text and signature, and the
     * zip's address */
    char *manifest_text;
    u32 manifest_size;
    u8 signature[PORT_UPDATE_SIGNATURE_BYTES];
    char zip_url[URL_BYTES];
    PortUpdateManifest manifest;
    /* the thread's */
    SDL_mutex *lock;
    s32 is_running;
    s32 job;
    s32 job_next;
    SDL_atomic_t in_fight;
    /* online play's pages or a session are up (port_UpdateSetOnline) */
    SDL_atomic_t online;
    PortUpdateView view;
} g_update;

/* ---- the view ---- */

/* A fight, or online play: nothing is downloaded, shown or installed. */
static s32 port_UpdateIsBusy(void) {
    return SDL_AtomicGet(&g_update.in_fight) || SDL_AtomicGet(&g_update.online);
}

static void port_UpdateLock(void) {
    if (g_update.lock != NULL) {
        SDL_LockMutex(g_update.lock);
    }
}

static void port_UpdateUnlock(void) {
    if (g_update.lock != NULL) {
        SDL_UnlockMutex(g_update.lock);
    }
}

/* The view changed: its status, and the text (NULL leaves it). */
static void port_UpdateSet(s32 status, const char *text) {
    port_UpdateLock();
    g_update.view.status = status;
    if (text != NULL) {
        snprintf(g_update.view.text, sizeof(g_update.view.text), "%s", text);
    }
    g_update.view.generation++;
    port_UpdateUnlock();
}

void port_UpdateGetView(PortUpdateView *view) {
    port_UpdateLock();
    *view = g_update.view;
    port_UpdateUnlock();
}

s32 port_UpdateNotice(char *first, char *second, u32 room, s32 *generation) {
    PortUpdateView view;

    port_UpdateGetView(&view);
    *generation = view.generation + SDL_AtomicGet(&g_update.online);
    first[0] = second[0] = 0;
    if (SDL_AtomicGet(&g_update.online)) {
        return FALSE; /* never over online play's pages */
    }
    if (view.rolled_back[0] != 0 && view.status != PORT_UPDATE_READY) {
        snprintf(first, room, "Version %s did not start", view.rolled_back);
        snprintf(second, room, "ReBushido %s was put back", view.own);
    } else if (view.status == PORT_UPDATE_AVAILABLE && !view.is_dismissed) {
        snprintf(first, room, "Version %s is available", view.version);
        snprintf(second, room, "Options, Updates: update now or later");
    } else if (view.status == PORT_UPDATE_DOWNLOADING) {
        snprintf(first, room, "Version %s: downloading, %d%%", view.version,
                 view.permille / 10);
    } else if (view.status == PORT_UPDATE_READY) {
        snprintf(first, room, "Version %s is downloaded", view.version);
        snprintf(second, room, "%s",
                 g_port_settings.updates == PORT_UPDATES_AUTO
                     ? "It is installed at the next start"
                     : "Options, Updates: restart to install it");
    } else if (view.status == PORT_UPDATE_TOO_NEW && !view.is_dismissed) {
        snprintf(first, room, "Version %s is available", view.version);
        snprintf(second, room, "Download it from the releases page");
    } else {
        return FALSE;
    }
    return TRUE;
}

/* ---- update/state.txt ---- */

static void port_UpdatePath(char *out, u32 room, const char *name) {
    snprintf(out, room, "%supdate/%s", g_update.beside, name);
}

#if defined(_WIN32)

/* UTF-8 as Windows' wide characters, `/` made `\`. */
static void port_Wide(const char *text, wchar_t *out, s32 room) {
    s32 i;

    if (MultiByteToWideChar(
            CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, out, room) == 0) {
        MultiByteToWideChar(CP_ACP, 0, text, -1, out, room);
    }
    for (i = 0; out[i] != 0; i++) {
        if (out[i] == L'/') {
            out[i] = L'\\';
        }
    }
}

static FILE *port_UpdateOpen(const char *path, const wchar_t *mode) {
    wchar_t wide[PORT_UPDATE_PATH_BYTES * 2];

    port_Wide(path, wide, PORT_UPDATE_PATH_BYTES * 2);
    return _wfopen(wide, mode);
}

/* Every folder of `path` up to its last part. */
static void port_UpdateMakeFolders(const wchar_t *path) {
    wchar_t folder[PORT_UPDATE_PATH_BYTES * 2];
    s32 i;

    for (i = 0; path[i] != 0 && i + 1 < PORT_UPDATE_PATH_BYTES * 2; i++) {
        folder[i] = path[i];
        if (path[i] == L'\\' && i > 2) {
            folder[i] = 0;
            CreateDirectoryW(folder, NULL);
            folder[i] = L'\\';
        }
    }
}

static void port_UpdateRemove(const char *path) {
    wchar_t wide[PORT_UPDATE_PATH_BYTES * 2];

    port_Wide(path, wide, PORT_UPDATE_PATH_BYTES * 2);
    DeleteFileW(wide);
}

/* A folder this made, and everything in it. */
static void port_UpdateRemoveTree(const wchar_t *folder) {
    wchar_t pattern[PORT_UPDATE_PATH_BYTES * 2];
    wchar_t path[PORT_UPDATE_PATH_BYTES * 2];
    WIN32_FIND_DATAW found;
    HANDLE find;

    _snwprintf(pattern, PORT_UPDATE_PATH_BYTES * 2, L"%ls\\*", folder);
    find = FindFirstFileW(pattern, &found);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(found.cFileName, L".") == 0 ||
                wcscmp(found.cFileName, L"..") == 0) {
                continue;
            }
            _snwprintf(path, PORT_UPDATE_PATH_BYTES * 2, L"%ls\\%ls", folder,
                       found.cFileName);
            if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                !(found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                port_UpdateRemoveTree(path);
            } else if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveDirectoryW(path); /* a link alone, never its folder */
            } else {
                SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(path);
            }
        } while (FindNextFileW(find, &found));
        FindClose(find);
    }
    RemoveDirectoryW(folder);
}

static void port_UpdateRemoveFolder(const char *name) {
    char path[PORT_UPDATE_PATH_BYTES * 2];
    wchar_t wide[PORT_UPDATE_PATH_BYTES * 2];

    port_UpdatePath(path, sizeof(path), name);
    port_Wide(path, wide, PORT_UPDATE_PATH_BYTES * 2);
    port_UpdateRemoveTree(wide);
}

#else

static FILE *port_UpdateOpen(const char *path, const char *mode) {
    return fopen(path, mode);
}

#endif

#if defined(_WIN32)
#define MODE(m) L##m
#else
#define MODE(m) m
#endif

/* A whole file into memory, at most `most` bytes: NULL if it is not
 * there or larger. */
static char *port_UpdateReadFile(const char *path, u32 most, u32 *size) {
    FILE *file = port_UpdateOpen(path, MODE("rb"));
    char *data;
    long length;

    if (file == NULL) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    length = ftell(file);
    fseek(file, 0, SEEK_SET);
    if (length < 0 || (unsigned long)length > most) {
        fclose(file);
        return NULL;
    }
    data = malloc((size_t)length + 1);
    if (data != NULL &&
        fread(data, 1, (size_t)length, file) != (size_t)length) {
        free(data);
        data = NULL;
    }
    fclose(file);
    if (data != NULL) {
        data[length] = 0;
        *size = (u32)length;
    }
    return data;
}

static s32 port_UpdateWriteFile(const char *path, const void *data, u32 size) {
    FILE *file;
    s32 ok;

#if defined(_WIN32)
    {
        wchar_t wide[PORT_UPDATE_PATH_BYTES * 2];

        port_Wide(path, wide, PORT_UPDATE_PATH_BYTES * 2);
        port_UpdateMakeFolders(wide);
    }
#endif
    file = port_UpdateOpen(path, MODE("wb"));

    if (file == NULL) {
        return FALSE;
    }
    ok = fwrite(data, 1, size, file) == size;
    ok = fclose(file) == 0 && ok;
    return ok;
}

static void port_UpdateReadState(void) {
    char path[PORT_UPDATE_PATH_BYTES * 2];
    char *text;
    char *line;
    u32 size;

    port_UpdatePath(path, sizeof(path), "state.txt");
    text = port_UpdateReadFile(path, 64 * 1024, &size);
    if (text == NULL) {
        return;
    }
    for (line = strtok(text, "\r\n"); line != NULL;
         line = strtok(NULL, "\r\n")) {
        if (strncmp(line, "checked = ", 10) == 0) {
            g_update.checked = atoll(line + 10);
        } else if (strncmp(line, "dismissed = ", 12) == 0) {
            snprintf(g_update.dismissed, sizeof(g_update.dismissed), "%s",
                     line + 12);
        } else if (strncmp(line, "failed = ", 9) == 0) {
            snprintf(g_update.failed, sizeof(g_update.failed), "%s", line + 9);
        }
    }
    free(text);
}

static void port_UpdateWriteState(void) {
    char path[PORT_UPDATE_PATH_BYTES * 2];
    char text[LINE_BYTES];

    snprintf(text, sizeof(text),
             "# the updater's (docs/PLAYING.md, \"Updates\")\n"
             "checked = %lld\ndismissed = %s\nfailed = %s\n",
             g_update.checked, g_update.dismissed, g_update.failed);
    port_UpdatePath(path, sizeof(path), "state.txt");
    if (!port_UpdateWriteFile(path, text, (u32)strlen(text))) {
        port_Note("update: %s cannot be written", path);
    }
}

/* ---- the manifest ---- */

/* `text` and `signature` (its hex) held to the release key and read into
 * g_update.manifest: FALSE and why if not. */
static s32 port_UpdateAccept(
    const char *text, u32 size, const char *signature, char *why, u32 room) {
    u8 bytes[PORT_UPDATE_SIGNATURE_BYTES];

    if (!port_UpdateReadHex(signature, bytes, PORT_UPDATE_SIGNATURE_BYTES)) {
        snprintf(why, room, "the signature is not one");
        return FALSE;
    }
    if (!port_UpdateCheckSignature(g_update.key, bytes, text, size)) {
        snprintf(why, room, "the signature is not the release key's: refused");
        return FALSE;
    }
    if (g_update.manifest.files == NULL) {
        g_update.manifest.files = malloc(sizeof(PortUpdateFile) * FILES_MAX);
        g_update.manifest.room =
            g_update.manifest.files != NULL ? FILES_MAX : 0;
    }
    if (!port_UpdateParseManifest(text, size, &g_update.manifest, why, room)) {
        return FALSE;
    }
    memcpy(g_update.signature, bytes, sizeof(bytes));
    return TRUE;
}

/* The view's version and notes from the manifest accepted. */
static void port_UpdateShowManifest(s32 status) {
    s32 i;

    port_UpdateLock();
    snprintf(g_update.view.version, sizeof(g_update.view.version), "%s",
             g_update.manifest.version);
    for (i = 0; i < g_update.manifest.note_count; i++) {
        snprintf(g_update.view.notes[i], PORT_UPDATE_TEXT_BYTES, "%s",
                 g_update.manifest.notes[i]);
    }
    g_update.view.note_count = g_update.manifest.note_count;
    g_update.view.is_dismissed =
        strcmp(g_update.dismissed, g_update.manifest.version) == 0;
    g_update.view.status = status;
    g_update.view.generation++;
    port_UpdateUnlock();
}

#if defined(_WIN32)

/* ---- the network (WinHTTP: the system's own TLS) ---- */

/* Where a download goes: memory (`data`, at most `most` bytes) or a file,
 * whose SHA-256 is worked out as it comes. */
typedef struct {
    char *data;
    u32 most;
    u32 got;
    FILE *file;
    PortSha256 sha;
    u32 expected; /* for how far it has come; 0 for not known */
} PortFetch;

static s32 port_UpdateFetch(const char *url, s32 is_api, s32 timeout,
                            PortFetch *into, char *why, u32 room) {
    static wchar_t wide[URL_BYTES];
    static wchar_t host[URL_BYTES];
    static u8 piece[64 * 1024];
    URL_COMPONENTSW parts;
    HINTERNET session;
    HINTERNET connection = NULL;
    HINTERNET request = NULL;
    DWORD status = 0;
    DWORD length = sizeof(status);
    s32 ok = FALSE;

    MultiByteToWideChar(CP_UTF8, 0, url, -1, wide, URL_BYTES);
    memset(&parts, 0, sizeof(parts));
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = (DWORD)-1;
    parts.dwUrlPathLength = (DWORD)-1;
    parts.dwExtraInfoLength = (DWORD)-1;
    parts.dwSchemeLength = (DWORD)-1;
    if (!WinHttpCrackUrl(wide, 0, 0, &parts) ||
        parts.dwHostNameLength >= URL_BYTES) {
        snprintf(why, room, "%s is not an address", url);
        return FALSE;
    }
    memcpy(host, parts.lpszHostName, parts.dwHostNameLength * sizeof(wchar_t));
    host[parts.dwHostNameLength] = 0;
    session =
        WinHttpOpen(L"ReBushido-updater/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == NULL) {
        session = WinHttpOpen(
            L"ReBushido-updater/1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
            WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    }
    if (session == NULL) {
        snprintf(why, room, "WinHTTP cannot be opened (error %lu)",
                 (unsigned long)GetLastError());
        return FALSE;
    }
    WinHttpSetTimeouts(
        session, CHECK_TIMEOUT_MS, CHECK_TIMEOUT_MS, CHECK_TIMEOUT_MS, timeout);
    connection = WinHttpConnect(session, host, parts.nPort, 0);
    if (connection != NULL) {
        request = WinHttpOpenRequest(
            connection, L"GET", parts.lpszUrlPath, NULL, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES,
            parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0);
    }
    if (request == NULL ||
        !WinHttpSendRequest(request,
                            is_api ? L"Accept: application/vnd.github+json\r\n"
                                   : L"Accept: application/octet-stream\r\n",
                            (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, NULL)) {
        DWORD error = GetLastError();

        snprintf(why, room, "%s",
                 error == ERROR_WINHTTP_TIMEOUT ? "no answer in time"
                 : error == ERROR_WINHTTP_CANNOT_CONNECT ||
                         error == ERROR_WINHTTP_NAME_NOT_RESOLVED
                     ? "no connection"
                     : "the request failed");
        port_Note("update: %s: %s (WinHTTP error %lu)", url, why,
                  (unsigned long)error);
        goto done;
    }
    WinHttpQueryHeaders(
        request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &length,
        WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        snprintf(why, room, "the answer was %lu", (unsigned long)status);
        port_Note("update: %s: %s", url, why);
        goto done;
    }
    for (;;) {
        DWORD read = 0;

        if (!WinHttpReadData(request, piece, sizeof(piece), &read)) {
            snprintf(why, room, "the download broke off (error %lu)",
                     (unsigned long)GetLastError());
            goto done;
        }
        if (read == 0) {
            break;
        }
        if (into->most - into->got < read) {
            snprintf(why, room, "more came than was asked for");
            goto done;
        }
        if (into->file != NULL) {
            if (fwrite(piece, 1, read, into->file) != read) {
                snprintf(why, room, "the download cannot be written");
                goto done;
            }
            port_Sha256Add(&into->sha, piece, read);
        } else {
            memcpy(into->data + into->got, piece, read);
        }
        into->got += read;
        if (into->expected > 0) {
            port_UpdateLock();
            g_update.view.permille =
                (s32)((unsigned long long)into->got * 1000 / into->expected);
            g_update.view.generation++;
            port_UpdateUnlock();
        }
        if (into->file != NULL && port_UpdateIsBusy()) {
            snprintf(why, room, "a fight or online play began");
            goto done;
        }
    }
    ok = TRUE;
done:
    if (request != NULL) {
        WinHttpCloseHandle(request);
    }
    if (connection != NULL) {
        WinHttpCloseHandle(connection);
    }
    WinHttpCloseHandle(session);
    return ok;
}

/* An answer of at most `most` bytes into memory, ended by a 0: NULL and
 * why if it does not come. */
static char *port_UpdateFetchText(
    const char *url, s32 is_api, u32 most, u32 *size, char *why, u32 room) {
    PortFetch into;

    memset(&into, 0, sizeof(into));
    into.data = malloc((size_t)most + 1);
    into.most = most;
    if (into.data == NULL) {
        snprintf(why, room, "no memory");
        return NULL;
    }
    if (!port_UpdateFetch(url, is_api, CHECK_TIMEOUT_MS, &into, why, room)) {
        free(into.data);
        return NULL;
    }
    into.data[into.got] = 0;
    *size = into.got;
    return into.data;
}

/* ---- the check ---- */

static void port_UpdateCheck(void) {
    char url[URL_BYTES];
    char manifest_url[URL_BYTES];
    char signature_url[URL_BYTES];
    char why[LINE_BYTES];
    char *answer;
    char *manifest;
    char *signature;
    u32 size;
    u32 manifest_size;
    u32 signature_size;

    port_UpdateSet(PORT_UPDATE_CHECKING, "");
    snprintf(url, sizeof(url), "%s%s", g_update.api,
             g_port_settings.update_channel == PORT_UPDATE_CHANNEL_BETA
                 ? "/releases?per_page=10"
                 : "/releases/latest");
    port_Note("update: asking %s", url);
    answer =
        port_UpdateFetchText(url, TRUE, ANSWER_MAX, &size, why, sizeof(why));
    if (answer == NULL) {
        port_Say("update: the releases cannot be asked: %s", why);
        port_UpdateSet(PORT_UPDATE_FAILED, why);
        return;
    }
    g_update.checked = (long long)time(NULL);
    port_UpdateWriteState();
    if (!port_UpdateFindAsset(
            answer, size, MANIFEST_NAME, manifest_url, sizeof(manifest_url)) ||
        !port_UpdateFindAsset(answer, size, SIGNATURE_NAME, signature_url,
                              sizeof(signature_url))) {
        free(answer);
        port_Say("update: the newest release has no manifest: nothing to "
                 "update to");
        port_UpdateSet(PORT_UPDATE_UP_TO_DATE, "");
        return;
    }
    manifest = port_UpdateFetchText(
        manifest_url, FALSE, MANIFEST_MAX, &manifest_size, why, sizeof(why));
    signature = manifest == NULL
                    ? NULL
                    : port_UpdateFetchText(signature_url, FALSE, SIGNATURE_MAX,
                                           &signature_size, why, sizeof(why));
    if (signature == NULL) {
        free(answer);
        free(manifest);
        port_Say("update: the manifest cannot be downloaded: %s", why);
        port_UpdateSet(PORT_UPDATE_FAILED, why);
        return;
    }
    if (!port_UpdateAccept(
            manifest, manifest_size, signature, why, sizeof(why))) {
        free(answer);
        free(manifest);
        free(signature);
        port_Say("update: the newest release's manifest is refused: %s", why);
        port_UpdateSet(PORT_UPDATE_FAILED, "the release is not signed by "
                                           "the release key: refused");
        return;
    }
    free(g_update.manifest_text);
    g_update.manifest_text = manifest;
    g_update.manifest_size = manifest_size;
    free(signature);
    if (!port_UpdateFindAsset(answer, size, g_update.manifest.zip,
                              g_update.zip_url, sizeof(g_update.zip_url))) {
        g_update.zip_url[0] = 0;
    }
    free(answer);
    if (port_UpdateCompareVersions(g_update.manifest.version, g_update.own) <=
        0) {
        port_Say("update: up to date: the newest release is %s, this is %s",
                 g_update.manifest.version, g_update.own);
        port_UpdateShowManifest(PORT_UPDATE_UP_TO_DATE);
        return;
    }
    if (g_update.manifest.updater > PORT_UPDATER_VERSION) {
        port_Say("update: %s is available, and needs updater %d (this is "
                 "%d): download it from the releases page",
                 g_update.manifest.version, g_update.manifest.updater,
                 PORT_UPDATER_VERSION);
        port_UpdateShowManifest(PORT_UPDATE_TOO_NEW);
        return;
    }
    port_Say("update: %s is available (this is %s)", g_update.manifest.version,
             g_update.own);
    port_UpdateShowManifest(PORT_UPDATE_AVAILABLE);
    if (g_port_settings.updates == PORT_UPDATES_AUTO &&
        strcmp(g_update.failed, g_update.manifest.version) != 0) {
        port_UpdateLock();
        if (g_update.job == JOB_NONE) {
            g_update.job = JOB_DOWNLOAD;
        }
        port_UpdateUnlock();
    }
}

/* ---- the download ---- */

static u32 port_Little16(const u8 *at) { return at[0] | ((u32)at[1] << 8); }

static u32 port_Little32(const u8 *at) {
    return at[0] | ((u32)at[1] << 8) | ((u32)at[2] << 16) | ((u32)at[3] << 24);
}

/* The manifest's file of this path, or -1. */
static s32 port_UpdateFindFile(const char *path) {
    s32 i;

    for (i = 0; i < g_update.manifest.file_count; i++) {
        if (strcmp(g_update.manifest.files[i].path, path) == 0) {
            return i;
        }
    }
    return -1;
}

/* The zip at `zip_path` into update/staged/, each file held to the
 * manifest: FALSE and why if a file is not the manifest's, one is
 * missing or one more is there. */
static s32 port_UpdateUnpack(const char *zip_path, char *why, u32 room) {
    char path[PORT_UPDATE_PATH_BYTES * 2];
    wchar_t wide[PORT_UPDATE_PATH_BYTES * 2];
    u8 tail[65557];
    u8 *directory = NULL;
    u8 *packed = NULL;
    u8 *file_bytes = NULL;
    u8 *found = NULL;
    FILE *zip = port_UpdateOpen(zip_path, L"rb");
    long size;
    long from;
    u32 tail_size;
    u32 entries = 0;
    u32 directory_size = 0;
    u32 directory_at = 0;
    u32 at;
    u32 i;
    s32 ok = FALSE;
    size_t folder_length = strlen(g_update.manifest.folder);

    if (zip == NULL) {
        snprintf(why, room, "the download cannot be read");
        return FALSE;
    }
    fseek(zip, 0, SEEK_END);
    size = ftell(zip);
    from = size > (long)sizeof(tail) ? size - (long)sizeof(tail) : 0;
    fseek(zip, from, SEEK_SET);
    tail_size = (u32)fread(tail, 1, sizeof(tail), zip);
    /* the end of the central directory, from the end back */
    for (at = tail_size >= 22 ? tail_size - 22 : 0; at + 22 <= tail_size;
         at--) {
        if (port_Little32(tail + at) == 0x06054b50) {
            entries = port_Little16(tail + at + 10);
            directory_size = port_Little32(tail + at + 12);
            directory_at = port_Little32(tail + at + 16);
            break;
        }
        if (at == 0) {
            break;
        }
    }
    if (entries == 0 || (long)directory_at + (long)directory_size > size) {
        snprintf(why, room, "the download is not a zip");
        goto done;
    }
    directory = malloc(directory_size);
    found = calloc((size_t)g_update.manifest.file_count, 1);
    if (directory == NULL || found == NULL) {
        snprintf(why, room, "no memory");
        goto done;
    }
    fseek(zip, (long)directory_at, SEEK_SET);
    if (fread(directory, 1, directory_size, zip) != directory_size) {
        snprintf(why, room, "the zip's directory cannot be read");
        goto done;
    }
    port_UpdateRemoveFolder("staged");
    for (i = 0, at = 0; i < entries; i++) {
        u8 local[30];
        u32 method;
        u32 packed_size;
        u32 file_size;
        u32 name_length;
        u32 local_at;
        u32 data_at;
        char name[PORT_UPDATE_PATH_BYTES + PORT_UPDATE_TEXT_BYTES];
        u8 hash[PORT_UPDATE_HASH_BYTES];
        s32 index;
        const PortUpdateFile *file;

        if (at + 46 > directory_size ||
            port_Little32(directory + at) != 0x02014b50) {
            snprintf(why, room, "the zip's directory is damaged");
            goto done;
        }
        method = port_Little16(directory + at + 10);
        packed_size = port_Little32(directory + at + 20);
        file_size = port_Little32(directory + at + 24);
        name_length = port_Little16(directory + at + 28);
        local_at = port_Little32(directory + at + 42);
        if ((port_Little16(directory + at + 8) & 1) != 0 ||
            name_length >= sizeof(name) ||
            at + 46 + name_length > directory_size) {
            snprintf(why, room, "the zip has a file it cannot read");
            goto done;
        }
        memcpy(name, directory + at + 46, name_length);
        name[name_length] = 0;
        at += 46 + name_length + port_Little16(directory + at + 30) +
              port_Little16(directory + at + 32);
        if (name_length > 0 && name[name_length - 1] == '/') {
            continue; /* a folder */
        }
        if (strncmp(name, g_update.manifest.folder, folder_length) != 0 ||
            name[folder_length] != '/') {
            snprintf(why, room, "%s is not in the package's folder", name);
            goto done;
        }
        index = port_UpdateFindFile(name + folder_length + 1);
        if (index < 0 || found[index]) {
            snprintf(why, room, "%s is not the manifest's", name);
            goto done;
        }
        file = &g_update.manifest.files[index];
        if (file_size != file->size || (method != 0 && method != 8) ||
            (method == 0 && packed_size != file_size)) {
            snprintf(why, room, "%s is not the manifest's size", file->path);
            goto done;
        }
        fseek(zip, (long)local_at, SEEK_SET);
        if (fread(local, 1, sizeof(local), zip) != sizeof(local) ||
            port_Little32(local) != 0x04034b50) {
            snprintf(why, room, "the zip is damaged at %s", file->path);
            goto done;
        }
        data_at = local_at + 30 + port_Little16(local + 26) +
                  port_Little16(local + 28);
        free(packed);
        free(file_bytes);
        packed = malloc(packed_size > 0 ? packed_size : 1);
        file_bytes = malloc(file_size > 0 ? file_size : 1);
        if (packed == NULL || file_bytes == NULL) {
            snprintf(why, room, "no memory for %s", file->path);
            goto done;
        }
        fseek(zip, (long)data_at, SEEK_SET);
        if (fread(packed, 1, packed_size, zip) != packed_size ||
            (method == 8 &&
             !port_Inflate(packed, packed_size, file_bytes, file_size))) {
            snprintf(why, room, "the zip is damaged at %s", file->path);
            goto done;
        }
        if (method == 0) {
            memcpy(file_bytes, packed, file_size);
        }
        port_Sha256(file_bytes, file_size, hash);
        if (memcmp(hash, file->hash, sizeof(hash)) != 0) {
            snprintf(why, room, "%s is not the manifest's: its SHA-256 differs",
                     file->path);
            goto done;
        }
        snprintf(path, sizeof(path), "%supdate/staged/%s", g_update.beside,
                 file->path);
        port_Wide(path, wide, PORT_UPDATE_PATH_BYTES * 2);
        port_UpdateMakeFolders(wide);
        if (!port_UpdateWriteFile(path, file_bytes, file_size)) {
            snprintf(why, room, "%s cannot be written", path);
            goto done;
        }
        found[index] = 1;
    }
    for (i = 0; i < (u32)g_update.manifest.file_count; i++) {
        if (!found[i]) {
            snprintf(why, room, "%s is missing from the zip",
                     g_update.manifest.files[i].path);
            goto done;
        }
    }
    ok = TRUE;
done:
    fclose(zip);
    free(directory);
    free(packed);
    free(file_bytes);
    free(found);
    return ok;
}

static void port_UpdateDownload(void) {
    char part[PORT_UPDATE_PATH_BYTES * 2];
    char path[PORT_UPDATE_PATH_BYTES * 2];
    char why[LINE_BYTES];
    char signature[PORT_UPDATE_SIGNATURE_BYTES * 2 + 2];
    wchar_t wide[PORT_UPDATE_PATH_BYTES * 2];
    u8 hash[PORT_UPDATE_HASH_BYTES];
    PortFetch into;
    s32 i;
    s32 ok;

    if (g_update.manifest_text == NULL || g_update.zip_url[0] == 0) {
        port_Say("update: the release has no zip of its manifest's name");
        port_UpdateSet(PORT_UPDATE_FAILED, "the release has no package");
        return;
    }
    /* not in a fight: it waits for the fight to end */
    while (port_UpdateIsBusy()) {
        SDL_Delay(500);
    }
    port_UpdateLock();
    g_update.view.status = PORT_UPDATE_DOWNLOADING;
    g_update.view.permille = 0;
    g_update.view.generation++;
    port_UpdateUnlock();
    snprintf(part, sizeof(part), "%supdate/download/%s.part", g_update.beside,
             g_update.manifest.zip);
    port_Wide(part, wide, PORT_UPDATE_PATH_BYTES * 2);
    port_UpdateMakeFolders(wide);
    memset(&into, 0, sizeof(into));
    into.file = port_UpdateOpen(part, L"wb");
    into.most = g_update.manifest.zip_size;
    into.expected = g_update.manifest.zip_size;
    if (into.file == NULL) {
        port_Say("update: %s cannot be written", part);
        port_UpdateSet(PORT_UPDATE_FAILED, "the download cannot be written");
        return;
    }
    port_Sha256Begin(&into.sha);
    port_Say("update: downloading %s", g_update.zip_url);
    ok = port_UpdateFetch(
        g_update.zip_url, FALSE, DOWNLOAD_TIMEOUT_MS, &into, why, sizeof(why));
    ok = fclose(into.file) == 0 && ok;
    if (ok && into.got != g_update.manifest.zip_size) {
        snprintf(why, sizeof(why), "it came short: %u bytes of %u", into.got,
                 g_update.manifest.zip_size);
        ok = FALSE;
    }
    if (ok) {
        port_Sha256End(&into.sha, hash);
        if (memcmp(hash, g_update.manifest.zip_hash, sizeof(hash)) != 0) {
            snprintf(
                why, sizeof(why), "its SHA-256 is not the manifest's: refused");
            ok = FALSE;
        }
    }
    if (ok) {
        port_Say(
            "update: %u bytes downloaded, the manifest's SHA-256", into.got);
        ok = port_UpdateUnpack(part, why, sizeof(why));
    }
    port_UpdateRemove(part);
    if (!ok) {
        port_UpdateRemoveFolder("staged");
        port_Say("update: the download of %s is refused: %s",
                 g_update.manifest.version, why);
        port_UpdateSet(PORT_UPDATE_FAILED, why);
        return;
    }
    for (i = 0; i < PORT_UPDATE_SIGNATURE_BYTES; i++) {
        snprintf(signature + i * 2, 3, "%02x", g_update.signature[i]);
    }
    signature[PORT_UPDATE_SIGNATURE_BYTES * 2] = '\n';
    port_UpdatePath(path, sizeof(path), "ready.sig");
    ok = port_UpdateWriteFile(path, signature, sizeof(signature) - 1);
    port_UpdatePath(path, sizeof(path), "ready.txt");
    ok = ok && port_UpdateWriteFile(
                   path, g_update.manifest_text, g_update.manifest_size);
    if (!ok) {
        port_UpdateSet(PORT_UPDATE_FAILED, "the update cannot be kept");
        return;
    }
    port_Say("update: %s is ready: every file is the manifest's; it is put "
             "in place at the next start",
             g_update.manifest.version);
    port_UpdateLock();
    g_update.view.permille = 1000;
    port_UpdateUnlock();
    port_UpdateSet(PORT_UPDATE_READY, "");
}

/* ---- the thread ---- */

static int SDLCALL port_UpdateThread(void *unused) {
    (void)unused;
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_LOW);
    for (;;) {
        s32 job;

        port_UpdateLock();
        job = g_update.job != JOB_NONE ? g_update.job : g_update.job_next;
        if (g_update.job == JOB_NONE) {
            g_update.job_next = JOB_NONE;
        }
        g_update.job = JOB_NONE;
        if (job == JOB_NONE) {
            g_update.is_running = FALSE;
            port_UpdateUnlock();
            return 0;
        }
        port_UpdateUnlock();
        if (job == JOB_CHECK) {
            port_UpdateCheck();
        } else {
            port_UpdateDownload();
        }
    }
}

static void port_UpdateStartJob(s32 job) {
    SDL_Thread *thread;

    if (!g_update.is_active || g_update.lock == NULL) {
        return;
    }
    port_UpdateLock();
    if (g_update.is_running) {
        if (g_update.job == JOB_NONE) {
            g_update.job = job;
        } else {
            g_update.job_next = job;
        }
        port_UpdateUnlock();
        return;
    }
    g_update.job = job;
    g_update.is_running = TRUE;
    port_UpdateUnlock();
    thread = SDL_CreateThread(port_UpdateThread, "update", NULL);
    if (thread == NULL) {
        port_UpdateLock();
        g_update.is_running = FALSE;
        g_update.job = JOB_NONE;
        port_UpdateUnlock();
        return;
    }
    SDL_DetachThread(thread);
}

/* ---- starting the step that puts it in place ---- */

/* The command line the program was started with, after its own name, and
 * without the words the updater gives (--update-started,
 * --update-rolled-back VERSION). */
static void port_UpdateTail(wchar_t *out, s32 room) {
    const wchar_t *line = GetCommandLineW();
    wchar_t **words;
    s32 count = 0;
    s32 used = 0;
    s32 i;

    out[0] = 0;
    words = CommandLineToArgvW(line, &count);
    if (words == NULL) {
        return;
    }
    for (i = 1; i < count; i++) {
        const wchar_t *word = words[i];
        s32 j;
        s32 needs_quotes = word[0] == 0 || wcspbrk(word, L" \t\"") != NULL;

        if (wcscmp(word, L"--update-started") == 0) {
            continue;
        }
        if (wcscmp(word, L"--update-rolled-back") == 0) {
            i++;
            continue;
        }
        if (used + (s32)wcslen(word) * 2 + 4 >= room) {
            break;
        }
        out[used++] = L' ';
        if (needs_quotes) {
            out[used++] = L'"';
        }
        for (j = 0; word[j] != 0; j++) {
            s32 slashes = 0;

            while (word[j] == L'\\') {
                slashes++;
                j++;
            }
            if (word[j] == 0) {
                /* before the closing quote, each backslash twice */
                while (slashes-- > 0) {
                    out[used++] = L'\\';
                    if (needs_quotes) {
                        out[used++] = L'\\';
                    }
                }
                break;
            }
            if (word[j] == L'"') {
                slashes = slashes * 2 + 1;
            }
            while (slashes-- > 0) {
                out[used++] = L'\\';
            }
            out[used++] = word[j];
        }
        if (needs_quotes) {
            out[used++] = L'"';
        }
        out[used] = 0;
    }
    LocalFree(words);
}

/* This program and SDL2.dll copied into update/apply/, and that copy
 * started to put update/staged/ in place once this program has ended:
 * TRUE if it was. */
static s32 port_UpdateStartApply(void) {
    static wchar_t command[32768];
    static wchar_t tail[30000];
    wchar_t own[PORT_UPDATE_PATH_BYTES * 2];
    wchar_t folder[PORT_UPDATE_PATH_BYTES * 2];
    wchar_t copy[PORT_UPDATE_PATH_BYTES * 2];
    wchar_t from[PORT_UPDATE_PATH_BYTES * 2];
    char path[PORT_UPDATE_PATH_BYTES * 2];
    STARTUPINFOW start;
    PROCESS_INFORMATION process;
    DWORD length = GetModuleFileNameW(NULL, own, PORT_UPDATE_PATH_BYTES * 2);

    if (length == 0 || length >= PORT_UPDATE_PATH_BYTES * 2) {
        return FALSE;
    }
    port_UpdatePath(path, sizeof(path), "apply/rebushido.exe");
    port_Wide(path, copy, PORT_UPDATE_PATH_BYTES * 2);
    port_UpdateMakeFolders(copy);
    port_Wide(g_update.beside, folder, PORT_UPDATE_PATH_BYTES * 2);
    _snwprintf(from, PORT_UPDATE_PATH_BYTES * 2, L"%lsSDL2.dll", folder);
    if (!CopyFileW(own, copy, FALSE)) {
        port_Say("update: the program cannot be copied into update/apply/ "
                 "(error %lu)",
                 (unsigned long)GetLastError());
        return FALSE;
    }
    port_UpdatePath(path, sizeof(path), "apply/SDL2.dll");
    port_Wide(path, copy, PORT_UPDATE_PATH_BYTES * 2);
    if (!CopyFileW(from, copy, FALSE)) {
        port_Say("update: SDL2.dll cannot be copied into update/apply/");
        return FALSE;
    }
    port_UpdatePath(path, sizeof(path), "apply/rebushido.exe");
    port_Wide(path, copy, PORT_UPDATE_PATH_BYTES * 2);
    port_UpdateTail(tail, sizeof(tail) / sizeof(tail[0]));
    _snwprintf(command, sizeof(command) / sizeof(command[0]),
               L"\"%ls\" --update-apply %lu %hs --%ls", copy,
               (unsigned long)GetCurrentProcessId(), g_update.key_hex, tail);
    memset(&start, 0, sizeof(start));
    start.cb = sizeof(start);
    if (!CreateProcessW(copy, command, NULL, NULL, FALSE, CREATE_NO_WINDOW,
                        NULL, folder, &start, &process)) {
        port_Say("update: the step that installs it does not start (error "
                 "%lu)",
                 (unsigned long)GetLastError());
        return FALSE;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    port_Say("update: the program ends, and %s is put in place by "
             "update/apply/rebushido.exe (update/apply.log)",
             g_update.manifest.version);
    return TRUE;
}

#endif /* _WIN32 */

/* ---- what the program asks ---- */

void port_UpdateSetUp(
    const char *beside, const char *url, const char *key, s32 is_active) {
    const char *hex = key != NULL ? key : g_port_update_key;

    memset(&g_update.view, 0, sizeof(g_update.view));
    snprintf(g_update.beside, sizeof(g_update.beside), "%s", beside);
    g_update.is_trial = port_UpdateOwnVersion(
        port_GetVersionText(), g_update.own, sizeof(g_update.own));
    snprintf(g_update.view.own, sizeof(g_update.view.own), "%s", g_update.own);
    g_update.is_test = url != NULL;
    snprintf(g_update.api, sizeof(g_update.api), "%s",
             url != NULL ? url : REPOSITORY_API);
    g_update.has_key =
        strlen(hex) == PORT_UPDATE_KEY_BYTES * 2 &&
        port_UpdateReadHex(hex, g_update.key, PORT_UPDATE_KEY_BYTES);
    if (g_update.has_key) {
        snprintf(g_update.key_hex, sizeof(g_update.key_hex), "%s", hex);
    }
    g_update.is_set_up = TRUE;
#if defined(_WIN32)
    g_update.is_active = is_active && g_update.has_key;
#else
    /* (what only the network's half calls) */
    (void)is_active;
    (void)port_UpdateAccept;
    (void)port_UpdateShowManifest;
    (void)port_UpdateSet;
    (void)port_UpdateIsBusy;
    g_update.is_active = FALSE;
#endif
    if (!g_update.is_active) {
        snprintf(g_update.view.text, sizeof(g_update.view.text), "%s",
#if defined(_WIN32)
                 !g_update.has_key ? "this build has no release key"
                                   : "not in a session of online play"
#else
                 "the Windows package updates itself; this build does not"
#endif
        );
        g_update.view.status = PORT_UPDATE_OFF;
        port_Note("update: off: %s", g_update.view.text);
        return;
    }
    g_update.lock = SDL_CreateMutex();
    SDL_AtomicSet(&g_update.in_fight, 0);
    g_update.view.status = PORT_UPDATE_IDLE;
    port_UpdateReadState();
    port_Note("update: %s%s; version %s, release key %.16s...; asks %s",
              g_port_settings.updates == PORT_UPDATES_AUTO  ? "auto"
              : g_port_settings.updates == PORT_UPDATES_ASK ? "ask"
                                                            : "off",
              g_update.is_trial ? " (a trial build)" : "", g_update.own,
              g_update.key_hex, g_update.api);
}

s32 port_UpdateAtStart(void) {
#if defined(_WIN32)
    char path[PORT_UPDATE_PATH_BYTES * 2];
    char why[LINE_BYTES];
    char *text;
    char *signature;
    u32 size;
    u32 signature_size;
    s32 ok;

    if (!g_update.is_active) {
        return FALSE;
    }
    port_UpdatePath(path, sizeof(path), "ready.txt");
    text = port_UpdateReadFile(path, MANIFEST_MAX, &size);
    if (text == NULL) {
        /* what the last update left: the copy that installed it (which
         * cannot take itself away) and the download's folder */
        port_UpdateRemoveFolder("apply");
        port_UpdateRemoveFolder("download");
        return FALSE;
    }
    port_UpdatePath(path, sizeof(path), "ready.sig");
    signature = port_UpdateReadFile(path, SIGNATURE_MAX, &signature_size);
    ok = signature != NULL &&
         port_UpdateAccept(text, size, signature, why, sizeof(why));
    if (signature == NULL) {
        snprintf(why, sizeof(why), "it has no signature");
    }
    free(signature);
    if (ok && port_UpdateCompareVersions(
                  g_update.manifest.version, g_update.own) <= 0) {
        snprintf(why, sizeof(why), "it is not newer than %s", g_update.own);
        ok = FALSE;
    }
    if (ok && strcmp(g_update.failed, g_update.manifest.version) == 0) {
        snprintf(why, sizeof(why), "it did not start before");
        ok = FALSE;
    }
    free(g_update.manifest_text);
    g_update.manifest_text = text;
    g_update.manifest_size = size;
    if (!ok) {
        port_Say("update: the update downloaded is not installed: %s", why);
        port_UpdatePath(path, sizeof(path), "ready.txt");
        port_UpdateRemove(path);
        port_UpdatePath(path, sizeof(path), "ready.sig");
        port_UpdateRemove(path);
        port_UpdateRemoveFolder("staged");
        return FALSE;
    }
    port_Say("update: %s was downloaded: it is put in place now",
             g_update.manifest.version);
    return port_UpdateStartApply();
#else
    return FALSE;
#endif
}

void port_UpdateSayStarted(void) {
    char path[PORT_UPDATE_PATH_BYTES * 2];
    char text[PORT_UPDATE_TEXT_BYTES + 8];

    snprintf(text, sizeof(text), "%s\n", g_update.own);
    port_UpdatePath(path, sizeof(path), "started.txt");
    if (!port_UpdateWriteFile(path, text, (u32)strlen(text))) {
        port_Note("update: %s cannot be written", path);
    }
    port_Say("update: started as %s after the update", g_update.own);
}

void port_UpdateRolledBack(const char *version) {
    port_Say("update: %s did not start, and %s was put back "
             "(update/apply.log)",
             version, g_update.own);
    snprintf(g_update.view.rolled_back, sizeof(g_update.view.rolled_back), "%s",
             version);
    if (g_update.is_active) {
        snprintf(g_update.failed, sizeof(g_update.failed), "%s", version);
        port_UpdateWriteState();
    }
}

void port_UpdateBegin(void) {
    long long now = (long long)time(NULL);

    if (!g_update.is_active || g_port_settings.updates == PORT_UPDATES_OFF) {
        return;
    }
    if (g_update.is_trial && !g_update.is_test) {
        port_Note("update: a trial build asks only when told to (Updates, "
                  "Check for updates now)");
        return;
    }
    if (g_update.checked > 0 && now >= g_update.checked &&
        now - g_update.checked < DAY_SECONDS) {
        port_Note("update: asked %lld minutes ago; again after a day",
                  (now - g_update.checked) / 60);
        return;
    }
#if defined(_WIN32)
    port_UpdateStartJob(JOB_CHECK);
#endif
}

void port_UpdateSetOnline(s32 on) { SDL_AtomicSet(&g_update.online, on != 0); }

void port_UpdateWatch(s32 in_fight) {
    if (g_update.lock != NULL) {
        SDL_AtomicSet(&g_update.in_fight, in_fight != 0);
    }
}

void port_UpdateCheckNow(void) {
#if defined(_WIN32)
    port_UpdateStartJob(JOB_CHECK);
#endif
}

void port_UpdateDownloadNow(void) {
#if defined(_WIN32)
    PortUpdateView view;

    port_UpdateGetView(&view);
    if (view.status == PORT_UPDATE_AVAILABLE ||
        (view.status == PORT_UPDATE_FAILED && g_update.manifest_text != NULL &&
         port_UpdateCompareVersions(g_update.manifest.version, g_update.own) >
             0)) {
        port_UpdateStartJob(JOB_DOWNLOAD);
    }
#endif
}

void port_UpdateLater(void) {
    PortUpdateView view;

    port_UpdateGetView(&view);
    if (view.version[0] == 0 || !g_update.is_active) {
        return;
    }
    snprintf(
        g_update.dismissed, sizeof(g_update.dismissed), "%s", view.version);
    port_UpdateWriteState();
    port_UpdateLock();
    g_update.view.is_dismissed = TRUE;
    g_update.view.rolled_back[0] = 0;
    g_update.view.generation++;
    port_UpdateUnlock();
}

void port_UpdateRestartNow(void) {
#if defined(_WIN32)
    PortUpdateView view;

    port_UpdateGetView(&view);
    if (view.status != PORT_UPDATE_READY || SDL_AtomicGet(&g_update.online)) {
        return;
    }
    if (port_UpdateStartApply()) {
        port_Quit(PORT_EXIT_OK);
    }
    port_UpdateSet(PORT_UPDATE_FAILED, "the step that installs it does not "
                                       "start");
#endif
}
