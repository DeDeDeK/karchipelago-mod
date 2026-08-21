#ifndef GATE_AP_STAR_H
#define GATE_AP_STAR_H

// The Archipelago gate over the ap_star mod. That mod owns the machine, the six
// spheres and the assembly, and knows only which spheres are in play; this holds
// ap_star_piece_unlocked_mask, decides from it which those are, and announces one
// arriving. `piece` is an APStarPiece throughout.

// Import ap_star if it has not resolved yet and push the saved mask into it.
// Idempotent; safe from any scene.
void GateApStar_Resolve(void);

// Push ap_star_piece_unlocked_mask into the mod's gate. Called on every write to
// the mask, since ap_star reads its gate at 3D load start and mods run in
// alphabetical order - ap_star's load-start callback is already past by the time
// ours runs.
void GateApStar_PushMask(void);

// Mark one sphere unlocked. Returns 1 if applied.
int GateApStar_UnlockPiece(int piece);

// MachineKind of the Archipelago Star, or -1 while nothing has registered it.
int GateApStar_MachineKind(void);

// Drop one sphere in front of a player's machine, bypassing the delivery
// schedule. Returns 0 if the sphere was locked when this scene loaded.
int GateApStar_SpawnPiece(int piece, int ply);

// Add one sphere to every human rider's collected set and announce it, the
// give-item counterpart to unlocking one. Collects directly rather than spawning
// a pickup, so it lands whether or not the sphere is in this round's item
// registry - the way a Hydra or Dragoon part give ignores that part's unlock.
// Both return an APItemResult: RETRY while no player can take it, and DROP with
// ap_star absent, since no later round changes that.
int GateApStar_GivePiece(int piece);

// Put a human player straight through the assembly, awarding the star without
// the six spheres.
int GateApStar_GiveStar(void);

// 1 once a player has assembled the star this boot; 1 if this player assembled
// it in the round currently loaded.
int GateApStar_WasAssembled(void);
int GateApStar_AssembledThisRound(int ply);

#endif
