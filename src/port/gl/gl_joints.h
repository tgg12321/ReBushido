#ifndef PORT_GL_JOINTS_H
#define PORT_GL_JOINTS_H

/* The fighters' joints blended (ROADMAP 19; docs/PORT.md, "The joints
 * blended"). The game builds a fighter of rigid parts, one a bone, with a
 * few of its own vertices between two of them; where two parts turn
 * against each other their seam opens, creases or passes through itself.
 * With `joints blended` the vertices of the two parts near the seam are
 * carried by a mix of the two bones that turns rather than shrinks (dual
 * quaternions), so that the two parts are one skin bending about the
 * joint.
 *
 * Nothing of it is the game's: the bones are the game's, the hitboxes are
 * on them, and only where a model's vertices are drawn changes. The head,
 * the hands (which hold the weapon) and the feet (which stand on the
 * ground) stay rigid on their own bones.
 *
 * - When a model is read, each vertex near one of the joints of the table
 *   (gl_joints.c) is given the joint's two bones and how much of the second
 *   carries it: by where it lies along the second bone, from the first
 *   part's end of the band to the second's, and less the farther it lies
 *   from the joint's axis. The band's reach is a share of each part's own
 *   length from the joint, and its width the second part's own thickness
 *   there, both from the model's own vertices at its bind pose; the joint's
 *   place is the second bone's bind place.
 * - Each pose, each bone's skinning matrix (its world matrix times its
 *   inverse bind matrix) as a dual quaternion, made rigid about the bone's
 *   own place; a vertex of a band is the two bones' dual quaternions mixed,
 *   made unit, applied to its bind place, and its normal turned by the
 *   same rotation. */

#include "common.h"
#include "gl_fighter_file.h"

/* The dual quaternions of a pose: two four-vectors a bone (the rotation,
 * x y z w; then the dual part), as the shaders' `vec4` arrays read them. */
#define PORT_GL_JOINT_DUALS (2 * PORT_FIGHTER_FILE_JOINTS)

/* `joints blended` (1) or `rigid` (0, the game's own). */
extern void port_GlJointsSetOn(s32 is_on);
extern s32 port_GlJointsIsOn(void);

/* Each vertex of `file` its band, into `band` (vertex_count of them): the
 * first bone, the second, the second's share, and 0; or (0, 0, 0, 1) for a
 * vertex no joint moves. The number of vertices in a band (0 for a model
 * with no fighter's skeleton, a weapon's). */
extern s32 port_GlJointsWeigh(const PortFighterFile *file, float (*band)[4]);

/* How far from bone `bone`'s place a vertex of a band can be in any pose,
 * where that bone has the larger share of it: for the boxes of the shadows
 * and the fighters' layer (port_GlFighterCastBox). -1 for a vertex in no
 * band. */
extern float port_GlJointsReach(const PortFighterFile *file, const float *band,
                                const float *place, s32 bone);

/* The dual quaternions of a pose, into `dual` (PORT_GL_JOINT_DUALS): from
 * each bone's world matrix (a column after a column, the shaders' u_world:
 * the world's own units or less the camera's place, as long as the bands
 * are drawn with the same) and the model's inverse bind matrices. */
extern void port_GlJointsDuals(const float (*inverse_bind)[16],
                               const float (*world)[16], float (*dual)[4]);

/* GLSL: `vec3 joint_place(qa, da, qb, db, share, bind, inout normal)`, a
 * band's vertex at bind place `bind` posed by the mix of two bones' dual
 * quaternions (the first's rotation and dual part, the second's, and the
 * second's share), and its normal turned by the same rotation. A shader
 * reads each bone's two from its own array, at twice the bone's number. */
#define PORT_GL_JOINTS_GLSL                                                    \
    "vec3 joint_place(vec4 qa, vec4 da, vec4 qb, vec4 db, float share,\n"      \
    "                 vec3 bind, inout vec3 normal) {\n"                       \
    "    if (dot(qa, qb) < 0.0) {\n"                                           \
    "        qb = -qb;\n"                                                      \
    "        db = -db;\n"                                                      \
    "    }\n"                                                                  \
    "    vec4 r = mix(qa, qb, share);\n"                                       \
    "    vec4 d = mix(da, db, share);\n"                                       \
    "    float size = length(r);\n"                                            \
    "    r /= size;\n"                                                         \
    "    d /= size;\n"                                                         \
    "    vec3 t = 2.0 * (r.w * d.xyz - d.w * r.xyz + cross(r.xyz, d.xyz));\n"  \
    "    normal += 2.0 * cross(r.xyz, cross(r.xyz, normal) + r.w * normal);\n" \
    "    return bind + 2.0 * cross(r.xyz, cross(r.xyz, bind) + r.w * bind)\n"  \
    "           + t;\n"                                                        \
    "}\n"

#endif /* PORT_GL_JOINTS_H */
