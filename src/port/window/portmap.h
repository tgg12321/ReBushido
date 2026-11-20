/* The host's port opened in its router, by the router's own protocols
 * (portmap.c; docs/NETPLAY.md, "The ladder"): PCP, NAT-PMP, and UPnP's
 * Internet Gateway Device, tried in that order, each with a lease and
 * taken away again when the program ends. Only the window's build has
 * it: it speaks HTTP to the router, through the C library's sockets. */
#ifndef PORT_PORTMAP_H
#define PORT_PORTMAP_H

#include "common.h"

#define PORT_MAP_METHOD_BYTES 16
#define PORT_MAP_WHY_BYTES 128
/* The lease asked for, and how long before its end it is asked again. */
#define PORT_MAP_LEASE_SECONDS 7200

typedef struct {
    s32 ok;                            /* 1 if a mapping is in place */
    char method[PORT_MAP_METHOD_BYTES]; /* "UPnP", "NAT-PMP", "PCP" or "" */
    u32 external_address;              /* IPv4, the host's own order */
    u16 external_port;
    u32 lease_seconds; /* as the router granted it; 0: for ever */
    char why[PORT_MAP_WHY_BYTES]; /* not ok: why, in a player's words */
} PortMapResult;

typedef struct {
    /* For tests: `A.B.C.D:PORT` that M-SEARCH goes to instead of
     * 239.255.255.250:1900, and the PCP / NAT-PMP server instead of the
     * gateway found; NULL for the real ones. */
    const char *ssdp_at;
    const char *gateway_at;
    /* All three tried within this many milliseconds (0: 4000). */
    u32 wait_ms;
} PortMapOptions;

/* UDP port `port` of this PC mapped to the same port outside, if the
 * router lets it (else to the one it gives): 1 and `out` filled, or 0
 * with `out->why`. */
extern s32 port_MapPort(
    u16 port, const PortMapOptions *options, PortMapResult *out);
/* The lease renewed when half of it has gone: call it now and then
 * (every few seconds is enough); `now_ms` is any clock in ms. */
extern void port_PortMapTick(u32 now_ms);
/* What port_MapPort made, taken away (UPnP DeletePortMapping, PCP and
 * NAT-PMP with a lifetime of 0). Safe to call twice or with none. */
extern void port_UnmapPort(void);

#endif /* PORT_PORTMAP_H */
