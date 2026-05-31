#ifndef TEXTBOX_COLORS_H
#define TEXTBOX_COLORS_H

#include "datatypes.h"
#include "structs.h"
#include "rider.h"
#include "item.h"
#include "game.h"

// Per-noun text colors. RGB only — alpha is set per-frame by the textbox
// lifetime/fade machinery. These are the canonical palette; the cross-mod
// API (textbox_api.h) re-exports them by value through TextBoxAPI fields.

extern const GXColor TextBox_DefaultColor;
extern const GXColor TextBox_AbilityColors[COPYKIND_NUM];
extern const GXColor TextBox_KirbyColors[KIRBYCOLOR_NUM];
extern const GXColor TextBox_ModeColors[GMMODE_NUM];
extern const GXColor TextBox_PatchColors[PATCHKIND_NUM];
extern const GXColor TextBox_BoxColors[BOXKIND_NUM];

extern const GXColor TextBox_MachineColor;
extern const GXColor TextBox_EventColor;
extern const GXColor TextBox_StadiumColor;     
extern const GXColor TextBox_StageColor;       
extern const GXColor TextBox_TopRideItemColor; 
extern const GXColor TextBox_ItemColor;        
extern const GXColor TextBox_TrapColor;        
extern const GXColor TextBox_DeathColor;       
extern const GXColor TextBox_EnergyColor;      
extern const GXColor TextBox_CheckColor;       
extern const GXColor TextBox_GoalColor;        
extern const GXColor TextBox_RewardColor;
extern const GXColor TextBox_ShopColor;
extern const GXColor TextBox_FillerColor;

#endif // TEXTBOX_COLORS_H
