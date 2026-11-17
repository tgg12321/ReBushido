/* The native port's text: printf's work, the program's two outputs, and
 * the few things about strings its files need (port_platform.h). The port
 * is linked with no C library (docs/PORT.md, "The program"), so these are
 * written here. */
#include "common.h"
#include "port_host.h"
#include "bb2.h"

/* The bases a number is written in. */
#define BASE_DECIMAL 10
#define BASE_HEX 16
#define BASE_OCTAL 8
/* What port_DigitValue gives a character that is no digit of any base. */
#define NOT_A_DIGIT 99
/* The most digits a 32-bit number has, in octal, and room to spare. */
#define DIGITS_MAX 12

/* ---- strings ---- */

u32 port_TextLength(const char *text) {
    u32 length = 0;

    while (text[length] != 0) {
        length++;
    }
    return length;
}

s32 port_TextsEqual(const char *first, const char *second) {
    while (*first != 0 && *first == *second) {
        first++;
        second++;
    }
    return *first == *second;
}

static char port_LowerCase(char character) {
    return (character >= 'A' && character <= 'Z')
               ? (char)(character + ('a' - 'A'))
               : character;
}

/* Whether a text ends with another, in either case (port_platform.h). */
s32 port_TextEndsWith(const char *text, const char *ending) {
    u32 length = port_TextLength(text);
    u32 ending_length = port_TextLength(ending);
    u32 i;

    if (ending_length > length) {
        return 0;
    }
    text += length - ending_length;
    for (i = 0; i < ending_length; i++) {
        if (port_LowerCase(text[i]) != port_LowerCase(ending[i])) {
            return 0;
        }
    }
    return 1;
}

static s32 port_DigitValue(char character) {
    if (character >= '0' && character <= '9') {
        return character - '0';
    }
    character = port_LowerCase(character);
    if (character >= 'a' && character <= 'f') {
        return character - 'a' + BASE_DECIMAL;
    }
    return NOT_A_DIGIT;
}

/* The number a whole text spells (port_platform.h). */
s32 port_ParseNumber(const char *text, s32 *value) {
    s32 negative = *text == '-';
    u32 base = BASE_DECIMAL;
    u32 total = 0;

    text += negative;
    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        base = BASE_HEX;
        text += 2;
    }
    if (*text == 0) {
        return 0;
    }
    for (; *text != 0; text++) {
        u32 digit = (u32)port_DigitValue(*text);

        if (digit >= base) {
            return 0;
        }
        total = total * base + digit;
    }
    *value = negative ? -(s32)total : (s32)total;
    return 1;
}

/* ---- printf's work ---- */

/* Where formatted text goes: a buffer with so much room left. */
typedef struct {
    char *out;
    u32 room;  /* bytes left, the ending 0 among them */
    u32 count; /* characters written */
} PortTextSink;

static void port_PutChar(PortTextSink *sink, char character) {
    if (sink->room > 1) {
        *sink->out++ = character;
        sink->room--;
        sink->count++;
    }
}

static void port_PutPadding(PortTextSink *sink, s32 count, char pad) {
    for (; count > 0; count--) {
        port_PutChar(sink, pad);
    }
}

/* What a conversion's flags, width and precision said. */
typedef struct {
    s32 left;      /* `-`: the padding goes after */
    char pad;      /* ' ', or '0' */
    s32 width;     /* at least this many characters */
    s32 precision; /* -1 if none: a string's most, a number's least digits */
} PortTextField;

/* A number in `base`, with its sign, in the field's width: zeroes up to
 * the precision, or up to the width if the field asks for those. */
static void port_PutNumber(PortTextSink *sink, const PortTextField *field,
                           u32 value, u32 base, s32 negative, s32 upper) {
    const char *alphabet = upper ? "0123456789ABCDEF" : "0123456789abcdef";
    char digits[DIGITS_MAX];
    s32 count = 0;
    s32 zeroes = 0;
    s32 padding;

    do {
        digits[count++] = alphabet[value % base];
        value /= base;
    } while (value != 0);
    if (field->precision > count) {
        zeroes = field->precision - count;
    }
    padding = field->width - count - zeroes - negative;
    if (field->pad == '0' && !field->left && field->precision < 0) {
        /* the sign goes ahead of the zeroes that fill the width */
        zeroes += padding > 0 ? padding : 0;
        padding = 0;
    }
    if (!field->left) {
        port_PutPadding(sink, padding, ' ');
    }
    if (negative) {
        port_PutChar(sink, '-');
    }
    port_PutPadding(sink, zeroes, '0');
    while (count > 0) {
        port_PutChar(sink, digits[--count]);
    }
    if (field->left) {
        port_PutPadding(sink, padding, ' ');
    }
}

/* A string, cut to the precision, in the field's width. */
static void port_PutString(
    PortTextSink *sink, const PortTextField *field, const char *text) {
    s32 length;
    s32 i;

    if (text == NULL) {
        text = "(null)";
    }
    length = (s32)port_TextLength(text);
    if (field->precision >= 0 && field->precision < length) {
        length = field->precision;
    }
    if (!field->left) {
        port_PutPadding(sink, field->width - length, ' ');
    }
    for (i = 0; i < length; i++) {
        port_PutChar(sink, text[i]);
    }
    if (field->left) {
        port_PutPadding(sink, field->width - length, ' ');
    }
}

/* A run of digits, or `*` for the next argument. */
static const char *port_ReadFieldNumber(
    const char *format, PortArgs *args, s32 *number) {
    if (*format == '*') {
        *number = PORT_ARG(*args, s32);
        return format + 1;
    }
    *number = 0;
    while (*format >= '0' && *format <= '9') {
        *number = *number * BASE_DECIMAL + (*format++ - '0');
    }
    return format;
}

/* printf's work (port_platform.h): the format's characters as they
 * stand, and each conversion's flags, width, precision and kind. */
u32 port_FormatText(char *out, u32 room, const char *format, PortArgs args) {
    PortTextSink sink;
    PortArgs walk;

    sink.out = out;
    sink.room = room;
    sink.count = 0;
    __builtin_va_copy(walk, args);
    while (*format != 0) {
        PortTextField field;
        char kind;

        if (*format != '%') {
            port_PutChar(&sink, *format++);
            continue;
        }
        format++;
        field.left = 0;
        field.pad = ' ';
        field.precision = -1;
        for (;; format++) {
            if (*format == '-') {
                field.left = 1;
            } else if (*format == '0') {
                field.pad = '0';
            } else if (*format != ' ' && *format != '+' && *format != '#') {
                break;
            }
        }
        format = port_ReadFieldNumber(format, &walk, &field.width);
        if (field.width < 0) {
            field.left = 1;
            field.width = -field.width;
        }
        if (*format == '.') {
            format = port_ReadFieldNumber(format + 1, &walk, &field.precision);
        }
        while (*format == 'l' || *format == 'h') {
            format++;
        }
        kind = *format;
        if (kind == 0) {
            break;
        }
        format++;
        switch (kind) {
        case 'd':
        case 'i': {
            s32 value = PORT_ARG(walk, s32);

            port_PutNumber(
                &sink, &field, value < 0 ? 0U - (u32)value : (u32)value,
                BASE_DECIMAL, value < 0, 0);
            break;
        }
        case 'u':
            port_PutNumber(
                &sink, &field, PORT_ARG(walk, u32), BASE_DECIMAL, 0, 0);
            break;
        case 'o':
            port_PutNumber(
                &sink, &field, PORT_ARG(walk, u32), BASE_OCTAL, 0, 0);
            break;
        case 'x':
        case 'p':
            port_PutNumber(&sink, &field, PORT_ARG(walk, u32), BASE_HEX, 0, 0);
            break;
        case 'X':
            port_PutNumber(&sink, &field, PORT_ARG(walk, u32), BASE_HEX, 0, 1);
            break;
        case 'c':
            port_PutPadding(&sink, field.left ? 0 : field.width - 1, ' ');
            port_PutChar(&sink, (char)PORT_ARG(walk, s32));
            port_PutPadding(&sink, field.left ? field.width - 1 : 0, ' ');
            break;
        case 's':
            port_PutString(&sink, &field, PORT_ARG(walk, const char *));
            break;
        default:
            /* `%%`, and anything not known, as it stands */
            port_PutChar(&sink, kind);
            break;
        }
    }
    __builtin_va_end(walk);
    if (sink.room > 0) {
        *sink.out = 0;
    }
    return sink.count;
}

/* ---- the program's outputs ---- */

#define PORT_LINE_MAX 1024

static char g_port_output[0x4000];
static u32 g_port_output_used;

s32 (*g_port_print_tap)(const char *text, u32 length);

void port_FlushOutput(void) {
    port_HostWrite(PORT_HOST_OUTPUT, g_port_output, g_port_output_used);
    g_port_output_used = 0;
}

/* A line or a piece of one to the program's output, by way of its
 * buffer (port_platform.h). */
void port_Print(const char *format, ...) {
    char line[PORT_LINE_MAX];
    PortArgs args;
    u32 length;
    u32 i;

    PORT_ARGS_START(args, format);
    length = port_FormatText(line, sizeof(line), format, args);
    PORT_ARGS_END(args);
    if (g_port_print_tap != NULL && !g_port_print_tap(line, length)) {
        return;
    }
    if (g_port_output_used + length > sizeof(g_port_output)) {
        port_FlushOutput();
    }
    for (i = 0; i < length; i++) {
        g_port_output[g_port_output_used++] = line[i];
    }
}

static void port_LogArgs(const char *format, PortArgs args) {
    char line[PORT_LINE_MAX];
    u32 length = port_FormatText(line, sizeof(line), format, args);

    /* what was printed before it comes out before it */
    port_FlushOutput();
    port_HostWrite(PORT_HOST_ERRORS, line, length);
}

void port_Log(const char *format, ...) {
    PortArgs args;

    PORT_ARGS_START(args, format);
    port_LogArgs(format, args);
    PORT_ARGS_END(args);
}

void port_Fail(const char *format, ...) {
    PortArgs args;

    PORT_ARGS_START(args, format);
    port_LogArgs(format, args);
    PORT_ARGS_END(args);
    port_Quit(PORT_EXIT_FAILED);
}
