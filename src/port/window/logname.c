/* The native port's window: the session's log, as far as it is
 * arithmetic (window.h; docs/PORT.md, "The window"): a log file's name,
 * which old ones go, the bytes printed cut into whole lines, and a sum
 * of the disc image's first bytes. No SDL, no C library and no file in
 * it: sdl_log.c has those, and tests/port/window.c runs this on made-up
 * names and bytes. */
#include "common.h"
#include "window.h"

#define LOG_STEM "rebushido-"
#define LOG_ENDING ".log"
#define STEM_BYTES 10
#define DATE_DIGITS 8
#define TIME_DIGITS 6
#define DECIMAL 10
#define CRC_POLYNOMIAL 0xEDB88320U
#define BITS_PER_BYTE 8

/* `digits` decimal digits of a number, the highest first. */
static char *port_PutDigits(char *out, s32 value, s32 digits) {
    s32 i;

    for (i = digits - 1; i >= 0; i--) {
        out[i] = (char)('0' + value % DECIMAL);
        value /= DECIMAL;
    }
    return out + digits;
}

/* `rebushido-YYYYMMDD-HHMMSS.log`: names of one length, whose order as
 * text is their order in time (window.h). */
void port_FormatLogName(
    s32 year, s32 month, s32 day, s32 hour, s32 minute, s32 second, char *out) {
    const char *text;

    for (text = LOG_STEM; *text != 0; text++) {
        *out++ = *text;
    }
    out = port_PutDigits(out, year, 4);
    out = port_PutDigits(out, month, 2);
    out = port_PutDigits(out, day, 2);
    *out++ = '-';
    out = port_PutDigits(out, hour, 2);
    out = port_PutDigits(out, minute, 2);
    out = port_PutDigits(out, second, 2);
    for (text = LOG_ENDING; *text != 0; text++) {
        *out++ = *text;
    }
    *out = 0;
}

s32 port_IsLogName(const char *name) {
    const char *text;
    s32 i;

    for (text = LOG_STEM; *text != 0; text++) {
        if (*name++ != *text) {
            return 0;
        }
    }
    for (i = 0; i < DATE_DIGITS + 1 + TIME_DIGITS; i++, name++) {
        if (i == DATE_DIGITS ? *name != '-' : (*name < '0' || *name > '9')) {
            return 0;
        }
    }
    for (text = LOG_ENDING; *text != 0; text++) {
        if (*name++ != *text) {
            return 0;
        }
    }
    return *name == 0;
}

/* Whether the first log's name is later than the second's: they are of
 * one length, so the first character that differs says. */
static s32 port_IsLaterLog(const char *first, const char *second) {
    while (*first != 0 && *first == *second) {
        first++;
        second++;
    }
    return *first > *second;
}

/* A log is old when `keep` logs or more are later than it. What is not
 * a log's name is never old: it is not the port's to delete (window.h). */
s32 port_FindOldLogs(
    const char * const *names, s32 count, s32 keep, u8 *is_old) {
    s32 old = 0;
    s32 i;
    s32 j;

    for (i = 0; i < count; i++) {
        s32 later = 0;

        is_old[i] = 0;
        if (!port_IsLogName(names[i])) {
            continue;
        }
        for (j = 0; j < count; j++) {
            later += j != i && port_IsLogName(names[j]) &&
                     port_IsLaterLog(names[j], names[i]);
        }
        if (later >= keep) {
            is_old[i] = 1;
            old++;
        }
    }
    return old;
}

/* Each whole line to the sink, without its end, an empty one too; a
 * line longer than the buffer in pieces (window.h). */
void port_TeeBytes(
    PortTee *tee, const char *bytes, u32 count, PortLineSink sink) {
    u32 i;

    for (i = 0; i < count; i++) {
        if (bytes[i] == '\n') {
            sink(tee->line, tee->used);
            tee->used = 0;
        } else if (bytes[i] != '\r') {
            if (tee->used == PORT_TEE_LINE_MAX) {
                sink(tee->line, tee->used);
                tee->used = 0;
            }
            tee->line[tee->used++] = bytes[i];
        }
    }
}

/* What has come of a line that has not ended yet. */
void port_FlushTee(PortTee *tee, PortLineSink sink) {
    if (tee->used != 0) {
        sink(tee->line, tee->used);
        tee->used = 0;
    }
}

/* The usual sum of 32 bits (zlib's, PNG's), a bit at a time; `crc` is 0
 * at the start and the sum so far after it. */
u32 port_Crc32(u32 crc, const u8 *bytes, u32 count) {
    u32 i;
    s32 bit;

    crc = ~crc;
    for (i = 0; i < count; i++) {
        crc ^= bytes[i];
        for (bit = 0; bit < BITS_PER_BYTE; bit++) {
            crc = (crc & 1) ? (crc >> 1) ^ CRC_POLYNOMIAL : crc >> 1;
        }
    }
    return ~crc;
}
