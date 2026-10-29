#include "game.h"
#include "hsd.h"
#include "os.h"
#include "enemy.h"
#include "hurt.h"
#include "inline.h"
#include "code_patch/code_patch.h"

#include "enemy_ai.h"

int enemy_ai_preset_ar = ENEMY_AI_DEFAULT;
int enemy_ai_preset_ct = ENEMY_AI_DEFAULT;
int enemy_damage = ENEMY_DAMAGE_DEFAULT;
int enemy_behavior = ENEMY_BEHAVIOR_DEFAULT;

char *enemy_ai_preset_names[ENEMY_AI_MENU_NUM] = {
    [ENEMY_AI_DEFAULT] = "Default",
    [ENEMY_AI_AGGRESSIVE] = "Aggressive",
    [ENEMY_AI_RELENTLESS] = "Relentless",
    [ENEMY_AI_DOCILE] = "Docile",
    [ENEMY_AI_ERRATIC] = "Erratic",
    [ENEMY_AI_TANKY] = "Tanky",
    [ENEMY_AI_RANDOM] = "Random",
};

char *enemy_damage_names[ENEMY_DAMAGE_NUM] = {
    [ENEMY_DAMAGE_NONE] = "None",
    [ENEMY_DAMAGE_HALF] = "x0.5",
    [ENEMY_DAMAGE_DEFAULT] = "Default",
    [ENEMY_DAMAGE_DOUBLE] = "x2",
    [ENEMY_DAMAGE_TRIPLE] = "x3",
};

static const float stc_damage_mult[ENEMY_DAMAGE_NUM] = {
    [ENEMY_DAMAGE_NONE] = 0.0f,
    [ENEMY_DAMAGE_HALF] = 0.5f,
    [ENEMY_DAMAGE_DEFAULT] = 1.0f,
    [ENEMY_DAMAGE_DOUBLE] = 2.0f,
    [ENEMY_DAMAGE_TRIPLE] = 3.0f,
};

char *enemy_behavior_names[ENEMY_BEHAVIOR_NUM] = {
    [ENEMY_BEHAVIOR_DEFAULT] = "Default",
    [ENEMY_BEHAVIOR_PURSUE] = "Pursue",
    [ENEMY_BEHAVIOR_SKITTISH] = "Skittish",
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

#define ENEMY_DRIFT_SPEED 0.6f

// A preloaded Enemy.dat is reused in place, so the table can arrive already
// scaled. Every load writes vanilla * multiplier from the first table seen.
static EnemyParamTable stc_vanilla;
static int stc_vanilla_captured = 0;

static int EnemyAI_Resolve(int selection)
{
    if (selection == ENEMY_AI_RANDOM)
        return RandomInRange(ENEMY_AI_DEFAULT + 1, ENEMY_AI_RANDOM - 1);
    return selection;
}

// Runs on every 3D scene load, so Random rolls per load.
static void EnemyAI_ApplyParams(void)
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

    for (i = 0; i < GetElementsIn(t->kb_launch); i++)
        t->kb_launch[i] = stc_vanilla.kb_launch[i] * def->knockback;
    t->detect_range = stc_vanilla.detect_range * def->range;
    t->leash_range = stc_vanilla.leash_range * def->range;
    t->retarget_min = (int)(stc_vanilla.retarget_min * def->retarget + 0.5f);
    t->retarget_max = (int)(stc_vanilla.retarget_max * def->retarget + 0.5f);

    if (preset != ENEMY_AI_DEFAULT)
        OSReport("[CustomAI] Enemy preset %s applied\n", enemy_ai_preset_names[preset]);
}

// EnemyAnimCmd_SetHitDesc's (0x80200eb8) EventActor_EmitHitDesc call (0x8020111c), with the
// decoded region on its stack: scales what the region deals before it is armed. The title
// demo stays vanilla.
static void EnemyAI_EmitHitDesc(EnemyData *ed, int region, int joint, HurtParams *params)
{
    float mult = stc_damage_mult[enemy_damage];

    if (mult != 1.0f && !Gm_IsAutoDemo())
    {
        params->base_damage = (int)(params->base_damage * mult + 0.5f);
        params->base_knockback *= mult;
    }
    EventActor_EmitHitDesc(ed, region, joint, params);
}

// Bronto Burt, Scarfy and Bomber hover around their anchor, pos_initial, in every tier.
static int EnemyAI_IsFlyer(int kind)
{
    switch (kind % ENEMYKIND_ENEMIES_PER_TIER)
    {
    case ENEMYKIND_BRONTO_BURT:
    case ENEMYKIND_BRONTO_BURT_B:
    case ENEMYKIND_SCARFY:
    case ENEMYKIND_BOMBER:
        return 1;
    default:
        return 0;
    }
}

// Pursue drifts each flyer's anchor toward the nearest rider in leash range, Skittish away
// from it. Actors in a common (hit, inhaled, launched) state are left alone.
static void EnemyAI_DriftThink(GOBJ *gobj)
{
    float range = (*stc_enemy_param_table)->leash_range;
    float sign = enemy_behavior == ENEMY_BEHAVIOR_PURSUE ? ENEMY_DRIFT_SPEED : -ENEMY_DRIFT_SPEED;
    float best_dist, dist;
    EnemyData *ed;
    Vec3 pos, best_pos, dir;
    GOBJ *g;
    int i;

    for (g = (*stc_gobj_lookup)[GAMEPLINK_ENEMY]; g; g = g->next)
    {
        ed = g->userdata;
        if (ed->kind >= ENEMYKIND_SPECIAL_START || !EnemyAI_IsFlyer(ed->kind) || ed->state < ENEMYSTATE_PERTYPE ||
            ed->captor_gobj)
            continue;

        best_dist = range * range;
        for (i = 0; i < PLY_NUM; i++)
        {
            if (!Ply_GetRiderGObj(i))
                continue;
            Ply_GetPosition(i, &pos);
            dist = VECSquareDistance(&pos, &ed->pos_initial);
            if (dist < best_dist)
            {
                best_dist = dist;
                best_pos = pos;
            }
        }
        if (best_dist >= range * range)
            continue;

        VECSubtract(&best_pos, &ed->pos_initial, &dir);
        if (VECMag(&dir) < ENEMY_DRIFT_SPEED)
            continue;
        VECNormalize(&dir, &dir);
        VECScale(&dir, &dir, sign);
        VECAdd(&ed->pos_initial, &dir, &ed->pos_initial);
    }
}

// On GAMEPLINK_1, so the drift freezes with the enemies in every pause.
void EnemyAI_On3DLoadEnd(void)
{
    int major = Scene_GetCurrentMajor();

    if (enemy_behavior == ENEMY_BEHAVIOR_DEFAULT || (major != MJRKIND_AIR && major != MJRKIND_CITY))
        return;
    GObj_AddProc(GObj_Create(0, GAMEPLINK_1, 0), EnemyAI_DriftThink, 0);
}

// Enemy_LoadCommonParams (0x801fd580) epilogue `lwz r0,20(r1)`: the table
// pointer is already stored to stc_enemy_param_table.
CODEPATCH_HOOKCREATE(0x801fd664,
    "",
    EnemyAI_ApplyParams,
    "",
    0)

void EnemyAI_InstallHooks(void)
{
    CODEPATCH_HOOKAPPLY(0x801fd664);
    CODEPATCH_REPLACECALL(0x8020111c, EnemyAI_EmitHitDesc);
}
