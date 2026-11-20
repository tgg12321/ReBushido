/* The host's UDP port opened in its router (portmap.h; docs/NETPLAY.md,
 * "The ladder"), by the three protocols routers speak for it, in this
 * order:
 *
 *   - PCP (RFC 6887): a MAP request to the default gateway's port 5351;
 *   - NAT-PMP (RFC 6886), PCP's older form on the same port: the external
 *     address (opcode 0), then a UDP mapping (opcode 1). A router that
 *     speaks only NAT-PMP answers PCP's request with "unsupported
 *     version", which sends us straight on to it;
 *   - UPnP's Internet Gateway Device: an SSDP M-SEARCH (sent first of
 *     all, so that its answers gather while the other two are tried), the
 *     device's description fetched over HTTP, its WANIPConnection or
 *     WANPPPConnection service's control URL, and SOAP's
 *     GetExternalIPAddress and AddPortMapping (with a lease of 0 when the
 *     router takes only permanent ones, error 725).
 *
 * Everything is done within the caller's wait, on sockets that never
 * block. What was made is remembered: port_PortMapTick renews it when half
 * its lease has gone, without blocking (a few bytes sent and read on each
 * call), and port_UnmapPort takes it away. Only the window's build has
 * this file; it talks to the C library's sockets, POSIX on Linux and
 * Winsock on Windows, where the default gateway comes from iphlpapi.dll,
 * loaded when it is wanted so that nothing more is linked to. */
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#else
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#endif
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "portmap.h"

/* ---- what is asked and how long it is waited for ---- */

#define PORTMAP_WAIT_MS 4000
/* A datagram is sent again after this, then twice as long each time
 * (RFC 6886, 3.1). */
#define PORTMAP_RESEND_MS 250
/* The parts of the wait that PCP, and PCP with NAT-PMP, may take, in
 * eighths of it; UPnP's search gathers until the last part, which is
 * left for its HTTP. */
#define PORTMAP_PCP_EIGHTHS 2
#define PORTMAP_PMP_EIGHTHS 4
#define PORTMAP_PMP_SILENT_EIGHTHS 3
#define PORTMAP_SEARCH_EIGHTHS 6
#define PORTMAP_EIGHTHS 8
/* The search is sent again when nothing has answered after this. */
#define PORTMAP_SEARCH_AGAIN_MS 1000
/* How long a removal is waited for at the end. */
#define PORTMAP_UNMAP_MS 800
#define PORTMAP_UNMAP_HTTP_MS 1500
/* A renewal: sent again after this, given up after this, and tried again
 * this long after giving up. */
#define PORTMAP_RENEW_RESEND_MS 2000
#define PORTMAP_RENEW_GIVE_UP_MS 10000
#define PORTMAP_RENEW_RETRY_MS 30000
/* At most this much of the lease, in ms, before it is renewed. */
#define PORTMAP_HALF_LEASE_MAX_MS 2000000000U
#define MS_PER_SECOND 1000
/* The external ports tried after the one asked for, where UPnP says it
 * is another PC's (error 718). */
#define PORTMAP_OTHER_PORTS 3

/* ---- the protocols' numbers ---- */

#define PCP_PORT 5351
#define PCP_VERSION 2
#define PCP_OPCODE_MAP 1
#define PCP_ANSWER 0x80
#define PCP_PROTOCOL_UDP 17
#define PCP_NONCE_BYTES 12
/* the header, and the header with MAP's own fields */
#define PCP_HEADER_BYTES 24
#define PCP_MAP_BYTES 60
/* where a request's and an answer's fields are */
#define PCP_AT_RESULT 3
#define PCP_AT_LIFETIME 4
#define PCP_AT_CLIENT 8
#define PCP_AT_NONCE 24
#define PCP_AT_PROTOCOL 36
#define PCP_AT_INTERNAL_PORT 40
#define PCP_AT_EXTERNAL_PORT 42
#define PCP_AT_EXTERNAL 44
/* an IPv4 address in PCP's 16 bytes: ::ffff:A.B.C.D */
#define PCP_V4_MARK 10
#define PCP_V4_AT 12

enum {
    PCP_OK = 0,
    PCP_UNSUPP_VERSION = 1,
    PCP_NOT_AUTHORIZED = 2,
    PCP_NO_RESOURCES = 8,
    PCP_ADDRESS_MISMATCH = 12
};

#define PMP_VERSION 0
#define PMP_OPCODE_ADDRESS 0
#define PMP_OPCODE_MAP_UDP 1
#define PMP_ANSWER 0x80
#define PMP_ADDRESS_BYTES 12
#define PMP_MAP_BYTES 12
#define PMP_MAP_ANSWER_BYTES 16
#define PMP_SHORTEST_ANSWER 8

enum {
    PMP_OK = 0,
    PMP_UNSUPPORTED_VERSION = 1,
    PMP_REFUSED = 2,
    PMP_NETWORK_FAILURE = 3,
    PMP_NO_RESOURCES = 4
};

#define SSDP_ADDRESS 0xEFFFFFFAU /* 239.255.255.250 */
#define SSDP_PORT 1900
#define SSDP_TTL 2
#define HTTP_PORT 80
#define HTTP_OK 200

enum {
    UPNP_NOT_AUTHORIZED = 606,
    UPNP_CONFLICT = 718,
    UPNP_SAME_PORTS_ONLY = 724,
    UPNP_PERMANENT_ONLY = 725
};

/* ---- room ---- */

#define PORTMAP_LOCATIONS 4
#define PORTMAP_SERVICES 4
#define PORTMAP_URL_BYTES 256
#define PORTMAP_TYPE_BYTES 96
#define PORTMAP_LINE_BYTES 256
#define PORTMAP_DATAGRAM_BYTES 1500
#define PORTMAP_ARGUMENTS_BYTES 768
#define PORTMAP_ENVELOPE_BYTES 1536
#define PORTMAP_REQUEST_BYTES 2048
#define PORTMAP_ANSWER_BYTES 8192
#define PORTMAP_DESCRIPTION_BYTES 65536
/* the most datagrams read in one go */
#define PORTMAP_DRAIN_MAX 64

/* ---- the platform: sockets, the clock, the default gateway ---- */

#ifdef _WIN32
typedef SOCKET PortMapSocket;
typedef DWORD(WINAPI *PortMapBestRoute)(DWORD, DWORD, PMIB_IPFORWARDROW);
#define PORTMAP_NO_SOCKET INVALID_SOCKET
#define PORTMAP_SEND_FLAGS 0
#else
typedef s32 PortMapSocket;
#define PORTMAP_NO_SOCKET (-1)
#define PORTMAP_SEND_FLAGS MSG_NOSIGNAL
/* the default route's flag in /proc/net/route */
#define PORTMAP_ROUTE_GATEWAY 0x2
#endif

/* What a socket's last call left. */
enum { PORTMAP_FAILED, PORTMAP_AGAIN, PORTMAP_REFUSED };

/* How one protocol's try ended. */
enum {
    PORTMAP_SILENT, /* nothing answered */
    PORTMAP_NOBODY, /* refused: nothing listens there */
    PORTMAP_OTHER,  /* a NAT-PMP router answered PCP */
    PORTMAP_DENIED, /* the router said no (the reason is kept) */
    PORTMAP_MADE
};

enum { METHOD_NONE, METHOD_PCP, METHOD_PMP, METHOD_UPNP };

/* What the reasons are worth: the most telling is told. */
enum {
    WHY_NONE,
    WHY_NOBODY,
    WHY_PCP,
    WHY_PMP,
    WHY_UPNP_SILENT,
    WHY_UPNP,
    WHY_LOCAL
};

typedef struct {
    u32 address;
    u16 port;
    char path[PORTMAP_URL_BYTES];
} PortMapUrl;

typedef struct {
    PortMapUrl control;
    char type[PORTMAP_TYPE_BYTES];
} PortMapService;

typedef struct {
    PortMapSocket socket;
    s32 unicast;
    s32 refused;
    u32 address;
    u16 port;
    s32 count;
    char locations[PORTMAP_LOCATIONS][PORTMAP_URL_BYTES];
} PortMapSearch;

/* What is made, and its renewal on the caller's clock. */
static struct {
    s32 method;
    u16 internal_port;
    u16 external_port;
    u32 external_address;
    u32 lease;
    u32 server; /* PCP's or NAT-PMP's */
    u16 server_port;
    u32 client; /* this PC's address toward the router */
    u8 nonce[PCP_NONCE_BYTES];
    PortMapService service;

    s32 clock_set;
    u32 granted_at;
    s32 retry_set;
    u32 retry_at;
    s32 renewing;
    PortMapSocket socket;
    u32 renew_started;
    u32 renew_sent;
    s32 connected;
    u32 request_bytes;
    u32 sent;
    u32 got;
    char request[PORTMAP_REQUEST_BYTES];
    char answer[PORTMAP_ANSWER_BYTES];
} g_portmap;

static char g_portmap_description[PORTMAP_DESCRIPTION_BYTES];
static char g_portmap_soap[PORTMAP_ANSWER_BYTES];

static u32 portmap_Now(void) {
#ifdef _WIN32
    return (u32)GetTickCount();
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (u32)now.tv_sec * MS_PER_SECOND + (u32)(now.tv_nsec / 1000000);
#endif
}

/* TRUE while `now` is before `deadline`, on a clock that wraps. */
static s32 portmap_Before(u32 now, u32 deadline) {
    return (s32)(deadline - now) > 0;
}

/* Windows' sockets started, once (they count their starts, so the rest
 * of the program's do not mind). */
static s32 portmap_Ready(void) {
#ifdef _WIN32
    static s32 started;
    if (!started) {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            return FALSE;
        }
        started = TRUE;
    }
#endif
    return TRUE;
}

static void portmap_Close(PortMapSocket sock) {
    if (sock == PORTMAP_NO_SOCKET) {
        return;
    }
#ifdef _WIN32
    closesocket(sock);
#else
    close(sock);
#endif
}

/* A socket of `type` (SOCK_DGRAM or SOCK_STREAM) that never blocks. */
static PortMapSocket portmap_Open(s32 type) {
    PortMapSocket sock;
    if (!portmap_Ready()) {
        return PORTMAP_NO_SOCKET;
    }
    sock = socket(AF_INET, type, 0);
    if (sock == PORTMAP_NO_SOCKET) {
        return sock;
    }
#ifdef _WIN32
    {
        u_long on = 1;
        if (ioctlsocket(sock, FIONBIO, &on) != 0) {
            closesocket(sock);
            return PORTMAP_NO_SOCKET;
        }
    }
#else
    {
        s32 flags = fcntl(sock, F_GETFL, 0);
        if (flags < 0 || fcntl(sock, F_SETFL, flags | O_NONBLOCK) < 0) {
            close(sock);
            return PORTMAP_NO_SOCKET;
        }
    }
#endif
    return sock;
}

static s32 portmap_LastError(void) {
#ifdef _WIN32
    s32 code = WSAGetLastError();
    if (code == WSAEWOULDBLOCK || code == WSAEINPROGRESS ||
        code == WSAEMSGSIZE || code == WSAEINTR) {
        return PORTMAP_AGAIN;
    }
    if (code == WSAECONNRESET || code == WSAECONNREFUSED) {
        return PORTMAP_REFUSED;
    }
#else
    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINPROGRESS ||
        errno == EINTR) {
        return PORTMAP_AGAIN;
    }
    if (errno == ECONNREFUSED) {
        return PORTMAP_REFUSED;
    }
#endif
    return PORTMAP_FAILED;
}

static void portmap_SocketAddress(
    struct sockaddr_in *at, u32 address, u16 port) {
    memset(at, 0, sizeof(*at));
    at->sin_family = AF_INET;
    at->sin_addr.s_addr = htonl(address);
    at->sin_port = htons(port);
}

/* The socket's other end set (a UDP socket then hears only it, and is
 * told when nothing listens there); TRUE, or FALSE if it failed. A TCP
 * socket's connection is only begun. */
static s32 portmap_Connect(PortMapSocket sock, u32 address, u16 port) {
    struct sockaddr_in at;
    portmap_SocketAddress(&at, address, port);
    if (connect(sock, (struct sockaddr *)&at, sizeof(at)) == 0) {
        return TRUE;
    }
    return portmap_LastError() == PORTMAP_AGAIN;
}

/* Up to `ms` waited for the socket to be readable (or writable, or to
 * have failed to connect): TRUE when it is. */
static s32 portmap_Wait(PortMapSocket sock, s32 writing, u32 ms) {
    fd_set ready;
    fd_set failed;
    struct timeval wait;
#ifndef _WIN32
    if (sock >= FD_SETSIZE) {
        return FALSE;
    }
#endif
    FD_ZERO(&ready);
    FD_ZERO(&failed);
    FD_SET(sock, &ready);
    FD_SET(sock, &failed);
    wait.tv_sec = ms / MS_PER_SECOND;
    wait.tv_usec = (ms % MS_PER_SECOND) * 1000;
    return select((s32)sock + 1, writing ? NULL : &ready,
                  writing ? &ready : NULL, writing ? &failed : NULL, &wait) > 0;
}

/* A datagram waiting, without waiting for one: its length, 0 when none,
 * -1 when nothing listens at the other end or the socket failed. */
static s32 portmap_Receive(PortMapSocket sock, u8 *bytes, u32 room) {
    s32 got = (s32)recv(sock, (char *)bytes, (s32)room, 0);
    if (got >= 0) {
        return got;
    }
    return portmap_LastError() == PORTMAP_AGAIN ? 0 : -1;
}

/* This PC's address on the connection `sock` has (0: none). */
static u32 portmap_SocketLocal(PortMapSocket sock) {
    struct sockaddr_in at;
    socklen_t bytes = sizeof(at);
    memset(&at, 0, sizeof(at));
    if (getsockname(sock, (struct sockaddr *)&at, &bytes) != 0) {
        return 0;
    }
    return ntohl(at.sin_addr.s_addr);
}

/* This PC's address toward `address`, as the routing table chooses it: a
 * UDP socket's connection sends nothing (0: none). */
static u32 portmap_LocalToward(u32 address, u16 port) {
    PortMapSocket sock = portmap_Open(SOCK_DGRAM);
    u32 local = 0;
    if (sock == PORTMAP_NO_SOCKET) {
        return 0;
    }
    if (portmap_Connect(sock, address, port)) {
        local = portmap_SocketLocal(sock);
    }
    portmap_Close(sock);
    return local;
}

/* The default gateway's address (0: none found): Windows' best route to
 * the Internet, or Linux's default route. */
static u32 portmap_Gateway(void) {
    u32 gateway = 0;
#ifdef _WIN32
    HMODULE library = LoadLibraryA("iphlpapi.dll");
    PortMapBestRoute best_route;
    MIB_IPFORWARDROW row;
    if (library == NULL) {
        return 0;
    }
    /* (a function's pointer through void (*)(void), which may be cast
     * to any other) */
    best_route = (PortMapBestRoute)(void (*)(void))GetProcAddress(
        library, "GetBestRoute");
    memset(&row, 0, sizeof(row));
    /* any address outside: 8.8.8.8 */
    if (best_route != NULL &&
        best_route(htonl(0x08080808U), 0, &row) == NO_ERROR) {
        gateway = ntohl(row.dwForwardNextHop);
    }
    FreeLibrary(library);
#else
    char line[PORTMAP_LINE_BYTES];
    FILE *file = fopen("/proc/net/route", "r");
    if (file == NULL) {
        return 0;
    }
    /* Iface Destination Gateway Flags ..., the addresses in the bytes'
     * own order, written as numbers */
    while (fgets(line, sizeof(line), file) != NULL) {
        char name[64];
        u32 destination;
        u32 through;
        u32 flags;
        if (sscanf(line, "%63s %x %x %x", name, &destination, &through,
                   &flags) == 4 &&
            destination == 0 && (flags & PORTMAP_ROUTE_GATEWAY) &&
            through != 0) {
            gateway = ntohl(through);
            break;
        }
    }
    fclose(file);
#endif
    if (gateway == 0xFFFFFFFFU) {
        return 0;
    }
    return gateway;
}

/* ---- text, bounded ---- */

static s32 portmap_Lower(s32 c) {
    return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c;
}

/* TRUE if the `count` characters at `a` are those at `b`, whatever their
 * case. */
static s32 portmap_SameText(const char *a, const char *b, u32 count) {
    u32 i;
    for (i = 0; i < count; i++) {
        if (portmap_Lower((u8)a[i]) != portmap_Lower((u8)b[i])) {
            return FALSE;
        }
        if (a[i] == '\0') {
            return TRUE;
        }
    }
    return TRUE;
}

/* TRUE if `text` has `part` in it, whatever its case. */
static s32 portmap_Contains(const char *text, const char *part) {
    u32 count = (u32)strlen(part);
    for (; *text != '\0'; text++) {
        if (portmap_SameText(text, part, count)) {
            return TRUE;
        }
    }
    return FALSE;
}

static s32 portmap_Space(s32 c) {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* [from, end) copied into `out` (room bytes, ended), its white space
 * trimmed; TRUE if it fitted. */
static s32 portmap_CopyTrimmed(
    const char *from, const char *end, char *out, u32 room) {
    while (from < end && portmap_Space((u8)*from)) {
        from++;
    }
    while (end > from && portmap_Space((u8)end[-1])) {
        end--;
    }
    out[0] = '\0';
    if ((u32)(end - from) >= room) {
        return FALSE;
    }
    memcpy(out, from, (u32)(end - from));
    out[end - from] = '\0';
    return TRUE;
}

/* The decimal number at `*text`, of at most `most`: TRUE with `*text`
 * moved past it, FALSE if there is none or it is larger. */
static s32 portmap_Number(const char **text, u32 most, u32 *value) {
    const char *at = *text;
    u32 number = 0;
    if (*at < '0' || *at > '9') {
        return FALSE;
    }
    while (*at >= '0' && *at <= '9') {
        number = number * 10 + (u32)(*at - '0');
        if (number > most) {
            return FALSE;
        }
        at++;
    }
    *value = number;
    *text = at;
    return TRUE;
}

/* A dotted IPv4 address at `*text`, in the host's order: TRUE with
 * `*text` moved past it. */
static s32 portmap_Address(const char **text, u32 *address) {
    const char *at = *text;
    u32 value = 0;
    s32 i;
    for (i = 0; i < 4; i++) {
        u32 part;
        if (i > 0) {
            if (*at != '.') {
                return FALSE;
            }
            at++;
        }
        if (!portmap_Number(&at, 255, &part)) {
            return FALSE;
        }
        value = value << 8 | part;
    }
    *address = value;
    *text = at;
    return TRUE;
}

/* `A.B.C.D:PORT`: TRUE and both filled. */
static s32 portmap_ParseAt(const char *text, u32 *address, u16 *port) {
    u32 number;
    if (text == NULL || !portmap_Address(&text, address) || *text != ':') {
        return FALSE;
    }
    text++;
    if (!portmap_Number(&text, 65535, &number) || number == 0 ||
        *text != '\0') {
        return FALSE;
    }
    *port = (u16)number;
    return TRUE;
}

/* A reason, kept if it says more than the one kept so far. */
static void portmap_Why(
    PortMapResult *out, s32 *kept, s32 worth, const char *format, ...) {
    va_list list;
    if (worth <= *kept) {
        return;
    }
    *kept = worth;
    va_start(list, format);
    vsnprintf(out->why, sizeof(out->why), format, list);
    va_end(list);
}

/* ---- the bytes of PCP and NAT-PMP ---- */

static void portmap_Put16(u8 *at, u32 value) {
    at[0] = (u8)(value >> 8);
    at[1] = (u8)value;
}

static void portmap_Put32(u8 *at, u32 value) {
    at[0] = (u8)(value >> 24);
    at[1] = (u8)(value >> 16);
    at[2] = (u8)(value >> 8);
    at[3] = (u8)value;
}

static u32 portmap_Get16(const u8 *at) { return (u32)at[0] << 8 | at[1]; }

static u32 portmap_Get32(const u8 *at) {
    return (u32)at[0] << 24 | (u32)at[1] << 16 | (u32)at[2] << 8 | at[3];
}

/* An IPv4 address in PCP's form (::ffff:A.B.C.D; 0.0.0.0 for "any"). */
static void portmap_PutPcpAddress(u8 *at, u32 address) {
    memset(at, 0, 16);
    at[PCP_V4_MARK] = 0xFF;
    at[PCP_V4_MARK + 1] = 0xFF;
    portmap_Put32(at + PCP_V4_AT, address);
}

/* A new nonce for PCP's mapping: anything that another program's
 * mapping is unlikely to have. */
static void portmap_NewNonce(void) {
    u32 seed = portmap_Now() ^ (u32)time(NULL) * 2654435761U ^ g_portmap.client;
    s32 i;
    for (i = 0; i < PCP_NONCE_BYTES; i++) {
        seed = seed * 1103515245U + 12345U;
        g_portmap.nonce[i] = (u8)(seed >> 16);
    }
}

/* PCP's MAP request of UDP's internal port, the external one suggested
 * (0: any), for `lifetime` seconds; its length. */
static u32 portmap_PcpRequest(
    u8 *request, u16 internal_port, u16 external_port, u32 lifetime) {
    memset(request, 0, PCP_MAP_BYTES);
    request[0] = PCP_VERSION;
    request[1] = PCP_OPCODE_MAP;
    portmap_Put32(request + PCP_AT_LIFETIME, lifetime);
    portmap_PutPcpAddress(request + PCP_AT_CLIENT, g_portmap.client);
    memcpy(request + PCP_AT_NONCE, g_portmap.nonce, PCP_NONCE_BYTES);
    request[PCP_AT_PROTOCOL] = PCP_PROTOCOL_UDP;
    portmap_Put16(request + PCP_AT_INTERNAL_PORT, internal_port);
    portmap_Put16(request + PCP_AT_EXTERNAL_PORT, external_port);
    portmap_PutPcpAddress(request + PCP_AT_EXTERNAL, 0);
    return PCP_MAP_BYTES;
}

/* TRUE for an answer to our MAP: PCP's with our nonce, or a NAT-PMP
 * router's "unsupported version". */
static s32 portmap_PcpAccept(const u8 *answer, s32 bytes) {
    if (bytes >= 4 && answer[0] == PMP_VERSION &&
        answer[1] == (PMP_ANSWER | PCP_OPCODE_MAP)) {
        return TRUE;
    }
    if (bytes < PCP_HEADER_BYTES || answer[0] != PCP_VERSION ||
        answer[1] != (PCP_ANSWER | PCP_OPCODE_MAP)) {
        return FALSE;
    }
    return bytes < PCP_MAP_BYTES ||
           memcmp(answer + PCP_AT_NONCE, g_portmap.nonce, PCP_NONCE_BYTES) == 0;
}

static s32 portmap_PmpAddressAccept(const u8 *answer, s32 bytes) {
    return bytes >= PMP_SHORTEST_ANSWER && answer[0] == PMP_VERSION &&
           answer[1] == (PMP_ANSWER | PMP_OPCODE_ADDRESS);
}

static s32 portmap_PmpMapAccept(const u8 *answer, s32 bytes) {
    return bytes >= PMP_SHORTEST_ANSWER && answer[0] == PMP_VERSION &&
           answer[1] == (PMP_ANSWER | PMP_OPCODE_MAP_UDP) &&
           (bytes < PMP_MAP_ANSWER_BYTES ||
            portmap_Get16(answer + 8) == g_portmap.internal_port);
}

/* NAT-PMP's UDP mapping of the internal port for `lifetime` seconds
 * (0 with external port 0: taken away); its length. */
static u32 portmap_PmpRequest(
    u8 *request, u16 internal_port, u16 external_port, u32 lifetime) {
    memset(request, 0, PMP_MAP_BYTES);
    request[0] = PMP_VERSION;
    request[1] = PMP_OPCODE_MAP_UDP;
    portmap_Put16(request + 4, internal_port);
    portmap_Put16(request + 6, external_port);
    portmap_Put32(request + 8, lifetime);
    return PMP_MAP_BYTES;
}

typedef s32 (*PortMapAccept)(const u8 *answer, s32 bytes);

/* `request` sent on the connected UDP socket, again after 250 ms, 500,
 * 1000..., until an answer `accept` takes comes or `deadline` passes:
 * the answer's length, 0 when none came, -1 when nothing listens there. */
static s32 portmap_Exchange(
    PortMapSocket sock, const u8 *request, u32 request_bytes, u8 *answer,
    u32 room, u32 deadline, PortMapAccept accept) {
    u32 interval = PORTMAP_RESEND_MS;
    u32 next = portmap_Now();
    for (;;) {
        u32 now = portmap_Now();
        u32 until;
        s32 got;
        s32 reads = 0;
        if (!portmap_Before(now, deadline)) {
            return 0;
        }
        if (!portmap_Before(now, next)) {
            if (send(sock, (const char *)request, (s32)request_bytes,
                     PORTMAP_SEND_FLAGS) < 0 &&
                portmap_LastError() == PORTMAP_REFUSED) {
                return -1;
            }
            next = now + interval;
            interval *= 2;
        }
        until = portmap_Before(next, deadline) ? next : deadline;
        if (!portmap_Wait(sock, FALSE, until - now)) {
            continue;
        }
        while (reads++ < PORTMAP_DRAIN_MAX &&
               (got = portmap_Receive(sock, answer, room)) != 0) {
            if (got < 0) {
                return -1;
            }
            if (accept(answer, got)) {
                return got;
            }
        }
    }
}

/* PCP's MAP of UDP `port` asked of the server; MADE with `out` filled,
 * or how it ended. */
static s32 portmap_TryPcp(
    PortMapSocket sock, u16 port, u32 deadline, PortMapResult *out, s32 *kept) {
    u8 request[PCP_MAP_BYTES];
    u8 answer[PORTMAP_DATAGRAM_BYTES];
    u32 request_bytes;
    s32 got;
    u32 result;
    portmap_NewNonce();
    request_bytes =
        portmap_PcpRequest(request, port, port, PORT_MAP_LEASE_SECONDS);
    got = portmap_Exchange(sock, request, request_bytes, answer, sizeof(answer),
                           deadline, portmap_PcpAccept);
    if (got < 0) {
        return PORTMAP_NOBODY;
    }
    if (got == 0) {
        return PORTMAP_SILENT;
    }
    result = answer[PCP_AT_RESULT];
    if (answer[0] == PMP_VERSION || result == PCP_UNSUPP_VERSION) {
        return PORTMAP_OTHER;
    }
    if (result == PCP_OK && got >= PCP_MAP_BYTES) {
        const u8 *external = answer + PCP_AT_EXTERNAL;
        out->lease_seconds = portmap_Get32(answer + PCP_AT_LIFETIME);
        out->external_port = (u16)portmap_Get16(answer + PCP_AT_EXTERNAL_PORT);
        if (external[PCP_V4_MARK] == 0xFF &&
            external[PCP_V4_MARK + 1] == 0xFF) {
            out->external_address = portmap_Get32(external + PCP_V4_AT);
        }
        return PORTMAP_MADE;
    }
    if (result == PCP_NOT_AUTHORIZED) {
        portmap_Why(
            out, kept, WHY_PCP,
            "The router does not let programs open ports (PCP): forward "
            "UDP port %u by hand, or use a lobby room",
            port);
    } else if (result == PCP_ADDRESS_MISMATCH) {
        portmap_Why(out, kept, WHY_PCP,
                    "Another router stands between this PC and the Internet: "
                    "forward UDP port %u by hand, or use a lobby room",
                    port);
    } else if (result == PCP_NO_RESOURCES) {
        portmap_Why(
            out, kept, WHY_PCP,
            "The router has no room for another port (PCP): forward UDP "
            "port %u by hand, or use a lobby room",
            port);
    } else {
        portmap_Why(out, kept, WHY_PCP,
                    "The router refused UDP port %u (PCP error %u): forward it "
                    "by hand, or use a lobby room",
                    port, result);
    }
    return PORTMAP_DENIED;
}

/* NAT-PMP's external address, then its mapping of UDP `port`. */
static s32 portmap_TryPmp(
    PortMapSocket sock, u16 port, u32 deadline, PortMapResult *out, s32 *kept) {
    u8 request[PMP_MAP_BYTES];
    u8 answer[PORTMAP_DATAGRAM_BYTES];
    u32 request_bytes;
    s32 got;
    u32 result;
    memset(request, 0, sizeof(request));
    request[0] = PMP_VERSION;
    request[1] = PMP_OPCODE_ADDRESS;
    got = portmap_Exchange(sock, request, 2, answer, sizeof(answer), deadline,
                           portmap_PmpAddressAccept);
    if (got <= 0) {
        return got < 0 ? PORTMAP_NOBODY : PORTMAP_SILENT;
    }
    result = portmap_Get16(answer + 2);
    if (result == PMP_OK && got >= PMP_ADDRESS_BYTES) {
        out->external_address = portmap_Get32(answer + 8);
    } else if (result != PMP_OK) {
        if (result == PMP_NETWORK_FAILURE) {
            portmap_Why(
                out, kept, WHY_PMP,
                "The router is not connected to the Internet yet "
                "(NAT-PMP): try again in a moment, or use a lobby room");
        } else {
            portmap_Why(out, kept, WHY_PMP,
                        "The router refused UDP port %u (NAT-PMP error %u): "
                        "forward it by hand, or use a lobby room",
                        port, result);
        }
        return PORTMAP_DENIED;
    }
    request_bytes =
        portmap_PmpRequest(request, port, port, PORT_MAP_LEASE_SECONDS);
    got = portmap_Exchange(sock, request, request_bytes, answer, sizeof(answer),
                           deadline, portmap_PmpMapAccept);
    if (got <= 0) {
        return got < 0 ? PORTMAP_NOBODY : PORTMAP_SILENT;
    }
    result = portmap_Get16(answer + 2);
    if (result == PMP_OK && got >= PMP_MAP_ANSWER_BYTES) {
        out->external_port = (u16)portmap_Get16(answer + 10);
        out->lease_seconds = portmap_Get32(answer + 12);
        return PORTMAP_MADE;
    }
    if (result == PMP_REFUSED) {
        portmap_Why(
            out, kept, WHY_PMP,
            "The router's NAT-PMP is turned off: forward UDP port %u by "
            "hand, or use a lobby room",
            port);
    } else if (result == PMP_NO_RESOURCES) {
        portmap_Why(
            out, kept, WHY_PMP,
            "The router has no room for another port (NAT-PMP): forward "
            "UDP port %u by hand, or use a lobby room",
            port);
    } else {
        portmap_Why(
            out, kept, WHY_PMP,
            "The router refused UDP port %u (NAT-PMP error %u): forward "
            "it by hand, or use a lobby room",
            port, result);
    }
    return PORTMAP_DENIED;
}

/* ---- SSDP: where the router's UPnP description is ---- */

static const char * const g_portmap_search_types[] = {
    "urn:schemas-upnp-org:device:InternetGatewayDevice:1",
    "urn:schemas-upnp-org:device:InternetGatewayDevice:2",
    "urn:schemas-upnp-org:service:WANIPConnection:1",
    "urn:schemas-upnp-org:service:WANIPConnection:2",
    "urn:schemas-upnp-org:service:WANPPPConnection:1",
};
#define PORTMAP_SEARCH_TYPES                                                   \
    (s32)(sizeof(g_portmap_search_types) / sizeof(g_portmap_search_types[0]))

/* The value of header `name` in the lines [from, end) of an HTTP
 * message, trimmed into `out`: TRUE if it is there and fitted. */
static s32 portmap_Header(
    const char *from, const char *end, const char *name, char *out, u32 room) {
    u32 count = (u32)strlen(name);
    const char *line = from;
    while (line < end) {
        const char *stop = line;
        while (stop < end && *stop != '\n') {
            stop++;
        }
        if ((u32)(stop - line) > count && line[count] == ':' &&
            portmap_SameText(line, name, count)) {
            return portmap_CopyTrimmed(line + count + 1, stop, out, room);
        }
        line = stop + 1;
    }
    out[0] = '\0';
    return FALSE;
}

/* An answer to the search: its LOCATION kept if it is a gateway's and
 * new. */
static void portmap_SearchAnswer(PortMapSearch *search, char *text, s32 bytes) {
    char location[PORTMAP_URL_BYTES];
    char kind[PORTMAP_LINE_BYTES];
    const char *end = text + bytes;
    s32 i;
    if (bytes < 5 || !portmap_SameText(text, "HTTP/", 5)) {
        return;
    }
    if (!portmap_Header(text, end, "LOCATION", location, sizeof(location)) ||
        location[0] == '\0') {
        return;
    }
    if (!portmap_Header(text, end, "ST", kind, sizeof(kind))) {
        portmap_Header(text, end, "NT", kind, sizeof(kind));
    }
    if (!portmap_Contains(kind, "InternetGatewayDevice") &&
        !portmap_Contains(kind, "WANIPConnection") &&
        !portmap_Contains(kind, "WANPPPConnection")) {
        return;
    }
    for (i = 0; i < search->count; i++) {
        if (strcmp(search->locations[i], location) == 0) {
            return;
        }
    }
    if (search->count < PORTMAP_LOCATIONS) {
        strcpy(search->locations[search->count++], location);
    }
}

static void portmap_SearchSend(PortMapSearch *search) {
    struct sockaddr_in at;
    char text[PORTMAP_LINE_BYTES * 2];
    s32 i;
    portmap_SocketAddress(&at, search->address, search->port);
    for (i = 0; i < PORTMAP_SEARCH_TYPES; i++) {
        u32 address = search->address;
        s32 bytes = snprintf(
            text, sizeof(text),
            "M-SEARCH * HTTP/1.1\r\nHOST: %u.%u.%u.%u:%u\r\n"
            "MAN: \"ssdp:discover\"\r\nMX: 1\r\nST: %s\r\n\r\n",
            address >> 24, address >> 16 & 0xFF, address >> 8 & 0xFF,
            address & 0xFF, search->port, g_portmap_search_types[i]);
        if (bytes <= 0 || bytes >= (s32)sizeof(text)) {
            continue;
        }
        if (search->unicast) {
            if (send(search->socket, text, bytes, PORTMAP_SEND_FLAGS) < 0 &&
                portmap_LastError() == PORTMAP_REFUSED) {
                search->refused = TRUE;
            }
        } else {
            sendto(search->socket, text, bytes, 0, (struct sockaddr *)&at,
                   sizeof(at));
        }
    }
}

/* The search's answers read, without waiting. */
static void portmap_SearchRead(PortMapSearch *search) {
    char text[PORTMAP_DATAGRAM_BYTES + 1];
    s32 reads;
    for (reads = 0; reads < PORTMAP_DRAIN_MAX; reads++) {
        s32 got = portmap_Receive(search->socket, (u8 *)text, sizeof(text) - 1);
        if (got == 0) {
            return;
        }
        if (got < 0) {
            /* a unicast search's target is not listening; a multicast
             * socket's errors are other hosts' */
            if (search->unicast) {
                search->refused = TRUE;
                return;
            }
            continue;
        }
        text[got] = '\0';
        portmap_SearchAnswer(search, text, got);
    }
}

/* The search sent: to `ssdp_at` alone, or to SSDP's group by the
 * interface toward the gateway (`local`). */
static void portmap_SearchStart(
    PortMapSearch *search, const char *ssdp_at, u32 local) {
    memset(search, 0, sizeof(*search));
    search->socket = portmap_Open(SOCK_DGRAM);
    if (search->socket == PORTMAP_NO_SOCKET) {
        return;
    }
    if (ssdp_at != NULL) {
        search->unicast = TRUE;
        if (!portmap_ParseAt(ssdp_at, &search->address, &search->port) ||
            !portmap_Connect(search->socket, search->address, search->port)) {
            portmap_Close(search->socket);
            search->socket = PORTMAP_NO_SOCKET;
            return;
        }
    } else {
        s32 ttl = SSDP_TTL;
        search->address = SSDP_ADDRESS;
        search->port = SSDP_PORT;
        setsockopt(search->socket, IPPROTO_IP, IP_MULTICAST_TTL,
                   (const char *)&ttl, sizeof(ttl));
        if (local != 0) {
            struct in_addr from;
            from.s_addr = htonl(local);
            setsockopt(search->socket, IPPROTO_IP, IP_MULTICAST_IF,
                       (const char *)&from, sizeof(from));
        }
    }
    portmap_SearchSend(search);
}

/* ---- HTTP ---- */

/* The end of a chunked body that starts at `from`: TRUE if its last
 * chunk (of size 0) is in [from, end). With `out`, the chunks' bytes are
 * moved together to `out` (which may be `from`) and their count left in
 * `*bytes`. */
static s32 portmap_Chunks(char *from, const char *end, char *out, u32 *bytes) {
    char *read = from;
    u32 written = 0;
    for (;;) {
        u32 size = 0;
        s32 digits = 0;
        for (; read < end; read++) {
            s32 c = portmap_Lower((u8)*read);
            s32 value;
            if (c >= '0' && c <= '9') {
                value = c - '0';
            } else if (c >= 'a' && c <= 'f') {
                value = c - 'a' + 10;
            } else {
                break;
            }
            if (size > 0x0FFFFFFFU) {
                break;
            }
            size = size * 16 + (u32)value;
            digits++;
        }
        while (read < end && *read != '\n') {
            read++;
        }
        if (digits == 0 || read >= end) {
            break;
        }
        read++;
        if (size == 0) {
            if (bytes != NULL) {
                *bytes = written;
            }
            return TRUE;
        }
        if ((u32)(end - read) < size) {
            size = (u32)(end - read);
        }
        if (out != NULL) {
            memmove(out + written, read, size);
        }
        written += size;
        read += size;
        if (read < end && *read == '\r') {
            read++;
        }
        if (read < end && *read == '\n') {
            read++;
        }
    }
    if (bytes != NULL) {
        *bytes = written;
    }
    return FALSE;
}

/* Where an HTTP message's body starts (NULL: its head is not all in). */
static char *portmap_BodyOf(char *text, const char *end) {
    char *at;
    for (at = text; at < end; at++) {
        if (at[0] != '\n') {
            continue;
        }
        if (at + 1 < end && at[1] == '\n') {
            return at + 2;
        }
        if (at + 2 < end && at[1] == '\r' && at[2] == '\n') {
            return at + 3;
        }
    }
    return NULL;
}

/* The length its head gives the body: TRUE if it gives one. */
static s32 portmap_ContentLength(
    const char *text, const char *head_end, u32 *length) {
    char value[PORTMAP_LINE_BYTES];
    const char *at = value;
    if (!portmap_Header(
            text, head_end, "Content-Length", value, sizeof(value)) ||
        !portmap_Number(&at, PORTMAP_DESCRIPTION_BYTES * 16, length)) {
        return FALSE;
    }
    return TRUE;
}

static s32 portmap_IsChunked(const char *text, const char *head_end) {
    char value[PORTMAP_LINE_BYTES];
    return portmap_Header(
               text, head_end, "Transfer-Encoding", value, sizeof(value)) &&
           portmap_Contains(value, "chunked");
}

/* TRUE when the whole answer is in, before the router closes. */
static s32 portmap_HttpWhole(char *text, u32 bytes) {
    const char *end = text + bytes;
    char *body = portmap_BodyOf(text, end);
    u32 length;
    if (body == NULL) {
        return FALSE;
    }
    if (portmap_IsChunked(text, body)) {
        return portmap_Chunks(body, end, NULL, NULL);
    }
    if (portmap_ContentLength(text, body, &length)) {
        return (u32)(end - body) >= length;
    }
    return FALSE;
}

/* An HTTP answer's status (0: not HTTP) and its body, de-chunked and
 * ended, at `*body`. */
static s32 portmap_HttpParse(char *text, u32 bytes, char **body) {
    const char *end = text + bytes;
    const char *at;
    char *start;
    u32 status;
    u32 length;
    *body = text + bytes;
    if (bytes < 12 || !portmap_SameText(text, "HTTP/", 5)) {
        return 0;
    }
    at = text + 5;
    while (at < end && *at != ' ') {
        at++;
    }
    while (at < end && *at == ' ') {
        at++;
    }
    if (!portmap_Number(&at, 999, &status)) {
        return 0;
    }
    start = portmap_BodyOf(text, end);
    if (start == NULL) {
        return (s32)status;
    }
    length = (u32)(end - start);
    if (portmap_IsChunked(text, start)) {
        portmap_Chunks(start, end, start, &length);
    } else {
        u32 given;
        if (portmap_ContentLength(text, start, &given) && given < length) {
            length = given;
        }
    }
    start[length] = '\0';
    *body = start;
    return (s32)status;
}

/* `request` sent to address:port over TCP and the answer read until the
 * router closes, it is all in, `buffer` is full or `deadline` passes:
 * its status (0: no answer) and its body at `*body`. `local`, if not
 * NULL, gets this PC's address on the connection. */
static s32 portmap_Http(
    u32 address, u16 port, const char *request, char *buffer, u32 room,
    u32 deadline, char **body, u32 *local) {
    PortMapSocket sock = portmap_Open(SOCK_STREAM);
    u32 total = (u32)strlen(request);
    u32 sent = 0;
    u32 got = 0;
    s32 error = 0;
    socklen_t error_bytes = sizeof(error);
    *body = buffer;
    buffer[0] = '\0';
    if (sock == PORTMAP_NO_SOCKET) {
        return 0;
    }
    if (!portmap_Connect(sock, address, port)) {
        portmap_Close(sock);
        return 0;
    }
    if (!portmap_Before(portmap_Now(), deadline) ||
        !portmap_Wait(sock, TRUE, deadline - portmap_Now()) ||
        getsockopt(sock, SOL_SOCKET, SO_ERROR, (char *)&error, &error_bytes) !=
            0 ||
        error != 0) {
        portmap_Close(sock);
        return 0;
    }
    if (local != NULL) {
        *local = portmap_SocketLocal(sock);
    }
    while (sent < total) {
        s32 count = (s32)send(
            sock, request + sent, (s32)(total - sent), PORTMAP_SEND_FLAGS);
        u32 now = portmap_Now();
        if (count > 0) {
            sent += (u32)count;
            continue;
        }
        if (portmap_LastError() != PORTMAP_AGAIN ||
            !portmap_Before(now, deadline)) {
            portmap_Close(sock);
            return 0;
        }
        portmap_Wait(sock, TRUE, deadline - now);
    }
    while (got < room - 1) {
        u32 now = portmap_Now();
        s32 count;
        if (!portmap_Before(now, deadline)) {
            break;
        }
        if (!portmap_Wait(sock, FALSE, deadline - now)) {
            continue;
        }
        count = (s32)recv(sock, buffer + got, (s32)(room - 1 - got), 0);
        if (count > 0) {
            got += (u32)count;
            buffer[got] = '\0';
            if (portmap_HttpWhole(buffer, got)) {
                break;
            }
        } else if (count == 0 || portmap_LastError() != PORTMAP_AGAIN) {
            break;
        }
    }
    portmap_Close(sock);
    buffer[got] = '\0';
    return portmap_HttpParse(buffer, got, body);
}

/* ---- XML, as much of it as a router's description and SOAP need ---- */

/* The next tag named `name` in [from, end), whatever its namespace
 * prefix and case (</name> when `closing`): where its '<' is, NULL if
 * there is none. `*after` gets what follows its '>', `*empty` whether it
 * is <name/>. */
static const char *portmap_FindTag(
    const char *from, const char *end, const char *name, s32 closing,
    const char **after, s32 *empty) {
    u32 count = (u32)strlen(name);
    const char *at;
    for (at = from; at < end; at++) {
        const char *word;
        const char *local;
        const char *stop;
        if (*at != '<') {
            continue;
        }
        word = at + 1;
        if (closing) {
            if (word >= end || *word != '/') {
                continue;
            }
            word++;
        } else if (
            word < end && (*word == '/' || *word == '?' || *word == '!')) {
            continue;
        }
        stop = word;
        local = word;
        while (stop < end && !portmap_Space((u8)*stop) && *stop != '>' &&
               *stop != '/') {
            if (*stop == ':') {
                local = stop + 1;
            }
            stop++;
        }
        if ((u32)(stop - local) != count ||
            !portmap_SameText(local, name, count)) {
            continue;
        }
        while (stop < end && *stop != '>') {
            stop++;
        }
        if (stop >= end) {
            return NULL;
        }
        *empty = stop[-1] == '/';
        *after = stop + 1;
        return at;
    }
    return NULL;
}

/* The text of the first <name>...</name> in [from, end), trimmed and its
 * five entities read, into `out`: TRUE if it is there and fitted. */
static s32 portmap_TagText(
    const char *from, const char *end, const char *name, char *out, u32 room) {
    static const char * const entities[][2] = {
        {"&amp;", "&"},
        {"&lt;", "<"},
        {"&gt;", ">"},
        {"&quot;", "\""},
        {"&apos;", "'"}};
    const char *inside;
    const char *close_after;
    const char *close;
    const char *read;
    s32 empty;
    u32 written = 0;
    out[0] = '\0';
    if (portmap_FindTag(from, end, name, FALSE, &inside, &empty) == NULL) {
        return FALSE;
    }
    if (empty) {
        return TRUE;
    }
    close = portmap_FindTag(inside, end, name, TRUE, &close_after, &empty);
    if (close == NULL) {
        return FALSE;
    }
    while (inside < close && portmap_Space((u8)*inside)) {
        inside++;
    }
    while (close > inside && portmap_Space((u8)close[-1])) {
        close--;
    }
    for (read = inside; read < close;) {
        char c = *read;
        u32 used = 1;
        u32 i;
        if (c == '&') {
            for (i = 0; i < sizeof(entities) / sizeof(entities[0]); i++) {
                u32 length = (u32)strlen(entities[i][0]);
                if ((u32)(close - read) >= length &&
                    memcmp(read, entities[i][0], length) == 0) {
                    c = entities[i][1][0];
                    used = length;
                    break;
                }
            }
        }
        if (written + 1 >= room) {
            out[0] = '\0';
            return FALSE;
        }
        out[written++] = c;
        read += used;
    }
    out[written] = '\0';
    return TRUE;
}

/* `http://A.B.C.D[:PORT][/path]`: TRUE and `out` filled. */
static s32 portmap_ParseUrl(const char *url, PortMapUrl *out) {
    const char *at = url + 7;
    u32 port = HTTP_PORT;
    if (!portmap_SameText(url, "http://", 7) ||
        !portmap_Address(&at, &out->address)) {
        return FALSE;
    }
    if (*at == ':') {
        at++;
        if (!portmap_Number(&at, 65535, &port)) {
            return FALSE;
        }
    }
    if (port == 0) {
        return FALSE;
    }
    out->port = (u16)port;
    if (*at == '\0') {
        strcpy(out->path, "/");
        return TRUE;
    }
    if (*at != '/' || strlen(at) >= sizeof(out->path)) {
        return FALSE;
    }
    strcpy(out->path, at);
    return TRUE;
}

/* `reference` (absolute, from the root, or beside `base`'s path) read
 * against `base`. */
static s32 portmap_ResolveUrl(
    const char *base, const char *reference, PortMapUrl *out) {
    char path[PORTMAP_URL_BYTES];
    char *slash;
    if (portmap_SameText(reference, "http://", 7)) {
        return portmap_ParseUrl(reference, out);
    }
    if (!portmap_ParseUrl(base, out)) {
        return FALSE;
    }
    if (reference[0] == '/') {
        if (strlen(reference) >= sizeof(out->path)) {
            return FALSE;
        }
        strcpy(out->path, reference);
        return TRUE;
    }
    strcpy(path, out->path);
    slash = strrchr(path, '/');
    slash[1] = '\0';
    if (strlen(path) + strlen(reference) >= sizeof(out->path)) {
        return FALSE;
    }
    strcat(path, reference);
    strcpy(out->path, path);
    return TRUE;
}

/* The WANIPConnection and WANPPPConnection services of a description,
 * the first ones first: how many were found. */
static s32 portmap_FindServices(
    const char *xml, const char *end, const char *location,
    PortMapService *services) {
    PortMapService found[PORTMAP_SERVICES];
    char base[PORTMAP_URL_BYTES];
    char control[PORTMAP_URL_BYTES];
    const char *at = xml;
    const char *inside;
    s32 empty;
    s32 count = 0;
    s32 kept = 0;
    s32 pass;
    s32 i;
    if (!portmap_TagText(xml, end, "URLBase", base, sizeof(base)) ||
        base[0] == '\0') {
        strcpy(base, location);
    }
    while (
        count < PORTMAP_SERVICES &&
        portmap_FindTag(at, end, "service", FALSE, &inside, &empty) != NULL) {
        const char *after;
        const char *close;
        PortMapService *service = &found[count];
        if (empty) {
            at = inside;
            continue;
        }
        close = portmap_FindTag(inside, end, "service", TRUE, &after, &empty);
        if (close == NULL) {
            break;
        }
        if (portmap_TagText(inside, close, "serviceType", service->type,
                            sizeof(service->type)) &&
            portmap_TagText(
                inside, close, "controlURL", control, sizeof(control)) &&
            (portmap_Contains(service->type, ":service:WANIPConnection:") ||
             portmap_Contains(service->type, ":service:WANPPPConnection:")) &&
            portmap_ResolveUrl(base, control, &service->control)) {
            count++;
        }
        at = after;
    }
    /* WANIPConnection ahead of WANPPPConnection */
    for (pass = 0; pass < 2; pass++) {
        for (i = 0; i < count; i++) {
            s32 ip = portmap_Contains(found[i].type, "WANIPConnection");
            if (ip == (pass == 0)) {
                services[kept++] = found[i];
            }
        }
    }
    return kept;
}

/* ---- SOAP ---- */

/* The HTTP request of the service's `action` with its arguments (XML),
 * into `out`: its length, 0 if it does not fit. */
static u32 portmap_SoapRequest(
    const PortMapService *service, const char *action, const char *arguments,
    char *out, u32 room) {
    char envelope[PORTMAP_ENVELOPE_BYTES];
    const PortMapUrl *control = &service->control;
    s32 envelope_bytes = snprintf(
        envelope, sizeof(envelope),
        "<?xml version=\"1.0\"?>\r\n"
        "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
        "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
        "<s:Body><u:%s xmlns:u=\"%s\">%s</u:%s></s:Body></s:Envelope>\r\n",
        action, service->type, arguments, action);
    s32 bytes;
    if (envelope_bytes <= 0 || envelope_bytes >= (s32)sizeof(envelope)) {
        return 0;
    }
    bytes = snprintf(
        out, room,
        "POST %s HTTP/1.0\r\nHost: %u.%u.%u.%u:%u\r\n"
        "Content-Type: text/xml; charset=\"utf-8\"\r\n"
        "Content-Length: %d\r\nSOAPAction: \"%s#%s\"\r\n"
        "Connection: close\r\n\r\n%s",
        control->path, control->address >> 24, control->address >> 16 & 0xFF,
        control->address >> 8 & 0xFF, control->address & 0xFF, control->port,
        envelope_bytes, service->type, action, envelope);
    if (bytes <= 0 || bytes >= (s32)room) {
        return 0;
    }
    return (u32)bytes;
}

/* The service's `action` called: the HTTP status, the body at `*body`. */
static s32 portmap_Soap(
    const PortMapService *service, const char *action, const char *arguments,
    u32 deadline, char **body, u32 *local) {
    char request[PORTMAP_REQUEST_BYTES];
    *body = g_portmap_soap;
    g_portmap_soap[0] = '\0';
    if (portmap_SoapRequest(
            service, action, arguments, request, sizeof(request)) == 0) {
        return 0;
    }
    return portmap_Http(
        service->control.address, service->control.port, request,
        g_portmap_soap, sizeof(g_portmap_soap), deadline, body, local);
}

/* A SOAP fault's UPnP error code (0: none). */
static s32 portmap_SoapError(const char *body) {
    char code[PORTMAP_LINE_BYTES];
    const char *at = code;
    u32 value;
    if (!portmap_TagText(
            body, body + strlen(body), "errorCode", code, sizeof(code)) ||
        !portmap_Number(&at, 99999, &value)) {
        return 0;
    }
    return (s32)value;
}

static void portmap_AddArguments(char *out, u32 room, u16 external_port,
                                 u16 internal_port, u32 client, u32 lease) {
    snprintf(out, room,
             "<NewRemoteHost></NewRemoteHost>"
             "<NewExternalPort>%u</NewExternalPort>"
             "<NewProtocol>UDP</NewProtocol>"
             "<NewInternalPort>%u</NewInternalPort>"
             "<NewInternalClient>%u.%u.%u.%u</NewInternalClient>"
             "<NewEnabled>1</NewEnabled>"
             "<NewPortMappingDescription>ReBushido</NewPortMappingDescription>"
             "<NewLeaseDuration>%u</NewLeaseDuration>",
             external_port, internal_port, client >> 24, client >> 16 & 0xFF,
             client >> 8 & 0xFF, client & 0xFF, lease);
}

/* The gateway at `location` asked for UDP `port`: MADE with `out` and
 * g_portmap's service filled, or how it ended. */
static s32 portmap_TryUpnp(const char *location, u16 port, u32 deadline,
                           PortMapResult *out, s32 *kept) {
    PortMapService services[PORTMAP_SERVICES];
    PortMapUrl url;
    char request[PORTMAP_REQUEST_BYTES];
    char arguments[PORTMAP_ARGUMENTS_BYTES];
    char address_text[PORTMAP_LINE_BYTES];
    char *body;
    s32 count;
    s32 chosen = -1;
    s32 status;
    s32 i;
    u32 local = 0;
    u32 external = 0;
    u32 lease = PORT_MAP_LEASE_SECONDS;
    u16 external_port = port;
    if (!portmap_ParseUrl(location, &url)) {
        return PORTMAP_SILENT;
    }
    snprintf(request, sizeof(request),
             "GET %s HTTP/1.0\r\nHost: %u.%u.%u.%u:%u\r\n"
             "User-Agent: ReBushido\r\nConnection: close\r\n\r\n",
             url.path, url.address >> 24, url.address >> 16 & 0xFF,
             url.address >> 8 & 0xFF, url.address & 0xFF, url.port);
    status = portmap_Http(url.address, url.port, request, g_portmap_description,
                          sizeof(g_portmap_description), deadline, &body, NULL);
    if (status != HTTP_OK) {
        portmap_Why(
            out, kept, WHY_UPNP_SILENT,
            "The router's UPnP did not answer in time: forward UDP port %u "
            "by hand, or use a lobby room",
            port);
        return PORTMAP_SILENT;
    }
    count = portmap_FindServices(body, body + strlen(body), location, services);
    if (count == 0) {
        portmap_Why(
            out, kept, WHY_UPNP_SILENT,
            "The router's UPnP has no Internet connection to open a port "
            "on: forward UDP port %u by hand, or use a lobby room",
            port);
        return PORTMAP_SILENT;
    }
    /* the first service that knows its external address: the one that
     * is connected */
    for (i = 0; i < count && chosen < 0; i++) {
        u32 address;
        const char *at = address_text;
        status = portmap_Soap(
            &services[i], "GetExternalIPAddress", "", deadline, &body, &local);
        if (status == HTTP_OK &&
            portmap_TagText(body, body + strlen(body), "NewExternalIPAddress",
                            address_text, sizeof(address_text)) &&
            portmap_Address(&at, &address) && address != 0) {
            chosen = i;
            external = address;
        }
    }
    if (chosen < 0) {
        chosen = 0;
    }
    if (local == 0) {
        local = portmap_LocalToward(
            services[chosen].control.address, services[chosen].control.port);
    }
    for (;;) {
        s32 error;
        if (!portmap_Before(portmap_Now(), deadline)) {
            portmap_Why(out, kept, WHY_UPNP_SILENT,
                        "The router's UPnP did not answer in time: forward UDP "
                        "port %u by hand, or use a lobby room",
                        port);
            return PORTMAP_SILENT;
        }
        portmap_AddArguments(
            arguments, sizeof(arguments), external_port, port, local, lease);
        status = portmap_Soap(&services[chosen], "AddPortMapping", arguments,
                              deadline, &body, NULL);
        if (status == HTTP_OK) {
            break;
        }
        error = portmap_SoapError(body);
        if (error == UPNP_PERMANENT_ONLY && lease != 0) {
            lease = 0;
            continue;
        }
        if (error == UPNP_CONFLICT &&
            external_port - port < PORTMAP_OTHER_PORTS &&
            external_port < 65535) {
            external_port++;
            continue;
        }
        if (status == 0) {
            portmap_Why(out, kept, WHY_UPNP_SILENT,
                        "The router's UPnP did not answer in time: forward UDP "
                        "port %u by hand, or use a lobby room",
                        port);
        } else if (error == UPNP_CONFLICT) {
            portmap_Why(
                out, kept, WHY_UPNP,
                "Another PC has UDP port %u on the router (UPnP): forward "
                "it by hand, or use a lobby room",
                port);
        } else if (error == UPNP_NOT_AUTHORIZED) {
            portmap_Why(out, kept, WHY_UPNP,
                        "The router's UPnP does not let programs open ports: "
                        "forward UDP port %u by hand, or use a lobby room",
                        port);
        } else {
            portmap_Why(
                out, kept, WHY_UPNP,
                "The router refused UDP port %u (UPnP error %d): forward "
                "it by hand, or use a lobby room",
                port, error != 0 ? error : status);
        }
        return PORTMAP_DENIED;
    }
    g_portmap.service = services[chosen];
    g_portmap.client = local;
    out->external_address = external;
    out->external_port = external_port;
    out->lease_seconds = lease;
    return PORTMAP_MADE;
}

/* ---- the three, one after the other ---- */

static void portmap_Forget(void) {
    if (g_portmap.renewing) {
        portmap_Close(g_portmap.socket);
    }
    memset(&g_portmap, 0, sizeof(g_portmap));
    g_portmap.method = METHOD_NONE;
}

s32 port_MapPort(u16 port, const PortMapOptions *options, PortMapResult *out) {
    static const char * const names[] = {"", "PCP", "NAT-PMP", "UPnP"};
    PortMapSearch search;
    PortMapSocket sock;
    u32 wait = options != NULL && options->wait_ms != 0 ? options->wait_ms
                                                        : PORTMAP_WAIT_MS;
    u32 start;
    u32 deadline;
    u32 server = 0;
    u16 server_port = PCP_PORT;
    s32 kept = WHY_NONE;
    s32 method = METHOD_NONE;
    s32 i;
    memset(out, 0, sizeof(*out));
    port_UnmapPort();
    if (port == 0) {
        portmap_Why(out, &kept, WHY_LOCAL, "No port was given to open");
        return FALSE;
    }
    if (!portmap_Ready()) {
        portmap_Why(
            out, &kept, WHY_LOCAL,
            "This PC's network could not be started: forward UDP port %u "
            "by hand, or use a lobby room",
            port);
        return FALSE;
    }
    /* (NAT-PMP's answers are told by it) */
    g_portmap.internal_port = port;
    start = portmap_Now();
    deadline = start + wait;
    if (options != NULL && options->gateway_at != NULL) {
        if (!portmap_ParseAt(options->gateway_at, &server, &server_port)) {
            server = 0;
        }
    } else {
        server = portmap_Gateway();
    }
    if (server != 0) {
        g_portmap.client = portmap_LocalToward(server, server_port);
    }
    /* first, so that its answers gather while PCP and NAT-PMP are tried */
    portmap_SearchStart(
        &search, options != NULL ? options->ssdp_at : NULL, g_portmap.client);

    sock = server != 0 ? portmap_Open(SOCK_DGRAM) : PORTMAP_NO_SOCKET;
    if (sock != PORTMAP_NO_SOCKET &&
        portmap_Connect(sock, server, server_port)) {
        s32 ended = portmap_TryPcp(
            sock, port, start + wait / PORTMAP_EIGHTHS * PORTMAP_PCP_EIGHTHS,
            out, &kept);
        if (ended == PORTMAP_MADE) {
            method = METHOD_PCP;
        } else if (ended != PORTMAP_NOBODY) {
            u32 eighths = ended == PORTMAP_SILENT ? PORTMAP_PMP_SILENT_EIGHTHS
                                                  : PORTMAP_PMP_EIGHTHS;
            if (portmap_TryPmp(
                    sock, port, start + wait / PORTMAP_EIGHTHS * eighths, out,
                    &kept) == PORTMAP_MADE) {
                method = METHOD_PMP;
            }
        }
    }
    portmap_Close(sock);

    if (method == METHOD_NONE && search.socket != PORTMAP_NO_SOCKET) {
        u32 search_end =
            start + wait / PORTMAP_EIGHTHS * PORTMAP_SEARCH_EIGHTHS;
        s32 again = FALSE;
        for (;;) {
            u32 now = portmap_Now();
            portmap_SearchRead(&search);
            if (search.count > 0 || search.refused ||
                !portmap_Before(now, search_end)) {
                break;
            }
            if (!again && now - start >= PORTMAP_SEARCH_AGAIN_MS) {
                again = TRUE;
                portmap_SearchSend(&search);
            }
            portmap_Wait(search.socket, FALSE,
                         search_end - now < PORTMAP_RESEND_MS
                             ? search_end - now
                             : PORTMAP_RESEND_MS);
        }
        for (i = 0; i < search.count && method == METHOD_NONE; i++) {
            if (portmap_TryUpnp(search.locations[i], port, deadline, out,
                                &kept) == PORTMAP_MADE) {
                method = METHOD_UPNP;
            }
        }
    }
    portmap_Close(search.socket);

    if (method == METHOD_NONE) {
        portmap_Why(
            out, &kept, WHY_NOBODY,
            "No router answered (UPnP, NAT-PMP and PCP): forward UDP port "
            "%u by hand, or use a lobby room",
            port);
        portmap_Forget();
        return FALSE;
    }
    g_portmap.method = method;
    g_portmap.internal_port = port;
    g_portmap.external_port = out->external_port;
    g_portmap.external_address = out->external_address;
    g_portmap.lease = out->lease_seconds;
    g_portmap.server = server;
    g_portmap.server_port = server_port;
    out->ok = TRUE;
    out->why[0] = '\0';
    strcpy(out->method, names[method]);
    return TRUE;
}

/* ---- the lease renewed, a little on each tick ---- */

static u32 portmap_HalfLease(void) {
    if (g_portmap.lease > PORTMAP_HALF_LEASE_MAX_MS / (MS_PER_SECOND / 2)) {
        return PORTMAP_HALF_LEASE_MAX_MS;
    }
    return g_portmap.lease * (MS_PER_SECOND / 2);
}

static void portmap_RenewEnd(u32 now_ms, s32 renewed) {
    portmap_Close(g_portmap.socket);
    g_portmap.socket = PORTMAP_NO_SOCKET;
    g_portmap.renewing = FALSE;
    if (renewed) {
        g_portmap.granted_at = now_ms;
        g_portmap.retry_set = FALSE;
    } else {
        g_portmap.retry_at = now_ms + PORTMAP_RENEW_RETRY_MS;
        g_portmap.retry_set = TRUE;
    }
}

static void portmap_RenewSend(u32 now_ms) {
    send(g_portmap.socket, g_portmap.request, (s32)g_portmap.request_bytes,
         PORTMAP_SEND_FLAGS);
    g_portmap.renew_sent = now_ms;
}

static void portmap_RenewStart(u32 now_ms) {
    u8 *request = (u8 *)g_portmap.request;
    s32 tcp = g_portmap.method == METHOD_UPNP;
    u32 address = tcp ? g_portmap.service.control.address : g_portmap.server;
    u16 port = tcp ? g_portmap.service.control.port : g_portmap.server_port;
    if (g_portmap.method == METHOD_PCP) {
        g_portmap.request_bytes = portmap_PcpRequest(
            request, g_portmap.internal_port, g_portmap.external_port,
            PORT_MAP_LEASE_SECONDS);
    } else if (g_portmap.method == METHOD_PMP) {
        g_portmap.request_bytes = portmap_PmpRequest(
            request, g_portmap.internal_port, g_portmap.external_port,
            PORT_MAP_LEASE_SECONDS);
    } else {
        char arguments[PORTMAP_ARGUMENTS_BYTES];
        portmap_AddArguments(
            arguments, sizeof(arguments), g_portmap.external_port,
            g_portmap.internal_port, g_portmap.client, g_portmap.lease);
        g_portmap.request_bytes = portmap_SoapRequest(
            &g_portmap.service, "AddPortMapping", arguments, g_portmap.request,
            sizeof(g_portmap.request));
    }
    g_portmap.socket = portmap_Open(tcp ? SOCK_STREAM : SOCK_DGRAM);
    g_portmap.renewing = TRUE;
    g_portmap.renew_started = now_ms;
    g_portmap.connected = FALSE;
    g_portmap.sent = 0;
    g_portmap.got = 0;
    if (g_portmap.request_bytes == 0 || g_portmap.socket == PORTMAP_NO_SOCKET ||
        !portmap_Connect(g_portmap.socket, address, port)) {
        portmap_RenewEnd(now_ms, FALSE);
        return;
    }
    if (!tcp) {
        portmap_RenewSend(now_ms);
    }
}

/* PCP's or NAT-PMP's answer, if it has come. */
static void portmap_RenewDatagram(u32 now_ms) {
    u8 answer[PORTMAP_DATAGRAM_BYTES];
    s32 reads;
    for (reads = 0; reads < PORTMAP_DRAIN_MAX; reads++) {
        s32 got = portmap_Receive(g_portmap.socket, answer, sizeof(answer));
        if (got <= 0) {
            break;
        }
        if (g_portmap.method == METHOD_PCP && portmap_PcpAccept(answer, got) &&
            answer[0] == PCP_VERSION) {
            s32 made = answer[PCP_AT_RESULT] == PCP_OK && got >= PCP_MAP_BYTES;
            if (made) {
                g_portmap.lease = portmap_Get32(answer + PCP_AT_LIFETIME);
                g_portmap.external_port =
                    (u16)portmap_Get16(answer + PCP_AT_EXTERNAL_PORT);
            }
            portmap_RenewEnd(now_ms, made);
            return;
        }
        if (g_portmap.method == METHOD_PMP &&
            portmap_PmpMapAccept(answer, got)) {
            s32 made = portmap_Get16(answer + 2) == PMP_OK &&
                       got >= PMP_MAP_ANSWER_BYTES;
            if (made) {
                g_portmap.external_port = (u16)portmap_Get16(answer + 10);
                g_portmap.lease = portmap_Get32(answer + 12);
            }
            portmap_RenewEnd(now_ms, made);
            return;
        }
    }
    if (now_ms - g_portmap.renew_sent >= PORTMAP_RENEW_RESEND_MS) {
        portmap_RenewSend(now_ms);
    }
}

/* UPnP's answer, as far as it has come: connected, sent, read. */
static void portmap_RenewHttp(u32 now_ms) {
    char *body;
    s32 reads;
    if (!g_portmap.connected) {
        s32 error = 0;
        socklen_t error_bytes = sizeof(error);
        if (!portmap_Wait(g_portmap.socket, TRUE, 0)) {
            return;
        }
        if (getsockopt(g_portmap.socket, SOL_SOCKET, SO_ERROR, (char *)&error,
                       &error_bytes) != 0 ||
            error != 0) {
            portmap_RenewEnd(now_ms, FALSE);
            return;
        }
        g_portmap.connected = TRUE;
    }
    while (g_portmap.sent < g_portmap.request_bytes) {
        s32 count = (s32)send(
            g_portmap.socket, g_portmap.request + g_portmap.sent,
            (s32)(g_portmap.request_bytes - g_portmap.sent),
            PORTMAP_SEND_FLAGS);
        if (count <= 0) {
            if (portmap_LastError() != PORTMAP_AGAIN) {
                portmap_RenewEnd(now_ms, FALSE);
            }
            return;
        }
        g_portmap.sent += (u32)count;
    }
    for (reads = 0; reads < PORTMAP_DRAIN_MAX; reads++) {
        u32 room = sizeof(g_portmap.answer) - 1 - g_portmap.got;
        s32 count =
            room == 0
                ? 0
                : (s32)recv(g_portmap.socket, g_portmap.answer + g_portmap.got,
                            (s32)room, 0);
        if (count > 0) {
            g_portmap.got += (u32)count;
            g_portmap.answer[g_portmap.got] = '\0';
            if (!portmap_HttpWhole(g_portmap.answer, g_portmap.got)) {
                continue;
            }
        } else if (count < 0 && portmap_LastError() == PORTMAP_AGAIN) {
            return;
        }
        /* closed, whole, full or failed */
        g_portmap.answer[g_portmap.got] = '\0';
        portmap_RenewEnd(
            now_ms, portmap_HttpParse(g_portmap.answer, g_portmap.got, &body) ==
                        HTTP_OK);
        return;
    }
}

void port_PortMapTick(u32 now_ms) {
    if (g_portmap.method == METHOD_NONE || g_portmap.lease == 0) {
        return;
    }
    if (!g_portmap.clock_set) {
        g_portmap.clock_set = TRUE;
        g_portmap.granted_at = now_ms;
        return;
    }
    if (g_portmap.renewing) {
        if (now_ms - g_portmap.renew_started >= PORTMAP_RENEW_GIVE_UP_MS) {
            portmap_RenewEnd(now_ms, FALSE);
        } else if (g_portmap.method == METHOD_UPNP) {
            portmap_RenewHttp(now_ms);
        } else {
            portmap_RenewDatagram(now_ms);
        }
        return;
    }
    if (now_ms - g_portmap.granted_at < portmap_HalfLease()) {
        return;
    }
    if (g_portmap.retry_set && portmap_Before(now_ms, g_portmap.retry_at)) {
        return;
    }
    portmap_RenewStart(now_ms);
}

/* ---- taken away ---- */

void port_UnmapPort(void) {
    s32 method = g_portmap.method;
    if (method == METHOD_NONE) {
        return;
    }
    if (g_portmap.renewing) {
        portmap_Close(g_portmap.socket);
        g_portmap.renewing = FALSE;
    }
    if (method == METHOD_PCP || method == METHOD_PMP) {
        u8 request[PCP_MAP_BYTES];
        u8 answer[PORTMAP_DATAGRAM_BYTES];
        PortMapSocket sock = portmap_Open(SOCK_DGRAM);
        u32 request_bytes =
            method == METHOD_PCP
                ? portmap_PcpRequest(request, g_portmap.internal_port,
                                     g_portmap.external_port, 0)
                : portmap_PmpRequest(request, g_portmap.internal_port, 0, 0);
        if (sock != PORTMAP_NO_SOCKET &&
            portmap_Connect(sock, g_portmap.server, g_portmap.server_port)) {
            portmap_Exchange(sock, request, request_bytes, answer,
                             sizeof(answer), portmap_Now() + PORTMAP_UNMAP_MS,
                             method == METHOD_PCP ? portmap_PcpAccept
                                                  : portmap_PmpMapAccept);
        }
        portmap_Close(sock);
    } else {
        char arguments[PORTMAP_ARGUMENTS_BYTES];
        char *body;
        snprintf(arguments, sizeof(arguments),
                 "<NewRemoteHost></NewRemoteHost>"
                 "<NewExternalPort>%u</NewExternalPort>"
                 "<NewProtocol>UDP</NewProtocol>",
                 g_portmap.external_port);
        portmap_Soap(&g_portmap.service, "DeletePortMapping", arguments,
                     portmap_Now() + PORTMAP_UNMAP_HTTP_MS, &body, NULL);
    }
    g_portmap.renewing = FALSE;
    portmap_Forget();
}
