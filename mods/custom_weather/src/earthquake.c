// Earthquakes for custom_weather: the ground rumbles under every view and tears open
// cracks that anything on the surface can fall through, which close again as the
// quake settles.

#include <string.h>

#include "os.h"
#include "game.h"
#include "hsd.h"
#include "stage.h"
#include "obj.h"
#include "gx.h"
#include "collision.h"
#include "audio.h"
#include "effect.h"
#include "enemy.h"
#include "inline.h"
#include "code_patch/code_patch.h"
#include "hoshi/settings.h"

#include "custom_weather.h"
#include "weather_fx.h"

// Defaults applied when a preset leaves the matching EarthquakeDef field 0.
#define QUAKE_DEF_COUNT      2
#define QUAKE_DEF_DURATION   720     // 12s at 60fps
#define QUAKE_DEF_CRACKS     3
#define QUAKE_DEF_SIZE       1.0f
#define QUAKE_DEF_INTENSITY  1.0f

// One quake's timeline in frames. The rumble builds, the cracks tear outward from
// their seeds, hold, then close over the quake's tail while the rumble dies away.
#define QUAKE_RAMP_IN        45
#define QUAKE_TEAR_DELAY     40
#define QUAKE_TEAR_FRAMES    90
#define QUAKE_CLOSE_FRAMES   150
#define QUAKE_RAMP_OUT       120

// Crack shape at size 1.0, world units. A crack is two arms grown in opposite
// directions from a seed, widest at the seed and tapering to a point at each end,
// with a few narrower branches forking off the arms.
#define CRACK_STEP           20.0f   // centreline segment length
#define CRACK_ARM_STEPS      9       // segments per arm
#define CRACK_ARM_STEPS_MAX  24
#define CRACK_HALF_WIDTH     16.0f   // half-width at the seed
#define CRACK_TURN           0.45f   // max radians a segment turns off the last
#define CRACK_BRANCHES       2       // branch attempts per crack
#define CRACK_BRANCH_STEPS   5
#define CRACK_BRANCH_MIN_VERTS 5     // a line shorter than this grows no branches
#define CRACK_BRANCH_WIDTH   0.65f   // branch root half-width over the fork vertex's
#define CRACK_BRANCH_ANGLE   0.55f   // min radians a branch leaves its arm at (up to twice that)
#define CRACK_TEAR_SPREAD    60.0f   // arc length over which the tearing edge widens

// Placement: near-flat floor, off the sea, with nothing beneath it, so a fall ends
// below the city.
#define CRACK_PLAY_FRACTION  0.75f
#define CRACK_SEED_TRIES     12
#define CRACK_MIN_NORMAL_Y   0.85f
#define CRACK_MAX_RISE       10.0f   // a step steeper than this ends an arm (kerb, wall, building)
#define CRACK_BELOW_GAP      8.0f    // the under-check casts down from this far below the floor
#define CRACK_LIFT           2.0f    // decal height above the floor
// A floor hit further than this above or below the crack's surface is another floor. The
// band reaches further down, so a body dropping through cannot catch a lower skin by the rim.
#define CRACK_BAND_UP        14.0f
#define CRACK_BAND_DOWN      40.0f

#define CRACK_MAX_CRACKS     6
#define CRACK_MAX_LINES      (CRACK_MAX_CRACKS * (1 + CRACK_BRANCHES))
#define CRACK_MAX_VERTS      640

#define CRACK_JOINT_SEGS     8
#define CRACK_RIM_SCALE      1.3f    // the lip is this much wider than the hole
#define CRACK_RIM_PAD        1.5f
static const GXColor crack_core_color = {0, 0, 0, 255};
static const GXColor crack_rim_color = {46, 34, 24, 200};

// Camera rumble from one global constant quake source, world units of eye offset at full
// intensity. Priority 2 overrides the machine speed shake (0) and the small event quakes,
// and yields to a large one (3).
#define QUAKE_SHAKE_AMP      2.6f
#define QUAKE_SHAKE_PRIORITY 2

// Controller rumble: a short LbRumble pulse per player every QUAKE_RUMBLE_GAP frames, the
// pattern stepping up and back down over the quake, then a decaying burst as it settles.
#define QUAKE_RUMBLE_GAP     20
#define QUAKE_RUMBLE_LEN     24
#define QUAKE_RUMBLE_SETTLE  6
static const u8 quake_rumble_kinds[] = {8, 1, 10, 7, 10, 1, 8};

// A looping rumble bed that follows the shake envelope, a crash at each crack as it tears
// and a crack as it starts to close.
#define SFX_QUAKE_BED        0x120005  // SFX_map_city_volcano_lp
#define SFX_QUAKE_TEAR       0x12000f  // SFX_map_city_big_stone_crash
#define SFX_QUAKE_CLOSE      0x12000e  // SFX_map_city_big_stone_hibi
#define QUAKE_BED_VOLUME     200

// The pillar event's ground burst at each seed as the tear starts, then dust puffs along
// the opening and closing edges.
#define QUAKE_BURST_SCALE    3.0f
#define QUAKE_BURST_DROP     5.0f
#define QUAKE_PUFF_SCALE     1.5f
#define QUAKE_PUFF_GAP_TEAR  3
#define QUAKE_PUFF_GAP_CLOSE 8

typedef struct CrackVert
{
    Vec3  pos;    // on the floor
    float w;      // half-width when fully open
    float d;      // arc length from the crack's seed; the tear reaches it in that order
    float cur;    // this frame's half-width
} CrackVert;

typedef struct CrackLine
{
    int   first;  // into stc_verts
    int   num;
    int   seed;   // the seed vertex on a crack's main line, -1 on a branch
    float min_x, max_x, min_z, max_z;  // padded by the widest vertex
    float min_y, max_y;
} CrackLine;

static int stc_active = 0;

// The live preset's EarthquakeDef with the module defaults and menu overrides folded
// in, latched by Earthquake_SetActive.
static int   stc_count;
static int   stc_duration;
static int   stc_cracks;
static float stc_size;
static float stc_intensity;

_Static_assert(CRACK_MAX_CRACKS <= QUAKE_TEAR_DELAY,
               "every crack must be grown, one per frame, before the tear starts");

static WeatherSchedule stc_schedule;

// The live quake.
static int   stc_live = 0;
static int   stc_elapsed = 0;
static int   stc_cracks_tried = 0;  // cracks are grown one per frame ahead of the tear
static int   stc_cracks_made = 0;
static float stc_tear_len = 0.0f;  // the longest arc length any crack's tear must cover
static float stc_shake = 0.0f;     // this frame's rumble envelope, 0..1
static int   stc_open = 0;         // some crack has a width this frame; gates the collision hooks
static int   stc_round_cracked = 0;  // a crack opened this round, so a walking player who falls respawns
static GOBJ *stc_gx_gobj = NULL;

static u32         stc_shake_src = 0;  // 0 = no quake source
static int         stc_bed_live = 0;
static FGMInstance stc_bed;

static CrackVert stc_verts[CRACK_MAX_VERTS];
static int       stc_vert_count = 0;
static CrackLine stc_lines[CRACK_MAX_LINES];
static int       stc_line_count = 0;

static float stc_joint_cos[CRACK_JOINT_SEGS + 1];
static float stc_joint_sin[CRACK_JOINT_SEGS + 1];
static int   stc_joint_seeded = 0;

// Menu overrides. Index 0 is "Preset", the pass-through value, on every knob.
static int show_index = 0;

static const int count_values[] = {0, 1, 2, 3, 5};
static char *count_names[] = {"Preset", "1", "2", "3", "5"};
WEATHER_KNOB_CHECK(count_values, count_names);
static int count_index = 0;

static const float duration_factors[] = {1.0f, 0.5f, 1.6f, 2.5f};
static char *duration_names[] = {"Preset", "Brief", "Long", "Sustained"};
WEATHER_KNOB_CHECK(duration_factors, duration_names);
static int duration_index = 0;

// One Big trades the crack count for a single crack twice the size.
static const int cracks_values[] = {0, 1, 1, 3, 6};
static const float cracks_size[] = {1.0f, 2.0f, 1.0f, 1.0f, 1.0f};
static char *cracks_names[] = {"Preset", "One Big", "1", "3", "6"};
WEATHER_KNOB_CHECK(cracks_values, cracks_names);
WEATHER_KNOB_CHECK(cracks_size, cracks_names);
static int cracks_index = 0;

static const float size_factors[] = {1.0f, 0.6f, 1.5f, 2.2f};
static char *size_names[] = {"Preset", "Narrow", "Wide", "Chasm"};
WEATHER_KNOB_CHECK(size_factors, size_names);
static int size_index = 0;

static const float shake_factors[] = {1.0f, 0.0f, 0.5f, 1.8f};
static char *shake_names[] = {"Preset", "Off", "Light", "Violent"};
WEATHER_KNOB_CHECK(shake_factors, shake_names);
static int shake_index = 0;

static int rumble_index = 1;

static float Clamp01(float x)
{
    return (x < 0.0f) ? 0.0f : (x > 1.0f) ? 1.0f : x;
}

// A floor point a crack may cross: near-flat, not the sea, and nothing beneath it.
// `prev_y` rejects a step up or down a kerb, wall or roof; pass the point's own
// height to skip that test.
static int CrackFloorAt(StageNode *sn, float x, float z, float prev_y, int check_rise,
                        Vec3 *out)
{
    Vec3 hit;
    int tri = Weather_CastDown(sn, x, z, sn->oob_min.Y - WEATHER_CAST_MARGIN, &hit);
    if (tri < 0)
        return 0;
    if (check_rise && _fabs(hit.Y - prev_y) > CRACK_MAX_RISE)
        return 0;

    Vec3 nrm;
    PointCollision_GetNormalByID(tri, &nrm);
    if (nrm.Y < CRACK_MIN_NORMAL_Y)
        return 0;
    if (grGetGroundTypeFromTriangleID(tri) == GRCOLL_GROUNDTYPE_SEA)
        return 0;

    Vec3 start = {x, hit.Y - CRACK_BELOW_GAP, z};
    Vec3 end = {x, sn->oob_min.Y - WEATHER_CAST_MARGIN, z};
    Vec3 below;
    if (Raycast_Ground(&start, &end, &below) >= 0)
        return 0;

    *out = hit;
    return 1;
}

// Walks `steps` segments from `from` along the unit direction (dx, dz), turning a
// little each step, appending each accepted vertex. Stops early at anything
// CrackFloorAt rejects. Returns the number appended.
static int CrackGrowArm(StageNode *sn, const Vec3 *from, float dx, float dz, int steps,
                        float step, float d0)
{
    Vec3 at = *from;
    int n = 0;
    for (int i = 1; i <= steps && stc_vert_count < CRACK_MAX_VERTS; i++)
    {
        float turn = Weather_Randf2() * CRACK_TURN;
        float c = cosf(turn), s = sinf(turn);
        float rx = dx * c - dz * s;
        dz = dx * s + dz * c;
        dx = rx;
        float x = at.X + dx * step;
        float z = at.Z + dz * step;
        Vec3 hit;
        if (!CrackFloorAt(sn, x, z, at.Y, 1, &hit))
            break;
        CrackVert *v = &stc_verts[stc_vert_count++];
        v->pos = hit;
        v->d = d0 + step * (float)i;
        v->w = 0.0f;
        v->cur = 0.0f;
        at = hit;
        n++;
    }
    return n;
}

static void CrackLineBounds(CrackLine *line)
{
    float pad = 0.0f;
    CrackVert *v = &stc_verts[line->first];
    for (int i = 0; i < line->num; i++)
    {
        if (v[i].w > pad)
            pad = v[i].w;
    }
    pad = pad * CRACK_RIM_SCALE + CRACK_RIM_PAD;

    line->min_x = line->max_x = v[0].pos.X;
    line->min_z = line->max_z = v[0].pos.Z;
    line->min_y = line->max_y = v[0].pos.Y;
    for (int i = 1; i < line->num; i++)
    {
        if (v[i].pos.X < line->min_x) line->min_x = v[i].pos.X;
        if (v[i].pos.X > line->max_x) line->max_x = v[i].pos.X;
        if (v[i].pos.Z < line->min_z) line->min_z = v[i].pos.Z;
        if (v[i].pos.Z > line->max_z) line->max_z = v[i].pos.Z;
        if (v[i].pos.Y < line->min_y) line->min_y = v[i].pos.Y;
        if (v[i].pos.Y > line->max_y) line->max_y = v[i].pos.Y;
    }
    line->min_x -= pad;
    line->max_x += pad;
    line->min_z -= pad;
    line->max_z += pad;
    line->min_y -= CRACK_BAND_DOWN;
    line->max_y += CRACK_BAND_UP;
}

// A branch forks off an interior vertex of `parent` at an angle, narrower than the
// arm it leaves.
static void CrackGrowBranch(StageNode *sn, const CrackLine *parent, float step)
{
    if (parent->num < CRACK_BRANCH_MIN_VERTS || stc_line_count >= CRACK_MAX_LINES)
        return;

    int fork_idx = parent->first + 1 + HSD_Randi(parent->num - 2);
    CrackVert fork = stc_verts[fork_idx];
    if (fork.w <= 0.0f)
        return;

    // The parent's direction there, turned off to one side.
    const CrackVert *nb = &stc_verts[fork_idx + 1];
    float ex = nb->pos.X - fork.pos.X;
    float ez = nb->pos.Z - fork.pos.Z;
    float len = sqrtf(ex * ex + ez * ez);
    if (len < 1e-4f)
        return;
    float off = CRACK_BRANCH_ANGLE * (1.0f + HSD_Randf());
    if (HSD_Randf() < 0.5f)
        off = -off;
    float c = cosf(off), s = sinf(off);
    float dx = (ex * c - ez * s) / len;
    float dz = (ex * s + ez * c) / len;

    CrackLine *line = &stc_lines[stc_line_count];
    line->first = stc_vert_count;
    line->seed = -1;
    if (stc_vert_count >= CRACK_MAX_VERTS)
        return;
    stc_verts[stc_vert_count++] = fork;
    int steps = (int)(CRACK_BRANCH_STEPS * stc_size + 0.5f);
    int n = CrackGrowArm(sn, &fork.pos, dx, dz, steps, step, fork.d);
    if (n < 2)
    {
        stc_vert_count = line->first;
        return;
    }
    line->num = n + 1;

    float root = fork.w * CRACK_BRANCH_WIDTH;
    for (int i = 0; i < line->num; i++)
    {
        float t = (float)i / (float)(line->num - 1);
        stc_verts[line->first + i].w = (i == 0) ? fork.w : root * (1.0f - t);
    }
    CrackLineBounds(line);
    stc_line_count++;
}

// One crack: a seed on clear floor and two arms grown from it in opposite directions,
// stored as one polyline from the end of one arm through the seed to the end of the
// other. Returns 0 when no seed or no arm length was found.
static int CrackGenerate(StageNode *sn)
{
    if (stc_line_count >= CRACK_MAX_LINES)
        return 0;

    float step = CRACK_STEP;
    int arm_steps = (int)(CRACK_ARM_STEPS * stc_size + 0.5f);
    if (arm_steps > CRACK_ARM_STEPS_MAX)
        arm_steps = CRACK_ARM_STEPS_MAX;
    if (arm_steps < 2)
        arm_steps = 2;
    if (stc_vert_count + 2 * arm_steps + 1 > CRACK_MAX_VERTS)
        return 0;

    Vec3 seed;
    int found = 0;
    for (int t = 0; t < CRACK_SEED_TRIES && !found; t++)
    {
        float x, z;
        Weather_RandBoxPoint(sn, CRACK_PLAY_FRACTION, &x, &z);
        found = CrackFloorAt(sn, x, z, 0.0f, 0, &seed);
    }
    if (!found)
        return 0;

    float heading = RandomAngle();
    float dx = sinf(heading);
    float dz = cosf(heading);

    // Arm A grows into scratch space past the line, then is copied in reversed.
    int base = stc_vert_count;
    int na = CrackGrowArm(sn, &seed, -dx, -dz, arm_steps, step, 0.0f);
    CrackVert arm_a[CRACK_ARM_STEPS_MAX];
    memcpy(arm_a, &stc_verts[base], na * sizeof(CrackVert));
    stc_vert_count = base;

    for (int i = na - 1; i >= 0; i--)
        stc_verts[stc_vert_count++] = arm_a[i];
    CrackVert *s = &stc_verts[stc_vert_count++];
    s->pos = seed;
    s->d = 0.0f;
    int nb = CrackGrowArm(sn, &seed, dx, dz, arm_steps, step, 0.0f);

    CrackLine *line = &stc_lines[stc_line_count];
    line->first = base;
    line->num = na + 1 + nb;
    line->seed = base + na;
    if (line->num < 3)
    {
        stc_vert_count = base;
        return 0;
    }

    float arm_len = step * (float)arm_steps;
    float width = CRACK_HALF_WIDTH * stc_size;
    for (int i = 0; i < line->num; i++)
    {
        CrackVert *v = &stc_verts[base + i];
        int end = (i == 0 || i == line->num - 1);
        float x = Clamp01(v->d / arm_len);
        float taper = 1.0f - x * sqrtf(x);
        v->w = end ? 0.0f : width * taper * RandomInRangef(0.75f, 1.15f);
        v->cur = 0.0f;
    }
    CrackLineBounds(line);
    stc_line_count++;

    // Branches only append past the main line, so it stays put.
    int main_line = stc_line_count - 1;
    for (int b = 0; b < CRACK_BRANCHES; b++)
        CrackGrowBranch(sn, &stc_lines[main_line], step);
    return 1;
}

// This frame's half-width at every vertex: the tear front sweeps outward from each
// seed by arc length, and the whole crack narrows shut over the quake's tail.
static void CrackUpdateWidths(void)
{
    float tear = Clamp01((float)(stc_elapsed - QUAKE_TEAR_DELAY) / (float)QUAKE_TEAR_FRAMES);
    float front = tear * (stc_tear_len + CRACK_TEAR_SPREAD);
    int left = stc_duration - stc_elapsed;
    float close = Clamp01((float)left / (float)QUAKE_CLOSE_FRAMES);
    close = close * close * (3.0f - 2.0f * close);

    int open = 0;
    for (int i = 0; i < stc_vert_count; i++)
    {
        CrackVert *v = &stc_verts[i];
        float reveal = Clamp01((front - v->d) / CRACK_TEAR_SPREAD);
        v->cur = v->w * reveal * close;
        if (v->cur > 0.0f)
            open = 1;
    }
    stc_open = open;
}

static int CrackInBand(float y, float surface_y)
{
    float dy = y - surface_y;
    return dy <= CRACK_BAND_UP && dy >= -CRACK_BAND_DOWN;
}

// Whether a floor hit at `p` lands inside an open crack: within a segment's
// trapezoid (perpendicular offsets, half-width lerped along it) or a joint's disc, and
// within the band around the crack's own surface.
static int CrackContains(float x, float y, float z)
{
    for (int l = 0; l < stc_line_count; l++)
    {
        const CrackLine *line = &stc_lines[l];
        if (x < line->min_x || x > line->max_x || z < line->min_z || z > line->max_z)
            continue;
        if (y < line->min_y || y > line->max_y)
            continue;

        const CrackVert *v = &stc_verts[line->first];
        for (int i = 0; i < line->num; i++)
        {
            const CrackVert *a = &v[i];
            float ax = x - a->pos.X;
            float az = z - a->pos.Z;
            if (a->cur > 0.0f && ax * ax + az * az <= a->cur * a->cur
                && CrackInBand(y, a->pos.Y))
                return 1;

            if (i + 1 >= line->num)
                break;
            const CrackVert *b = &v[i + 1];
            if (a->cur <= 0.0f && b->cur <= 0.0f)
                continue;
            float ex = b->pos.X - a->pos.X;
            float ez = b->pos.Z - a->pos.Z;
            float len2 = ex * ex + ez * ez;
            if (len2 <= 0.0f)
                continue;
            float t = (ax * ex + az * ez) / len2;
            if (t < 0.0f || t > 1.0f)
                continue;
            float px = ax - ex * t;
            float pz = az - ez * t;
            float w = lerp(a->cur, b->cur, t);
            if (px * px + pz * pz > w * w)
                continue;
            if (CrackInBand(y, lerp(a->pos.Y, b->pos.Y, t)))
                return 1;
        }
    }
    return 0;
}

int Earthquake_IsHole(const Vec3 *p)
{
    if (!stc_open)
        return 0;
    return CrackContains(p->X, p->Y, p->Z);
}

// Respawner_Update (0x8000ff78) eliminates a walking player who fell anywhere but the
// Derby. Once a crack has opened this round it is told the round is the Derby, so a
// rider who drops through on foot respawns like a machine.
static int Earthquake_RespawnCityKind(int city_kind)
{
    return stc_round_cracked ? CITYKIND_DERBY : city_kind;
}

// Every floor query ends in one of two narrowphase tests, hooked on their shared
// epilogue: r3 holds the result, r27 / r19 the triangle's normal (its kind at +0x24) and
// r29 the hit point. A floor hit inside an open crack is turned into a miss.
CODEPATCH_HOOKCREATE(0x800d9938,
    "cmpwi 3, 0\n\t"
    "beq 1f\n\t"
    "lwz 0, 0x24(27)\n\t"
    "andi. 0, 0, 1\n\t"
    "beq 1f\n\t"
    "mr 3, 29\n\t",
    Earthquake_IsHole,
    "cntlzw 3, 3\n\t"
    "srwi 3, 3, 5\n\t"
    "1:\n\t",
    0)

CODEPATCH_HOOKCREATE(0x80244ea0,
    "cmpwi 3, 0\n\t"
    "beq 1f\n\t"
    "lwz 0, 0x24(19)\n\t"
    "andi. 0, 0, 1\n\t"
    "beq 1f\n\t"
    "mr 3, 29\n\t",
    Earthquake_IsHole,
    "cntlzw 3, 3\n\t"
    "srwi 3, 3, 5\n\t"
    "1:\n\t",
    0)

// Bodies at rest trust a cached ground triangle and never query again; each re-checks it
// through ItemColl_GetGenericCollFlags, and a 0 drops it the way a retired prop's floor
// does. Each hook reads that flag as 0 when the cached contact point is in a crack.
#define QUAKE_CACHED_GROUND_HOOK(addr, owner, pos_off)  \
    CODEPATCH_HOOKCREATE(addr,                          \
        "cmpwi 3, 0\n\t"                               \
        "beq 1f\n\t"                                   \
        "addi 3, " #owner ", " #pos_off "\n\t",        \
        Earthquake_IsHole,                              \
        "cntlzw 3, 3\n\t"                              \
        "srwi 3, 3, 5\n\t"                             \
        "1:\n\t",                                      \
        0)

QUAKE_CACHED_GROUND_HOOK(0x801ee7d0, 31, 0xb5c)  // parked star, MachineData.ground_probe_pos
QUAKE_CACHED_GROUND_HOOK(0x801f8a68, 31, 0x718)  // stopped bike, MachineData.ground_pos
QUAKE_CACHED_GROUND_HOOK(0x80254d94, 31, 0x1ac)  // resting item, ItemData.point_coll.land_pos
QUAKE_CACHED_GROUND_HOOK(0x80255148, 31, 0x1ac)
QUAKE_CACHED_GROUND_HOOK(0x80255484, 31, 0x1ac)  // Item_GenericEnvColl
QUAKE_CACHED_GROUND_HOOK(0x80255dfc, 30, 0x1ac)  // ItemColl_HandleLand

CODEPATCH_HOOKCREATE(0x800100f4,
    "mr 3, 0\n\t",
    Earthquake_RespawnCityKind,
    "mr 0, 3\n\t",
    0)

// The hole's outline at one scale: a trapezoid per segment and a disc per vertex,
// which together close the joints a bend opens. Exactly the shape CrackContains
// tests at scale 1.
static void CrackDrawShape(const CrackLine *line, float scale, float pad, GXColor c)
{
    const CrackVert *v = &stc_verts[line->first];

    GXBegin(GX_QUADS, GX_VTXFMT0, 4 * (line->num - 1));
    for (int i = 0; i + 1 < line->num; i++)
    {
        const CrackVert *a = &v[i];
        const CrackVert *b = &v[i + 1];
        float ex = b->pos.X - a->pos.X;
        float ez = b->pos.Z - a->pos.Z;
        float len = sqrtf(ex * ex + ez * ez);
        float nx = 0.0f, nz = 0.0f;
        if (len > 1e-4f)
        {
            nx = -ez / len;
            nz = ex / len;
        }
        float wa = (a->cur > 0.0f) ? a->cur * scale + pad : 0.0f;
        float wb = (b->cur > 0.0f) ? b->cur * scale + pad : 0.0f;
        float ya = a->pos.Y + CRACK_LIFT;
        float yb = b->pos.Y + CRACK_LIFT;
        GXPosition3f32(a->pos.X + nx * wa, ya, a->pos.Z + nz * wa);
        GXColor4u8(c.r, c.g, c.b, c.a);
        GXPosition3f32(b->pos.X + nx * wb, yb, b->pos.Z + nz * wb);
        GXColor4u8(c.r, c.g, c.b, c.a);
        GXPosition3f32(b->pos.X - nx * wb, yb, b->pos.Z - nz * wb);
        GXColor4u8(c.r, c.g, c.b, c.a);
        GXPosition3f32(a->pos.X - nx * wa, ya, a->pos.Z - nz * wa);
        GXColor4u8(c.r, c.g, c.b, c.a);
    }

    GXBegin(GX_TRIANGLES, GX_VTXFMT0, 3 * CRACK_JOINT_SEGS * line->num);
    for (int i = 0; i < line->num; i++)
    {
        const CrackVert *a = &v[i];
        float r = (a->cur > 0.0f) ? a->cur * scale + pad : 0.0f;
        float y = a->pos.Y + CRACK_LIFT;
        for (int s = 0; s < CRACK_JOINT_SEGS; s++)
        {
            GXPosition3f32(a->pos.X, y, a->pos.Z);
            GXColor4u8(c.r, c.g, c.b, c.a);
            GXPosition3f32(a->pos.X + stc_joint_cos[s] * r, y, a->pos.Z + stc_joint_sin[s] * r);
            GXColor4u8(c.r, c.g, c.b, c.a);
            GXPosition3f32(a->pos.X + stc_joint_cos[s + 1] * r, y,
                           a->pos.Z + stc_joint_sin[s + 1] * r);
            GXColor4u8(c.r, c.g, c.b, c.a);
        }
    }
}

// GX callback on the world camera link: an earthen lip, then the black hole over it.
// It also runs under the match pause, which freezes the tick but not an SG 1 sound, so
// it silences the rumble bed there.
static void Earthquake_GX(GOBJ *g, int pass)
{
    (void)g;
    if (stc_bed_live && !stc_hsd_update->is_running)
        FGM_SetVolume(stc_bed, 0);
    if (pass != GXPASS_TEXEDGE || !stc_open)
        return;

    COBJ *cam = COBJ_GetCurrent();
    if (!cam)
        return;

    if (!stc_joint_seeded)
    {
        for (int s = 0; s <= CRACK_JOINT_SEGS; s++)
        {
            float ang = 2.0f * M_PI * (float)s / (float)CRACK_JOINT_SEGS;
            stc_joint_cos[s] = cosf(ang);
            stc_joint_sin[s] = sinf(ang);
        }
        stc_joint_seeded = 1;
    }

    GX_BeginXlu(cam, RENDER_VERTEX, GX_BL_INVSRCALPHA);
    for (int l = 0; l < stc_line_count; l++)
        CrackDrawShape(&stc_lines[l], CRACK_RIM_SCALE, CRACK_RIM_PAD, crack_rim_color);
    for (int l = 0; l < stc_line_count; l++)
        CrackDrawShape(&stc_lines[l], 1.0f, 0.0f, crack_core_color);
    HSD_StateInvalidate(HSD_STATE_ALL);
}

// The rumble's envelope: in over the quake's head, out over its tail.
static float QuakeEnvelope(void)
{
    float in = Clamp01((float)stc_elapsed / (float)QUAKE_RAMP_IN);
    float out = Clamp01((float)(stc_duration - stc_elapsed) / (float)QUAKE_RAMP_OUT);
    return (in < out) ? in : out;
}

// Every view slides back and forth along its right axis with a little vertical jolt, so
// the whole map appears to rock. The jitter comes from the frame count, so the shake draws
// nothing from the game RNG.
static void QuakeShake(void)
{
    float amp = QUAKE_SHAKE_AMP * stc_intensity * stc_shake;
    if (!stc_shake_src && amp <= 0.0f)
        return;

    float t = (float)stc_elapsed;
    Weather_ShakeSet(&stc_shake_src, NULL, QUAKE_SHAKE_PRIORITY,
                     amp * (0.75f * sinf(t * 0.95f) + 0.25f * sinf(t * 2.6f)),
                     amp * 0.3f * sinf(t * 1.9f));
}

// Enemy_RumblePlayer goes through Rider_TriggerRumble, so the player's own rumble option
// applies and CPUs never rumble.
static void QuakeRumble(int kind, int duration)
{
    if (!rumble_index)
        return;
    for (int ply = 0; ply < PLY_NUM; ply++)
        Enemy_RumblePlayer(ply, kind, duration);
}

static void QuakeRumblePulse(void)
{
    if (stc_elapsed % QUAKE_RUMBLE_GAP != 0)
        return;
    int n = GetElementsIn(quake_rumble_kinds);
    int i = stc_elapsed * n / stc_duration;
    if (i >= n)
        i = n - 1;
    QuakeRumble(quake_rumble_kinds[i], QUAKE_RUMBLE_LEN);
}

static void QuakeBed(void)
{
    int vol = (int)(QUAKE_BED_VOLUME * stc_shake);
    if (stc_bed_live)
    {
        FGM_SetVolume(stc_bed, vol);
        return;
    }
    if (vol <= 0)
        return;
    stc_bed = SFX_PlayRaw(SFX_QUAKE_BED, vol, 0x7f, 0, 1);
    stc_bed_live = (stc_bed != (FGMInstance)-1);
}

static void QuakeBedStop(void)
{
    if (stc_bed_live)
        FGM_Stop(stc_bed);
    stc_bed_live = 0;
}

static void QuakeAtSeeds(int sfx, int burst)
{
    for (int l = 0; l < stc_line_count; l++)
    {
        if (stc_lines[l].seed < 0)
            continue;
        Vec3 pos = stc_verts[stc_lines[l].seed].pos;
        Weather_PlaySoundAt(sfx, &pos);
        if (!burst)
            continue;
        pos.Y -= QUAKE_BURST_DROP;
        Weather_SpawnParticle(EFFECT_ID_GROUND_BURST, &pos, QUAKE_BURST_SCALE);
        Weather_SpawnParticle(EFFECT_ID_GROUND_BURST_ALT, &pos, QUAKE_BURST_SCALE);
    }
}

// A puff of dust somewhere along an open edge.
static void QuakePuff(void)
{
    if (stc_vert_count <= 0)
        return;
    const CrackVert *v = &stc_verts[HSD_Randi(stc_vert_count)];
    if (v->cur <= 0.0f)
        return;
    Vec3 pos = v->pos;
    pos.X += Weather_Randf2() * v->cur;
    pos.Z += Weather_Randf2() * v->cur;
    Weather_SpawnParticle(EFFECT_ID_DUST_PUFF, &pos, QUAKE_PUFF_SCALE);
}

// The tear's crash and dust as the cracks start to open, the crack sound as they start to
// close, and puffs along the edges while either moves.
static void QuakeCrackFx(void)
{
    int left = stc_duration - stc_elapsed;
    if (stc_elapsed == QUAKE_TEAR_DELAY)
        QuakeAtSeeds(SFX_QUAKE_TEAR, 1);
    else if (left == QUAKE_CLOSE_FRAMES)
        QuakeAtSeeds(SFX_QUAKE_CLOSE, 0);

    int tearing = stc_elapsed >= QUAKE_TEAR_DELAY
               && stc_elapsed < QUAKE_TEAR_DELAY + QUAKE_TEAR_FRAMES;
    if (tearing && stc_elapsed % QUAKE_PUFF_GAP_TEAR == 0)
        QuakePuff();
    else if (left < QUAKE_CLOSE_FRAMES && stc_elapsed % QUAKE_PUFF_GAP_CLOSE == 0)
        QuakePuff();
}

static void QuakeStart(void)
{
    stc_vert_count = 0;
    stc_line_count = 0;
    stc_cracks_tried = 0;
    stc_cracks_made = 0;
    stc_tear_len = 0.0f;
    stc_live = 1;
    stc_elapsed = 0;
    stc_shake = 0.0f;
    stc_open = 0;
    WeatherGX_EnsureLayer(&stc_gx_gobj, Earthquake_GX, "Earthquake");
    OSReport("[Earthquake] Quake %d/%d started, %d frames\n",
             stc_schedule.next, stc_schedule.num, stc_duration);
}

// Each crack costs a few dozen ground casts, so they are grown one per frame while
// the rumble builds, all before the tear begins.
static void QuakeGrowNextCrack(StageNode *sn)
{
    if (stc_cracks_tried >= stc_cracks)
        return;

    int first = stc_vert_count;
    stc_cracks_made += CrackGenerate(sn);
    for (int i = first; i < stc_vert_count; i++)
    {
        if (stc_verts[i].d > stc_tear_len)
            stc_tear_len = stc_verts[i].d;
    }

    if (++stc_cracks_tried == stc_cracks)
        OSReport("[Earthquake] Opened %d/%d cracks (%d lines, %d vertices)\n",
                 stc_cracks_made, stc_cracks, stc_line_count, stc_vert_count);
}

static void QuakeEnd(void)
{
    Weather_ShakeStop(&stc_shake_src);
    QuakeBedStop();
    stc_live = 0;
    stc_open = 0;
    stc_shake = 0.0f;
    stc_line_count = 0;
    stc_vert_count = 0;
}

// A new preset mid-round - a legendary machine's sky - ends a live quake, closing its
// cracks at once, and keeps the round's schedule.
void Earthquake_SetActive(const EarthquakeDef *def)
{
    if (stc_live)
        QuakeEnd();

    stc_active = WeatherToggle(show_index, def && def->enabled);

    int def_count = (def && def->count > 0) ? def->count : QUAKE_DEF_COUNT;
    stc_count = (count_index > 0) ? count_values[count_index] : def_count;

    int def_duration = (def && def->duration > 0) ? def->duration : QUAKE_DEF_DURATION;
    stc_duration = (int)(def_duration * duration_factors[duration_index]);
    int min_duration = QUAKE_TEAR_DELAY + QUAKE_TEAR_FRAMES + QUAKE_CLOSE_FRAMES;
    if (stc_duration < min_duration)
        stc_duration = min_duration;

    int def_cracks = (def && def->cracks > 0) ? def->cracks : QUAKE_DEF_CRACKS;
    stc_cracks = (cracks_index > 0) ? cracks_values[cracks_index] : def_cracks;
    if (stc_cracks > CRACK_MAX_CRACKS)
        stc_cracks = CRACK_MAX_CRACKS;

    float def_size = (def && def->size > 0.0f) ? def->size : QUAKE_DEF_SIZE;
    stc_size = def_size * size_factors[size_index] * cracks_size[cracks_index];

    float def_intensity = (def && def->intensity > 0.0f) ? def->intensity : QUAKE_DEF_INTENSITY;
    stc_intensity = def_intensity * shake_factors[shake_index];
}

void Earthquake_Tick(void)
{
    if (!stc_active)
        return;

    StageNode *sn = Weather_StageNode();
    if (!sn)
        return;

    float p = Weather_RoundProgress();
    if (p < 0.0f)
        return;

    if (stc_live)
    {
        if (++stc_elapsed >= stc_duration)
        {
            OSReport("[Earthquake] Quake settled\n");
            QuakeRumble(QUAKE_RUMBLE_SETTLE, 0);
            QuakeEnd();
            return;
        }
        stc_shake = QuakeEnvelope();
        QuakeGrowNextCrack(sn);
        CrackUpdateWidths();
        stc_round_cracked |= stc_open;
        QuakeShake();
        QuakeBed();
        QuakeRumblePulse();
        QuakeCrackFx();
        WeatherGX_EnsureLayer(&stc_gx_gobj, Earthquake_GX, "Earthquake");
        return;
    }

    if (WeatherSchedule_Due(&stc_schedule, stc_count, p))
        QuakeStart();
}

// Quake_Init wipes the source pool at every 3D load, so a held source is dropped rather
// than removed. The bed is stopped so it cannot outlive the round.
void Earthquake_Reset(void)
{
    stc_active = 0;
    stc_schedule.planned = 0;
    stc_shake_src = 0;
    QuakeEnd();
    stc_elapsed = 0;
    stc_round_cracked = 0;
    stc_gx_gobj = NULL;
}

void Earthquake_OnBoot(void)
{
    CODEPATCH_HOOKAPPLY(0x800d9938);
    CODEPATCH_HOOKAPPLY(0x80244ea0);
    CODEPATCH_HOOKAPPLY(0x801ee7d0);
    CODEPATCH_HOOKAPPLY(0x801f8a68);
    CODEPATCH_HOOKAPPLY(0x80254d94);
    CODEPATCH_HOOKAPPLY(0x80255148);
    CODEPATCH_HOOKAPPLY(0x80255484);
    CODEPATCH_HOOKAPPLY(0x80255dfc);
    CODEPATCH_HOOKAPPLY(0x800100f4);
    OSReport("[Earthquake] Hooks installed\n");
}

static void OnQuakeShowChange(int val)
{
    OSReport("[Earthquake] Earthquake %s\n", weather_toggle_names[val]);
}

static void OnQuakeCountChange(int val)
{
    OSReport("[Earthquake] Quakes %s\n", count_names[val]);
}

static void OnQuakeDurationChange(int val)
{
    OSReport("[Earthquake] Duration %s\n", duration_names[val]);
}

static void OnQuakeCracksChange(int val)
{
    OSReport("[Earthquake] Cracks %s\n", cracks_names[val]);
}

static void OnQuakeSizeChange(int val)
{
    OSReport("[Earthquake] Crack size %s\n", size_names[val]);
}

static void OnQuakeShakeChange(int val)
{
    OSReport("[Earthquake] Shake %s\n", shake_names[val]);
}

static void OnQuakeRumbleChange(int val)
{
    OSReport("[Earthquake] Controller rumble %s\n", weather_onoff_names[val]);
}

MenuDesc earthquake_menu = {
    .option_num = 7,
    .options = {
        &(OptionDesc){
            .name = "Earthquake",
            .description = "Let earthquakes crack open City Trial: Preset = only presets that set it, Off = never, On = all presets",
            .kind = OPTKIND_VALUE,
            .val = &show_index,
            .value_num = WEATHER_TOGGLE_NUM,
            .value_names = weather_toggle_names,
            .on_change = OnQuakeShowChange,
        },
        &(OptionDesc){
            .name = "Quakes",
            .description = "How many earthquakes strike over a City Trial round, spread across the match timer",
            .kind = OPTKIND_VALUE,
            .val = &count_index,
            .value_num = GetElementsIn(count_values),
            .value_names = count_names,
            .on_change = OnQuakeCountChange,
        },
        &(OptionDesc){
            .name = "Duration",
            .description = "How long each quake shakes and how long its cracks stay open",
            .kind = OPTKIND_VALUE,
            .val = &duration_index,
            .value_num = GetElementsIn(duration_factors),
            .value_names = duration_names,
            .on_change = OnQuakeDurationChange,
        },
        &(OptionDesc){
            .name = "Cracks",
            .description = "How many cracks each quake tears open (One Big = a single crack twice the size)",
            .kind = OPTKIND_VALUE,
            .val = &cracks_index,
            .value_num = GetElementsIn(cracks_values),
            .value_names = cracks_names,
            .on_change = OnQuakeCracksChange,
        },
        &(OptionDesc){
            .name = "Crack Size",
            .description = "How long and wide the cracks grow",
            .kind = OPTKIND_VALUE,
            .val = &size_index,
            .value_num = GetElementsIn(size_factors),
            .value_names = size_names,
            .on_change = OnQuakeSizeChange,
        },
        &(OptionDesc){
            .name = "Screen Shake",
            .description = "How hard every view rocks while the ground quakes",
            .kind = OPTKIND_VALUE,
            .val = &shake_index,
            .value_num = GetElementsIn(shake_factors),
            .value_names = shake_names,
            .on_change = OnQuakeShakeChange,
        },
        &(OptionDesc){
            .name = "Controller Rumble",
            .description = "Rumble every player's controller while the ground quakes",
            .kind = OPTKIND_VALUE,
            .val = &rumble_index,
            .value_num = WEATHER_ONOFF_NUM,
            .value_names = weather_onoff_names,
            .on_change = OnQuakeRumbleChange,
        },
    },
};
