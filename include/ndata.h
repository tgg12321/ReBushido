#ifndef NDATA_H
#define NDATA_H

/* The NDATA archive: the file that holds everything a fight loads (models,
 * stages, weapons, motions), read by entry number (ndata.c; the format is in
 * docs/formats/NDATA.md). */

#include "common.h"

#define NDATA_SECTOR_SIZE 2048

/* Where each kind of entry starts. */
enum NdataId {
    NDATA_MODELS = 0,
    /* These seven models can also come out of a chanbara pack. */
    NDATA_PACKED_MODELS = 30,
    NDATA_PACKED_MODEL_COUNT = 7,
    NDATA_CHANBARA_PACKS = 37, /* two packs of chanbara's opponents */
    NDATA_STAGES = 39,
    NDATA_TINY_MODELS = 77,
    NDATA_TINY_MODELS_GRAY = 104, /* the same again for grayscale mode */
    NDATA_WEAPONS = 131,          /* one file per pair of weapon sound kinds */
    NDATA_WEAPONS_SET_1 = 268,    /* the same for the second weapon set */
    NDATA_MOTIONS = 305,
};

/* Model numbers from here up share the files of the models 0x1B below. */
#define NDATA_FIRST_SHARED_MODEL 0x1F
#define NDATA_SHARED_MODEL_STEP 27

/* One entry of the directory, NDATA.INF. */
typedef struct {
    s16 start_sector;
    s16 length_sectors;
} NdataInfEntry;

/* Starts reading the directory from the disc over the copy built into the
 * program. */
extern void ndata_StartReadInf(void);

/* Reads entry `id` to `dest_addr`; if the destination is in the arena,
 * first makes sure the entry fits in what is left of it. Returns the entry's
 * size in bytes. */
extern s32 ndata_ReadChecked(s32 id, s32 dest_addr);
/* How much of the arena would be left after reading the entry: negative if
 * it does not fit. */
extern s32 ndata_GetArenaRoomAfter(s32 id);

/* Reading by what the entry is. */
extern void ndata_ReadModel(s32 model, s32 dest_addr);
extern void ndata_ReadStage(s32 stage, s32 dest_addr);
extern void ndata_ReadWeapons(s32 weapon_set, s32 file, s32 dest_addr);
#if RB_PORT
/* (The native port.) The same read from the player's image at once, with
 * no time of the drive's: two on two's partners' weapon sounds
 * (resource.h), so that a fight's load takes the time it took. */
extern void ndata_PortReadWeaponsNow(s32 weapon_set, s32 file, s32 dest_addr);
#endif
extern void ndata_ReadTinyModel(s32 tiny_model, s32 dest_addr);

/* A chanbara pack is one entry holding several of the opponents' models, so
 * that the next opponent can be had without going to the disc. While a pack
 * is loaded, ndata_ReadModel takes those models from it. */
extern void ndata_LoadPack(s32 pack, s32 pack_addr);
extern void ndata_InvalidatePack(void);
extern s32 ndata_IsPackLoaded(void);

#endif /* NDATA_H */
