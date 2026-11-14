#ifndef NARRATION_H
#define NARRATION_H

/* The narration (narration.c): what story mode opens with. Pictures fade in
 * and out one after another while lines of text come and go over them: a
 * battlefield, the two clans' crests, and a sword on its stand. */

#include "common.h"
#include "sprite.h"

/* The scenes the pictures go through. */
enum NarrationScene {
    NARRATION_PICTURE_1 = 0, /* a picture across the screen */
    NARRATION_LEFT = 1,      /* a smaller one on the left: a crest */
    NARRATION_BOTH = 2,      /* joined by one on the right: the other's */
    NARRATION_SWAP = 3,      /* the second picture's pixels are loaded */
    NARRATION_PICTURE_2 = 4, /* and it is shown as the first was */
    NARRATION_END = 5,       /* only text is left */
    NARRATION_SCENES
};

/* When a line of text comes and when it starts to go, in frames. */
typedef struct {
    s16 on;
    s16 off;
} NarrationLine;

#define NARRATION_LINES 21
/* A picture across the screen is this many strips, a sheet each. */
#define NARRATION_STRIPS 5

/* NAR.BIN, the narration's resource file. After the head:
 * - strip_cells: the cells every sheet of a picture is drawn with;
 * - five lists of sheets (narration_RelocateFile): the strips of the first
 *   picture and of the second, the two halves of the left picture and of
 *   the right, and the lines of text;
 * - lines: when each line shows; scene_starts and scene_ends: for each
 *   scene the frame its picture starts to come and the frame it starts to
 *   go (the last scene's end is the narration's);
 * - picture_2_pixels: the second picture, a strip each, which goes into
 *   video memory over the first when its turn comes. */
typedef struct {
    SpriteFileHead head;
    SpriteCell *strip_cells;
    SpriteSheet **picture_1_sheets;
    SpriteSheet **picture_2_sheets;
    SpriteSheet **left_sheets;
    SpriteSheet **right_sheets;
    SpriteSheet **line_sheets;
    NarrationLine *lines;
    s16 *scene_starts;
    s16 *scene_ends;
    s32 picture_2_pixels[NARRATION_STRIPS];
} NarrationFile;

/* Loads NAR.BIN to `memory` and starts the narration. */
extern s32 narration_Load(s32 memory);
/* A frame of it. Returns 1 when it is over or the cross button skips it. */
extern s32 narration_Update(s32 pressed);

#endif /* NARRATION_H */
