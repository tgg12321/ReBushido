/* ReBushidoPatcher: makes the ReBushido disc image out of the player's own
 * image of the retail disc and the patch of a release (docs/REBUSHIDO.md,
 * "Applying the patch").
 *
 *     ReBushidoPatcher [--patch X.bps] [--out Y.bin] [--yes] image.bin
 *
 * On Windows a player starts it with no argument, or drops the image on
 * it: it asks for the image in the usual "open file" window and says how
 * it went in a message box. Given its arguments in a shell it asks
 * nothing, on any system, and ends with one of the codes below.
 *
 * One file of C99 and the standard library, with the file names, the
 * folder's listing and (on Windows) the two windows from the system. `make
 * patcher` builds it (the Makefile has the two commands).
 *
 * The patch is a BPS file. tools/rebushido_patch.py describes the format
 * at its top and is the reference: this applies a patch as that does, and
 * tests/rebushido/patcher.py holds the two to the same results. Nothing
 * here is taken from another patcher.
 *
 * What it holds in memory: the patch (under 200 KB for ReBushido) and one
 * block of CHUNK bytes. The image is read from its file and the result's
 * earlier bytes are read back from its own file, so an image of any size
 * takes no more than that; every size and place is 64 bits wide.
 *
 * The order it works in: the patch's own checksum, the image's size and
 * checksum, and only then a file is written, under a temporary name
 * (<out>.part); the result's checksum is compared before that gets its
 * name. The image is only ever opened for reading.
 */
#if defined(_WIN32)
/* C99's printf from mingw's own library, not the system's older one. */
#define __USE_MINGW_ANSI_STDIO 1
#else
#define _FILE_OFFSET_BITS 64
#if !defined(__APPLE__)
#define _XOPEN_SOURCE 700
#endif
#endif

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#else
#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
#endif

/* How the program ends. */
enum {
    END_OK = 0,
    END_FAILED = 1,    /* a file cannot be read or written; the disk is full */
    END_USAGE = 2,     /* the arguments */
    END_PATCH = 3,     /* the patch is missing, damaged or wrong */
    END_IMAGE = 4,     /* the image is not the one the patch is for */
    END_EXISTS = 5,    /* the result is already there and --yes was not given */
    END_CANCELLED = 6, /* the player closed a window without choosing */
};

enum { SOURCE_READ, TARGET_READ, SOURCE_COPY, TARGET_COPY };

#define CHUNK ((size_t)1 << 20)
#define FOOTER 12
#define MESSAGE_MOST 8192
/* A number of the patch may not go much past this (number() stops at the
 * byte that would): sizes and places stay far inside 64 bits, and their
 * sums cannot wrap. */
#define NUMBER_MOST ((uint64_t)1 << 56)

/* What a wrong image is told about; the Python tool says the same. */
#define DISC "Bushido Blade 2, the North American disc (SLUS-00663)"
#define PROGRAM "SLUS_006.63"
#define RAW_SECTOR 2352
#define DATA_SECTOR 2048
#define PROGRAM_SEARCH ((size_t)4 << 20)
#define CUE_MOST 65536

static const unsigned char SYNC[12] = {
    0x00, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x00};

static const char USAGE[] =
    "ReBushidoPatcher [--patch X.bps] [--out Y.bin] [--yes] image.bin\n"
    "\n"
    "Makes the ReBushido disc image from your own image of " DISC ".\n"
    "\n"
    "  image.bin    your image of the disc (or its .cue); it is only read\n"
    "  --patch X    the patch; without it, the one ReBushido-*.bps file\n"
    "               in this program's folder\n"
    "  --out Y      the image to write, its .cue beside it; without it,\n"
    "               ReBushido.bin in your image's folder\n"
    "  --yes        write over a result that is already there\n"
    "\n"
    "Ends with 0 when the image is made, 1 when a file cannot be read or\n"
    "written, 2 for wrong arguments, 3 when the patch is missing or\n"
    "damaged, 4 when the image is not the one the patch is for, and 5\n"
    "when the result is already there and --yes was not given.\n";

/* The result while it is being written, for stop() to take away. */
static FILE *g_result;
static char *g_part;
/* On Windows: started by a double click or a drop, so that what it has to
 * say goes in message boxes and a question may be asked. */
static int g_windowed;
/* A line of progress is on the screen and has no end yet. */
static int g_progress_open;

static void stop(int code, const char *format, ...);

/* -------------------------------------------------------------------------
 * Text
 * ---------------------------------------------------------------------- */
static void *memory(size_t size) {
    void *block = malloc(size ? size : 1);

    if (block == NULL) {
        stop(END_FAILED, "There is not enough memory.");
    }
    return block;
}

/* A new string, as printf would print it. */
static char *text_of(const char *format, ...) {
    va_list args;
    int length;
    char *text;

    va_start(args, format);
    length = vsnprintf(NULL, 0, format, args);
    va_end(args);
    text = memory((size_t)(length < 0 ? 0 : length) + 1);
    va_start(args, format);
    vsnprintf(text, (size_t)(length < 0 ? 0 : length) + 1, format, args);
    va_end(args);
    return text;
}

/* A number with its thousands apart: 701,714,496. `text` holds 32. */
static const char *grouped(uint64_t value, char *text) {
    char digits[32];
    int count = 0, at = 0;

    do {
        digits[count++] = (char)('0' + value % 10);
        value /= 10;
    } while (value);
    while (count) {
        text[at++] = digits[--count];
        if (count && count % 3 == 0) {
            text[at++] = ',';
        }
    }
    text[at] = '\0';
    return text;
}

static int same_letter(char a, char b) {
    if (a >= 'A' && a <= 'Z') {
        a = (char)(a + 'a' - 'A');
    }
    if (b >= 'A' && b <= 'Z') {
        b = (char)(b + 'a' - 'A');
    }
    return a == b;
}

/* Whether `text` begins with `start`, whatever the case of its letters. */
static int begins_with(const char *text, const char *start) {
    while (*start) {
        if (!same_letter(*text++, *start++)) {
            return 0;
        }
    }
    return 1;
}

static int ends_with(const char *text, const char *end) {
    size_t length = strlen(text), end_length = strlen(end);

    return length >= end_length && begins_with(text + length - end_length, end);
}

/* -------------------------------------------------------------------------
 * File names. They are UTF-8 here; Windows is given them as wide strings.
 * ---------------------------------------------------------------------- */
static int is_separator(char c) {
#if defined(_WIN32)
    return c == '\\' || c == '/' || c == ':';
#else
    return c == '/';
#endif
}

/* Where the file's own name begins in its path. */
static const char *name_of(const char *path) {
    const char *name = path;

    for (; *path; path++) {
        if (is_separator(*path)) {
            name = path + 1;
        }
    }
    return name;
}

/* The folder a path is in, with its last separator, or "" for none. */
static char *folder_of(const char *path) {
    size_t length = (size_t)(name_of(path) - path);
    char *folder = memory(length + 1);

    memcpy(folder, path, length);
    folder[length] = '\0';
    return folder;
}

/* The path with another extension: "a/b.bin", ".cue" -> "a/b.cue". */
static char *with_extension(const char *path, const char *extension) {
    const char *name = name_of(path);
    const char *dot = strrchr(name, '.');
    size_t length = dot && dot != name ? (size_t)(dot - path) : strlen(path);

    return text_of("%.*s%s", (int)length, path, extension);
}

#if defined(_WIN32)
static wchar_t *wide(const char *text) {
    int count = MultiByteToWideChar(CP_UTF8, 0, text, -1, NULL, 0);
    wchar_t *wide_text =
        memory((size_t)(count > 0 ? count : 1) * sizeof(wchar_t));

    wide_text[0] = L'\0';
    if (count > 0) {
        MultiByteToWideChar(CP_UTF8, 0, text, -1, wide_text, count);
    }
    return wide_text;
}

static char *narrow(const wchar_t *wide_text) {
    int count =
        WideCharToMultiByte(CP_UTF8, 0, wide_text, -1, NULL, 0, NULL, NULL);
    char *text = memory((size_t)(count > 0 ? count : 1));

    text[0] = '\0';
    if (count > 0) {
        WideCharToMultiByte(CP_UTF8, 0, wide_text, -1, text, count, NULL, NULL);
    }
    return text;
}
#endif

static FILE *file_open(const char *path, const char *mode) {
#if defined(_WIN32)
    wchar_t *wide_path = wide(path), *wide_mode = wide(mode);
    FILE *file = _wfopen(wide_path, wide_mode);

    free(wide_path);
    free(wide_mode);
    return file;
#else
    return fopen(path, mode);
#endif
}

static int file_seek(FILE *file, int64_t at) {
#if defined(_WIN32)
    return _fseeki64(file, at, SEEK_SET);
#else
    return fseeko(file, (off_t)at, SEEK_SET);
#endif
}

/* The file's size, or -1. Leaves the file at its end. */
static int64_t file_size(FILE *file) {
#if defined(_WIN32)
    return _fseeki64(file, 0, SEEK_END) ? -1 : (int64_t)_ftelli64(file);
#else
    return fseeko(file, 0, SEEK_END) ? -1 : (int64_t)ftello(file);
#endif
}

static int path_exists(const char *path) {
#if defined(_WIN32)
    wchar_t *wide_path = wide(path);
    int exists = GetFileAttributesW(wide_path) != INVALID_FILE_ATTRIBUTES;

    free(wide_path);
    return exists;
#else
    struct stat status;

    return stat(path, &status) == 0;
#endif
}

static void path_remove(const char *path) {
#if defined(_WIN32)
    wchar_t *wide_path = wide(path);

    DeleteFileW(wide_path);
    free(wide_path);
#else
    remove(path);
#endif
}

/* Gives a file its name, in place of one that has it. 0 when done. */
static int path_rename(const char *from, const char *to) {
#if defined(_WIN32)
    wchar_t *wide_from = wide(from), *wide_to = wide(to);
    int failed = !MoveFileExW(wide_from, wide_to, MOVEFILE_REPLACE_EXISTING);

    free(wide_from);
    free(wide_to);
    return failed;
#else
    return rename(from, to);
#endif
}

/* The whole path of a file that may not be there yet: its folder's, as the
 * system resolves it, and its name. */
static char *path_whole(const char *path) {
#if defined(_WIN32)
    wchar_t *wide_path = wide(path);
    DWORD count = GetFullPathNameW(wide_path, 0, NULL, NULL);
    wchar_t *whole = memory(((size_t)count + 1) * sizeof(wchar_t));
    char *text;

    whole[0] = L'\0';
    if (count == 0 ||
        GetFullPathNameW(wide_path, count + 1, whole, NULL) == 0) {
        free(whole);
        free(wide_path);
        return text_of("%s", path);
    }
    text = narrow(whole);
    free(whole);
    free(wide_path);
    return text;
#else
    char *folder = folder_of(path);
    char *resolved = realpath(folder[0] ? folder : ".", NULL);
    char *whole;

    if (resolved == NULL) {
        whole = text_of("%s", path);
    } else {
        whole = text_of("%s/%s", resolved, name_of(path));
    }
    free(resolved);
    free(folder);
    return whole;
#endif
}

/* Whether two paths are one file: by what the system says of the two when
 * both are there, and by their whole paths otherwise. */
static int path_same(const char *a, const char *b) {
    char *whole_a = path_whole(a), *whole_b = path_whole(b);
    int same;
#if defined(_WIN32)
    wchar_t *wide_a = wide(whole_a), *wide_b = wide(whole_b);
    DWORD share = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    HANDLE file_a = CreateFileW(wide_a, 0, share, NULL, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS, NULL);
    HANDLE file_b = CreateFileW(wide_b, 0, share, NULL, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS, NULL);
    BY_HANDLE_FILE_INFORMATION about_a, about_b;

    same = _wcsicmp(wide_a, wide_b) == 0;
    if (!same && file_a != INVALID_HANDLE_VALUE &&
        file_b != INVALID_HANDLE_VALUE &&
        GetFileInformationByHandle(file_a, &about_a) &&
        GetFileInformationByHandle(file_b, &about_b)) {
        same = about_a.dwVolumeSerialNumber == about_b.dwVolumeSerialNumber &&
               about_a.nFileIndexHigh == about_b.nFileIndexHigh &&
               about_a.nFileIndexLow == about_b.nFileIndexLow;
    }
    if (file_a != INVALID_HANDLE_VALUE) {
        CloseHandle(file_a);
    }
    if (file_b != INVALID_HANDLE_VALUE) {
        CloseHandle(file_b);
    }
    free(wide_a);
    free(wide_b);
#else
    struct stat status_a, status_b;

    same = strcmp(whole_a, whole_b) == 0;
    if (!same && stat(a, &status_a) == 0 && stat(b, &status_b) == 0) {
        same = status_a.st_dev == status_b.st_dev &&
               status_a.st_ino == status_b.st_ino;
    }
#endif
    free(whole_a);
    free(whole_b);
    return same;
}

/* The bytes free for a new file in a folder ("" is the current one), or
 * -1 when the system does not say. */
static int64_t folder_free(const char *folder) {
#if defined(_WIN32)
    wchar_t *wide_folder = wide(folder[0] ? folder : ".");
    ULARGE_INTEGER available;
    int known = GetDiskFreeSpaceExW(wide_folder, &available, NULL, NULL) != 0;

    free(wide_folder);
    return known && available.QuadPart < (uint64_t)INT64_MAX
               ? (int64_t)available.QuadPart
               : -1;
#else
    struct statvfs about;

    if (statvfs(folder[0] ? folder : ".", &about) != 0) {
        return -1;
    }
    return (int64_t)((uint64_t)about.f_bavail * (uint64_t)about.f_frsize);
#endif
}

/* The folder this program's own file is in, with its last separator. */
static char *program_folder(const char *argv0) {
#if defined(_WIN32)
    static wchar_t path[32768];
    DWORD count = GetModuleFileNameW(NULL, path, 32768);
    char *text, *folder;

    (void)argv0;
    if (count == 0 || count >= 32768) {
        return text_of("%s", "");
    }
    text = narrow(path);
    folder = folder_of(text);
    free(text);
    return folder;
#else
    static char path[4096];
    char *resolved = NULL;
#if defined(__APPLE__)
    uint32_t size = sizeof path;

    if (_NSGetExecutablePath(path, &size) == 0) {
        resolved = realpath(path, NULL);
    }
#else
    ssize_t count = readlink("/proc/self/exe", path, sizeof path - 1);

    if (count > 0) {
        path[count] = '\0';
        resolved = realpath(path, NULL);
    }
#endif
    if (resolved == NULL && argv0 != NULL && strchr(argv0, '/') != NULL) {
        resolved = realpath(argv0, NULL);
    }
    if (resolved != NULL) {
        char *folder = folder_of(resolved);

        free(resolved);
        return folder;
    }
    return text_of("%s", "");
#endif
}

/* Whether a file's name is a release's patch: ReBushido-<version>.bps. */
static int is_patch_name(const char *name) {
    return begins_with(name, "ReBushido-") && ends_with(name, ".bps");
}

/* How many patches a folder has, and the first one's name (a new string)
 * in *found. -1 when the folder cannot be listed. */
static int folder_patches(const char *folder, char **found) {
    int count = 0;
#if defined(_WIN32)
    char *pattern = text_of("%sReBushido-*.bps", folder);
    wchar_t *wide_pattern = wide(pattern);
    WIN32_FIND_DATAW entry;
    HANDLE listing = FindFirstFileW(wide_pattern, &entry);

    free(wide_pattern);
    free(pattern);
    if (listing == INVALID_HANDLE_VALUE) {
        return 0;
    }
    do {
        char *name = narrow(entry.cFileName);

        /* the pattern also takes ".bpsx": the system matches short names */
        if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) &&
            is_patch_name(name) && count++ == 0) {
            *found = name;
        } else {
            free(name);
        }
    } while (FindNextFileW(listing, &entry));
    FindClose(listing);
#else
    DIR *listing = opendir(folder[0] ? folder : ".");
    struct dirent *entry;

    if (listing == NULL) {
        return -1;
    }
    while ((entry = readdir(listing)) != NULL) {
        if (is_patch_name(entry->d_name) && count++ == 0) {
            *found = text_of("%s", entry->d_name);
        }
    }
    closedir(listing);
#endif
    return count;
}

/* -------------------------------------------------------------------------
 * What the player is told
 * ---------------------------------------------------------------------- */
/* Whether what is written there is seen at once: a terminal's window. */
static int is_screen(FILE *stream) {
#if defined(_WIN32)
    DWORD mode;

    return GetConsoleMode(GetStdHandle(stream == stderr ? STD_ERROR_HANDLE
                                                        : STD_OUTPUT_HANDLE),
                          &mode) != 0;
#else
    return isatty(fileno(stream));
#endif
}

static void text_out(FILE *stream, const char *text) {
#if defined(_WIN32)
    /* a console takes any language's letters only as wide characters */
    if (is_screen(stream)) {
        static wchar_t wide_text[MESSAGE_MOST];
        int count =
            MultiByteToWideChar(CP_UTF8, 0, text, -1, wide_text, MESSAGE_MOST);
        DWORD done;

        fflush(stream);
        if (count > 1) {
            WriteConsoleW(GetStdHandle(stream == stderr ? STD_ERROR_HANDLE
                                                        : STD_OUTPUT_HANDLE),
                          wide_text, (DWORD)(count - 1), &done, NULL);
        }
        return;
    }
#endif
    fputs(text, stream);
    fflush(stream);
}

static void say(const char *format, ...) {
    char text[MESSAGE_MOST];
    va_list args;

    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    text_out(stdout, text);
}

/* How far a long step is, as a line that is written over on a screen and
 * as nothing where the output is kept. */
static void progress(const char *what, uint64_t done, uint64_t total) {
    static int shown = -1;
    int percent = total ? (int)((double)done * 100.0 / (double)total) : 100;

    if (!is_screen(stdout)) {
        return;
    }
    if (!g_progress_open) {
        shown = -1;
    }
    if (percent != shown) {
        say("\r%s %3d%%", what, percent);
        shown = percent;
        g_progress_open = 1;
    }
}

static void progress_end(void) {
    if (g_progress_open) {
        say("\n");
        g_progress_open = 0;
    }
}

#if defined(_WIN32)
/* The windows have no owner: a console's own window may be one that is
 * never shown (Windows Terminal's), and a box owned by it can open behind
 * everything. */
static int message_box(const char *text, UINT kind) {
    wchar_t *wide_text = wide(text);
    int answer = MessageBoxW(
        NULL, wide_text, L"ReBushido", kind | MB_SETFOREGROUND | MB_TOPMOST);

    free(wide_text);
    return answer;
}
#endif

/* Ends the program with what went wrong. Whatever was being written is
 * taken away first: no half-made result is left behind. */
static void stop(int code, const char *format, ...) {
    char text[MESSAGE_MOST];
    va_list args;

    va_start(args, format);
    vsnprintf(text, sizeof text, format, args);
    va_end(args);
    if (g_result != NULL) {
        fclose(g_result);
        g_result = NULL;
    }
    if (g_part != NULL) {
        path_remove(g_part);
    }
    progress_end();
    text_out(stderr, "\nReBushidoPatcher: ");
    text_out(stderr, text);
    text_out(stderr, "\n");
#if defined(_WIN32)
    if (g_windowed && code != END_CANCELLED) {
        message_box(text, MB_OK | MB_ICONERROR);
    }
#endif
    exit(code);
}

/* A question with yes or no for an answer; asked only of a player at the
 * windows, never in a shell. */
static int ask(const char *text) {
#if defined(_WIN32)
    return message_box(text, MB_YESNO | MB_ICONQUESTION) == IDYES;
#else
    (void)text;
    return 0;
#endif
}

#if defined(_WIN32)
/* The player's image, chosen in the system's "open file" window; NULL when
 * the window was closed. */
static char *choose_image(void) {
    static wchar_t chosen[32768];
    OPENFILENAMEW window;

    memset(&window, 0, sizeof window);
    window.lStructSize = sizeof window;
    window.lpstrFilter =
        L"Disc images (*.bin, *.cue)\0*.bin;*.cue\0All files (*.*)\0*.*\0";
    window.lpstrFile = chosen;
    window.nMaxFile = 32768;
    window.lpstrTitle =
        L"Choose your Bushido Blade 2 disc image (.bin or .cue)";
    window.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST |
                   OFN_HIDEREADONLY | OFN_NOCHANGEDIR;
    chosen[0] = L'\0';
    return GetOpenFileNameW(&window) ? narrow(chosen) : NULL;
}

/* Where to write the result, chosen in the "save file" window, which asks
 * itself before a file that is there is taken; NULL when it was closed. */
static char *choose_result(void) {
    static wchar_t chosen[32768];
    OPENFILENAMEW window;

    memset(&window, 0, sizeof window);
    window.lStructSize = sizeof window;
    window.lpstrFilter = L"Disc image (*.bin)\0*.bin\0";
    window.lpstrFile = chosen;
    window.nMaxFile = 32768;
    window.lpstrTitle = L"Where to save the ReBushido disc image";
    window.lpstrDefExt = L"bin";
    window.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY |
                   OFN_NOCHANGEDIR | OFN_OVERWRITEPROMPT;
    wcscpy(chosen, L"ReBushido.bin");
    return GetSaveFileNameW(&window) ? narrow(chosen) : NULL;
}

/* Whether the console this runs in was opened for it alone, as it is for a
 * program started from the desktop: nobody will read it once this ends. */
static int own_console(void) {
    DWORD processes[2];

    return GetConsoleProcessList(processes, 2) == 1;
}
#endif

/* -------------------------------------------------------------------------
 * Checksums
 * ---------------------------------------------------------------------- */
static uint32_t g_crc_table[256];

static void crc_start(void) {
    uint32_t i, bit, value;

    for (i = 0; i < 256; i++) {
        value = i;
        for (bit = 0; bit < 8; bit++) {
            value = value & 1 ? (value >> 1) ^ 0xEDB88320u : value >> 1;
        }
        g_crc_table[i] = value;
    }
}

/* The CRC-32 of `crc`'s bytes and these after them; 0 starts one. */
static uint32_t crc_more(
    uint32_t crc, const unsigned char *bytes, size_t count) {
    crc = ~crc;
    while (count--) {
        crc = g_crc_table[(crc ^ *bytes++) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

/* -------------------------------------------------------------------------
 * The patch
 * ---------------------------------------------------------------------- */
typedef struct {
    const char *path;
    unsigned char *data;
    size_t size;
    uint64_t source_size, target_size;
    uint32_t source_crc, target_crc;
    size_t actions_at, actions_end; /* the actions: data[actions_at..end) */
} Patch;

static void patch_damaged(const Patch *patch, const char *how) {
    stop(END_PATCH,
         "The patch file is damaged (%s).\n\n%s\n\n"
         "Download the release again and use the .bps file that comes with it.",
         how, patch->path);
}

/* The number at data[*at], which moves past it: seven bits at a time, low
 * bits first, the last byte with its top bit set, and each byte after the
 * first counting one more than it says. */
static uint64_t number(const Patch *patch, size_t *at) {
    uint64_t value = 0, shift = 1;

    for (;;) {
        unsigned char byte;

        if (*at >= patch->actions_end) {
            patch_damaged(patch, "it ends in the middle of a number");
        }
        byte = patch->data[(*at)++];
        value += (uint64_t)(byte & 0x7F) * shift;
        if (byte & 0x80) {
            return value;
        }
        /* seven more bits would take the number past NUMBER_MOST */
        if (shift >= NUMBER_MOST >> 7) {
            patch_damaged(patch, "it has a number with no end");
        }
        shift <<= 7;
        value += shift;
    }
}

static uint32_t four_bytes(const unsigned char *bytes) {
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8 |
           (uint32_t)bytes[2] << 16 | (uint32_t)bytes[3] << 24;
}

/* Reads the patch's file whole and checks it as far as it can be without
 * the image: that it is a patch, its own checksum, and its header. */
static void patch_load(Patch *patch, const char *path) {
    FILE *file = file_open(path, "rb");
    int64_t size;
    size_t at;
    uint64_t metadata_size;

    patch->path = path;
    if (file == NULL) {
        stop(END_PATCH, "The patch file cannot be opened (%s).\n\n%s",
             strerror(errno), path);
    }
    size = file_size(file);
    if (size < 0 || (uint64_t)size > SIZE_MAX / 2 || file_seek(file, 0) != 0) {
        fclose(file);
        stop(END_PATCH, "The patch file cannot be read.\n\n%s", path);
    }
    patch->size = (size_t)size;
    patch->data = memory(patch->size);
    if (fread(patch->data, 1, patch->size, file) != patch->size) {
        fclose(file);
        stop(END_PATCH, "The patch file cannot be read.\n\n%s", path);
    }
    fclose(file);

    if (patch->size < 4 + 3 + FOOTER || memcmp(patch->data, "BPS1", 4) != 0) {
        stop(END_PATCH,
             "This file is not a ReBushido patch (not a BPS file).\n\n%s\n\n"
             "Use the .bps file that comes with the release.",
             path);
    }
    patch->actions_end = patch->size - FOOTER;
    patch->source_crc = four_bytes(patch->data + patch->actions_end);
    patch->target_crc = four_bytes(patch->data + patch->actions_end + 4);
    if (crc_more(0, patch->data, patch->size - 4) !=
        four_bytes(patch->data + patch->size - 4)) {
        patch_damaged(patch, "its own checksum does not hold");
    }
    at = 4;
    patch->source_size = number(patch, &at);
    patch->target_size = number(patch, &at);
    metadata_size = number(patch, &at);
    if (metadata_size > patch->actions_end - at) {
        patch_damaged(patch, "its metadata runs past its end");
    }
    patch->actions_at = at + (size_t)metadata_size;
}

/* -------------------------------------------------------------------------
 * Applying it
 * ---------------------------------------------------------------------- */
typedef struct {
    const Patch *patch;
    FILE *source;
    const char *source_path;
    int64_t source_place; /* where the source's file is, -1 if not known */
    FILE *out;
    const char *out_path;
    uint64_t written;
    uint32_t crc;
    unsigned char *block; /* CHUNK bytes */
} Job;

static void write_failed(const Job *job) {
    if (errno == ENOSPC) {
        stop(END_FAILED,
             "The disk is full: the image could not be written whole.\n\n%s\n\n"
             "Free some space, or save the result on another disk.",
             job->out_path);
    }
    stop(END_FAILED, "The image could not be written (%s).\n\n%s",
         strerror(errno), job->out_path);
}

/* Adds bytes to the end of the result. */
static void put(Job *job, const unsigned char *bytes, size_t count) {
    if (fwrite(bytes, 1, count, job->out) != count) {
        write_failed(job);
    }
    job->crc = crc_more(job->crc, bytes, count);
    job->written += count;
    progress("Writing the ReBushido image ...", job->written,
             job->patch->target_size);
}

/* `length` bytes of the source from `at`. */
static void from_source(Job *job, int64_t at, uint64_t length) {
    uint64_t size = job->patch->source_size;

    if (at < 0 || (uint64_t)at > size || length > size - (uint64_t)at) {
        patch_damaged(job->patch, "it reads outside the image");
    }
    if (job->source_place != at) {
        if (file_seek(job->source, at) != 0) {
            stop(END_FAILED, "Your image could not be read (%s).\n\n%s",
                 strerror(errno), job->source_path);
        }
        job->source_place = at;
    }
    while (length) {
        size_t count = length < CHUNK ? (size_t)length : CHUNK;

        if (fread(job->block, 1, count, job->source) != count) {
            stop(END_FAILED, "Your image could not be read.\n\n%s",
                 job->source_path);
        }
        job->source_place += (int64_t)count;
        put(job, job->block, count);
        length -= count;
    }
}

/* `length` bytes of the result itself from `at`, which is before its end.
 * The copy may run into what it writes: what is there then repeats. */
static void from_target(Job *job, int64_t at, uint64_t length) {
    while (length) {
        uint64_t there = job->written - (uint64_t)at;
        size_t have = there < CHUNK ? (size_t)there : CHUNK;
        size_t count = length < CHUNK ? (size_t)length : CHUNK;
        size_t i;

        /* reading after writing, and writing after reading, need the file
         * put at its place in between */
        if (fflush(job->out) != 0) {
            write_failed(job);
        }
        if (file_seek(job->out, at) != 0 ||
            fread(job->block, 1, have, job->out) != have ||
            file_seek(job->out, (int64_t)job->written) != 0) {
            stop(END_FAILED,
                 "The image being written could not be read back.\n\n%s",
                 job->out_path);
        }
        /* fewer than asked: they end where the result ends, and go on as
         * they began */
        for (i = have; i < count; i++) {
            job->block[i] = job->block[i - have];
        }
        put(job, job->block, count);
        at += (int64_t)count;
        length -= count;
    }
}

/* Writes the whole result and checks its size and checksum. */
static void apply(Job *job) {
    const Patch *patch = job->patch;
    size_t at = patch->actions_at;
    int64_t source_at = 0, target_at = 0; /* the two cursors of the copies */

    while (at < patch->actions_end) {
        uint64_t word = number(patch, &at);
        uint64_t length = (word >> 2) + 1;
        uint64_t move;

        if (length > patch->target_size - job->written) {
            patch_damaged(patch, "it writes more than it says the result has");
        }
        switch (word & 3) {
        case SOURCE_READ:
            from_source(job, (int64_t)job->written, length);
            break;
        case TARGET_READ:
            if (length > patch->actions_end - at) {
                patch_damaged(patch, "it ends in the middle of its bytes");
            }
            put(job, patch->data + at, (size_t)length);
            at += (size_t)length;
            break;
        case SOURCE_COPY:
            move = number(patch, &at);
            source_at +=
                move & 1 ? -(int64_t)(move >> 1) : (int64_t)(move >> 1);
            from_source(job, source_at, length);
            source_at += (int64_t)length;
            break;
        default: /* TARGET_COPY */
            move = number(patch, &at);
            target_at +=
                move & 1 ? -(int64_t)(move >> 1) : (int64_t)(move >> 1);
            if (target_at < 0 || (uint64_t)target_at >= job->written) {
                patch_damaged(
                    patch, "it copies from output it has not written");
            }
            from_target(job, target_at, length);
            target_at += (int64_t)length;
            break;
        }
    }
    progress_end();
    if (job->written != patch->target_size) {
        patch_damaged(patch, "it ends before the result is whole");
    }
    if (job->crc != patch->target_crc) {
        patch_damaged(patch, "the result's checksum does not hold");
    }
}

/* -------------------------------------------------------------------------
 * The image
 * ---------------------------------------------------------------------- */
/* The name of the program a PlayStation disc image boots ("SLUS_006.63")
 * into `name`, which holds 12, from the text `cdrom:\NAME` of its
 * SYSTEM.CNF; 0 when there is none. */
static int program_of(FILE *image, char *name) {
    unsigned char *start = malloc(PROGRAM_SEARCH);
    size_t count, i, k;
    int found = 0;

    if (start == NULL || file_seek(image, 0) != 0) {
        free(start);
        return 0;
    }
    count = fread(start, 1, PROGRAM_SEARCH, image);
    for (i = 0; !found && i + 6 + 1 + 11 <= count; i++) {
        const unsigned char *text = start + i + 6;

        if (memcmp(start + i, "cdrom:", 6) != 0) {
            continue;
        }
        if (*text == '\\') {
            text++;
        }
        /* four capitals, _, three digits, a dot, two digits */
        found = 1;
        for (k = 0; k < 11; k++) {
            unsigned char c = text[k];
            int fits = k < 4    ? c >= 'A' && c <= 'Z'
                       : k == 4 ? c == '_'
                       : k == 8 ? c == '.'
                                : c >= '0' && c <= '9';

            if (!fits) {
                found = 0;
                break;
            }
        }
        if (found) {
            memcpy(name, text, 11);
            name[11] = '\0';
        }
    }
    free(start);
    return found;
}

/* The CRC-32 of a whole file of `size` bytes, read from its start. */
static uint32_t image_crc(
    FILE *image, const char *path, uint64_t size, unsigned char *block) {
    uint32_t crc = 0;
    uint64_t done = 0;

    if (file_seek(image, 0) != 0) {
        stop(END_FAILED, "Your image could not be read (%s).\n\n%s",
             strerror(errno), path);
    }
    while (done < size) {
        size_t count = size - done < CHUNK ? (size_t)(size - done) : CHUNK;

        if (fread(block, 1, count, image) != count) {
            stop(END_FAILED, "Your image could not be read.\n\n%s", path);
        }
        crc = crc_more(crc, block, count);
        done += count;
        progress("Checking your image ...", done, size);
    }
    progress_end();
    return crc;
}

/* Ends the program unless the image is the one the patch is for: by its
 * size, then by its CRC-32. Says what it is instead where that shows. */
static void image_check(
    const Patch *patch, FILE *image, const char *path, unsigned char *block) {
    int64_t found_size = file_size(image);
    uint64_t size = (uint64_t)found_size;
    uint32_t crc = 0;
    int crc_known = 0;
    char program[12], expected[32], found[32], crc_text[32] = "";
    const char *why;

    if (found_size < 0) {
        stop(END_FAILED, "Your image could not be read (%s).\n\n%s",
             strerror(errno), path);
    }
    if (size == patch->source_size || size == patch->target_size) {
        crc = image_crc(image, path, size, block);
        crc_known = 1;
        snprintf(
            crc_text, sizeof crc_text, ", CRC32 %08lX", (unsigned long)crc);
    }
    if (size == patch->source_size && crc == patch->source_crc) {
        return;
    }
    if (crc_known && size == patch->target_size && crc == patch->target_crc) {
        stop(
            END_IMAGE,
            "This image is already patched: it is the ReBushido image itself.\n\n%s\n\n"
            "Choose your original image of the game instead.",
            path);
    }
    if (program_of(image, program) && strcmp(program, PROGRAM) != 0) {
        why = text_of(
            "Its program is %s: another release of the game, or another game.",
            program);
    } else if (size % RAW_SECTOR != 0 && size % DATA_SECTOR == 0) {
        why =
            "It looks like an .iso, with sectors of 2,048 bytes. The patch needs the disc as a .bin with whole "
            "sectors of 2,352.";
    } else if (size != patch->source_size && size % RAW_SECTOR == 0) {
        why =
            "It is a .bin of whole sectors, but not the same dump: more or fewer sectors at its end, or more "
            "than one track in the file.";
    } else if (size == patch->source_size) {
        why =
            "It has the right size, but some of its bytes are different: it may be an image that was already "
            "patched or changed, or a dump with errors.";
    } else {
        why =
            "It is not a disc image this patch knows: a compressed image (.chd, .pbp, .ecm, .zip) has to be "
            "turned back into a .bin first.";
    }
    stop(
        END_IMAGE,
        "This is not the disc image the patch is for.\n\n%s\n\n"
        "Expected: %s bytes, CRC32 %08lX\n"
        "Found: %s bytes%s\n\n"
        "%s\n\n"
        "The patch needs " DISC
        ", as a single-track .bin with its .cue, the usual dump of that disc. "
        "Another region's disc, an .iso or an image that was changed will not do.",
        path, grouped(patch->source_size, expected),
        (unsigned long)patch->source_crc, grouped(size, found), crc_text, why);
}

/* The .bin a .cue names: the file of its one FILE line, in the .cue's
 * folder, or the file of the .cue's own name with .bin. */
static char *bin_of_cue(const char *cue) {
    FILE *file = file_open(cue, "rb");
    char *text = memory(CUE_MOST + 1);
    char *folder = folder_of(cue), *named = NULL, *bin;
    char *line;
    size_t count;
    int files = 0, whole;

    if (file == NULL) {
        stop(END_FAILED, "The .cue file cannot be opened (%s).\n\n%s",
             strerror(errno), cue);
    }
    count = fread(text, 1, CUE_MOST, file);
    fclose(file);
    text[count] = '\0';
    for (line = text; line != NULL && *line; line = strchr(line, '\n')) {
        char *name, *end;

        line += strspn(line, " \t\r\n");
        if (!begins_with(line, "FILE") || (line[4] != ' ' && line[4] != '\t')) {
            continue;
        }
        if (files++ > 0) {
            continue;
        }
        name = line + 4 + strspn(line + 4, " \t");
        if (*name == '"') {
            end = strchr(++name, '"');
        } else {
            end = name + strcspn(name, " \t\r\n");
        }
        if (end != NULL && end > name) {
            named = text_of("%.*s", (int)(end - name), name);
        }
    }
    if (files > 1) {
        stop(
            END_IMAGE,
            "This disc image is in several files (the .cue names %d).\n\n%s\n\n"
            "The patch needs " DISC
            ", as a single-track .bin: one .bin file for the whole disc.",
            files, cue);
    }
    /* a whole path is taken as it is ("/x", "\x", "C:\x") */
    whole = named != NULL &&
            (is_separator(named[0]) || (named[0] && named[1] == ':'));
    bin = named != NULL ? text_of("%s%s", whole ? "" : folder, named) : NULL;
    if (bin == NULL || !path_exists(bin)) {
        free(bin);
        bin = with_extension(cue, ".bin");
    }
    if (!path_exists(bin)) {
        stop(END_FAILED,
             "The .bin file of this .cue is not beside it.\n\n%s\n\n"
             "Choose the .bin file itself.",
             cue);
    }
    free(named);
    free(folder);
    free(text);
    return bin;
}

/* -------------------------------------------------------------------------
 * The program
 * ---------------------------------------------------------------------- */
typedef struct {
    const char *patch; /* --patch */
    const char *out;   /* --out */
    const char *image; /* the image, as it was given: a .bin or a .cue */
    int yes;           /* --yes */
} Options;

/* The patch beside this program: the one file named ReBushido-*.bps. */
static char *patch_beside(const char *argv0) {
    char *folder = program_folder(argv0);
    char *name = NULL, *path;
    int count = folder_patches(folder, &name);

    if (count < 1) {
        stop(
            END_PATCH,
            "The patch file (ReBushido-<version>.bps) is not beside this program.\n\n"
            "Keep ReBushidoPatcher and the .bps file that came with it in one folder. If you started it from "
            "inside a .zip, extract all of the .zip's files first.");
    }
    if (count > 1) {
        stop(
            END_PATCH,
            "There are %d patch files (ReBushido-*.bps) beside this program, and it cannot tell which one you "
            "want.\n\nKeep only one of them in its folder.",
            count);
    }
    path = text_of("%s%s", folder, name);
    free(name);
    free(folder);
    return path;
}

/* Ends the program if writing `written` would be writing over one of the
 * files it reads. */
static void keep_apart(const char *written, const Options *options,
                       const char *image, const char *patch) {
    const char *read[3];
    int i;

    read[0] = image;
    read[1] = options->image;
    read[2] = patch;
    for (i = 0; i < 3; i++) {
        if (path_same(written, read[i])) {
            stop(
                END_USAGE,
                "The result would be written over a file this needs:\n\n%s\n\n"
                "Give your image another name or put it in another folder first%s.",
                read[i], g_windowed ? "" : ", or name the result with --out");
        }
    }
}

static void write_cue(const char *cue, const char *bin) {
    FILE *file = file_open(cue, "wb");
    int failed;

    if (file == NULL) {
        stop(END_FAILED, "The .cue file could not be written (%s).\n\n%s",
             strerror(errno), cue);
    }
    failed =
        fprintf(
            file,
            "FILE \"%s\" BINARY\r\n  TRACK 01 MODE2/2352\r\n    INDEX 01 00:00:00\r\n",
            name_of(bin)) < 0;
    if (fclose(file) != 0 || failed) {
        stop(END_FAILED, "The .cue file could not be written.\n\n%s", cue);
    }
}

static int run(const Options *options, const char *argv0) {
    Patch patch;
    Job job;
    char *patch_path, *image, *out, *cue = NULL, *part = NULL, *folder;
    unsigned char first[sizeof SYNC];
    int replace = options->yes, is_disc;
    char size_text[32];

    crc_start();
    say("ReBushido patcher\n\n");

    /* The patch first: without it there is nothing to ask an image for. */
    patch_path =
        options->patch ? text_of("%s", options->patch) : patch_beside(argv0);
    patch_load(&patch, patch_path);
    say("The patch: %s\n", patch_path);

    if (options->image != NULL) {
        image = text_of("%s", options->image);
    } else {
#if defined(_WIN32)
        say("Choose your Bushido Blade 2 disc image (.bin or .cue) in the window that opened.\n");
        image = choose_image();
        if (image == NULL) {
            stop(END_CANCELLED, "No image was chosen; nothing was done.");
        }
#else
        image = NULL;
#endif
    }
    if (ends_with(image, ".cue")) {
        char *bin = bin_of_cue(image);

        free(image);
        image = bin;
    }
    say("Your image: %s\n", image);

    memset(&job, 0, sizeof job);
    job.patch = &patch;
    job.block = memory(CHUNK);
    job.source_path = image;
    job.source = file_open(image, "rb");
    if (job.source == NULL) {
        stop(END_FAILED, "Your image cannot be opened (%s).\n\n%s",
             strerror(errno), image);
    }
    image_check(&patch, job.source, image, job.block);
    job.source_place = -1;

    /* Where the result goes, and that it is no file this reads. */
    if (options->out != NULL) {
        out = text_of("%s", options->out);
    } else {
        folder = folder_of(image);
        out = text_of("%sReBushido.bin", folder);
        free(folder);
    }
    for (;;) {
        int64_t room;

        if (ends_with(out, ".cue")) {
            stop(
                END_USAGE,
                "The result's name is its .bin's; the .cue is written beside it.\n\n%s",
                out);
        }
        free(cue);
        free(part);
        cue = with_extension(out, ".cue");
        part = text_of("%s.part", out);
        keep_apart(out, options, image, patch_path);
        keep_apart(cue, options, image, patch_path);
        keep_apart(part, options, image, patch_path);
        if (!replace && (path_exists(out) || path_exists(cue))) {
            const char *there = path_exists(out) ? out : cue;

            if (!g_windowed) {
                stop(
                    END_EXISTS,
                    "The result is already there (--yes writes over it):\n\n%s",
                    there);
            }
            if (!ask(text_of("This file is already there:\n\n%s\n\nReplace it?",
                             there))) {
                stop(END_CANCELLED, "Nothing was written.");
            }
        }
        folder = folder_of(out);
        room = folder_free(folder);
        free(folder);
        if (room >= 0 && (uint64_t)room < patch.target_size) {
            char need[32], have[32];

            stop(END_FAILED,
                 "There is not enough room on the disk for the image.\n\n%s\n\n"
                 "It needs %s MB free, and %s MB are.",
                 out, grouped(patch.target_size / 1000000 + 1, need),
                 grouped((uint64_t)room / 1000000, have));
        }
        job.out = file_open(part, "w+b");
        if (job.out != NULL) {
            break;
        }
        if (!g_windowed) {
            stop(END_FAILED,
                 "The image cannot be written there (%s).\n\n%s\n\n"
                 "Name a file in a folder you can write in with --out.",
                 strerror(errno), out);
        }
#if defined(_WIN32)
        if (!ask(text_of(
                "The image cannot be saved in this folder:\n\n%s\n\nSave it somewhere else?",
                out))) {
            stop(END_CANCELLED, "Nothing was written.");
        }
        free(out);
        out = choose_result();
        if (out == NULL) {
            stop(END_CANCELLED, "Nothing was written.");
        }
        replace = 1; /* the window asked */
#endif
    }
    g_result = job.out;
    g_part = part;
    job.out_path = out;

    if (!is_screen(stdout)) {
        say("Writing %s ...\n", out);
    }
    apply(&job);
    fclose(job.source);

    /* A disc image begins with a sector's twelve bytes of sync. */
    is_disc = fflush(job.out) == 0 && file_seek(job.out, 0) == 0 &&
              fread(first, 1, sizeof first, job.out) == sizeof first &&
              memcmp(first, SYNC, sizeof SYNC) == 0;
    g_result = NULL;
    if (fclose(job.out) != 0) {
        write_failed(&job);
    }
    if (path_rename(part, out) != 0) {
        stop(END_FAILED,
             "The image was made but could not be given its name.\n\n%s", out);
    }
    g_part = NULL;
    say("  %s bytes, CRC32 %08lX\n", grouped(patch.target_size, size_text),
        (unsigned long)patch.target_crc);
    if (is_disc) {
        char text[MESSAGE_MOST];

        write_cue(cue, out);
        snprintf(
            text, sizeof text,
            "Done. The ReBushido disc image is ready:\n\n%s\n%s\n\n"
            "To play, open %s in your PlayStation emulator as you would any disc, and keep the .bin beside "
            "it. Your own image was only read and is as it was.",
            cue, out, name_of(cue));
        say("\n%s\n", text);
#if defined(_WIN32)
        if (g_windowed) {
            message_box(text, MB_OK | MB_ICONINFORMATION);
        }
#endif
    } else {
        say("OK: %s\n", out);
    }
    return END_OK;
}

static int main_of(int argc, char **argv) {
    Options options;
    int i;

    memset(&options, 0, sizeof options);
#if defined(_WIN32)
    /* The windows are for a player who started it from the desktop: with
     * the console its own, or (below) with no image named. Never where the
     * output goes to a file or another program. */
    g_windowed = is_screen(stdout) && own_console();
#endif
    for (i = 1; i < argc; i++) {
        const char *word = argv[i];

        if (strcmp(word, "--help") == 0 || strcmp(word, "-h") == 0) {
            text_out(stdout, USAGE);
            return END_OK;
        } else if (strcmp(word, "--yes") == 0) {
            options.yes = 1;
        } else if (
            (strcmp(word, "--patch") == 0 || strcmp(word, "--out") == 0) &&
            i + 1 < argc) {
            if (word[2] == 'p') {
                options.patch = argv[++i];
            } else {
                options.out = argv[++i];
            }
        } else if (
            (word[0] == '-' && word[1] == '-') || options.image != NULL) {
            if (g_windowed) {
                stop(
                    END_USAGE,
                    "Start ReBushidoPatcher by itself, or drop one disc image (.bin or .cue) on it.");
            }
            text_out(stderr, USAGE);
            return END_USAGE;
        } else {
            options.image = word;
        }
    }
#if defined(_WIN32)
    if (options.image == NULL && is_screen(stdout)) {
        g_windowed = 1;
    }
#endif
    if (options.image == NULL && !g_windowed) {
        text_out(stderr, USAGE);
        return END_USAGE;
    }
    return run(&options, argv[0]);
}

#if defined(_WIN32)
/* The arguments as Windows has them, in any language, not as the C
 * library would narrow them to the system's code page. */
int main(void) {
    int argc = 0, i;
    wchar_t **wide_argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    char **argv;

    if (wide_argv == NULL) {
        return END_FAILED;
    }
    argv = memory(((size_t)argc + 1) * sizeof *argv);
    for (i = 0; i < argc; i++) {
        argv[i] = narrow(wide_argv[i]);
    }
    argv[argc] = NULL;
    return main_of(argc, argv);
}
#else
int main(int argc, char **argv) { return main_of(argc, argv); }
#endif
