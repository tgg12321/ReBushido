/* The processors with the larger cache, as far as it is arithmetic
 * (window.h; docs/PORT.md, "The processors with the larger cache"): what
 * a table of the third-level caches says is to be done, and Linux's
 * texts of them read and written. A processor with two dies of cores,
 * one of them under a larger cache (AMD's X3D of two dies: 32 MB beside
 * 96 MB), runs the step's thread faster on the larger; on any other
 * processor nothing is done. No system call, no C library: cpu_prefer.c
 * reads the system's table, and tests/port/window.c runs this on made-up
 * ones. */
#include "common.h"
#include "window.h"

#define BITS_PER_WORD 32
#define DECIMAL 10
#define KB_PER_MB 1024

void port_AddCpu(PortCpuSet *set, s32 cpu) {
    if (cpu >= 0 && cpu < PORT_CPUS_MAX) {
        set->words[cpu / BITS_PER_WORD] |= 1U << (cpu % BITS_PER_WORD);
    }
}

s32 port_HasCpu(const PortCpuSet *set, s32 cpu) {
    if (cpu < 0 || cpu >= PORT_CPUS_MAX) {
        return FALSE;
    }
    return (set->words[cpu / BITS_PER_WORD] >> (cpu % BITS_PER_WORD)) & 1;
}

s32 port_CountCpus(const PortCpuSet *set) {
    s32 cpu;
    s32 count = 0;

    for (cpu = 0; cpu < PORT_CPUS_MAX; cpu++) {
        count += port_HasCpu(set, cpu);
    }
    return count;
}

static void port_ClearCpus(PortCpuSet *set) {
    s32 i;

    for (i = 0; i < PORT_CPUS_MAX / BITS_PER_WORD; i++) {
        set->words[i] = 0;
    }
}

/* Every processor of the system under exactly one of the caches, and
 * every cache's processors the system's. */
static s32 port_IsCovered(const PortCacheTable *table) {
    s32 cpu;
    s32 i;

    for (cpu = 0; cpu < PORT_CPUS_MAX; cpu++) {
        s32 under = 0;

        for (i = 0; i < table->caches; i++) {
            under += port_HasCpu(&table->on[i], cpu);
        }
        if (under != (port_HasCpu(&table->all, cpu) ? 1 : 0)) {
            return FALSE;
        }
    }
    return TRUE;
}

s32 port_ChooseCpus(const PortCacheTable *table, PortCpuSet *prefer) {
    u32 largest = 0;
    u32 next = 0;
    s32 i;

    port_ClearCpus(prefer);
    if (table->is_unread || table->caches <= 0 ||
        table->caches > PORT_CACHES_MAX || port_CountCpus(&table->all) == 0) {
        return PORT_CPUS_UNREAD;
    }
    for (i = 0; i < table->caches; i++) {
        if (table->kb[i] == 0 || port_CountCpus(&table->on[i]) == 0) {
            return PORT_CPUS_UNREAD;
        }
    }
    /* a processor under no L3 (Intel's low-power island) or under two */
    if (!port_IsCovered(table)) {
        return PORT_CPUS_UNREAD;
    }
    /* Intel's P and E cores, ARM's big and little: the system schedules
     * those itself, and knows them better */
    if (table->kinds > 1) {
        return PORT_CPUS_HYBRID;
    }
    if (table->caches == 1) {
        return PORT_CPUS_ONE_CACHE;
    }
    for (i = 0; i < table->caches; i++) {
        if (table->kb[i] > largest) {
            largest = table->kb[i];
        }
    }
    for (i = 0; i < table->caches; i++) {
        if (table->kb[i] < largest && table->kb[i] > next) {
            next = table->kb[i];
        }
    }
    if (next == 0 ||
        (u64)largest * DECIMAL < (u64)next * PORT_CPUS_LARGER_TENTHS) {
        return PORT_CPUS_ONE_SIZE;
    }
    for (i = 0; i < table->caches; i++) {
        if (table->kb[i] == largest) {
            s32 cpu;

            for (cpu = 0; cpu < PORT_CPUS_MAX; cpu++) {
                if (port_HasCpu(&table->on[i], cpu)) {
                    port_AddCpu(prefer, cpu);
                }
            }
        }
    }
    return PORT_CPUS_PREFER;
}

static s32 port_IsDigit(char c) { return c >= '0' && c <= '9'; }

/* A number of the list, and where the text goes on after it; -1 where
 * there is none or it is too large. */
static s32 port_ReadListNumber(const char **text) {
    s32 value = 0;

    if (!port_IsDigit(**text)) {
        return -1;
    }
    while (port_IsDigit(**text)) {
        value = value * DECIMAL + (**text - '0');
        if (value >= PORT_CPUS_MAX) {
            return -1;
        }
        (*text)++;
    }
    return value;
}

s32 port_ReadCpuList(const char *text, PortCpuSet *set) {
    port_ClearCpus(set);
    for (;;) {
        s32 first = port_ReadListNumber(&text);
        s32 last = first;
        s32 cpu;

        if (first < 0) {
            return FALSE;
        }
        if (*text == '-') {
            text++;
            last = port_ReadListNumber(&text);
            if (last < first) {
                return FALSE;
            }
        }
        for (cpu = first; cpu <= last; cpu++) {
            port_AddCpu(set, cpu);
        }
        if (*text != ',') {
            break;
        }
        text++;
    }
    while (*text == '\n' || *text == '\r' || *text == ' ') {
        text++;
    }
    return *text == 0;
}

u32 port_ReadCacheSize(const char *text) {
    u32 value = 0;

    if (!port_IsDigit(*text)) {
        return 0;
    }
    while (port_IsDigit(*text)) {
        if (value > 0xFFFFFFFFU / DECIMAL / KB_PER_MB) {
            return 0;
        }
        value = value * DECIMAL + (u32)(*text - '0');
        text++;
    }
    if (*text == 'M') {
        value *= KB_PER_MB;
        text++;
    } else if (*text == 'K') {
        text++;
    } else {
        /* bytes */
        value /= KB_PER_MB;
    }
    while (*text == '\n' || *text == '\r' || *text == ' ') {
        text++;
    }
    return *text == 0 ? value : 0;
}

/* A number's digits at `out`, and where they end. */
static char *port_PutCpuNumber(char *out, s32 value) {
    char digits[4];
    s32 count = 0;

    do {
        digits[count++] = (char)('0' + value % DECIMAL);
        value /= DECIMAL;
    } while (value != 0);
    while (count > 0) {
        *out++ = digits[--count];
    }
    return out;
}

void port_FormatCpuList(const PortCpuSet *set, char *out) {
    /* room for `,255-255` and `...` and the 0 */
    char *end = out + PORT_CPU_LIST_BYTES - 12;
    char *at = out;
    s32 cpu = 0;

    while (cpu < PORT_CPUS_MAX) {
        s32 last;

        if (!port_HasCpu(set, cpu)) {
            cpu++;
            continue;
        }
        if (at >= end) {
            *at++ = '.';
            *at++ = '.';
            *at++ = '.';
            break;
        }
        for (last = cpu; port_HasCpu(set, last + 1); last++) {
        }
        if (at != out) {
            *at++ = ',';
        }
        at = port_PutCpuNumber(at, cpu);
        if (last != cpu) {
            *at++ = '-';
            at = port_PutCpuNumber(at, last);
        }
        cpu = last + 1;
    }
    if (at == out) {
        *at++ = '-';
    }
    *at = 0;
}
