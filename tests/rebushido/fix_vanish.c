/* Where the vanisher comes back (fighter_hit.c, RB_FIX_VANISH_PLACE). */
#include "src/main/fighter_hit.c"
#include "src/rebushido/settings.c"
#include "runtime/runtime.h"

/* A table of start positions in which every point says where it is: its
 * x is stage * 100 + pair * 10 + point, its z the same and 5000 more. */
static s16
    g_test_starts[STAGE_COUNT][FIGHT_START_PAIRS][2][FIGHT_START_POINT_SIZE];
#define TEST_X(stage, pair, point) ((stage) * 100 + (pair) * 10 + (point))
#define TEST_Z(stage, pair, point) (TEST_X(stage, pair, point) + 5000)

/* What vanishing touches of the fight, stood in for. */
s16 g_fight_stage;
static u16 g_test_event_table[64];
static MoveScript g_test_move;
static s32 g_test_moves_started;
static s32 g_test_events;

void *fight_GetStartPositions(void) { return g_test_starts; }

void *motion_GetMove(Fighter *fighter, s32 move_id, s16 *is_common_out) {
    return g_test_event_table;
}

void fighter_StartMove(s32 fighter_index, MoveScript *move, s32 is_common) {
    g_test_moves_started++;
}

void fight_RaiseEvent(s32 fighter_index, s32 event, s32 *pos, s16 *dir) {
    if (event == FIGHT_EVENT_VANISH) {
        g_test_events++;
    }
}

/* The second fighter, struck on `stage`, with the fix or without:
 * where it is afterwards. */
static Fighter *test_Vanish(s32 stage, s32 bugfixes) {
    static Fighter fighters[2];
    Fighter *fighter = &fighters[1];

    fighter->index = 1;
    fighter->other = &fighters[0];
    fighter->cur_move = &g_test_move;
    fighter->vanish_count = 0;
    fighter->world_pos.x = 7;
    fighter->world_pos.z = 9;
    fighter->base_pos.x = 7;
    fighter->base_pos.z = 9;
    fighter->foot_pos.vx = 7;
    fighter->foot_pos.vz = 9;
    g_fight_stage = stage;
    g_rb_settings.bugfixes = bugfixes;
    fighter_Vanish(fighter);
    CHECK_EQ(fighter->vanish_count, 1);
    /* its three places have moved together */
    CHECK_EQ(fighter->base_pos.x, fighter->world_pos.x);
    CHECK_EQ(fighter->base_pos.z, fighter->world_pos.z);
    CHECK_EQ(fighter->foot_pos.vx, fighter->world_pos.x);
    CHECK_EQ(fighter->foot_pos.vz, fighter->world_pos.z);
    return fighter;
}

void test_Main(void) {
    Fighter *fighter;
    s32 stage;
    s32 pair;
    s32 point;

    CHECK_EQ(RB_FIX_VANISH_PLACE, 1);
    CHECK(STAGE_TATARA_SHRINE != 0);
    for (stage = 0; stage < STAGE_COUNT; stage++) {
        for (pair = 0; pair < FIGHT_START_PAIRS; pair++) {
            for (point = 0; point < 2; point++) {
                g_test_starts[stage][pair][point][0] =
                    TEST_X(stage, pair, point);
                g_test_starts[stage][pair][point][2] =
                    TEST_Z(stage, pair, point);
            }
        }
    }

    /* On the table's first stage both read the same place: the first
     * pair's second point. */
    fighter = test_Vanish(0, FALSE);
    CHECK_EQ(fighter->world_pos.x, TEST_X(0, 0, 1));
    CHECK_EQ(fighter->world_pos.z, TEST_Z(0, 0, 1));
    fighter = test_Vanish(0, TRUE);
    CHECK_EQ(fighter->world_pos.x, TEST_X(0, 0, 1));
    CHECK_EQ(fighter->world_pos.z, TEST_Z(0, 0, 1));

    /* On the lighthouse, the nineteenth, the original steps 12 halfwords
     * a stage and reads the ninth stage's third pair. */
    CHECK_EQ(STAGE_LIGHTHOUSE, 19);
    fighter = test_Vanish(STAGE_LIGHTHOUSE, FALSE);
    CHECK_EQ(fighter->world_pos.x, TEST_X(9, 2, 1));
    CHECK_EQ(fighter->world_pos.z, TEST_Z(9, 2, 1));
    /* Fixed, it is the lighthouse's own first pair. */
    fighter = test_Vanish(STAGE_LIGHTHOUSE, TRUE);
    CHECK_EQ(fighter->world_pos.x, TEST_X(STAGE_LIGHTHOUSE, 0, 1));
    CHECK_EQ(fighter->world_pos.z, TEST_Z(STAGE_LIGHTHOUSE, 0, 1));

    /* and so on every stage but the shrine's, which has its own way */
    for (stage = 0; stage < STAGE_COUNT; stage++) {
        if (stage == STAGE_TATARA_SHRINE) {
            continue;
        }
        fighter = test_Vanish(stage, TRUE);
        CHECK_EQ(fighter->world_pos.x, TEST_X(stage, 0, 1));
        CHECK_EQ(fighter->world_pos.z, TEST_Z(stage, 0, 1));
    }

    /* each time the move that brings it back was started and the fight
     * was told */
    CHECK_EQ(g_test_moves_started, 4 + STAGE_COUNT - 1);
    CHECK_EQ(g_test_events, g_test_moves_started);
}
