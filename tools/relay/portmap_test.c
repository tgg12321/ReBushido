/* The window's port mapping (src/port/window/portmap.c) driven against
 * tools/relay/fake_router.py, which tools/relay/test_portmap.py starts:
 * the port mapped, the lease renewed if asked, the mapping taken away
 * twice. Built with the host's compiler beside portmap.c alone:
 *
 *   portmap_test --ssdp A.B.C.D:P --gateway A.B.C.D:P [--port N]
 *                [--wait MS] [--renew]
 *
 * It prints one line for what port_MapPort gave (`map ok=1 method=PCP
 * external=127.0.0.1:7845 lease=7200 took=12 why=`), `renewing` while
 * the renewal's ticks run, and `unmapped`. */
#ifdef _WIN32
#include <windows.h>
#else
#include <time.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "portmap.h"

#define TEST_PORT 7845
#define TEST_RENEW_MS 1500
#define TEST_TICK_MS 20

static u32 test_Now(void) {
#ifdef _WIN32
    return (u32)GetTickCount();
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (u32)now.tv_sec * 1000 + (u32)(now.tv_nsec / 1000000);
#endif
}

static void test_Sleep(u32 ms) {
#ifdef _WIN32
    Sleep(ms);
#else
    struct timespec wait;
    wait.tv_sec = ms / 1000;
    wait.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&wait, NULL);
#endif
}

s32 main(s32 argc, char **argv) {
    PortMapOptions options;
    PortMapResult result;
    u32 port = TEST_PORT;
    s32 renew = 0;
    s32 i;
    u32 start;
    u32 took;
    s32 ok;
    memset(&options, 0, sizeof(options));
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--ssdp") == 0 && i + 1 < argc) {
            options.ssdp_at = argv[++i];
        } else if (strcmp(argv[i], "--gateway") == 0 && i + 1 < argc) {
            options.gateway_at = argv[++i];
        } else if (strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            port = (u32)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--wait") == 0 && i + 1 < argc) {
            options.wait_ms = (u32)atoi(argv[++i]);
        } else if (strcmp(argv[i], "--renew") == 0) {
            renew = 1;
        } else {
            fprintf(stderr, "portmap_test: what is %s?\n", argv[i]);
            return 2;
        }
    }
    start = test_Now();
    ok = port_MapPort((u16)port, &options, &result);
    took = test_Now() - start;
    printf("map ok=%d method=%s external=%u.%u.%u.%u:%u lease=%u took=%u "
           "why=%s\n",
           ok, result.method, result.external_address >> 24,
           result.external_address >> 16 & 0xFF,
           result.external_address >> 8 & 0xFF, result.external_address & 0xFF,
           result.external_port, result.lease_seconds, took, result.why);
    fflush(stdout);
    if (ok != result.ok || (ok && result.why[0] != '\0') ||
        (!ok && result.why[0] == '\0')) {
        printf("FAILED: ok and why disagree\n");
        return 1;
    }
    if (renew) {
        /* the caller's clock, any: a first tick, then one past half the
         * lease, then ticks while the renewal's answer comes */
        u32 clock = 1000;
        u32 until = test_Now() + TEST_RENEW_MS;
        printf("renewing\n");
        fflush(stdout);
        port_PortMapTick(clock);
        clock += result.lease_seconds * 500 + 1;
        while ((s32)(until - test_Now()) > 0) {
            port_PortMapTick(clock);
            test_Sleep(TEST_TICK_MS);
            clock += TEST_TICK_MS;
        }
    }
    port_UnmapPort();
    port_UnmapPort();
    printf("unmapped\n");
    return 0;
}
