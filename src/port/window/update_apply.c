/* The step that puts a downloaded update in place (update.h,
 * port_UpdateApplyMain; docs/PORT.md, "Updates"). The program copies
 * itself and SDL2.dll into update/apply/ and starts the copy as
 *
 *     update\apply\rebushido.exe --update-apply PID KEY -- WORDS...
 *
 * and ends. The copy waits for process PID to end; holds update/ready.txt
 * to its signature and every file of update/staged/ to the manifest again;
 * sets aside into update/old/ each file of the program's folder the new
 * version changes (and each the old version had and the new has not,
 * where package-files.txt says so and the player has not changed it), and
 * moves the new one in, one file at a time; starts the new program with
 * WORDS and --update-started; and waits for it to say it has started
 * (update/started.txt). If anything fails, or the new program ends
 * without saying so, or says nothing in a minute, every file is put back
 * as it was, and the old program is started again with
 * --update-rolled-back VERSION. Nothing it does not name is touched: the
 * settings, the cards, the logs, the pictures, the disc and the mods made
 * from it, a player's own mods. What it did is in update/apply.log.
 *
 * It runs before SDL or anything of the game's is set up, and uses
 * neither. Only the build for Windows has it. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#include "sdl_window.h"
#include "update.h"

#if defined(_WIN32)

extern const char g_port_update_key[];

#define PATH_WIDE (PORT_UPDATE_PATH_BYTES * 2)
#define FILES_MAX 32768
/* how long it waits for the old program to end, and for the new to say
 * it has started */
#define WAIT_OLD_MS 60000
#define WAIT_NEW_MS 60000
#define MOVE_TRIES 30
#define MOVE_PAUSE_MS 100

enum {
    DONE_ASIDE, /* the program's folder's file moved into update/old/ */
    DONE_NEW,   /* update/staged/'s moved into the program's folder */
};

typedef struct {
    s32 kind;
    char path[PORT_UPDATE_PATH_BYTES];
} ApplyDone;

static struct {
    FILE *log;
    wchar_t install[PATH_WIDE]; /* the program's folder, `\` at its end */
    wchar_t tail[30000];        /* the words to start the program with */
    PortUpdateManifest manifest;
    PortUpdateFile *old_files; /* package-files.txt of the old version */
    s32 old_count;
    ApplyDone *done;
    s32 done_count;
} g_apply;

static void apply_Log(const char *format, ...)
    __attribute__((format(printf, 1, 2)));

static void apply_Log(const char *format, ...) {
    va_list args;

    if (g_apply.log == NULL) {
        return;
    }
    va_start(args, format);
    vfprintf(g_apply.log, format, args);
    va_end(args);
    fputc('\n', g_apply.log);
    fflush(g_apply.log);
}

/* The program's folder's `under` + `path` (`/` in it made `\`). */
static void apply_Path(wchar_t *out, const wchar_t *under, const char *path) {
    s32 used;
    s32 i;

    _snwprintf(out, PATH_WIDE, L"%ls%ls", g_apply.install, under);
    used = (s32)wcslen(out);
    MultiByteToWideChar(CP_UTF8, 0, path, -1, out + used, PATH_WIDE - used);
    for (i = 0; out[i] != 0; i++) {
        if (out[i] == L'/') {
            out[i] = L'\\';
        }
    }
}

static void apply_MakeFolders(const wchar_t *path) {
    wchar_t folder[PATH_WIDE];
    s32 i;

    for (i = 0; path[i] != 0 && i + 1 < PATH_WIDE; i++) {
        folder[i] = path[i];
        if (path[i] == L'\\' && i > 2) {
            folder[i] = 0;
            CreateDirectoryW(folder, NULL);
            folder[i] = L'\\';
        }
    }
}

/* A file moved, a few times over if the system has it open a moment
 * longer (a scanner of viruses, the old program's last handle). */
static s32 apply_Move(const wchar_t *from, const wchar_t *to) {
    s32 i;

    apply_MakeFolders(to);
    for (i = 0; i < MOVE_TRIES; i++) {
        if (MoveFileExW(
                from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return TRUE;
        }
        Sleep(MOVE_PAUSE_MS);
    }
    return FALSE;
}

static s32 apply_Exists(const wchar_t *path) {
    DWORD attributes = GetFileAttributesW(path);

    return attributes != INVALID_FILE_ATTRIBUTES &&
           !(attributes & FILE_ATTRIBUTE_DIRECTORY);
}

/* Whether the file at `path` is `size` bytes whose SHA-256 is `hash`. */
static s32 apply_IsFile(const wchar_t *path, const u8 *hash, u32 size) {
    static u8 piece[256 * 1024];
    PortSha256 sha;
    u8 got[PORT_UPDATE_HASH_BYTES];
    FILE *file = _wfopen(path, L"rb");
    unsigned long long total = 0;
    size_t read;

    if (file == NULL) {
        return FALSE;
    }
    port_Sha256Begin(&sha);
    while ((read = fread(piece, 1, sizeof(piece), file)) > 0) {
        port_Sha256Add(&sha, piece, (u32)read);
        total += read;
    }
    fclose(file);
    port_Sha256End(&sha, got);
    return total == size && memcmp(got, hash, sizeof(got)) == 0;
}

static char *apply_ReadText(const wchar_t *path, u32 most, u32 *size) {
    FILE *file = _wfopen(path, L"rb");
    char *data;
    long length;

    if (file == NULL) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    length = ftell(file);
    fseek(file, 0, SEEK_SET);
    data = length >= 0 && (unsigned long)length <= most
               ? malloc((size_t)length + 1)
               : NULL;
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

/* A folder this step made, and all in it (update.c's, again: this file
 * has none of update.c's). */
static void apply_RemoveTree(const wchar_t *folder) {
    wchar_t pattern[PATH_WIDE];
    wchar_t path[PATH_WIDE];
    WIN32_FIND_DATAW found;
    HANDLE find;

    _snwprintf(pattern, PATH_WIDE, L"%ls\\*", folder);
    find = FindFirstFileW(pattern, &found);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(found.cFileName, L".") == 0 ||
                wcscmp(found.cFileName, L"..") == 0) {
                continue;
            }
            _snwprintf(path, PATH_WIDE, L"%ls\\%ls", folder, found.cFileName);
            if ((found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
                !(found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
                apply_RemoveTree(path);
            } else if (found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                RemoveDirectoryW(path);
            } else {
                SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(path);
            }
        } while (FindNextFileW(find, &found));
        FindClose(find);
    }
    RemoveDirectoryW(folder);
}

static void apply_Remove(const wchar_t *under, const char *name) {
    wchar_t path[PATH_WIDE];

    apply_Path(path, under, name);
    DeleteFileW(path);
}

/* The program in the folder started with the words it had and `more`:
 * its process, or NULL. */
static HANDLE apply_Start(const wchar_t *more) {
    static wchar_t command[32768];
    wchar_t program[PATH_WIDE];
    STARTUPINFOW start;
    PROCESS_INFORMATION process;

    _snwprintf(program, PATH_WIDE, L"%lsrebushido.exe", g_apply.install);
    _snwprintf(command, sizeof(command) / sizeof(command[0]), L"\"%ls\"%ls%ls",
               program, g_apply.tail, more);
    memset(&start, 0, sizeof(start));
    start.cb = sizeof(start);
    if (!CreateProcessW(program, command, NULL, NULL, FALSE, 0, NULL,
                        g_apply.install, &start, &process)) {
        apply_Log("the program does not start (error %lu)",
                  (unsigned long)GetLastError());
        return NULL;
    }
    CloseHandle(process.hThread);
    return process.hProcess;
}

/* Every file as it was before, the last moved first. */
static void apply_Undo(void) {
    wchar_t here[PATH_WIDE];
    wchar_t there[PATH_WIDE];
    s32 i;

    for (i = g_apply.done_count - 1; i >= 0; i--) {
        const ApplyDone *done = &g_apply.done[i];

        apply_Path(here, L"", done->path);
        if (done->kind == DONE_NEW) {
            apply_Path(there, L"update\\staged\\", done->path);
        } else {
            apply_Path(there, L"update\\old\\", done->path);
        }
        /* the new file back into staged/, the old back in place */
        if (done->kind == DONE_NEW ? !apply_Move(here, there)
                                   : !apply_Move(there, here)) {
            apply_Log("put back: %s cannot be moved (error %lu)", done->path,
                      (unsigned long)GetLastError());
        }
    }
    apply_Log("put back: %d moves undone", g_apply.done_count);
    g_apply.done_count = 0;
}

static s32 apply_Did(s32 kind, const char *path) {
    ApplyDone *done = &g_apply.done[g_apply.done_count++];

    done->kind = kind;
    snprintf(done->path, sizeof(done->path), "%s", path);
    return TRUE;
}

/* The file `path` of the manifest moved in place: set aside first if
 * there is one and it is not already the new one. */
static s32 apply_PutInPlace(const PortUpdateFile *file) {
    wchar_t here[PATH_WIDE];
    wchar_t aside[PATH_WIDE];
    wchar_t staged[PATH_WIDE];

    apply_Path(here, L"", file->path);
    apply_Path(staged, L"update\\staged\\", file->path);
    if (apply_Exists(here)) {
        if (apply_IsFile(here, file->hash, file->size)) {
            return TRUE; /* the same in both versions */
        }
        apply_Path(aside, L"update\\old\\", file->path);
        if (!apply_Move(here, aside)) {
            apply_Log("%s cannot be set aside (error %lu)", file->path,
                      (unsigned long)GetLastError());
            return FALSE;
        }
        apply_Did(DONE_ASIDE, file->path);
    }
    if (!apply_Move(staged, here)) {
        apply_Log("%s cannot be moved in (error %lu)", file->path,
                  (unsigned long)GetLastError());
        return FALSE;
    }
    apply_Did(DONE_NEW, file->path);
    return TRUE;
}

/* package-files.txt of the version in the folder: what it had, so that
 * what the new one has not can go. Nothing if there is none. */
static void apply_ReadOldFiles(void) {
    wchar_t path[PATH_WIDE];
    char *text;
    char *line;
    u32 size;

    _snwprintf(path, PATH_WIDE, L"%lspackage-files.txt", g_apply.install);
    text = apply_ReadText(path, 16 * 1024 * 1024, &size);
    if (text == NULL) {
        apply_Log("no package-files.txt: nothing of the old version is "
                  "taken away");
        return;
    }
    g_apply.old_files = malloc(sizeof(PortUpdateFile) * FILES_MAX);
    for (line = strtok(text, "\r\n");
         line != NULL && g_apply.old_files != NULL &&
         g_apply.old_count < FILES_MAX;
         line = strtok(NULL, "\r\n")) {
        if (line[0] != '#' &&
            port_UpdateReadFileLine(
                line, &g_apply.old_files[g_apply.old_count])) {
            g_apply.old_count++;
        }
    }
    free(text);
}

/* The old version's file that the new has not, set aside where it is
 * what the old version had (the player did not change it). */
static s32 apply_TakeAway(const PortUpdateFile *file) {
    wchar_t here[PATH_WIDE];
    wchar_t aside[PATH_WIDE];
    s32 i;

    for (i = 0; i < g_apply.manifest.file_count; i++) {
        if (_stricmp(g_apply.manifest.files[i].path, file->path) == 0) {
            return TRUE;
        }
    }
    apply_Path(here, L"", file->path);
    if (!apply_IsFile(here, file->hash, file->size)) {
        return TRUE; /* not there, or the player's now */
    }
    apply_Path(aside, L"update\\old\\", file->path);
    if (!apply_Move(here, aside)) {
        apply_Log("%s cannot be taken away (error %lu)", file->path,
                  (unsigned long)GetLastError());
        return FALSE;
    }
    apply_Did(DONE_ASIDE, file->path);
    return TRUE;
}

/* The update's manifest and staged files held to the key: FALSE if
 * anything is not as it says. */
static s32 apply_Check(const u8 *key) {
    wchar_t path[PATH_WIDE];
    u8 signature[PORT_UPDATE_SIGNATURE_BYTES];
    char why[256];
    char *text;
    char *signature_text;
    u32 size;
    u32 signature_size;
    s32 ok;
    s32 i;

    _snwprintf(path, PATH_WIDE, L"%lsupdate\\ready.txt", g_apply.install);
    text = apply_ReadText(path, 8 * 1024 * 1024, &size);
    _snwprintf(path, PATH_WIDE, L"%lsupdate\\ready.sig", g_apply.install);
    signature_text = apply_ReadText(path, 1024, &signature_size);
    ok = text != NULL && signature_text != NULL &&
         port_UpdateReadHex(
             signature_text, signature, PORT_UPDATE_SIGNATURE_BYTES) &&
         port_UpdateCheckSignature(key, signature, text, size);
    if (!ok) {
        apply_Log("the manifest is not signed by the release key: refused");
    }
    g_apply.manifest.files = malloc(sizeof(PortUpdateFile) * FILES_MAX);
    g_apply.manifest.room = g_apply.manifest.files != NULL ? FILES_MAX : 0;
    if (ok && !port_UpdateParseManifest(
                  text, size, &g_apply.manifest, why, sizeof(why))) {
        apply_Log("the manifest is refused: %s", why);
        ok = FALSE;
    }
    free(text);
    free(signature_text);
    for (i = 0; ok && i < g_apply.manifest.file_count; i++) {
        const PortUpdateFile *file = &g_apply.manifest.files[i];

        apply_Path(path, L"update\\staged\\", file->path);
        if (!apply_IsFile(path, file->hash, file->size)) {
            apply_Log(
                "update/staged/%s is not the manifest's: refused", file->path);
            ok = FALSE;
        }
    }
    return ok;
}

/* Waits for the new program to say it has started: TRUE if it did. */
static s32 apply_WaitStarted(HANDLE process) {
    wchar_t marker[PATH_WIDE];
    DWORD waited = 0;

    _snwprintf(marker, PATH_WIDE, L"%lsupdate\\started.txt", g_apply.install);
    while (waited < WAIT_NEW_MS) {
        if (apply_Exists(marker)) {
            return TRUE;
        }
        if (WaitForSingleObject(process, 100) == WAIT_OBJECT_0) {
            DWORD code = 0;

            if (apply_Exists(marker)) {
                return TRUE;
            }
            GetExitCodeProcess(process, &code);
            apply_Log("the new program ended (%lu) without starting",
                      (unsigned long)code);
            return FALSE;
        }
        waited += 100;
    }
    apply_Log("the new program did not say it started in %d seconds",
              WAIT_NEW_MS / 1000);
    TerminateProcess(process, 1);
    WaitForSingleObject(process, 5000);
    return FALSE;
}

/* Starts the old program again after the update was refused or put
 * back. */
static s32 apply_GiveUp(const char *version, s32 was_put_back) {
    wchar_t more[PORT_UPDATE_TEXT_BYTES + 32];
    HANDLE process;

    apply_Remove(L"update\\", "ready.txt");
    apply_Remove(L"update\\", "ready.sig");
    _snwprintf(more, sizeof(more) / sizeof(more[0]), L"%ls%hs",
               was_put_back ? L" --update-rolled-back " : L"",
               was_put_back ? version : "");
    process = apply_Start(more);
    apply_Log("the old program started again%s", process != NULL ? "" : ": no");
    if (process != NULL) {
        CloseHandle(process);
    }
    return 1;
}

s32 port_UpdateApplyMain(void) {
    const wchar_t *line = GetCommandLineW();
    wchar_t **words;
    wchar_t own[PATH_WIDE];
    wchar_t path[PATH_WIDE];
    char key_hex[PORT_UPDATE_KEY_BYTES * 2 + 8];
    u8 key[PORT_UPDATE_KEY_BYTES];
    const wchar_t *tail;
    HANDLE old;
    HANDLE process;
    DWORD pid;
    s32 count = 0;
    s32 cut = 0;
    s32 i;
    s32 ok = TRUE;
    char version[PORT_UPDATE_TEXT_BYTES];
    char own_version[PORT_UPDATE_TEXT_BYTES];
    s32 is_trial;

    words = CommandLineToArgvW(line, &count);
    if (words == NULL || count < 2 || wcscmp(words[1], L"--update-apply")) {
        if (words != NULL) {
            LocalFree(words);
        }
        return -1;
    }
    if (count < 5 || wcscmp(words[4], L"--") != 0) {
        LocalFree(words);
        return 2;
    }
    pid = (DWORD)wcstoul(words[2], NULL, 10);
    WideCharToMultiByte(
        CP_UTF8, 0, words[3], -1, key_hex, sizeof(key_hex), NULL, NULL);
    /* the words after ` -- `, as they were given */
    tail = wcsstr(line, L" --update-apply ");
    tail = tail != NULL ? wcsstr(tail + 16, words[3]) : NULL;
    tail = tail != NULL ? tail + wcslen(words[3]) : NULL;
    if (tail == NULL || wcsncmp(tail, L" --", 3) != 0) {
        LocalFree(words);
        return 2;
    }
    _snwprintf(g_apply.tail, sizeof(g_apply.tail) / sizeof(g_apply.tail[0]),
               L"%ls", tail + 3);
    LocalFree(words);
    /* the program's folder: two above this copy's, update\apply\ */
    GetModuleFileNameW(NULL, own, PATH_WIDE);
    for (i = (s32)wcslen(own) - 1; i >= 0 && cut < 3; i--) {
        if (own[i] == L'\\') {
            cut++;
            if (cut == 3) {
                own[i + 1] = 0;
            }
        }
    }
    if (cut != 3) {
        return 2;
    }
    _snwprintf(g_apply.install, PATH_WIDE, L"%ls", own);
    _snwprintf(path, PATH_WIDE, L"%lsupdate\\apply.log", g_apply.install);
    g_apply.log = _wfopen(path, L"ab");
    is_trial = port_UpdateOwnVersion(
        port_GetVersionText(), own_version, sizeof(own_version));
    apply_Log("---- %s puts an update in place, once process %lu has ended",
              port_GetVersionText(), (unsigned long)pid);
    /* a release's own key, whatever it is told; a trial's (a test's) as
     * it is told */
    if (!is_trial || strlen(key_hex) != PORT_UPDATE_KEY_BYTES * 2) {
        snprintf(key_hex, sizeof(key_hex), "%s", g_port_update_key);
    }
    if (!port_UpdateReadHex(key_hex, key, PORT_UPDATE_KEY_BYTES)) {
        apply_Log("no release key: nothing is put in place");
        return apply_GiveUp("", FALSE);
    }
    old = OpenProcess(SYNCHRONIZE, FALSE, pid);
    if (old != NULL) {
        if (WaitForSingleObject(old, WAIT_OLD_MS) != WAIT_OBJECT_0) {
            apply_Log("process %lu did not end: nothing is put in place",
                      (unsigned long)pid);
            CloseHandle(old);
            return 1;
        }
        CloseHandle(old);
    }
    if (!apply_Check(key)) {
        _snwprintf(path, PATH_WIDE, L"%lsupdate\\staged", g_apply.install);
        apply_RemoveTree(path);
        return apply_GiveUp("", FALSE);
    }
    snprintf(version, sizeof(version), "%s", g_apply.manifest.version);
    apply_Log("version %s: %d files, held to the manifest and its signature",
              version, g_apply.manifest.file_count);
    apply_ReadOldFiles();
    g_apply.done = malloc(
        sizeof(ApplyDone) *
        (size_t)(g_apply.manifest.file_count * 2 + g_apply.old_count + 1));
    if (g_apply.done == NULL) {
        apply_Log("no memory");
        return apply_GiveUp("", FALSE);
    }
    _snwprintf(path, PATH_WIDE, L"%lsupdate\\old", g_apply.install);
    apply_RemoveTree(path);
    for (i = 0; ok && i < g_apply.manifest.file_count; i++) {
        ok = apply_PutInPlace(&g_apply.manifest.files[i]);
    }
    for (i = 0; ok && i < g_apply.old_count; i++) {
        ok = apply_TakeAway(&g_apply.old_files[i]);
    }
    if (!ok) {
        apply_Undo();
        return apply_GiveUp(version, TRUE);
    }
    apply_Log("%d files moved; the new program is started", g_apply.done_count);
    apply_Remove(L"update\\", "started.txt");
    process = apply_Start(L" --update-started");
    if (process == NULL || !apply_WaitStarted(process)) {
        if (process != NULL) {
            CloseHandle(process);
        }
        apply_Undo();
        return apply_GiveUp(version, TRUE);
    }
    CloseHandle(process);
    apply_Log("version %s started: kept", version);
    apply_Remove(L"update\\", "ready.txt");
    apply_Remove(L"update\\", "ready.sig");
    apply_Remove(L"update\\", "started.txt");
    _snwprintf(path, PATH_WIDE, L"%lsupdate\\old", g_apply.install);
    apply_RemoveTree(path);
    _snwprintf(path, PATH_WIDE, L"%lsupdate\\staged", g_apply.install);
    apply_RemoveTree(path);
    if (g_apply.log != NULL) {
        fclose(g_apply.log);
    }
    return 0;
}

#else

s32 port_UpdateApplyMain(void) { return -1; }

#endif
