#ifndef AP_STAR_PIECE_HUD_H
#define AP_STAR_PIECE_HUD_H

#include "structs.h"

// The collection tracker: a row of sphere icons under the vanilla legendary-piece row,
// one per sphere a player holds, in collection order.

// Forget the icons and the icon archive, which the heap reset freed.
void ApStarPieceHud_OnSceneChange(void);

// Load the icons and read the row's anchors for a City Trial round.
void ApStarPieceHud_Load(void);

// Diff a player's collected set into their row. Run from the frame boundary rather than
// the pickup, so no GObj is created from inside the collision call that collected it.
void ApStarPieceHud_Update(int ply, u8 mask);

#endif // AP_STAR_PIECE_HUD_H
