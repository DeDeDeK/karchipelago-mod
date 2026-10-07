#include "os.h"
#include "game.h"
#include "obj.h"
#include "rider.h"
#include "gx.h"
#include "inline.h"

#include "hypernova.h"

// Base-circle rim as unit (cos, sin) pairs, seeded once so the per-frame draw does no trig.
static Vec2 stc_cone_unit[HYPERNOVA_DEBUG_CONE_SEGS];

// Orthonormal basis (u, v) spanning the plane perpendicular to unit `aim`.
static void ConeBasis(Vec3 *aim, Vec3 *u, Vec3 *v)
{
    // Reference axis not parallel to aim: world up, unless aim is near-vertical (then world X).
    Vec3 ref = {0.0f, 1.0f, 0.0f};
    if (_fabs(aim->Y) > 0.99f)
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

    // Z-write off and cull-none so the cone reads as a see-through volume.
    GX_BeginXlu(COBJ_GetCurrent(), 4, GX_BL_INVSRCALPHA);

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

    GXColor col = GXColor_Unpack(HYPERNOVA_DEBUG_CONE_RGBA);

    for (int i = 0; i < PLY_NUM; i++)
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
            continue; // a forward shorter than 0.01 is not a usable facing
        DrawConeGX(&rd->pos, &aim, &col);
    }
}

void Hypernova_DebugConeCreate(void)
{
    GOBJ *g = GObj_Create(HYPERNOVA_DEBUG_GOBJ_CLASS, HYPERNOVA_DEBUG_GOBJ_PLINK, 0);
    if (g == NULL)
        return;
    for (int i = 0; i < HYPERNOVA_DEBUG_CONE_SEGS; i++)
    {
        float a = (2.0f * M_PI * i) / HYPERNOVA_DEBUG_CONE_SEGS;
        stc_cone_unit[i].X = cosf(a);
        stc_cone_unit[i].Y = sinf(a);
    }

    GObj_AddGXLink(g, Hypernova_DebugConeGX, HYPERNOVA_DEBUG_GX_LINK, HYPERNOVA_DEBUG_GX_PRI);
}
