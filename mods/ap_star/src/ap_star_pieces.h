#ifndef AP_STAR_PIECES_H
#define AP_STAR_PIECES_H

// The star is assembled in City Trial from six colored sphere items, delivered by the
// forced-content red box the vanilla legendary pieces ride in on. A sphere whose gate
// is closed is held out of the item registry, so it has no ItemKind at all.

// Install the spawn and drop patches.
void ApStarPieces_OnBoot(void);

// Enable or hold back each sphere item for the round about to load. Must run before
// CityItemSpawn_Init registers the custom items, so it hangs off the load start.
void ApStarPieces_On3DLoadStart(void);

// Clear the per-player collection and roll this round's spawn schedule.
void ApStarPieces_On3DLoadEnd(void);

// Drive the collection tracker and the deferred mount. Collection lands inside the
// item-touch call, which is no place to create HUD objects or tear the collecting
// machine down, so both wait for the frame boundary.
void ApStarPieces_OnFrameStart(void);

// CustomItemDesc.name of one sphere.
const char *ApStarPieces_GetName(int piece);

int ApStarPieces_SpawnPiece(int piece, int ply);
int ApStarPieces_CollectPiece(int piece, int ply);
int ApStarPieces_Assemble(int ply);
int ApStarPieces_AssembledThisRound(int ply);

#endif // AP_STAR_PIECES_H
