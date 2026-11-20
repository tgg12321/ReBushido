/* The updater's arithmetic (update.h): SHA-256, the manifest read, the
 * signature checked, versions compared, the paths a package may write and
 * the asset's address found in the GitHub API's answer. Nothing here
 * touches a file or the network; update.c does. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "update.h"
#include "../vendor/monocypher/monocypher-ed25519.h"

/* ---- SHA-256 (FIPS 180-4) ---- */

static const u32 g_sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

static u32 port_Rotate(u32 x, s32 n) { return (x >> n) | (x << (32 - n)); }

static void port_Sha256Block(PortSha256 *sha, const u8 *block) {
    u32 w[64];
    u32 v[8];
    s32 i;

    for (i = 0; i < 16; i++) {
        w[i] = ((u32)block[i * 4] << 24) | ((u32)block[i * 4 + 1] << 16) |
               ((u32)block[i * 4 + 2] << 8) | block[i * 4 + 3];
    }
    for (i = 16; i < 64; i++) {
        u32 s0 = port_Rotate(w[i - 15], 7) ^ port_Rotate(w[i - 15], 18) ^
                 (w[i - 15] >> 3);
        u32 s1 = port_Rotate(w[i - 2], 17) ^ port_Rotate(w[i - 2], 19) ^
                 (w[i - 2] >> 10);

        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    memcpy(v, sha->state, sizeof(v));
    for (i = 0; i < 64; i++) {
        u32 s1 = port_Rotate(v[4], 6) ^ port_Rotate(v[4], 11) ^
                 port_Rotate(v[4], 25);
        u32 choose = (v[4] & v[5]) ^ (~v[4] & v[6]);
        u32 t1 = v[7] + s1 + choose + g_sha256_k[i] + w[i];
        u32 s0 = port_Rotate(v[0], 2) ^ port_Rotate(v[0], 13) ^
                 port_Rotate(v[0], 22);
        u32 major = (v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]);

        v[7] = v[6];
        v[6] = v[5];
        v[5] = v[4];
        v[4] = v[3] + t1;
        v[3] = v[2];
        v[2] = v[1];
        v[1] = v[0];
        v[0] = t1 + s0 + major;
    }
    for (i = 0; i < 8; i++) {
        sha->state[i] += v[i];
    }
}

void port_Sha256Begin(PortSha256 *sha) {
    static const u32 start[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};

    memcpy(sha->state, start, sizeof(start));
    sha->used = 0;
    sha->length = 0;
}

void port_Sha256Add(PortSha256 *sha, const void *data, u32 size) {
    const u8 *at = data;

    sha->length += size;
    while (size > 0) {
        u32 take = 64 - sha->used;

        if (take > size) {
            take = size;
        }
        if (sha->used == 0 && size >= 64) {
            port_Sha256Block(sha, at);
            take = 64;
        } else {
            memcpy(sha->block + sha->used, at, take);
            sha->used += take;
            if (sha->used == 64) {
                port_Sha256Block(sha, sha->block);
                sha->used = 0;
            }
        }
        at += take;
        size -= take;
    }
}

void port_Sha256End(PortSha256 *sha, u8 *out) {
    unsigned long long bits = sha->length * 8;
    u8 pad = 0x80;
    u8 zero = 0;
    u8 length[8];
    s32 i;

    port_Sha256Add(sha, &pad, 1);
    while (sha->used != 56) {
        port_Sha256Add(sha, &zero, 1);
    }
    for (i = 0; i < 8; i++) {
        length[i] = (u8)(bits >> (56 - i * 8));
    }
    port_Sha256Add(sha, length, 8);
    for (i = 0; i < 8; i++) {
        out[i * 4] = (u8)(sha->state[i] >> 24);
        out[i * 4 + 1] = (u8)(sha->state[i] >> 16);
        out[i * 4 + 2] = (u8)(sha->state[i] >> 8);
        out[i * 4 + 3] = (u8)sha->state[i];
    }
}

void port_Sha256(const void *data, u32 size, u8 *out) {
    PortSha256 sha;

    port_Sha256Begin(&sha);
    port_Sha256Add(&sha, data, size);
    port_Sha256End(&sha, out);
}

/* ---- text ---- */

static s32 port_HexDigit(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

s32 port_UpdateReadHex(const char *text, u8 *out, s32 count) {
    s32 i;

    for (i = 0; i < count; i++) {
        s32 high = port_HexDigit(text[i * 2]);
        s32 low = high < 0 ? -1 : port_HexDigit(text[i * 2 + 1]);

        if (low < 0) {
            return FALSE;
        }
        out[i] = (u8)(high * 16 + low);
    }
    text += count * 2;
    if (*text == '\r') {
        text++;
    }
    if (*text == '\n') {
        text++;
    }
    return *text == 0;
}

s32 port_UpdateCompareVersions(const char *a, const char *b) {
    for (;;) {
        long x = 0;
        long y = 0;
        s32 more_a = *a >= '0' && *a <= '9';
        s32 more_b = *b >= '0' && *b <= '9';

        if (!more_a && !more_b) {
            return 0;
        }
        while (*a >= '0' && *a <= '9') {
            x = x * 10 + (*a++ - '0');
        }
        while (*b >= '0' && *b <= '9') {
            y = y * 10 + (*b++ - '0');
        }
        if (x != y) {
            return x < y ? -1 : 1;
        }
        /* past the dot, to the next number; what is not a number after
         * a number ends the version */
        a += *a == '.' && a[1] >= '0' && a[1] <= '9';
        b += *b == '.' && b[1] >= '0' && b[1] <= '9';
        if (!(*a >= '0' && *a <= '9')) {
            a = "";
        }
        if (!(*b >= '0' && *b <= '9')) {
            b = "";
        }
    }
}

s32 port_UpdateOwnVersion(const char *text, char *out, u32 room) {
    const char *at = strchr(text, ' ');
    u32 used = 0;

    at = at != NULL ? at + 1 : text;
    while ((*at == '.' || (*at >= '0' && *at <= '9')) && used + 1 < room) {
        out[used++] = *at++;
    }
    out[used] = 0;
    return strncmp(at, "-dev", 4) == 0;
}

/* ---- the manifest ---- */

#define MANIFEST_FIRST "rebushido-update 1"

/* The value of `line` if it is `name = value`, or NULL. */
static const char *port_ManifestValue(const char *line, const char *name) {
    size_t length = strlen(name);

    if (strncmp(line, name, length) != 0 || strncmp(line + length, " = ", 3)) {
        return NULL;
    }
    return line + length + 3;
}

static s32 port_ManifestText(const char *value, char *out) {
    size_t length = strlen(value);

    if (length == 0 || length >= PORT_UPDATE_TEXT_BYTES) {
        return FALSE;
    }
    memcpy(out, value, length + 1);
    return TRUE;
}

/* A whole number of at most nine digits. */
static s32 port_ManifestNumber(const char *value, u32 *out) {
    u32 n = 0;
    s32 digits = 0;

    while (*value >= '0' && *value <= '9' && digits < 10) {
        n = n * 10 + (u32)(*value++ - '0');
        digits++;
    }
    *out = n;
    return digits > 0 && digits < 10 && *value == 0;
}

s32 port_UpdateReadFileLine(const char *value, PortUpdateFile *file) {
    char hash[65];
    char size[16];
    const char *at = value;
    size_t length;

    if (strlen(at) < 66 || at[64] != ' ') {
        return FALSE;
    }
    memcpy(hash, at, 64);
    hash[64] = 0;
    if (!port_UpdateReadHex(hash, file->hash, PORT_UPDATE_HASH_BYTES)) {
        return FALSE;
    }
    at += 65;
    length = strcspn(at, " ");
    if (length == 0 || length >= sizeof(size) || at[length] != ' ') {
        return FALSE;
    }
    memcpy(size, at, length);
    size[length] = 0;
    if (!port_ManifestNumber(size, &file->size)) {
        return FALSE;
    }
    at += length + 1;
    if (strlen(at) >= PORT_UPDATE_PATH_BYTES || !port_UpdatePathIsOurs(at)) {
        return FALSE;
    }
    strcpy(file->path, at);
    return TRUE;
}

static void port_Why(char *why, u32 room, const char *what, s32 line) {
    snprintf(why, room, "%s (its line %d)", what, line);
}

s32 port_UpdateParseManifest(
    const char *text, u32 size, PortUpdateManifest *manifest, char *why,
    u32 why_room) {
    char line[PORT_UPDATE_PATH_BYTES + 128];
    u32 at = 0;
    s32 number = 0;
    s32 has_hash = FALSE;
    s32 has_size = FALSE;
    s32 has_updater = FALSE;
    s32 i;
    s32 j;

    manifest->version[0] = manifest->commit[0] = manifest->channel[0] = 0;
    manifest->zip[0] = manifest->folder[0] = 0;
    manifest->updater = 0;
    manifest->note_count = 0;
    manifest->file_count = 0;
    while (at < size) {
        u32 length = 0;
        const char *value;

        while (at + length < size && text[at + length] != '\n') {
            length++;
        }
        number++;
        if (length >= sizeof(line) || memchr(text + at, 0, length) != NULL) {
            port_Why(why, why_room, "a line too long", number);
            return FALSE;
        }
        memcpy(line, text + at, length);
        line[length] = 0;
        at += length + 1;
        if (number == 1) {
            if (strcmp(line, MANIFEST_FIRST) != 0) {
                snprintf(why, why_room, "not a manifest of ReBushido's");
                return FALSE;
            }
            continue;
        }
        if (line[0] == 0) {
            continue;
        }
        if ((value = port_ManifestValue(line, "version")) != NULL) {
            if (!port_ManifestText(value, manifest->version) ||
                !(value[0] >= '0' && value[0] <= '9')) {
                port_Why(why, why_room, "a version that is not one", number);
                return FALSE;
            }
        } else if ((value = port_ManifestValue(line, "commit")) != NULL) {
            if (!port_ManifestText(value, manifest->commit)) {
                port_Why(why, why_room, "a commit too long", number);
                return FALSE;
            }
        } else if ((value = port_ManifestValue(line, "channel")) != NULL) {
            if (strcmp(value, "release") != 0 && strcmp(value, "beta") != 0) {
                port_Why(
                    why, why_room, "a channel not release or beta", number);
                return FALSE;
            }
            strcpy(manifest->channel, value);
        } else if ((value = port_ManifestValue(line, "updater")) != NULL) {
            u32 n;

            if (!port_ManifestNumber(value, &n) || n == 0) {
                port_Why(
                    why, why_room, "an updater that is not a number", number);
                return FALSE;
            }
            manifest->updater = (s32)n;
            has_updater = TRUE;
        } else if ((value = port_ManifestValue(line, "zip")) != NULL) {
            if (!port_ManifestText(value, manifest->zip) ||
                strpbrk(value, "/\\:") != NULL) {
                port_Why(why, why_room, "a zip's name that is not one", number);
                return FALSE;
            }
        } else if ((value = port_ManifestValue(line, "zip_size")) != NULL) {
            if (!port_ManifestNumber(value, &manifest->zip_size)) {
                port_Why(why, why_room, "a size that is not one", number);
                return FALSE;
            }
            has_size = TRUE;
        } else if ((value = port_ManifestValue(line, "zip_sha256")) != NULL) {
            if (!port_UpdateReadHex(
                    value, manifest->zip_hash, PORT_UPDATE_HASH_BYTES)) {
                port_Why(why, why_room, "a hash that is not one", number);
                return FALSE;
            }
            has_hash = TRUE;
        } else if ((value = port_ManifestValue(line, "folder")) != NULL) {
            if (!port_ManifestText(value, manifest->folder) ||
                strpbrk(value, "/\\:") != NULL || strcmp(value, "..") == 0) {
                port_Why(why, why_room, "a folder that is not one", number);
                return FALSE;
            }
        } else if ((value = port_ManifestValue(line, "note")) != NULL ||
                   strcmp(line, "note =") == 0) {
            if (manifest->note_count < PORT_UPDATE_NOTES_MAX) {
                snprintf(manifest->notes[manifest->note_count],
                         PORT_UPDATE_TEXT_BYTES, "%s",
                         value != NULL ? value : "");
                manifest->note_count++;
            }
        } else if ((value = port_ManifestValue(line, "file")) != NULL) {
            if (manifest->file_count >= manifest->room) {
                port_Why(why, why_room, "more files than are read", number);
                return FALSE;
            }
            if (!port_UpdateReadFileLine(
                    value, &manifest->files[manifest->file_count])) {
                port_Why(why, why_room, "a file that is not one a package has",
                         number);
                return FALSE;
            }
            manifest->file_count++;
        } else {
            port_Why(why, why_room, "a line it does not know", number);
            return FALSE;
        }
    }
    if (number == 0 || manifest->version[0] == 0 || manifest->zip[0] == 0 ||
        manifest->folder[0] == 0 || !has_hash || !has_size || !has_updater ||
        manifest->file_count == 0) {
        snprintf(why, why_room, "a manifest with something missing");
        return FALSE;
    }
    if (manifest->channel[0] == 0) {
        strcpy(manifest->channel, "release");
    }
    /* no file twice, in any case (Windows' names are one in any case) */
    for (i = 0; i < manifest->file_count; i++) {
        for (j = i + 1; j < manifest->file_count; j++) {
            const char *x = manifest->files[i].path;
            const char *y = manifest->files[j].path;

            while (*x != 0 && (*x | 0x20) == (*y | 0x20)) {
                x++;
                y++;
            }
            if (*x == 0 && *y == 0) {
                snprintf(why, why_room, "%s twice", manifest->files[i].path);
                return FALSE;
            }
        }
    }
    return TRUE;
}

s32 port_UpdateCheckSignature(
    const u8 *key, const u8 *signature, const void *text, u32 size) {
    return crypto_ed25519_check(signature, key, text, size) == 0;
}

/* ---- paths ---- */

/* Whether `a` ends with `b`, in any case. */
static s32 port_EndsWith(const char *a, const char *b) {
    size_t la = strlen(a);
    size_t lb = strlen(b);
    size_t i;

    if (la < lb) {
        return FALSE;
    }
    for (i = 0; i < lb; i++) {
        char x = a[la - lb + i];
        char y = b[i];

        if (x >= 'A' && x <= 'Z') {
            x = (char)(x + 32);
        }
        if (x != y) {
            return FALSE;
        }
    }
    return TRUE;
}

/* Whether the path's first folder is `name`, in any case. */
static s32 port_IsUnder(const char *path, const char *name) {
    size_t length = strlen(name);
    size_t i;

    for (i = 0; i < length; i++) {
        char c = path[i];

        if (c >= 'A' && c <= 'Z') {
            c = (char)(c + 32);
        }
        if (c != name[i]) {
            return FALSE;
        }
    }
    return path[length] == '/';
}

s32 port_UpdatePathIsOurs(const char *path) {
    static const char * const endings[] = {
        ".cfg", ".mcd", ".bin",
        ".cue", ".iso", ".chd",
        ".img", ".log", "made-from-disc.txt"};
    static const char * const folders[] = {
        "logs", "screenshots", "update", "prepare-work"};
    const char *part = path;
    size_t i;

    if (path[0] == 0 || path[0] == '/' || strpbrk(path, "\\:*?\"<>|") != NULL) {
        return FALSE;
    }
    /* each part a name: not empty, not . or .., no space at its end
     * (Windows takes those away) */
    while (*part != 0) {
        size_t length = strcspn(part, "/");

        if (length == 0 || (length == 1 && part[0] == '.') ||
            (length == 2 && part[0] == '.' && part[1] == '.') ||
            part[length - 1] == ' ' || part[length - 1] == '.') {
            return FALSE;
        }
        part += length;
        if (*part == '/') {
            part++;
            if (*part == 0) {
                return FALSE;
            }
        }
    }
    for (i = 0; i < sizeof(endings) / sizeof(endings[0]); i++) {
        if (port_EndsWith(path, endings[i])) {
            return FALSE;
        }
    }
    for (i = 0; i < sizeof(folders) / sizeof(folders[0]); i++) {
        if (port_IsUnder(path, folders[i])) {
            return FALSE;
        }
    }
    if (strncmp(path, "report-", 7) == 0) {
        return FALSE;
    }
    return TRUE;
}

/* ---- the GitHub API's answer ---- */

/* The JSON string at `at` (just after its opening quote) into `out`:
 * where it ends, or NULL. Only the escapes an address can have. */
static const char *port_JsonString(
    const char *at, const char *end, char *out, u32 room) {
    u32 used = 0;

    while (at < end && *at != '"') {
        char c = *at++;

        if (c == '\\') {
            if (at >= end) {
                return NULL;
            }
            c = *at++;
            if (c != '/' && c != '\\' && c != '"') {
                return NULL;
            }
        }
        if (used + 1 >= room) {
            return NULL;
        }
        out[used++] = c;
    }
    if (at >= end) {
        return NULL;
    }
    out[used] = 0;
    return at + 1;
}

s32 port_UpdateFindAsset(
    const char *json, u32 size, const char *name, char *out, u32 room) {
    static const char key[] = "\"browser_download_url\"";
    const char *end = json + size;
    const char *at = json;
    size_t name_length = strlen(name);

    while (at < end) {
        const char *found = NULL;
        const char *scan;
        size_t length;

        for (scan = at; scan + sizeof(key) - 1 <= end; scan++) {
            if (memcmp(scan, key, sizeof(key) - 1) == 0) {
                found = scan;
                break;
            }
        }
        if (found == NULL) {
            return FALSE;
        }
        at = found + sizeof(key) - 1;
        while (at < end && (*at == ' ' || *at == '\t' || *at == '\r' ||
                            *at == '\n' || *at == ':')) {
            at++;
        }
        if (at >= end || *at != '"') {
            continue;
        }
        at = port_JsonString(at + 1, end, out, room);
        if (at == NULL) {
            return FALSE;
        }
        length = strlen(out);
        if (length > name_length && out[length - name_length - 1] == '/' &&
            strcmp(out + length - name_length, name) == 0 &&
            (strncmp(out, "https://", 8) == 0 ||
             strncmp(out, "http://", 7) == 0)) {
            return TRUE;
        }
    }
    return FALSE;
}
