#ifndef BB2_H
#define BB2_H

/* Everything a file of the game's code (src/main/) may use: Sony's libraries
 * and each subsystem's header. Nothing is declared here. */

#include "common.h"
/* ReBushido's switches, all 0 in the matching build: ahead of every header,
 * so that any of them can ask. */
#include "rebushido.h"
#include <psxsdk/kernel.h>
#include <psxsdk/libapi.h>
#include <psxsdk/libc.h>
#include <psxsdk/libcd.h>
#include <psxsdk/libcomb.h>
#include <psxsdk/libetc.h>
#include <psxsdk/libgpu.h>
#include <psxsdk/libgte.h>
#include <psxsdk/libsnd.h>
#include <psxsdk/libspu.h>

/* The system: boot, display, pad, disc, memory. */
#include "sys.h"
#include "disp.h"
#include "pad.h"
#include "cdrom.h"
#include "arena.h"
#include "ndata.h"
#include "resource.h"
#include "memcard.h"
#include "link.h"
#include "snd.h"

/* The scene and how it is drawn. */
#include "math.h"
#include "node.h"
#include "mesh.h"
#include "model.h"
#include "light.h"
#include "render.h"
#include "tex.h"
#include "stage.h"
#include "wave.h"
#include "scene.h"
#include "cloth.h"
#include "eff.h"
#include "camera.h"

/* What is drawn over the scene, and the menus. */
#include "sprite.h"
#include "font.h"
#include "hud.h"
#include "menu.h"
#include "select.h"
#include "narration.h"
#include "staff.h"

/* The fight. */
#include "collision.h"
#include "motion.h"
#include "cutscene.h"
#include "weapon.h"
#include "object.h"
#include "cpu.h"
#include "fighter.h"
#include "fight.h"
#include "story.h"
#include "replay.h"

/* What only the ReBushido build has (src/rebushido/). */
#if REBUSHIDO
#include "rb_settings.h"
#include "rb_save.h"
#include "rb_options.h"
#include "rb_quickboot.h"
#endif

/* What only the native port has (src/port/). */
#if RB_PORT
#include "port_gte.h"
#include "port_lights.h"
#include "port_platform.h"
#include "port_remaster.h"
#include "port_state.h"
#include "port_weapons.h"
#include "port_arenas.h"
#include "port_stage_choice.h"
#include "port_play.h"
#include "port_fight_music.h"
#include "port_top_menu.h"
#include "port_match.h"
#include "port_duo.h"
#include "port_training.h"
#include "port_cpu.h"
#include "port_stats.h"
#include "port_theatre.h"
#endif

#endif /* BB2_H */
