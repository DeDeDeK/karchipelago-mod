#include <math.h>
#include <string.h>

#include "game.h"
#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "gx.h"
#include "item.h"
#include "machine.h"
#include "stage.h"
#include "collision.h"
#include "inline.h"
#include "audio.h"

#include "custom_events.h"
#include "event_sky_rings.h"
#include "road_spot.h"
#include "item_offer.h"

#define RING_GOBJ_CLASS    217
#define RING_GX_PRI        2 // after Map_GX (1), whose backdrop draws with no depth test
#define RING_COUNT         15
#define RING_TRIES         300
#define RING_SCALE         1.5f   // of the vanilla hoop
#define RING_INNER_RADIUS  9.26f  // the vanilla hoop's inner corners, world units
#define RING_OUTER_RADIUS  10.14f
#define RING_Y_MIN         56.0f  // the lowest vanilla ring
#define RING_Y_MAX         463.8f // the garden in the sky's roof
#define RING_SKY_TOP       600.0f // above every floor in the city
#define RING_FLOOR_GAP     25.0f
#define RING_MIN_GAP       80.0f
#define RING_VANILLA_GAP   50.0f
#define RING_APPROACH      50.0f // kept clear on each side of the opening
#define RING_EDGE_MARGIN   0.1f
#define RING_MAX_STEP      60.0f // a longer move in one frame is a respawn, not a flight
#define RING_REPASS_FRAMES 30

// City Trial's dash ring row of the GrCommon dash table.
#define RING_DASH_ACCEL   0.12f
#define RING_DASH_IMPULSE 0.05f
#define RING_DASH_FRAMES  40
#define RING_PASS_SFX     FGMCITY_DASHRING

#define RING_ITEM_SCALE     1.5f
#define RING_ITEM_RESPAWN   300
#define RING_ITEM_LIFETIME  4800 // outlasts the event, whose end expires what is left
#define RING_ITEM_HEADROOM  12
#define RING_ITEM_SPAWN_TYPE 0x534b // an ItemSpawnType no vanilla source uses, tagging the rings' items

// The terrain root's DObj holding the vanilla rings in its second POBJ, among other
// strips, and the position indices of the axis-aligned ring at (76.8, 56.0, -96.2),
// which opens along Z.
#define RING_SRC_DOBJ      55
#define RING_SRC_N_DISPLAY 398
#define RING_SRC_IDX_FIRST 3351
#define RING_SRC_IDX_LAST  3382
#define RING_VTX_SIZE      4 // u16 position index, RGBA4 color
#define RING_DL_SIZE       0x200

// The vanilla rings, which a new ring keeps clear of.
static const Vec3 vanilla_rings[] = {
    {91.4f, 144.9f, -294.3f},
    {105.3f, 145.1f, -265.1f},
    {16.7f, 127.3f, 174.5f},
    {9.5f, 318.8f, -222.0f},
    {180.8f, 330.8f, -183.0f},
    {136.5f, 59.3f, 133.0f},
    {-113.3f, 144.6f, 104.9f},
    {76.8f, 56.0f, -96.2f},
    {-113.3f, 153.6f, -197.8f},
};

static const ItemKind ring_items[] = {
    ITKIND_ACCEL,
    ITKIND_TOPSPEED,
    ITKIND_OFFENSE,
    ITKIND_DEFENSE,
    ITKIND_TURN,
    ITKIND_GLIDE,
    ITKIND_CHARGE,
    ITKIND_WEIGHT,
    ITKIND_HP,
    ITKIND_ALLUP,
};

typedef struct SkyRing
{
    Vec3 pos;
    Vec3 axis;
    GOBJ *item;     // waiting in the ring, NULL = none
    int item_serial;
    int respawn;    // frames until the next item
} SkyRing;

typedef struct RingRider
{
    MachineData *md; // the machine prev was read from
    Vec3 prev;
    int last_ring;
    int repass;      // frames last_ring ignores this rider
} RingRider;

static SkyRing rings[RING_COUNT];
static int ring_num;
static RingRider riders[PLY_NUM];
static GOBJ *ring_gobj;
static ItemKind offered[GetElementsIn(ring_items)];
static int offered_num;
static int pass_num;
static int item_num;

static u8 ring_dl[RING_DL_SIZE] __attribute__((aligned(32)));
static POBJDesc ring_pobj;
static HSD_DObjDesc ring_dobj = {.pobjdesc = &ring_pobj};
static JOBJDesc mesh_desc = {
    .flags = JOBJ_CLASSICAL_SCALING | JOBJ_OPA,
    .u.dobjdesc = &ring_dobj,
    .scale = {1.0f, 1.0f, 1.0f},
};
static JOBJDesc ring_descs[RING_COUNT];
static JOBJDesc root_desc = {
    .flags = JOBJ_CLASSICAL_SCALING | JOBJ_ROOT_MASK,
    .scale = {1.0f, 1.0f, 1.0f},
};

// The POBJ holding the vanilla rings, checked against its known layout.
static POBJDesc *SkyRings_FindSource(HSD_DObjDesc **dobj)
{
    GrObj *gr = *stc_grobj;
    if (!gr || !gr->joint_table || !gr->joint_table[0].desc)
        return NULL;

    HSD_DObjDesc *d = gr->joint_table[0].desc->u.dobjdesc;
    for (int i = 0; d && i < RING_SRC_DOBJ; i++)
        d = d->next;
    if (!d || !d->pobjdesc)
        return NULL;

    POBJDesc *p = ((POBJDesc *)d->pobjdesc)->next;
    if (!p || p->n_display != RING_SRC_N_DISPLAY || !p->verts)
        return NULL;

    HSD_VtxDescList *v = p->verts;
    if (v[0].attr != GX_VA_POS || v[0].attr_type != GX_INDEX16 || v[0].comp_type != GX_F32 ||
        v[1].attr != GX_VA_CLR0 || v[1].attr_type != GX_DIRECT || v[2].attr != GX_VA_NULL)
        return NULL;

    *dobj = d;
    return p;
}

// Copies the one ring's triangle strips out of the shared display list. Returns the
// padded length, 0 when they do not fit.
static int SkyRings_CutDisplayList(POBJDesc *src)
{
    u8 *in = src->display;
    u8 *end = in + src->n_display * 32;
    int len = 0;
    while (in < end)
    {
        if (*in == 0)
        {
            in++;
            continue;
        }

        int count = in[1] << 8 | in[2];
        int size = 3 + count * RING_VTX_SIZE;
        int keep = 1;
        for (int i = 0; i < count && keep; i++)
        {
            u8 *vtx = in + 3 + i * RING_VTX_SIZE;
            int idx = vtx[0] << 8 | vtx[1];
            keep = idx >= RING_SRC_IDX_FIRST && idx <= RING_SRC_IDX_LAST;
        }

        if (keep)
        {
            if (len + size > RING_DL_SIZE)
                return 0;
            memcpy(ring_dl + len, in, size);
            len += size;
        }
        in += size;
    }

    int padded = (len + 31) & ~31;
    memset(ring_dl + len, 0, padded - len);
    DCFlushRange(ring_dl, padded);
    return padded;
}

// The template ring's center, in the file units its vertices are in.
static void SkyRings_GetSourceCenter(POBJDesc *src, Vec3 *out)
{
    u8 *base = src->verts[0].vertex;
    int stride = src->verts[0].stride;
    Vec3 sum = {0};
    for (int i = RING_SRC_IDX_FIRST; i <= RING_SRC_IDX_LAST; i++)
        VECAdd(&sum, (Vec3 *)(base + i * stride), &sum);
    VECScale(&sum, out, 1.0f / (RING_SRC_IDX_LAST - RING_SRC_IDX_FIRST + 1));
}

static int SkyRings_BuildModel(void)
{
    HSD_DObjDesc *src_dobj;
    POBJDesc *src = SkyRings_FindSource(&src_dobj);
    if (!src)
        return 0;

    int len = SkyRings_CutDisplayList(src);
    if (len == 0)
        return 0;

    ring_pobj = (POBJDesc){
        .verts = src->verts,
        .flags = src->flags,
        .n_display = len / 32,
        .display = ring_dl,
    };
    ring_dobj.mobjdesc = src_dobj->mobjdesc;

    Vec3 center;
    SkyRings_GetSourceCenter(src, &center);
    VECScale(&center, &mesh_desc.position, -1.0f);
    return 1;
}

static int SkyRings_IsSpaced(Vec3 *pos)
{
    for (int i = 0; i < ring_num; i++)
    {
        if (VECSquareDistance(pos, &rings[i].pos) < RING_MIN_GAP * RING_MIN_GAP)
            return 0;
    }
    for (int i = 0; i < GetElementsIn(vanilla_rings); i++)
    {
        if (VECSquareDistance(pos, (Vec3 *)&vanilla_rings[i]) < RING_VANILLA_GAP * RING_VANILLA_GAP)
            return 0;
    }
    return 1;
}

// Rays are one-sided, so each line through the opening is cast both ways.
static int SkyRings_IsLineClear(Vec3 *a, Vec3 *b)
{
    Vec3 hit;
    return Raycast_Any(a, b, &hit) < 0 && Raycast_Any(b, a, &hit) < 0;
}

// Open sky above, and a clear flight through the opening along the axis.
static int SkyRings_IsClear(Vec3 *pos, Vec3 *axis)
{
    float r = RING_OUTER_RADIUS * RING_SCALE;
    Vec3 top = {pos->X, RING_SKY_TOP, pos->Z};
    Vec3 rim = {pos->X, pos->Y + r, pos->Z};
    Vec3 hit;
    if (Raycast_Ground(&top, &rim, &hit) >= 0)
        return 0;

    // The center and four points on the rim, the axis being level.
    const float offsets[][2] = {{0, 0}, {1, 0}, {-1, 0}, {0, 1}, {0, -1}};
    for (int i = 0; i < GetElementsIn(offsets); i++)
    {
        float side = offsets[i][0] * r;
        Vec3 p = {pos->X + axis->Z * side, pos->Y + offsets[i][1] * r, pos->Z - axis->X * side};
        Vec3 a = {p.X - axis->X * RING_APPROACH, p.Y, p.Z - axis->Z * RING_APPROACH};
        Vec3 b = {p.X + axis->X * RING_APPROACH, p.Y, p.Z + axis->Z * RING_APPROACH};
        if (!SkyRings_IsLineClear(&a, &b))
            return 0;
    }
    return 1;
}

// Over a random road, at a uniform height between the lowest vanilla ring and the
// garden in the sky's roof.
static int SkyRings_Pick(Vec3 *pos, Vec3 *axis)
{
    Vec3 floor;
    if (RoadSpot_PickFloor(&floor, RING_EDGE_MARGIN) < 0)
        return 0;

    *pos = (Vec3){floor.X, RandomInRangef(RING_Y_MIN, RING_Y_MAX), floor.Z};
    if (pos->Y - floor.Y < RING_FLOOR_GAP || !SkyRings_IsSpaced(pos))
        return 0;

    *axis = CustomEvents_RandomHeading();
    return SkyRings_IsClear(pos, axis);
}

static void SkyRings_Place(void)
{
    for (int i = 0; i < RING_TRIES && ring_num < RING_COUNT; i++)
    {
        Vec3 pos, axis;
        if (!SkyRings_Pick(&pos, &axis))
            continue;
        rings[ring_num++] = (SkyRing){.pos = pos, .axis = axis};
    }
}

// One joint per ring under one root, each opening along its local Z.
static void SkyRings_Spawn(void)
{
    float scale = grGetStageScale() * RING_SCALE;
    for (int i = 0; i < ring_num; i++)
    {
        ring_descs[i] = (JOBJDesc){
            .flags = JOBJ_CLASSICAL_SCALING,
            .child = &mesh_desc,
            .next = i + 1 < ring_num ? &ring_descs[i + 1] : NULL,
            .rotation = {0.0f, atan2(rings[i].axis.X, rings[i].axis.Z), 0.0f},
            .scale = {scale, scale, scale},
            .position = rings[i].pos,
        };
    }
    root_desc.child = &ring_descs[0];

    ring_gobj = GObj_Create(RING_GOBJ_CLASS, GAMEPLINK_1, 0);
    GObj_AddObject(ring_gobj, HSD_OBJKIND_JOBJ, JObj_LoadJoint(&root_desc));
    GObj_AddGXLink(ring_gobj, JObj_GX, GAMEGX_MAP, RING_GX_PRI);
}

// Hangs where it is created: no ground raycast, and a rest state that never tests the floor.
static void SkyRings_SpawnItem(SkyRing *ring)
{
    if (!CityItem_CanSpawnNMore(RING_ITEM_HEADROOM) || !CustomEvents_HeapOk())
        return;

    Vec3 up = {0.0f, 1.0f, 0.0f};
    ItemDesc desc;
    Item_InitDesc(&desc, offered[HSD_Randi(offered_num)], RING_ITEM_SCALE, RING_ITEM_SPAWN_TYPE,
                  &ring->pos, &up, &ring->axis, -1, -1, -1, ITEM_COLL_REST, -1, -1);
    GOBJ *g = CityItem_Create(&desc);
    if (!g)
        return;

    ItemData *id = g->userdata;
    id->lifetime = RING_ITEM_LIFETIME;
    ring->item = g;
    ring->item_serial = id->spawn_serial;
}

// Item GObjs are reused, so the tag and serial confirm it is still this ring's item.
static ItemData *SkyRings_GetWaitingItem(SkyRing *ring)
{
    if (!ring->item || !CustomEvents_IsOnPLink(ring->item, GAMEPLINK_ITEM))
        return NULL;

    ItemData *id = ring->item->userdata;
    if (id->spawn_type != RING_ITEM_SPAWN_TYPE || id->spawn_serial != ring->item_serial ||
        (id->state != ITSTATE_POWERUP_SPAWN && id->state != ITSTATE_POWERUP_REST))
        return NULL;
    return id;
}

static void SkyRings_UpdateItems(void)
{
    if (offered_num == 0)
        return;

    for (int i = 0; i < ring_num; i++)
    {
        SkyRing *ring = &rings[i];
        if (ring->item && !SkyRings_GetWaitingItem(ring))
        {
            ring->item = NULL;
            ring->respawn = RING_ITEM_RESPAWN;
            item_num++;
        }
        if (!ring->item && --ring->respawn <= 0)
            SkyRings_SpawnItem(ring);
    }
}

// What Machine_CheckDashZone does on entering a vanilla ring zone, boosting along the
// ring's axis in the direction flown, and the waiting item collected as a touch would.
static void SkyRings_Pass(SkyRing *ring, MachineData *md, int ply, Vec3 *dir)
{
    md->dash_accel = RING_DASH_ACCEL;
    md->dash_dir = *dir;
    md->dash_frames = RING_DASH_FRAMES;
    Vec3 impulse;
    VECScale(dir, &impulse, RING_DASH_IMPULSE);
    VECAdd(&md->velocity, &impulse, &md->velocity);
    if (!md->is_bike)
        md->star_dash_accum += RING_DASH_IMPULSE;

    PlayerStats *stats = Ply_GetStats(ply);
    stats->dash_gate_hits++;
    stats->sky_rings++;
    Machine_PlaySFX(md, RING_PASS_SFX, 1.0f);
    Ply_PlaySkyRingSFX(ply);

    ItemData *id = SkyRings_GetWaitingItem(ring);
    if (id && id->state == ITSTATE_POWERUP_REST && ItemGObj_CanCollect(ring->item) &&
        !(md->xc31 & MACHINE_XC31_NO_PICKUP))
        Machine_OnTouchItem(md, id);
    pass_num++;
}

// The segment the machine moved along this frame, crossing the ring's plane inside
// its opening.
static void SkyRings_CheckPasses(RingRider *r, MachineData *md, int ply)
{
    float radius = RING_INNER_RADIUS * RING_SCALE;
    for (int i = 0; i < ring_num; i++)
    {
        if (i == r->last_ring && r->repass > 0)
            continue;

        SkyRing *ring = &rings[i];
        Vec3 from, to;
        VECSubtract(&r->prev, &ring->pos, &from);
        VECSubtract(&md->pos, &ring->pos, &to);
        float s0 = VECDotProduct(&from, &ring->axis);
        float s1 = VECDotProduct(&to, &ring->axis);
        if ((s0 < 0.0f) == (s1 < 0.0f))
            continue;

        Vec3 step, hit;
        VECSubtract(&to, &from, &step);
        VECScale(&step, &step, s0 / (s0 - s1));
        VECAdd(&from, &step, &hit);
        if (VECSquareMag(&hit) >= radius * radius)
            continue;

        Vec3 dir = ring->axis;
        if (s1 < s0)
            VECScale(&dir, &dir, -1.0f);
        SkyRings_Pass(ring, md, ply, &dir);
        r->last_ring = i;
        r->repass = RING_REPASS_FRAMES;
        return;
    }
}

// Rails, cannons and gondolas never read a dash zone.
static void SkyRings_UpdateRiders(void)
{
    for (int ply = 0; ply < PLY_NUM; ply++)
    {
        RingRider *r = &riders[ply];
        if (r->repass > 0)
            r->repass--;

        MachineData *md = Ply_GetMachineData(ply);
        if (!md || !md->rider_gobj || Machine_GetRiderPly(md) != ply || md->is_on_rail ||
            md->is_adhered || md->is_dead || md->is_fall_dead)
        {
            r->md = NULL;
            continue;
        }

        if (r->md == md && VECSquareDistance(&r->prev, &md->pos) < RING_MAX_STEP * RING_MAX_STEP)
            SkyRings_CheckPasses(r, md, ply);
        r->md = md;
        r->prev = md->pos;
    }
}

int SkyRings_CanStart(void)
{
    HSD_DObjDesc *dobj;
    return SkyRings_FindSource(&dobj) != NULL;
}

void SkyRings_Start(void)
{
    ring_num = 0;
    ring_gobj = NULL;
    pass_num = 0;
    item_num = 0;
    memset(riders, 0, sizeof(riders));

    offered_num = 0;
    for (int i = 0; i < GetElementsIn(ring_items); i++)
    {
        if (ItemOffer_IsOffered(ring_items[i]))
            offered[offered_num++] = ring_items[i];
    }

    if (!SkyRings_BuildModel())
    {
        OSReport("[SkyRings] Ring model not found, no rings placed\n");
        return;
    }

    SkyRings_Place();
    if (ring_num == 0 || !CustomEvents_HeapOk())
    {
        OSReport("[SkyRings] No room for rings, none placed\n");
        ring_num = 0;
        return;
    }

    SkyRings_Spawn();
    OSReport("[SkyRings] Placed %d of %d rings, %d of %d item kinds offered\n", ring_num, RING_COUNT,
             offered_num, (int)GetElementsIn(ring_items));
}

void SkyRings_Active(void)
{
    if (!ring_gobj)
        return;
    SkyRings_UpdateItems();
    SkyRings_UpdateRiders();
}

// The rings and their waiting items go as the sky changes back.
void SkyRings_End(void)
{
    if (!ring_gobj)
        return;

    for (int i = 0; i < ring_num; i++)
    {
        if (SkyRings_GetWaitingItem(&rings[i]))
            ItemGObj_EnterExpire(rings[i].item);
    }
    GObj_Destroy(ring_gobj);
    ring_gobj = NULL;
}

void SkyRings_End2(void)
{
    OSReport("[SkyRings] %d passes, %d items taken\n", pass_num, item_num);
}
