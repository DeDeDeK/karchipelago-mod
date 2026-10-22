// Global wind for custom_weather: one horizontal vector (gusting + wandering)
// that slants the rain/hail, nudges airborne items, and pushes gliding machines.

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "obj.h"
#include "machine.h"
#include "item.h"
#include "inline.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

// Module defaults, applied when a WindDef field is left 0.
#define WIND_DEF_SPEED      6.0f    // base wind speed, world units/frame
#define WIND_DEF_HEADING    90.0f   // base heading (degrees; 0 = +Z, 90 = +X)
#define WIND_DEF_GUSTINESS  0.35f   // speed pulses +/-35% around the base
#define WIND_DEF_CHAOS      0.25f   // heading wanders gently

// Gust / heading random walk: a fresh target is rolled every *_PERIOD frames and
// eased toward by *_LERP each frame, so motion reads as gusting rather than jitter.
#define WIND_GUST_PERIOD    40
#define WIND_GUST_LERP      0.04f
#define WIND_HEAD_PERIOD    90
#define WIND_HEAD_LERP      0.02f
#define WIND_HEAD_RANGE     75.0f      // max heading deviation (deg) at chaos = 1

// Per-consumer coupling: the wind vector is one shared magnitude that each consumer
// scales by its own susceptibility (rain uses it directly, at factor 1).
#define WIND_ITEM_FACTOR        0.08f  // fraction of the wind speed added to a falling item's velocity/frame
#define WIND_MACHINE_FACTOR     0.012f // fraction added to an airborne machine's velocity/frame at full glide
#define WIND_MACHINE_GLIDE_BASE 0.40f  // floor of the glide-stat susceptibility scale

// Resolved per-preset config (WindDef + defaults + global strength).
static int   stc_active = 0;
static float stc_base_speed = 0.0f;     // already scaled by the global strength
static float stc_base_heading = 0.0f;   // degrees
static float stc_gustiness = 0.0f;
static float stc_chaos = 0.0f;

static float stc_vx = 0.0f, stc_vz = 0.0f;   // current wind vector
static float stc_gust_cur = 0.0f, stc_gust_target = 0.0f;
static int   stc_gust_timer = 0;
static float stc_head_cur = 0.0f, stc_head_target = 0.0f; // heading offset, degrees
static int   stc_head_timer = 0;

// Index 0 ("Preset") is the pass-through value.
static const float wind_strength_factors[] = {1.0f, 0.0f, 0.5f, 1.5f, 2.0f};
static char *wind_strength_names[] = {"Preset", "Off", "50%", "150%", "200%"};
WEATHER_KNOB_CHECK(wind_strength_factors, wind_strength_names);
static int wind_strength_index = 0;

static int wind_randomize_dir = 0;
static int wind_affect_machines = 1;
static int wind_affect_items = 1;

// Also re-seeds the gust and heading random walks.
void Wind_SetActive(const WindDef *def)
{
    float strength = wind_strength_factors[wind_strength_index];
    if (!def || !def->enabled || strength <= 0.0f)
    {
        stc_active = 0;
        stc_vx = stc_vz = 0.0f;
        return;
    }
    stc_active = 1;

    float speed = def->speed > 0.0f ? def->speed : WIND_DEF_SPEED;
    stc_base_speed = speed * strength;

    if (wind_randomize_dir)
        stc_base_heading = HSD_Randf() * 360.0f;
    else
        stc_base_heading = (def->heading != 0.0f) ? def->heading : WIND_DEF_HEADING;

    stc_gustiness = def->gustiness > 0.0f ? def->gustiness : WIND_DEF_GUSTINESS;
    stc_chaos = def->chaos > 0.0f ? def->chaos : WIND_DEF_CHAOS;

    stc_gust_cur = 0.0f;
    stc_gust_target = Weather_Randf2();
    stc_gust_timer = WIND_GUST_PERIOD;
    stc_head_cur = 0.0f;
    stc_head_target = Weather_Randf2() * WIND_HEAD_RANGE * stc_chaos;
    stc_head_timer = WIND_HEAD_PERIOD;
}

void Wind_GetVector(Vec3 *out)
{
    out->X = stc_vx;
    out->Y = 0.0f;
    out->Z = stc_vz;
}

float Wind_GetHeading(float *dx, float *dz)
{
    float speed = sqrtf(stc_vx * stc_vx + stc_vz * stc_vz);
    if (speed <= 1e-4f)
    {
        *dx = *dz = 0.0f;
        return 0.0f;
    }
    *dx = stc_vx / speed;
    *dz = stc_vz / speed;
    return speed;
}

// Blow falling items downwind until they travel with it, never past the wind's own
// speed. Resting items (is_airborne 0) and held ones (-1) are left alone.
static void Wind_ApplyToItems(void)
{
    float dx, dz;
    float speed = Wind_GetHeading(&dx, &dz);
    if (speed <= 0.0f)
        return;
    float push = speed * WIND_ITEM_FACTOR;

    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_ITEM]; g != NULL; g = g->next)
    {
        ItemData *id = (ItemData *)g->userdata;
        if (id->is_airborne != 1)
            continue;

        float room = speed - (id->vel.X * dx + id->vel.Z * dz);
        if (room <= 0.0f)
            continue;
        float a = (push < room) ? push : room;
        id->vel.X += dx * a;
        id->vel.Z += dz * a;
    }
}

// Push airborne machines (is_airborne == 1), scaled by Machine_GetStatRatio's glide
// ratio, which counts the Glide patches the machine has collected.
static void Wind_ApplyToMachines(void)
{
    for (int ply = 0; ply < PLY_NUM; ply++)
    {
        MachineData *md = Ply_GetMachineData(ply);
        if (md == NULL)
            continue;
        if (md->is_airborne != 1 || md->is_dead)
            continue;

        // The ratio is [-1,1]; lerp clamps a negative Glide stat to the base share
        // so it never pushes the machine upwind.
        float glide = Machine_GetStatRatio(md, MACHINESTAT_GLIDE);
        float scale = WIND_MACHINE_FACTOR * lerp(WIND_MACHINE_GLIDE_BASE, 1.0f, glide);
        md->velocity.X += stc_vx * scale;
        md->velocity.Z += stc_vz * scale;
    }
}

void Wind_Tick(void)
{
    if (!stc_active)
        return;

    if (--stc_gust_timer <= 0)
    {
        stc_gust_target = Weather_Randf2();
        stc_gust_timer = WIND_GUST_PERIOD;
    }
    stc_gust_cur = lerp(stc_gust_cur, stc_gust_target, WIND_GUST_LERP);

    // Ease the heading offset toward a fresh random target, bounded by chaos so
    // calm presets stay near their base direction.
    if (--stc_head_timer <= 0)
    {
        stc_head_target = Weather_Randf2() * WIND_HEAD_RANGE * stc_chaos;
        stc_head_timer = WIND_HEAD_PERIOD;
    }
    stc_head_cur = lerp(stc_head_cur, stc_head_target, WIND_HEAD_LERP);

    float speed = stc_base_speed * (1.0f + stc_gustiness * stc_gust_cur);
    if (speed < 0.0f)
        speed = 0.0f;

    float rad = MTXDegToRad(stc_base_heading + stc_head_cur);
    stc_vx = speed * sinf(rad);
    stc_vz = speed * cosf(rad);

    // Visual-only until the round timer starts. Riders are still boarding through the
    // intro and count as airborne, so a push there shoves them off the start line, and the
    // opening item drop would be blown across the city before anyone can play.
    if (Weather_RoundProgress() < 0.0f)
        return;

    if (wind_affect_items)
        Wind_ApplyToItems();
    if (wind_affect_machines)
        Wind_ApplyToMachines();
}

void Wind_Reset(void)
{
    stc_active = 0;
    stc_vx = stc_vz = 0.0f;
}

static void OnWindStrengthChange(int val)
{
    OSReport("[Wind] Strength %s\n", wind_strength_names[val]);
}

static void OnWindRandomizeChange(int val)
{
    OSReport("[Wind] Randomize direction %s\n", weather_onoff_names[val]);
}

static void OnWindMachinesChange(int val)
{
    OSReport("[Wind] Affect machines %s\n", weather_onoff_names[val]);
}

static void OnWindItemsChange(int val)
{
    OSReport("[Wind] Affect items %s\n", weather_onoff_names[val]);
}

MenuDesc wind_menu = {
    .option_num = 4,
    .options = {
        &(OptionDesc){
            .name = "Wind Strength",
            .description = "Master wind multiplier for every CT preset (Off disables wind entirely)",
            .kind = OPTKIND_VALUE,
            .val = &wind_strength_index,
            .value_num = GetElementsIn(wind_strength_factors),
            .value_names = wind_strength_names,
            .on_change = OnWindStrengthChange,
        },
        &(OptionDesc){
            .name = "Randomize Direction",
            .description = "Roll a random wind heading each round instead of the preset's authored direction",
            .kind = OPTKIND_VALUE,
            .val = &wind_randomize_dir,
            .value_num = WEATHER_ONOFF_NUM,
            .value_names = weather_onoff_names,
            .on_change = OnWindRandomizeChange,
        },
        &(OptionDesc){
            .name = "Affect Machines",
            .description = "Let wind push airborne machines, harder the more Glide patches they carry",
            .kind = OPTKIND_VALUE,
            .val = &wind_affect_machines,
            .value_num = WEATHER_ONOFF_NUM,
            .value_names = weather_onoff_names,
            .on_change = OnWindMachinesChange,
        },
        &(OptionDesc){
            .name = "Affect Items",
            .description = "Let wind blow falling items sideways",
            .kind = OPTKIND_VALUE,
            .val = &wind_affect_items,
            .value_num = WEATHER_ONOFF_NUM,
            .value_names = weather_onoff_names,
            .on_change = OnWindItemsChange,
        },
    },
};
