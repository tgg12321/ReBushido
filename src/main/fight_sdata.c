/* A few of the fight's values that sit in the small data, between those of
 * fighter.c and of fighter_hit.c, and that no code reaches through the gp
 * register. Each is declared in its subsystem's header (fight.h, fighter.h,
 * motion.h).
 *
 * The file has no code. It is built without small data, and the linker
 * script puts its data among the small data. */
#include "common.h"
#include "bb2.h"

/* The colour of the practice partner, for each clan of the player: red,
 * green, blue and a byte to make four. They are the first band of henchmen's
 * colours in the two clans' story scripts. The third row is no clan's and
 * is not read. */
u8 g_fight_practice_tints[3][4] = {
    {3, 51, 36, 0},  /* the player is of the Narukagami */
    {80, 17, 45, 0}, /* the player is of the Shainto */
    {38, 0, 0, 0},
};

/* The top speed of each class of runner (g_fighter_run_classes), in units
 * a frame for a fighter of full size, and a fourth class nobody is of. */
s16 g_fighter_run_speed_limits[4] = {166, 200, 233, 0};

/* Motion files that the table of them (g_motion_set_files) names. */
char g_str_motion_file_on[8] = "ON.BBM";
char g_str_motion_file_gs[8] = "GS.BBM";
char g_str_motion_file_gn[8] = "GN.BBM";

/* Which way the blood of the kaishaku goes: along x and a quarter as much
 * up. */
SVECTOR g_kaishaku_blood_dir = {ONE, -ONE / 4, 0, 0};

/* How near a fighter must be for ROSTER_ISOHACHI to stagger it. */
s32 g_fight_stun_reach = 3000;

/* A unit along y: turned by the torso's matrix it is the torso's axis
 * (fighter_PlacePoints). */
SVECTOR g_unit_y_vector = {0, ONE, 0, 0};
