#include <string.h>

#include "game.h"
#include "os.h"
#include "hsd.h"
#include "gx.h"
#include "obj.h"
#include "stage.h"
#include "collision.h"
#include "machine.h"
#include "rider.h"
#include "item.h"
#include "inline.h"
#include "code_patch/code_patch.h"

#include "custom_events.h"
#include "event_planet.h"

// Resting on the three prongs atop the central tower, with the top well under the lid
// at Y 1040.
#define PLANET_CENTER_X (-60.0f)
#define PLANET_CENTER_Y 710.0f
#define PLANET_CENTER_Z 0.0f
#define PLANET_RADIUS 180.0f
#define PLANET_FIELD_RADIUS 280.0f // gravity points at the center inside this
#define PLANET_FIELD_BLEND 50.0f   // outer band where the stage's down turns toward the planet's

// Airborne ridden machines feel the planet much further out than everything else, so a
// jump or flight near it ends on its surface without turning the city's ground or cameras.
#define PLANET_PULL_RADIUS 600.0f
#define PLANET_PULL_BLEND 100.0f

// Facets sag up to ~0.5 below the true radius, so the core starts a little deeper.
// Inside it gravity points out and bodies are lifted to the surface: the triangles are
// one-sided, and nothing else would push a body out.
#define PLANET_CORE_RADIUS (PLANET_RADIUS - 3.0f)
#define PLANET_EJECT_CLEARANCE 8.0f

// Icosahedron with each face cut into FREQ^2 triangles, one collision record per face
// so the moving pass's record box test culls most of the sphere for each query.
#define PLANET_FREQ 8
#define PLANET_FACES 20
#define PLANET_FACE_VTX ((PLANET_FREQ + 1) * (PLANET_FREQ + 2) / 2)
#define PLANET_FACE_TRI (PLANET_FREQ * PLANET_FREQ)
#define PLANET_VTX_NUM (PLANET_FACES * PLANET_FACE_VTX)
#define PLANET_TRI_NUM (PLANET_FACES * PLANET_FACE_TRI)

// City Trial's main island ground, for matching traction, sounds and dust.
#define PLANET_TRI_KIND (GRCOLL_KIND_UNDER | (GRCOLL_GROUNDTYPE_ASPHALT << GRCOLL_KIND_GROUNDTYPE_SHIFT))
#define PLANET_TRI_OFF GRCOLL_STATE_SURFACE_PARAM
#define PLANET_TRI_ON (GRCOLL_STATE_SURFACE_PARAM | GRCOLL_STATE_COLLIDABLE)
#define PLANET_PARKED_Y (-100000.0f) // record box while off, so the box test rejects it first

#define PLANET_GROW_FRAMES 90
#define PLANET_SHRINK_FRAMES 30

// Suction of resting items and parked machines in a column under the planet.
#define PLANET_CAPTURE_RADIUS 600.0f // horizontal, from the planet's axis
#define PLANET_CLAIM_GAP 10
#define PLANET_MAX_CLAIMS 16
#define PLANET_LIFT_ACCEL 0.04f
#define PLANET_LIFT_SPEED 4.0f
#define PLANET_LIFT_TIMEOUT 600
#define PLANET_RELEASE_RADIUS (PLANET_FIELD_RADIUS - PLANET_FIELD_BLEND)
#define PLANET_TUMBLE 0.08f

#define PLANET_GOBJ_CLASS 213
#define PLANET_PROC_PRI 2 // after the priority-0 displacement clear, before the priority-4 physics
#define PLANET_GX_LINK 0  // world camera

// Gr_GetDownVector's own threshold for "no zone here" and its no-StageNode fallback.
#define GRAVITY_ZONE_EPS 1e-05f
#define GRAVITY_DEFAULT_STRENGTH 0.025f

// grColl_Alloc (0x800d6dcc)'s bl grColl_CountArrays for the terrain node and its
// closing bl grColl_Build for the terrain.
#define COLL_ALLOC_COUNT_CALL 0x800d6e4c
#define COLL_ALLOC_BUILD_CALL 0x800d7044

typedef enum PlanetPhase
{
    PLANET_OFF,
    PLANET_GROW,
    PLANET_SOLID,
    PLANET_SHRINK,
} PlanetPhase;

typedef struct PlanetClaim
{
    GOBJ *gobj;
    int is_machine;
    int age;
    float speed;
} PlanetClaim;

static const Vec3 planet_center = {PLANET_CENTER_X, PLANET_CENTER_Y, PLANET_CENTER_Z};

static Vec3 unit_vtx[PLANET_VTX_NUM];
static GXColor vtx_color[PLANET_VTX_NUM];
static u16 tri_vtx[PLANET_TRI_NUM][3]; // wound so the engine's normal faces out

// This stage's slices of the collision pool, NULL off City Trial.
static GrCollTri *planet_tris;
static GrCollRecord *planet_records;
static Vec3 record_center[PLANET_FACES];
static Vec3 record_half[PLANET_FACES];

static PlanetPhase phase;
static int phase_timer;
static int field_on;
static float draw_scale;
static GOBJ *planet_gobj;
static PlanetClaim claims[PLANET_MAX_CLAIMS];
static int claim_num;
static int pulled_items;
static int pulled_machines;

static void Planet_Nlerp(Vec3 *a, Vec3 *b, float t, Vec3 *out)
{
    out->X = lerp(a->X, b->X, t);
    out->Y = lerp(a->Y, b->Y, t);
    out->Z = lerp(a->Z, b->Z, t);
    VECNormalize(out, out);
}

// Turns unit a toward unit b by t through their halfway direction, so each half spans at
// most 90 degrees and the blend never passes through a zero vector, even for opposite
// directions straight under the planet.
static void Planet_TurnToward(Vec3 *a, Vec3 *b, float t, Vec3 *out)
{
    Vec3 mid = {a->X + b->X, a->Y + b->Y, a->Z + b->Z};
    if (VECSquareMag(&mid) < 1e-6f)
    {
        Vec3 axis = {0.0f, 0.0f, 0.0f};
        if (a->Y > -0.9f && a->Y < 0.9f)
            axis.Y = 1.0f;
        else
            axis.X = 1.0f;
        VECCrossProduct(a, &axis, &mid);
    }
    VECNormalize(&mid, &mid);

    if (t < 0.5f)
        Planet_Nlerp(a, &mid, t * 2.0f, out);
    else
        Planet_Nlerp(&mid, b, t * 2.0f - 1.0f, out);
}

// Distance from the planet's center, with the unit direction out from it.
static float Planet_Radial(const Vec3 *pos, Vec3 *out_dir)
{
    Vec3 d = {pos->X - planet_center.X, pos->Y - planet_center.Y, pos->Z - planet_center.Z};
    float dist = sqrtf(VECDotProduct(&d, &d));
    if (dist < 1e-3f)
    {
        *out_dir = (Vec3){0.0f, 1.0f, 0.0f};
        return dist;
    }
    out_dir->X = d.X / dist;
    out_dir->Y = d.Y / dist;
    out_dir->Z = d.Z / dist;
    return dist;
}

// Gr_GetDownVector's vanilla body: point zones, spline zones, then the stage's own.
static float Planet_StageDownVector(Vec3 *pos, Vec3 *out)
{
    GrObj *gr = *stc_grobj;
    StageNode *sn = gr->gr_data->stage_node;

    float s = grGravity_GetPointZoneDown(gr, pos, out);
    if (s >= GRAVITY_ZONE_EPS || s <= -GRAVITY_ZONE_EPS)
        return s;
    s = grGravity_GetSplineZoneDown(gr, pos, out);
    if (s >= GRAVITY_ZONE_EPS || s <= -GRAVITY_ZONE_EPS)
        return s;

    if (!sn)
    {
        *out = (Vec3){0.0f, 1.0f, 0.0f};
        return GRAVITY_DEFAULT_STRENGTH;
    }
    *out = sn->gravity_dir;
    return sn->gravity_strength;
}

// Replaces Gr_GetDownVector, the one source of down for machines, riders,
// items, enemies, weapons and the camera. Strength stays the stage's.
static float Planet_GetDownVector(Vec3 *pos, Vec3 *out)
{
    float strength = Planet_StageDownVector(pos, out);
    if (!field_on)
        return strength;

    Vec3 radial;
    float dist = Planet_Radial(pos, &radial);
    if (dist >= PLANET_FIELD_RADIUS)
        return strength;

    Vec3 down = radial;
    if (dist >= PLANET_CORE_RADIUS)
    {
        down.X = -radial.X;
        down.Y = -radial.Y;
        down.Z = -radial.Z;
    }

    float t = (PLANET_FIELD_RADIUS - dist) / PLANET_FIELD_BLEND;
    Vec3 stage = *out;
    if (t >= 1.0f)
        *out = down;
    else
        Planet_TurnToward(&stage, &down, t, out);
    return strength;
}

static int Machine_FeelsPull(MachineData *md)
{
    return field_on && md->rider_gobj && md->is_airborne && !md->is_on_rail && !md->is_dead && !md->is_fall_dead;
}

// Replaces Machine_RefreshGravity: the vanilla refresh, with the planet's down out to
// the pull radius for an airborne ridden machine.
static void Planet_MachineRefreshGravity(MachineData *md)
{
    if (md->xc3a & MACHINE_XC3A_ASLEEP)
        return;

    md->gravity_strength = Gr_GetDownVector(&md->pos, &md->gravity_down);
    if (Machine_FeelsPull(md))
    {
        Vec3 radial;
        float dist = Planet_Radial(&md->pos, &radial);
        if (dist >= PLANET_CORE_RADIUS && dist < PLANET_PULL_RADIUS)
        {
            Vec3 down = {-radial.X, -radial.Y, -radial.Z};
            float t = (PLANET_PULL_RADIUS - dist) / PLANET_PULL_BLEND;
            Vec3 stage = md->gravity_down;
            if (t >= 1.0f)
                md->gravity_down = down;
            else
                Planet_TurnToward(&stage, &down, t, &md->gravity_down);
        }
    }
    md->gravity_up.X = -md->gravity_down.X;
    md->gravity_up.Y = -md->gravity_down.Y;
    md->gravity_up.Z = -md->gravity_down.Z;
}

static float Planet_Terrain(const Vec3 *p)
{
    return sinf(p->X * 3.1f + 1.3f) * sinf(p->Y * 2.7f + 0.4f) * sinf(p->Z * 3.4f + 2.2f) +
           0.45f * sinf(p->X * 7.3f + p->Z * 5.1f + 0.7f) * sinf(p->Y * 6.1f + 1.9f);
}

// Ocean, beaches, grassland and ice caps from a few octaves of sines over the unit
// sphere, lit once from a fixed sun since the planet never turns.
static GXColor Planet_ShadeVertex(Vec3 *p)
{
    static const GXColor ocean = {46, 92, 196, 255};
    static const GXColor shallows = {72, 150, 214, 255};
    static const GXColor sand = {226, 208, 140, 255};
    static const GXColor grass = {96, 178, 82, 255};
    static const GXColor forest = {58, 128, 64, 255};
    static const GXColor ice = {236, 244, 250, 255};
    Vec3 sun = {0.36f, 0.80f, 0.48f};

    float h = Planet_Terrain(p);
    GXColor c;
    if (p->Y > 0.86f || p->Y < -0.86f)
        c = ice;
    else if (h < -0.05f)
        c = ocean;
    else if (h < 0.05f)
        c = shallows;
    else if (h < 0.12f)
        c = sand;
    else if (h < 0.40f)
        c = grass;
    else
        c = forest;

    float lit = VECDotProduct(p, &sun);
    float shade = 0.55f + 0.45f * (lit > 0.0f ? lit : 0.0f);
    c.r = (u8)(c.r * shade);
    c.g = (u8)(c.g * shade);
    c.b = (u8)(c.b * shade);
    return c;
}

static int FaceVertex(int face, int i, int j)
{
    return face * PLANET_FACE_VTX + i * (PLANET_FREQ + 1) - i * (i - 1) / 2 + j;
}

// The engine's normal is (v2 - v0) x (v1 - v0), so a triangle whose normal comes out
// pointing inward is flipped.
static void AddTri(int *n, int a, int b, int c)
{
    const Vec3 *v0 = &unit_vtx[a], *v1 = &unit_vtx[b], *v2 = &unit_vtx[c];
    Vec3 e1 = {v2->X - v0->X, v2->Y - v0->Y, v2->Z - v0->Z};
    Vec3 e2 = {v1->X - v0->X, v1->Y - v0->Y, v1->Z - v0->Z};
    Vec3 nrm;
    VECCrossProduct(&e1, &e2, &nrm);
    Vec3 mid = {v0->X + v1->X + v2->X, v0->Y + v1->Y + v2->Y, v0->Z + v1->Z + v2->Z};

    int outward = VECDotProduct(&nrm, &mid) >= 0.0f;
    tri_vtx[*n][0] = a;
    tri_vtx[*n][1] = outward ? b : c;
    tri_vtx[*n][2] = outward ? c : b;
    (*n)++;
}

static void Planet_BuildUnitMesh(void)
{
    static const float t = 1.618034f;
    static const Vec3 ico[12] = {
        {-1, t, 0}, {1, t, 0}, {-1, -t, 0}, {1, -t, 0},
        {0, -1, t}, {0, 1, t}, {0, -1, -t}, {0, 1, -t},
        {t, 0, -1}, {t, 0, 1}, {-t, 0, -1}, {-t, 0, 1},
    };
    static const u8 faces[PLANET_FACES][3] = {
        {0, 11, 5}, {0, 5, 1}, {0, 1, 7}, {0, 7, 10}, {0, 10, 11},
        {1, 5, 9}, {5, 11, 4}, {11, 10, 2}, {10, 7, 6}, {7, 1, 8},
        {3, 9, 4}, {3, 4, 2}, {3, 2, 6}, {3, 6, 8}, {3, 8, 9},
        {4, 9, 5}, {2, 4, 11}, {6, 2, 10}, {8, 6, 7}, {9, 8, 1},
    };

    int tri = 0;
    for (int f = 0; f < PLANET_FACES; f++)
    {
        const Vec3 *A = &ico[faces[f][0]], *B = &ico[faces[f][1]], *C = &ico[faces[f][2]];
        for (int i = 0; i <= PLANET_FREQ; i++)
        {
            for (int j = 0; j <= PLANET_FREQ - i; j++)
            {
                float u = (float)i / PLANET_FREQ, v = (float)j / PLANET_FREQ;
                Vec3 *p = &unit_vtx[FaceVertex(f, i, j)];
                p->X = A->X + (B->X - A->X) * u + (C->X - A->X) * v;
                p->Y = A->Y + (B->Y - A->Y) * u + (C->Y - A->Y) * v;
                p->Z = A->Z + (B->Z - A->Z) * u + (C->Z - A->Z) * v;
                VECNormalize(p, p);
                vtx_color[FaceVertex(f, i, j)] = Planet_ShadeVertex(p);
            }
        }

        for (int i = 0; i < PLANET_FREQ; i++)
        {
            for (int j = 0; j < PLANET_FREQ - i; j++)
            {
                AddTri(&tri, FaceVertex(f, i, j), FaceVertex(f, i + 1, j), FaceVertex(f, i, j + 1));
                if (j < PLANET_FREQ - 1 - i)
                    AddTri(&tri, FaceVertex(f, i + 1, j), FaceVertex(f, i + 1, j + 1), FaceVertex(f, i, j + 1));
            }
        }
    }
}

static void Planet_BoundsGrow(Vec3 *lo, Vec3 *hi, const Vec3 *p)
{
    if (p->X < lo->X) lo->X = p->X;
    if (p->Y < lo->Y) lo->Y = p->Y;
    if (p->Z < lo->Z) lo->Z = p->Z;
    if (p->X > hi->X) hi->X = p->X;
    if (p->Y > hi->Y) hi->Y = p->Y;
    if (p->Z > hi->Z) hi->Z = p->Z;
}

static void Planet_BoundsToBox(const Vec3 *lo, const Vec3 *hi, Vec3 *center, Vec3 *half)
{
    center->X = (lo->X + hi->X) * 0.5f;
    center->Y = (lo->Y + hi->Y) * 0.5f;
    center->Z = (lo->Z + hi->Z) * 0.5f;
    half->X = (hi->X - lo->X) * 0.5f;
    half->Y = (hi->Y - lo->Y) * 0.5f;
    half->Z = (hi->Z - lo->Z) * 0.5f;
}

// Off: every triangle non-collidable and every record box parked out of reach. The counts
// and the moving_record entries stay, so later prop windows and cached ids stay valid.
static void Planet_SetSolid(int solid)
{
    if (!planet_tris)
        return;

    for (int t = 0; t < PLANET_TRI_NUM; t++)
        planet_tris[t].state = solid ? PLANET_TRI_ON : PLANET_TRI_OFF;

    for (int f = 0; f < PLANET_FACES; f++)
    {
        GrCollRecord *rec = &planet_records[f];
        if (solid)
        {
            rec->aabb_center = record_center[f];
            rec->aabb_half = record_half[f];
        }
        else
        {
            rec->aabb_center = (Vec3){0.0f, PLANET_PARKED_Y, 0.0f};
            rec->aabb_half = (Vec3){0.0f, 0.0f, 0.0f};
        }
    }
}

// Appends the sphere to the pool at the fill position, as static jobj-less records that
// only the moving pass reaches; no rebake window covers them.
static void Planet_AppendCollision(GrCollParam *coll, GrCollParam *max)
{
    if (coll->vtx_num + PLANET_VTX_NUM > max->vtx_num || coll->tri_num + PLANET_TRI_NUM > max->tri_num ||
        coll->record_num + PLANET_FACES > max->record_num ||
        coll->moving_record_num + PLANET_FACES > max->moving_record_num)
    {
        OSReport("[Planet] Collision pool has no room reserved, planet disabled\n");
        return;
    }

    GrCollVtx *vtx = coll->vtx + coll->vtx_num;
    GrCollTri *tris = coll->tri + coll->tri_num;
    GrCollRecord *records = coll->record + coll->record_num;
    coll->vtx_num += PLANET_VTX_NUM;
    coll->tri_num += PLANET_TRI_NUM;
    coll->record_num += PLANET_FACES;

    for (int v = 0; v < PLANET_VTX_NUM; v++)
    {
        vtx[v].pos.X = planet_center.X + unit_vtx[v].X * PLANET_RADIUS;
        vtx[v].pos.Y = planet_center.Y + unit_vtx[v].Y * PLANET_RADIUS;
        vtx[v].pos.Z = planet_center.Z + unit_vtx[v].Z * PLANET_RADIUS;
        vtx[v].prev = vtx[v].pos;
    }

    for (int f = 0; f < PLANET_FACES; f++)
    {
        GrCollRecord *rec = &records[f];
        memset(rec, 0, sizeof(*rec));
        rec->vtx = vtx + f * PLANET_FACE_VTX;
        rec->vtx_num = PLANET_FACE_VTX;
        rec->tri_begin = tris + f * PLANET_FACE_TRI;
        rec->tri_num = PLANET_FACE_TRI;
        PSMTXIdentity(&rec->world);
        PSMTXIdentity(&rec->prev_inv);

        Vec3 rec_lo = {1e9f, 1e9f, 1e9f}, rec_hi = {-1e9f, -1e9f, -1e9f};
        for (int k = 0; k < PLANET_FACE_TRI; k++)
        {
            int t = f * PLANET_FACE_TRI + k;
            GrCollTri *tri = &tris[t];
            memset(tri, 0, sizeof(*tri));
            tri->v0 = &vtx[tri_vtx[t][0]].pos;
            tri->v1 = &vtx[tri_vtx[t][1]].pos;
            tri->v2 = &vtx[tri_vtx[t][2]].pos;

            Vec3 e1 = {tri->v2->X - tri->v0->X, tri->v2->Y - tri->v0->Y, tri->v2->Z - tri->v0->Z};
            Vec3 e2 = {tri->v1->X - tri->v0->X, tri->v1->Y - tri->v0->Y, tri->v1->Z - tri->v0->Z};
            VECCrossProduct(&e1, &e2, &tri->normal);
            VECNormalize(&tri->normal, &tri->normal);

            Vec3 lo = *tri->v0, hi = *tri->v0;
            Planet_BoundsGrow(&lo, &hi, tri->v1);
            Planet_BoundsGrow(&lo, &hi, tri->v2);
            Planet_BoundsToBox(&lo, &hi, &tri->aabb_center, &tri->aabb_half);
            Planet_BoundsGrow(&rec_lo, &rec_hi, &lo);
            Planet_BoundsGrow(&rec_lo, &rec_hi, &hi);

            tri->kind = PLANET_TRI_KIND;
            tri->record = rec;
        }
        Planet_BoundsToBox(&rec_lo, &rec_hi, &record_center[f], &record_half[f]);
        coll->moving_record[coll->moving_record_num++] = rec;
    }

    planet_tris = tris;
    planet_records = records;
    Planet_SetSolid(0);

    OSReport("[Planet] Reserved %d triangles in %d records\n", PLANET_TRI_NUM, PLANET_FACES);
}

// Only a City Trial round with events on can run the event.
static int Planet_StageHasRoom(void)
{
    return Gr_GetCurrentGrKind() == GR_CITY1 && Gm_IsEventsEnabled();
}

static void Planet_CountArrays(GrCollParam *max, GrCollisionNode *node)
{
    grColl_CountArrays(max, node);
    if (!Planet_StageHasRoom())
        return;

    max->vtx_num += PLANET_VTX_NUM;
    max->tri_num += PLANET_TRI_NUM;
    max->record_num += PLANET_FACES;
    max->moving_record_num += PLANET_FACES;
}

static void Planet_BuildTerrain(GrCollParam *coll, GrCollParam *max, GrCollParam *window,
                                GrCollisionNode *node, GrJoint **joints)
{
    grColl_Build(coll, max, window, node, joints);

    planet_tris = NULL;
    planet_records = NULL;
    if (Planet_StageHasRoom())
        Planet_AppendCollision(coll, max);
}

static int IsClaimed(GOBJ *g)
{
    for (int i = 0; i < claim_num; i++)
    {
        if (claims[i].gobj == g)
            return 1;
    }
    return 0;
}

static void Machine_EnterRiderlessAir(MachineData *md)
{
    if (md->is_bike)
        Machine_Wheel_EnterWaitJump(md);
    else
        Machine_Star_EnterWaitFly(md);
}

static int Machine_IsParked(MachineData *md)
{
    return md->rider_gobj == NULL && !md->is_dead && !md->is_fall_dead && !md->is_on_rail &&
           (md->status == VCSTATE_WAITSTOP || md->status == VCSTATE_WAITRUN);
}

// Hands an item to whatever gravity holds at its spot, upright against it, falling.
static void Planet_DropItem(GOBJ *g, ItemData *it)
{
    CityItem_UpdateGravity(it);
    it->vel = (Vec3){0.0f, 0.0f, 0.0f};
    it->coll_kind = ITEM_COLL_POINT;
    it->point_coll.raycast_idx = -1;

    Vec3 up = {-it->fall_dir.X, -it->fall_dir.Y, -it->fall_dir.Z};
    Vec3 side;
    VECCrossProduct(&up, &it->forward, &side);
    if (VECSquareMag(&side) > 1e-6f)
    {
        VECCrossProduct(&side, &up, &it->forward);
        VECNormalize(&it->forward, &it->forward);
        it->up = up;
    }

    Item_SetAirborne(it);
    ItemGObj_EnterFamilyFall(g);
}

static void Planet_Release(int i)
{
    PlanetClaim *c = &claims[i];
    if (!c->is_machine && CustomEvents_IsOnPLink(c->gobj, GAMEPLINK_ITEM))
        Planet_DropItem(c->gobj, (ItemData *)c->gobj->userdata);
    claims[i] = claims[--claim_num];
}

static void Planet_ReleaseAll(void)
{
    while (claim_num > 0)
        Planet_Release(claim_num - 1);
}

static int InCaptureColumn(const Vec3 *pos)
{
    float dx = pos->X - planet_center.X;
    float dz = pos->Z - planet_center.Z;
    if (dx * dx + dz * dz > PLANET_CAPTURE_RADIUS * PLANET_CAPTURE_RADIUS)
        return 0;
    Vec3 radial;
    return Planet_Radial(pos, &radial) > PLANET_FIELD_RADIUS;
}

// One new claim per call, picked uniformly from every eligible target.
static void Planet_ClaimOne(void)
{
    if (claim_num >= PLANET_MAX_CLAIMS)
        return;

    GOBJ *pick = NULL;
    int pick_machine = 0;
    int seen = 0;

    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_ITEM]; g; g = g->next)
    {
        ItemData *it = (ItemData *)g->userdata;
        if (it->is_airborne != 0 ||
            (it->flags_x35a & ITEM_X35A_PICKUP_BUSY) || !InCaptureColumn(&it->pos) || IsClaimed(g))
            continue;
        if (HSD_Randi(++seen) == 0)
        {
            pick = g;
            pick_machine = 0;
        }
    }

    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_MACHINE]; g; g = g->next)
    {
        MachineData *md = (MachineData *)g->userdata;
        if (!Machine_IsParked(md) ||
            !InCaptureColumn(&md->pos) || IsClaimed(g))
            continue;
        if (HSD_Randi(++seen) == 0)
        {
            pick = g;
            pick_machine = 1;
        }
    }

    if (!pick)
        return;

    PlanetClaim *c = &claims[claim_num++];
    c->gobj = pick;
    c->is_machine = pick_machine;
    c->age = 0;
    c->speed = 0.0f;
    if (pick_machine)
        Machine_EnterRiderlessAir((MachineData *)pick->userdata);
}

// Lifts a claim straight at the center until it is well inside the field, where the
// planet's own gravity takes over. An item is moved directly with its envcoll suppressed;
// a machine is moved through ext_disp, held airborne with its velocity zeroed.
static int Planet_Lift(PlanetClaim *c)
{
    if (c->is_machine)
    {
        if (!CustomEvents_IsOnPLink(c->gobj, GAMEPLINK_MACHINE))
            return 0;
        MachineData *md = (MachineData *)c->gobj->userdata;
        if (md->rider_gobj || md->is_dead || md->is_fall_dead || md->is_on_rail)
            return 0;
    }
    else if (!CustomEvents_IsOnPLink(c->gobj, GAMEPLINK_ITEM))
    {
        return 0;
    }

    Vec3 *pos = c->is_machine ? &((MachineData *)c->gobj->userdata)->pos : &((ItemData *)c->gobj->userdata)->pos;
    Vec3 out;
    float dist = Planet_Radial(pos, &out);
    if (dist < PLANET_RELEASE_RADIUS || ++c->age > PLANET_LIFT_TIMEOUT)
    {
        if (c->is_machine)
            pulled_machines++;
        else
            pulled_items++;
        return 0;
    }

    c->speed += PLANET_LIFT_ACCEL;
    if (c->speed > PLANET_LIFT_SPEED)
        c->speed = PLANET_LIFT_SPEED;
    Vec3 step = {-out.X * c->speed, -out.Y * c->speed, -out.Z * c->speed};

    if (c->is_machine)
    {
        MachineData *md = (MachineData *)c->gobj->userdata;
        if (md->status == VCSTATE_WAITSTOP || md->status == VCSTATE_WAITRUN)
            Machine_EnterRiderlessAir(md);
        md->ext_disp.X += step.X;
        md->ext_disp.Y += step.Y;
        md->ext_disp.Z += step.Z;
        md->velocity = (Vec3){0.0f, 0.0f, 0.0f};
        return 1;
    }

    ItemData *it = (ItemData *)c->gobj->userdata;
    it->pos.X += step.X;
    it->pos.Y += step.Y;
    it->pos.Z += step.Z;
    it->vel = (Vec3){0.0f, 0.0f, 0.0f};
    it->is_airborne = -1;

    Vec3 axis = {out.Z, 0.0f, -out.X};
    if (VECSquareMag(&axis) > 1e-6f)
    {
        VECNormalize(&axis, &axis);
        float spin = (((u32)c->gobj >> 4) & 1) ? PLANET_TUMBLE : -PLANET_TUMBLE;
        Vec3_RotateAboutUnitAxis(&it->forward, &axis, spin);
        Vec3_RotateAboutUnitAxis(&it->up, &axis, spin);
    }
    return 1;
}

static void Planet_UpdateClaims(void)
{
    for (int i = 0; i < claim_num;)
    {
        if (Planet_Lift(&claims[i]))
            i++;
        else
            Planet_Release(i);
    }
}

// How far out a body inside the core must move to clear the surface, 0 when outside.
static float Planet_EjectDistance(const Vec3 *pos, Vec3 *out_dir)
{
    float dist = Planet_Radial(pos, out_dir);
    if (dist >= PLANET_CORE_RADIUS)
        return 0.0f;
    return PLANET_RADIUS + PLANET_EJECT_CLEARANCE - dist;
}

static void Planet_EjectBodies(void)
{
    Vec3 dir;
    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_MACHINE]; g; g = g->next)
    {
        MachineData *md = (MachineData *)g->userdata;
        if (md->is_dead || md->is_fall_dead || md->is_on_rail)
            continue;
        float d = Planet_EjectDistance(&md->pos, &dir);
        if (d <= 0.0f || IsClaimed(g))
            continue;
        if (md->xc3a & MACHINE_XC3A_ASLEEP)
            Machine_EnterRiderlessAir(md);
        md->ext_disp.X += dir.X * d;
        md->ext_disp.Y += dir.Y * d;
        md->ext_disp.Z += dir.Z * d;
    }

    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_RIDER]; g; g = g->next)
    {
        RiderData *rd = (RiderData *)g->userdata;
        if (Rider_IsOnMachine(rd))
            continue;
        float d = Planet_EjectDistance(&rd->pos, &dir);
        if (d <= 0.0f)
            continue;
        rd->platform_disp.X += dir.X * d;
        rd->platform_disp.Y += dir.Y * d;
        rd->platform_disp.Z += dir.Z * d;
    }

    for (GOBJ *g = (*stc_gobj_lookup)[GAMEPLINK_ITEM]; g; g = g->next)
    {
        ItemData *it = (ItemData *)g->userdata;
        if (IsClaimed(g))
            continue;
        float d = Planet_EjectDistance(&it->pos, &dir);
        if (d <= 0.0f)
            continue;
        it->pos.X += dir.X * d;
        it->pos.Y += dir.Y * d;
        it->pos.Z += dir.Z * d;
        Planet_DropItem(g, it);
    }
}

static float EaseOutBack(float t)
{
    const float c1 = 1.70158f, c3 = c1 + 1.0f;
    float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

static void Planet_Think(GOBJ *g)
{
    (void)g;
    phase_timer++;

    switch (phase)
    {
    case PLANET_GROW:
        draw_scale = EaseOutBack((float)phase_timer / PLANET_GROW_FRAMES);
        if (phase_timer < PLANET_GROW_FRAMES)
            break;
        phase = PLANET_SOLID;
        phase_timer = 0;
        draw_scale = 1.0f;
        Planet_SetSolid(1);
        field_on = 1;
        Planet_EjectBodies();
        OSReport("[Planet] Surface and gravity on\n");
        break;

    case PLANET_SOLID:
        Planet_EjectBodies();
        if (phase_timer % PLANET_CLAIM_GAP == 0)
            Planet_ClaimOne();
        Planet_UpdateClaims();
        break;

    case PLANET_SHRINK:
    {
        float t = (float)phase_timer / PLANET_SHRINK_FRAMES;
        draw_scale = t >= 1.0f ? 0.0f : 1.0f - t * t;
        break;
    }

    default:
        break;
    }
}

// Unlit and opaque: the vertex colors carry the baked sun, the depth test and write
// let terrain and machines sort against it, and no culling keeps the winding moot.
// Drawn in TEXEDGE, after every link's opaque pass, so the sky cannot paint over it.
static void Planet_GX(GOBJ *g, int pass)
{
    (void)g;
    if (pass != GXPASS_TEXEDGE || draw_scale <= 0.0f)
        return;
    COBJ *cam = COBJ_GetCurrent();
    if (!cam)
        return;

    float s = PLANET_RADIUS * draw_scale;
    Mtx mv;
    for (int r = 0; r < 3; r++)
    {
        float *v = cam->view_mtx[r];
        mv[r][0] = v[0] * s;
        mv[r][1] = v[1] * s;
        mv[r][2] = v[2] * s;
        mv[r][3] = v[0] * planet_center.X + v[1] * planet_center.Y + v[2] * planet_center.Z + v[3];
    }

    HSD_StateInitDirect(GX_VTXFMT0, RENDER_VERTEX);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetNumTexGens(0);
    GXSetNumChans(1);
    GXSetChanCtrl(GX_COLOR0, GX_DISABLE, Vertex, Vertex, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetChanCtrl(GX_ALPHA0, GX_DISABLE, Vertex, Vertex, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetBlendMode(GX_BM_NONE, GX_BL_ONE, GX_BL_ZERO, GX_LO_CLEAR);
    GXSetZMode(GX_ENABLE, GX_LEQUAL, GX_ENABLE);
    GXSetCullMode(GX_CULL_NONE);
    GXLoadPosMtxImm(&mv, GX_PNMTX0);

    for (int f = 0; f < PLANET_FACES; f++)
    {
        GXBegin(GX_TRIANGLES, GX_VTXFMT0, PLANET_FACE_TRI * 3);
        for (int k = 0; k < PLANET_FACE_TRI; k++)
        {
            const u16 *idx = tri_vtx[f * PLANET_FACE_TRI + k];
            for (int e = 0; e < 3; e++)
            {
                const Vec3 *p = &unit_vtx[idx[e]];
                GXColor c = vtx_color[idx[e]];
                GXPosition3f32(p->X, p->Y, p->Z);
                GXColor4u8(c.r, c.g, c.b, 255);
            }
        }
    }
    HSD_StateInvalidate(HSD_STATE_ALL);
}

static void Planet_ResetState(void)
{
    field_on = 0;
    phase = PLANET_OFF;
    phase_timer = 0;
    draw_scale = 0.0f;
    planet_gobj = NULL;
    claim_num = 0;
}

void Planet_Start(void)
{
    Planet_ResetState();
    pulled_items = 0;
    pulled_machines = 0;

    if (!planet_tris)
    {
        OSReport("[Planet] No collision reserved on this stage, skipped\n");
        return;
    }

    planet_gobj = GObj_Create(PLANET_GOBJ_CLASS, GAMEPLINK_1, 0);
    GObj_AddProc(planet_gobj, Planet_Think, PLANET_PROC_PRI);
    GObj_AddGXLink(planet_gobj, Planet_GX, PLANET_GX_LINK, 0);
    phase = PLANET_GROW;

    OSReport("[Planet] Appeared at (%.0f, %.0f, %.0f), radius %.0f\n",
             planet_center.X, planet_center.Y, planet_center.Z, PLANET_RADIUS);
}

// Gravity and surface go at once; whatever stood on the planet falls back to the city
// through the engine's retired-ground paths. Claims still rising drop where they are.
void Planet_End(void)
{
    if (phase != PLANET_GROW && phase != PLANET_SOLID)
        return;

    field_on = 0;
    Planet_SetSolid(0);
    Planet_ReleaseAll();
    phase = PLANET_SHRINK;
    phase_timer = 0;
}

void Planet_End2(void)
{
    Planet_End();
    if (planet_gobj)
        GObj_Destroy(planet_gobj);
    Planet_ResetState();

    OSReport("[Planet] Gone after pulling in %d item(s) and %d machine(s)\n", pulled_items, pulled_machines);
}

void Planet_Abort(void)
{
    Planet_ResetState();
}

void Planet_InstallHooks(void)
{
    Planet_BuildUnitMesh();
    CODEPATCH_REPLACEFUNC(Gr_GetDownVector, Planet_GetDownVector);
    CODEPATCH_REPLACEFUNC(Machine_RefreshGravity, Planet_MachineRefreshGravity);
    CODEPATCH_REPLACECALL(COLL_ALLOC_COUNT_CALL, Planet_CountArrays);
    CODEPATCH_REPLACECALL(COLL_ALLOC_BUILD_CALL, Planet_BuildTerrain);
}
