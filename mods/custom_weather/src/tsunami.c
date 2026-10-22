// Tsunamis for custom_weather: sets of translucent waves roll in off the sea and
// across City Trial, carrying whatever they pass over and dissipating as they go.

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "obj.h"
#include "gx.h"
#include "item.h"
#include "machine.h"
#include "rider.h"
#include "collision.h"
#include "audio.h"
#include "effect.h"
#include "inline.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

// Defaults applied when a preset leaves the matching TsunamiDef field 0.
static const GXColor tsu_def_color = {64, 140, 214, 150};
#define TSU_DEF_COUNT      2
#define TSU_DEF_WAVES      3
#define TSU_DEF_SIZE       1.0f
#define TSU_DEF_STRENGTH   1.0f
#define TSU_DEF_SPEED      4.0f    // world units/frame

// Wave shape at size 1.0, world units. The front is a shallow arc whose middle leads
// its ends, and the height tapers to nothing at both ends.
#define TSU_HEIGHT         56.0f   // crest above the ground
#define TSU_DEPTH         170.0f   // toe to back, along the travel direction
#define TSU_HALF_WIDTH    950.0f   // half the front's length
#define TSU_BOW           150.0f   // how far the middle of the front leads its ends
#define TSU_RIPPLE         0.08f   // crest wobble as a fraction of the height

// The front is sampled in columns, each riding the ground under it. Casts start a
// step above the wave's base, so it climbs kerbs and slopes but runs through
// buildings rather than over their roofs.
#define TSU_COLUMNS          17
#define TSU_CASTS_PER_FRAME   6
#define TSU_STEP_UP        45.0f
#define TSU_BASE_FOLLOW     0.3f   // fraction of the gap to the cast height closed per frame

#define TSU_SPAWN_REACH     0.85f  // spawn-line distance from the box centre, of its half-extent
#define TSU_SET_GAP         150    // frames between the waves of one tsunami
#define TSU_SET_JITTER     160.0f  // lateral scatter of each wave's centre within a set
#define TSU_RISE_FRAMES      90    // a wave swells to full height out at sea
#define TSU_MAX_WAVES         8
#define TSU_AMP_EPS        0.02f

// A body inside the wave is carried along its travel direction toward a little over the
// wave's speed: each frame it moves by the part of that speed its own velocity does not
// already cover, scaled by where in the water it is and by the strength. A body driving
// against the wave is slowed, and one outrunning it is left alone.
#define TSU_CARRY_OVER     1.15f
#define TSU_TOE_WEIGHT     0.30f   // the foam ahead of the face still pushes this much
#define TSU_CLEAR          12.0f   // a body this far over the surface rides above it
#define TSU_UNDER          30.0f   // a body this far under the base is on a lower floor
#define TSU_ITEM_STEP      12.0f   // a carried item climbs or drops at most this much a frame

// A player a wave first catches is splashed with Kirby's water-landing particle and
// splash sound, and spray rises along the toe as the wave rolls.
#define TSU_SPLASH_SCALE   2.5f
#define TSU_SPRAY_GAP      10
#define TSU_SPRAY_MIN_AMP  0.3f
#define SFX_TSU_SPLASH     0x40031  // SFX_tyak_water

// The water's cross-section, toe to back: distance behind the toe as a fraction of
// the depth, height as a fraction of the crest, opacity, and how much white foam.
typedef struct WaveRow
{
    float u;
    float h;
    float alpha;
    float foam;
} WaveRow;

static const WaveRow wave_rows[] = {
    {0.00f, 0.00f, 0.30f, 0.70f},
    {0.10f, 0.72f, 0.85f, 0.80f},
    {0.24f, 1.00f, 1.00f, 1.00f},
    {0.46f, 0.70f, 0.80f, 0.25f},
    {1.00f, 0.00f, 0.00f, 0.00f},
};
#define TSU_ROWS ((int)GetElementsIn(wave_rows))
#define TSU_FOAM_MIX 0.55f

// p_link 1 freezes with the stage tick under the match pause and the hitstops.
#define TSU_GOBJ_CLASS     212
#define TSU_GOBJ_PLINK     GAMEPLINK_1
// After the priority-0 clear of the per-frame displacements and before the priority-4
// physics adds them to pos, so the push moves bodies this frame.
#define TSU_PROC_PRI       2

typedef struct Wave
{
    GOBJ *gobj;
    float sx, sz;   // spawn-line centre
    float dx, dz;   // unit travel direction
    float px, pz;   // unit direction along the front
    float front;    // distance travelled from the spawn line
    float travel;   // distance by which it has dissipated
    float amp;      // 0..1, scales height, opacity and push
    int   age;
    int   cast_next;
    u8    splashed;  // bit per ply already splashed
    float base_y[TSU_COLUMNS];
} Wave;

static int stc_active = 0;

// The live preset's TsunamiDef with the module defaults and menu overrides folded in,
// latched by Tsunami_SetActive.
static GXColor stc_color;
static int     stc_count;
static int     stc_waves;
static float   stc_size;
static float   stc_strength;
static float   stc_speed;

static WeatherSchedule stc_schedule;

// The tsunami in progress: waves still to launch, the heading they share, and the
// countdown to the next.
static int   stc_set_left = 0;
static int   stc_set_cd = 0;
static float stc_set_dx = 0.0f, stc_set_dz = 1.0f;

static Wave stc_waves_pool[TSU_MAX_WAVES];
static int  stc_wave_live[TSU_MAX_WAVES];

// Menu overrides. Index 0 is "Preset", the pass-through value, on every knob.
static int show_index = 0;

static const int count_values[] = {0, 1, 2, 3, 5};
static char *count_names[] = {"Preset", "1", "2", "3", "5"};
WEATHER_KNOB_CHECK(count_values, count_names);
static int count_index = 0;

static const int waves_values[] = {0, 1, 3, 5};
static char *waves_names[] = {"Preset", "1", "3", "5"};
WEATHER_KNOB_CHECK(waves_values, waves_names);
static int waves_index = 0;

static const float size_factors[] = {1.0f, 0.6f, 1.5f, 2.2f};
static char *size_names[] = {"Preset", "Small", "Large", "Huge"};
WEATHER_KNOB_CHECK(size_factors, size_names);
static int size_index = 0;

static const float strength_factors[] = {1.0f, 0.5f, 1.6f, 2.4f};
static char *strength_names[] = {"Preset", "Gentle", "Strong", "Violent"};
WEATHER_KNOB_CHECK(strength_factors, strength_names);
static int strength_index = 0;

static const float speed_factors[] = {1.0f, 0.65f, 1.5f};
static char *speed_names[] = {"Preset", "Slow", "Fast"};
WEATHER_KNOB_CHECK(speed_factors, speed_names);
static int speed_index = 0;

static float Clamp01(float x)
{
    return (x < 0.0f) ? 0.0f : (x > 1.0f) ? 1.0f : x;
}

// Height fraction of the cross-section at `u` (0 toe .. 1 back), linear between rows.
static float WaveProfile(float u)
{
    if (u <= 0.0f || u >= 1.0f)
        return 0.0f;
    for (int r = 1; r < TSU_ROWS; r++)
    {
        if (u <= wave_rows[r].u)
        {
            const WaveRow *a = &wave_rows[r - 1];
            const WaveRow *b = &wave_rows[r];
            return lerp(a->h, b->h, (u - a->u) / (b->u - a->u));
        }
    }
    return 0.0f;
}

// Height taper along the front, `s` in [-1, 1].
static float WaveEdge(float s)
{
    float s2 = s * s;
    return 1.0f - s2 * s2;
}

static float WaveColumnS(int k)
{
    return -1.0f + 2.0f * (float)k / (float)(TSU_COLUMNS - 1);
}

// Where column `s` of the front's toe stands this frame.
static void WaveToe(const Wave *w, float s, float *x, float *z)
{
    float lat = s * TSU_HALF_WIDTH * stc_size;
    float lead = w->front - TSU_BOW * stc_size * s * s;
    *x = w->sx + w->dx * lead + w->px * lat;
    *z = w->sz + w->dz * lead + w->pz * lat;
}

static float WaveBaseAt(const Wave *w, float s)
{
    float fk = (s + 1.0f) * 0.5f * (float)(TSU_COLUMNS - 1);
    int k = (int)fk;
    if (k < 0)
        k = 0;
    if (k >= TSU_COLUMNS - 1)
        k = TSU_COLUMNS - 2;
    float t = fk - (float)k;
    return lerp(w->base_y[k], w->base_y[k + 1], t);
}

// A few columns re-cast per frame, round robin, each easing toward what it finds.
static void WaveFollowGround(Wave *w, StageNode *sn)
{
    for (int c = 0; c < TSU_CASTS_PER_FRAME; c++)
    {
        int k = w->cast_next;
        w->cast_next = (k + 1) % TSU_COLUMNS;

        float x, z;
        WaveToe(w, WaveColumnS(k), &x, &z);
        Vec3 start = {x, w->base_y[k] + TSU_STEP_UP, z};
        Vec3 end = {x, sn->oob_min.Y - WEATHER_CAST_MARGIN, z};
        Vec3 hit;
        if (Raycast_Ground(&start, &end, &hit) < 0)
            continue;
        w->base_y[k] += (hit.Y - w->base_y[k]) * TSU_BASE_FOLLOW;
    }
}

// How hard the wave pushes at `pos`, 0 when it is outside the water: off either end
// of the front, ahead of the foam, behind the back, above the surface or on a floor
// below it.
static float WaveWeightAt(const Wave *w, const Vec3 *pos)
{
    float rx = pos->X - w->sx;
    float rz = pos->Z - w->sz;
    float s = (rx * w->px + rz * w->pz) / (TSU_HALF_WIDTH * stc_size);
    if (s < -1.0f || s > 1.0f)
        return 0.0f;

    float lead = w->front - TSU_BOW * stc_size * s * s;
    float u = (lead - (rx * w->dx + rz * w->dz)) / (TSU_DEPTH * stc_size);
    if (u < -0.08f || u >= 1.0f)
        return 0.0f;

    float prof = WaveProfile(u);
    float base = WaveBaseAt(w, s);
    float top = base + prof * TSU_HEIGHT * stc_size * w->amp * WaveEdge(s);
    if (pos->Y > top + TSU_CLEAR || pos->Y < base - TSU_UNDER)
        return 0.0f;

    float weight = (prof > TSU_TOE_WEIGHT) ? prof : TSU_TOE_WEIGHT;
    return weight * WaveEdge(s) * w->amp;
}

// This frame's carry for a body moving at `vel`: the shortfall of its speed along the wave
// against the carry speed, at most the carry speed itself.
static float WaveCarryStep(const Wave *w, const Vec3 *vel, float weight)
{
    float carry = stc_speed * TSU_CARRY_OVER;
    float need = carry - (vel->X * w->dx + vel->Z * w->dz);
    if (need <= 0.0f)
        return 0.0f;
    if (need > carry)
        need = carry;
    return need * weight * stc_strength;
}

// MachineData.ext_disp is a displacement no vanilla code writes, so the wave moves a
// machine like a conveyor without touching its velocity: grip cannot eat the push, and it
// ends the moment the wave passes. A parked machine that moves slides on and settles under
// its own WaitRun friction.
static void WavePushMachines(const Wave *w)
{
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_MACHINE]; g != NULL; g = g->next)
    {
        MachineData *md = (MachineData *)g->userdata;
        if (md->is_dead || md->is_fall_dead || md->is_on_rail)
            continue;
        float weight = WaveWeightAt(w, &md->pos);
        if (weight <= 0.0f)
            continue;
        float step = WaveCarryStep(w, &md->velocity, weight);
        md->ext_disp.X += w->dx * step;
        md->ext_disp.Z += w->dz * step;
    }
}

// The walk physics steers, damps and clamps self_vel, so a rider on foot is carried
// through its platform displacement instead.
static void WavePushRiders(const Wave *w)
{
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_RIDER]; g != NULL; g = g->next)
    {
        RiderData *rd = (RiderData *)g->userdata;
        if (Rider_IsOnMachine(rd))
            continue;
        float weight = WaveWeightAt(w, &rd->pos);
        if (weight <= 0.0f)
            continue;
        float step = WaveCarryStep(w, &rd->self_vel, weight);
        rd->platform_disp.X += w->dx * step;
        rd->platform_disp.Z += w->dz * step;
    }
}

// A resting power-up's physics is a no-op and nothing snaps it, so it is carried by moving
// it along the ground directly. A step it cannot make - a wall, a ledge, the sea - leaves
// it where it is. Boxes and items in flight are left alone.
static void WavePushItems(const Wave *w)
{
    static const Vec3 at_rest = {0.0f, 0.0f, 0.0f};
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_ITEM]; g != NULL; g = g->next)
    {
        ItemData *it = (ItemData *)g->userdata;
        if (Item_IsBox(it) || it->is_airborne != 0)
            continue;
        float weight = WaveWeightAt(w, &it->pos);
        if (weight <= 0.0f)
            continue;
        float step = WaveCarryStep(w, &at_rest, weight);
        float x = it->pos.X + w->dx * step;
        float z = it->pos.Z + w->dz * step;
        Vec3 start = {x, it->pos.Y + TSU_ITEM_STEP, z};
        Vec3 end = {x, it->pos.Y - TSU_ITEM_STEP, z};
        Vec3 hit;
        int tri = Raycast_Ground(&start, &end, &hit);
        if (tri < 0 || grGetGroundTypeFromTriangleID(tri) == GRCOLL_GROUNDTYPE_SEA)
            continue;
        it->pos = hit;
        it->point_coll.raycast_idx = tri;
        it->point_coll.land_pos = hit;
    }
}

static void WaveSplashPlayers(Wave *w)
{
    for (int ply = 0; ply < PLY_NUM; ply++)
    {
        if (w->splashed & (1 << ply))
            continue;
        GOBJ *rg = Ply_GetRiderGObj(ply);
        if (!rg || !rg->userdata)
            continue;
        RiderData *rd = (RiderData *)rg->userdata;
        if (WaveWeightAt(w, &rd->pos) <= 0.0f)
            continue;
        w->splashed |= 1 << ply;
        Weather_SpawnParticle(EFFECT_ID_WATER_SPLASH, &rd->pos, TSU_SPLASH_SCALE);
        Weather_PlaySoundAt(SFX_TSU_SPLASH, &rd->pos);
    }
}

static void WaveSpray(const Wave *w)
{
    if (w->age % TSU_SPRAY_GAP != 0 || w->amp < TSU_SPRAY_MIN_AMP)
        return;
    int k = HSD_Randi(TSU_COLUMNS);
    Vec3 pos;
    WaveToe(w, WaveColumnS(k), &pos.X, &pos.Z);
    pos.Y = w->base_y[k];
    Weather_SpawnParticle(EFFECT_ID_WATER_SPLASH, &pos, TSU_SPLASH_SCALE * stc_size * w->amp);
}

static void WaveRelease(Wave *w)
{
    stc_wave_live[w - stc_waves_pool] = 0;
    w->gobj = NULL;
}

// The wave's own proc: advance, swell or dissipate, follow the ground and push. A spent
// wave destroys its own GObj, which GObj_UpdateAll defers until this callback returns.
static void WaveThink(GOBJ *g)
{
    Wave *w = (Wave *)g->userdata;
    if (!w)
        return;

    StageNode *sn = Weather_StageNode();
    if (!sn)
        return;

    w->age++;
    w->front += stc_speed;

    float rise = Clamp01((float)w->age / (float)TSU_RISE_FRAMES);
    float fade = Clamp01(1.0f - w->front / w->travel);
    w->amp = rise * sqrtf(fade);
    if (w->front >= w->travel || (w->age > TSU_RISE_FRAMES && w->amp <= TSU_AMP_EPS))
    {
        WaveRelease(w);
        GObj_Destroy(g);
        return;
    }

    WaveFollowGround(w, sn);
    WavePushMachines(w);
    WavePushRiders(w);
    WavePushItems(w);
    WaveSplashPlayers(w);
    WaveSpray(w);
}

// GX callback on the world camera link: the cross-section rows extruded along the
// front as triangle strips, foamy and opaque at the crest, clear at the back.
static void WaveGX(GOBJ *g, int pass)
{
    if (pass != GXPASS_TEXEDGE)
        return;
    Wave *w = (Wave *)g->userdata;
    if (!w || w->amp <= TSU_AMP_EPS)
        return;

    COBJ *cam = COBJ_GetCurrent();
    if (!cam)
        return;

    float height = TSU_HEIGHT * stc_size * w->amp;
    float depth = TSU_DEPTH * stc_size;
    float alpha = (float)stc_color.a * sqrtf(w->amp);

    GX_BeginXlu(cam, RENDER_VERTEX, GX_BL_INVSRCALPHA);
    for (int r = 0; r + 1 < TSU_ROWS; r++)
    {
        GXBegin(GX_TRIANGLESTRIP, GX_VTXFMT0, 2 * TSU_COLUMNS);
        for (int k = 0; k < TSU_COLUMNS; k++)
        {
            float s = WaveColumnS(k);
            float e = WaveEdge(s);
            float x, z;
            WaveToe(w, s, &x, &z);
            float ripple = 1.0f + TSU_RIPPLE * sinf((float)w->age * 0.11f + s * 9.0f);

            for (int i = 0; i < 2; i++)
            {
                const WaveRow *row = &wave_rows[r + i];
                float back = row->u * depth;
                float y = w->base_y[k] + row->h * height * e * ripple;
                float m = row->foam * TSU_FOAM_MIX;
                u8 cr = (u8)lerp(stc_color.r, 255.0f, m);
                u8 cg = (u8)lerp(stc_color.g, 255.0f, m);
                u8 cb = (u8)lerp(stc_color.b, 255.0f, m);
                u8 ca = (u8)(alpha * row->alpha * sqrtf(e));
                GXPosition3f32(x - w->dx * back, y, z - w->dz * back);
                GXColor4u8(cr, cg, cb, ca);
            }
        }
    }
    HSD_StateInvalidate(HSD_STATE_ALL);
}

// GObj_Destroy requires a destructor; the wave's userdata is a static pool slot.
static void WaveFreeUserData(void *data)
{
    (void)data;
}

static void WaveDestroy(int i)
{
    if (stc_waves_pool[i].gobj)
        GObj_Destroy(stc_waves_pool[i].gobj);
    WaveRelease(&stc_waves_pool[i]);
}

static void WaveSpawn(StageNode *sn, float dx, float dz)
{
    int slot = -1;
    for (int i = 0; i < TSU_MAX_WAVES; i++)
    {
        if (!stc_wave_live[i])
        {
            slot = i;
            break;
        }
    }
    if (slot < 0)
        return;

    GOBJ *g = GObj_Create(TSU_GOBJ_CLASS, TSU_GOBJ_PLINK, 0);
    if (!g)
    {
        static int warned;
        if (!warned)
        {
            warned = 1;
            OSReport("[Tsunami] Wave not created, GObj pool exhausted\n");
        }
        return;
    }

    Wave *w = &stc_waves_pool[slot];
    w->gobj = g;
    w->dx = dx;
    w->dz = dz;
    w->px = dz;
    w->pz = -dx;
    w->front = 0.0f;
    w->amp = 0.0f;
    w->age = 0;
    w->cast_next = 0;
    w->splashed = 0;

    // City Trial's island sits in a sea that fills the whole box, so the spawn line is
    // open water on every heading and twice its reach clears the far coast.
    float cx, cz, hx, hz;
    Weather_PlayBox(sn, &cx, &cz, &hx, &hz);
    float jitter = Weather_Randf2() * TSU_SET_JITTER;
    float reach = ((hx < hz) ? hx : hz) * TSU_SPAWN_REACH;
    w->sx = cx - dx * reach + w->px * jitter;
    w->sz = cz - dz * reach + w->pz * jitter;
    w->travel = 2.0f * reach;

    // The sea is terraced, -8 in the south to 57 in the north, so each column starts on
    // the water under it.
    for (int k = 0; k < TSU_COLUMNS; k++)
    {
        float x, z;
        Vec3 hit;
        WaveToe(w, WaveColumnS(k), &x, &z);
        int tri = Weather_CastDown(sn, x, z, sn->oob_min.Y - WEATHER_CAST_MARGIN, &hit);
        w->base_y[k] = (tri >= 0) ? hit.Y : 0.0f;
    }

    GObj_AddUserData(g, 0, WaveFreeUserData, w);
    GObj_AddProc(g, WaveThink, TSU_PROC_PRI);
    GObj_AddGXLink(g, WaveGX, WEATHER_GX_LINK, WEATHER_GX_PRI);
    stc_wave_live[slot] = 1;
}

static void TsunamiEndAll(void)
{
    for (int i = 0; i < TSU_MAX_WAVES; i++)
    {
        if (stc_wave_live[i])
            WaveDestroy(i);
    }
    stc_set_left = 0;
}

// A new preset mid-round - a legendary machine's sky - washes out every live wave and
// keeps the round's schedule.
void Tsunami_SetActive(const TsunamiDef *def)
{
    TsunamiEndAll();

    stc_active = WeatherToggle(show_index, def && def->enabled);

    stc_color = (def && def->color) ? GXColor_Unpack(def->color) : tsu_def_color;

    int def_count = (def && def->count > 0) ? def->count : TSU_DEF_COUNT;
    stc_count = (count_index > 0) ? count_values[count_index] : def_count;

    int def_waves = (def && def->waves > 0) ? def->waves : TSU_DEF_WAVES;
    stc_waves = (waves_index > 0) ? waves_values[waves_index] : def_waves;

    float def_size = (def && def->size > 0.0f) ? def->size : TSU_DEF_SIZE;
    stc_size = def_size * size_factors[size_index];

    float def_strength = (def && def->strength > 0.0f) ? def->strength : TSU_DEF_STRENGTH;
    stc_strength = def_strength * strength_factors[strength_index];

    float def_speed = (def && def->speed > 0.0f) ? def->speed : TSU_DEF_SPEED;
    stc_speed = def_speed * speed_factors[speed_index];
}

void Tsunami_Tick(void)
{
    if (!stc_active)
        return;

    StageNode *sn = Weather_StageNode();
    if (!sn)
        return;

    float p = Weather_RoundProgress();
    if (p < 0.0f)
        return;

    if (stc_set_left > 0)
    {
        if (--stc_set_cd <= 0)
        {
            WaveSpawn(sn, stc_set_dx, stc_set_dz);
            stc_set_left--;
            stc_set_cd = TSU_SET_GAP;
        }
        return;
    }

    if (WeatherSchedule_Due(&stc_schedule, stc_count, p))
    {
        float a = RandomAngle();
        stc_set_dx = sinf(a);
        stc_set_dz = cosf(a);
        stc_set_left = stc_waves;
        stc_set_cd = 0;
        OSReport("[Tsunami] Tsunami %d/%d at progress %d%%, %d waves heading %d deg\n",
                 stc_schedule.next, stc_schedule.num, (int)(p * 100.0f), stc_waves,
                 (int)(a * 180.0f / M_PI));
    }
}

// The waves' GObjs went with the scene heap.
void Tsunami_Reset(void)
{
    stc_active = 0;
    stc_schedule.planned = 0;
    stc_set_left = 0;
    for (int i = 0; i < TSU_MAX_WAVES; i++)
    {
        stc_wave_live[i] = 0;
        stc_waves_pool[i].gobj = NULL;
    }
}

static void OnTsunamiShowChange(int val)
{
    OSReport("[Tsunami] Tsunami %s\n", weather_toggle_names[val]);
}

static void OnTsunamiCountChange(int val)
{
    OSReport("[Tsunami] Appearances %s\n", count_names[val]);
}

static void OnTsunamiWavesChange(int val)
{
    OSReport("[Tsunami] Waves %s\n", waves_names[val]);
}

static void OnTsunamiSizeChange(int val)
{
    OSReport("[Tsunami] Size %s\n", size_names[val]);
}

static void OnTsunamiStrengthChange(int val)
{
    OSReport("[Tsunami] Strength %s\n", strength_names[val]);
}

static void OnTsunamiSpeedChange(int val)
{
    OSReport("[Tsunami] Speed %s\n", speed_names[val]);
}

MenuDesc tsunami_menu = {
    .option_num = 6,
    .options = {
        &(OptionDesc){
            .name = "Tsunami",
            .description = "Let tsunamis roll in off the sea: Preset = only presets that set it, Off = never, On = all presets",
            .kind = OPTKIND_VALUE,
            .val = &show_index,
            .value_num = WEATHER_TOGGLE_NUM,
            .value_names = weather_toggle_names,
            .on_change = OnTsunamiShowChange,
        },
        &(OptionDesc){
            .name = "Appearances",
            .description = "How many tsunamis strike over a City Trial round, spread across the match timer",
            .kind = OPTKIND_VALUE,
            .val = &count_index,
            .value_num = GetElementsIn(count_values),
            .value_names = count_names,
            .on_change = OnTsunamiCountChange,
        },
        &(OptionDesc){
            .name = "Waves",
            .description = "How many waves each tsunami sends in, one after another from the same side",
            .kind = OPTKIND_VALUE,
            .val = &waves_index,
            .value_num = GetElementsIn(waves_values),
            .value_names = waves_names,
            .on_change = OnTsunamiWavesChange,
        },
        &(OptionDesc){
            .name = "Size",
            .description = "How tall, deep and wide each wave is",
            .kind = OPTKIND_VALUE,
            .val = &size_index,
            .value_num = GetElementsIn(size_factors),
            .value_names = size_names,
            .on_change = OnTsunamiSizeChange,
        },
        &(OptionDesc){
            .name = "Strength",
            .description = "How hard a wave carries the riders, machines and items it catches",
            .kind = OPTKIND_VALUE,
            .val = &strength_index,
            .value_num = GetElementsIn(strength_factors),
            .value_names = strength_names,
            .on_change = OnTsunamiStrengthChange,
        },
        &(OptionDesc){
            .name = "Speed",
            .description = "How fast the waves travel across the city",
            .kind = OPTKIND_VALUE,
            .val = &speed_index,
            .value_num = GetElementsIn(speed_factors),
            .value_names = speed_names,
            .on_change = OnTsunamiSpeedChange,
        },
    },
};
