#include <string.h>

#include "game.h"
#include "os.h"
#include "audio.h"
#include "hsd.h"
#include "obj.h"
#include "stage.h"
#include "yakumono.h"
#include "machine.h"
#include "collision.h"
#include "preload.h"
#include "inline.h"
#include "code_patch/code_patch.h"

#include "custom_events.h"
#include "event_cannon.h"

// CannonManifest.dat holds no game data: it is a recipe for rebuilding Machine
// Passage's cannon mesh and flight out of the retail disc.
#define CANNON_MANIFEST_FILE    "CannonManifest.dat"
#define CANNON_MANIFEST_SYMBOL  "cannonManifest"
#define CANNON_MANIFEST_MAGIC   0x434E4D46u // 'CNMF'
#define CANNON_MANIFEST_VERSION 1

#define CANNON_GOBJ_CLASS     214
#define CANNON_GX_PRI         2        // after Map_GX (1), whose backdrop draws with no depth test
#define CANNON_DATA_IDX       31       // Rail Fire's YakumonoTable slot, lent for GrYaku_Create alone
#define CANNON_WAIT           60       // frames a machine sits in the barrel
#define CANNON_COOLDOWN       4        // frames between two launches
#define CANNON_SFX            FGMCITY_SJUMP_L // Machine Passage's shot is in a bank City Trial does not load
#define CANNON_CATCH_RADIUS   22.0f    // around the pivot, about the base's footprint
#define CANNON_CATCH_BELOW    5.0f
#define CANNON_CATCH_ABOVE    35.0f
#define CANNON_RECATCH_FRAMES (CANNON_WAIT + 60) // until a launched machine has flown clear
#define CANNON_YAW_SETTLE     30       // flight frame by which the machine faces the path heading
#define CANNON_CV_MAX         8
#define CANNON_GROUND_TOP     600.0f
#define CANNON_GROUND_BOTTOM  -300.0f

// All three fields are multiples of 32, which is what File_Read requires of an
// offset, a length and a destination alike.
typedef struct PayloadRange
{
    u32 donor_off; // 0x00
    u32 dest_off;  // 0x04
    u32 length;    // 0x08
} PayloadRange;

// dest_val is the pointer's payload offset, wrapped below zero for a vertex array
// of which only a slice is read, leaving the runtime to add the payload base.
typedef struct PayloadReloc
{
    u32 dest_off;  // 0x00
    u32 dest_val;  // 0x04
} PayloadReloc;

typedef struct DonorPayload
{
    const char *donor;           // 0x00
    u32 size;                    // 0x04
    const PayloadRange *ranges;  // 0x08
    u32 range_num;               // 0x0C
    const PayloadReloc *relocs;  // 0x10
    u32 reloc_num;               // 0x14
} DonorPayload;                  // 0x18

// Geometry in the donor stage's units.
typedef struct CannonManifest
{
    u32 magic;            // 0x00
    u32 version;          // 0x04
    DonorPayload mesh;    // 0x08
    const u32 *dobjs;     // 0x20 payload offsets of the mesh's DObjDescs, in draw order
    u32 dobj_num;         // 0x24
    Vec3 pivot;           // 0x28 the barrel anchor's parent joint, unrotated
    Vec3 anchor_pos;      // 0x34 the barrel anchor, local to the pivot
    Vec3 anchor_rot;      // 0x40
    float floor;          // 0x4C the mesh's lowest point, relative to the pivot
    float scale;          // 0x50 the donor's stage scale, which the mesh is drawn at
    DonorPayload path;    // 0x54
    u32 path_root;        // 0x6C the flight AnimJointDesc, payload-relative
} CannonManifest;         // 0x70

typedef struct CannonPlacement
{
    const char *label;
    float x, z;    // pivot, world units
    float heading; // flight heading in degrees, 0 = +Z, 90 = +X
    float range;   // flight length relative to Machine Passage's
} CannonPlacement;

// Each flight stays clear of collision until the anim ends.
static const CannonPlacement placements[] = {
    {"south, by the lighthouse", 254.3f, 403.3f, 180.0f, 1.5f},
    {"west, facing the volcano", -345.3f, -249.2f, 184.0f, 1.4f},
    {"north, over the city", 126.1f, -572.5f, 30.0f, 1.3f},
};
#define PLACEMENT_NUM ((int)GetElementsIn(placements))

// Rebuilt at every City Trial load, in the scene heap; NULL when the event cannot run.
static HSD_DObjDesc *mesh_dobjs;
static AnimJointDesc *flight;
static HSD_Spline *flight_spline;
static HSD_FObjDesc *flight_yaw;
static float flight_end;

// The donor's own layout, kept so every placement transforms from the original.
static Vec3 donor_pivot;
static Vec3 donor_anchor_pos;
static Vec3 donor_anchor_rot;
static Vec3 donor_flight_start; // the barrel anchor in stage units, where the flight begins
static float donor_floor;
static float donor_scale;
static float donor_heading;     // from the flight's start to its end
static Vec3 flight_cv[CANNON_CV_MAX];
static float flight_poly[CANNON_CV_MAX - 1][5];
static float flight_length;

static JOBJDesc mesh_desc = {
    .flags = JOBJ_CLASSICAL_SCALING | JOBJ_OPA | JOBJ_XLU | JOBJ_TEXEDGE,
    .scale = {1.0f, 1.0f, 1.0f},
};
static JOBJDesc root_desc = {
    .flags = JOBJ_CLASSICAL_SCALING | JOBJ_ROOT_MASK,
    .child = &mesh_desc,
};

static AnimJointDesc *flight_list[1];
static GrSubAnim flight_subanim = {flight_list, 1};
static YakuCannonDesc cannon_desc = {
    .wait = CANNON_WAIT,
    .cooldown = CANNON_COOLDOWN,
    .path_idx = 0,
    .sfx = CANNON_SFX,
};
static YakuCannonParam cannon_param = {.desc = &cannon_desc};

// The yaw keys are written per placement: one LIN run of three keys, each a float and
// a one- or two-byte frame delta.
static u8 yaw_stream[1 + 3 * (4 + 2)];

static GOBJ *cannon_gobj; // the mesh, whose proc catches machines
static GOBJ *cannon_yaku; // the YAKUKIND_CANNON yakumono
static int cannon_closing;
static Vec3 pivot_pos;
static Vec3 mouth_pos, mouth_fwd, mouth_up;
static int recatch[PLY_NUM];

static GrSubAnimNode *patched_subanim;

// A missing file is permanent and the load runs every round, so the report latches.
static int warned;

static int Cannon_ShouldWarn(void)
{
    if (warned)
        return 0;
    warned = 1;
    return 1;
}

static u8 *RebuildPayload(const DonorPayload *p)
{
    int entrynum = DVDConvertPathToEntrynum((char *)p->donor);
    if (entrynum < 0)
    {
        if (Cannon_ShouldWarn())
            OSReport("[Cannon] %s not on disc\n", p->donor);
        return NULL;
    }

    // The heap hands out 32-byte-aligned blocks, as File_Read's DMA needs.
    u8 *base = Heap_Alloc(PRELOADHEAPKIND_HSD, p->size);
    for (u32 i = 0; i < p->range_num; i++)
    {
        const PayloadRange *r = &p->ranges[i];
        *stc_file_read_done = 0;
        File_Read(entrynum, r->donor_off, base + r->dest_off, r->length,
                  DEVCOMSRC_DVD | DEVCOMDEST_DRAM, 1, File_ReadDone, NULL);
        while (File_PollReadDone() == 0)
            ;
    }

    for (u32 i = 0; i < p->reloc_num; i++)
    {
        const PayloadReloc *rl = &p->relocs[i];
        *(u32 *)(base + rl->dest_off) = (u32)base + rl->dest_val;
    }
    return base;
}

static void AxisOf(Mtx m, float x, float y, float z, Vec3 *out)
{
    Vec3 v = {x, y, z}, w;
    MTXMultVecSR(m, &v, &w);
    VECNormalize(&w, out);
}

static int FindFlight(AnimJointDesc *root)
{
    HSD_AObjDesc *aobj = root->aobj;
    if (aobj == NULL || aobj->obj_id == 0)
        return 0;

    HSD_Spline *spl = ((JOBJDesc *)aobj->obj_id)->u.spline;
    if (spl->cv_num < 2 || spl->cv_num > CANNON_CV_MAX)
        return 0;

    HSD_FObjDesc *yaw = NULL;
    for (HSD_FObjDesc *f = aobj->fobjdesc; f; f = f->next)
    {
        if (f->track == HSD_A_J_ROTY)
            yaw = f;
    }
    if (yaw == NULL)
        return 0;

    flight = root;
    flight_spline = spl;
    flight_yaw = yaw;
    flight_end = aobj->end_frame;
    flight_length = spl->total_length;
    memcpy(flight_cv, spl->cv, spl->cv_num * sizeof(Vec3));
    memcpy(flight_poly, spl->seg_poly, (spl->cv_num - 1) * sizeof(flight_poly[0]));
    return 1;
}

void Cannon_OnCityLoad(void)
{
    mesh_dobjs = NULL;
    flight = NULL;
    cannon_gobj = NULL;
    cannon_yaku = NULL;

    // Archive_LoadFile asserts on a missing file rather than returning NULL.
    if (DVDConvertPathToEntrynum(CANNON_MANIFEST_FILE) < 0)
    {
        if (Cannon_ShouldWarn())
            OSReport("[Cannon] %s missing\n", CANNON_MANIFEST_FILE);
        return;
    }
    HSD_Archive *arc = Archive_LoadFile(CANNON_MANIFEST_FILE);
    const CannonManifest *m = Archive_GetPublicAddress(arc, CANNON_MANIFEST_SYMBOL);
    if (m == NULL || m->magic != CANNON_MANIFEST_MAGIC || m->version != CANNON_MANIFEST_VERSION)
    {
        if (Cannon_ShouldWarn())
            OSReport("[Cannon] %s unusable (magic/version)\n", CANNON_MANIFEST_FILE);
        return;
    }

    u8 *mesh = RebuildPayload(&m->mesh);
    u8 *path = RebuildPayload(&m->path);
    if (mesh == NULL || path == NULL)
        return;

    if (!FindFlight((AnimJointDesc *)(path + m->path_root)))
    {
        if (Cannon_ShouldWarn())
            OSReport("[Cannon] Flight in %s unusable\n", m->path.donor);
        return;
    }

    // The DObjs were picked out of a longer chain, so their links are rewritten.
    for (u32 i = 0; i < m->dobj_num; i++)
    {
        HSD_DObjDesc *d = (HSD_DObjDesc *)(mesh + m->dobjs[i]);
        d->next = i + 1 < m->dobj_num ? (HSD_DObjDesc *)(mesh + m->dobjs[i + 1]) : NULL;
    }
    mesh_dobjs = (HSD_DObjDesc *)(mesh + m->dobjs[0]);

    donor_pivot = m->pivot;
    donor_anchor_pos = m->anchor_pos;
    donor_anchor_rot = m->anchor_rot;
    donor_floor = m->floor;
    donor_scale = m->scale;
    VECAdd(&donor_pivot, &donor_anchor_pos, &donor_flight_start);
    Vec3 *end = &flight_cv[flight_spline->cv_num - 1];
    donor_heading = atan2(end->X - donor_flight_start.X, end->Z - donor_flight_start.Z);

    OSReport("[Cannon] Rebuilt from disc (%d KB mesh, %d B flight)\n",
             m->mesh.size / 1024, m->path.size);
}

// The yaw track is absolute, so it is the one part of the flight a new heading
// rewrites outright: the barrel's yaw at launch, turning onto the path heading.
static void WriteYawTrack(float launch_yaw, float path_yaw)
{
    const struct
    {
        float value;
        int frame;
    } keys[] = {
        {launch_yaw, 0},
        {path_yaw, CANNON_YAW_SETTLE},
        {path_yaw, (int)flight_end},
    };

    u8 *p = yaw_stream;
    *p++ = ((GetElementsIn(keys) - 1) << 4) | HSD_A_OP_LIN;
    for (int i = 0; i < (int)GetElementsIn(keys); i++)
    {
        union
        {
            float f;
            u32 u;
        } v = {.f = keys[i].value};
        // Key values are little-endian.
        for (int b = 0; b < 4; b++)
            *p++ = v.u >> (8 * b);

        int delta = i + 1 < (int)GetElementsIn(keys) ? keys[i + 1].frame - keys[i].frame : 0;
        do
        {
            *p = delta & 0x7f;
            delta >>= 7;
            if (delta)
                *p |= 0x80;
            p++;
        } while (delta);
    }

    flight_yaw->buffer = yaw_stream;
    flight_yaw->length = p - yaw_stream;
    flight_yaw->value_flag = 0;
    flight_yaw->tan_flag = 0;
}

// The machine moves by the flight's frame-to-frame translate times the stage scale,
// while the cannon camera aims at the flight's position itself, so the spline is
// written in City Trial's stage units (world / scale), starting at the barrel mouth.
static void PlaceFlight(float psi, float range)
{
    float city_scale = grGetStageScale();
    float f = donor_scale / city_scale * range;

    Mtx rot;
    Vec3 one = {1.0f, 1.0f, 1.0f};
    Vec3 yaw = {0.0f, psi, 0.0f};
    Vec3 zero = {0.0f, 0.0f, 0.0f};
    HSD_MtxSRT(&rot, &one, &yaw, &zero, 0);

    for (int i = 0; i < flight_spline->cv_num; i++)
    {
        Vec3 rel, d;
        VECSubtract(&flight_cv[i], &donor_flight_start, &rel);
        MTXMultVecSR(rot, &rel, &d);
        flight_spline->cv[i].X = mouth_pos.X / city_scale + d.X * f;
        flight_spline->cv[i].Y = mouth_pos.Y / city_scale + d.Y * f;
        flight_spline->cv[i].Z = mouth_pos.Z / city_scale + d.Z * f;
    }

    // Arc length grows by f, and |dP/du|^2 by f^2; the normalized table is unchanged.
    flight_spline->total_length = flight_length * f;
    for (int s = 0; s < flight_spline->cv_num - 1; s++)
    {
        for (int k = 0; k < (int)GetElementsIn(flight_poly[0]); k++)
            flight_spline->seg_poly[s][k] = flight_poly[s][k] * f * f;
    }

    WriteYawTrack(atan2(mouth_fwd.X, mouth_fwd.Z), donor_heading + psi);
}

static void Place(const CannonPlacement *pl)
{
    Vec3 from = {pl->x, CANNON_GROUND_TOP, pl->z};
    Vec3 to = {pl->x, CANNON_GROUND_BOTTOM, pl->z};
    Vec3 ground = {pl->x, 0.0f, pl->z};
    if (Raycast_Ground(&from, &to, &ground) < 0)
        OSReport("[Cannon] No ground under %s, placed at height 0\n", pl->label);

    float psi = MTXDegToRad(pl->heading) - donor_heading;
    pivot_pos = (Vec3){ground.X, ground.Y - donor_floor * donor_scale, ground.Z};

    root_desc.position = pivot_pos;
    root_desc.rotation = (Vec3){0.0f, psi, 0.0f};
    root_desc.scale = (Vec3){donor_scale, donor_scale, donor_scale};
    mesh_desc.position = (Vec3){-donor_pivot.X, -donor_pivot.Y, -donor_pivot.Z};
    mesh_desc.u.dobjdesc = mesh_dobjs;

    // The barrel anchor hangs off the pivot; machines sit at it facing its Z, up its Y.
    Mtx root, anchor, world;
    Vec3 one = {1.0f, 1.0f, 1.0f};
    HSD_MtxSRT(&root, &root_desc.scale, &root_desc.rotation, &root_desc.position, 0);
    HSD_MtxSRT(&anchor, &one, &donor_anchor_rot, &donor_anchor_pos, 0);
    PSMTXConcat(&root, &anchor, &world);
    HSD_MtxGetTranslate(&world, &mouth_pos);
    AxisOf(world, 0.0f, 0.0f, 1.0f, &mouth_fwd);
    AxisOf(world, 0.0f, 1.0f, 0.0f, &mouth_up);

    PlaceFlight(psi, pl->range);
}

static void RestoreStage(void)
{
    if (patched_subanim)
        patched_subanim->cannon = NULL;
    patched_subanim = NULL;
}

static int InCatchVolume(const Vec3 *pos)
{
    float dx = pos->X - pivot_pos.X;
    float dz = pos->Z - pivot_pos.Z;
    float base = pivot_pos.Y + donor_floor * donor_scale;
    return dx * dx + dz * dz <= CANNON_CATCH_RADIUS * CANNON_CATCH_RADIUS
           && pos->Y >= base - CANNON_CATCH_BELOW && pos->Y <= base + CANNON_CATCH_ABOVE;
}

// The cannon launches whatever it has queued, from wherever it is, so it outlives the
// event until its queue is empty and no machine is still in the barrel.
static int IsBusy(void)
{
    YakuCannonQueue *q = YakuCannon_GetQueue(cannon_yaku);
    if (q->head != q->tail)
        return 1;
    for (int ply = 0; ply < PLY_NUM; ply++)
    {
        if (Ply_GetPKind(ply) != PKIND_NONE && Ply_IsInCannon(ply))
            return 1;
    }
    return 0;
}

static void Remove(void)
{
    GObj_Destroy(cannon_yaku);
    cannon_yaku = NULL;
    RestoreStage();

    GOBJ *g = cannon_gobj;
    cannon_gobj = NULL;
    GObj_Destroy(g);
    OSReport("[Cannon] Removed\n");
}

// Priority 0, before Machine_ProcessEnvColl (5) and the model-matrix think (6), where
// a machine is caught by a stage's own cannon. Machine_SetAirborne skips a matrix copy
// at priority 8 and up.
static void Cannon_Think(GOBJ *gobj)
{
    if (cannon_closing)
    {
        if (!IsBusy())
            Remove();
        return;
    }

    int caught = 0;
    for (int ply = 0; ply < PLY_NUM; ply++)
    {
        if (recatch[ply] > 0)
        {
            recatch[ply]--;
            continue;
        }
        if (Ply_GetPKind(ply) == PKIND_NONE)
            continue;

        GOBJ *mg = Ply_GetMachineGObj(ply);
        if (mg == NULL)
            continue;
        MachineData *md = mg->userdata;
        if (md->rider_gobj == NULL || md->is_adhered || !InCatchVolume(&md->pos))
            continue;

        // What Machine_ProcessEnvColl writes for a catch zone. Only a machine that
        // entered is queued, since the cannon launches every queued machine.
        md->adhere_yaku = cannon_yaku;
        if (!Machine_TryEnterAdhereYaku(md))
        {
            md->adhere_yaku = NULL;
            continue;
        }
        GrYakuCannon_Enqueue(cannon_yaku, mg);
        recatch[ply] = CANNON_RECATCH_FRAMES;
        caught++;
    }

    if (caught)
        OSReport("[Cannon] Loaded %d machine(s)\n", caught);
}

void Cannon_Start(void)
{
    if (cannon_gobj)
    {
        cannon_closing = 0;
        OSReport("[Cannon] Still standing, reopened\n");
        return;
    }
    if (mesh_dobjs == NULL)
    {
        OSReport("[Cannon] Not rebuilt this round, nothing placed\n");
        return;
    }

    GrData *gd = (*stc_grobj)->gr_data;
    YakumonoTable *table = gd->yakumono;
    if (gd->subanim == NULL || gd->subanim->cannon != NULL || table == NULL
        || table->data_count <= CANNON_DATA_IDX)
    {
        OSReport("[Cannon] Stage slots in use, nothing placed\n");
        return;
    }

    const CannonPlacement *pl = &placements[HSD_Randi(PLACEMENT_NUM)];
    Place(pl);

    flight_list[0] = flight;
    gd->subanim->cannon = &flight_subanim;
    patched_subanim = gd->subanim;

    // GrYakuCannon_Create without discarding the GObj. City Trial fills every slot, so
    // one is lent and handed back: GrYaku_Create keeps the static block's pointer as
    // YakumonoData.data_ptr and never reads the slot again.
    void *lent = table->data_array[CANNON_DATA_IDX];
    table->data_array[CANNON_DATA_IDX] = &cannon_param;
    cannon_yaku = GrYaku_Create(YAKUKIND_CANNON, CANNON_DATA_IDX);
    table->data_array[CANNON_DATA_IDX] = lent;
    GrYakuCannon_TailInit(cannon_yaku);

    cannon_gobj = GObj_Create(CANNON_GOBJ_CLASS, GAMEPLINK_1, 0);
    GObj_AddObject(cannon_gobj, HSD_OBJKIND_JOBJ, JObj_LoadJoint(&root_desc));
    GObj_AddGXLink(cannon_gobj, JObj_GX, GAMEGX_MAP, CANNON_GX_PRI);
    GObj_AddProc(cannon_gobj, Cannon_Think, 0);

    memset(recatch, 0, sizeof(recatch));
    cannon_closing = 0;
    OSReport("[Cannon] Placed at %s (%.0f, %.0f, %.0f), heading %.0f\n",
             pl->label, pivot_pos.X, pivot_pos.Y, pivot_pos.Z, pl->heading);
}

void Cannon_End2(void)
{
    if (cannon_gobj)
        cannon_closing = 1;
}

void Cannon_On3DExit(void)
{
    RestoreStage();
    cannon_gobj = NULL;
    cannon_yaku = NULL;
    mesh_dobjs = NULL;
    flight = NULL;
}

// GrYakuCannon_GetPose (0x800ff3e4), the pose a machine entering a cannon snaps to.
static void Cannon_GetPose(GOBJ *yaku_gobj, Vec3 *pos, Vec3 *fwd, Vec3 *up)
{
    if (yaku_gobj == cannon_yaku)
    {
        *pos = mouth_pos;
        *fwd = mouth_fwd;
        *up = mouth_up;
        return;
    }

    YakuCannonDesc *desc = ((YakuCannonParam *)Yaku_GetData(yaku_gobj)->data_ptr)->desc;
    Gr_GetNodeWorldPos(desc->joint, pos);
    Gr_GetNodeWorldAxisZ(desc->joint, fwd);
    Gr_GetNodeWorldAxisY(desc->joint, up);
}

void Cannon_InstallHooks(void)
{
    CODEPATCH_REPLACEFUNC(GrYakuCannon_GetPose, Cannon_GetPose);
}
