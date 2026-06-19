#include "game.h"
#include "os.h"
#include "scene.h"
#include "enemy.h"
#include "code_patch/code_patch.h"

#include "enemy_ai.h"

// Field offsets into the global enemy param table (stc_enemy_param_table, the
// Enemy.dat emDataAll block). The per-tier arrays are float[4] indexed by damage
// tier; a preset scales all four tiers uniformly.
#define EPT_KB_MAG     0x30  // float[4] per-tier knockback magnitude
#define EPT_KB_SCALE   0x40  // float[4] per-tier knockback scale
#define EPT_KB_LAUNCH  0x50  // float[4] per-tier launch speed
#define EPT_ACQ        0x80  // float    player acquisition radius (50.0)
#define EPT_MID        0x8C  // float    mid range (300.0)
#define EPT_LEASH      0x90  // float    max/leash range (500.0)
#define EPT_RETARGET0  0x94  // int      retarget cooldown lo bound (HSD_Randi arg)
#define EPT_RETARGET1  0x98  // int      retarget cooldown hi bound

// Vanilla baseline, snapshotted the first time we see the table - which is always
// before we have modified it, so the snapshot is genuine vanilla. We then always
// write base * mult, making re-application idempotent whether the underlying
// table buffer is reloaded fresh or returned cached, and letting "Default"
// restore the stock values exactly.
typedef struct EnemyParamBase
{
    float kb_mag[4];
    float kb_scale[4];
    float kb_launch[4];
    float acq;
    float mid;
    float leash;
    int   retarget0;
    int   retarget1;
} EnemyParamBase;

static EnemyParamBase stc_base;
static int stc_base_captured = 0;

static inline float *EnemyAI_F(void *t, int off)
{
    return (float *)((u8 *)t + off);
}

static inline int *EnemyAI_I(void *t, int off)
{
    return (int *)((u8 *)t + off);
}

static void EnemyAI_CaptureBase(void *t)
{
    int i;
    for (i = 0; i < 4; i++)
    {
        stc_base.kb_mag[i]    = EnemyAI_F(t, EPT_KB_MAG)[i];
        stc_base.kb_scale[i]  = EnemyAI_F(t, EPT_KB_SCALE)[i];
        stc_base.kb_launch[i] = EnemyAI_F(t, EPT_KB_LAUNCH)[i];
    }
    stc_base.acq       = *EnemyAI_F(t, EPT_ACQ);
    stc_base.mid       = *EnemyAI_F(t, EPT_MID);
    stc_base.leash     = *EnemyAI_F(t, EPT_LEASH);
    stc_base.retarget0 = *EnemyAI_I(t, EPT_RETARGET0);
    stc_base.retarget1 = *EnemyAI_I(t, EPT_RETARGET1);
    stc_base_captured = 1;
}

// Scale an integer cooldown bound, keeping it >= 1 (HSD_Randi needs a positive
// range).
static int EnemyAI_ScaleCooldown(int base, float mult)
{
    int v = (int)(base * mult + 0.5f);
    return v < 1 ? 1 : v;
}

// Apply the active per-mode enemy preset to the global param table. Runs from the
// Enemy_LoadCommonParams epilogue, after the table pointer has been populated.
// Changing the menu mid-session takes effect the next time the enemy system loads
// (the next Air Ride course / City Trial entry), mirroring the CPU re-profile hook.
void EnemyAI_ApplyParams(void)
{
    void *t = stc_enemy_param_table;
    MajorKind major;
    int selection;
    int preset;
    const EnemyAIPresetDef *def;
    int i;

    if (t == NULL)
        return;

    if (!stc_base_captured)
        EnemyAI_CaptureBase(t);

    // Only Air Ride courses and the City Trial Melee stadiums spawn pool enemies.
    major = Scene_GetCurrentMajor();
    if (major == MJRKIND_AIR)
        selection = enemy_ai_preset_ar;
    else if (major == MJRKIND_CITY)
        selection = enemy_ai_preset_ct;
    else
        return;

    preset = EnemyAI_Resolve(selection);
    def = EnemyAI_GetPresetDef(preset);

    for (i = 0; i < 4; i++)
    {
        EnemyAI_F(t, EPT_KB_MAG)[i]    = stc_base.kb_mag[i]    * def->knockback_mult;
        EnemyAI_F(t, EPT_KB_SCALE)[i]  = stc_base.kb_scale[i]  * def->knockback_mult;
        EnemyAI_F(t, EPT_KB_LAUNCH)[i] = stc_base.kb_launch[i] * def->knockback_mult;
    }
    *EnemyAI_F(t, EPT_ACQ)   = stc_base.acq   * def->range_mult;
    *EnemyAI_F(t, EPT_MID)   = stc_base.mid   * def->range_mult;
    *EnemyAI_F(t, EPT_LEASH) = stc_base.leash * def->range_mult;
    *EnemyAI_I(t, EPT_RETARGET0) = EnemyAI_ScaleCooldown(stc_base.retarget0, def->retarget_mult);
    *EnemyAI_I(t, EPT_RETARGET1) = EnemyAI_ScaleCooldown(stc_base.retarget1, def->retarget_mult);

    OSReport("[CustomAI] Enemy preset '%s' applied (%s)\n",
             def->name, major == MJRKIND_AIR ? "Air Ride" : "City Trial");
}

// Land on the first epilogue instruction of Enemy_LoadCommonParams (0x801fd664,
// `lwz r0,20(r1)`): by here the param-table pointer is stored to 0x805dd878.
// EnemyAI_ApplyParams reads the table from that global, so the hook needs no
// register setup. The clobbered load is re-executed by the hook framework.
CODEPATCH_HOOKCREATE(0x801fd664,
    "",
    EnemyAI_ApplyParams,
    "",
    0)

void EnemyAI_InstallHook(void)
{
    CODEPATCH_HOOKAPPLY(0x801fd664);
    OSReport("[CustomAI] Enemy param hook installed (Enemy_LoadCommonParams)\n");
}
