#include "os.h"
#include "game.h"
#include "obj.h"
#include "rider.h"
#include "gx.h"

#include "hypernova.h"

// Cached only to avoid recreating it every frame; never dereferenced (the engine owns it).
static GOBJ *stc_cone_gobj = NULL;

// One base-circle subdivision step (360 / HYPERNOVA_DEBUG_CONE_SEGS = 15deg), as cos/sin
// constants so the circle is walked by incremental 2D rotation - one complex multiply per rim
// point instead of a trig call per vertex every frame.
#define CONE_STEP_COS 0.96592583f // cos(15deg)
#define CONE_STEP_SIN 0.25881905f // sin(15deg)

// Build an orthonormal basis (u, v) spanning the plane perpendicular to unit `aim`.
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
    VEC_CrossNormalizeSnap(&ref, aim, u); // u = normalize(ref x aim); ref is never parallel to aim
    VECCrossProduct(aim, u, v);           // already unit (aim and u are orthonormal)
}

// Emit one translucent cone: apex at `apex`, axis along unit `aim`, flat base at the forward
// reach (axial distance == HYPERNOVA_RANGE), base radius = reach * tan(half-angle). Cull-none +
// alpha-blend draws both faces so it reads as a see-through volume.
static void DrawConeGX(Vec3 *apex, Vec3 *aim, GXColor *col)
{
    float radius = HYPERNOVA_RANGE * HYPERNOVA_HALF_ANGLE_TAN; // reach * tan(half-angle)

    Vec3 u, v;
    ConeBasis(aim, &u, &v);

    // Base-circle center = apex + aim * reach.
    Vec3 axis, center;
    VECScale(aim, &axis, HYPERNOVA_RANGE);
    VECAdd(apex, &axis, &center);

    // Rim points around the base circle, by incremental rotation of (u, v).
    Vec3 rim[HYPERNOVA_DEBUG_CONE_SEGS];
    float cx = 1.0f, cy = 0.0f;
    for (int i = 0; i < HYPERNOVA_DEBUG_CONE_SEGS; i++)
    {
        rim[i].X = center.X + radius * (cx * u.X + cy * v.X);
        rim[i].Y = center.Y + radius * (cx * u.Y + cy * v.Y);
        rim[i].Z = center.Z + radius * (cx * u.Z + cy * v.Z);
        float nx = cx * CONE_STEP_COS - cy * CONE_STEP_SIN;
        float ny = cx * CONE_STEP_SIN + cy * CONE_STEP_COS;
        cx = nx;
        cy = ny;
    }

    // GX state: flat per-vertex color (no texture/lighting), alpha blend, depth-tested but not
    // depth-writing (translucent), both faces drawn. Mirrors the inline GX_DrawRect setup.
    HSD_StateInitDirect(GX_VTXFMT0, 4);
    GXSetNumTevStages(1);
    GXSetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD_NULL, GX_TEXMAP_NULL, GX_COLOR0A0);
    GXSetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
    GXSetNumTexGens(0);
    GXSetNumChans(1);
    // Source both color and alpha of channel 0 from the vertex (no lighting), so the per-vertex
    // alpha reaches the blender - the cone's translucency depends on it.
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

        // Lateral face: apex -> rim[i] -> rim[i+1].
        GXPosition3f32(apex->X, apex->Y, apex->Z);
        GXColor4u8(col->r, col->g, col->b, col->a);
        GXPosition3f32(a->X, a->Y, a->Z);
        GXColor4u8(col->r, col->g, col->b, col->a);
        GXPosition3f32(b->X, b->Y, b->Z);
        GXColor4u8(col->r, col->g, col->b, col->a);

        // Base cap: center -> rim[i+1] -> rim[i] (winding is moot under cull-none).
        GXPosition3f32(center.X, center.Y, center.Z);
        GXColor4u8(col->r, col->g, col->b, col->a);
        GXPosition3f32(b->X, b->Y, b->Z);
        GXColor4u8(col->r, col->g, col->b, col->a);
        GXPosition3f32(a->X, a->Y, a->Z);
        GXColor4u8(col->r, col->g, col->b, col->a);
    }
    HSD_StateInvalidate(-1);
}

// GX callback on the world camera link. Draws each human rider's suction cone on the XLU pass
// (pass 1) so it blends over opaque world geometry; a no-op otherwise.
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
            continue; // no usable facing this frame (matches the vacuum's guard)
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
    OSReport("[HypernovaDebug] Inhale-cone overlay installed\n");
}

void Hypernova_DebugConeReset(void)
{
    // The engine frees every world GObj on scene teardown; just drop our cached handle.
    stc_cone_gobj = NULL;
}
