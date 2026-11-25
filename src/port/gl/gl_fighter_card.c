/* The HD fighters' work done on the card (gl_fighter_card.h; docs/PORT.md,
 * "The HD fighters on the card").
 *
 * A pose is five compute passes over the model's own vertex buffer: each
 * vertex placed as the card draws it (the sum of its terms on the bones)
 * and its normal turned by the joints, counted into a hashed grid of
 * 64-unit cubes; the counts summed into where each cube's list begins;
 * each vertex put in its cube's list; each list put in the order of the
 * vertices' numbers, so that a sum over a cube is the same every run
 * whatever order the card ran the vertices in; and the posed vertices
 * copied in the lists' order.
 *
 * Every fighter's poses and cloths of a picture are made together, each
 * pass of all of them before the barrier after it
 * (port_GlFighterCardPoses, port_GlFighterCardHolds): the passes are
 * chains of waits on memory more than work, and side by side they wait
 * at once.
 *
 * A cloth is three: its points kept out of the body, then each place of
 * the surface (the fixed sum of the points, gl_cloth.h) kept out, then
 * each vertex given its place, the normal of the quads about it and its
 * texture point, into a buffer the fighters' layer and the shadows draw
 * from. A point's distance from the body is the sum of the body's
 * vertices about it, each one's distance along its own normal weighed by
 * (1 - d^2/r^2)^2, r one and a half times the nearest one's distance and
 * at least 48 (the processor's arithmetic of before, in single
 * precision). */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "gl_bench.h"
#include "gl_cloth.h"
#include "gl_fighter_card.h"
#include "gl_fighter_file.h"
#include "gl_joints.h"
#include "port_fighter.h"

typedef unsigned int GLenum;
typedef unsigned int GLbitfield;
typedef int GLint;
typedef unsigned int GLuint;
typedef int GLsizei;
typedef float GLfloat;
typedef char GLchar;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;

#define GL_FALSE 0
#define GL_TRUE 1
#define GL_INT 0x1404
#define GL_UNSIGNED_INT 0x1405
#define GL_R32I 0x8235
#define GL_R32UI 0x8236
#define GL_RED_INTEGER 0x8D94
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#define GL_COMPUTE_SHADER 0x91B9
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_DYNAMIC_COPY 0x88EA
#define GL_STATIC_DRAW 0x88E4

#define PORT_GL_CARD_FUNCTIONS                                                 \
    GL(GLuint, glCreateShader, (GLenum))                                       \
    GL(void, glShaderSource,                                                   \
       (GLuint, GLsizei, const GLchar * const *, const GLint *))               \
    GL(void, glCompileShader, (GLuint))                                        \
    GL(void, glGetShaderiv, (GLuint, GLenum, GLint *))                         \
    GL(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))       \
    GL(GLuint, glCreateProgram, (void))                                        \
    GL(void, glAttachShader, (GLuint, GLuint))                                 \
    GL(void, glLinkProgram, (GLuint))                                          \
    GL(void, glGetProgramiv, (GLuint, GLenum, GLint *))                        \
    GL(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei *, GLchar *))      \
    GL(void, glUseProgram, (GLuint))                                           \
    GL(GLint, glGetUniformLocation, (GLuint, const GLchar *))                  \
    GL(void, glUniform1i, (GLint, GLint))                                      \
    GL(void, glUniform4fv, (GLint, GLsizei, const GLfloat *))                  \
    GL(void, glUniform1f, (GLint, GLfloat))                                    \
    GL(void, glUniform3fv, (GLint, GLsizei, const GLfloat *))                  \
    GL(void, glUniformMatrix3fv, (GLint, GLsizei, GLint, const GLfloat *))     \
    GL(void, glUniformMatrix4fv, (GLint, GLsizei, GLint, const GLfloat *))     \
    GL(void, glGenBuffers, (GLsizei, GLuint *))                                \
    GL(void, glDeleteBuffers, (GLsizei, const GLuint *))                       \
    GL(void, glBindBuffer, (GLenum, GLuint))                                   \
    GL(void, glBindBufferBase, (GLenum, GLuint, GLuint))                       \
    GL(void, glBufferData, (GLenum, GLsizeiptr, const void *, GLenum))         \
    GL(void, glGetBufferSubData, (GLenum, GLintptr, GLsizeiptr, void *))       \
    GL(void, glClearBufferSubData,                                             \
       (GLenum, GLenum, GLintptr, GLsizeiptr, GLenum, GLenum, const void *))   \
    GL(void, glDispatchCompute, (GLuint, GLuint, GLuint))                      \
    GL(void, glMemoryBarrier, (GLbitfield))

#define GL(result, name, args) static result(*name) args;
PORT_GL_CARD_FUNCTIONS
#undef GL

/* The grid's lists: as many as the processor's grid had (BODY_BUCKETS). */
#define BUCKETS 4096
/* A pose's grid buffer: the count of each list, where each begins (and
 * the end of the last), and the lowest point. */
#define GRID_FIRST BUCKETS
#define GRID_WORDS (PORT_GL_CARD_FEET_WORD + 1)
/* The poses kept for each slot: the frame on show, the frame after it,
 * the frames run ahead (POSES_KEPT); and for each copy the benchmark's
 * stress draws of him as many again (port_GlFighterCardKeep). */
#define POSES_KEPT 4
#define POSES_MAX 8
#define POSE_SLOTS (PORT_FIGHTER_SLOTS * POSES_MAX)
/* A vertex of the model's file, in words (PortFighterVertex). */
#define VERTEX_WORDS 47
/* The threads of a group. */
#define GROUP 64
#define SCAN_GROUP 1024

typedef char PortGlCardVertexWords[sizeof(PortFighterVertex) == VERTEX_WORDS * 4
                                       ? 1
                                       : -1];

typedef struct {
    u64 key; /* 0: nothing */
    s32 count;
    s32 room;
    GLuint posed; /* a place and a normal a vertex, vec4 each */
    GLuint grid;
    GLuint rank; /* each vertex's list and its place in it */
    GLuint order;
    GLuint sorted; /* `posed` in the lists' order: a list is a run of it */
    u32 asked;     /* the last batch of asks that wanted it */
} CardPose;

struct PortGlCardCloth {
    PortGlClothSums sums;
    GLuint data; /* the sums, quads and vertices, as words */
    GLuint points;
    GLuint places;
    s32 quads_at;
    s32 first_at;
    s32 list_at;
    s32 place_at;
    s32 uv_at;
};

static struct {
    s32 is_started;
    char problem[512];
    GLuint skin;
    GLuint scan;
    GLuint scatter;
    GLuint sort;
    GLuint gather;
    GLuint hold_points;
    GLuint hold_places;
    GLuint vertices;
    GLint skin_at_world;
    GLint skin_at_turn;
    GLint skin_at_dual;
    GLint skin_at_joints;
    GLint skin_at_count;
    GLint scatter_at_count;
    GLint gather_at_count;
    GLint points_at_points;
    GLint points_at_count;
    GLint places_at_count;
    GLint points_at_gap;
    GLint points_at_held;
    GLint places_at_points;
    GLint places_at_gap;
    GLint places_at_held;
    GLint places_at_counting;
    GLint vertices_at_count;
    GLint vertices_at_quads;
    GLint vertices_at_first;
    GLint vertices_at_list;
    GLint vertices_at_place;
    GLint vertices_at_uv;
    GLuint counts; /* the places inside, and left inside */
    CardPose pose[POSE_SLOTS];
    s32 kept; /* poses kept a slot, POSES_KEPT unless stressed */
    s32 next[PORT_FIGHTER_SLOTS];
    u32 asks; /* batches of asks for poses */
} g_card;

/* ---- the shaders ---- */

#define CARD_VERSION "#version 430 core\n"

/* A vertex placed as the fighters' shader places it, its normal turned
 * as that shader turns it, and counted into its cube's list. */
static const char g_skin_text[] = CARD_VERSION
    "layout(local_size_x = 64) in;\n"
    "layout(std430, binding = 0) readonly buffer Model { uint m[]; };\n"
    "layout(std430, binding = 1) writeonly buffer Posed { vec4 posed[]; };\n"
    "layout(std430, binding = 2) buffer Grid { int grid[]; };\n"
    "layout(std430, binding = 3) writeonly buffer Rank { ivec2 rank[]; };\n"
    "uniform mat4 u_world[21];\n"
    "uniform mat3 u_turn[21];\n"
    "uniform int u_count;\n"
    /* the joints blended (gl_joints.h): each vertex's band, and the pose's
     * dual quaternions */
    "layout(std430, binding = 4) readonly buffer Band { vec4 band[]; };\n"
    "uniform vec4 u_dual[42];\n"
    "uniform int u_joints;\n" PORT_GL_JOINTS_GLSL
    "float f(uint i) { return uintBitsToFloat(m[i]); }\n"
    "uint byte_of(uint word, uint k) { return (word >> (8u * k)) & 255u; }\n"
    "int cell(float x) { return int(floor(x / 64.0)); }\n"
    "void main() {\n"
    "    uint i = gl_GlobalInvocationID.x;\n"
    "    if (i >= uint(u_count)) { return; }\n"
    "    uint b = i * 47u;\n"
    "    vec3 place = vec3(0.0);\n"
    "    for (uint k = 0u; k < 8u; k++) {\n"
    "        vec4 term = vec4(f(b + 12u + 4u * k), f(b + 13u + 4u * k),\n"
    "                         f(b + 14u + 4u * k), f(b + 15u + 4u * k));\n"
    "        if (term == vec4(0.0)) { continue; }\n"
    "        uint j = byte_of(m[b + 45u + k / 4u], k & 3u);\n"
    "        place += (u_world[j] * term).xyz;\n"
    "    }\n"
    "    vec3 n = vec3(f(b + 3u), f(b + 4u), f(b + 5u));\n"
    "    vec3 normal = vec3(0.0);\n"
    "    for (uint k = 0u; k < 4u; k++) {\n"
    "        float w = f(b + 8u + k);\n"
    "        if (w == 0.0) { continue; }\n"
    "        normal += w * (u_turn[byte_of(m[b + 44u], k) % 21u] * n);\n"
    "    }\n"
    "    if (u_joints != 0 && band[i].w == 0.0) {\n"
    "        int ja = int(band[i].x) * 2;\n"
    "        int jb = int(band[i].y) * 2;\n"
    "        normal = n;\n"
    "        place = joint_place(u_dual[ja], u_dual[ja + 1], u_dual[jb],\n"
    "                            u_dual[jb + 1], band[i].z,\n"
    "                            vec3(f(b), f(b + 1u), f(b + 2u)), normal);\n"
    "    }\n"
    "    posed[i * 2u] = vec4(place, 0.0);\n"
    "    posed[i * 2u + 1u] = vec4(normal, 0.0);\n"
    "    ivec3 c = ivec3(cell(place.x), cell(place.y), cell(place.z));\n"
    "    int bucket = int(((uint(c.x) * 73856093u) ^ (uint(c.y) * 19349663u)\n"
    "                      ^ (uint(c.z) * 83492791u)) & 4095u);\n"
    "    rank[i] = ivec2(bucket, atomicAdd(grid[bucket], 1));\n"
    /* the lowest point, y running down: the greatest y, as an integer
     * that orders as the number does */
    "    int bits = floatBitsToInt(place.y);\n"
    "    atomicMax(grid[8193], bits >= 0 ? bits : bits ^ 0x7FFFFFFF);\n"
    "}\n";

/* Where each list begins: the counts summed, four lists a thread. */
static const char g_scan_text[] = CARD_VERSION
    "layout(local_size_x = 1024) in;\n"
    "layout(std430, binding = 2) buffer Grid { int grid[]; };\n"
    "shared int s_sum[1024];\n"
    "void main() {\n"
    "    int t = int(gl_LocalInvocationID.x);\n"
    "    int a0 = grid[4 * t];\n"
    "    int a1 = grid[4 * t + 1];\n"
    "    int a2 = grid[4 * t + 2];\n"
    "    int a3 = grid[4 * t + 3];\n"
    "    int own = a0 + a1 + a2 + a3;\n"
    "    s_sum[t] = own;\n"
    "    barrier();\n"
    "    for (int step = 1; step < 1024; step *= 2) {\n"
    "        int v = t >= step ? s_sum[t - step] : 0;\n"
    "        barrier();\n"
    "        s_sum[t] += v;\n"
    "        barrier();\n"
    "    }\n"
    "    int before = s_sum[t] - own;\n"
    "    grid[4096 + 4 * t] = before;\n"
    "    grid[4096 + 4 * t + 1] = before + a0;\n"
    "    grid[4096 + 4 * t + 2] = before + a0 + a1;\n"
    "    grid[4096 + 4 * t + 3] = before + a0 + a1 + a2;\n"
    "    if (t == 1023) { grid[8192] = s_sum[t]; }\n"
    "}\n";

static const char g_scatter_text[] = CARD_VERSION
    "layout(local_size_x = 64) in;\n"
    "layout(std430, binding = 2) readonly buffer Grid { int grid[]; };\n"
    "layout(std430, binding = 3) readonly buffer Rank { ivec2 rank[]; };\n"
    "layout(std430, binding = 4) writeonly buffer Order { int order[]; };\n"
    "uniform int u_count;\n"
    "void main() {\n"
    "    int i = int(gl_GlobalInvocationID.x);\n"
    "    if (i >= u_count) { return; }\n"
    "    order[grid[4096 + rank[i].x] + rank[i].y] = i;\n"
    "}\n";

/* Each list in the order of the vertices' numbers, as the processor's
 * grid had them, a group a list: the list read into shared memory, and
 * each vertex put where as many of the list's are below it (no two the
 * same). A list of more than SORTED (none was seen) is sorted by one
 * thread. A thread a list sorting in place took a millisecond and more
 * where a list was long, the fingers' and the face's. */
static const char g_sort_text[] = CARD_VERSION
    "layout(local_size_x = 64) in;\n"
    "layout(std430, binding = 2) readonly buffer Grid { int grid[]; };\n"
    "layout(std430, binding = 4) buffer Order { int order[]; };\n"
    "const int SORTED = 2048;\n"
    "shared int s_vertex[SORTED];\n"
    "void main() {\n"
    "    int b = int(gl_WorkGroupID.x);\n"
    "    int t = int(gl_LocalInvocationIndex);\n"
    "    int from = grid[4096 + b];\n"
    "    int count = grid[4096 + b + 1] - from;\n"
    "    if (count <= 1) { return; }\n"
    "    if (count > SORTED) {\n"
    "        for (int i = from + 1; t == 0 && i < from + count; i++) {\n"
    "            int v = order[i];\n"
    "            int k = i - 1;\n"
    "            while (k >= from && order[k] > v) {\n"
    "                order[k + 1] = order[k];\n"
    "                k--;\n"
    "            }\n"
    "            order[k + 1] = v;\n"
    "        }\n"
    "        return;\n"
    "    }\n"
    "    for (int i = t; i < count; i += 64) { s_vertex[i] = order[from + i]; }\n"
    "    barrier();\n"
    "    for (int i = t; i < count; i += 64) {\n"
    "        int v = s_vertex[i];\n"
    "        int below = 0;\n"
    "        for (int k = 0; k < count; k++) {\n"
    "            below += s_vertex[k] < v ? 1 : 0;\n"
    "        }\n"
    "        order[from + below] = v;\n"
    "    }\n"
    "}\n";

/* The posed vertices copied in the lists' order: a cube's list is then a
 * run of the buffer, read without going through `order`. */
static const char g_gather_text[] = CARD_VERSION
    "layout(local_size_x = 64) in;\n"
    "layout(std430, binding = 1) readonly buffer Posed { vec4 posed[]; };\n"
    "layout(std430, binding = 4) readonly buffer Order { int order[]; };\n"
    "layout(std430, binding = 5) writeonly buffer Sorted { vec4 sorted[]; };\n"
    "uniform int u_count;\n"
    "void main() {\n"
    "    int i = int(gl_GlobalInvocationID.x);\n"
    "    if (i >= u_count) { return; }\n"
    "    int v = order[i];\n"
    "    sorted[i * 2] = posed[v * 2];\n"
    "    sorted[i * 2 + 1] = posed[v * 2 + 1];\n"
    "}\n";

/* The body a point is kept out of (gl_cloth.h, "the cloth is kept out of
 * the model's body"): the processor's BODY_LOOK 2, BODY_FAR 100,
 * BODY_REACH 48 and 1.5 over the nearest, BODY_STEPS 4.
 *
 * **A group of 32 threads an item** (a point or a place). An item's sums
 * walk the cubes about it, a few hundred vertices, some ten times over
 * (for each of up to five steps out, the nearest and the sums): done by
 * one thread an item, as the processor did it, each thread waited on
 * memory for every vertex in turn, and a frame's cloths took 34 ms of an
 * RTX 3090. Here the group walks them together. The cubes of a round (32
 * of them, one a thread) are found at once, each thread its cube's list,
 * and their vertices are shared out among the threads as one run, every
 * 32nd to each (a thread finds which cube a vertex of the run is in by
 * halving the 32 places where the cubes' shares begin): so a walk waits
 * on memory once a round and not once a cube, which is what took the
 * time, a walk being a chain of waits and a step's dispatch as long as
 * its longest walk. The 32 threads' sums are added by halves exchanged
 * five times over through shared memory (a butterfly): every thread ends
 * with the same sums, so every decision is the group's and they take one
 * path and meet the same barriers; the same sums every run, in another
 * order than the processor's. (Subgroups' shuffles in place of the
 * shared memory, and four such warps a group, were no faster on an RTX
 * 3090.)
 *
 * **The body in the lists' order** (`sorted`, g_gather_text): a cube's
 * vertices are a run of the buffer, read without going through `order`.
 *
 * **What a walk can leave out.** A cube wholly farther than the reach
 * from the item adds nothing to the sums, and is not walked. The first
 * walk asks for the nearest vertex and the sums at the least reach (48)
 * at once, leaving out the cubes beyond 48: where the nearest it finds is
 * within 48 it is the nearest of all (any nearer is in a cube walked);
 * within 32 the reach is 48, and those are the sums, as for most of a
 * cloth, which lies close over the body. Otherwise the sums are walked
 * again at the reach the nearest gives, and where no vertex was within 48
 * the nearest is first looked for in every cube about the item. A bucket
 * of the hashed grid that two cubes in reach share is walked twice, as
 * the processor did; where one of the two lies beyond the reach, once.
 *
 * **Measured** (RTX 3090, `run_ahead 1`, the card's medians of a fight
 * frame with two fighters' cloths new): 0.97 ms with the cubes about an
 * item copied into shared memory and walked one after another, 0.48 ms
 * with the culled walks, 0.46 with the cubes of a round walked as one,
 * and 0.22 with every fighter's cloths kept out side by side
 * (port_GlFighterCardHolds). */
#define CARD_BODY_TEXT                                                                \
    "layout(local_size_x = 32) in;\n"                                                 \
    "layout(std430, binding = 1) readonly buffer Sorted { vec4 sorted[]; };\n"        \
    "layout(std430, binding = 2) readonly buffer Grid { int grid[]; };\n"             \
    "layout(std430, binding = 8) buffer Counts { uint counts[]; };\n"                 \
    "#define g_item int(gl_WorkGroupID.x)\n"                                          \
    "#define g_lane int(gl_LocalInvocationIndex)\n"                                   \
    "shared float s_swap[32 * 6];\n"                                                  \
    "shared int s_from[32];\n"                                                        \
    "shared int s_start[32];\n"                                                       \
    "float least(float a, float b) {\n"                                               \
    "    return a < 0.0 ? b : (b < 0.0 ? a : min(a, b));\n"                           \
    "}\n"                                                                             \
    "int cell(float x) { return int(floor(x / 64.0)); }\n"                            \
    "struct Sums {\n"                                                                 \
    "    float nearest;\n"                                                            \
    "    float along;\n"                                                              \
    "    vec3 normal;\n"                                                              \
    "    float weight;\n"                                                             \
    "};\n"                                                                            \
    "Sums all_of(Sums s) {\n"                                                         \
    "    int t = g_lane;\n"                                                           \
    "    for (int m = 16; m >= 1; m /= 2) {\n"                                        \
    "        int u = t ^ m;\n"                                                        \
    "        s_swap[t] = s.nearest;\n"                                                \
    "        s_swap[32 + t] = s.along;\n"                                             \
    "        s_swap[64 + t] = s.normal.x;\n"                                          \
    "        s_swap[96 + t] = s.normal.y;\n"                                          \
    "        s_swap[128 + t] = s.normal.z;\n"                                         \
    "        s_swap[160 + t] = s.weight;\n"                                           \
    "        barrier();\n"                                                            \
    "        s.nearest = least(s.nearest, s_swap[u]);\n"                              \
    "        s.along += s_swap[32 + u];\n"                                            \
    "        s.normal += vec3(s_swap[64 + u], s_swap[96 + u], s_swap[128 + u]);\n"    \
    "        s.weight += s_swap[160 + u];\n"                                          \
    "        barrier();\n"                                                            \
    "    }\n"                                                                         \
    "    return s;\n"                                                                 \
    "}\n"                                                                             \
    "int bucket(int x, int y, int z) {\n"                                             \
    "    return int(((uint(x) * 73856093u) ^ (uint(y) * 19349663u)\n"                 \
    "                ^ (uint(z) * 83492791u)) & 4095u);\n"                            \
    "}\n"                                                                             \
    "Sums near_of(vec3 p, int look, float reach, bool is_culled) {\n"                 \
    "    int t = g_lane;\n"                                                           \
    "    float nearest = -1.0;\n"                                                     \
    "    float along = 0.0;\n"                                                        \
    "    vec3 normal = vec3(0.0);\n"                                                  \
    "    float weight = 0.0;\n"                                                       \
    "    ivec3 c = ivec3(cell(p.x), cell(p.y), cell(p.z));\n"                         \
    "    float reach2 = reach * reach;\n"                                             \
    "    int side = 2 * look + 1;\n"                                                  \
    "    int cubes = side * side * side;\n"                                           \
    "    for (int first = 0; first < cubes; first += 32) {\n"                         \
    "        int k = first + t;\n"                                                    \
    "        int from = 0;\n"                                                         \
    "        int count = 0;\n"                                                        \
    "        if (k < cubes) {\n"                                                      \
    "            ivec3 at = c - ivec3(look) +\n"                                      \
    "                       ivec3(k / (side * side), (k / side) % side, k % side);\n" \
    "            int b = bucket(at.x, at.y, at.z);\n"                                 \
    "            from = grid[4096 + b];\n"                                            \
    "            count = grid[4096 + b + 1] - from;\n"                                \
    "            vec3 low = vec3(at) * 64.0;\n"                                       \
    "            vec3 e = p - clamp(p, low, low + vec3(64.0));\n"                     \
    "            if (is_culled && dot(e, e) > reach2 * 1.01 + 1.0) { count = 0; }\n"  \
    "        }\n"                                                                     \
    "        s_from[t] = from;\n"                                                     \
    "        s_start[t] = count;\n"                                                   \
    "        barrier();\n"                                                            \
    "        int start = 0;\n"                                                        \
    "        for (int q = 0; q < t; q++) { start += s_start[q]; }\n"                  \
    "        int last = s_start[31];\n"                                               \
    "        barrier();\n"                                                            \
    "        s_start[t] = start;\n"                                                   \
    "        barrier();\n"                                                            \
    "        int total = s_start[31] + last;\n"                                       \
    "        for (int j = t; j < total; j += 32) {\n"                                 \
    "            int q = 0;\n"                                                        \
    "            for (int step = 16; step >= 1; step /= 2) {\n"                       \
    "                if (s_start[q + step] <= j) { q += step; }\n"                    \
    "            }\n"                                                                 \
    "            int i = s_from[q] + j - s_start[q];\n"                               \
    "            vec3 m = sorted[i * 2 + 1].xyz;\n"                                   \
    "            if (m == vec3(0.0)) { continue; }\n"                                 \
    "            vec3 d = p - sorted[i * 2].xyz;\n"                                   \
    "            float d2 = d.x * d.x + d.y * d.y + d.z * d.z;\n"                     \
    "            nearest = least(nearest, d2);\n"                                     \
    "            if (d2 >= reach2) { continue; }\n"                                   \
    "            float w2 = 1.0 - d2 / reach2;\n"                                     \
    "            w2 *= w2;\n"                                                         \
    "            along += w2 * (d.x * m.x + d.y * m.y + d.z * m.z);\n"                \
    "            normal += w2 * m;\n"                                                 \
    "            weight += w2;\n"                                                     \
    "        }\n"                                                                     \
    "        barrier();\n"                                                            \
    "    }\n"                                                                         \
    "    return all_of(Sums(nearest, along, normal, weight));\n"                      \
    "}\n"                                                                             \
    "bool distance_of(vec3 p, out float distance, out vec3 way) {\n"                  \
    "    distance = 0.0;\n"                                                           \
    "    way = vec3(0.0);\n"                                                          \
    "    Sums s = near_of(p, 1, 48.0, true);\n"                                       \
    "    bool is_near = s.nearest >= 0.0 &&\n"                                        \
    "                   max(1.5 * sqrt(s.nearest), 48.0) == 48.0;\n"                  \
    "    /* the nearest is among those walked where it is within 48 */\n"             \
    "    if (!is_near && !(s.nearest >= 0.0 && s.nearest <= 2304.0)) {\n"             \
    "        s = near_of(p, 1, 0.0, false);\n"                                        \
    "    }\n"                                                                         \
    "    if (s.nearest < 0.0) { s = near_of(p, 2, 0.0, false); }\n"                   \
    "    if (s.nearest < 0.0 || s.nearest > 10000.0) { return false; }\n"             \
    "    float reach = max(1.5 * sqrt(s.nearest), 48.0);\n"                           \
    "    if (!is_near) {\n"                                                           \
    "        s = near_of(p, int(reach / 64.0) + 1, reach, true);\n"                   \
    "    }\n"                                                                         \
    "    float length_of = length(s.normal);\n"                                       \
    "    if (s.weight <= 0.0 || length_of < 0.25 * s.weight) {\n"                     \
    "        return false;\n"                                                         \
    "    }\n"                                                                         \
    "    distance = s.along / s.weight;\n"                                            \
    "    way = s.normal / length_of;\n"                                               \
    "    return true;\n"                                                              \
    "}\n"                                                                             \
    "bool hold(inout vec3 p, float gap) {\n"                                          \
    "    float distance;\n"                                                           \
    "    vec3 way;\n"                                                                 \
    "    if (!distance_of(p, distance, way)) { return false; }\n"                     \
    "    bool was_inside = distance < 0.0;\n"                                         \
    "    for (int step = 0; step < 4 && distance < gap; step++) {\n"                  \
    "        p += (gap - distance) * way;\n"                                          \
    "        if (!distance_of(p, distance, way)) { break; }\n"                        \
    "    }\n"                                                                         \
    "    return was_inside;\n"                                                        \
    "}\n"

/* The cloth's points, kept out first: a warp a point. */
static const char g_hold_points_text[] = CARD_VERSION CARD_BODY_TEXT
    "layout(std430, binding = 6) writeonly buffer Points { vec4 points[]; };\n"
    "uniform vec3 u_points[64];\n"
    "uniform int u_count;\n"
    "uniform float u_gap;\n"
    "uniform int u_held;\n"
    "void main() {\n"
    "    int i = g_item;\n"
    "    if (i >= u_count) { return; }\n"
    "    vec3 p = u_points[i];\n"
    "    if (u_held != 0) { hold(p, u_gap); }\n"
    "    if (g_lane == 0) { points[i] = vec4(p, 0.0); }\n"
    "}\n";

/* Each place of the surface: its sum of the points, kept out; a warp a
 * place. */
static const char g_hold_places_text[] = CARD_VERSION CARD_BODY_TEXT
    "layout(std430, binding = 5) readonly buffer Data { uint data[]; };\n"
    "layout(std430, binding = 6) readonly buffer Points { vec4 points[]; };\n"
    "layout(std430, binding = 7) writeonly buffer Places { vec4 places[]; };\n"
    "uniform int u_points;\n"
    "uniform int u_count;\n"
    "uniform float u_gap;\n"
    "uniform int u_held;\n"
    "uniform int u_counting;\n"
    "void main() {\n"
    "    int i = g_item;\n"
    "    if (i >= u_count) { return; }\n"
    "    bool is_first = g_lane == 0;\n"
    "    vec3 p = vec3(0.0);\n"
    "    for (int j = 0; j < u_points; j++) {\n"
    "        float w = uintBitsToFloat(data[i * u_points + j]);\n"
    "        if (w != 0.0) { p += w * points[j].xyz; }\n"
    "    }\n"
    "    vec3 where = p;\n"
    "    if (hold(where, u_gap) && is_first) { atomicAdd(counts[0], 1u); }\n"
    "    if (u_held != 0) { p = where; }\n"
    "    if (u_counting != 0) {\n"
    "        where = p;\n"
    "        if (hold(where, 0.0) && is_first) { atomicAdd(counts[1], 1u); }\n"
    "    }\n"
    "    if (is_first) { places[i] = vec4(p, 0.0); }\n"
    "}\n";

/* Each vertex: its place, the turns of the quads about it across their
 * diagonals added in the quads' order, and its texture point, as
 * PortGlClothVertex lays them. */
static const char g_vertices_text[] = CARD_VERSION
    "layout(local_size_x = 64) in;\n"
    "layout(std430, binding = 5) readonly buffer Data { uint data[]; };\n"
    "layout(std430, binding = 7) readonly buffer Places { vec4 places[]; };\n"
    "layout(std430, binding = 9) writeonly buffer Out { float o[]; };\n"
    "uniform int u_count;\n"
    "uniform int u_quads;\n"
    "uniform int u_first;\n"
    "uniform int u_list;\n"
    "uniform int u_place;\n"
    "uniform int u_uv;\n"
    "vec3 at(int q, int c) {\n"
    "    return places[int(data[u_quads + q * 4 + c])].xyz;\n"
    "}\n"
    "void main() {\n"
    "    int i = int(gl_GlobalInvocationID.x);\n"
    "    if (i >= u_count) { return; }\n"
    "    int p = int(data[u_place + i]);\n"
    "    vec3 normal = vec3(0.0);\n"
    "    int to = int(data[u_first + p + 1]);\n"
    "    for (int k = int(data[u_first + p]); k < to; k++) {\n"
    "        int q = int(data[u_list + k]);\n"
    "        normal += cross(at(q, 2) - at(q, 0), at(q, 3) - at(q, 1));\n"
    "    }\n"
    "    float length_of = length(normal);\n"
    "    if (length_of > 0.0) { normal /= length_of; }\n"
    "    vec3 place = places[p].xyz;\n"
    "    o[i * 8] = place.x;\n"
    "    o[i * 8 + 1] = place.y;\n"
    "    o[i * 8 + 2] = place.z;\n"
    "    o[i * 8 + 3] = normal.x;\n"
    "    o[i * 8 + 4] = normal.y;\n"
    "    o[i * 8 + 5] = normal.z;\n"
    "    o[i * 8 + 6] = uintBitsToFloat(data[u_uv + i * 2]);\n"
    "    o[i * 8 + 7] = uintBitsToFloat(data[u_uv + i * 2 + 1]);\n"
    "}\n";

/* ---- starting ---- */

static void port_GlFighterCardSay(const char *what, const char *more) {
    snprintf(g_card.problem, sizeof(g_card.problem), "%s%s", what,
             more != NULL ? more : "");
}

static GLuint port_GlFighterCardProgram(const char *text) {
    GLuint shader = glCreateShader(GL_COMPUTE_SHADER);
    GLuint program;
    GLint ok = GL_FALSE;
    static char log[2048];

    glShaderSource(shader, 1, &text, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        glGetShaderInfoLog(shader, (GLsizei)sizeof(log), NULL, log);
        port_GlFighterCardSay("a compute shader does not compile: ", log);
        return 0;
    }
    program = glCreateProgram();
    glAttachShader(program, shader);
    glLinkProgram(program);
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        glGetProgramInfoLog(program, (GLsizei)sizeof(log), NULL, log);
        port_GlFighterCardSay("a compute shader does not link: ", log);
        return 0;
    }
    return program;
}

const char *port_GlFighterCardStart(void *(*getproc)(const char *)) {
    static const u32 zero[4] = {0, 0, 0, 0};

    if (g_card.is_started) {
        return NULL;
    }
    g_card.problem[0] = 0;
#define GL(result, name, args)                                                 \
    name = (result(*) args)getproc(#name);                                     \
    if (name == NULL) {                                                        \
        port_GlFighterCardSay("OpenGL has no ", #name);                        \
        return g_card.problem;                                                 \
    }
    PORT_GL_CARD_FUNCTIONS
#undef GL
    g_card.kept = POSES_KEPT;
    g_card.skin = port_GlFighterCardProgram(g_skin_text);
    g_card.scan = port_GlFighterCardProgram(g_scan_text);
    g_card.scatter = port_GlFighterCardProgram(g_scatter_text);
    g_card.sort = port_GlFighterCardProgram(g_sort_text);
    g_card.gather = port_GlFighterCardProgram(g_gather_text);
    g_card.hold_points = port_GlFighterCardProgram(g_hold_points_text);
    g_card.hold_places = port_GlFighterCardProgram(g_hold_places_text);
    g_card.vertices = port_GlFighterCardProgram(g_vertices_text);
    if (g_card.skin == 0 || g_card.scan == 0 || g_card.scatter == 0 ||
        g_card.sort == 0 || g_card.gather == 0 || g_card.hold_points == 0 ||
        g_card.hold_places == 0 || g_card.vertices == 0) {
        return g_card.problem;
    }
#define AT(member, program, name)                                              \
    g_card.member = glGetUniformLocation(g_card.program, name)
    AT(skin_at_world, skin, "u_world");
    AT(skin_at_turn, skin, "u_turn");
    AT(skin_at_dual, skin, "u_dual");
    AT(skin_at_joints, skin, "u_joints");
    AT(skin_at_count, skin, "u_count");
    AT(scatter_at_count, scatter, "u_count");
    AT(gather_at_count, gather, "u_count");
    AT(points_at_points, hold_points, "u_points");
    AT(points_at_count, hold_points, "u_count");
    AT(places_at_count, hold_places, "u_count");
    AT(points_at_gap, hold_points, "u_gap");
    AT(points_at_held, hold_points, "u_held");
    AT(places_at_points, hold_places, "u_points");
    AT(places_at_gap, hold_places, "u_gap");
    AT(places_at_held, hold_places, "u_held");
    AT(places_at_counting, hold_places, "u_counting");
    AT(vertices_at_count, vertices, "u_count");
    AT(vertices_at_quads, vertices, "u_quads");
    AT(vertices_at_first, vertices, "u_first");
    AT(vertices_at_list, vertices, "u_list");
    AT(vertices_at_place, vertices, "u_place");
    AT(vertices_at_uv, vertices, "u_uv");
#undef AT
    glGenBuffers(1, &g_card.counts);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_card.counts);
    glBufferData(GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)sizeof(zero), zero,
                 GL_DYNAMIC_COPY);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    g_card.is_started = TRUE;
    return NULL;
}

/* ---- a pose ---- */

static GLuint port_GlFighterCardMake(GLsizeiptr bytes) {
    GLuint buffer = 0;

    glGenBuffers(1, &buffer);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, buffer);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, NULL, GL_DYNAMIC_COPY);
    return buffer;
}

static s32 port_GlFighterCardRoom(CardPose *pose, s32 count) {
    if (pose->grid == 0) {
        pose->grid = port_GlFighterCardMake(GRID_WORDS * sizeof(s32));
    }
    if (pose->room >= count) {
        return TRUE;
    }
    if (pose->posed != 0) {
        GLuint old[4] = {pose->posed, pose->rank, pose->order, pose->sorted};

        glDeleteBuffers(4, old);
    }
    pose->posed = port_GlFighterCardMake((GLsizeiptr)count * 8 * sizeof(float));
    pose->rank = port_GlFighterCardMake((GLsizeiptr)count * 2 * sizeof(s32));
    pose->order = port_GlFighterCardMake((GLsizeiptr)count * sizeof(s32));
    pose->sorted =
        port_GlFighterCardMake((GLsizeiptr)count * 8 * sizeof(float));
    pose->room = count;
    return TRUE;
}

static GLuint port_GlFighterCardGroups(s32 count, s32 group) {
    return (GLuint)((count + group - 1) / group);
}

/* A pose of the slot under `key` already made: its number, or -1. */
static s32 port_GlFighterCardPoseFound(s32 slot, u64 key, s32 vertex_count) {
    s32 i;

    for (i = 0; i < g_card.kept; i++) {
        s32 at = slot * POSES_MAX + i;

        if (g_card.pose[at].key == key &&
            g_card.pose[at].count == vertex_count) {
            return at;
        }
    }
    return -1;
}

void port_GlFighterCardPoses(PortGlCardPoseAsk *asks, s32 count) {
    static const s32 lowest = (s32)0x80000000;
    static s32 made[PORT_GL_CARD_ASKS];
    s32 n = 0;
    s32 i;

    /* a batch's number, never 0, which no pose was asked in */
    g_card.asks = g_card.asks + 1 != 0 ? g_card.asks + 1 : 1;
    for (i = 0; i < count; i++) {
        PortGlCardPoseAsk *ask = &asks[i];
        CardPose *pose;
        s32 tried = 0;
        s32 at;

        ask->pose = -1;
        if (!g_card.is_started || ask->slot < 0 ||
            ask->slot >= PORT_FIGHTER_SLOTS || ask->vertex_count <= 0) {
            continue;
        }
        ask->key = ask->key != 0 ? ask->key : 1;
        ask->pose =
            port_GlFighterCardPoseFound(ask->slot, ask->key, ask->vertex_count);
        if (ask->pose >= 0) {
            g_card.pose[ask->pose].asked = g_card.asks;
            continue;
        }
        if (n == PORT_GL_CARD_ASKS) {
            continue;
        }
        /* the next of the ring that this batch has not asked for */
        do {
            at = ask->slot * POSES_MAX + g_card.next[ask->slot];
            g_card.next[ask->slot] = (g_card.next[ask->slot] + 1) % g_card.kept;
        } while (g_card.pose[at].asked == g_card.asks && ++tried < g_card.kept);
        pose = &g_card.pose[at];
        pose->asked = g_card.asks;
        pose->key = 0;
        if (!port_GlFighterCardRoom(pose, ask->vertex_count)) {
            continue;
        }
        /* found by the asks after it of the same key */
        pose->key = ask->key;
        pose->count = ask->vertex_count;
        ask->pose = at;
        made[n++] = i;
    }
    if (n == 0) {
        return;
    }
    /* each step for every new pose, and one barrier after it: the poses'
     * buffers are their own, so the card makes them side by side */
    PORT_GPU_BEGIN(PORT_GPU_SKIN);
    for (i = 0; i < n; i++) {
        const CardPose *pose = &g_card.pose[asks[made[i]].pose];

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, pose->grid);
        glClearBufferSubData(
            GL_SHADER_STORAGE_BUFFER, GL_R32I, 0, BUCKETS * sizeof(s32),
            GL_RED_INTEGER, GL_INT, NULL);
        glClearBufferSubData(GL_SHADER_STORAGE_BUFFER, GL_R32I,
                             PORT_GL_CARD_FEET_WORD * sizeof(s32), sizeof(s32),
                             GL_RED_INTEGER, GL_INT, &lowest);
    }
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUseProgram(g_card.skin);
    for (i = 0; i < n; i++) {
        const PortGlCardPoseAsk *ask = &asks[made[i]];
        const CardPose *pose = &g_card.pose[ask->pose];

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, ask->vertex_buffer);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, pose->posed);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, pose->grid);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, pose->rank);
        glUniformMatrix4fv(g_card.skin_at_world, PORT_FIGHTER_FILE_JOINTS,
                           GL_FALSE, ask->world[0]);
        glUniformMatrix3fv(g_card.skin_at_turn, PORT_FIGHTER_FILE_JOINTS,
                           GL_TRUE, ask->turn[0]);
        glUniform1i(g_card.skin_at_count, ask->vertex_count);
        if (ask->band_buffer != 0 && ask->dual != NULL) {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, ask->band_buffer);
            glUniform4fv(
                g_card.skin_at_dual, PORT_GL_JOINT_DUALS, ask->dual[0]);
            glUniform1i(g_card.skin_at_joints, 1);
        } else {
            glUniform1i(g_card.skin_at_joints, 0);
        }
        glDispatchCompute(
            port_GlFighterCardGroups(ask->vertex_count, GROUP), 1, 1);
    }
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUseProgram(g_card.scan);
    for (i = 0; i < n; i++) {
        glBindBufferBase(
            GL_SHADER_STORAGE_BUFFER, 2, g_card.pose[asks[made[i]].pose].grid);
        glDispatchCompute(1, 1, 1);
    }
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUseProgram(g_card.scatter);
    for (i = 0; i < n; i++) {
        const PortGlCardPoseAsk *ask = &asks[made[i]];
        const CardPose *pose = &g_card.pose[ask->pose];

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, pose->grid);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, pose->rank);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, pose->order);
        glUniform1i(g_card.scatter_at_count, ask->vertex_count);
        glDispatchCompute(
            port_GlFighterCardGroups(ask->vertex_count, GROUP), 1, 1);
    }
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUseProgram(g_card.sort);
    for (i = 0; i < n; i++) {
        const CardPose *pose = &g_card.pose[asks[made[i]].pose];

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, pose->grid);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, pose->order);
        glDispatchCompute(BUCKETS, 1, 1);
    }
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUseProgram(g_card.gather);
    for (i = 0; i < n; i++) {
        const PortGlCardPoseAsk *ask = &asks[made[i]];
        const CardPose *pose = &g_card.pose[ask->pose];

        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, pose->posed);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, pose->order);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, pose->sorted);
        glUniform1i(g_card.gather_at_count, ask->vertex_count);
        glDispatchCompute(
            port_GlFighterCardGroups(ask->vertex_count, GROUP), 1, 1);
    }
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUseProgram(0);
    PORT_GPU_END();
}

s32 port_GlFighterCardPose(
    s32 slot, u64 key, u32 vertex_buffer, s32 vertex_count,
    const float (*world)[16], const float (*turn)[9]) {
    PortGlCardPoseAsk ask;

    ask.slot = slot;
    ask.key = key;
    ask.vertex_buffer = vertex_buffer;
    ask.vertex_count = vertex_count;
    ask.world = world;
    ask.turn = turn;
    ask.band_buffer = 0;
    ask.dual = NULL;
    port_GlFighterCardPoses(&ask, 1);
    return ask.pose;
}

void port_GlFighterCardWarm(s32 slot, u32 vertex_buffer, s32 vertex_count,
                            const PortGlCardHold *holds, s32 count) {
    static float world[PORT_FIGHTER_FILE_JOINTS][16];
    static float turn[PORT_FIGHTER_FILE_JOINTS][9];
    s32 pose;
    s32 i;

    if (!g_card.is_started || slot < 0 || slot >= PORT_FIGHTER_SLOTS) {
        return;
    }
    for (i = 0; i < PORT_FIGHTER_FILE_JOINTS; i++) {
        world[i][0] = world[i][5] = world[i][10] = world[i][15] = 1.0f;
        turn[i][0] = turn[i][4] = turn[i][8] = 1.0f;
    }
    /* every pose's room at the model's size */
    for (i = 0; i < g_card.kept; i++) {
        port_GlFighterCardRoom(
            &g_card.pose[slot * POSES_MAX + i], vertex_count);
    }
    /* each program run once, on the bind pose: the driver makes a
     * compute shader ready for the card at its first dispatch */
    pose = port_GlFighterCardPose(
        slot, 1, vertex_buffer, vertex_count, (const float(*)[16])world,
        (const float(*)[9])turn);
    port_GlFighterCardHold(holds, count, pose, 0.0, TRUE, FALSE);
    for (i = 0; i < g_card.kept; i++) {
        g_card.pose[slot * POSES_MAX + i].key = 0;
    }
}

u32 port_GlFighterCardFeetBuffer(s32 pose) {
    if (pose < 0 || pose >= POSE_SLOTS || g_card.pose[pose].key == 0) {
        return 0;
    }
    return g_card.pose[pose].grid;
}

/* ---- a cloth ---- */

PortGlCardCloth *port_GlFighterCardCloth(const PortGlCloth *surface) {
    PortGlCardCloth *cloth;
    PortGlClothSums *sums;
    s32 *first;
    u32 *data;
    s32 words;
    s32 i;
    s32 c;

    if (!g_card.is_started) {
        return NULL;
    }
    cloth = calloc(1, sizeof(*cloth));
    if (cloth == NULL) {
        return NULL;
    }
    sums = &cloth->sums;
    port_GlClothSums(surface, sums);
    if (sums->point_count > PORT_FIGHTER_CLOTH_POINTS) {
        free(cloth);
        return NULL;
    }
    /* the words: the sums, the quads, where each place's quads begin in
     * the list and the list (the quads of each place in their order, a
     * quad that has a place twice there twice), each vertex's place and
     * its texture point */
    cloth->quads_at = sums->place_count * sums->point_count;
    cloth->first_at = cloth->quads_at + sums->quad_count * 4;
    cloth->list_at = cloth->first_at + sums->place_count + 1;
    cloth->place_at = cloth->list_at + sums->quad_count * 4;
    cloth->uv_at = cloth->place_at + sums->vertex_count;
    words = cloth->uv_at + sums->vertex_count * 2;
    data = malloc((size_t)words * sizeof(u32));
    first = calloc((size_t)sums->place_count + 1, sizeof(s32));
    if (data == NULL || first == NULL) {
        free(data);
        free(first);
        free(cloth);
        return NULL;
    }
    memcpy(
        data, sums->weight,
        (size_t)sums->place_count * (size_t)sums->point_count * sizeof(float));
    for (i = 0; i < sums->quad_count; i++) {
        for (c = 0; c < 4; c++) {
            data[cloth->quads_at + i * 4 + c] = (u32)sums->quad[i][c];
            first[sums->quad[i][c] + 1]++;
        }
    }
    for (i = 0; i < sums->place_count; i++) {
        first[i + 1] += first[i];
    }
    for (i = 0; i <= sums->place_count; i++) {
        data[cloth->first_at + i] = (u32)first[i];
    }
    for (i = 0; i < sums->quad_count; i++) {
        for (c = 0; c < 4; c++) {
            data[cloth->list_at + first[sums->quad[i][c]]++] = (u32)i;
        }
    }
    for (i = 0; i < sums->vertex_count; i++) {
        data[cloth->place_at + i] = (u32)sums->vertex_place[i];
        memcpy(
            &data[cloth->uv_at + i * 2], sums->vertex_uv[i], 2 * sizeof(float));
    }
    free(first);
    glGenBuffers(1, &cloth->data);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, cloth->data);
    glBufferData(
        GL_SHADER_STORAGE_BUFFER, (GLsizeiptr)words * 4, data, GL_STATIC_DRAW);
    free(data);
    cloth->points = port_GlFighterCardMake(
        (GLsizeiptr)PORT_FIGHTER_CLOTH_POINTS * 4 * sizeof(float));
    cloth->places = port_GlFighterCardMake(
        (GLsizeiptr)(sums->place_count + 1) * 4 * sizeof(float));
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    return cloth;
}

u32 port_GlFighterCardBuffer(s32 bytes) {
    GLuint buffer;

    if (!g_card.is_started || bytes <= 0) {
        return 0;
    }
    buffer = port_GlFighterCardMake((GLsizeiptr)bytes);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    return buffer;
}

void port_GlFighterCardKeep(s32 copies) {
    s32 kept = POSES_KEPT * (copies < 1 ? 1 : copies);

    g_card.kept = kept < POSES_MAX ? kept : POSES_MAX;
}

/* The body a set's cloths are kept out of, bound; FALSE where it has none. */
static s32 port_GlFighterCardBody(const PortGlCardHoldSet *set) {
    const CardPose *body;

    if (set->count <= 0 || set->pose < 0 || set->pose >= POSE_SLOTS ||
        g_card.pose[set->pose].key == 0) {
        return FALSE;
    }
    body = &g_card.pose[set->pose];
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, body->sorted);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, body->grid);
    return TRUE;
}

void port_GlFighterCardHolds(const PortGlCardHoldSet *sets, s32 count,
                             double gap, s32 is_held, s32 is_counting) {
    s32 k;
    s32 i;

    for (k = 0; k < count && sets[k].count <= 0; k++) {
    }
    if (k == count || !g_card.is_started) {
        return;
    }
    PORT_GPU_BEGIN(PORT_GPU_CLOTH);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 8, g_card.counts);
    /* each step for every cloth of every fighter, and one barrier after
     * it: the cloths' own buffers are their own, so the card runs them
     * side by side (a step's work is mostly waiting on memory, the
     * longest walk of one item its end) */
    glUseProgram(g_card.hold_points);
    glUniform1f(g_card.points_at_gap, (GLfloat)gap);
    glUniform1i(g_card.points_at_held, is_held);
    for (k = 0; k < count; k++) {
        for (i = 0; port_GlFighterCardBody(&sets[k]) && i < sets[k].count;
             i++) {
            const PortGlCardHold *hold = &sets[k].holds[i];
            const PortGlCardCloth *cloth = hold->cloth;

            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, cloth->points);
            glUniform3fv(
                g_card.points_at_points, cloth->sums.point_count, hold->points);
            glUniform1i(g_card.points_at_count, cloth->sums.point_count);
            glDispatchCompute((GLuint)cloth->sums.point_count, 1, 1);
        }
    }
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUseProgram(g_card.hold_places);
    glUniform1f(g_card.places_at_gap, (GLfloat)gap);
    glUniform1i(g_card.places_at_held, is_held);
    glUniform1i(g_card.places_at_counting, is_counting);
    for (k = 0; k < count; k++) {
        for (i = 0; port_GlFighterCardBody(&sets[k]) && i < sets[k].count;
             i++) {
            const PortGlCardCloth *cloth = sets[k].holds[i].cloth;

            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, cloth->data);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 6, cloth->points);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, cloth->places);
            glUniform1i(g_card.places_at_points, cloth->sums.point_count);
            glUniform1i(g_card.places_at_count, cloth->sums.place_count);
            glDispatchCompute((GLuint)cloth->sums.place_count, 1, 1);
        }
    }
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUseProgram(g_card.vertices);
    for (k = 0; k < count; k++) {
        for (i = 0; sets[k].count > 0 && i < sets[k].count; i++) {
            const PortGlCardHold *hold = &sets[k].holds[i];
            const PortGlCardCloth *cloth = hold->cloth;

            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, cloth->data);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 7, cloth->places);
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 9, hold->out);
            glUniform1i(g_card.vertices_at_count, cloth->sums.vertex_count);
            glUniform1i(g_card.vertices_at_quads, cloth->quads_at);
            glUniform1i(g_card.vertices_at_first, cloth->first_at);
            glUniform1i(g_card.vertices_at_list, cloth->list_at);
            glUniform1i(g_card.vertices_at_place, cloth->place_at);
            glUniform1i(g_card.vertices_at_uv, cloth->uv_at);
            glDispatchCompute(
                port_GlFighterCardGroups(cloth->sums.vertex_count, GROUP), 1,
                1);
        }
    }
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
    glUseProgram(0);
    PORT_GPU_END();
}

void port_GlFighterCardHold(const PortGlCardHold *holds, s32 count, s32 pose,
                            double gap, s32 is_held, s32 is_counting) {
    PortGlCardHoldSet set;

    set.holds = holds;
    set.count = count;
    set.pose = pose;
    port_GlFighterCardHolds(&set, 1, gap, is_held, is_counting);
}

void port_GlFighterCardCounts(u32 *inside, u32 *left_inside) {
    u32 counts[4] = {0, 0, 0, 0};

    if (g_card.is_started) {
        glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, g_card.counts);
        glGetBufferSubData(
            GL_SHADER_STORAGE_BUFFER, 0, (GLsizeiptr)sizeof(counts), counts);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }
    *inside = counts[0];
    *left_inside = counts[1];
}
