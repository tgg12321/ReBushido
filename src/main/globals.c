/* The small variables that no file can define for itself. A file reaches a
 * variable of eight bytes or fewer through the gp register only if it
 * defines it (main.c defines the ones its own code uses), and every file
 * that uses one of these reaches it by its full address. So they are
 * defined here, in a file with no code. Each is declared, and explained, in
 * its subsystem's header. */
#include "common.h"
#include "bb2.h"

/* The screens (sys.h). */
s32 g_sys_screen_frames;

/* The fight, the run against the CPU, chanbara and the team battle (fight.h).
 */
s16 g_fight_stage;
s8 g_fight_loaded_stage;
s16 g_fight_first_person;
s16 g_fight_viewer;
MATRIX *g_fight_squashed_view;
u16 g_fight_rng_seed;
u8 g_fight_restart;
u8 g_fight_restart_pair;
u8 g_fight_start_pair;
s16 g_fight_defeat_frames;
s8 g_fight_winner;
u8 g_fight_wins_p1;
u8 g_fight_wins_p2;
s32 g_fight_continues;
u8 g_fight_story_wounds;
u8 g_fight_story_wounds_p2;
u8 g_fight_choice;
u8 g_fight_first_choice;
u8 g_fight_rematch_offered;
u8 g_fight_choice_timer;
s32 g_fight_distance;
s32 g_fight_distance_change;
s16 g_fight_blow_dir_x;
s16 g_fight_blow_dir_y;
s16 g_fight_blow_dir_z;
s16 g_fight_paired_move_initiator;
s32 g_fight_paired_move_separation;
u8 g_fight_paired_move_break_frame;
s16 g_fight_paired_move_start_pending;
MoveScript *g_fight_paired_move;
u16 g_fight_paired_move_flags;
s16 g_fight_paired_move_is_common;
u8 g_fight_falling_fighter;
u8 g_fight_falling_forward;
s16 g_fight_blades_met;
s16 g_fight_lock_requested;
s16 g_fight_lock_runner;
s16 g_fight_lock_frames;
s16 g_fight_lock_flash_delay;
u8 g_vscpu_count;
u8 g_vscpu_faced;
u8 g_vscpu_weapon_cursor;
u8 g_chanbara_count;
u8 g_chanbara_kind;
u8 g_chanbara_model;
CVECTOR g_chanbara_tint;
u8 g_chanbara_area_changed;
s32 g_chanbara_time;
u8 g_chanbara_rank;
u8 g_chanbara_fast_unlock;
u8 g_chanbara_clean_unlock;
u8 g_team_match_count;
u8 g_team_match;
u8 g_team_points_to_win;
u8 g_team_time_limit;
s32 g_team_time_frames;
u8 g_team_clan_bits;
u8 g_team_results[5];
u8 g_team_winner;
u8 g_team_begin_countdown;
u8 g_team_out_timer;
u8 g_team_warning_timer;
u8 g_team_ippon_timer;
u8 g_team_break_timer;
u8 g_team_time_up_timer;
u8 g_team_result_timer;
u8 g_team_penalized;
/* The points and warnings of the team battle's match, a variable for each
 * side (see fight.h). */
u8 g_team_points;
u8 g_team_points_p2;
u8 g_team_warnings;
u8 g_team_warnings_p2;
u8 g_fight_ending;

/* The story (story.h). */
u8 *g_story_script_cursor;
u8 g_story_chapter_fights;
u8 g_story_route[8];
u8 g_story_opponent_named;
u8 g_story_henchmen_count;
u8 g_story_henchmen_left;
u8 g_story_first_henchmen;
CVECTOR g_story_henchman_tint;
u8 *g_story_interlude;
u8 g_story_interlude_scene;
u8 g_story_player_roster;
u8 g_story_player_wounds;
u8 g_story_guest_roster;
u8 g_story_guest_wounds;
s32 g_story_unlocks;
u8 *g_story_victim;
u8 g_story_victim_scene;
u8 *g_story_cutscene;
u8 g_story_cutscene_set;

/* The cutscenes (cutscene.h). */
u8 g_cutscene_unused_start_flag;

/* The camera (camera.h). */
CameraView *g_camera_current;
u8 g_camera_mode;
u8 g_camera_zoom_used;
s16 g_camera_turn_frames;

/* Motions (motion.h). */
s32 g_motion_common_file;
u8 g_motion_common_loaded;
u16 g_motion_loaded_sets[2];
MotionFrame *g_motion_win_frames[2];
u8 g_motion_win_frames_loaded[2];

/* The renderer (render.h). */
u32 *g_render_ot_base;
SVECTOR g_render_view_tilt;

/* The scene's nodes (node.h). */
SceneNode *g_node_identity_root_ptr;

/* The scene (scene.h). */
u16 g_scene_count;

/* Sound (snd.h). */
s16 g_snd_ambient_timers[SND_AMBIENCE_SLOTS];
u8 g_snd_prop_sound_ticks[SND_PROP_SLOTS];

/* The pads (pad.h). */
u8 g_pad_remote_map_circle;
u8 g_pad_remote_map_cross;
u8 g_pad_remote_map_square;

/* The link cable (link.h). */
s32 g_link_side;
s32 g_link_peer_unlock_mask;
/* The second word of each packet of link.c, which defines the first.
 * FAKE: no variables of their own; reached from the first words */
s32 g_link_send_check_word;
s32 g_link_recv_check_word;
s32 g_link_prev_send_check_word;
