// Damaging hail for custom_weather: a per-machine cloud of falling stones that chip
// an exposed machine. Each cloud is a tight box centered on its machine.

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "obj.h"
#include "gx.h"
#include "machine.h"
#include "collision.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

#define HAIL_MAX_STONES     32       // per-machine pool; resolved count clamps to this
#define HAIL_BASE_STONES    20       // per-machine stones at "Normal"
#define HAIL_BOX_HALF       120.0f   // XZ half-extent of the cloud around the machine
#define HAIL_TOP            220.0f   // stones (re)spawn this far above the machine
#define HAIL_BELOW          90.0f    // recycle once a stone falls this far below it
#define HAIL_STRAY          300.0f   // recycle if a stone drifts this far from it horizontally
#define HAIL_HIT_RADIUS     20.0f    // body sphere a stone must enter to deal damage
#define HAIL_BODY_Y         10.0f    // lift the hit sphere off the machine origin toward the body
#define HAIL_FALL           32.0f    // downward speed, world units/frame (heavier than rain)
#define HAIL_HIT_COOLDOWN   10       // frames a machine is immune after a hail hit (rate cap)

// Shelter probe, throttled because cover changes slowly relative to the frame rate.
#define HAIL_SHELTER_INTERVAL  8        // frames between shelter re-checks per machine
#define HAIL_PROBE_LIFT        20.0f    // end the down-cast this far above the machine origin
#define HAIL_SKY_PROBE         3000.0f  // fallback cast height when the stage box is unavailable

// Appearance: a short, thick, icy-white chunk that reads as a particle, not a line.
#define HAIL_COLOR_R        230
#define HAIL_COLOR_G        240
#define HAIL_COLOR_B        255
#define HAIL_COLOR_A        220
#define HAIL_LINE_WIDTH     18       // 1/6-pixel units (~3px)
#define HAIL_STREAK         0.25f    // segment length = per-frame velocity * this

// Entity class / p_link high enough to avoid the engine's own.
#define HAIL_GOBJ_CLASS  204
#define HAIL_GOBJ_PLINK  28

// One world-space hailstone. Velocity is shared across all stones, so a stone is
// just its current position.
typedef struct HailStone
{
    Vec3 pos;
} HailStone;

// Per-machine cloud. `seeded` doubles as the active flag: a slot whose machine is
// gone or sheltered is cleared to 0 so it neither steps nor draws.
typedef struct HailCloud
{
    HailStone stones[HAIL_MAX_STONES];
    int       hit_cd;
    int       seeded;
    int       sheltered;
    int       shelter_cd;
} HailCloud;

// Cached only to avoid recreating the GObj every frame; never dereferenced.
static GOBJ *stc_hail_gobj = NULL;

static int stc_active = 0;
static int stc_stone_count = HAIL_BASE_STONES;  // active stones per cloud (menu-scaled)

static HailCloud stc_clouds[WEATHER_PLAYER_SLOTS];

// Shared per-frame velocity (fall + wind slant). vel_y is negative (downward).
static float stc_vel_x = 0.0f, stc_vel_y = -HAIL_FALL, stc_vel_z = 0.0f;

// The amount scales the cloud density (stones per machine), so it governs both the
// look and the chip-damage rate. Index 0 (Preset) resolves to stc_preset_amount,
// so its slot here is never read; the rest force an amount over every preset.
static const float hail_factors[] = {0.0f, 0.0f, 0.5f, 1.0f, 1.5f};
static char *hail_names[] = {"Preset", "Off", "Light", "Normal", "Heavy"};
#define HAIL_AMOUNT_NUM (sizeof(hail_factors) / sizeof(hail_factors[0]))
static int hail_index = 0;

// The active preset's hail amount, 0 = off.
static float stc_preset_amount = 0.0f;

// Place a stone at the top of the box over the machine, at a fresh random XZ.
static void RespawnStone(HailStone *s, const MachineData *md)
{
    s->pos.X = md->pos.X + Weather_Randf2() * HAIL_BOX_HALF;
    s->pos.Y = md->pos.Y + HAIL_TOP;
    s->pos.Z = md->pos.Z + Weather_Randf2() * HAIL_BOX_HALF;
}

// Fills the whole pool, not just stc_stone_count, so a later count increase is safe.
static void SeedCloud(HailCloud *c, const MachineData *md)
{
    for (int i = 0; i < HAIL_MAX_STONES; i++)
    {
        c->stones[i].pos.X = md->pos.X + Weather_Randf2() * HAIL_BOX_HALF;
        c->stones[i].pos.Y = md->pos.Y - HAIL_BELOW + HSD_Randf() * (HAIL_TOP + HAIL_BELOW);
        c->stones[i].pos.Z = md->pos.Z + Weather_Randf2() * HAIL_BOX_HALF;
    }
    c->hit_cd = 0;
    c->seeded = 1;
}

static void StepCloud(HailCloud *c, MachineData *md, GOBJ *mg)
{
    float r2 = HAIL_HIT_RADIUS * HAIL_HIT_RADIUS;
    float stray2 = HAIL_STRAY * HAIL_STRAY;

    for (int i = 0; i < stc_stone_count; i++)
    {
        HailStone *s = &c->stones[i];
        s->pos.X += stc_vel_x;
        s->pos.Y += stc_vel_y;
        s->pos.Z += stc_vel_z;

        float dx = s->pos.X - md->pos.X;
        float dy = s->pos.Y - (md->pos.Y + HAIL_BODY_Y);
        float dz = s->pos.Z - md->pos.Z;

        if (c->hit_cd == 0 && (dx * dx + dy * dy + dz * dz) <= r2)
        {
            // The machine GObj is the damage source (City Trial requires non-NULL).
            Machine_GiveDamage(md, 1.0f, mg);
            c->hit_cd = HAIL_HIT_COOLDOWN;
            RespawnStone(s, md);
            continue;
        }

        if (s->pos.Y < md->pos.Y - HAIL_BELOW || (dx * dx + dz * dz) > stray2)
            RespawnStone(s, md);
    }

    if (c->hit_cd > 0)
        c->hit_cd--;
}

// GX callback on the world camera link, XLU pass. Stones draw as short thick
// segments.
static void Hail_GX(GOBJ *g, int pass)
{
    (void)g;
    if (pass != 1)
        return;
    if (!stc_active || stc_stone_count <= 0)
        return;

    COBJ *cam = COBJ_GetCurrent();
    if (!cam)
        return;

    float sx = stc_vel_x * HAIL_STREAK;
    float sy = stc_vel_y * HAIL_STREAK;
    float sz = stc_vel_z * HAIL_STREAK;

    WeatherGX_BeginXlu(cam, 0, HAIL_LINE_WIDTH);

    for (int slot = 0; slot < WEATHER_PLAYER_SLOTS; slot++)
    {
        HailCloud *c = &stc_clouds[slot];
        if (!c->seeded)
            continue;

        GXBegin(GX_LINES, GX_VTXFMT0, stc_stone_count * 2);
        for (int i = 0; i < stc_stone_count; i++)
        {
            float wx = c->stones[i].pos.X;
            float wy = c->stones[i].pos.Y;
            float wz = c->stones[i].pos.Z;
            GXPosition3f32(wx, wy, wz);
            GXColor4u8(HAIL_COLOR_R, HAIL_COLOR_G, HAIL_COLOR_B, HAIL_COLOR_A);
            GXPosition3f32(wx + sx, wy + sy, wz + sz);
            GXColor4u8(HAIL_COLOR_R, HAIL_COLOR_G, HAIL_COLOR_B, HAIL_COLOR_A);
        }
    }

    HSD_StateInvalidate(-1);
}

static void Hail_Ensure(void)
{
    if (stc_hail_gobj)
        return;
    stc_hail_gobj = WeatherGX_EnsureLayer(HAIL_GOBJ_CLASS, HAIL_GOBJ_PLINK, Hail_GX, "Hail");
}

// Whether the machine has stage geometry overhead. Casting down from the top of the
// playable volume detects a roof by its walkable top face, so it works regardless of
// how the collision triangles are sided.
static int Machine_IsSheltered(const MachineData *md)
{
    StageNode *sn = Weather_StageNode();
    float sky_y = sn ? (sn->oob_max.Y + 50.0f) : (md->pos.Y + HAIL_SKY_PROBE);

    float floor_y = md->pos.Y + HAIL_PROBE_LIFT;
    if (sky_y <= floor_y)
        return 0; // machine is at/above the top of the volume - nothing overhead

    Vec3 start = {md->pos.X, sky_y, md->pos.Z};
    Vec3 end = {md->pos.X, floor_y, md->pos.Z};
    Vec3 hit;
    return Raycast_Ground(&start, &end, &hit) >= 0;
}

// Latches the amount the menu's Preset index resolves to. A preset with no hail is
// still overridable by a forced menu amount, so this runs either way.
void Hail_SetActive(const HailDef *def)
{
    stc_preset_amount = (def && def->enabled) ? (def->amount > 0.0f ? def->amount : 1.0f)
                                              : 0.0f;
}

void Hail_Tick(void)
{
    // Riders are still boarding through the intro, so nothing chips them there.
    float f = (hail_index == 0) ? stc_preset_amount : hail_factors[hail_index];
    if (!Rain_IsActive() || f <= 0.0f || Weather_RoundProgress() < 0.0f)
    {
        // Drop every cloud so re-enabling re-seeds over current machine positions.
        if (stc_active)
        {
            for (int slot = 0; slot < WEATHER_PLAYER_SLOTS; slot++)
                stc_clouds[slot].seeded = 0;
            stc_active = 0;
        }
        return;
    }
    stc_active = 1;

    int n = (int)(HAIL_BASE_STONES * f + 0.5f);
    if (n > HAIL_MAX_STONES)
        n = HAIL_MAX_STONES;
    if (n < 1)
        n = 1;
    stc_stone_count = n;

    Hail_Ensure();

    // Stones fall straight down plus the global wind slant, read fresh each frame
    // so gusts visibly carry the hail.
    Vec3 wind;
    Wind_GetVector(&wind);
    stc_vel_x = wind.X;
    stc_vel_y = -HAIL_FALL;
    stc_vel_z = wind.Z;

    for (int slot = 0; slot < WEATHER_PLAYER_SLOTS; slot++)
    {
        HailCloud *c = &stc_clouds[slot];

        GOBJ *mg = Ply_GetMachineGObj(slot);
        if (mg == NULL)
        {
            c->seeded = 0;
            continue;
        }
        MachineData *md = (MachineData *)mg->userdata;
        if (md == NULL || Machine_IsDead(md))
        {
            c->seeded = 0;
            continue;
        }

        // A machine under cover takes no hail; drop its cloud so re-emerging
        // re-seeds over open ground.
        if (--c->shelter_cd <= 0)
        {
            c->sheltered = Machine_IsSheltered(md);
            c->shelter_cd = HAIL_SHELTER_INTERVAL;
        }
        if (c->sheltered)
        {
            c->seeded = 0;
            continue;
        }

        if (!c->seeded)
            SeedCloud(c, md);
        StepCloud(c, md, mg);
    }
}

void Hail_Reset(void)
{
    stc_hail_gobj = NULL;
    stc_active = 0;
    stc_preset_amount = 0.0f;
    for (int slot = 0; slot < WEATHER_PLAYER_SLOTS; slot++)
    {
        stc_clouds[slot].seeded = 0;
        stc_clouds[slot].hit_cd = 0;
        stc_clouds[slot].sheltered = 0;
        stc_clouds[slot].shelter_cd = 0;
    }
}

static void OnHailChange(int val)
{
    OSReport("[Hail] Amount %s\n", hail_names[val]);
}

OptionDesc hail_option = {
    .name = "Hail",
    .description = "Icy hail in the rain; a stone hitting an exposed machine does 1 damage (Preset = per-preset amount)",
    .kind = OPTKIND_VALUE,
    .val = &hail_index,
    .value_num = HAIL_AMOUNT_NUM,
    .value_names = hail_names,
    .on_change = OnHailChange,
};
