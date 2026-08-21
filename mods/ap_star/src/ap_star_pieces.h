#ifndef AP_STAR_PIECES_H
#define AP_STAR_PIECES_H

// The Archipelago Star is assembled in City Trial from six colored spheres, the way
// Hydra and Dragoon are assembled from their three parts. The spheres are custom items
// (items/ApSphere*.dat) delivered by the same forced-content red box the vanilla pieces
// ride in on, scheduled against match progress. A sphere whose gate is closed is held
// out of the item registry, so it has no ItemKind and no path can spawn it.

// Install the pickup handler and the two spawn-path hooks.
void ApStarPieces_OnBoot(void);

// Enable or hold back each sphere item for the round about to load. Must run
// before CityItemSpawn_Init registers the custom items, so it hangs off the load
// start rather than the load end.
void ApStarPieces_On3DLoadStart(void);

// Clear the per-player collection and roll this round's spawn schedule.
void ApStarPieces_On3DLoadEnd(void);

// Drive the collection tracker and the deferred mount. Collection lands inside
// the item-touch call, which is no place to create HUD objects or tear the
// collecting machine down, so both wait for the frame boundary.
void ApStarPieces_OnFrameStart(void);

// CustomItemDesc.name of one sphere. `piece` is an APStarPieceKind.
const char *ApStarPieces_GetName(int piece);

// Drop one sphere on the ground in front of a player's machine, bypassing the
// delivery schedule. `piece` is an APStarPieceKind. Returns 1 if one spawned;
// the sphere has to be open as of this scene's load to have an ItemKind at all,
// so a closed one reports and spawns nothing.
int ApStarPieces_DebugSpawn(int piece, int ply);

// 1 once a human player has assembled the star this boot. Sticky.
int ApStarPieces_WasAssembled(void);

// 1 if this player assembled the star in the round currently loaded. Cleared on
// every 3D scene load, the same scope the vanilla per-run assembly flags have.
int ApStarPieces_AssembledThisRound(int ply);

#endif // AP_STAR_PIECES_H
