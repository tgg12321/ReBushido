/* An object launched when twelve are loose (object.c, RB_FIX_OBJECT_POOL). */
#include "src/main/object.c"
#include "src/main/math_sin.c"
#include "src/rebushido/settings.c"
#include "runtime/runtime.h"

#define TEST_KIND 3
#define TEST_OLD_KIND 7

/* The kinds' table, stood in for: nothing is asked of a kind here but
 * that it is there. */
LooseObjectKind g_object_kinds[30];

static Fighter g_test_fighter;

/* Every place free. */
static void test_EmptyPool(void) {
    s32 i;

    for (i = 0; i < OBJECT_COUNT; i++) {
        g_object_pool[i].kind = OBJECT_NONE;
        g_object_pool[i].slot = OBJECT_NO_SLOT;
    }
}

void test_Main(void) {
    LooseObject *obj;
    s32 bugfixes;
    s32 i;

    CHECK_EQ(RB_FIX_OBJECT_POOL, 1);
    g_test_fighter.index = 1;
    g_test_fighter.world_pos.x = 1000;
    g_test_fighter.world_pos.y = -50;
    g_test_fighter.world_pos.z = 2000;

    /* With a place free the fix changes nothing: the first free place is
     * taken, whatever the setting. */
    for (bugfixes = 0; bugfixes < 2; bugfixes++) {
        g_rb_settings.bugfixes = bugfixes;
        test_EmptyPool();
        for (i = 0; i < OBJECT_COUNT; i++) {
            obj = object_Launch(&g_test_fighter, TEST_KIND);
            CHECK(obj == &g_object_pool[i]);
            CHECK_EQ(obj->slot, i);
            CHECK_EQ(obj->kind, TEST_KIND);
            CHECK_EQ(obj->owner, 1);
            CHECK_EQ(obj->pos.x, 1000);
            CHECK_EQ(obj->pos.z, 2000);
        }
    }

    /* All twelve loose, with the fix: the new object takes the last
     * place, and the eleven before it are not touched. (The original
     * writes a thirteenth after the pool, which a test cannot ask for.) */
    g_rb_settings.bugfixes = TRUE;
    for (i = 0; i < OBJECT_COUNT; i++) {
        g_object_pool[i].kind = TEST_OLD_KIND;
    }
    obj = object_Launch(&g_test_fighter, TEST_KIND);
    CHECK(obj == &g_object_pool[OBJECT_COUNT - 1]);
    CHECK_EQ(obj->slot, OBJECT_COUNT - 1);
    CHECK_EQ(obj->kind, TEST_KIND);
    for (i = 0; i < OBJECT_COUNT - 1; i++) {
        CHECK_EQ(g_object_pool[i].kind, TEST_OLD_KIND);
        CHECK_EQ(g_object_pool[i].slot, i);
    }
}
