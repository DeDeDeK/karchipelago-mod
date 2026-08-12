// Volcanic eruptions for custom_weather: the City Trial volcano periodically fires
// volleys of themed copy-ability projectiles out of its crater on ballistic arcs.

#include <string.h>

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "obj.h"
#include "rider.h"
#include "projectile.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

#define VOLC_PI      3.14159265358979f
#define VOLC_DEG2RAD (VOLC_PI / 180.0f)

// Crater mouth in City Trial world space, surveyed at the rim. The play box is
// X/Z +/-1300, Y -300..1500, so the crater sits left-and-back of the map center.
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
// projectile most of the way across the play box. Speed and gravity are tied: to
// change how fast the arc plays out without moving where shots land, scale gravity
// by the square of the speed change.
#define VOLC_BASE_SPEED    5.5f
#define VOLC_SPEED_VAR     0.30f   // +/- fraction rolled per shot
#define VOLC_MAX_TILT     70.0f    // degrees off vertical at spread == 1
#define VOLC_MIN_TILT_F    0.40f   // shallowest tilt as a fraction of the rolled max
#define VOLC_GRAVITY       0.021875f // per-frame downward accel written by VolcanoGravity
#define VOLC_LIFETIME     1680     // frames; long enough to complete the arc

// Per-shot size roll, uniform over the range. Drives both the model and the hitbox,
// so a big one is genuinely more dangerous. 0.0 would mean no hitbox at all.
#define VOLC_SCALE_MIN     0.5f
#define VOLC_SCALE_MAX     3.5f

// Per-kind data lives at 0x8055a9a8[kind], registered in one pass when the first
// rider is created from the ability archive. Between scene load (the table is
// zeroed) and that point every slot is NULL, and Projectile_Create dereferences the
// slot without checking.
static void **stc_proj_kind_data = (void **)0x8055a9a8;

// Projectile kinds per theme. A volley picks uniformly within the theme's list.
// Every kind here spawns and flies with no owner rider; see VolcanoTheme for the
// ones that cannot.
static const u8 theme_fire[]   = { PROJKIND_FIRE_BULLET };
static const u8 theme_plasma[] = { PROJKIND_PLASMA_A, PROJKIND_PLASMA_B,
                                   PROJKIND_PLASMA_SPREAD_MID, PROJKIND_PLASMA_SPREAD_SIDE };
static const u8 theme_bomb[]   = { PROJKIND_BOMB, PROJKIND_SENSORBOMB };
static const u8 theme_star[]   = { PROJKIND_SWORD_STAR_CHARGED };

typedef struct ThemeKinds
{
    const u8 *kinds;
    int       count;
} ThemeKinds;

#define THEME_ENTRY(arr) { arr, (int)(sizeof(arr) / sizeof((arr)[0])) }

// Indexed by VolcanoTheme; VOLC_THEME_DEFAULT and VOLC_THEME_CHAOS are resolved
// before this table is read.
static const ThemeKinds theme_table[] = {
    THEME_ENTRY(theme_fire),    // VOLC_THEME_DEFAULT -> Fire
    THEME_ENTRY(theme_fire),
    THEME_ENTRY(theme_plasma),
    THEME_ENTRY(theme_bomb),
    THEME_ENTRY(theme_star),
};
#define THEME_TABLE_NUM (int)(sizeof(theme_table) / sizeof(theme_table[0]))

static int stc_active = 0;

// The live preset's config (VolcanoDef with the module defaults filled in). Latched
// by Volcano_SetActive and never written elsewhere, so a menu knob returned to
// "Preset" resolves back to it.
static int   stc_def_theme = VOLC_THEME_FIRE;
static int   stc_def_eruptions = VOLC_DEF_ERUPTIONS;
static int   stc_def_duration = VOLC_DEF_DURATION;
static int   stc_def_interval = VOLC_DEF_INTERVAL;
static int   stc_def_burst = VOLC_DEF_BURST;
static float stc_def_power = VOLC_DEF_POWER;
static float stc_def_spread = VOLC_DEF_SPREAD;

// Effective config for this frame: the preset values with the menu overrides folded
// in. Recomputed every frame so a live menu change lands immediately.
static int   stc_theme = VOLC_THEME_FIRE;
static int   stc_eruptions = VOLC_DEF_ERUPTIONS;
static int   stc_duration = VOLC_DEF_DURATION;
static int   stc_interval = VOLC_DEF_INTERVAL;
static int   stc_burst = VOLC_DEF_BURST;
static float stc_power = VOLC_DEF_POWER;
static float stc_spread = VOLC_DEF_SPREAD;

// Round schedule: normalized match progress at which each eruption starts.
static float stc_schedule[VOLC_MAX_ERUPTIONS];
static int   stc_scheduled = 0;   // eruption count the schedule was planned for; 0 = unplanned
static int   stc_next = 0;        // next unfired entry in stc_schedule
static int   stc_frames_left = 0; // remaining frames of the eruption in progress
static int   stc_volley_cd = 0;

// Menu overrides. Index 0 is "Preset" on every knob.
static char *toggle_names[] = {"Preset", "Off", "On"};
static int show_index = 0;

static const int count_values[] = {0, 0, 1, 2, 3, 5, 8};
static char *count_names[] = {"Preset", "Off", "1", "2", "3", "5", "8"};
#define VOLC_COUNT_NUM (int)(sizeof(count_values) / sizeof(count_values[0]))
static int count_index = 0;

static const float duration_factors[] = {0.0f, 0.5f, 1.0f, 1.8f, 3.0f};
static char *duration_names[] = {"Preset", "Brief", "Normal", "Long", "Sustained"};
#define VOLC_DURATION_NUM (int)(sizeof(duration_factors) / sizeof(duration_factors[0]))
static int duration_index = 0;

// Scales the per-volley projectile count and tightens the gap between volleys.
static const float density_factors[] = {0.0f, 0.5f, 1.0f, 2.0f, 3.5f};
static char *density_names[] = {"Preset", "Sparse", "Normal", "Heavy", "Cataclysm"};
#define VOLC_DENSITY_NUM (int)(sizeof(density_factors) / sizeof(density_factors[0]))
static int density_index = 0;

static const float power_factors[] = {0.0f, 0.65f, 1.0f, 1.4f};
static char *power_names[] = {"Preset", "Weak", "Normal", "Strong"};
#define VOLC_POWER_NUM (int)(sizeof(power_factors) / sizeof(power_factors[0]))
static int power_index = 0;

// Index 0 is "Preset"; 1..5 line up 1:1 with VolcanoTheme.
static char *theme_names[] = {"Preset", "Fire", "Plasma", "Bombs", "Stars", "Chaos"};
#define VOLC_THEME_NUM (int)(sizeof(theme_names) / sizeof(theme_names[0]))
static int theme_index = 0;

// FIRE_BULLET's init and post_init read rider fields through the owner GObj from
// inside Projectile_Create, so it is the one kind that cannot be handed a null
// owner. Any live rider satisfies them - the two floats cached from it are per-kind
// scratch - and the owner is dropped the moment create returns.
static int NeedsOwner(int kind)
{
    return kind == PROJKIND_FIRE_BULLET;
}

static void *FindDonorRider(void)
{
    for (int i = 0; i < 4; i++)
    {
        GOBJ *rg = stc_playerdata[i].rider_gobj;
        if (stc_playerdata[i].player_kind == PKIND_NONE || !rg || !rg->userdata)
            continue;
        return rg;
    }
    return NULL;
}

// Runs from the projectile's own prio-0 proc, right after that proc zeroes the
// acceleration vector and before prio 4 integrates it into velocity. Every theme
// gets it: the plasma and star kinds have an all-`blr` pre-physics slot and would
// otherwise fly dead straight, and the bomb kinds only ever add the stage air
// current to accel, so this value survives on them too.
static void VolcanoGravity(void *p)
{
    ((ProjectileData *)p)->accel.Y = -VOLC_GRAVITY;
}

// Spread the round's eruptions over the match, one per equal slice with jitter
// inside the slice so they never land on the same beat twice. Entries already
// behind `p` are skipped so re-planning mid-round does not replay them.
static void SeedSchedule(int n, float p)
{
    for (int i = 0; i < n; i++)
        stc_schedule[i] = ((float)i + 0.15f + 0.70f * HSD_Randf()) / (float)n;
    stc_next = 0;
    while (stc_next < n && stc_schedule[stc_next] <= p)
        stc_next++;
    stc_scheduled = n;
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
    if (stc_proj_kind_data[kind] == NULL)
        return -1;
    return kind;
}

// Launch one projectile out of the crater along a random upward cone ray.
static void LaunchOne(void)
{
    int kind = PickKind();
    if (kind < 0)
        return;

    void *donor = NULL;
    if (NeedsOwner(kind))
    {
        donor = FindDonorRider();
        if (!donor)
            return;
    }

    float max_tilt = VOLC_MAX_TILT * stc_spread * VOLC_DEG2RAD;
    float tilt = max_tilt * (VOLC_MIN_TILT_F + (1.0f - VOLC_MIN_TILT_F) * HSD_Randf());
    float az = HSD_Randf() * 2.0f * VOLC_PI;
    float st = sinf(tilt), ct = cosf(tilt);

    float sa = sinf(az), ca = cosf(az);

    Vec3 dir;
    dir.X = st * sa;
    dir.Y = ct;
    dir.Z = st * ca;

    // Projectile_Create crosses forward with up to build the orientation basis, and
    // a near-vertical launch makes world up parallel to forward. This is the unit
    // vector perpendicular to dir in the same vertical plane, so it never degenerates.
    Vec3 up;
    up.X = -ct * sa;
    up.Y = st;
    up.Z = -ct * ca;

    float speed = VOLC_BASE_SPEED * stc_power * (1.0f + VOLC_SPEED_VAR * Weather_Randf2());
    float scale = VOLC_SCALE_MIN + (VOLC_SCALE_MAX - VOLC_SCALE_MIN) * HSD_Randf();

    Vec3 pos;
    pos.X = VOLC_MOUTH_X + Weather_Randf2() * VOLC_MOUTH_JIT;
    pos.Y = VOLC_MOUTH_Y;
    pos.Z = VOLC_MOUTH_Z + Weather_Randf2() * VOLC_MOUTH_JIT;

    Vec3 vel;
    vel.X = dir.X * speed;
    vel.Y = dir.Y * speed;
    vel.Z = dir.Z * speed;

    ProjectileDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.kind = (ProjectileKind)kind;
    desc.owner_gobj = donor;
    desc.owner_unk2 = 0;
    desc.position = pos;
    desc.forward = dir;
    desc.up = up;
    desc.velocity_scale = scale;   // a size scale despite the name
    desc.velocity = vel;
    desc.type_flag = 1;
    desc.charge = 1.0f;

    GOBJ *handle = Projectile_Create(&desc);
    if (!handle)
        return;
    ProjectileData *proj = (ProjectileData *)handle->userdata;
    if (!proj)
        return;

    // Ownerless from here on, including the rider FIRE_BULLET borrowed for create.
    // HitColl_CheckIfSamePlayer treats a NULL owner as "never the same player", so
    // an eruption is excluded from nobody and no damage lands on a player's tally.
    proj->owner_gobj = NULL;

    // Projectile_Create only snapshots desc.velocity at proj+0x88, and the plasma
    // and sword-star post_inits then derive proj+0x94 from their own muzzle speed.
    // Overwriting it here is what makes the launch speed ours.
    proj->velocity = vel;

    // Bomb and sensor bomb spawn holding on a rider hand that does not exist; their
    // state-0 slot would dereference the missing owner on the very next frame.
    if (kind == PROJKIND_BOMB)
        Projectile_SetState(proj, BOMB_STATE_THROWN, 1.0f, 1.0f, 1);
    else if (kind == PROJKIND_SENSORBOMB)
        Projectile_SetState(proj, SENSOR_BOMB_STATE_ARMED_FLYING, 1.0f, 1.0f, 1);
    // Single-state kinds are already in their one flying state after create.

    // FIRE_BULLET's init caches the owner's Fire-ability charge in kind scratch: word 0
    // normalized, word 1 raw. On impact it multiplies its hitbox radius by the first and
    // assigns the second to cur_scale. A borrowed rider is never charged, so both arrive
    // 0 and the burst lands inert, invisible, and noisy (the effect system warns on the
    // zero scale). Word 0 gets a full charge, whose vanilla ceiling is 1.0; word 1 gets
    // the rolled size, so the burst stays as big as the shot that made it.
    if (kind == PROJKIND_FIRE_BULLET)
    {
        float *charge = (float *)proj->kind_scratch;
        charge[0] = 1.0f;
        charge[1] = scale;
    }

    // The hook write must follow the transition, since Projectile_SetState clears
    // the user-hook slots. The per-kind default lifetimes are unusable here: plasma
    // expires in 6 to 9 frames, and bomb and sensor bomb never expire at all (0).
    proj->lifetime = VOLC_LIFETIME;
    proj->user_hook_0 = VolcanoGravity;
}

// Fold the menu overrides over the latched preset config into the effective values
// for this frame. Returns 0 when the volcano is dormant.
static int ResolveConfig(void)
{
    if (!WeatherToggle(show_index, stc_active))
        return 0;

    stc_eruptions = (count_index > 0) ? count_values[count_index] : stc_def_eruptions;
    if (stc_eruptions <= 0)
        return 0;
    if (stc_eruptions > VOLC_MAX_ERUPTIONS)
        stc_eruptions = VOLC_MAX_ERUPTIONS;

    stc_duration = (duration_index > 0)
                       ? (int)(stc_def_duration * duration_factors[duration_index])
                       : stc_def_duration;
    if (stc_duration < 1)
        stc_duration = 1;

    float density = (density_index > 0) ? density_factors[density_index] : 1.0f;
    stc_burst = (int)(stc_def_burst * density + 0.5f);
    stc_interval = (int)(stc_def_interval / density);
    if (stc_burst < 1)
        stc_burst = 1;
    if (stc_burst > VOLC_MAX_BURST)
        stc_burst = VOLC_MAX_BURST;
    if (stc_interval < 2)
        stc_interval = 2;

    stc_power = (power_index > 0) ? power_factors[power_index] : stc_def_power;
    stc_spread = stc_def_spread;
    stc_theme = (theme_index > 0) ? theme_index : stc_def_theme;

    return 1;
}

// Latch the active preset's volcano config, applying the module defaults for any
// field the preset left at 0. The menu can still force it on over a preset that
// leaves it off, so the resolved values are kept either way.
void Volcano_SetActive(const VolcanoDef *def)
{
    stc_active = (def && def->enabled) ? 1 : 0;

    stc_def_theme     = (def && def->theme > 0) ? def->theme : VOLC_THEME_FIRE;
    stc_def_eruptions = (def && def->eruptions > 0) ? def->eruptions : VOLC_DEF_ERUPTIONS;
    stc_def_duration  = (def && def->duration > 0) ? def->duration : VOLC_DEF_DURATION;
    stc_def_interval  = (def && def->interval > 0) ? def->interval : VOLC_DEF_INTERVAL;
    stc_def_burst     = (def && def->burst > 0) ? def->burst : VOLC_DEF_BURST;
    stc_def_power     = (def && def->power > 0.0f) ? def->power : VOLC_DEF_POWER;
    stc_def_spread    = (def && def->spread > 0.0f) ? def->spread : VOLC_DEF_SPREAD;

    // A new preset mid-round reschedules the eruptions it has left.
    stc_scheduled = 0;
    stc_frames_left = 0;
}

void Volcano_Tick(void)
{
    // Config is resolved before the active test so a forced-On menu value can wake
    // a preset that ships the volcano dormant.
    if (!ResolveConfig())
    {
        stc_frames_left = 0;
        return;
    }

    float p = Weather_RoundProgress();
    if (p < 0.0f)
        return;

    // A changed eruption count re-plans the round from the current progress.
    if (stc_scheduled != stc_eruptions)
        SeedSchedule(stc_eruptions, p);

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
    stc_scheduled = 0;
    stc_next = 0;
    stc_frames_left = 0;
    stc_volley_cd = 0;
}

MenuDesc volcano_menu = {
    .option_num = 6,
    .options = {
        &(OptionDesc){
            .name = "Volcano",
            .description = "Let the City Trial volcano erupt: Preset = only presets that set it, Off = never, On = every CT preset",
            .kind = OPTKIND_VALUE,
            .val = &show_index,
            .value_num = 3,
            .value_names = toggle_names,
        },
        &(OptionDesc){
            .name = "Eruptions",
            .description = "How many times the volcano erupts over a City Trial round, spread across the match timer",
            .kind = OPTKIND_VALUE,
            .val = &count_index,
            .value_num = VOLC_COUNT_NUM,
            .value_names = count_names,
        },
        &(OptionDesc){
            .name = "Duration",
            .description = "How long each eruption keeps firing before the volcano settles",
            .kind = OPTKIND_VALUE,
            .val = &duration_index,
            .value_num = VOLC_DURATION_NUM,
            .value_names = duration_names,
        },
        &(OptionDesc){
            .name = "Intensity",
            .description = "How many projectiles each eruption throws, and how fast the volleys come",
            .kind = OPTKIND_VALUE,
            .val = &density_index,
            .value_num = VOLC_DENSITY_NUM,
            .value_names = density_names,
        },
        &(OptionDesc){
            .name = "Power",
            .description = "How hard projectiles are thrown, which sets how far across the map they land",
            .kind = OPTKIND_VALUE,
            .val = &power_index,
            .value_num = VOLC_POWER_NUM,
            .value_names = power_names,
        },
        &(OptionDesc){
            .name = "Projectiles",
            .description = "What the volcano throws (Preset = each preset's own theme, Chaos = a fresh roll every shot)",
            .kind = OPTKIND_VALUE,
            .val = &theme_index,
            .value_num = VOLC_THEME_NUM,
            .value_names = theme_names,
        },
    },
};
