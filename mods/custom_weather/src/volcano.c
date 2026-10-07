// Volcanic eruptions for custom_weather: the City Trial volcano periodically fires
// volleys of themed copy-ability projectiles out of its crater on ballistic arcs.

#include <string.h>

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "obj.h"
#include "rider.h"
#include "weapon.h"
#include "inline.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

// Crater mouth in City Trial world space.
#define VOLC_MOUTH_X   -366.19f
#define VOLC_MOUTH_Y    114.97f
#define VOLC_MOUTH_Z   -575.42f
#define VOLC_MOUTH_JIT   18.0f   // per-shot scatter about the mouth

// Defaults applied when a preset leaves the matching VolcanoDef field 0.
#define VOLC_DEF_ERUPTIONS  3
#define VOLC_DEF_DURATION   270    // 4.5s at 60fps
#define VOLC_DEF_INTERVAL   14     // frames between volleys within an eruption
#define VOLC_DEF_BURST      3      // projectiles per volley
#define VOLC_DEF_POWER      1.0f
#define VOLC_DEF_SPREAD     0.65f

#define VOLC_MAX_ERUPTIONS  12     // schedule capacity
#define VOLC_MAX_BURST      8      // per-volley cap

// Ballistic launch. Range is roughly speed^2 / gravity, so the defaults carry a
// projectile most of the way across the play box.
#define VOLC_BASE_SPEED    5.5f
#define VOLC_SPEED_VAR     0.30f   // +/- fraction rolled per shot
#define VOLC_MAX_TILT     70.0f    // degrees off vertical at spread == 1
#define VOLC_MIN_TILT_F    0.40f   // shallowest tilt as a fraction of the rolled max
#define VOLC_GRAVITY       0.021875f // per-frame downward accel written by VolcanoGravity
#define VOLC_LIFETIME     1680     // frames; long enough to complete the arc

// Per-shot size roll. Drives both the model and the hitbox, so a big one is
// genuinely more dangerous.
#define VOLC_SCALE_MIN     0.5f
#define VOLC_SCALE_MAX     3.5f

// A volley picks uniformly within the theme's list. Every kind here survives an
// ownerless spawn, which is what limits the roster.
static const u8 theme_fire[]   = { WPKIND_FIRE_BULLET };
static const u8 theme_plasma[] = { WPKIND_PLASMA_A, WPKIND_PLASMA_B,
                                   WPKIND_PLASMA_SPREAD_MID, WPKIND_PLASMA_SPREAD_SIDE };
static const u8 theme_bomb[]   = { WPKIND_BOMB, WPKIND_SENSORBOMB };
static const u8 theme_star[]   = { WPKIND_SPITCHARGED };

typedef struct ThemeKinds
{
    const u8 *kinds;
    int       count;
} ThemeKinds;

#define THEME_ENTRY(arr) { arr, (int)GetElementsIn(arr) }

// Indexed by VolcanoTheme; VOLC_THEME_DEFAULT and VOLC_THEME_CHAOS are resolved
// before this table is read.
static const ThemeKinds theme_table[] = {
    THEME_ENTRY(theme_fire),    // VOLC_THEME_DEFAULT -> Fire
    THEME_ENTRY(theme_fire),
    THEME_ENTRY(theme_plasma),
    THEME_ENTRY(theme_bomb),
    THEME_ENTRY(theme_star),
};
#define THEME_TABLE_NUM (int)GetElementsIn(theme_table)

static int stc_active = 0;

// The live preset's VolcanoDef with the module defaults and menu overrides folded in,
// latched by Volcano_SetActive.
static int   stc_theme = VOLC_THEME_FIRE;
static int   stc_eruptions = VOLC_DEF_ERUPTIONS;
static int   stc_duration = VOLC_DEF_DURATION;
static int   stc_interval = VOLC_DEF_INTERVAL;
static int   stc_burst = VOLC_DEF_BURST;
static float stc_power = VOLC_DEF_POWER;
static float stc_spread = VOLC_DEF_SPREAD;

// Round schedule: normalized match progress at which each eruption starts.
static float stc_schedule[VOLC_MAX_ERUPTIONS];
static int   stc_planned = 0;
static int   stc_next = 0;        // next unfired entry in stc_schedule
static int   stc_frames_left = 0; // remaining frames of the eruption in progress
static int   stc_volley_cd = 0;

// Menu overrides. Index 0 is "Preset", the pass-through value, on every knob.
static int show_index = 0;

static const int count_values[] = {0, 1, 2, 3, 5, 8};
static char *count_names[] = {"Preset", "1", "2", "3", "5", "8"};
#define VOLC_COUNT_NUM (int)GetElementsIn(count_values)
static int count_index = 0;

static const float duration_factors[] = {1.0f, 0.5f, 1.0f, 1.8f, 3.0f};
static char *duration_names[] = {"Preset", "Brief", "Normal", "Long", "Sustained"};
#define VOLC_DURATION_NUM (int)GetElementsIn(duration_factors)
static int duration_index = 0;

// Scales the per-volley projectile count and tightens the gap between volleys.
static const float density_factors[] = {1.0f, 0.5f, 1.0f, 2.0f, 3.5f};
static char *density_names[] = {"Preset", "Sparse", "Normal", "Heavy", "Cataclysm"};
#define VOLC_DENSITY_NUM (int)GetElementsIn(density_factors)
static int density_index = 0;

static const float power_factors[] = {1.0f, 0.65f, 1.0f, 1.4f};
static char *power_names[] = {"Preset", "Weak", "Normal", "Strong"};
#define VOLC_POWER_NUM (int)GetElementsIn(power_factors)
static int power_index = 0;

// Index 0 is "Preset"; 1..5 line up 1:1 with VolcanoTheme.
static char *theme_names[] = {"Preset", "Fire", "Plasma", "Bombs", "Stars", "Chaos"};
#define VOLC_THEME_NUM (int)GetElementsIn(theme_names)
static int theme_index = 0;

// Runs from the projectile's own prio-0 proc, right after that proc zeroes the
// acceleration vector and before prio 4 integrates it into velocity.
static void VolcanoGravity(void *p)
{
    ((WeaponData *)p)->accel.Y = -VOLC_GRAVITY;
}

// Resolve the theme to a concrete kind, rerolling per projectile under Chaos.
// Returns -1 when the chosen kind is not loaded on this stage.
static int PickKind(void)
{
    int theme = stc_theme;
    if (theme == VOLC_THEME_CHAOS)
        theme = VOLC_THEME_FIRE + HSD_Randi(VOLC_THEME_CHAOS - VOLC_THEME_FIRE);
    if (theme < 0 || theme >= THEME_TABLE_NUM)
        theme = VOLC_THEME_FIRE;

    const ThemeKinds *t = &theme_table[theme];
    int kind = t->kinds[HSD_Randi(t->count)];
    if (wp_kind_data[kind] == NULL)
        return -1;
    return kind;
}

// Launch one projectile out of the crater along a random upward cone ray.
static void LaunchOne(void)
{
    int kind = PickKind();
    if (kind < 0)
        return;

    // FIRE_BULLET's init and post_init read rider fields through the owner GObj from
    // inside Weapon_Create; every other kind here tolerates a null owner.
    void *donor = NULL;
    if (kind == WPKIND_FIRE_BULLET)
    {
        donor = Weather_FindDonorRider();
        if (!donor)
            return;
    }

    float max_tilt = MTXDegToRad(VOLC_MAX_TILT * stc_spread);
    float tilt = max_tilt * Weather_RandRange(VOLC_MIN_TILT_F, 1.0f);
    float az = HSD_Randf() * 2.0f * M_PI;
    float st = sinf(tilt), ct = cosf(tilt);

    float sa = sinf(az), ca = cosf(az);

    Vec3 dir;
    dir.X = st * sa;
    dir.Y = ct;
    dir.Z = st * ca;

    // Weapon_Create crosses forward with up to build the orientation basis, and
    // a near-vertical launch makes world up parallel to forward. This is the unit
    // vector perpendicular to dir in the same vertical plane, so it never degenerates.
    Vec3 up;
    up.X = -ct * sa;
    up.Y = st;
    up.Z = -ct * ca;

    float speed = VOLC_BASE_SPEED * stc_power * (1.0f + VOLC_SPEED_VAR * Weather_Randf2());
    float scale = Weather_RandRange(VOLC_SCALE_MIN, VOLC_SCALE_MAX);

    Vec3 pos;
    pos.X = VOLC_MOUTH_X + Weather_Randf2() * VOLC_MOUTH_JIT;
    pos.Y = VOLC_MOUTH_Y;
    pos.Z = VOLC_MOUTH_Z + Weather_Randf2() * VOLC_MOUTH_JIT;

    Vec3 vel;
    vel.X = dir.X * speed;
    vel.Y = dir.Y * speed;
    vel.Z = dir.Z * speed;

    WeaponDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.kind = (WeaponKind)kind;
    desc.owner_gobj = donor;
    desc.owner_gobj2 = NULL;
    desc.pos = pos;
    desc.forward = dir;
    desc.up = up;
    desc.scale = scale;
    desc.vel = vel;
    desc.type_flag = 1;
    desc.charge = 1.0f;

    GOBJ *handle = Weapon_Create(&desc);
    if (!handle)
        return;
    WeaponData *proj = (WeaponData *)handle->userdata;
    if (!proj)
        return;

    // Ownerless from here on, including the rider FIRE_BULLET borrowed for create.
    // HitColl_CheckIfSamePlayer treats a NULL owner as "never the same player", so
    // an eruption is excluded from nobody and no damage lands on a player's tally.
    proj->owner_gobj = NULL;

    // Weapon_Create only snapshots desc.vel at proj+0x88, and the plasma
    // and sword-star post_inits then derive proj+0x94 from their own muzzle speed.
    // Overwriting it here is what makes the launch speed ours.
    proj->vel = vel;

    // Bomb and sensor bomb spawn holding on a rider hand that does not exist; their
    // state-0 slot would dereference the missing owner on the very next frame.
    if (kind == WPKIND_BOMB)
        Weapon_StateChange(proj, BOMB_STATE_THROWN, 1.0f, 1.0f, 1);
    else if (kind == WPKIND_SENSORBOMB)
        Weapon_StateChange(proj, SENSOR_BOMB_STATE_ARMED_FLYING, 1.0f, 1.0f, 1);
    // Single-state kinds are already in their one flying state after create.

    // kind_scratch word 0 is the normalized charge, word 1 the burst size. A borrowed
    // rider is never charged, so both would arrive 0 and the burst would land inert.
    if (kind == WPKIND_FIRE_BULLET)
    {
        float *charge = (float *)proj->kind_scratch;
        charge[0] = 1.0f;
        charge[1] = scale;
    }

    // The hook write must follow the transition, since Weapon_StateChange clears
    // the user-hook slots. The per-kind default lifetimes are unusable here: plasma
    // expires in 6 to 9 frames, and bomb and sensor bomb never expire at all.
    proj->lifetime = VOLC_LIFETIME;
    proj->framestart_callback = VolcanoGravity;
}

void Volcano_SetActive(const VolcanoDef *def)
{
    stc_active = WeatherToggle(show_index, def && def->enabled);

    int def_eruptions = (def && def->eruptions > 0) ? def->eruptions : VOLC_DEF_ERUPTIONS;
    stc_eruptions = (count_index > 0) ? count_values[count_index] : def_eruptions;
    if (stc_eruptions > VOLC_MAX_ERUPTIONS)
        stc_eruptions = VOLC_MAX_ERUPTIONS;

    int def_duration = (def && def->duration > 0) ? def->duration : VOLC_DEF_DURATION;
    stc_duration = (int)(def_duration * duration_factors[duration_index]);
    if (stc_duration < 1)
        stc_duration = 1;

    float density = density_factors[density_index];
    int def_burst = (def && def->burst > 0) ? def->burst : VOLC_DEF_BURST;
    int def_interval = (def && def->interval > 0) ? def->interval : VOLC_DEF_INTERVAL;
    stc_burst = (int)(def_burst * density + 0.5f);
    stc_interval = (int)(def_interval / density);
    if (stc_burst < 1)
        stc_burst = 1;
    if (stc_burst > VOLC_MAX_BURST)
        stc_burst = VOLC_MAX_BURST;
    if (stc_interval < 2)
        stc_interval = 2;

    float def_power = (def && def->power > 0.0f) ? def->power : VOLC_DEF_POWER;
    stc_power = power_factors[power_index] * def_power;
    stc_spread = (def && def->spread > 0.0f) ? def->spread : VOLC_DEF_SPREAD;
    if (theme_index > 0)
        stc_theme = theme_index;
    else
        stc_theme = (def && def->theme > 0) ? def->theme : VOLC_THEME_FIRE;

    // A new preset mid-round reschedules the eruptions it has left.
    stc_planned = 0;
    stc_frames_left = 0;
}

void Volcano_Tick(void)
{
    if (!stc_active)
        return;

    float p = Weather_RoundProgress();
    if (p < 0.0f)
        return;

    if (!stc_planned)
    {
        stc_next = Weather_SeedSchedule(stc_schedule, stc_eruptions, p);
        stc_planned = 1;
    }

    if (stc_frames_left > 0)
    {
        stc_frames_left--;
        if (--stc_volley_cd <= 0)
        {
            for (int i = 0; i < stc_burst; i++)
                LaunchOne();
            stc_volley_cd = stc_interval;
        }
        return;
    }

    if (stc_next < stc_eruptions && p >= stc_schedule[stc_next])
    {
        stc_next++;
        stc_frames_left = stc_duration;
        stc_volley_cd = 1;
        OSReport("[Volcano] Eruption %d/%d at progress %d%%, theme %s, %d frames\n",
                 stc_next, stc_eruptions, (int)(p * 100.0f),
                 theme_names[(stc_theme < VOLC_THEME_NUM) ? stc_theme : 0],
                 stc_duration);
    }
}

void Volcano_Reset(void)
{
    stc_active = 0;
    stc_planned = 0;
    stc_next = 0;
    stc_frames_left = 0;
    stc_volley_cd = 0;
}

static void OnVolcanoShowChange(int val)
{
    OSReport("[Volcano] Volcano %s\n", weather_toggle_names[val]);
}

static void OnVolcanoCountChange(int val)
{
    OSReport("[Volcano] Eruptions %s\n", count_names[val]);
}

static void OnVolcanoDurationChange(int val)
{
    OSReport("[Volcano] Duration %s\n", duration_names[val]);
}

static void OnVolcanoDensityChange(int val)
{
    OSReport("[Volcano] Intensity %s\n", density_names[val]);
}

static void OnVolcanoPowerChange(int val)
{
    OSReport("[Volcano] Power %s\n", power_names[val]);
}

static void OnVolcanoThemeChange(int val)
{
    OSReport("[Volcano] Projectiles %s\n", theme_names[val]);
}

MenuDesc volcano_menu = {
    .option_num = 6,
    .options = {
        &(OptionDesc){
            .name = "Volcano",
            .description = "Let a volcano erupt in City Trial: Preset = only presets that set it, Off = never, On = all presets",
            .kind = OPTKIND_VALUE,
            .val = &show_index,
            .value_num = 3,
            .value_names = weather_toggle_names,
            .on_change = OnVolcanoShowChange,
        },
        &(OptionDesc){
            .name = "Eruptions",
            .description = "How many times the volcano erupts over a City Trial round, spread across the match timer",
            .kind = OPTKIND_VALUE,
            .val = &count_index,
            .value_num = VOLC_COUNT_NUM,
            .value_names = count_names,
            .on_change = OnVolcanoCountChange,
        },
        &(OptionDesc){
            .name = "Duration",
            .description = "How long each eruption keeps firing before the volcano settles",
            .kind = OPTKIND_VALUE,
            .val = &duration_index,
            .value_num = VOLC_DURATION_NUM,
            .value_names = duration_names,
            .on_change = OnVolcanoDurationChange,
        },
        &(OptionDesc){
            .name = "Intensity",
            .description = "How many projectiles each eruption throws, and how fast the volleys come",
            .kind = OPTKIND_VALUE,
            .val = &density_index,
            .value_num = VOLC_DENSITY_NUM,
            .value_names = density_names,
            .on_change = OnVolcanoDensityChange,
        },
        &(OptionDesc){
            .name = "Power",
            .description = "How hard projectiles are thrown, which sets how far across the map they land",
            .kind = OPTKIND_VALUE,
            .val = &power_index,
            .value_num = VOLC_POWER_NUM,
            .value_names = power_names,
            .on_change = OnVolcanoPowerChange,
        },
        &(OptionDesc){
            .name = "Projectiles",
            .description = "What the volcano throws (Preset = each preset's own theme, Chaos = a fresh roll every shot)",
            .kind = OPTKIND_VALUE,
            .val = &theme_index,
            .value_num = VOLC_THEME_NUM,
            .value_names = theme_names,
            .on_change = OnVolcanoThemeChange,
        },
    },
};
