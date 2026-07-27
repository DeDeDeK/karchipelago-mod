#ifndef ENEMY_AI_H
#define ENEMY_AI_H

#include "datatypes.h"

// Behavior presets for the pool enemies of Air Ride courses and the City Trial
// Kirby Melee stadiums (Waddle Dee, Sword Knight, Scarfy, ...). City Trial's
// free-roam city has no pool enemies.
//
// A preset retunes the global enemy parameter table (stc_enemy_param_table); the
// per-enemy copies of the same knobs (ed+0x378 detect range, ed+0x3c0 move speed)
// are dead - nothing reads them.
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

// Multipliers on the global enemy param table; 1.0 keeps vanilla.
typedef struct EnemyAIPresetDef
{
    const char *name;           // Menu label
    const char *description;     // One-line behavior summary
    float range_mult;            // acquisition/mid/leash range (+0x80/+0x8C/+0x90)
    float retarget_mult;         // retarget cooldown bounds (+0x94/+0x98)
    float knockback_mult;        // per-tier kb magnitude/scale/launch (+0x30/+0x40/+0x50)
} EnemyAIPresetDef;

// Independent per-mode selections, each of which may be ENEMY_AI_RANDOM.
extern int enemy_ai_preset_ar;  // Air Ride courses
extern int enemy_ai_preset_ct;  // City Trial Kirby Melee stadiums

const EnemyAIPresetDef *EnemyAI_GetPresetDef(int preset);
const char *EnemyAI_GetPresetName(int preset);
// Name for any menu selection, including the "Random" sentinel.
const char *EnemyAI_GetSelectionName(int selection);
// Resolve a menu selection to a concrete preset index, rolling for "Random".
int EnemyAI_Resolve(int selection);

// Installs the hook that applies the active preset to the global enemy param
// table. Call once at boot.
void EnemyAI_InstallHook(void);

#endif // ENEMY_AI_H
