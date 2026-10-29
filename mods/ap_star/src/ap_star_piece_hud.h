#ifndef AP_STAR_PIECE_HUD_H
#define AP_STAR_PIECE_HUD_H

#include "structs.h"

// The collection tracker: a row of sphere icons under the vanilla legendary-piece row,
// one per sphere a player holds, in collection order.

// Forget the icons and the icon archive, which the heap reset freed.
void ApStarPieceHud_OnSceneChange(void);

// Load the icons for a City Trial round and start diffing each player's collected set,
// `masks[PLY_NUM]`, into their row once a frame. The diff runs from a proc rather than
// the pickup, so no GObj is created inside the collision call that collected a sphere.
void ApStarPieceHud_Create(const u8 *masks);

#endif // AP_STAR_PIECE_HUD_H
