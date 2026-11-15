/* The cloths of the disc's models, made by cloth_model.c and cloth.c. */
#include "src/main/cloth_model.c"
#include "src/main/cloth.c"
#include "src/main/math.c"
#include "src/main/math_sin.c"
#include "src/main/stage_data.c"
#include "runtime/runtime.h"
#include "stubs/sys.c"
#include "stubs/libgte.c"

/* ---- stand-ins ---- */

/* render.c's: the table of each loaded file's meshes. The test fills it in
 * as reloc_RelocateBlock does. */
s32 *g_reloc_tables[RELOC_BLOCK_COUNT];

/* The arena and the model's data are not there: the pieces are made in
 * memory of the test's. */
void arena_UpdatePeak(s32 end_addr) {}

void resource_ResizeModelData(s32 slot, s32 new_size) {}

s32 tex_IsGrayscale(void) { return 0; }

/* ---- the test ---- */

/* A model file's header: a count of sections, then where each starts. The
 * second word is the meshes' table, the fourth the cloth section. */
#define FILE_MESHES 1
#define FILE_CLOTH 3

/* Room for a model's pieces: their vertices, points and springs. */
#define CLOTH_ROOM 0x80000
/* The scratchpad, where cloth_CreatePiece lists a mesh's faces: a kilobyte
 * on the PlayStation. The test gives it more, so that a list that is too
 * long is measured (cloth.py) and does not stop the test. */
#define SCRATCHPAD_ROOM 0x10000

/* How the bone the cloth hangs from is turned when the cloth is made: not
 * at all, and seven other ways. Which way a spring runs depends on its
 * ends' distances from the held points, which are worked out after the
 * turn and rounded. */
#define POSE_COUNT 8
static const SVECTOR g_test_poses[POSE_COUNT] = {
    {0, 0, 0},        {1024, 0, 0},      {0, 1024, 0},     {0, 0, 1024},
    {300, 700, 1100}, {2000, 150, 3500}, {-900, 2500, 40}, {1700, -1300, 600},
};

static Model g_test_model;
static ModelNode g_test_nodes[MODEL_MESH_COUNT];

/* Makes the cloths of the model file at `file`, as the game does when it
 * has loaded the model, with every mesh's bone turned by `pose`. Returns
 * the model's cloth, or NULL if it has none. */
static ModelCloth *test_SetUp(u8 *file, ModelCloth *room, s32 pose) {
    s32 *header = (s32 *)file;
    MATRIX world;
    s32 mesh;

    math_RotMatrixYXZ((SVECTOR *)&g_test_poses[pose], &world);
    world.t[0] = 100 * pose;
    world.t[1] = -1500;
    world.t[2] = 250 * pose;
    for (mesh = 0; mesh < MODEL_MESH_COUNT; mesh++) {
        g_test_nodes[mesh].node.mesh_block = 0;
        g_test_nodes[mesh].node.mesh = mesh;
        g_test_nodes[mesh].node.world = world;
        SET_PTR(g_test_model.mesh_nodes[mesh], &g_test_nodes[mesh]);
    }
    SET_PTR(g_test_model.cloth, room);
    g_test_model.cloth_params = (s32)(file + header[FILE_CLOTH]);
    cloth_FreeAll();
    cloth_SetupModel(&g_test_model, 0);
    return PTR(g_test_model.cloth);
}

/* The arguments are the disc's model files (cloth.py takes them out of the
 * archive). For each, and for each pose: the cloths are made and every
 * point's pin and counts of springs are printed; in the first pose the
 * springs too. cloth.py reads the same files its own way and compares.
 * Here: the game does not stop, and a cloth has no more springs than its
 * buffer was sized for or than a point's one-byte spring numbers can
 * name. */
void test_Main(void) {
    ModelCloth *room = test_Alloc(CLOTH_ROOM);
    s32 file_index;

    test_AllocAt(SCRATCHPAD, SCRATCHPAD_ROOM);
    for (file_index = 0; test_Arg(file_index); file_index++) {
        u8 *file = test_ReadFile(test_Arg(file_index), NULL);
        s32 *table;
        s32 pose, i, j;

        CHECK(file != NULL);
        if (!file) {
            continue;
        }
        /* the meshes' table: a count, then each mesh's offset from the
         * table, which become addresses */
        table = (s32 *)(file + ((s32 *)file)[FILE_MESHES]);
        for (i = 0; i < table[0]; i++) {
            table[1 + i] += (s32)table;
        }
        g_reloc_tables[0] = table + 1;

        for (pose = 0; pose < POSE_COUNT; pose++) {
            ModelCloth *cloth = test_SetUp(file, room, pose);

            if (!cloth) {
                test_Print("model %d none\n", file_index);
                break;
            }
            for (i = 0; i < cloth->count; i++) {
                ClothPiece *piece = &cloth->pieces[i];
                Cloth *made = &g_cloths[piece->cloth];

                CHECK(piece->cloth >= 0);
                CHECK(made->spring_count <= 0x100);
                test_Print("cloth %d %d %d %d %d\n", file_index,
                           (s32)(PTR(piece->node) - g_test_nodes), pose,
                           made->count, made->spring_count);
                for (j = 0; j < made->count; j++) {
                    test_Print("point %d %d %d %d\n", j, made->points[j].pin,
                               made->points[j].out_count,
                               made->points[j].in_count);
                }
                for (j = 0; pose == 0 && j < made->spring_count; j++) {
                    test_Print("spring %d %d\n",
                               made->springs[j].ends.point.from,
                               made->springs[j].ends.point.to);
                }
            }
        }
    }
    CHECK_EQ(g_test_traps, 0);
}
