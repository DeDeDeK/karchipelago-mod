#include "game.h"
#include "hsd.h"
#include "os.h"
#include "enemy.h"
#include "code_patch/code_patch.h"

#include "enemy_ai.h"

int enemy_ai_preset_ar = ENEMY_AI_DEFAULT;
int enemy_ai_preset_ct = ENEMY_AI_DEFAULT;

char *enemy_ai_preset_names[ENEMY_AI_MENU_NUM] = {
    [ENEMY_AI_DEFAULT] = "Default",
    [ENEMY_AI_AGGRESSIVE] = "Aggressive",
    [ENEMY_AI_RELENTLESS] = "Relentless",
    [ENEMY_AI_DOCILE] = "Docile",
    [ENEMY_AI_ERRATIC] = "Erratic",
    [ENEMY_AI_TANKY] = "Tanky",
    [ENEMY_AI_RANDOM] = "Random",
};

// Multipliers on the vanilla EnemyParamTable.
typedef struct EnemyAIPresetDef
{
    float range;     // detect_range and leash_range
    float retarget;  // retarget cooldown bounds, <1 switches targets more often
    float knockback; // kb_launch
} EnemyAIPresetDef;

static const EnemyAIPresetDef stc_presets[ENEMY_AI_RANDOM] = {
    [ENEMY_AI_DEFAULT] = {.range = 1.0f, .retarget = 1.0f, .knockback = 1.0f},
    [ENEMY_AI_AGGRESSIVE] = {.range = 1.75f, .retarget = 0.6f, .knockback = 1.0f},
    [ENEMY_AI_RELENTLESS] = {.range = 2.5f, .retarget = 2.0f, .knockback = 0.65f},
    [ENEMY_AI_DOCILE] = {.range = 0.4f, .retarget = 1.3f, .knockback = 1.0f},
    [ENEMY_AI_ERRATIC] = {.range = 1.15f, .retarget = 0.3f, .knockback = 1.0f},
    [ENEMY_AI_TANKY] = {.range = 1.0f, .retarget = 1.0f, .knockback = 0.4f},
};

// A preloaded Enemy.dat is reused in place, so the table can arrive already
// scaled. Every load writes vanilla * multiplier from the first table seen.
static EnemyParamTable stc_vanilla;
static int stc_vanilla_captured = 0;

static int EnemyAI_Resolve(int selection)
{
    if (selection == ENEMY_AI_RANDOM)
        return ENEMY_AI_DEFAULT + 1 + HSD_Randi(ENEMY_AI_RANDOM - 1);
    return selection;
}

// Runs on every 3D scene load, so a menu change applies from the next load.
void EnemyAI_ApplyParams(void)
{
    EnemyParamTable *t = *stc_enemy_param_table;
    int preset;
    const EnemyAIPresetDef *def;
    int i;

    if (!stc_vanilla_captured)
    {
        stc_vanilla = *t;
        stc_vanilla_captured = 1;
    }

    // Any other major (the title demo) gets vanilla back.
    switch (Scene_GetCurrentMajor())
    {
    case MJRKIND_AIR:
        preset = EnemyAI_Resolve(enemy_ai_preset_ar);
        break;
    case MJRKIND_CITY:
        preset = EnemyAI_Resolve(enemy_ai_preset_ct);
        break;
    default:
        preset = ENEMY_AI_DEFAULT;
        break;
    }
    def = &stc_presets[preset];

    for (i = 0; i < 4; i++)
        t->kb_launch[i] = stc_vanilla.kb_launch[i] * def->knockback;
    t->detect_range = stc_vanilla.detect_range * def->range;
    t->leash_range = stc_vanilla.leash_range * def->range;
    t->retarget_min = (int)(stc_vanilla.retarget_min * def->retarget + 0.5f);
    t->retarget_max = (int)(stc_vanilla.retarget_max * def->retarget + 0.5f);

    if (preset != ENEMY_AI_DEFAULT)
        OSReport("[CustomAI] Enemy preset %s applied\n", enemy_ai_preset_names[preset]);
}

// Enemy_LoadCommonParams (0x801fd580) epilogue `lwz r0,20(r1)`: the table
// pointer is already stored to stc_enemy_param_table.
CODEPATCH_HOOKCREATE(0x801fd664,
    "",
    EnemyAI_ApplyParams,
    "",
    0)

void EnemyAI_InstallHook(void)
{
    CODEPATCH_HOOKAPPLY(0x801fd664);
}
