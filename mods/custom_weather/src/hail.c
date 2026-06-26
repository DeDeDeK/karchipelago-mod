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

// The box is a tight volume centered on the machine: stones spawn HAIL_TOP above
// it, fall through, and recycle once they pass HAIL_BELOW under it (or stray more
// than HAIL_STRAY away horizontally, e.g. when the machine warps off). The hit
// sphere is centered slightly above the machine origin so stones strike the body
// rather than grazing its base.
#define HAIL_PLAYER_SLOTS   5
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

// Shelter probe. A machine with anything overhead takes no hail: cast straight
// down from the top of the playable volume to just above the machine, and any
// hit means a roof/overpass/bridge is covering it. Throttled - cover changes
// slowly relative to the frame rate.
#define HAIL_SHELTER_INTERVAL  8        // frames between shelter re-checks per machine
#define HAIL_PROBE_LIFT        20.0f    // end the down-cast this far above the machine origin
#define HAIL_SKY_MARGIN        50.0f    // start the cast this far above the stage's OoB top
#define HAIL_SKY_PROBE         3000.0f  // fallback cast height when the stage box is unavailable

// Appearance: a short, thick, icy-white chunk. Shorter streak than rain so it
// reads as a particle rather than a line; wider GX line so it looks heavy.
#define HAIL_COLOR_R        230
#define HAIL_COLOR_G        240
#define HAIL_COLOR_B        255
#define HAIL_COLOR_A        220
#define HAIL_LINE_WIDTH     18       // 1/6-pixel units (~3px)
#define HAIL_STREAK         0.25f    // segment length = per-frame velocity * this

// Render GObj: arbitrary high entity class (avoids real engine classes), a spare
// p_link past rain (201/25), lightning (202/26) and puddle (203/27), and the
// world camera's gx_link 0 on the XLU sub-pass.
#define HAIL_GOBJ_CLASS  204
#define HAIL_GOBJ_PLINK  28
#define HAIL_GX_LINK     0
#define HAIL_GX_PRI      0

// One world-space hailstone. Velocity is shared across all stones (computed per
// frame in Hail_Tick), so a stone is just its current position.
typedef struct HailStone
{
    Vec3 pos;
} HailStone;

// Per-machine cloud. `seeded` doubles as the active flag: a slot with no live
// machine - or a sheltered one - is cleared to 0 so it neither steps nor draws
// until its machine returns to the open. `hit_cd` is the post-hit damage cooldown
// for rate capping; `sheltered`/`shelter_cd` cache the throttled cover probe.
typedef struct HailCloud
{
    HailStone stones[HAIL_MAX_STONES];
    int       hit_cd;
    int       seeded;
    int       sheltered;
    int       shelter_cd;
} HailCloud;

// Cached only to avoid recreating the GObj every frame; never dereferenced (the
// engine owns it and frees it on scene teardown).
static GOBJ *stc_hail_gobj = NULL;

static int stc_active = 0;
static int stc_stone_count = HAIL_BASE_STONES;  // active stones per cloud (menu-scaled)

static HailCloud stc_clouds[HAIL_PLAYER_SLOTS];

// Shared per-frame velocity (fall + wind slant). vel_y is negative (downward).
static float stc_vel_x = 0.0f, stc_vel_y = -HAIL_FALL, stc_vel_z = 0.0f;

// "Hail" scales the cloud density (stones per machine); denser hail lands more
// often, so the amount governs both the look and the chip-damage rate (the
// per-machine cooldown caps the worst case). Off disables hail entirely. The
// option lives under the Rain submenu (rain.c references hail_option) since hail
// only falls when the active preset's rain is on.
static const float hail_factors[] = {0.0f, 0.5f, 1.0f, 1.5f};
static char *hail_names[] = {"Off", "Light", "Normal", "Heavy"};
#define HAIL_AMOUNT_NUM (sizeof(hail_factors) / sizeof(hail_factors[0]))
static int hail_index = 0; // default Off: hail chips machine HP, so it is opt-in

// Symmetric random offset in [-half, half].
static float RandSym(float half)
{
    return (HSD_Randf() * 2.0f - 1.0f) * half;
}

// Place a stone at the top of the box over the machine's current position, at a
// fresh random XZ within the box footprint.
static void RespawnStone(HailStone *s, const MachineData *md)
{
    s->pos.X = md->pos.X + RandSym(HAIL_BOX_HALF);
    s->pos.Y = md->pos.Y + HAIL_TOP;
    s->pos.Z = md->pos.Z + RandSym(HAIL_BOX_HALF);
}

// Fill the whole pool, scattering stones through the full height of the box so
// the cloud reads as full immediately rather than raining in from the top edge.
// Only the first stc_stone_count are stepped/drawn, but seeding all of them keeps
// a later count increase (live menu change) from exposing uninitialized stones.
static void SeedCloud(HailCloud *c, const MachineData *md)
{
    for (int i = 0; i < HAIL_MAX_STONES; i++)
    {
        c->stones[i].pos.X = md->pos.X + RandSym(HAIL_BOX_HALF);
        c->stones[i].pos.Y = md->pos.Y - HAIL_BELOW + HSD_Randf() * (HAIL_TOP + HAIL_BELOW);
        c->stones[i].pos.Z = md->pos.Z + RandSym(HAIL_BOX_HALF);
    }
    c->hit_cd = 0;
    c->seeded = 1;
}

// Advance one cloud: fall every stone by the shared velocity, deal 1 damage on
// the first stone to enter the machine's body sphere (then arm the cooldown), and
// recycle any stone that falls through or strays.
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
            // Honest contact: this visible stone struck the machine body. The
            // machine GObj is the damage source (non-NULL as City Trial requires).
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

// GX callback on the world camera link. Draws every live cloud's stones as short
// thick icy segments on the XLU pass (pass 1), depth-tested but not depth-writing
// so opaque geometry occludes hail behind it. Mirrors rain.c's GX setup; stones
// are world-space, so the current camera's view matrix transforms them per
// viewport.
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

    HSD_StateInitDirect(GX_VTXFMT0, 2);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetNumTexGens(0);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0, GX_DISABLE, Vertex, Vertex, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetChanCtrl(GX_ALPHA0, GX_DISABLE, Vertex, Vertex, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetZMode(GX_ENABLE, GX_LEQUAL, GX_DISABLE);
    GXSetCullMode(GX_CULL_NONE);
    GXSetLineWidth(HAIL_LINE_WIDTH, 5);
    GXLoadPosMtxImm(&cam->view_mtx, GX_PNMTX0);

    for (int slot = 0; slot < HAIL_PLAYER_SLOTS; slot++)
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
    GOBJ *g = GObj_Create(HAIL_GOBJ_CLASS, HAIL_GOBJ_PLINK, 0);
    if (!g)
        return;
    GObj_AddGXLink(g, Hail_GX, HAIL_GX_LINK, HAIL_GX_PRI);
    stc_hail_gobj = g;
    OSReport("[Hail] Damaging hail layer installed\n");
}

// Whether the machine has stage geometry overhead (a roof / overpass / bridge),
// in which case hail neither falls on it nor damages it. Cast straight down from
// the top of the playable volume to just above the machine: the first surface the
// ray meets is the lowest thing directly overhead, and any hit at all means the
// machine is covered. The down-cast detects a roof by its walkable top face, so
// it works regardless of how the collision triangles are sided.
static int MachineSheltered(const MachineData *md)
{
    float sky_y;
    GrObj *gr = *stc_grobj;
    if (gr && gr->gr_data && gr->gr_data->stage_node)
        sky_y = gr->gr_data->stage_node->oob_max.Y + HAIL_SKY_MARGIN;
    else
        sky_y = md->pos.Y + HAIL_SKY_PROBE;

    float floor_y = md->pos.Y + HAIL_PROBE_LIFT;
    if (sky_y <= floor_y)
        return 0; // machine is at/above the top of the volume - nothing overhead

    Vec3 start = {md->pos.X, sky_y, md->pos.Z};
    Vec3 end = {md->pos.X, floor_y, md->pos.Z};
    Vec3 hit;
    return EnvColl_Raycast(&start, &end, &hit) >= 0;
}

void Hail_Tick(void)
{
    // Hail only falls when the current preset's rain is active (rain.c owns that
    // decision, including the master Rain Intensity = Off floor) and the Hail
    // menu is on. Both are read live so the knob takes effect immediately.
    float f = hail_factors[hail_index];
    if (!Rain_IsActive() || f <= 0.0f)
    {
        // Going inactive drops every cloud so re-enabling re-seeds fresh ones
        // over the machines' current positions rather than resuming stale boxes.
        if (stc_active)
        {
            for (int slot = 0; slot < HAIL_PLAYER_SLOTS; slot++)
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
    // so gusts visibly carry the hail (the same vector that slants the rain).
    Vec3 wind;
    Wind_GetVector(&wind);
    stc_vel_x = wind.X;
    stc_vel_y = -HAIL_FALL;
    stc_vel_z = wind.Z;

    for (int slot = 0; slot < HAIL_PLAYER_SLOTS; slot++)
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
        // re-seeds fresh over the open ground rather than resuming a stale box.
        if (--c->shelter_cd <= 0)
        {
            c->sheltered = MachineSheltered(md);
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
    // The engine frees every world GObj on scene teardown; drop our cached handle
    // so the next active frame recreates it, and clear every cloud so the next
    // round re-seeds over fresh machine positions.
    stc_hail_gobj = NULL;
    stc_active = 0;
    for (int slot = 0; slot < HAIL_PLAYER_SLOTS; slot++)
    {
        stc_clouds[slot].seeded = 0;
        stc_clouds[slot].hit_cd = 0;
        stc_clouds[slot].sheltered = 0;
        stc_clouds[slot].shelter_cd = 0;
    }
}

// Surfaced in the Rain submenu (rain.c adds &hail_option to rain_menu) since hail
// rides on the rain layer.
OptionDesc hail_option = {
    .name = "Hail",
    .description = "Mix thicker icy hail into the rain; a stone striking an exposed machine does 1 damage - duck under a roof to take cover (Off = rain only)",
    .kind = OPTKIND_VALUE,
    .val = &hail_index,
    .value_num = HAIL_AMOUNT_NUM,
    .value_names = hail_names,
};
