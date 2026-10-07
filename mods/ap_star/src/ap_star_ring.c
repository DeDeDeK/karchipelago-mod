#include <string.h>

#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "machine.h"

#include "ap_star.h"
#include "ap_star_ring.h"

#define AP_STAR_POD_NUM   APSTARPIECE_NUM // one per sphere color, in the same order
#define AP_STAR_POD_JOINT 9 // first pod; the six are consecutive in the archive's joint tree

// Machines tracked at once; past this a machine has no ring, and no shot, until one frees.
#define AP_STAR_RING_MAX 32

#define POD_SHRINK_FRAMES  10
#define RING_REGROW_FRAMES 60
#define RESPREAD_RATE      0.18f // per-frame fraction of the way to the even ring
#define RESPREAD_SNAP      0.0005f

typedef struct RingState
{
    MachineData *md;   // owner, NULL while the slot is free
    JOBJ *root;        // the model the pods were resolved against
    JOBJ *pod[AP_STAR_POD_NUM];
    Vec3 pod_trans[AP_STAR_POD_NUM]; // authored ring positions
    float pod_rot_y[AP_STAR_POD_NUM];
    Vec3 pod_scale;    // the authored scale a full-size pod returns to
    float offset[AP_STAR_POD_NUM];   // current swing around the ring, radians
    float target[AP_STAR_POD_NUM];
    u8 alive_mask;
    u8 spread_mask;    // the alive_mask the targets were solved for
    u8 shrink[AP_STAR_POD_NUM]; // frames left of a fired pod's collapse
    u8 regrowing;
    u8 regrow_timer;
    u32 frame;         // last stc_frame this machine was seen
} RingState;

static RingState stc_rings[AP_STAR_RING_MAX];
static u32 stc_frame;        // counts only frames in which some star ran its Think
static u32 stc_engine_frame; // engine_frames of the latest such frame

static int stc_star_slot = -1; // class slot, which is what MachineData.kind holds

static RingState *FindRing(MachineData *md)
{
    for (int i = 0; i < AP_STAR_RING_MAX; i++)
    {
        if (stc_rings[i].md == md)
            return &stc_rings[i];
    }
    return NULL;
}

// A slot is reclaimed once its machine has gone a whole frame unseen, so a machine
// destroyed without warning leaves nothing to clean up. A live slot is never taken:
// its owner would take another in turn, and the cascade leaves the machines first in
// the proc list without a ring every frame.
static RingState *ClaimRing(MachineData *md)
{
    RingState *free_slot = NULL;
    RingState *stale = NULL;

    // A pause runs no Think, so it does not age every ring at once.
    if (stc_hsd_update->engine_frames != stc_engine_frame)
    {
        stc_engine_frame = stc_hsd_update->engine_frames;
        stc_frame++;
    }
    for (int i = 0; i < AP_STAR_RING_MAX; i++)
    {
        RingState *r = &stc_rings[i];
        if (r->md == md)
        {
            r->frame = stc_frame;
            return r;
        }
        if (r->md == NULL)
        {
            if (free_slot == NULL)
                free_slot = r;
        }
        else if (stale == NULL && stc_frame - r->frame > 1)
        {
            stale = r;
        }
    }

    RingState *r = free_slot != NULL ? free_slot : stale;
    if (r == NULL)
        return NULL;
    memset(r, 0, sizeof(*r));
    r->md = md;
    r->alive_mask = AP_STAR_PIECE_ALL;
    r->frame = stc_frame;
    return r;
}

// The pods carry no animation tracks, so the authored pose is still readable the
// first time a fresh model is walked.
static int ResolvePods(RingState *r, MachineData *md)
{
    JOBJ *root = (JOBJ *)md->gobj->hsd_object;
    if (root != NULL && root == r->root)
        return 1;

    r->root = NULL;
    if (root == NULL)
        return 0;

    for (int i = 0; i < AP_STAR_POD_NUM; i++)
    {
        r->pod[i] = cm_api->GetMachineJoint(md, AP_STAR_POD_JOINT + i);
        if (r->pod[i] == NULL)
            return 0;
        r->pod_trans[i] = r->pod[i]->trans;
        r->pod_rot_y[i] = r->pod[i]->rot.Y;
        r->offset[i] = 0.0f;
        r->target[i] = 0.0f;
    }
    r->pod_scale = r->pod[0]->scale;
    r->spread_mask = 0;
    r->root = root;
    return 1;
}

static float WrapTurns(float t)
{
    while (t > 0.5f)
        t -= 1.0f;
    while (t <= -0.5f)
        t += 1.0f;
    return t;
}

// Even angles for however many pods are left. The ring never stops spinning, so
// only the transient matters: the phase chosen is the one that moves the pods least.
static void SolveSpread(RingState *r)
{
    int alive[AP_STAR_POD_NUM];
    int count = 0;

    for (int i = 0; i < AP_STAR_POD_NUM; i++)
    {
        if (r->alive_mask & (1 << i))
            alive[count++] = i;
    }
    r->spread_mask = r->alive_mask;

    // How far each survivor already sits from an even count-ring, in turns.
    float residual[AP_STAR_POD_NUM];
    float drift = 0.0f;

    for (int j = 0; j < count; j++)
    {
        residual[j] = (float)alive[j] / (float)AP_STAR_POD_NUM - (float)j / (float)count;
        drift += WrapTurns(residual[j] - residual[0]);
    }

    float base = residual[0] + drift / (float)count;
    for (int j = 0; j < count; j++)
        r->target[alive[j]] = (base - residual[j]) * 2.0f * M_PI;
}

// Overshoots a little before settling, so the ring snaps back rather than
// creeping up to size.
static float GrowCurve(float t)
{
    float u = t - 1.0f;
    return 1.0f + 2.70158f * u * u * u + 1.70158f * u * u;
}

// Swinging a pod around the ring is a rotation of its authored position about
// the pivot's Y, and the same delta on its own yaw so it keeps facing outward.
static void SetPodPose(RingState *r, int i, float f)
{
    JOBJ *j = r->pod[i];
    float d = r->offset[i];

    j->scale.X = r->pod_scale.X * f;
    j->scale.Y = r->pod_scale.Y * f;
    j->scale.Z = r->pod_scale.Z * f;

    if (d != 0.0f)
    {
        float s = sinf(d);
        float c = cosf(d);
        j->trans.X = r->pod_trans[i].X * c + r->pod_trans[i].Z * s;
        j->trans.Y = r->pod_trans[i].Y;
        j->trans.Z = r->pod_trans[i].Z * c - r->pod_trans[i].X * s;
        j->rot.Y = r->pod_rot_y[i] + d;
    }
    else
    {
        j->trans = r->pod_trans[i];
        j->rot.Y = r->pod_rot_y[i];
    }

    JObj_SetMtxDirtySub(j);
}

// Tail of the star class's per-kind Think slot, once per frame per machine.
static void OnStarThink(MachineData *md)
{
    RingState *r = ClaimRing(md);
    if (r == NULL || !ResolvePods(r, md))
        return;

    float grow = 0.0f;
    if (r->regrowing)
    {
        grow = GrowCurve((float)r->regrow_timer / (float)RING_REGROW_FRAMES);
        if (++r->regrow_timer > RING_REGROW_FRAMES)
        {
            r->regrowing = 0;
            r->alive_mask = AP_STAR_PIECE_ALL;
        }
    }
    else if (r->spread_mask != r->alive_mask)
    {
        SolveSpread(r);
    }

    for (int i = 0; i < AP_STAR_POD_NUM; i++)
    {
        float f;
        if (r->regrowing)
        {
            f = grow;
        }
        else if (r->alive_mask & (1 << i))
        {
            // A spent pod holds where it died while it collapses; only the
            // survivors slide, easing in so the ring settles rather than snaps.
            float gap = r->target[i] - r->offset[i];
            r->offset[i] = (gap < RESPREAD_SNAP && gap > -RESPREAD_SNAP)
                               ? r->target[i]
                               : r->offset[i] + gap * RESPREAD_RATE;
            f = 1.0f;
        }
        else if (r->shrink[i] != 0)
        {
            f = (float)(--r->shrink[i]) / (float)POD_SHRINK_FRAMES;
        }
        else
        {
            f = 0.0f;
        }
        SetPodPose(r, i, f);
    }
}

// Tail of the star class's per-kind Init slot. The next Think rebuilds the ring
// against whatever model the new machine loaded.
static void OnStarInit(MachineData *md)
{
    RingState *r = FindRing(md);
    if (r != NULL)
        r->md = NULL;
}

// The remaining pod closest to the machine's heading, compared in the horizontal
// plane so the ring's tilt does not decide it.
static int NearestPod(RingState *r, MachineData *md)
{
    int best = -1;
    float best_dot = -2.0f;

    Vec3 fwd = { md->forward.X, 0.0f, md->forward.Z };
    if (VECSquareMag(&fwd) < 0.0001f)
    {
        // No horizontal heading to pick by, so the lowest-index remaining pod stands in.
        u32 alive = r->alive_mask & AP_STAR_PIECE_ALL;
        return alive ? __builtin_ctz(alive) : -1;
    }
    VECNormalize(&fwd, &fwd);

    for (int i = 0; i < AP_STAR_POD_NUM; i++)
    {
        if (!(r->alive_mask & (1 << i)))
            continue;

        Vec3 p;
        JObj_GetWorldPosition(r->pod[i], NULL, &p);

        Vec3 d = { p.X - md->pos.X, 0.0f, p.Z - md->pos.Z };
        if (VECSquareMag(&d) < 0.0001f)
            continue;
        VECNormalize(&d, &d);

        float dot = VECDotProduct(&d, &fwd);
        if (dot > best_dot)
        {
            best_dot = dot;
            best = i;
        }
    }
    return best;
}

int ApStarRing_AimPod(MachineData *md, Vec3 *muzzle)
{
    // A ring outlives its machine by a frame, so a freed MachineData reused by another
    // machine can still find one.
    if (md->is_bike || md->kind != stc_star_slot)
        return -1;

    RingState *r = FindRing(md);
    if (r == NULL || r->root == NULL || r->regrowing)
        return -1;

    int pod = NearestPod(r, md);
    if (pod >= 0)
        JObj_GetWorldPosition(r->pod[pod], NULL, muzzle);
    return pod;
}

void ApStarRing_Spend(MachineData *md, int pod)
{
    RingState *r = FindRing(md);

    r->alive_mask &= (u8)~(1 << pod);
    if (r->alive_mask != 0)
    {
        r->shrink[pod] = POD_SHRINK_FRAMES;
        return;
    }

    // Every pod is at zero scale on this frame, so putting the ring back to its
    // authored spacing here is invisible.
    r->regrowing = 1;
    r->regrow_timer = 0;
    memset(r->shrink, 0, sizeof(r->shrink));
    memset(r->offset, 0, sizeof(r->offset));
    memset(r->target, 0, sizeof(r->target));
    r->spread_mask = 0;
}

void ApStarRing_Bind(int kind)
{
    int is_bike;
    stc_star_slot = cm_api->ClassIndexFromKind(kind, &is_bike);
    cm_api->SetInitHandler(kind, OnStarInit);
    cm_api->SetThinkHandler(kind, OnStarThink);
    OSReport("[ApStarRing] Init and Think handlers installed on star slot %d\n", stc_star_slot);
}

void ApStarRing_OnSceneChange(void)
{
    memset(stc_rings, 0, sizeof(stc_rings));
}
