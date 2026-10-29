#ifndef AP_STAR_PIECES_H
#define AP_STAR_PIECES_H

#include "ap_star_api.h"

// The star is assembled in City Trial from six colored sphere items, delivered by the
// forced-content red box the vanilla legendary pieces ride in on. A sphere whose gate
// is closed is held out of the item registry, so it has no ItemKind at all.

// Install the spawn and drop patches.
void ApStarPieces_OnBoot(void);

// Import custom_items, subscribe to its pickups and match the six sphere archives.
void ApStarPieces_OnSaveLoaded(void);

// Enable or hold back each sphere item for the round about to load. Must run before
// CityItemSpawn_Init registers the custom items, so it hangs off the load start.
void ApStarPieces_On3DLoadStart(void);

// Drop the previous scene's collection, kinds and schedule.
void ApStarPieces_OnSceneChange(void);

// Resolve this round's sphere kinds and roll its delivery schedule.
void ApStarPieces_On3DLoadEnd(void);

// CustomItemDesc.name of one sphere.
const char *ApStarPieces_GetName(int piece);

void ApStarPieces_SetGate(u32 mask);
void ApStarPieces_AddAssembleHandler(ApStarAssembleFn fn);
int ApStarPieces_SpawnPiece(int piece, int ply);
int ApStarPieces_CollectPiece(int piece, int ply);
int ApStarPieces_Assemble(int ply);
int ApStarPieces_AssembledThisRound(int ply);

#endif // AP_STAR_PIECES_H
