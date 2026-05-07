#ifndef TEXTBOX_COLORS_H
#define TEXTBOX_COLORS_H

#include "datatypes.h"
#include "structs.h"
#include "rider.h"
#include "item.h"

// Per-noun text colors used by the AP textbox. RGB only — alpha is set
// per-frame by the textbox lifetime/fade machinery (see TextBox_SetAlpha).

extern const GXColor TextBox_DefaultColor;     // white, used for prefix/suffix
extern const GXColor TextBox_AbilityColors[COPYKIND_NUM];
extern const GXColor TextBox_KirbyColors[KIRBYCOLOR_NUM];

extern const GXColor TextBox_PatchColor;       // gold (patches/stat-ups)
extern const GXColor TextBox_MachineColor;     // sky blue (machines)
extern const GXColor TextBox_BoxColor;         // wood-brown (item boxes)
extern const GXColor TextBox_EventColor;       // violet (City Trial events)
extern const GXColor TextBox_StadiumColor;     // orange (stadium)
extern const GXColor TextBox_StageColor;       // cyan (Air Ride / Top Ride courses)
extern const GXColor TextBox_TopRideItemColor; // mustard (TR ability items)
extern const GXColor TextBox_ItemColor;        // light green (general items)
extern const GXColor TextBox_TrapColor;        // dark red (traps)
extern const GXColor TextBox_DeathColor;       // red (deathlink)
extern const GXColor TextBox_EnergyColor;      // cyan (energylink)
extern const GXColor TextBox_CheckColor;       // green (check sent)
extern const GXColor TextBox_GoalColor;        // gold (goal complete)
extern const GXColor TextBox_RewardColor;      // pale yellow (checklist reward)
extern const GXColor TextBox_ShopColor;        // teal (energylink shop purchase)

#endif // TEXTBOX_COLORS_H
