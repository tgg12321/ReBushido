/* The table of sounds (snd.h): what each sound number plays, and the banks
 * a stage's sound file fills.
 *
 * The file has no code. It is built without small data, which keeps the
 * four bytes at its end here. */
#include "common.h"
#include "bb2.h"

/* What each sound number plays: its bank (enum SndVabId) and its entry in
 * that bank's key-on table. A bank's sounds are in a row, counted from the
 * first; the ranges are in enum SoundId. */
/* clang-format off */
SoundDef g_snd_sounds[SND_SOUND_COUNT] = {
    /* the menus' sounds (MENU_SOUND_...) */
    {SND_VAB_COMMON, 0}, {SND_VAB_COMMON, 1}, {SND_VAB_COMMON, 2},
    {SND_VAB_COMMON, 3}, {SND_VAB_COMMON, 4},
    /* SND_MENU_SCREEN_FIRST: a 2D screen's own */
    {SND_VAB_MENU, 0},  {SND_VAB_MENU, 1},  {SND_VAB_MENU, 2},
    {SND_VAB_MENU, 3},  {SND_VAB_MENU, 4},  {SND_VAB_MENU, 5},
    {SND_VAB_MENU, 6},  {SND_VAB_MENU, 7},  {SND_VAB_MENU, 8},
    {SND_VAB_MENU, 9},  {SND_VAB_MENU, 10}, {SND_VAB_MENU, 11},
    {SND_VAB_MENU, 12}, {SND_VAB_MENU, 13}, {SND_VAB_MENU, 14},
    {SND_VAB_MENU, 15}, {SND_VAB_MENU, 16}, {SND_VAB_MENU, 17},
    {SND_VAB_MENU, 18}, {SND_VAB_MENU, 19}, {SND_VAB_MENU, 20},
    {SND_VAB_MENU, 21}, {SND_VAB_MENU, 22}, {SND_VAB_MENU, 23},
    {SND_VAB_MENU, 24}, {SND_VAB_MENU, 25}, {SND_VAB_MENU, 26},
    {SND_VAB_MENU, 27},
    /* SND_FIGHTER_FIRST: the first fighter's voice, */
    {SND_VAB_MODEL, 0},  {SND_VAB_MODEL, 1},  {SND_VAB_MODEL, 2},
    {SND_VAB_MODEL, 3},  {SND_VAB_MODEL, 4},  {SND_VAB_MODEL, 5},
    {SND_VAB_MODEL, 6},  {SND_VAB_MODEL, 7},  {SND_VAB_MODEL, 8},
    {SND_VAB_MODEL, 9},  {SND_VAB_MODEL, 10}, {SND_VAB_MODEL, 11},
    {SND_VAB_MODEL, 12}, {SND_VAB_MODEL, 13}, {SND_VAB_MODEL, 14},
    {SND_VAB_MODEL, 15},
    /* its weapon, */
    {SND_VAB_WEAPON_1, 0},  {SND_VAB_WEAPON_1, 1},  {SND_VAB_WEAPON_1, 2},
    {SND_VAB_WEAPON_1, 3},  {SND_VAB_WEAPON_1, 4},  {SND_VAB_WEAPON_1, 5},
    {SND_VAB_WEAPON_1, 6},  {SND_VAB_WEAPON_1, 7},  {SND_VAB_WEAPON_1, 8},
    {SND_VAB_WEAPON_1, 9},  {SND_VAB_WEAPON_1, 10}, {SND_VAB_WEAPON_1, 11},
    {SND_VAB_WEAPON_1, 12}, {SND_VAB_WEAPON_1, 13}, {SND_VAB_WEAPON_1, 14},
    {SND_VAB_WEAPON_1, 15},
    /* and its class */
    {SND_VAB_CLASS_1, 0}, {SND_VAB_CLASS_1, 1}, {SND_VAB_CLASS_1, 2},
    {SND_VAB_CLASS_1, 3}, {SND_VAB_CLASS_1, 4}, {SND_VAB_CLASS_1, 5},
    {SND_VAB_CLASS_1, 6}, {SND_VAB_CLASS_1, 7},
    /* the second fighter's voice, */
    {SND_VAB_MODEL_2, 0},  {SND_VAB_MODEL_2, 1},  {SND_VAB_MODEL_2, 2},
    {SND_VAB_MODEL_2, 3},  {SND_VAB_MODEL_2, 4},  {SND_VAB_MODEL_2, 5},
    {SND_VAB_MODEL_2, 6},  {SND_VAB_MODEL_2, 7},  {SND_VAB_MODEL_2, 8},
    {SND_VAB_MODEL_2, 9},  {SND_VAB_MODEL_2, 10}, {SND_VAB_MODEL_2, 11},
    {SND_VAB_MODEL_2, 12}, {SND_VAB_MODEL_2, 13}, {SND_VAB_MODEL_2, 14},
    {SND_VAB_MODEL_2, 15},
    /* its weapon, */
    {SND_VAB_WEAPON_2, 0},  {SND_VAB_WEAPON_2, 1},  {SND_VAB_WEAPON_2, 2},
    {SND_VAB_WEAPON_2, 3},  {SND_VAB_WEAPON_2, 4},  {SND_VAB_WEAPON_2, 5},
    {SND_VAB_WEAPON_2, 6},  {SND_VAB_WEAPON_2, 7},  {SND_VAB_WEAPON_2, 8},
    {SND_VAB_WEAPON_2, 9},  {SND_VAB_WEAPON_2, 10}, {SND_VAB_WEAPON_2, 11},
    {SND_VAB_WEAPON_2, 12}, {SND_VAB_WEAPON_2, 13}, {SND_VAB_WEAPON_2, 14},
    {SND_VAB_WEAPON_2, 15},
    /* and its class */
    {SND_VAB_CLASS_2, 0}, {SND_VAB_CLASS_2, 1}, {SND_VAB_CLASS_2, 2},
    {SND_VAB_CLASS_2, 3}, {SND_VAB_CLASS_2, 4}, {SND_VAB_CLASS_2, 5},
    {SND_VAB_CLASS_2, 6}, {SND_VAB_CLASS_2, 7},
    /* SND_STAGE_FIRST: the stage's */
    {SND_VAB_STAGE, 0},  {SND_VAB_STAGE, 1},  {SND_VAB_STAGE, 2},
    {SND_VAB_STAGE, 3},  {SND_VAB_STAGE, 4},  {SND_VAB_STAGE, 5},
    {SND_VAB_STAGE, 6},  {SND_VAB_STAGE, 7},  {SND_VAB_STAGE, 8},
    {SND_VAB_STAGE, 9},  {SND_VAB_STAGE, 10}, {SND_VAB_STAGE, 11},
    {SND_VAB_STAGE, 12}, {SND_VAB_STAGE, 13}, {SND_VAB_STAGE, 14},
    /* SND_CHANBARA_FIRST: chanbara's opponents' */
    {SND_VAB_CHANBARA, 0},  {SND_VAB_CHANBARA, 1},  {SND_VAB_CHANBARA, 2},
    {SND_VAB_CHANBARA, 3},  {SND_VAB_CHANBARA, 4},  {SND_VAB_CHANBARA, 5},
    {SND_VAB_CHANBARA, 6},  {SND_VAB_CHANBARA, 7},  {SND_VAB_CHANBARA, 8},
    {SND_VAB_CHANBARA, 9},  {SND_VAB_CHANBARA, 10}, {SND_VAB_CHANBARA, 11},
    {SND_VAB_CHANBARA, 12}, {SND_VAB_CHANBARA, 13}, {SND_VAB_CHANBARA, 14},
    {SND_VAB_CHANBARA, 15}, {SND_VAB_CHANBARA, 16}, {SND_VAB_CHANBARA, 17},
    {SND_VAB_CHANBARA, 18}, {SND_VAB_CHANBARA, 19}, {SND_VAB_CHANBARA, 20},
    {SND_VAB_CHANBARA, 21}, {SND_VAB_CHANBARA, 22}, {SND_VAB_CHANBARA, 23},
    {SND_VAB_CHANBARA, 24}, {SND_VAB_CHANBARA, 25}, {SND_VAB_CHANBARA, 26},
    {SND_VAB_CHANBARA, 27},
    /* SND_JUDGE_FIRST: the team battle's judge's */
    {SND_VAB_JUDGE, 0},  {SND_VAB_JUDGE, 1},  {SND_VAB_JUDGE, 2},
    {SND_VAB_JUDGE, 3},  {SND_VAB_JUDGE, 4},  {SND_VAB_JUDGE, 5},
    {SND_VAB_JUDGE, 6},  {SND_VAB_JUDGE, 7},  {SND_VAB_JUDGE, 8},
    {SND_VAB_JUDGE, 9},  {SND_VAB_JUDGE, 10}, {SND_VAB_JUDGE, 11},
    {SND_VAB_JUDGE, 12}, {SND_VAB_JUDGE, 13},
};
/* clang-format on */

/* The banks a stage's sound file fills, in the order it is read, and a
 * byte to make four. */
u8 g_snd_stage_vab_ids[4] = {
    SND_VAB_STAGE, SND_VAB_CLASS_1, SND_VAB_CLASS_2, 0};
