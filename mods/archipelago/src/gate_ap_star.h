#ifndef GATE_AP_STAR_H
#define GATE_AP_STAR_H

// Archipelago's gate over the ap_star mod: owns ap_star_piece_unlocked_mask and announces
// sphere grants. `piece` is an APStarPiece throughout.

// Imports ap_star, binds the star's MachineKind and pushes the saved mask. Anything still
// missing then stays missing.
void GateApStar_Resolve(void);

// Called on every write to the sphere or box mask: ap_star reads its gate at 3D load
// start, before this mod's callback runs.
void GateApStar_PushMask(void);

int GateApStar_UnlockPiece(int piece);

// MachineKind of the Archipelago Star, bound at OnSaveLoaded; -1 before that or while
// unregistered. Binds AP_MACHINE_BIT_AP_STAR to a kind.
int GateApStar_MachineKind(void);

// Drops one sphere in front of a player's machine. Returns 0 if the sphere was locked as
// this scene loaded.
int GateApStar_SpawnPiece(int piece, int ply);

// Adds one sphere to every human rider's collected set, bypassing this round's item
// registry.
int GateApStar_GivePiece(int piece);

// Runs a human through the assembly. Both gives return an APItemResult: RETRY while no
// player can take it, DROP with ap_star absent.
int GateApStar_GiveStar(void);

// 1 if this player assembled the star in the round loaded now.
int GateApStar_AssembledThisRound(int ply);

// 1 if a DmgLog.credited_attack names one of the star's sphere shots.
int GateApStar_IsShotAttack(int credited_attack);

#endif
