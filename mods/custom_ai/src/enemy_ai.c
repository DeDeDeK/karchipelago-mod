#include "custom_ai.h"
#include "enemy_ai.h"

// Air Ride / City Trial Melee enemy preset selections. Each may hold
// ENEMY_AI_RANDOM; resolve to a concrete preset with EnemyAI_Resolve.
int enemy_ai_preset_ar = ENEMY_AI_DEFAULT;
int enemy_ai_preset_ct = ENEMY_AI_DEFAULT;

// Multipliers stack on top of the vanilla global param-table values; 1.0 keeps
// vanilla. EnemyAI_ApplyParams writes base * mult once per enemy-system load.
static const EnemyAIPresetDef enemy_presets[ENEMY_AI_PRESET_NUM] = {
    [ENEMY_AI_DEFAULT] = {
        .name = "Default",
        .description = "Vanilla enemy behavior - no changes",
        .range_mult     = 1.0f,
        .retarget_mult  = 1.0f,
        .knockback_mult = 1.0f,
    },
    [ENEMY_AI_AGGRESSIVE] = {
        .name = "Aggressive",
        .description = "Spots riders from much farther and chases the closest one",
        .range_mult     = 1.75f,
        .retarget_mult  = 0.6f,  // re-picks the nearest threat more often
        .knockback_mult = 1.0f,
    },
    [ENEMY_AI_RELENTLESS] = {
        .name = "Relentless",
        .description = "Enormous engage range, locks onto a target, shrugs off hits",
        .range_mult     = 2.5f,
        .retarget_mult  = 2.0f,  // commits to one target instead of switching
        .knockback_mult = 0.65f,
    },
    [ENEMY_AI_DOCILE] = {
        .name = "Docile",
        .description = "Short reaction range - enemies mostly leave you alone",
        .range_mult     = 0.4f,
        .retarget_mult  = 1.3f,
        .knockback_mult = 1.0f,
    },
    [ENEMY_AI_ERRATIC] = {
        .name = "Erratic",
        .description = "Normal range but constantly switches targets - jittery",
        .range_mult     = 1.15f,
        .retarget_mult  = 0.3f,  // very twitchy retargeting
        .knockback_mult = 1.0f,
    },
    [ENEMY_AI_TANKY] = {
        .name = "Tanky",
        .description = "Normal aggression but very hard to knock out of the arena",
        .range_mult     = 1.0f,
        .retarget_mult  = 1.0f,
        .knockback_mult = 0.4f,
    },
};

const EnemyAIPresetDef *EnemyAI_GetPresetDef(int preset)
{
    if (preset < 0 || preset >= ENEMY_AI_PRESET_NUM)
        preset = ENEMY_AI_DEFAULT;
    return &enemy_presets[preset];
}

const char *EnemyAI_GetPresetName(int preset)
{
    return EnemyAI_GetPresetDef(preset)->name;
}

const char *EnemyAI_GetSelectionName(int selection)
{
    if (selection == ENEMY_AI_RANDOM)
        return "Random";
    return EnemyAI_GetPresetName(selection);
}

int EnemyAI_Resolve(int selection)
{
    // Roll among the non-default presets so "Random" always changes behavior.
    if (selection == ENEMY_AI_RANDOM)
        return ENEMY_AI_DEFAULT + 1 + CustomAI_RollRandom(ENEMY_AI_PRESET_NUM - 1);
    if (selection < 0 || selection >= ENEMY_AI_PRESET_NUM)
        return ENEMY_AI_DEFAULT;
    return selection;
}
