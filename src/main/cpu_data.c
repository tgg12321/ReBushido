/* The CPU opponent's tables (cpu.h): each stage's obstacles, where it
 * probes the ground, how far the weapons reach, the moves it follows up,
 * its built-in pad scripts, and what it may do in each mode. Its profiles
 * are in cpu.c.
 *
 * The file has no code. It is built without small data, which keeps its
 * small tables here among the others. */
#include "common.h"
#include "bb2.h"

/* For each weapon of the opponent's, a bit for each stance class of its
 * that is better stepped round than met. */
#define CLASS(stance_class) (1 << (stance_class))

u8 g_cpu_side_step_stances[8] = {
    CLASS(1) | CLASS(STANCE_CLASS_TWO_SWORDS), /* WEAPON_ID_KATANA */
    CLASS(1),                                  /* WEAPON_ID_LONG_SWORD */
    CLASS(1),                                  /* WEAPON_ID_NODACHI */
    CLASS(1),                                  /* WEAPON_ID_BROADSWORD */
    CLASS(2),                                  /* WEAPON_ID_NAGINATA */
    CLASS(0) | CLASS(1) | CLASS(2),            /* WEAPON_ID_YARI */
    0,                                         /* the two guns */
    0};

#undef CLASS

/* ---- the stages' obstacles ----
 * What the CPU walks round on each stage: chains of corners (x and z);
 * NAV_CHAIN_OPEN marks one whose ends do not meet. A stage's tables
 * are named for it, by the name the game shows for it: on the stage select
 * screen, or on its story stage card (enum StageIndex in stage.h).
 *
 * A chain (NavChain) is: its flags (what bit 0x40 is for is not known); the
 * kind given to the waypoints made round it; how far out from a corner to
 * walk, in units of 40; how many corners; the corners. */
static s16 g_cpu_meikyokan_dojo_nav_chain_0_vertices[6][2] = {
    {8000, 4600}, {8550, 4270}, {8550, 3720},
    {8000, 3440}, {7440, 3720}, {7440, 4270}};
static s16 g_cpu_meikyokan_dojo_nav_chain_1_vertices[6][2] = {
    {8000, -3390}, {8550, -3720}, {8550, -4280},
    {8000, -4550}, {7440, -4280}, {7440, -3720}};
static s16 g_cpu_meikyokan_dojo_nav_chain_2_vertices[4][2] = {
    {-10000, 4400}, {-9653, 4200}, {-9653, 3800}, {-10000, 3600}};
static s16 g_cpu_meikyokan_dojo_nav_chain_3_vertices[4][2] = {
    {-10000, -3600}, {-9653, -3800}, {-9653, -4200}, {-10000, -4400}};
static NavChain g_cpu_meikyokan_dojo_nav_chains[4] = {
    {0, 6, 37, 6, g_cpu_meikyokan_dojo_nav_chain_0_vertices},
    {0, 6, 37, 6, g_cpu_meikyokan_dojo_nav_chain_1_vertices},
    {NAV_CHAIN_OPEN, 6, 37, 4, g_cpu_meikyokan_dojo_nav_chain_2_vertices},
    {NAV_CHAIN_OPEN, 6, 37, 4, g_cpu_meikyokan_dojo_nav_chain_3_vertices}};
static s16 g_cpu_bus_stop_nav_chain_0_vertices[3][2] = {
    {-4000, 14000}, {-4000, 10000}, {-12000, 10000}};
static s16 g_cpu_bus_stop_nav_chain_1_vertices[4][2] = {
    {-4000, 2500}, {-3500, 2000}, {-4000, 1500}, {-4500, 2000}};
static NavChain g_cpu_bus_stop_nav_chains[2] = {
    {NAV_CHAIN_OPEN, 3, 37, 3, g_cpu_bus_stop_nav_chain_0_vertices},
    {0, 6, 37, 4, g_cpu_bus_stop_nav_chain_1_vertices}};
static s16 g_cpu_tatara_shrine_path_nav_chain_0_vertices[10][2] = {
    {-22000, -2000}, {-20000, -2000}, {-16000, 1000}, {-14000, 1000},
    {-8000, -3000},  {-6000, -3000},  {-2000, -5000}, {8000, -5000},
    {14000, -9000},  {20000, -9000}};
static s16 g_cpu_tatara_shrine_path_nav_chain_1_vertices[10][2] = {
    {20000, 1000},  {14000, 1000}, {8000, 5000},    {-2000, 5000},
    {-6000, 7000},  {-8000, 7000}, {-14000, 11000}, {-16000, 11000},
    {-20000, 8000}, {-22000, 8000}};
static NavChain g_cpu_tatara_shrine_path_nav_chains[2] = {
    {NAV_CHAIN_OPEN, 3, 37, 10, g_cpu_tatara_shrine_path_nav_chain_0_vertices},
    {NAV_CHAIN_OPEN, 3, 37, 10, g_cpu_tatara_shrine_path_nav_chain_1_vertices}};
static s16 g_cpu_bamboo_thicket_nav_chain_0_vertices[4][2] = {
    {1950, 2550}, {1050, 2550}, {1050, 3540}, {1950, 3540}};
static s16 g_cpu_bamboo_thicket_nav_chain_1_vertices[7][2] = {
    {12000, -8000}, {14000, -6000}, {14000, -2000}, {13000, 0},
    {13000, 6000},  {12000, 8000},  {12000, 10000}};
static s16 g_cpu_bamboo_thicket_nav_chain_2_vertices[6][2] = {
    {-8000, 8000}, {-8000, 6000},  {-10000, 4000},
    {-10000, 0},   {-8000, -2000}, {-8000, -8000}};
static NavChain g_cpu_bamboo_thicket_nav_chains[3] = {
    {0, 3, 37, 4, g_cpu_bamboo_thicket_nav_chain_0_vertices},
    {NAV_CHAIN_OPEN, 3, 37, 7, g_cpu_bamboo_thicket_nav_chain_1_vertices},
    {NAV_CHAIN_OPEN, 3, 37, 6, g_cpu_bamboo_thicket_nav_chain_2_vertices}};
static s16 g_cpu_outside_meikyokan_nav_chain_0_vertices[3][2] = {
    {4000, -20000}, {4000, -4000}, {12000, -4000}};
static s16 g_cpu_outside_meikyokan_nav_chain_1_vertices[3][2] = {
    {-14000, -4000}, {-6000, -4000}, {-6000, -20000}};
static s16 g_cpu_outside_meikyokan_nav_chain_2_vertices[4][2] = {
    {7189, 11989}, {7189, 6010}, {6810, 6010}, {6810, 11989}};
static s16 g_cpu_outside_meikyokan_nav_chain_3_vertices[4][2] = {
    {3000, 20000}, {3000, 17000}, {-5000, 17000}, {-5000, 20000}};
static NavChain g_cpu_outside_meikyokan_nav_chains[4] = {
    {NAV_CHAIN_OPEN, 3, 37, 3, g_cpu_outside_meikyokan_nav_chain_0_vertices},
    {NAV_CHAIN_OPEN, 3, 37, 3, g_cpu_outside_meikyokan_nav_chain_1_vertices},
    {0x40, 3, 37, 4, g_cpu_outside_meikyokan_nav_chain_2_vertices},
    {NAV_CHAIN_OPEN, 3, 37, 4, g_cpu_outside_meikyokan_nav_chain_3_vertices}};
static s16 g_cpu_future_dump_site_nav_chain_0_vertices[3][2] = {
    {18000, -20000}, {22000, 0}, {30000, 8000}};
static s16 g_cpu_future_dump_site_nav_chain_1_vertices[3][2] = {
    {4000, 24000}, {-8000, 12000}, {-16000, 12000}};
static s16 g_cpu_future_dump_site_nav_chain_2_vertices[4][2] = {
    {7500, 15500}, {7500, 12500}, {4500, 12500}, {4500, 15500}};
static NavChain g_cpu_future_dump_site_nav_chains[3] = {
    {NAV_CHAIN_OPEN, 3, 37, 3, g_cpu_future_dump_site_nav_chain_0_vertices},
    {NAV_CHAIN_OPEN, 3, 37, 3, g_cpu_future_dump_site_nav_chain_1_vertices},
    {0, 3, 37, 4, g_cpu_future_dump_site_nav_chain_2_vertices}};
static s16 g_cpu_snake_cliff_nav_chain_0_vertices[5][2] = {
    {1615, -16215}, {-3848, -8140}, {0, -4625}, {3800, -4625}, {3800, -15740}};
static s16 g_cpu_snake_cliff_nav_chain_1_vertices[5][2] = {
    {3800, 16265}, {3800, 5720}, {-4000, 6370}, {-4465, 8760}, {1235, 15978}};
static NavChain g_cpu_snake_cliff_nav_chains[2] = {
    {NAV_CHAIN_OPEN, 3, 37, 5, g_cpu_snake_cliff_nav_chain_0_vertices},
    {NAV_CHAIN_OPEN, 3, 37, 5, g_cpu_snake_cliff_nav_chain_1_vertices}};
static s16 g_cpu_kimigaura_coast_nav_chain_0_vertices[3][2] = {
    {16000, 4000}, {10000, 10000}, {10000, 12000}};
static s16 g_cpu_kimigaura_coast_nav_chain_1_vertices[3][2] = {
    {13000, -8000}, {13000, -6000}, {16000, -2000}};
static NavChain g_cpu_kimigaura_coast_nav_chains[2] = {
    {NAV_CHAIN_OPEN, 6, 37, 3, g_cpu_kimigaura_coast_nav_chain_0_vertices},
    {NAV_CHAIN_OPEN, 6, 37, 3, g_cpu_kimigaura_coast_nav_chain_1_vertices}};
static s16 g_cpu_orange_tree_fields_nav_chain_0_vertices[3][2] = {
    {3923, 16289}, {4289, 15921}, {3787, 15788}};
static s16 g_cpu_orange_tree_fields_nav_chain_1_vertices[3][2] = {
    {-4076, 15289}, {-3710, 14921}, {-4212, 14788}};
static s16 g_cpu_orange_tree_fields_nav_chain_2_vertices[3][2] = {
    {5923, 6289}, {6289, 5921}, {5787, 5788}};
static s16 g_cpu_orange_tree_fields_nav_chain_3_vertices[3][2] = {
    {-2076, 6289}, {-1710, 5921}, {-2212, 5788}};
static s16 g_cpu_orange_tree_fields_nav_chain_4_vertices[3][2] = {
    {-2076, -1710}, {-1710, -2078}, {-2212, -2211}};
static NavChain g_cpu_orange_tree_fields_nav_chains[5] = {
    {0, 6, 37, 3, g_cpu_orange_tree_fields_nav_chain_0_vertices},
    {0, 6, 37, 3, g_cpu_orange_tree_fields_nav_chain_1_vertices},
    {0, 6, 37, 3, g_cpu_orange_tree_fields_nav_chain_2_vertices},
    {0, 6, 37, 3, g_cpu_orange_tree_fields_nav_chain_3_vertices},
    {0, 6, 37, 3, g_cpu_orange_tree_fields_nav_chain_4_vertices}};
static s16 g_cpu_terraced_fields_nav_chain_0_vertices[4][2] = {
    {6220, 1030}, {6220, 390}, {1750, 390}, {1750, 1030}};
static s16 g_cpu_terraced_fields_nav_chain_1_vertices[4][2] = {
    {-2250, -8000}, {-2250, -9420}, {-6720, -9420}, {-6720, -8000}};
static NavChain g_cpu_terraced_fields_nav_chains[2] = {
    {0, 3, 37, 4, g_cpu_terraced_fields_nav_chain_0_vertices},
    {0, 3, 37, 4, g_cpu_terraced_fields_nav_chain_1_vertices}};
static s16 g_cpu_under_railway_nav_chain_0_vertices[4][2] = {
    {20000, -4000}, {16000, -4000}, {16000, 4000}, {20000, 4000}};
static NavChain g_cpu_under_railway_nav_chains[1] = {
    {NAV_CHAIN_OPEN, 3, 37, 4, g_cpu_under_railway_nav_chain_0_vertices}};
static s16 g_cpu_water_tower_dam_nav_chain_0_vertices[3][2] = {
    {-6000, 6000}, {-6000, 4000}, {-8000, 4000}};
static s16 g_cpu_water_tower_dam_nav_chain_1_vertices[4][2] = {
    {-8000, -2000}, {-6000, -2000}, {-6000, -6000}, {-8000, -6000}};
static s16 g_cpu_water_tower_dam_nav_chain_2_vertices[4][2] = {
    {8000, -6000}, {6000, -6000}, {6000, -2000}, {8000, -2000}};
static NavChain g_cpu_water_tower_dam_nav_chains[3] = {
    {NAV_CHAIN_OPEN, 3, 37, 3, g_cpu_water_tower_dam_nav_chain_0_vertices},
    {NAV_CHAIN_OPEN, 3, 37, 4, g_cpu_water_tower_dam_nav_chain_1_vertices},
    {NAV_CHAIN_OPEN, 3, 37, 4, g_cpu_water_tower_dam_nav_chain_2_vertices}};
static s16 g_cpu_lighthouse_nav_chain_0_vertices[4][2] = {
    {0, 14000}, {0, 12000}, {-8000, 4000}, {-12000, 4000}};
static NavChain g_cpu_lighthouse_nav_chains[1] = {
    {NAV_CHAIN_OPEN, 3, 37, 4, g_cpu_lighthouse_nav_chain_0_vertices}};
static s16 g_cpu_kabuki_stage_nav_chain_0_vertices[3][2] = {
    {-5000, -9000}, {-5000, -2900}, {4000, -2900}};
static NavChain g_cpu_kabuki_stage_nav_chains[1] = {
    {NAV_CHAIN_OPEN, 3, 37, 3, g_cpu_kabuki_stage_nav_chain_0_vertices}};
static s16 g_cpu_ferry_dock_nav_chain_0_vertices[4][2] = {
    {-5500, -4420}, {-4620, -4420}, {-4620, -5600}, {-5500, -5600}};
static s16 g_cpu_ferry_dock_nav_chain_1_vertices[4][2] = {
    {5500, -5600}, {4620, -5600}, {4620, -4420}, {5500, -4420}};
static s16 g_cpu_ferry_dock_nav_chain_2_vertices[4][2] = {
    {5500, -10100}, {4620, -10100}, {4620, -8910}, {5500, -8910}};
static s16 g_cpu_ferry_dock_nav_chain_3_vertices[4][2] = {
    {-5500, -8910}, {-4620, -8910}, {-4620, -10100}, {-5500, -10100}};
static NavChain g_cpu_ferry_dock_nav_chains[4] = {
    {NAV_CHAIN_OPEN | 0x40, 3, 37, 4, g_cpu_ferry_dock_nav_chain_0_vertices},
    {NAV_CHAIN_OPEN | 0x40, 3, 37, 4, g_cpu_ferry_dock_nav_chain_1_vertices},
    {NAV_CHAIN_OPEN | 0x40, 3, 37, 4, g_cpu_ferry_dock_nav_chain_2_vertices},
    {NAV_CHAIN_OPEN | 0x40, 3, 37, 4, g_cpu_ferry_dock_nav_chain_3_vertices}};
static s16 g_cpu_shainto_gate_nav_chain_0_vertices[6][2] = {
    {4500, 866}, {5000, 0}, {4500, -866}, {3500, -866}, {3000, 0}, {3500, 866}};
static s16 g_cpu_shainto_gate_nav_chain_1_vertices[6][2] = {
    {-5500, 866},  {-5000, 0}, {-5500, -866},
    {-6500, -866}, {-7000, 0}, {-6500, 866}};
static s16 g_cpu_shainto_gate_nav_chain_2_vertices[5][2] = {
    {14500, 866}, {13500, 0}, {13000, 0}, {13500, 866}, {14500, 866}};
static s16 g_cpu_shainto_gate_nav_chain_3_vertices[5][2] = {
    {-16500, 866}, {-15500, 866}, {-15000, 0}, {-15500, -866}, {-16500, -866}};
static NavChain g_cpu_shainto_gate_nav_chains[4] = {
    {0, 3, 37, 6, g_cpu_shainto_gate_nav_chain_0_vertices},
    {0, 3, 37, 6, g_cpu_shainto_gate_nav_chain_1_vertices},
    {NAV_CHAIN_OPEN, 3, 37, 5, g_cpu_shainto_gate_nav_chain_2_vertices},
    {NAV_CHAIN_OPEN, 3, 37, 5, g_cpu_shainto_gate_nav_chain_3_vertices}};
static s16 g_cpu_purple_cavern_nav_chain_0_vertices[3][2] = {
    {9160, 4120}, {5160, 2000}, {1160, 8120}};
static s16 g_cpu_purple_cavern_nav_chain_1_vertices[3][2] = {
    {-6840, 6120}, {-8840, 4120}, {-10840, 4120}};
static s16 g_cpu_purple_cavern_nav_chain_2_vertices[3][2] = {
    {3160, -11880}, {5160, -9880}, {9160, -9880}};
static NavChain g_cpu_purple_cavern_nav_chains[3] = {
    {NAV_CHAIN_OPEN, 3, 37, 3, g_cpu_purple_cavern_nav_chain_0_vertices},
    {NAV_CHAIN_OPEN, 3, 37, 3, g_cpu_purple_cavern_nav_chain_1_vertices},
    {NAV_CHAIN_OPEN, 3, 37, 3, g_cpu_purple_cavern_nav_chain_2_vertices}};
static s16 g_cpu_abandoned_station_nav_chain_0_vertices[4][2] = {
    {-4375, 16250}, {-4375, 15750}, {-4625, 15750}, {-4625, 16250}};
static s16 g_cpu_abandoned_station_nav_chain_1_vertices[3][2] = {
    {-12000, -4000}, {-8000, -4000}, {-8000, -24000}};
static NavChain g_cpu_abandoned_station_nav_chains[2] = {
    {0, 6, 37, 4, g_cpu_abandoned_station_nav_chain_0_vertices},
    {NAV_CHAIN_OPEN, 3, 37, 3, g_cpu_abandoned_station_nav_chain_1_vertices}};
static s16 g_cpu_railway_nav_chain_0_vertices[3][2] = {
    {20000, 4000}, {-12000, 4000}, {-12000, 16000}};
static s16 g_cpu_railway_nav_chain_1_vertices[3][2] = {
    {-12000, -16000}, {-12000, -4000}, {20000, -4000}};
static NavChain g_cpu_railway_nav_chains[2] = {
    {NAV_CHAIN_OPEN, 3, 50, 3, g_cpu_railway_nav_chain_0_vertices},
    {NAV_CHAIN_OPEN, 3, 50, 3, g_cpu_railway_nav_chain_1_vertices}};
static s16 g_cpu_abandoned_shipyard_nav_chain_0_vertices[3][2] = {
    {12000, 2000}, {10000, 2000}, {10000, 18000}};
static s16 g_cpu_abandoned_shipyard_nav_chain_1_vertices[3][2] = {
    {-10000, 18000}, {-10000, 14000}, {-12000, 14000}};
static NavChain g_cpu_abandoned_shipyard_nav_chains[2] = {
    {NAV_CHAIN_OPEN, 3, 37, 3, g_cpu_abandoned_shipyard_nav_chain_0_vertices},
    {NAV_CHAIN_OPEN, 3, 37, 3, g_cpu_abandoned_shipyard_nav_chain_1_vertices}};
static s16 g_cpu_building_rooftop_nav_chain_0_vertices[3][2] = {
    {12000, 4000}, {4000, 4000}, {4000, 12000}};
static NavChain g_cpu_building_rooftop_nav_chains[1] = {
    {NAV_CHAIN_OPEN, 3, 37, 3, g_cpu_building_rooftop_nav_chain_0_vertices}};
static s16 g_cpu_building_parking_lot_nav_chain_0_vertices[4][2] = {
    {10000, -2000}, {0, -2000}, {0, 2000}, {10000, 2000}};
static s16 g_cpu_building_parking_lot_nav_chain_1_vertices[4][2] = {
    {-10000, 4000}, {-9000, 4000}, {-9000, -4000}, {-10000, -4000}};
static s16 g_cpu_building_parking_lot_nav_chain_2_vertices[3][2] = {
    {-9000, 20000}, {-9000, 14000}, {-10000, 14000}};
static s16 g_cpu_building_parking_lot_nav_chain_3_vertices[3][2] = {
    {-10000, -14000}, {-9000, -14000}, {-9000, -20000}};
static NavChain g_cpu_building_parking_lot_nav_chains[4] = {
    {NAV_CHAIN_OPEN, 3, 37, 4, g_cpu_building_parking_lot_nav_chain_0_vertices},
    {NAV_CHAIN_OPEN, 3, 37, 4, g_cpu_building_parking_lot_nav_chain_1_vertices},
    {NAV_CHAIN_OPEN, 3, 37, 3, g_cpu_building_parking_lot_nav_chain_2_vertices},
    {NAV_CHAIN_OPEN, 3, 37, 3,
     g_cpu_building_parking_lot_nav_chain_3_vertices}};
static s16 g_cpu_shainto_cemetery_nav_chain_0_vertices[4][2] = {
    {-13500, -10000}, {-2000, -10000}, {-4000, -4000}, {16000, -4000}};
static s16 g_cpu_shainto_cemetery_nav_chain_1_vertices[4][2] = {
    {-8620, 1640}, {-8620, 350}, {-10000, 350}, {-10000, 1640}};
static s16 g_cpu_shainto_cemetery_nav_chain_2_vertices[4][2] = {
    {-340, 1640}, {-340, 350}, {-5650, 350}, {-5650, 1640}};
static s16 g_cpu_shainto_cemetery_nav_chain_3_vertices[4][2] = {
    {11180, 1640}, {11180, 350}, {4370, 350}, {4370, 1640}};
static NavChain g_cpu_shainto_cemetery_nav_chains[4] = {
    {NAV_CHAIN_OPEN | 0x40, 3, 37, 4,
     g_cpu_shainto_cemetery_nav_chain_0_vertices},
    {0x40, 3, 37, 4, g_cpu_shainto_cemetery_nav_chain_1_vertices},
    {0x40, 3, 37, 4, g_cpu_shainto_cemetery_nav_chain_2_vertices},
    {0x40, 3, 37, 4, g_cpu_shainto_cemetery_nav_chain_3_vertices}};

/* Each stage's chains, by enum StageIndex. */
NavSet g_cpu_stage_obstacles[STAGE_COUNT] = {
    {0},
    {4, g_cpu_meikyokan_dojo_nav_chains, {0}},
    {0},
    {0},
    {2, g_cpu_bus_stop_nav_chains, {0}},
    {0},
    {2, g_cpu_tatara_shrine_path_nav_chains, {0}},
    {3, g_cpu_bamboo_thicket_nav_chains, {0}},
    {4, g_cpu_outside_meikyokan_nav_chains, {0}},
    {3, g_cpu_future_dump_site_nav_chains, {0}},
    {2, g_cpu_snake_cliff_nav_chains, {0}},
    {2, g_cpu_kimigaura_coast_nav_chains, {0}},
    {5, g_cpu_orange_tree_fields_nav_chains, {0}},
    {0},
    {0},
    {2, g_cpu_terraced_fields_nav_chains, {0}},
    {1, g_cpu_under_railway_nav_chains, {0}},
    {3, g_cpu_water_tower_dam_nav_chains, {0}},
    {0},
    {1, g_cpu_lighthouse_nav_chains, {0}},
    {1, g_cpu_kabuki_stage_nav_chains, {0}},
    {0},
    {4, g_cpu_ferry_dock_nav_chains, {0}},
    {4, g_cpu_shainto_gate_nav_chains, {0}},
    {3, g_cpu_purple_cavern_nav_chains, {0}},
    {2, g_cpu_abandoned_station_nav_chains, {0}},
    {2, g_cpu_railway_nav_chains, {0}},
    {0},
    {2, g_cpu_abandoned_shipyard_nav_chains, {0}},
    {1, g_cpu_building_rooftop_nav_chains, {0}},
    {4, g_cpu_building_parking_lot_nav_chains, {0}},
    {4, g_cpu_shainto_cemetery_nav_chains, {0}},
    {0},
    {0},
    {0},
    {0},
    {0},
    {0}};

/* ---- probes, reaches and follow-ups ---- */

/* The eight probes of the ground: how far (in units of 256) and which way
 * (in sixteenths of a turn from straight ahead; CPU_PROBE_FROM_OPPONENT:
 * from the opponent, not from the fighter). */
u8 g_cpu_probe_table[8][2] = {
    {13, 0},                           /* CPU_PROBE_TOWARD */
    {9, 4},                            /* CPU_PROBE_SIDE_B */
    {11, 6},                           /* CPU_PROBE_BACK_B */
    {13, 8},                           /* CPU_PROBE_AWAY */
    {11, 10},                          /* CPU_PROBE_BACK_A */
    {9, 12},                           /* CPU_PROBE_SIDE_A */
    {46, 0},                           /* CPU_PROBE_FAR_TOWARD */
    {13, CPU_PROBE_FROM_OPPONENT | 8}, /* CPU_PROBE_PAST_OPPONENT */
};

/* How far a weapon reaches, for working out whether an attack will land:
 * by weapon (enum WeaponId) for an attack of the fighter's own file (in
 * units of 2) and of the common file (in units of 8, signed), and by
 * sub-weapon for an attack with the off hand. The two say the same of the
 * nodachi, the broadsword and the naginata (248, 196 and 502 against 248,
 * 192 and 520) and not of the long sword (418 against -88) or the yari (36
 * against 936): the original's data. */
u8 g_cpu_weapon_reaches[8] = {0, 209, 124, 98, 251, 18, 0, 0};
s8 g_cpu_weapon_ranges[8] = {0, -11, 31, 24, 65, 117, 0, 0};
u8 g_cpu_sub_weapon_reaches[16] = {
    0, 0, 145, 0, 84, 81, 0, 0, 41, 0, 0, 0, 0, 151, 0, 0};

/* Moves the CPU follows up: its own move kind, the opponent's, a distance
 * (in units of 16, past the weapon's range) and flags (CPU_FOLLOW_...). */
u8 g_cpu_follow_ups[8][4] = {
    {MOVE_KIND_DEFLECT, CPU_FOLLOW_ANY_KIND, 125,
     CPU_FOLLOW_AT_END | CPU_FOLLOW_ATTACKS},
    {MOVE_KIND_DEFLECTED, CPU_FOLLOW_ANY_KIND, 125,
     CPU_FOLLOW_AT_END | CPU_FOLLOW_ATTACKS},
    {MOVE_KIND_GET_UP, CPU_FOLLOW_ANY_KIND, 156,
     CPU_FOLLOW_AT_END | CPU_FOLLOW_ATTACKS | CPU_FOLLOW_FACING},
    {MOVE_KIND_CLIMB, CPU_FOLLOW_ANY_KIND, 187,
     CPU_FOLLOW_AT_END | CPU_FOLLOW_ATTACKS | CPU_FOLLOW_FACING},
    {MOVE_KIND_LJUMP, CPU_FOLLOW_ANY_KIND, 125,
     CPU_FOLLOW_AT_END | CPU_FOLLOW_ATTACKS},
    {MOVE_KIND_MJUMP, CPU_FOLLOW_ANY_KIND, 125,
     CPU_FOLLOW_AT_END | CPU_FOLLOW_ATTACKS},
    {MOVE_KIND_RUN, CPU_FOLLOW_ANY_KIND, 156,
     CPU_FOLLOW_ATTACKS | CPU_FOLLOW_FACING},
    {MOVE_KIND_STANCE, MOVE_KIND_LJUMP, 250, 0},
};

/* ---- the built-in pad scripts ----
 * A script presses a button (by its number on the pad), lets it go, or waits
 * some frames; zeroes fill it out to whole words (cpu_StepScript). */
#define WAIT(frames) (CPU_SCRIPT_WAIT | (frames))
#define RELEASE(button) (CPU_SCRIPT_RELEASE | (button))
#define END CPU_SCRIPT_END

/* The buttons of the fighter's pad, by number. */
enum {
    R2 = 1,
    L1 = 2,
    R1 = 3,
    FRONTAL = 5, /* FIGHTER_PAD_FRONTAL_ATTACK */
    REVERSE = 6, /* FIGHTER_PAD_REVERSE_ATTACK */
    SIDE_A = 12,
    TOWARD = 13,
    SIDE_B = 14,
    AWAY = 15,
};

u8 g_cpu_script_guard_a[4] = {FRONTAL, END, 0, 0};
u8 g_cpu_script_guard_b[4] = {REVERSE, END, 0, 0};
u8 g_cpu_script_step_side_b[8] = {
    SIDE_B, WAIT(1), RELEASE(SIDE_B), WAIT(1), SIDE_B, END, 0, 0};
u8 g_cpu_script_step_side_a[8] = {
    SIDE_A, WAIT(1), RELEASE(SIDE_A), WAIT(1), SIDE_A, END, 0, 0};
u8 g_cpu_script_step_toward[8] = {
    TOWARD, WAIT(1), RELEASE(TOWARD), WAIT(1), TOWARD, END, 0, 0};
u8 g_cpu_script_step_away[8] = {
    AWAY, WAIT(1), RELEASE(AWAY), WAIT(1), AWAY, END, 0, 0};
u8 g_cpu_script_strike_fallen[4] = {R1, END, 0, 0};
u8 g_cpu_script_leap[8] = {TOWARD, WAIT(5), R2, WAIT(4), R1, END, 0, 0};
u8 g_cpu_script_running_leap[8] = {L1, WAIT(1), R2, WAIT(4), R1, END, 0, 0};
u8 g_cpu_script_throw[8] = {R2, WAIT(5), FRONTAL, END, END, 0, 0, 0};
u8 g_cpu_script_throw_twice[12] = {
    R2,  WAIT(5), FRONTAL, WAIT(1), RELEASE(FRONTAL), WAIT(5), FRONTAL, END,
    END, 0,       0,       0};
u8 g_cpu_script_throw_dirt[8] = {R2, WAIT(5), REVERSE, END, END, 0, 0, 0};

/* ---- what the CPU may do in each mode ---- */

/* A story boss's plan for each stage of the story, by its stance class:
 * how often it must meet an attack to know it, how many frames it stands
 * by before it attacks, and a bit for each class of attack it may use. */
CpuStoryLimits g_cpu_story_limits[3][8] = {
    {{0, 50, 0x83},
     {0, 50, 0x87},
     {16, 40, 0x8F},
     {16, 40, 0x43CF},
     {16, 40, 0x43CF},
     {16, 30, 0x41BF},
     {12, 30, 0x427F},
     {0, 0, 0x0}},
    {{0, 50, 0x0},
     {0, 50, 0x2083},
     {12, 40, 0x2083},
     {12, 40, 0x226B},
     {12, 40, 0x226B},
     {12, 30, 0x21AF},
     {8, 30, 0x21AF},
     {0, 0, 0x0}},
    {{0, 50, 0x0},
     {0, 50, 0x1007},
     {12, 40, 0x100F},
     {12, 40, 0x105F},
     {12, 40, 0x105F},
     {12, 30, 0x10AF},
     {8, 30, 0x10AF},
     {0, 0, 0x0}}};

/* The classes of attack chanbara's opponents may use: [stance class][how
 * far up the ladder]. */
u16 g_cpu_chanbara_attacks[3][23] = {
    {0x1,  0x5,   0x2,   0xE,    0x82,  0x15,   0x41,  0x2A,
     0x28, 0x30,  0x204, 0x1201, 0x108, 0x2102, 0x30C, 0x3C,
     0xF,  0x12C, 0x221, 0x2C,   0x1F,  0x1124, 0x303C},
    {0x1,  0x5,   0x2,   0xA,   0x82,  0xB,   0x41,  0x2A,
     0x28, 0x30,  0x204, 0x601, 0x104, 0x902, 0x309, 0x3C,
     0xF,  0x12C, 0x109, 0x1C,  0x1F,  0x60C, 0xC3C},
    {0x1,  0x5,   0x2,  0xE,  0x22, 0x5,   0x21,  0xD, 0xC,  0x2C,  0x44, 0x241,
     0x88, 0x288, 0xCC, 0x8F, 0xF,  0x48C, 0x109, 0xF, 0x4F, 0x28C, 0x60F}};

/* A chanbara opponent's two limits (as CpuStoryLimits has them: how often
 * it must meet an attack to know it, how many frames it stands by), by how
 * far up the ladder. */
u8 g_cpu_chanbara_limits[20][2] = {
    {0, 30},  {0, 30},  {0, 30},  {0, 30},  {0, 30},  {0, 30},  {24, 30},
    {16, 20}, {24, 30}, {16, 20}, {20, 30}, {12, 20}, {20, 30}, {12, 20},
    {16, 30}, {8, 20},  {16, 30}, {8, 20},  {12, 20}, {4, 15}};

/* Practice's patterns of attack classes: the row of g_cpu_practice_attacks
 * for [weapon][stance class], */
u8 g_cpu_practice_rows[6][3] = {
    {0, 0, 2}, /* WEAPON_ID_KATANA */
    {0, 0, 2}, /* WEAPON_ID_LONG_SWORD */
    {2, 2, 2}, /* WEAPON_ID_NODACHI */
    {2, 2, 2}, /* WEAPON_ID_BROADSWORD */
    {0, 2, 0}, /* WEAPON_ID_NAGINATA */
    {1, 0, 1}, /* WEAPON_ID_YARI */
};

/* and the rows: [row][option]. */
s32 g_cpu_practice_attacks[3][8] = {
    {0x0, 0x312, 0x422, 0xF0043214, 0x652, 0x5313, 0x6423, 0xF6543216},
    {0x0, 0x11, 0x21, 0xF0000212, 0x652, 0x512, 0x622, 0xF0065214},
    {0x0, 0x312, 0x422, 0xF0043214, 0x432, 0x312, 0x422, 0xF0043214}};

/* The directions of the pad, in eighths of a turn. */
s32 g_cpu_heading_buttons[8] = {
    FIGHTER_PAD_SIDE_A | FIGHTER_PAD_TOWARD, FIGHTER_PAD_SIDE_A,
    FIGHTER_PAD_SIDE_A | FIGHTER_PAD_AWAY,   FIGHTER_PAD_AWAY,
    FIGHTER_PAD_SIDE_B | FIGHTER_PAD_AWAY,   FIGHTER_PAD_SIDE_B,
    FIGHTER_PAD_SIDE_B | FIGHTER_PAD_TOWARD, FIGHTER_PAD_TOWARD,
};
