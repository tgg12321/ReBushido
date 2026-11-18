/* The window's thread sampled (--sample FILE; docs/PORT.md, "Stalls"):
 * a thread of its own stops the window's thread about a thousand times a
 * second, keeps where it was (the program counter and the return
 * addresses above it), the blank it was in and the part of
 * port_profile.h it was counting to, and lets it go on. At the end the
 * samples are written to FILE with the modules their addresses are in;
 * tools/port_stalls.py names the functions, so that a blank over the
 * budget says not only its part but what in it took the time.
 *
 * Nothing is allocated, locked or written while the window's thread is
 * stopped: it may hold the heap's lock or a file's, and the sampler
 * would then wait on it for ever. The samples go into an array made at
 * the start; a full array keeps the first ones.
 *
 * Windows, 64 bits only: the return addresses are found by the system's
 * own unwinder (RtlVirtualUnwind), which reads each function's unwind
 * information, the driver's as well as the program's. Elsewhere --sample
 * says it cannot and the run goes on. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN64)
#include <windows.h>

#include <tlhelp32.h>
#endif

#include "common.h"
#include "port_profile.h"
#include "sdl_window.h"

/* The blank whose time is being counted now (port_NoteProfile's next):
 * written by the window's thread, read by the sampler's. */
volatile u32 g_port_sample_blank;

#if defined(_WIN64)

/* The program counter and the return addresses above it, innermost
 * first. */
#define SAMPLE_DEPTH 24
/* Samples at most: about 80 MB. */
#define SAMPLES_MAX 400000
/* A thousand a second, from a timer of high resolution where Windows has
 * one (10, 1803 on). */
#define SAMPLE_PERIOD_100NS (-10000LL)
#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

typedef struct {
    u32 blank;
    /* 0: the window's thread; else another thread's id, sampled while
     * the window's has been waiting outside the program */
    u32 thread;
    u16 part;
    u16 depth;
    DWORD64 pc[SAMPLE_DEPTH];
} PortSample;

static PortSample *g_port_samples;
static volatile LONG g_port_sample_count;
static volatile LONG g_port_sample_stop;
static HANDLE g_port_sample_target;
static HANDLE g_port_sample_thread;
static const char *g_port_sample_path;
/* The program's own code, from its first byte to its last. */
static DWORD64 g_port_sample_own_from;
static DWORD64 g_port_sample_own_to;
/* After this many samples in a row of the window's thread outside the
 * program (in the driver, in the system: a wait), the other threads are
 * sampled too, every fourth sample: what the wait is for. */
#define STUCK_SAMPLES 3

/* One sample of the stopped thread: its context unwound frame by frame.
 * A frame with no unwind information is a leaf, whose return address is
 * at the top of its stack. */
static void port_TakeSample(PortSample *sample, CONTEXT *context) {
    s32 depth = 0;

    while (depth < SAMPLE_DEPTH && context->Rip != 0) {
        DWORD64 base = 0;
        DWORD64 frame = 0;
        void *handler = NULL;
        PRUNTIME_FUNCTION function;

        sample->pc[depth++] = context->Rip;
        function = RtlLookupFunctionEntry(context->Rip, &base, NULL);
        if (function == NULL) {
            /* The game's own files are compiled with no unwind
             * information: past one of them the stack cannot be read,
             * and what it calls was found already. Only the innermost
             * frame can be a leaf that keeps its return address on top. */
            if (depth > 1 || context->Rsp == 0 || (context->Rsp & 7) != 0 ||
                (context->Rip >= g_port_sample_own_from &&
                 context->Rip < g_port_sample_own_to)) {
                break;
            }
            context->Rip = *(DWORD64 *)context->Rsp;
            context->Rsp += 8;
        } else {
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, base, context->Rip, function,
                             context, &handler, &frame, NULL);
        }
    }
    sample->depth = (u16)depth;
}

/* The program's other threads (the driver's, SDL's, the helpers'),
 * listed again at most once a second: listing the system's threads
 * takes milliseconds. */
#define OTHERS_MAX 64
#define OTHERS_LISTED_EVERY 1000
static HANDLE g_port_sample_others[OTHERS_MAX];
static DWORD g_port_sample_other_ids[OTHERS_MAX];
static s32 g_port_sample_other_count;

static void port_ListOthers(void) {
    HANDLE snapshot;
    DWORD process = GetCurrentProcessId();
    DWORD self = GetCurrentThreadId();
    DWORD target = GetThreadId(g_port_sample_target);
    THREADENTRY32 entry;
    s32 i;

    for (i = 0; i < g_port_sample_other_count; i++) {
        CloseHandle(g_port_sample_others[i]);
    }
    g_port_sample_other_count = 0;
    snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return;
    }
    entry.dwSize = sizeof(entry);
    if (Thread32First(snapshot, &entry)) {
        do {
            HANDLE thread;

            if (entry.th32OwnerProcessID != process ||
                entry.th32ThreadID == self || entry.th32ThreadID == target ||
                g_port_sample_other_count >= OTHERS_MAX) {
                continue;
            }
            thread = OpenThread(THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                                    THREAD_QUERY_INFORMATION,
                                FALSE, entry.th32ThreadID);
            if (thread != NULL) {
                g_port_sample_others[g_port_sample_other_count] = thread;
                g_port_sample_other_ids[g_port_sample_other_count++] =
                    entry.th32ThreadID;
            }
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
}

/* One sample of every other thread of the program: each stopped in
 * turn. */
static LONG port_SampleOthers(LONG at, u32 blank) {
    s32 i;

    for (i = 0; i < g_port_sample_other_count && at < SAMPLES_MAX; i++) {
        HANDLE thread = g_port_sample_others[i];
        CONTEXT context;

        if (SuspendThread(thread) == (DWORD)-1) {
            continue;
        }
        memset(&context, 0, sizeof(context));
        context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        if (GetThreadContext(thread, &context)) {
            PortSample *sample = &g_port_samples[at++];

            sample->blank = blank;
            sample->thread = g_port_sample_other_ids[i];
            sample->part = 0;
            port_TakeSample(sample, &context);
        }
        ResumeThread(thread);
    }
    return at;
}

static DWORD WINAPI port_SampleLoop(void *unused) {
    HANDLE timer;
    LARGE_INTEGER due;
    s32 stuck = 0;
    u32 ticks = 0;
    u32 listed_at = 0;

    (void)unused;
    timer = CreateWaitableTimerExW(
        NULL, NULL, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
    if (timer == NULL) {
        timer = CreateWaitableTimerW(NULL, FALSE, NULL);
    }
    due.QuadPart = SAMPLE_PERIOD_100NS;
    SetWaitableTimer(timer, &due, 1, NULL, NULL, FALSE);
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
    while (!g_port_sample_stop) {
        CONTEXT context;
        LONG at;
        s32 is_outside = FALSE;

        WaitForSingleObject(timer, INFINITE);
        ticks++;
        at = g_port_sample_count;
        if (at >= SAMPLES_MAX) {
            break;
        }
        if (SuspendThread(g_port_sample_target) == (DWORD)-1) {
            continue;
        }
        memset(&context, 0, sizeof(context));
        context.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
        /* (GetThreadContext also waits for the stop to be done) */
        if (GetThreadContext(g_port_sample_target, &context)) {
            PortSample *sample = &g_port_samples[at];

            sample->blank = g_port_sample_blank;
            sample->thread = 0;
            sample->part = (u16)port_ProfilePart();
            is_outside = context.Rip < g_port_sample_own_from ||
                         context.Rip >= g_port_sample_own_to;
            port_TakeSample(sample, &context);
            at++;
        }
        ResumeThread(g_port_sample_target);
        stuck = is_outside ? stuck + 1 : 0;
        if (stuck >= STUCK_SAMPLES && stuck % 4 == STUCK_SAMPLES % 4) {
            if (listed_at == 0 || ticks - listed_at >= OTHERS_LISTED_EVERY) {
                port_ListOthers();
                listed_at = ticks;
            }
            at = port_SampleOthers(at, g_port_sample_blank);
        }
        g_port_sample_count = at;
    }
    CloseHandle(timer);
    return 0;
}

void port_SampleStart(const char *path) {
    g_port_samples = calloc(SAMPLES_MAX, sizeof(PortSample));
    if (g_port_samples == NULL ||
        !DuplicateHandle(GetCurrentProcess(), GetCurrentThread(),
                         GetCurrentProcess(), &g_port_sample_target,
                         THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT |
                             THREAD_QUERY_INFORMATION,
                         FALSE, 0)) {
        port_Say("--sample: no room for the samples");
        return;
    }
    g_port_sample_path = path;
    {
        u8 *own = (u8 *)GetModuleHandleA(NULL);
        IMAGE_NT_HEADERS *nt =
            (IMAGE_NT_HEADERS *)(own + ((IMAGE_DOS_HEADER *)own)->e_lfanew);

        g_port_sample_own_from = (DWORD64)(uintptr_t)own;
        g_port_sample_own_to =
            g_port_sample_own_from + nt->OptionalHeader.SizeOfImage;
    }
    g_port_sample_thread =
        CreateThread(NULL, 0, port_SampleLoop, NULL, 0, NULL);
    if (g_port_sample_thread == NULL) {
        port_Say("--sample: no thread to sample with");
        return;
    }
    port_Note("sample: the window's thread, a thousand times a second, "
              "into %s",
              path);
}

/* The module an address is in, written once as `m BASE SIZE PATH`. */
#define MODULES_MAX 256
static HMODULE g_port_sample_modules[MODULES_MAX];
static s32 g_port_sample_module_count;

static void port_NoteModule(FILE *file, DWORD64 pc) {
    HMODULE module = NULL;
    IMAGE_DOS_HEADER *dos;
    IMAGE_NT_HEADERS *nt;
    char path[MAX_PATH];
    s32 i;

    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (const char *)(uintptr_t)pc, &module) ||
        module == NULL) {
        return;
    }
    for (i = 0; i < g_port_sample_module_count; i++) {
        if (g_port_sample_modules[i] == module) {
            return;
        }
    }
    if (g_port_sample_module_count >= MODULES_MAX) {
        return;
    }
    g_port_sample_modules[g_port_sample_module_count++] = module;
    dos = (IMAGE_DOS_HEADER *)module;
    nt = (IMAGE_NT_HEADERS *)((u8 *)module + dos->e_lfanew);
    if (GetModuleFileNameA(module, path, sizeof(path)) == 0) {
        strcpy(path, "?");
    }
    fprintf(file, "m %llx %lx %s\n", (unsigned long long)(uintptr_t)module,
            (unsigned long)nt->OptionalHeader.SizeOfImage, path);
}

void port_SampleEnd(void) {
    FILE *file;
    LONG count;
    LONG i;
    s32 d;

    if (g_port_sample_thread == NULL) {
        return;
    }
    g_port_sample_stop = 1;
    WaitForSingleObject(g_port_sample_thread, 1000);
    CloseHandle(g_port_sample_thread);
    g_port_sample_thread = NULL;
    count = g_port_sample_count;
    file = fopen(g_port_sample_path, "w");
    if (file == NULL) {
        port_Say("--sample: %s cannot be made", g_port_sample_path);
        return;
    }
    fprintf(file, "# the window's thread sampled (sampler.c): "
                  "s BLANK PART PC RETURN..., and t BLANK THREAD PC "
                  "RETURN...\n");
    fprintf(file, "parts");
    for (d = 0; d < PORT_PROFILE_PARTS; d++) {
        fprintf(file, " %s", port_ProfileName(d));
    }
    fputc('\n', file);
    for (i = 0; i < count; i++) {
        for (d = 0; d < g_port_samples[i].depth; d++) {
            port_NoteModule(file, g_port_samples[i].pc[d]);
        }
    }
    for (i = 0; i < count; i++) {
        const PortSample *sample = &g_port_samples[i];

        if (sample->thread != 0) {
            fprintf(file, "t %u %u", sample->blank, sample->thread);
        } else {
            fprintf(file, "s %u %u", sample->blank, sample->part);
        }
        for (d = 0; d < sample->depth; d++) {
            fprintf(file, " %llx", (unsigned long long)sample->pc[d]);
        }
        fputc('\n', file);
    }
    fclose(file);
    port_Note("sample: %ld samples written to %s%s", (long)count,
              g_port_sample_path,
              count >= SAMPLES_MAX ? " (the array was full)" : "");
}

#else

void port_SampleStart(const char *path) {
    (void)path;
    port_Say("--sample: only the 64-bit Windows program samples its "
             "thread; the run goes on without");
}

void port_SampleEnd(void) {}

#endif
