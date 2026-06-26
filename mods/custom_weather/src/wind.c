// Global wind for custom_weather: one horizontal vector (gusting + wandering)
// that slants the rain/hail, nudges airborne items, and pushes gliding machines.

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "obj.h"
#include "machine.h"
#include "item.h"
#include "hoshi/settings.h"

#include "custom_weather.h"

#define WIND_PI       3.14159265358979f
#define WIND_DEG2RAD  (WIND_PI / 180.0f)

// ---- Module defaults (applied when a WindDef field is left 0) ----
#define WIND_DEF_SPEED      6.0f    // base wind speed, world units/frame
#define WIND_DEF_HEADING    90.0f   // base heading (degrees; 0 = +Z, 90 = +X)
#define WIND_DEF_GUSTINESS  0.35f   // speed pulses +/-35% around the base
#define WIND_DEF_CHAOS      0.25f   // heading wanders gently

// ---- Gust / heading random-walk shape ----
// A new target is rolled every *_PERIOD frames; the current value eases toward
// it by *_LERP each frame, so the motion reads as smooth gusting rather than
// per-frame jitter. WIND_HEAD_RANGE is the max heading deviation (degrees) at
// chaos = 1.
#define WIND_GUST_PERIOD    40
#define WIND_GUST_LERP      0.04f
#define WIND_HEAD_PERIOD    90
#define WIND_HEAD_LERP      0.02f
#define WIND_HEAD_RANGE     75.0f

// ---- Per-consumer coupling. The wind vector is one shared magnitude; each
// consumer scales it by its own susceptibility. Rain uses it directly (factor
// 1, in rain.c). Items are light and blow easily; machines are heavy and only
// the airborne/gliding ones are meaningfully shoved.
#define WIND_ITEM_FACTOR        0.08f  // fraction of wind added to an airborne item's velocity/frame
#define WIND_MACHINE_FACTOR     0.012f // fraction added to an airborne machine's velocity/frame at full glide
#define WIND_MACHINE_GLIDE_BASE 0.40f  // floor of the glide-stat susceptibility scale
#define WIND_STAT_GLIDE         5      // index of the glide stat in MachineData.stats

#define WIND_ITEM_GOBJ_KIND     22     // gobj->entity_class for a City Trial item
#define WIND_PLAYER_SLOTS       5

// ---- Resolved per-preset config (WindDef + defaults + global strength) ----
static int   stc_active = 0;
static float stc_base_speed = 0.0f;     // already scaled by the global strength
static float stc_base_heading = 0.0f;   // degrees
static float stc_gustiness = 0.0f;
static float stc_chaos = 0.0f;

// ---- Evolving state ----
static float stc_vx = 0.0f, stc_vz = 0.0f;   // current wind vector
static float stc_gust_cur = 0.0f, stc_gust_target = 0.0f;
static int   stc_gust_timer = 0;
static float stc_head_cur = 0.0f, stc_head_target = 0.0f; // heading offset, degrees
static int   stc_head_timer = 0;

// ---- Settings (persisted by hoshi menu save) ----
static const float wind_strength_factors[] = {0.0f, 0.5f, 1.0f, 1.5f, 2.0f};
static char *wind_strength_names[] = {"Off", "50%", "100%", "150%", "200%"};
#define WIND_STRENGTH_NUM (sizeof(wind_strength_factors) / sizeof(wind_strength_factors[0]))
static int wind_strength_index = 2; // default 100%

static char *wind_toggle_names[] = {"Off", "On"};
static int wind_randomize_dir = 0;  // default off: honor each preset's heading
static int wind_affect_machines = 1;
static int wind_affect_items = 1;

static float WindStrength(void)
{
    return wind_strength_factors[wind_strength_index];
}

// Symmetric random in [-1, 1).
static float Randf2(void)
{
    return HSD_Randf() * 2.0f - 1.0f;
}

// Latch the active preset's wind config, resolving each 0 field to its module
// default and folding in the global strength. NULL, disabled, or strength Off
// turns wind off (calm). Re-seeds the gust/heading walks so a preset change
// starts fresh.
void Wind_SetActive(const WindDef *def)
{
    if (!def || !def->enabled || WindStrength() <= 0.0f)
    {
        stc_active = 0;
        stc_vx = stc_vz = 0.0f;
        return;
    }
    stc_active = 1;

    float speed = def->speed > 0.0f ? def->speed : WIND_DEF_SPEED;
    stc_base_speed = speed * WindStrength();

    // Randomize Direction rolls a fresh base heading per activation; otherwise
    // the preset's authored heading is used (default 0 -> module default).
    if (wind_randomize_dir)
        stc_base_heading = HSD_Randf() * 360.0f;
    else
        stc_base_heading = (def->heading != 0.0f) ? def->heading : WIND_DEF_HEADING;

    stc_gustiness = def->gustiness > 0.0f ? def->gustiness : WIND_DEF_GUSTINESS;
    stc_chaos = def->chaos > 0.0f ? def->chaos : WIND_DEF_CHAOS;

    stc_gust_cur = 0.0f;
    stc_gust_target = Randf2();
    stc_gust_timer = WIND_GUST_PERIOD;
    stc_head_cur = 0.0f;
    stc_head_target = Randf2() * WIND_HEAD_RANGE * stc_chaos;
    stc_head_timer = WIND_HEAD_PERIOD;
}

void Wind_GetVector(Vec3 *out)
{
    if (!out)
        return;
    out->X = stc_vx;
    out->Y = 0.0f;
    out->Z = stc_vz;
}

// Blow every airborne item sideways. Settled items (grounded bit 0x10 set) are
// left alone so wind never drags a resting item across the ground.
static void Wind_ApplyToItems(float wx, float wz)
{
    float ax = wx * WIND_ITEM_FACTOR;
    float az = wz * WIND_ITEM_FACTOR;
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_ITEM]; g != NULL; g = g->next)
    {
        if (g->entity_class != WIND_ITEM_GOBJ_KIND)
            continue;
        ItemData *id = (ItemData *)g->userdata;
        if (id == NULL)
            continue;
        if (id->x35a & 0x10) // grounded -> not falling
            continue;
        id->vel.X += ax;
        id->vel.Z += az;
    }
}

// Push gliding machines. Only airborne machines (action_state_class == 1) are
// affected, scaled by their glide stat so a Winged Star catches far more wind
// than a Wheelie Bike. Dead/respawning machines are skipped.
static void Wind_ApplyToMachines(float wx, float wz)
{
    for (int ply = 0; ply < WIND_PLAYER_SLOTS; ply++)
    {
        GOBJ *mg = Ply_GetMachineGObj(ply);
        if (mg == NULL)
            continue;
        MachineData *md = (MachineData *)mg->userdata;
        if (md == NULL)
            continue;
        if (md->action_state_class != 1) // grounded -> not gliding
            continue;
        if (Machine_IsDead(md))
            continue;

        float glide = Machine_GetStatRatio(md, WIND_STAT_GLIDE); // [0,1]
        float scale = WIND_MACHINE_FACTOR *
                      (WIND_MACHINE_GLIDE_BASE + (1.0f - WIND_MACHINE_GLIDE_BASE) * glide);
        md->velocity.X += wx * scale;
        md->velocity.Z += wz * scale;
    }
}

void Wind_Tick(void)
{
    if (!stc_active)
    {
        stc_vx = stc_vz = 0.0f;
        return;
    }

    // Gust: ease the speed multiplier toward a fresh random target periodically.
    if (--stc_gust_timer <= 0)
    {
        stc_gust_target = Randf2();
        stc_gust_timer = WIND_GUST_PERIOD;
    }
    stc_gust_cur += (stc_gust_target - stc_gust_cur) * WIND_GUST_LERP;

    // Heading: ease the heading offset toward a fresh random target, bounded by
    // chaos so calm presets stay near their base direction.
    if (--stc_head_timer <= 0)
    {
        stc_head_target = Randf2() * WIND_HEAD_RANGE * stc_chaos;
        stc_head_timer = WIND_HEAD_PERIOD;
    }
    stc_head_cur += (stc_head_target - stc_head_cur) * WIND_HEAD_LERP;

    float speed = stc_base_speed * (1.0f + stc_gustiness * stc_gust_cur);
    if (speed < 0.0f)
        speed = 0.0f;

    float rad = (stc_base_heading + stc_head_cur) * WIND_DEG2RAD;
    stc_vx = speed * sinf(rad);
    stc_vz = speed * cosf(rad);

    if (wind_affect_items)
        Wind_ApplyToItems(stc_vx, stc_vz);
    if (wind_affect_machines)
        Wind_ApplyToMachines(stc_vx, stc_vz);
}

void Wind_Reset(void)
{
    stc_active = 0;
    stc_vx = stc_vz = 0.0f;
}

MenuDesc wind_menu = {
    .option_num = 4,
    .options = {
        &(OptionDesc){
            .name = "Wind Strength",
            .description = "Master wind multiplier for every CT preset (Off disables wind entirely)",
            .kind = OPTKIND_VALUE,
            .val = &wind_strength_index,
            .value_num = WIND_STRENGTH_NUM,
            .value_names = wind_strength_names,
        },
        &(OptionDesc){
            .name = "Randomize Direction",
            .description = "Roll a random wind heading each round instead of the preset's authored direction",
            .kind = OPTKIND_VALUE,
            .val = &wind_randomize_dir,
            .value_num = 2,
            .value_names = wind_toggle_names,
        },
        &(OptionDesc){
            .name = "Affect Machines",
            .description = "Let wind push gliding/airborne machines (scaled by their glide stat)",
            .kind = OPTKIND_VALUE,
            .val = &wind_affect_machines,
            .value_num = 2,
            .value_names = wind_toggle_names,
        },
        &(OptionDesc){
            .name = "Affect Items",
            .description = "Let wind blow falling items sideways",
            .kind = OPTKIND_VALUE,
            .val = &wind_affect_items,
            .value_num = 2,
            .value_names = wind_toggle_names,
        },
    },
};
