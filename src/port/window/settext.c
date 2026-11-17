/* The native port's window: rebushido.cfg changed a line at a time, for
 * the settings menu (window.h; docs/PLAYING.md, "The settings menu").
 * Everything of the file that is not the line changed is kept as the
 * player wrote it. No SDL and no C library: tests/port/settings.c runs
 * it on texts of its own. */
#include "common.h"
#include "window.h"

#define TEXT_END 0
#define LINE_END '\n'
#define COMMENT '#'
#define EQUALS '='
/* the longest value with the spaces after it that is written in place */
#define VALUE_BYTES_MAX 256

/* A line of the text: where it begins and ends (its \n not counted), and
 * where its name and its value are, without the spaces round them and
 * without a comment. `name_end` is `name_at` for a line with no `=`. */
typedef struct {
    u32 at;
    u32 end;
    u32 name_at;
    u32 name_end;
    u32 value_at;
    u32 value_end;
    u32 comment_at; /* its `#`, or `end` */
} PortTextLine;

static u32 port_TextSize(const char *text) {
    u32 size = 0;

    while (text[size] != TEXT_END) {
        size++;
    }
    return size;
}

static s32 port_IsBlank(char letter) {
    return letter == ' ' || letter == '\t' || letter == '\r';
}

static char port_Lower(char letter) {
    return letter >= 'A' && letter <= 'Z' ? (char)(letter - 'A' + 'a') : letter;
}

/* The line that begins at `at`. */
static void port_CutLine(const char *text, u32 at, PortTextLine *line) {
    u32 i = at;
    u32 equals;

    line->at = at;
    while (text[i] != TEXT_END && text[i] != LINE_END) {
        i++;
    }
    line->end = i;
    line->comment_at = at;
    while (line->comment_at < line->end && text[line->comment_at] != COMMENT) {
        line->comment_at++;
    }
    equals = at;
    while (equals < line->comment_at && text[equals] != EQUALS) {
        equals++;
    }
    line->name_at = at;
    while (line->name_at < equals && port_IsBlank(text[line->name_at])) {
        line->name_at++;
    }
    if (equals == line->comment_at) {
        /* no `=`: no setting */
        line->name_end = line->name_at;
        line->value_at = line->value_end = line->comment_at;
        return;
    }
    line->name_end = equals;
    while (line->name_end > line->name_at &&
           port_IsBlank(text[line->name_end - 1])) {
        line->name_end--;
    }
    line->value_at = equals + 1;
    while (line->value_at < line->comment_at &&
           port_IsBlank(text[line->value_at])) {
        line->value_at++;
    }
    line->value_end = line->comment_at;
    while (line->value_end > line->value_at &&
           port_IsBlank(text[line->value_end - 1])) {
        line->value_end--;
    }
    if (line->value_at == line->comment_at) {
        /* no value: one goes a space after the = */
        line->value_at = equals + 1;
        if (line->value_at < line->comment_at &&
            port_IsBlank(text[line->value_at])) {
            line->value_at++;
        }
        line->value_end = line->value_at;
    }
}

/* The start of the line after this one, or the text's end. */
static u32 port_NextLine(const char *text, const PortTextLine *line) {
    return text[line->end] == LINE_END ? line->end + 1 : line->end;
}

/* Whether the line sets `name`, letters of either case. */
static s32 port_LineSets(
    const char *text, const PortTextLine *line, const char *name) {
    u32 i;

    if (line->name_end == line->name_at) {
        return FALSE;
    }
    for (i = 0; line->name_at + i < line->name_end; i++) {
        if (name[i] == TEXT_END ||
            port_Lower(text[line->name_at + i]) != port_Lower(name[i])) {
            return FALSE;
        }
    }
    return name[i] == TEXT_END;
}

/* Whether the line's value begins with the word `word`, letters of
 * either case. */
static s32 port_ValueIs(
    const char *text, const PortTextLine *line, const char *word) {
    u32 i;

    for (i = 0; word[i] != TEXT_END; i++) {
        if (line->value_at + i >= line->value_end ||
            port_Lower(text[line->value_at + i]) != port_Lower(word[i])) {
            return FALSE;
        }
    }
    return line->value_at + i == line->value_end ||
           port_IsBlank(text[line->value_at + i]);
}

/* `count` bytes at `at` become the `size` bytes of `with`: FALSE if the
 * text would not fit in `room` with its 0. */
static s32 port_Splice(
    char *text, u32 room, u32 at, u32 count, const char *with, u32 size) {
    u32 length = port_TextSize(text);
    u32 i;

    if (length - count + size + 1 > room) {
        return FALSE;
    }
    if (size > count) {
        for (i = length + 1; i-- > at + count;) {
            text[i + size - count] = text[i];
        }
    } else if (size < count) {
        for (i = at + count; i <= length; i++) {
            text[i - (count - size)] = text[i];
        }
    }
    for (i = 0; i < size; i++) {
        text[at + i] = with[i];
    }
    return TRUE;
}

/* `name = value` and a \n at the end of the text, after a \n of its own
 * where the last line has none. */
static s32 port_AddLine(
    char *text, u32 room, const char *name, const char *value) {
    u32 length = port_TextSize(text);
    u32 name_size = port_TextSize(name);
    u32 value_size = port_TextSize(value);
    u32 needs_end = length > 0 && text[length - 1] != LINE_END;
    u32 size = needs_end + name_size + 3 + value_size + 1;
    u32 at;
    u32 i;

    if (length + size + 1 > room) {
        return FALSE;
    }
    at = length;
    if (needs_end) {
        text[at++] = LINE_END;
    }
    for (i = 0; i < name_size; i++) {
        text[at++] = name[i];
    }
    text[at++] = ' ';
    text[at++] = EQUALS;
    text[at++] = ' ';
    for (i = 0; i < value_size; i++) {
        text[at++] = value[i];
    }
    text[at++] = LINE_END;
    text[at] = TEXT_END;
    return TRUE;
}

s32 port_SetSettingText(
    char *text, u32 room, const char *name, const char *value) {
    char with[VALUE_BYTES_MAX];
    PortTextLine line;
    PortTextLine last;
    s32 found = FALSE;
    u32 at = 0;
    u32 size = port_TextSize(value);
    u32 old_size;
    u32 gap;
    u32 wanted;
    u32 i;

    /* (given a value only for the compiler, which cannot see that ound`n     *
     * guards it) */
    port_CutLine(text, 0, &last);
    while (text[at] != TEXT_END) {
        port_CutLine(text, at, &line);
        if (port_LineSets(text, &line, name)) {
            last = line;
            found = TRUE;
        }
        at = port_NextLine(text, &line);
    }
    if (!found) {
        return port_AddLine(text, room, name, value);
    }
    old_size = last.value_end - last.value_at;
    if (last.comment_at == last.end) {
        /* nothing after the value to keep in its column; a space after
         * an = with nothing after it */
        if (old_size == 0 && last.value_at > 0 &&
            text[last.value_at - 1] == EQUALS && size + 1 <= VALUE_BYTES_MAX) {
            with[0] = ' ';
            for (i = 0; i < size; i++) {
                with[i + 1] = value[i];
            }
            return port_Splice(
                text, room, last.value_at, old_size, with, size + 1);
        }
        return port_Splice(text, room, last.value_at, old_size, value, size);
    }
    /* the comment stays where it was, as far as the spaces before it
     * allow, with one space at least between it and the value */
    gap = last.comment_at - last.value_end;
    wanted = gap + old_size > size + 1 ? gap + old_size - size : 1;
    if (size + wanted > VALUE_BYTES_MAX) {
        return FALSE;
    }
    for (i = 0; i < size; i++) {
        with[i] = value[i];
    }
    for (i = 0; i < wanted; i++) {
        with[size + i] = ' ';
    }
    return port_Splice(text, room, last.value_at,
                       last.comment_at - last.value_at, with, size + wanted);
}

/* Every line that `drop` says to take out, taken out with its \n. */
static void port_DropLines(
    char *text,
    s32 (*drop)(
        const char *text, const PortTextLine *line, const void *context),
    const void *context) {
    PortTextLine line;
    u32 at = 0;

    while (text[at] != TEXT_END) {
        u32 next;

        port_CutLine(text, at, &line);
        next = port_NextLine(text, &line);
        if (drop(text, &line, context)) {
            /* it only grows shorter: the room it has now is enough */
            port_Splice(text, port_TextSize(text) + 1, at, next - at, "", 0);
            continue;
        }
        at = next;
    }
}

/* What a binding's line is to be taken out for. */
typedef struct {
    const char *name;
    s32 keys;
} PortBindingDrop;

static s32 port_DropsBinding(
    const char *text, const PortTextLine *line, const void *context) {
    const PortBindingDrop *drop = (const PortBindingDrop *)context;

    if (!port_LineSets(text, line, drop->name)) {
        return FALSE;
    }
    return port_ValueIs(text, line, "none") ||
           port_ValueIs(text, line, drop->keys ? "key" : "pad");
}

s32 port_SetBindingText(
    char *text, u32 room, const char *name, s32 keys, const char *value) {
    PortBindingDrop drop;

    drop.name = name;
    drop.keys = keys;
    if (value != NULL &&
        port_TextSize(text) + port_TextSize(name) + port_TextSize(value) + 5 >
            room) {
        /* checked first, so that a change that does not fit leaves the
         * old lines where they were */
        return FALSE;
    }
    port_DropLines(text, port_DropsBinding, &drop);
    return value == NULL || port_AddLine(text, room, name, value);
}

/* A pad's lines: `pN.` and anything. */
static s32 port_DropsPad(
    const char *text, const PortTextLine *line, const void *context) {
    s32 pad = *(const s32 *)context;
    char first;

    if (line->name_end - line->name_at < 4) {
        return FALSE;
    }
    first = text[line->name_at + 1];
    return port_Lower(text[line->name_at]) == 'p' &&
           (first == '1' || first == '2') && text[line->name_at + 2] == '.' &&
           (pad < 0 || first - '1' == pad);
}

void port_ClearBindingsText(char *text, s32 pad) {
    port_DropLines(text, port_DropsPad, &pad);
}
