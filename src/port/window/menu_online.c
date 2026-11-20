/* The settings menu's Play online page (menu.h; docs/PLAYING.md, "Playing
 * online"; docs/NETPLAY.md, sections 13 and 14): what its rows say and
 * what a press on each does. menu.c lays the rows out and moves the
 * cursor; each row here is an item, and the page's rows are the items of
 * the role chosen:
 *
 *   Host a room     a room at the lobby, its name, a password if wanted;
 *                   the router asked to open the port (portmap.c)
 *   Browse rooms    the lobby's rooms, refreshed when asked; one whose
 *                   setup code is not this program's is shown so, with
 *                   what differs, and cannot be joined
 *   Host / Join by address   the advanced way: an address typed, with
 *                   this PC's own address found for the other to punch
 *                   through to
 *
 * Every row of every role ends in this program's setup code, the one
 * `rebushido --check-online` prints. A room's password is kept in memory
 * alone, never in the settings file; what the session is given is the
 * key made of it (relay.c), not the word. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "menu.h"
#include "port_net.h"
#include "portmap.h"
#include "shared.h"
#include "sdl_window.h"

#define ROOMS_MAX 12
#define HEX_BYTES 100
#define LABEL_ROOM_BYTES 160
#define ADDRESS_ALL "0.0.0.0"
#define ADDRESS_LOCAL "127.0.0.1"

enum {
    ONLINE_ROLE,
    ONLINE_LOBBY,
    ONLINE_ROOM_NAME,
    ONLINE_PASSWORD,
    ONLINE_PLAYER,
    ONLINE_PORT,
    ONLINE_LISTEN,
    ONLINE_DELAY,
    ONLINE_PORTMAP,
    ONLINE_ADDRESS,
    ONLINE_PUNCH_TO,
    ONLINE_MY_ADDRESS,
    ONLINE_REFRESH,
    ONLINE_SETUP,
    ONLINE_START,
    ONLINE_BACK,
    ONLINE_ROOM, /* and up: the rooms of the list */
};

static struct {
    char password[PORT_RELAY_PASSWORD_BYTES];
    PortRoom rooms[ROOMS_MAX];
    s32 room_count;
    s32 total;
    s32 listed;  /* the list has been asked for */
    s32 pending; /* a locked room chosen, waiting for its password */
    char my_address[LABEL_ROOM_BYTES];
    char setup[PORT_SETUP_CODE_BYTES];
} g_online;

static const char * const g_roles[] = {
    "Host by address", "Join by address", "Host a room", "Browse rooms"};
static const char * const g_role_words[] = {"host", "join", "room", "rooms"};

static s32 port_Role(void) { return g_port_settings.net_role; }

static const char *port_Setup(void) {
    if (g_online.setup[0] == 0) {
        port_SessionSetupCode(g_online.setup);
    }
    return g_online.setup;
}

s32 port_OnlineItems(s32 *items, s32 most) {
    s32 count = 0;
    s32 i;

#define ADD(item)                                                              \
    do {                                                                       \
        if (count < most) {                                                    \
            items[count++] = (item);                                           \
        }                                                                      \
    } while (0)
    ADD(ONLINE_ROLE);
    switch (port_Role()) {
    case PORT_NET_ROLE_ROOM:
        ADD(ONLINE_LOBBY);
        ADD(ONLINE_ROOM_NAME);
        ADD(ONLINE_PASSWORD);
        ADD(ONLINE_PLAYER);
        ADD(ONLINE_START);
        ADD(ONLINE_DELAY);
        ADD(ONLINE_PORTMAP);
        ADD(ONLINE_PORT);
        break;
    case PORT_NET_ROLE_ROOMS:
        ADD(ONLINE_LOBBY);
        ADD(ONLINE_PLAYER);
        ADD(ONLINE_REFRESH);
        for (i = 0; i < g_online.room_count; i++) {
            ADD(ONLINE_ROOM + i);
        }
        ADD(ONLINE_PASSWORD);
        break;
    case PORT_NET_ROLE_HOST:
        ADD(ONLINE_PORT);
        ADD(ONLINE_START);
        ADD(ONLINE_LISTEN);
        ADD(ONLINE_DELAY);
        ADD(ONLINE_PORTMAP);
        ADD(ONLINE_MY_ADDRESS);
        ADD(ONLINE_PUNCH_TO);
        break;
    default:
        ADD(ONLINE_ADDRESS);
        ADD(ONLINE_START);
        ADD(ONLINE_LISTEN);
        ADD(ONLINE_PORT);
        ADD(ONLINE_MY_ADDRESS);
        break;
    }
    ADD(ONLINE_SETUP);
#undef ADD
    return count;
}

/* Which part of two setup codes differs, in a few words; NULL if none. */
static const char *port_SetupDiffers(const char *theirs) {
    static const char * const parts[] = {
        "another version", "another build", "another disc image",
        "other gameplay mods"};
    const char *mine = port_Setup();
    s32 part = 0;
    s32 i;

    for (i = 0; mine[i] != 0 || theirs[i] != 0; i++) {
        if (mine[i] == '-') {
            part++;
        }
        if (mine[i] != theirs[i]) {
            return parts[part < 4 ? part : 3];
        }
    }
    return NULL;
}

static void port_ShowTyped(
    char *value, u32 room, const char *typed, s32 hidden) {
    u32 i;
    u32 n = (u32)strlen(typed);

    if (!hidden) {
        snprintf(value, room, "%s_", typed);
        return;
    }
    for (i = 0; i < n && i + 2 < room; i++) {
        value[i] = '*';
    }
    value[i++] = '_';
    value[i] = 0;
}

void port_OnlineDescribe(
    s32 item, const char *typed, char *label, u32 label_room, char *value,
    u32 value_room, s32 *is_arrows) {
    *is_arrows = FALSE;
    value[0] = 0;
    if (typed != NULL) {
        port_ShowTyped(value, value_room, typed,
                       item == ONLINE_PASSWORD || item >= ONLINE_ROOM);
    }
    if (item >= ONLINE_ROOM) {
        const PortRoom *room = &g_online.rooms[item - ONLINE_ROOM];
        const char *differs = port_SetupDiffers(room->setup);

        snprintf(label, label_room, "%s%s   (%s)",
                 room->locked ? "[locked] " : "", room->name, room->player);
        if (typed != NULL) {
            return;
        }
        if (differs != NULL) {
            snprintf(value, value_room, "not for you: %s", differs);
        } else {
            snprintf(value, value_room, "%d/2%s", room->players,
                     room->players >= 2 ? ", full" : "");
        }
        return;
    }
    switch (item) {
    case ONLINE_ROLE:
        snprintf(label, label_room, "You");
        snprintf(value, value_room, "%s", g_roles[port_Role()]);
        *is_arrows = TRUE;
        break;
    case ONLINE_LOBBY:
        snprintf(label, label_room, "Lobby server");
        if (typed == NULL) {
            snprintf(value, value_room, "%s",
                     g_port_settings.net_lobby[0] != 0
                         ? g_port_settings.net_lobby
                         : "(none: Enter, then type it)");
        }
        break;
    case ONLINE_ROOM_NAME:
        snprintf(label, label_room, "Room's name");
        if (typed == NULL) {
            snprintf(value, value_room, "%s",
                     g_port_settings.net_room[0] != 0 ? g_port_settings.net_room
                                                      : "(your player name)");
        }
        break;
    case ONLINE_PASSWORD:
        snprintf(label, label_room, "%s",
                 port_Role() == PORT_NET_ROLE_ROOM
                     ? "Password (none: open "
                       "to all)"
                     : "A locked room's password");
        if (typed == NULL) {
            port_ShowTyped(value, value_room, g_online.password, TRUE);
            value[strlen(value) - 1] = 0;
            if (value[0] == 0) {
                snprintf(value, value_room, "(none)");
            }
        }
        break;
    case ONLINE_PLAYER:
        snprintf(label, label_room, "Your name");
        if (typed == NULL) {
            snprintf(value, value_room, "%s",
                     g_port_settings.net_player[0] != 0
                         ? g_port_settings.net_player
                         : "(Enter, then type it)");
        }
        break;
    case ONLINE_PORT:
        snprintf(label, label_room, "%s",
                 port_Role() == PORT_NET_ROLE_JOIN ? "Your own port"
                                                   : "The port to wait at");
        if (typed == NULL) {
            snprintf(value, value_room, "%d", g_port_settings.net_port);
            *is_arrows = TRUE;
        }
        break;
    case ONLINE_LISTEN:
        snprintf(label, label_room, "Through");
        snprintf(value, value_room, "%s",
                 g_port_settings.net_listen
                     ? "Every network of this PC"
                     : "This PC alone (" ADDRESS_LOCAL ")");
        *is_arrows = TRUE;
        break;
    case ONLINE_DELAY:
        snprintf(label, label_room, "Input delay");
        snprintf(value, value_room, "%d frame%s", g_port_settings.net_delay,
                 g_port_settings.net_delay == 1 ? "" : "s");
        *is_arrows = TRUE;
        break;
    case ONLINE_PORTMAP:
        snprintf(label, label_room, "Open the port in the router");
        snprintf(
            value, value_room, "%s",
            g_port_settings.net_portmap ? "Yes (UPnP, NAT-PMP, PCP)" : "No");
        *is_arrows = TRUE;
        break;
    case ONLINE_ADDRESS:
        snprintf(label, label_room, "The host's address and port");
        if (typed == NULL) {
            snprintf(value, value_room, "%s",
                     g_port_settings.net_address[0] != 0
                         ? g_port_settings.net_address
                         : "(Enter, then type it)");
        }
        break;
    case ONLINE_PUNCH_TO:
        snprintf(label, label_room, "The other player's address");
        if (typed == NULL) {
            snprintf(value, value_room, "%s",
                     g_port_settings.net_punch_to[0] != 0
                         ? g_port_settings.net_punch_to
                         : "(none: only if the router has no port open)");
        }
        break;
    case ONLINE_MY_ADDRESS:
        snprintf(label, label_room, "Your address for the other player");
        snprintf(value, value_room, "%s",
                 g_online.my_address[0] != 0 ? g_online.my_address
                                             : "(Enter: find it)");
        break;
    case ONLINE_REFRESH:
        snprintf(label, label_room, "Look for rooms");
        if (g_online.listed) {
            snprintf(value, value_room, "%d room%s", g_online.total,
                     g_online.total == 1 ? "" : "s");
        }
        break;
    case ONLINE_SETUP:
        snprintf(label, label_room, "Your setup code");
        snprintf(value, value_room, "%s", port_Setup());
        break;
    case ONLINE_START:
        snprintf(label, label_room, "%s",
                 port_Role() == PORT_NET_ROLE_ROOM   ? "Open the room now"
                 : port_Role() == PORT_NET_ROLE_HOST ? "Host now"
                                                     : "Join now");
        break;
    default:
        snprintf(label, label_room, "Back");
        break;
    }
}

const char *port_OnlineHelp(s32 item, s32 is_typing) {
    if (is_typing && (item == ONLINE_PASSWORD || item >= ONLINE_ROOM)) {
        return "Type the password.  Enter: keep it.  Escape: leave it";
    }
    if (is_typing) {
        return item == ONLINE_PORT
                   ? "Type it.  Enter: keep it.  Escape: "
                     "leave it"
                   : "Type it.  Enter: keep it (nothing: "
                     "none).  Escape: leave it";
    }
    if (item >= ONLINE_ROOM) {
        return "Enter: join this room (a locked one asks for its password "
               "first)";
    }
    switch (item) {
    case ONLINE_ROLE:
        return "A room at the lobby is the easy way; by address is for "
               "those who know theirs";
    case ONLINE_LOBBY:
        return "The server that keeps the rooms and finds the way between "
               "you";
    case ONLINE_ROOM_NAME:
        return "What the other player sees in Browse rooms";
    case ONLINE_PASSWORD:
        return "Kept only while the program runs; it never goes over the "
               "network as it is";
    case ONLINE_PLAYER:
        return "Shown in the lobby beside your room";
    case ONLINE_PORT:
        return "The UDP port this PC uses (Enter: type it)";
    case ONLINE_LISTEN:
        return "Every network: found from the internet; this PC alone: two "
               "windows here";
    case ONLINE_DELAY:
        return "Frames a press counts later: fewer rollbacks with more; the "
               "host's is both players'";
    case ONLINE_PORTMAP:
        return "Asks your router to let the other player in, and closes it "
               "again after";
    case ONLINE_ADDRESS:
        return "As 192.168.1.20:7845 or [2001:db8::7]:7845";
    case ONLINE_PUNCH_TO:
        return "Both type each other's address and start at once: both "
               "routers then let the two through";
    case ONLINE_MY_ADDRESS:
        return "Asks your router (or the lobby) what the internet sees you "
               "at";
    case ONLINE_REFRESH:
        return "Asks the lobby for its rooms again";
    case ONLINE_SETUP:
        return "The other player's must be the same: compare them by "
               "message";
    case ONLINE_START:
        return "The session begins here, from power on: both go to versus "
               "select";
    default:
        return "";
    }
}

/* ---- finding things out ---- */

static s32 port_Lobby(PortNetAddress *lobby) {
    return g_port_settings.net_lobby[0] != 0 &&
           port_ParseNetAddress(g_port_settings.net_lobby, lobby) &&
           lobby->port != 0;
}

static void port_FindMyAddress(void) {
    PortNetAddress lobby;
    PortNetAddress seen;
    char at[PORT_NET_ADDRESS_TEXT_BYTES];

    snprintf(g_online.my_address, sizeof(g_online.my_address),
             "not found: no router answered and no lobby is set");
    if (g_port_settings.net_portmap && g_port_settings.net_listen) {
        PortMapResult map;

        if (port_MapPort((u16)g_port_settings.net_port,
                         port_WindowPortMapOptions(), &map)) {
            /* only to see: the session asks again as it starts */
            port_UnmapPort();
            snprintf(
                g_online.my_address, sizeof(g_online.my_address),
                "%u.%u.%u.%u:%u (%s)", map.external_address >> 24,
                (map.external_address >> 16) & 0xFF,
                (map.external_address >> 8) & 0xFF, map.external_address & 0xFF,
                map.external_port, map.method);
            return;
        }
    }
    if (port_Lobby(&lobby) &&
        port_RelayWhoAmI(
            &lobby,
            (u16)(g_port_settings.net_listen ? g_port_settings.net_port : 0),
            &seen)) {
        port_FormatNetAddress(&seen, at, sizeof(at));
        snprintf(g_online.my_address, sizeof(g_online.my_address),
                 "%s (as the lobby sees it)", at);
    }
}

static void port_ListRooms(char *note, u32 note_room) {
    PortNetAddress lobby;

    g_online.room_count = 0;
    g_online.listed = TRUE;
    if (!port_Lobby(&lobby)) {
        snprintf(note, note_room, "Set the lobby server first");
        return;
    }
    g_online.room_count =
        port_RelayListRooms(&lobby, g_online.rooms, ROOMS_MAX, &g_online.total);
    if (g_online.room_count < 0) {
        g_online.room_count = 0;
        g_online.total = 0;
        snprintf(note, note_room, "The lobby at %s did not answer",
                 g_port_settings.net_lobby);
    }
    port_Note("online: the lobby has %d rooms", g_online.total);
}

/* ---- starting ---- */

static void port_PutHex(char *out, const u8 *bytes, u32 count) {
    u32 i;

    for (i = 0; i < count; i++) {
        sprintf(out + 2 * i, "%02x", bytes[i]);
    }
}

/* The settings kept, the menu closed, and the program started again as
 * the session's. `room` is the room joined (rooms), or NULL. */
static void port_Start(const PortRoom *room, char *note, u32 note_room) {
    static char bind[PORT_NET_ADDRESS_BYTES];
    static char key_text[HEX_BYTES];
    static char code[PORT_RELAY_CODE_BYTES];
    PortOnlineStart start;
    s32 role = port_Role();
    s32 is_local = !g_port_settings.net_listen;

    memset(&start, 0, sizeof(start));
    start.role = role;
    start.delay = g_port_settings.net_delay;
    start.portmap = g_port_settings.net_portmap && !is_local;
    start.player = g_port_settings.net_player;
    snprintf(
        bind, sizeof(bind), "%s:%d", is_local ? ADDRESS_LOCAL : ADDRESS_ALL,
        role == PORT_NET_ROLE_JOIN || role == PORT_NET_ROLE_ROOMS
            ? (is_local ? 0 : g_port_settings.net_port)
            : g_port_settings.net_port);
    start.bind = bind;
    if (role == PORT_NET_ROLE_ROOM || role == PORT_NET_ROLE_ROOMS) {
        if (g_port_settings.net_lobby[0] == 0) {
            snprintf(note, note_room, "Set the lobby server first");
            return;
        }
        start.lobby = g_port_settings.net_lobby;
    }
    if (role == PORT_NET_ROLE_ROOM) {
        start.room = g_port_settings.net_room[0] != 0 ? g_port_settings.net_room
                     : g_port_settings.net_player[0] != 0
                         ? g_port_settings.net_player
                         : "ReBushido";
        if (g_online.password[0] != 0) {
            u8 salt[PORT_RELAY_SALT_BYTES];
            u8 key[PORT_RELAY_KEY_BYTES];
            u64 seed =
                SDL_GetPerformanceCounter() ^ ((u64)SDL_GetTicks() << 32);
            u32 i;

            /* the salt only has to differ between rooms */
            for (i = 0; i < sizeof(salt); i++) {
                seed = seed * 6364136223846793005ULL + 1442695040888963407ULL;
                salt[i] = (u8)(seed >> 56);
            }
            port_RelayRoomKey(salt, g_online.password, key);
            port_PutHex(key_text, salt, sizeof(salt));
            key_text[2 * sizeof(salt)] = ':';
            port_PutHex(key_text + 2 * sizeof(salt) + 1, key, sizeof(key));
            start.key = key_text;
        }
    } else if (role == PORT_NET_ROLE_ROOMS) {
        snprintf(code, sizeof(code), "%s", room->code);
        start.code = code;
        if (room->locked) {
            u8 key[PORT_RELAY_KEY_BYTES];

            port_RelayRoomKey(room->salt, g_online.password, key);
            port_PutHex(key_text, room->salt, PORT_RELAY_SALT_BYTES);
            key_text[2 * PORT_RELAY_SALT_BYTES] = ':';
            port_PutHex(
                key_text + 2 * PORT_RELAY_SALT_BYTES + 1, key, sizeof(key));
            start.key = key_text;
        }
    } else if (role == PORT_NET_ROLE_JOIN) {
        if (g_port_settings.net_address[0] == 0) {
            snprintf(note, note_room, "Type the host's address and port first");
            return;
        }
        start.address = g_port_settings.net_address;
    } else if (g_port_settings.net_punch_to[0] != 0) {
        start.punch_to = g_port_settings.net_punch_to;
    }
    port_MenuLeave();
    {
        const char *problem = port_StartOnline(&start);

        if (problem != NULL) {
            snprintf(note, note_room, "%s", problem);
        }
    }
}

/* A room of the list chosen. */
static s32 port_ChooseRoom(
    s32 which, char *typed, u32 typed_room, char *note, u32 note_room) {
    const PortRoom *room = &g_online.rooms[which];
    const char *differs = port_SetupDiffers(room->setup);

    if (differs != NULL) {
        snprintf(note, note_room, "Not compatible: %s (theirs %s, yours %s)",
                 differs, room->setup, port_Setup());
        return PORT_ONLINE_NOTHING;
    }
    if (room->players >= 2) {
        snprintf(note, note_room, "That room is full");
        return PORT_ONLINE_NOTHING;
    }
    if (room->locked && g_online.password[0] == 0) {
        g_online.pending = which + 1;
        snprintf(note, note_room, "Room %s is locked: type its password",
                 room->name);
        typed[0] = 0;
        (void)typed_room;
        return PORT_ONLINE_TYPE;
    }
    port_Start(room, note, note_room);
    return PORT_ONLINE_NOTHING;
}

s32 port_OnlineAccept(
    s32 item, char *typed, u32 typed_room, char *note, u32 note_room) {
    if (item >= ONLINE_ROOM) {
        return port_ChooseRoom(
            item - ONLINE_ROOM, typed, typed_room, note, note_room);
    }
    switch (item) {
    case ONLINE_ROLE:
    case ONLINE_LISTEN:
    case ONLINE_DELAY:
    case ONLINE_PORTMAP:
        return port_OnlineStep(item, 1);
    case ONLINE_LOBBY:
        snprintf(typed, typed_room, "%s", g_port_settings.net_lobby);
        return PORT_ONLINE_TYPE;
    case ONLINE_ROOM_NAME:
        snprintf(typed, typed_room, "%s", g_port_settings.net_room);
        return PORT_ONLINE_TYPE;
    case ONLINE_PASSWORD:
        snprintf(typed, typed_room, "%s", g_online.password);
        return PORT_ONLINE_TYPE;
    case ONLINE_PLAYER:
        snprintf(typed, typed_room, "%s", g_port_settings.net_player);
        return PORT_ONLINE_TYPE;
    case ONLINE_PORT:
        snprintf(typed, typed_room, "%d", g_port_settings.net_port);
        return PORT_ONLINE_TYPE;
    case ONLINE_ADDRESS:
        snprintf(typed, typed_room, "%s", g_port_settings.net_address);
        return PORT_ONLINE_TYPE;
    case ONLINE_PUNCH_TO:
        snprintf(typed, typed_room, "%s", g_port_settings.net_punch_to);
        return PORT_ONLINE_TYPE;
    case ONLINE_MY_ADDRESS:
        port_FindMyAddress();
        port_Note("online: your address for the other player: %s",
                  g_online.my_address);
        return PORT_ONLINE_NOTHING;
    case ONLINE_REFRESH:
        port_ListRooms(note, note_room);
        return PORT_ONLINE_REBUILD;
    case ONLINE_START:
        port_Start(NULL, note, note_room);
        return PORT_ONLINE_NOTHING;
    default:
        return PORT_ONLINE_NOTHING;
    }
}

s32 port_OnlineStep(s32 item, s32 step) {
    char value[16];

    switch (item) {
    case ONLINE_ROLE:
        g_port_settings.net_role = (g_port_settings.net_role + step + 4) % 4;
        port_MenuSetLine("net_role", g_role_words[g_port_settings.net_role]);
        return PORT_ONLINE_REBUILD;
    case ONLINE_PORT:
        g_port_settings.net_port += step;
        if (g_port_settings.net_port < 1) {
            g_port_settings.net_port = PORT_NET_PORT_MAX;
        } else if (g_port_settings.net_port > PORT_NET_PORT_MAX) {
            g_port_settings.net_port = 1;
        }
        snprintf(value, sizeof(value), "%d", g_port_settings.net_port);
        port_MenuSetLine("net_port", value);
        return PORT_ONLINE_NOTHING;
    case ONLINE_LISTEN:
        g_port_settings.net_listen = !g_port_settings.net_listen;
        port_MenuSetLine(
            "net_listen", g_port_settings.net_listen ? "all" : "local");
        return PORT_ONLINE_NOTHING;
    case ONLINE_DELAY:
        g_port_settings.net_delay =
            (g_port_settings.net_delay + step + PORT_NET_DELAY_MAX + 1) %
            (PORT_NET_DELAY_MAX + 1);
        snprintf(value, sizeof(value), "%d", g_port_settings.net_delay);
        port_MenuSetLine("net_delay", value);
        return PORT_ONLINE_NOTHING;
    case ONLINE_PORTMAP:
        g_port_settings.net_portmap = !g_port_settings.net_portmap;
        port_MenuSetLine(
            "net_portmap", g_port_settings.net_portmap ? "on" : "off");
        return PORT_ONLINE_NOTHING;
    default:
        return PORT_ONLINE_NOTHING;
    }
}

s32 port_OnlineTakes(s32 item, char c) {
    if (item >= ONLINE_ROOM) {
        /* a locked room's password */
        item = ONLINE_PASSWORD;
    }
    switch (item) {
    case ONLINE_PORT:
        return c >= '0' && c <= '9';
    case ONLINE_LOBBY:
    case ONLINE_ADDRESS:
    case ONLINE_PUNCH_TO:
        return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
               (c >= 'A' && c <= 'F') || c == '.' || c == ':' || c == '[' ||
               c == ']';
    case ONLINE_PASSWORD:
        return c > ' ' && c <= '~';
    default:
        return port_IsNameLetter(c);
    }
}

/* An address typed, or nothing where `empty` may be: 1 if it is one. */
static s32 port_IsAddress(const char *typed, s32 empty) {
    PortNetAddress at;

    if (typed[0] == 0) {
        return empty;
    }
    return port_ParseNetAddress(typed, &at) && at.port != 0;
}

s32 port_OnlineKeep(s32 item, const char *typed, char *note, u32 note_room) {
    char *out = NULL;
    u32 room = 0;
    const char *name = NULL;

    if (item >= ONLINE_ROOM) {
        /* a locked room's password, typed on its row */
        item = ONLINE_PASSWORD;
    }
    switch (item) {
    case ONLINE_PORT: {
        s32 number = atoi(typed);

        if (number < 1 || number > PORT_NET_PORT_MAX) {
            break;
        }
        g_port_settings.net_port = number;
        port_MenuSetLine("net_port", typed);
        return PORT_ONLINE_NOTHING;
    }
    case ONLINE_LOBBY:
        if (!port_IsAddress(typed, TRUE)) {
            break;
        }
        out = g_port_settings.net_lobby;
        room = sizeof(g_port_settings.net_lobby);
        name = "net_lobby";
        g_online.listed = FALSE;
        g_online.room_count = 0;
        break;
    case ONLINE_ADDRESS:
        if (!port_IsAddress(typed, FALSE)) {
            break;
        }
        out = g_port_settings.net_address;
        room = sizeof(g_port_settings.net_address);
        name = "net_address";
        break;
    case ONLINE_PUNCH_TO:
        if (!port_IsAddress(typed, TRUE)) {
            break;
        }
        out = g_port_settings.net_punch_to;
        room = sizeof(g_port_settings.net_punch_to);
        name = "net_punch_to";
        break;
    case ONLINE_ROOM_NAME:
        out = g_port_settings.net_room;
        room = sizeof(g_port_settings.net_room);
        name = "net_room";
        break;
    case ONLINE_PLAYER:
        out = g_port_settings.net_player;
        room = sizeof(g_port_settings.net_player);
        name = "net_player";
        break;
    case ONLINE_PASSWORD:
        snprintf(g_online.password, sizeof(g_online.password), "%s", typed);
        if (g_online.pending != 0) {
            s32 which = g_online.pending - 1;

            g_online.pending = 0;
            if (typed[0] != 0 && which < g_online.room_count) {
                port_Start(&g_online.rooms[which], note, note_room);
            }
        }
        return PORT_ONLINE_NOTHING;
    default:
        return PORT_ONLINE_NOTHING;
    }
    if (out == NULL || strlen(typed) + 1 > room) {
        snprintf(note, note_room, "Not kept: %s", typed);
        return -1;
    }
    snprintf(out, room, "%s", typed);
    port_MenuSetLine(name, typed);
    return PORT_ONLINE_NOTHING;
}
