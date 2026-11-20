/* The processors with the larger cache, read from the system and asked
 * for (cpu_prefer; docs/PORT.md, "The processors with the larger
 * cache"). The system's table of third-level caches is read once, at the
 * start: on Windows by GetLogicalProcessorInformationEx, on Linux from
 * /sys/devices/system/cpu/. What is made of it is cpus.c's: where one
 * cache is clearly the larger (AMD's X3D of two dies), the step's thread
 * asks the system to run it on that cache's processors; on any other
 * processor, or where the table cannot be read, nothing is asked. Where
 * a thread runs changes how soon it is done, never what it computes. */
#if !defined(_WIN32)
#define _GNU_SOURCE
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <sched.h>
#endif

#include "common.h"
#include "sdl_window.h"
#include "window.h"

#define KB 1024
#define CPUS_PER_GROUP 64

/* the processors preferred, where something is to be asked */
static PortCpuSet g_port_cpus_preferred;
static s32 g_port_cpus_preferring;

#if defined(_WIN32)

/* EfficiencyClass is the byte after Flags (MinGW's headers call it
 * Reserved[0]) */
#define CORE_EFFICIENCY(core) ((core)->Reserved[0])
#define CLASSES_MAX 256

static void port_AddGroupMask(PortCpuSet *set, const GROUP_AFFINITY *mask) {
    s32 bit;

    for (bit = 0; bit < CPUS_PER_GROUP; bit++) {
        if ((mask->Mask >> bit) & 1) {
            port_AddCpu(set, (s32)mask->Group * CPUS_PER_GROUP + bit);
        }
    }
}

static void port_ReadCaches(PortCacheTable *table) {
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *info;
    u8 is_class[CLASSES_MAX];
    DWORD bytes = 0;
    DWORD at;

    memset(is_class, 0, sizeof(is_class));
    GetLogicalProcessorInformationEx(RelationAll, NULL, &bytes);
    info = bytes != 0 ? malloc(bytes) : NULL;
    if (info == NULL ||
        !GetLogicalProcessorInformationEx(RelationAll, (void *)info, &bytes)) {
        free(info);
        table->is_unread = TRUE;
        return;
    }
    for (at = 0; at < bytes;) {
        const SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *one =
            (const void *)((const u8 *)info + at);

        if (one->Size == 0) {
            break;
        }
        if (one->Relationship == RelationProcessorCore) {
            const PROCESSOR_RELATIONSHIP *core = &one->Processor;
            WORD i;

            for (i = 0; i < core->GroupCount; i++) {
                port_AddGroupMask(&table->all, &core->GroupMask[i]);
            }
            if (!is_class[CORE_EFFICIENCY(core)]) {
                is_class[CORE_EFFICIENCY(core)] = TRUE;
                table->kinds++;
            }
        } else if (
            one->Relationship == RelationCache && one->Cache.Level == 3 &&
            (one->Cache.Type == CacheUnified || one->Cache.Type == CacheData)) {
            if (table->caches == PORT_CACHES_MAX) {
                table->is_unread = TRUE;
            } else {
                table->kb[table->caches] = one->Cache.CacheSize / KB;
                port_AddGroupMask(
                    &table->on[table->caches], &one->Cache.GroupMask);
                table->caches++;
            }
        }
        at += one->Size;
    }
    free(info);
}

/* The calling thread to the processors preferred: FALSE where they are
 * not all of one processor group, or none of them is the process's. */
static s32 port_PreferCpus(const PortCpuSet *set) {
    GROUP_AFFINITY affinity;
    DWORD_PTR process_mask;
    DWORD_PTR system_mask;
    s32 group = -1;
    s32 cpu;

    memset(&affinity, 0, sizeof(affinity));
    for (cpu = 0; cpu < PORT_CPUS_MAX; cpu++) {
        if (!port_HasCpu(set, cpu)) {
            continue;
        }
        if (group >= 0 && cpu / CPUS_PER_GROUP != group) {
            return FALSE;
        }
        group = cpu / CPUS_PER_GROUP;
        affinity.Mask |= (KAFFINITY)1 << (cpu % CPUS_PER_GROUP);
    }
    if (group < 0) {
        return FALSE;
    }
    affinity.Group = (WORD)group;
    if (group == 0 && GetProcessAffinityMask(
                          GetCurrentProcess(), &process_mask, &system_mask)) {
        affinity.Mask &= process_mask;
    }
    if (affinity.Mask == 0) {
        return FALSE;
    }
    /* the affinity, not only an ideal processor: an ideal processor
     * alone was measured no better than nothing (docs/PORT.md) */
    return SetThreadGroupAffinity(GetCurrentThread(), &affinity, NULL) != 0;
}

#else /* Linux */

#define CPU_DIR "/sys/devices/system/cpu/"
#define TEXT_BYTES 256
#define INDEXES_MAX 10

/* A file's text, its first TEXT_BYTES - 1 bytes; FALSE where it cannot
 * be read. */
static s32 port_ReadText(const char *path, char *out) {
    FILE *file = fopen(path, "r");
    size_t count;

    if (file == NULL) {
        return FALSE;
    }
    count = fread(out, 1, TEXT_BYTES - 1, file);
    fclose(file);
    out[count] = 0;
    return count != 0;
}

static s32 port_IsSameSet(const PortCpuSet *a, const PortCpuSet *b) {
    return memcmp(a, b, sizeof(*a)) == 0;
}

/* The L3 of processor `cpu`: its size and processors, from the index of
 * its caches whose level is 3. FALSE where it has none that can be
 * read. */
static s32 port_ReadCpuCache(s32 cpu, u32 *kb, PortCpuSet *on) {
    char path[TEXT_BYTES];
    char text[TEXT_BYTES];
    s32 index;

    for (index = 0; index < INDEXES_MAX; index++) {
        snprintf(path, sizeof(path), CPU_DIR "cpu%d/cache/index%d/level", cpu,
                 index);
        if (!port_ReadText(path, text)) {
            break;
        }
        if (atoi(text) != 3) {
            continue;
        }
        snprintf(
            path, sizeof(path), CPU_DIR "cpu%d/cache/index%d/type", cpu, index);
        if (port_ReadText(path, text) &&
            strncmp(text, "Instruction", 11) == 0) {
            continue;
        }
        snprintf(
            path, sizeof(path), CPU_DIR "cpu%d/cache/index%d/size", cpu, index);
        if (!port_ReadText(path, text)) {
            return FALSE;
        }
        *kb = port_ReadCacheSize(text);
        snprintf(path, sizeof(path),
                 CPU_DIR "cpu%d/cache/index%d/shared_cpu_list", cpu, index);
        return *kb != 0 && port_ReadText(path, text) &&
               port_ReadCpuList(text, on);
    }
    return FALSE;
}

static void port_ReadCaches(PortCacheTable *table) {
    char text[TEXT_BYTES];
    char path[TEXT_BYTES];
    s32 capacity = -1;
    s32 cpu;

    if (!port_ReadText(CPU_DIR "online", text) ||
        !port_ReadCpuList(text, &table->all)) {
        table->is_unread = TRUE;
        return;
    }
    table->kinds = 1;
    /* Intel's hybrid processors: a cpu_atom beside the cpu_core */
    if (port_ReadText("/sys/devices/cpu_atom/cpus", text)) {
        table->kinds = 2;
    }
    for (cpu = 0; cpu < PORT_CPUS_MAX; cpu++) {
        PortCpuSet on;
        u32 kb;
        s32 i;

        if (!port_HasCpu(&table->all, cpu)) {
            continue;
        }
        /* ARM's big and little cores: capacities that differ */
        snprintf(path, sizeof(path), CPU_DIR "cpu%d/cpu_capacity", cpu);
        if (port_ReadText(path, text)) {
            if (capacity >= 0 && atoi(text) != capacity) {
                table->kinds = 2;
            }
            capacity = atoi(text);
        }
        memset(&on, 0, sizeof(on));
        if (!port_ReadCpuCache(cpu, &kb, &on)) {
            table->is_unread = TRUE;
            return;
        }
        for (i = 0; i < table->caches; i++) {
            if (port_IsSameSet(&table->on[i], &on)) {
                break;
            }
        }
        if (i < table->caches) {
            continue;
        }
        if (table->caches == PORT_CACHES_MAX) {
            table->is_unread = TRUE;
            return;
        }
        table->kb[table->caches] = kb;
        table->on[table->caches] = on;
        table->caches++;
    }
}

static s32 port_PreferCpus(const PortCpuSet *set) {
    cpu_set_t mask;
    cpu_set_t allowed;
    s32 count = 0;
    s32 cpu;

    CPU_ZERO(&mask);
    if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) {
        CPU_ZERO(&allowed);
    }
    for (cpu = 0; cpu < PORT_CPUS_MAX && cpu < CPU_SETSIZE; cpu++) {
        if (port_HasCpu(set, cpu) && CPU_ISSET(cpu, &allowed)) {
            CPU_SET(cpu, &mask);
            count++;
        }
    }
    return count != 0 &&
           pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask) == 0;
}

#endif

void port_CpuPreferStart(s32 setting) {
    PortCacheTable table;
    char list[PORT_CPU_LIST_BYTES];
    char said[PORT_CPU_LIST_BYTES * 2];
    s32 choice;
    s32 i;

    g_port_cpus_preferring = FALSE;
    if (setting == PORT_CPU_PREFER_OFF) {
        port_Note("cpu: cpu_prefer off, nothing asked");
        return;
    }
    memset(&table, 0, sizeof(table));
    port_ReadCaches(&table);
    choice = port_ChooseCpus(&table, &g_port_cpus_preferred);
    said[0] = 0;
    for (i = 0; i < table.caches && strlen(said) < PORT_CPU_LIST_BYTES; i++) {
        port_FormatCpuList(&table.on[i], list);
        snprintf(said + strlen(said), sizeof(said) - strlen(said),
                 "%s%u MB on %s", i == 0 ? "" : ", ",
                 (unsigned)(table.kb[i] / KB), list);
    }
    switch (choice) {
    case PORT_CPUS_PREFER:
        g_port_cpus_preferring = TRUE;
        port_FormatCpuList(&g_port_cpus_preferred, list);
        port_Note(
            "cpu: L3 %s; preferring %s for the step and the software GPU's painters",
            said, list);
        break;
    case PORT_CPUS_HYBRID:
        port_Note("cpu: L3 %s; cores of %d kinds, left to the system", said,
                  (int)table.kinds);
        break;
    case PORT_CPUS_ONE_CACHE:
        port_Note("cpu: one L3 (%s), nothing done", said);
        break;
    case PORT_CPUS_ONE_SIZE:
        port_Note("cpu: L3 %s, none clearly larger, nothing done", said);
        break;
    default:
        port_Note("cpu: the caches could not be read, nothing done");
        break;
    }
}

s32 port_CpuPreferHere(void) {
    if (!g_port_cpus_preferring) {
        return PORT_CPU_ASKED_NOTHING;
    }
    return port_PreferCpus(&g_port_cpus_preferred)
               ? PORT_CPU_ASKED
               : PORT_CPU_REFUSED;
}

void port_CpuPreferSay(s32 asked, const char *thread) {
    if (asked == PORT_CPU_ASKED) {
        port_Note("cpu: the %s's thread runs on them", thread);
    } else if (asked == PORT_CPU_REFUSED) {
        port_Note("cpu: the %s's thread could not be given them", thread);
    }
}
