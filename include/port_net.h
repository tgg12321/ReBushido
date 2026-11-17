#ifndef PORT_NET_H
#define PORT_NET_H

/* The native port's network (src/port/net.c; docs/NETPLAY.md, sections
 * 11 and 12): one socket of UDP datagrams to one other program of the
 * same build. The game's own files never see it: the link cable's calls
 * are in port_platform.h, and netplay goes in by the input's hook. */

#include "common.h"
#include "port_host.h"

/* `ADDRESS:PORT` (four numbers and a port) into `out`: 1, or 0 if it is
 * not one. */
extern s32 port_ParseNetAddress(const char *text, PortNetAddress *out);
/* Where the run's socket is bound (--net-bind) and the one other program
 * it talks to (--net-peer). Nothing else is listened to. */
extern void port_SetNetAddresses(
    const PortNetAddress *bind, const PortNetAddress *peer);
/* A worse network on purpose, for tests: each datagram sent waits
 * `latency_ms` before it goes (--net-latency), and `loss` in a hundred
 * are dropped (--net-loss), by the run's own sequence of random numbers. */
extern void port_SetNetTrouble(u32 latency_ms, u32 loss);
/* Opens the socket, if the run asked for one: 1, or 0 if it could not. */
extern s32 port_StartNet(void);
extern s32 port_IsNetOn(void);
/* The link cable's side (--link-side): 0, the first player, or 1. */
extern void port_SetLinkSide(s32 side);

/* A datagram to the other program, by way of --net-latency and
 * --net-loss. */
extern void port_NetSend(const void *bytes, u32 count);
/* The other program's next datagram, waiting up to `wait_ms` for it: its
 * length, or -1 if none came. */
extern s32 port_NetReceive(void *bytes, u32 room, u32 wait_ms);
/* Every datagram of the port's begins with eight bytes: a mark, its kind
 * (NET_KIND_..., net.c; netplay's from PORT_NET_KIND_NETPLAY up), a byte
 * of the kind's own and a number. */
#define PORT_NET_HEADER_BYTES 8
#define PORT_NET_KIND_NETPLAY 16
extern void port_NetHeader(u8 *out, s32 kind, s32 detail, u32 number);
/* A datagram's kind, its detail and its number; 0 if it is not the port's. */
extern s32 port_NetReadHeader(
    const u8 *in, s32 bytes, s32 *detail, u32 *number);
/* What the network did, to the error output, as the run ends. */
extern void port_PrintNetReport(void);

#endif /* PORT_NET_H */
