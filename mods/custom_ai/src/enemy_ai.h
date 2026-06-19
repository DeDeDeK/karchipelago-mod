#ifndef ENEMY_AI_H
#define ENEMY_AI_H

#include "datatypes.h"

// Behavior presets for the pool enemies that spawn in Air Ride courses and the
// City Trial Kirby Melee stadiums (Waddle Dee, Sword Knight, Scarfy, ...). City
// Trial's free-roam city has no pool enemies, so the City Trial selection only
// bites inside the Melee stadiums.
//
// A preset retunes the GLOBAL enemy parameter table (Enemy.dat emDataAll, pointer
// at stc_enemy_param_table) rather than any per-enemy copy: the obvious per-enemy
// knobs (ed+0x378 detect range, ed+0x3c0 move speed) are dead copies nothing
// reads. The global table is shared by every enemy at once and is reloaded fresh
// on each enemy-system init, so the preset is (re)applied from the loader's
// epilogue (EnemyAI_InstallHook). Only one mode is live at a time, so the apply
// hook reads MajorKind and picks the Air Ride vs City Trial selection.
//
// Three dials the global table honestly exposes:
//   range_mult     -> acquisition (+0x80), mid (+0x8C), leash (+0x90): how far
//                     enemies notice and pursue riders.
//   retarget_mult  -> HSD_Randi cooldown bounds (+0x94/+0x98): how often an enemy
//                     re-picks its nearest target. <1 = twitchy, >1 = locks on.
//   knockback_mult -> per-tier kb magnitude/scale/launch (+0x30/+0x40/+0x50):
//                     how far an enemy flies when hit. <1 = tanky / shrugs hits.
typedef enum EnemyAIPreset
{
    ENEMY_AI_DEFAULT = 0,   // Vanilla enemy params - no changes
    ENEMY_AI_AGGRESSIVE,    // Notices from afar, chases the closest rider
    ENEMY_AI_RELENTLESS,    // Enormous engage range, locks on, tough to knock away
    ENEMY_AI_DOCILE,        // Short reaction range - mostly leaves you alone
    ENEMY_AI_ERRATIC,       // Normal range but constantly switches targets
    ENEMY_AI_TANKY,         // Normal aggression, very hard to knock out of the arena
    ENEMY_AI_PRESET_NUM,    // Count of concrete presets

    ENEMY_AI_RANDOM = ENEMY_AI_PRESET_NUM, // Menu-only: roll a concrete preset
    ENEMY_AI_MENU_NUM,      // Total selectable menu entries (presets + Random)
} EnemyAIPreset;

// Per-preset multipliers on the global enemy param table; 1.0 keeps vanilla.
// EnemyAI_ApplyParams scales the vanilla baseline by these once per load.
typedef struct EnemyAIPresetDef
{
    const char *name;           // Menu label
    const char *description;     // One-line behavior summary
    float range_mult;            // acquisition/mid/leash range (+0x80/+0x8C/+0x90)
    float retarget_mult;         // retarget cooldown bounds (+0x94/+0x98)
    float knockback_mult;        // per-tier kb magnitude/scale/launch (+0x30/+0x40/+0x50)
} EnemyAIPresetDef;

// Per-mode selections (each may be ENEMY_AI_RANDOM). Independent so Air Ride and
// the City Trial Melee stadiums can run different enemy behavior. Bound to the
// settings menu in main.c.
extern int enemy_ai_preset_ar;  // Air Ride courses
extern int enemy_ai_preset_ct;  // City Trial Kirby Melee stadiums

const EnemyAIPresetDef *EnemyAI_GetPresetDef(int preset);
const char *EnemyAI_GetPresetName(int preset);
// Name for any menu selection, including the "Random" sentinel.
const char *EnemyAI_GetSelectionName(int selection);
// Resolve a menu selection to a concrete preset index, rolling for "Random".
int EnemyAI_Resolve(int selection);

// Install the Enemy_LoadCommonParams epilogue hook that applies the active
// per-mode preset to the global enemy param table. Call once at boot.
void EnemyAI_InstallHook(void);

#endif // ENEMY_AI_H
