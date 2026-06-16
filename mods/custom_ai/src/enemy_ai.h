#ifndef ENEMY_AI_H
#define ENEMY_AI_H

#include "datatypes.h"

// What an Air Ride enemy prefers to move toward under a preset.
typedef enum EnemyTargetPref
{
    ENEMY_TARGET_VANILLA = 0,   // Use the enemy's stock targeting
    ENEMY_TARGET_PLAYER,        // Prefer the nearest rider
    ENEMY_TARGET_ITEM,          // Prefer the nearest item box / dropped patch
    ENEMY_TARGET_FLEE,          // Move away from the nearest rider
} EnemyTargetPref;

// Behavior presets for Air Ride enemies (Waddle Dee, Sword Knight, etc.).
// The concrete presets are 0..ENEMY_AI_PRESET_NUM-1; ENEMY_AI_RANDOM is a
// menu-only sentinel that resolves to one of them at apply time.
typedef enum EnemyAIPreset
{
    ENEMY_AI_DEFAULT = 0,   // Vanilla AI - no changes
    ENEMY_AI_AGGRESSIVE,    // Wider detection, relentless pursuit, attack on sight
    ENEMY_AI_ITEM_HOARDER,  // Seek out item boxes and dropped patches
    ENEMY_AI_COWARD,        // Flee from nearby players
    ENEMY_AI_ERRATIC,       // Unpredictable - frequent target switching, wandering
    ENEMY_AI_PRESET_NUM,    // Count of concrete presets

    ENEMY_AI_RANDOM = ENEMY_AI_PRESET_NUM, // Menu-only: roll a concrete preset
    ENEMY_AI_MENU_NUM,      // Total selectable menu entries (presets + Random)
} EnemyAIPreset;

// Per-preset tuning knobs. The multipliers scale the matching EnemyData
// behavioral params (param_detect_range / param_chase_range / param_move_speed,
// declared in enemy.h) when an enemy spawns; 1.0 keeps the vanilla value.
//
// NOT applied yet - the enemy-spawn hook that consumes the table is still a TODO.
typedef struct EnemyAIPresetDef
{
    const char *name;           // Menu label
    const char *description;     // One-line behavior summary
    float detect_range_mult;     // Scales param_detect_range (1.0 = vanilla)
    float chase_range_mult;      // Scales param_chase_range
    float move_speed_mult;       // Scales param_move_speed
    int   target_pref;           // EnemyTargetPref
} EnemyAIPresetDef;

// Menu selection (may be ENEMY_AI_RANDOM). Bound to the settings menu in main.c.
extern int enemy_ai_preset;

const EnemyAIPresetDef *EnemyAI_GetPresetDef(int preset);
const char *EnemyAI_GetPresetName(int preset);
// Name for any menu selection, including the "Random" sentinel.
const char *EnemyAI_GetSelectionName(int selection);
// Resolve a menu selection to a concrete preset index, rolling for "Random".
int EnemyAI_Resolve(int selection);

#endif // ENEMY_AI_H
