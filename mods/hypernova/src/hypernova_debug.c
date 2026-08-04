#include "os.h"
#include "game.h"
#include "obj.h"
#include "rider.h"
#include "gx.h"

#include "hypernova.h"

// Cached only to avoid recreating it every frame; never dereferenced (the engine owns it).
static GOBJ *stc_cone_gobj = NULL;

// Base-circle rim as unit (cos, sin) pairs, seeded once so the per-frame draw does no trig.
static Vec2 stc_cone_unit[HYPERNOVA_DEBUG_CONE_SEGS];

// Orthonormal basis (u, v) spanning the plane perpendicular to unit `aim`.
static void ConeBasis(Vec3 *aim, Vec3 *u, Vec3 *v)
{
    // Reference axis not parallel to aim: world up, unless aim is near-vertical (then world X).
    Vec3 ref = {0.0f, 1.0f, 0.0f};
    float ay = aim->Y < 0.0f ? -aim->Y : aim->Y;
    if (ay > 0.99f)
    {
        ref.X = 1.0f;
        ref.Y = 0.0f;
        ref.Z = 0.0f;
    }
    VEC_CrossNormalizeSnap(&ref, aim, u); // u = normalize(ref x aim)
    VECCrossProduct(aim, u, v);           // already unit (aim and u are orthonormal)
}

// One translucent cone: apex at `apex`, axis along unit `aim`, flat base at the forward reach
// (axial distance == HYPERNOVA_RANGE).
static void DrawConeGX(Vec3 *apex, Vec3 *aim, GXColor *col)
{
    float radius = HYPERNOVA_RANGE * HYPERNOVA_HALF_ANGLE_TAN;

    Vec3 u, v;
    ConeBasis(aim, &u, &v);

    // Base-circle center = apex + aim * reach.
    Vec3 axis, center;
    VECScale(aim, &axis, HYPERNOVA_RANGE);
    VECAdd(apex, &axis, &center);

    Vec3 rim[HYPERNOVA_DEBUG_CONE_SEGS];
    for (int i = 0; i < HYPERNOVA_DEBUG_CONE_SEGS; i++)
    {
        float cx = stc_cone_unit[i].X, cy = stc_cone_unit[i].Y;
        rim[i].X = center.X + radius * (cx * u.X + cy * v.X);
        rim[i].Y = center.Y + radius * (cx * u.Y + cy * v.Y);
        rim[i].Z = center.Z + radius * (cx * u.Z + cy * v.Z);
    }

    // Flat per-vertex color, alpha blend, depth-tested but not depth-writing, both faces drawn,
    // so the cone reads as a see-through volume.
    HSD_StateInitDirect(GX_VTXFMT0, 4);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetNumTexGens(0);
    GXSetNumChans(1);
    // Channel 0 color+alpha from the vertex (no lighting), so per-vertex alpha reaches the
    // blender - the cone's translucency depends on it.
    GXSetChanCtrl(GX_COLOR0, GX_DISABLE, Vertex, Vertex, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetChanCtrl(GX_ALPHA0, GX_DISABLE, Vertex, Vertex, GX_LIGHT_NULL, GX_DF_NONE, GX_AF_NONE);
    GXSetAlphaCompare(GX_ALWAYS, 0, GX_AOP_AND, GX_ALWAYS, 0);
    GXSetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
    GXSetZMode(GX_ENABLE, GX_LEQUAL, GX_DISABLE);
    GXSetCullMode(GX_CULL_NONE);
    GXLoadPosMtxImm(&COBJ_GetCurrent()->view_mtx, GX_PNMTX0);

    int segs = HYPERNOVA_DEBUG_CONE_SEGS;
    GXBegin(GX_TRIANGLES, GX_VTXFMT0, segs * 6);
    for (int i = 0; i < segs; i++)
    {
        Vec3 *a = &rim[i];
        Vec3 *b = &rim[(i + 1) % segs];

        // Lateral face.
        GXPosition3f32(apex->X, apex->Y, apex->Z);
        GXColor4u8(col->r, col->g, col->b, col->a);
        GXPosition3f32(a->X, a->Y, a->Z);
        GXColor4u8(col->r, col->g, col->b, col->a);
        GXPosition3f32(b->X, b->Y, b->Z);
        GXColor4u8(col->r, col->g, col->b, col->a);

        // Base cap; winding is moot under cull-none.
        GXPosition3f32(center.X, center.Y, center.Z);
        GXColor4u8(col->r, col->g, col->b, col->a);
        GXPosition3f32(b->X, b->Y, b->Z);
        GXColor4u8(col->r, col->g, col->b, col->a);
        GXPosition3f32(a->X, a->Y, a->Z);
        GXColor4u8(col->r, col->g, col->b, col->a);
    }
    HSD_StateInvalidate(-1);
}

// Drawn on the XLU pass (1) so the cone blends over already-rendered opaque world geometry.
static void Hypernova_DebugConeGX(GOBJ *g, int pass)
{
    if (pass != 1)
        return;
    if (!hypernova_enabled || !hypernova_debug_cone)
        return;

    GXColor col = GXColor_Unpack(HYPERNOVA_DEBUG_CONE_RGBA);

    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        GOBJ *rg = Ply_GetRiderGObj(i);
        if (!rg)
            continue;
        RiderData *rd = rg->userdata;

        Vec3 fwd = rd->forward;
        Vec3 aim;
        if (VEC_NormalizeAndSnap(&fwd, &aim) < 0.01f)
            continue; // no usable facing this frame, matching the vacuum's guard
        DrawConeGX(&rd->pos, &aim, &col);
    }
}

void Hypernova_DebugConeEnsure(void)
{
    if (!hypernova_debug_cone)
        return;
    if (stc_cone_gobj != NULL)
        return;

    GOBJ *g = GObj_Create(HYPERNOVA_DEBUG_GOBJ_CLASS, HYPERNOVA_DEBUG_GOBJ_PLINK, 0);
    if (g == NULL)
        return;
    GObj_AddGXLink(g, Hypernova_DebugConeGX, HYPERNOVA_DEBUG_GX_LINK, HYPERNOVA_DEBUG_GX_PRI);
    stc_cone_gobj = g;

    for (int i = 0; i < HYPERNOVA_DEBUG_CONE_SEGS; i++)
    {
        float a = (6.28318531f * i) / HYPERNOVA_DEBUG_CONE_SEGS; // i * (2*pi / segs)
        stc_cone_unit[i].X = cosf(a);
        stc_cone_unit[i].Y = sinf(a);
    }

    OSReport("[HypernovaDebug] Inhale-cone overlay installed\n");
}

void Hypernova_DebugConeReset(void)
{
    // The engine frees every world GObj on scene teardown; destroying it here would double-free.
    stc_cone_gobj = NULL;
}
