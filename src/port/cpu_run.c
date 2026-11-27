/* The run's own of the port's CPU (port_cpu.h): the seed --cpu-seed asked
 * for, and the log --cpu-log writes. Outside the state: a load puts
 * neither back, and the log only goes forward. */
#include "common.h"
#include "port_host.h"
#include "bb2.h"

#define LOG_LINE_BYTES 160
/* the longest name of a CPU, and of a level's number, with the 0 */
#define NAME_BYTES 16
#define FIELD_BYTES 24
#define DECIMAL 10

static u32 g_port_cpu_asked_seed;
static s32 g_port_cpu_log = -1;
static const char *g_port_cpu_log_path;

void port_CpuSetAskedSeed(u32 seed) { g_port_cpu_asked_seed = seed; }

u32 port_CpuAskedSeed(void) { return g_port_cpu_asked_seed; }

void port_CpuSetLog(const char *path) { g_port_cpu_log_path = path; }

/* The CPUs' names, as --cpu and the window's cpu setting call them. */
const char * const g_port_cpu_names[PORT_CPU_LEVELS + 1] = {
    "original", "novice", "adept", "veteran", "master"};

/* One place's CPU: a name or its number. */
static s32 port_CpuParseOne(const char *text, u32 length) {
    char word[NAME_BYTES];
    s32 i;

    if (length == 0 || length >= sizeof(word)) {
        return -1;
    }
    for (i = 0; i < (s32)length; i++) {
        word[i] = text[i];
    }
    word[length] = 0;
    if (length == 1 && word[0] >= '0' && word[0] <= '0' + PORT_CPU_LEVELS) {
        return word[0] - '0';
    }
    for (i = 0; i <= PORT_CPU_LEVELS; i++) {
        if (port_TextsEqual(word, g_port_cpu_names[i])) {
            return i;
        }
    }
    return -1;
}

/* --cpu: a CPU for both places, or two with a comma between. */
s32 port_CpuParseLevels(const char *text) {
    u32 comma = 0;
    s32 first;
    s32 second;

    while (text[comma] != 0 && text[comma] != ',') {
        comma++;
    }
    first = port_CpuParseOne(text, comma);
    if (text[comma] == ',') {
        u32 end = comma + 1;

        while (text[end] != 0) {
            end++;
        }
        second = port_CpuParseOne(text + comma + 1, end - comma - 1);
    } else {
        second = first;
    }
    if (first < 0 || second < 0) {
        return FALSE;
    }
    port_SetCpuLevels(first, second);
    return TRUE;
}

/* --cpu-tune LEVEL:FIELD=VALUE,FIELD=VALUE...: for trying numbers. */
s32 port_CpuParseTune(const char *text) {
    char field[FIELD_BYTES];
    s32 level;
    u32 at = 0;

    if (text[0] < '1' || text[0] > '0' + PORT_CPU_LEVELS || text[1] != ':') {
        return FALSE;
    }
    level = text[0] - '0';
    text += 2;
    while (text[at] != 0) {
        u32 length = 0;
        s32 value = 0;
        s32 sign = 1;

        while (text[at] != 0 && text[at] != '=' && length + 1 < sizeof(field)) {
            field[length++] = text[at++];
        }
        field[length] = 0;
        if (text[at] != '=') {
            return FALSE;
        }
        at++;
        if (text[at] == '-') {
            sign = -1;
            at++;
        }
        while (text[at] >= '0' && text[at] <= '9') {
            value = value * DECIMAL + (text[at++] - '0');
        }
        if (!port_CpuTune(level, field, sign * value)) {
            return FALSE;
        }
        if (text[at] == ',') {
            at++;
        } else if (text[at] != 0) {
            return FALSE;
        }
    }
    return TRUE;
}

static void port_CpuLogLine(const char *format, ...) {
    char line[LOG_LINE_BYTES];
    PortArgs args;
    u32 length;

    PORT_ARGS_START(args, format);
    length = port_FormatText(line, sizeof(line), format, args);
    PORT_ARGS_END(args);
    port_HostAppend(g_port_cpu_log, line, length);
}

/* A fighter's pad as it is given to him, and where he is and what he is
 * doing as the frame begins for him: the game's frame, the fighter, 1 for
 * the game's CPU or 2 for the port's, the move's kind, its motion and its
 * frame, the frame its blade begins to hit, which attack button began
 * it, the place, his wounds (legs, arm, body, the off arm), the buttons
 * held, whether he has fallen, and the CPU's pad script (its kind, 1
 * up: enum CpuScriptKind) or 0. */
void port_CpuLogPad(s32 fighter_index, const PadState *pad) {
    Fighter *fighter = &g_fighters[fighter_index];

    s32 script = 0;

    if (g_port_cpu_log_path == NULL) {
        return;
    }
    if (port_CpuPlays(fighter_index)) {
        script = port_CpuScriptKind(fighter_index);
    } else if (fighter->is_cpu && fighter->cpu_script != NULL) {
        script = fighter->cpu_script_kind + 1;
    }
    if (g_port_cpu_log < 0) {
        g_port_cpu_log = port_HostCreate(g_port_cpu_log_path);
        if (g_port_cpu_log < 0) {
            port_Log(
                "rebushido: --cpu-log cannot write %s\n", g_port_cpu_log_path);
            g_port_cpu_log_path = NULL;
            return;
        }
    }
    port_CpuLogLine(
        "f %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
        (s32)g_disp_frame_count, fighter_index,
        fighter->is_cpu ? (port_CpuPlays(fighter_index) ? 2 : 1) : 0,
        (s32)fighter->move_kind, (s32)fighter->motion_index,
        (s32)fighter->move_frame, (s32)fighter->hit_start_frames[0],
        (s32)fighter->attack_kind, fighter->world_pos.x, fighter->world_pos.z,
        (s32)fighter->leg_wounds, (s32)fighter->arm_wounds,
        (s32)fighter->body_wounds, (s32)fighter->off_arm_ok,
        (s32)(pad->held & 0xFFFF), (s32)fighter->is_defeated, script);
}
