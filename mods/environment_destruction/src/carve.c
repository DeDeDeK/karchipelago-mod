#include "os.h"

#include "carve.h"

static float Dot(Vec3 *a, Vec3 *b)
{
    return a->X * b->X + a->Y * b->Y + a->Z * b->Z;
}

static void Cross(Vec3 *a, Vec3 *b, Vec3 *out)
{
    out->X = a->Y * b->Z - a->Z * b->Y;
    out->Y = a->Z * b->X - a->X * b->Z;
    out->Z = a->X * b->Y - a->Y * b->X;
}

static float Normalize(Vec3 *v)
{
    float len2 = Dot(v, v);
    float len, inv;

    if (len2 <= 1e-12f)
        return 0.0f;
    len = sqrtf(len2);
    inv = 1.0f / len;
    v->X *= inv;
    v->Y *= inv;
    v->Z *= inv;
    return len;
}

static float PlaneDist(const EnvPlane *p, Vec3 *v)
{
    return p->n.X * v->X + p->n.Y * v->Y + p->n.Z * v->Z + p->d;
}

// Plane through three points, flipped so that `inside` lands on the negative side.
static void PlaneFrom3(EnvPlane *p, Vec3 *a, Vec3 *b, Vec3 *c, Vec3 *inside)
{
    Vec3 u, w;

    u.X = b->X - a->X; u.Y = b->Y - a->Y; u.Z = b->Z - a->Z;
    w.X = c->X - a->X; w.Y = c->Y - a->Y; w.Z = c->Z - a->Z;
    Cross(&u, &w, &p->n);
    Normalize(&p->n);
    p->d = -Dot(&p->n, a);
    if (PlaneDist(p, inside) > 0.0f)
    {
        p->n.X = -p->n.X;
        p->n.Y = -p->n.Y;
        p->n.Z = -p->n.Z;
        p->d = -p->d;
    }
}

void EnvVolume_Build(EnvVolume *v, Vec3 *hit, Vec3 *into, float half, float depth,
                     int n_sides, int capped)
{
    Vec3 dir, axis, u, w, mouth, mid;
    Vec3 *corner = v->corner;
    Vec3 *back = v->back;
    float mouth_out, span, lip;
    int k;

    if (n_sides < 3)
        n_sides = 3;
    if (n_sides > ENV_SIDES_MAX)
        n_sides = ENV_SIDES_MAX;

    dir = *into;
    if (Normalize(&dir) == 0.0f)
    {
        dir.X = 0.0f;
        dir.Y = -1.0f;
        dir.Z = 0.0f;
    }
    axis.X = -dir.X;
    axis.Y = -dir.Y;
    axis.Z = -dir.Z;

    // Any vector not parallel to the axis seeds the basis of the mouth plane. u
    // ends up along world up for a wall carve, which with the half-step phase
    // below puts one side face flat across the floor of the bore and one across
    // its ceiling - the rider drives on the first and both classify as ground
    // rather than as something a later chunk may cut out from under them.
    u.X = 0.0f; u.Y = 1.0f; u.Z = 0.0f;
    if (axis.Y > 0.9f || axis.Y < -0.9f)
    {
        u.X = 1.0f; u.Y = 0.0f; u.Z = 0.0f;
    }
    Cross(&axis, &u, &w);
    Normalize(&w);
    Cross(&w, &axis, &u);
    Normalize(&u);

    mouth_out = capped ? ENV_MOUTH_CLEARANCE : ENV_TAPER_CLEARANCE;
    if (mouth_out > half * 0.5f)
        mouth_out = half * 0.5f;
    mouth.X = hit->X + axis.X * mouth_out;
    mouth.Y = hit->Y + axis.Y * mouth_out;
    mouth.Z = hit->Z + axis.Z * mouth_out;

    v->apex.X = hit->X + dir.X * depth;
    v->apex.Y = hit->Y + dir.Y * depth;
    v->apex.Z = hit->Z + dir.Z * depth;
    v->center = *hit;
    v->n_sides = n_sides;
    v->capped = capped ? 1 : 0;
    v->n_planes = n_sides + 1 + v->capped;

    for (k = 0; k < n_sides; k++)
    {
        float a = (6.2831853f / (float)n_sides) * ((float)k + 0.5f);
        float cs = cosf(a) * half;
        float sn = sinf(a) * half;
        corner[k].X = mouth.X + u.X * cs + w.X * sn;
        corner[k].Y = mouth.Y + u.Y * cs + w.Y * sn;
        corner[k].Z = mouth.Z + u.Z * cs + w.Z * sn;
        if (v->capped)
        {
            back[k].X = v->apex.X + u.X * cs + w.X * sn;
            back[k].Y = v->apex.Y + u.Y * cs + w.Y * sn;
            back[k].Z = v->apex.Z + u.Z * cs + w.Z * sn;
        }
        else
        {
            back[k] = v->apex;
        }
    }

    mid.X = (mouth.X + v->apex.X) * 0.5f;
    mid.Y = (mouth.Y + v->apex.Y) * 0.5f;
    mid.Z = (mouth.Z + v->apex.Z) * 0.5f;

    for (k = 0; k < n_sides; k++)
        PlaneFrom3(&v->plane[k], &corner[k], &corner[(k + 1) % n_sides], &back[k], &mid);

    v->plane[n_sides].n = axis;
    v->plane[n_sides].d = -Dot(&axis, &mouth);
    if (v->capped)
    {
        v->plane[n_sides + 1].n.X = -axis.X;
        v->plane[n_sides + 1].n.Y = -axis.Y;
        v->plane[n_sides + 1].n.Z = -axis.Z;
        v->plane[n_sides + 1].d = Dot(&axis, &v->apex);
    }

    // Every mouth-to-back edge spans the same axial distance, so the plane at a
    // given depth cuts them all at the same fraction.
    span = mouth_out + depth;
    if (span < 1e-4f)
        span = 1e-4f;
    lip = (ENV_LINING_LIP < mouth_out) ? ENV_LINING_LIP : mouth_out;
    v->mouth_t = mouth_out / span;
    v->lip_t = (mouth_out - lip) / span;
    // Held as a fraction rather than a distance so it survives the transform into
    // a joint's local space, where a world unit is not one unit.
    v->tail_t = (mouth_out + ((ENV_LINING_DEPTH < depth) ? ENV_LINING_DEPTH : depth))
                / span;

    v->radius = depth;
    for (k = 0; k < n_sides; k++)
    {
        Vec3 e;
        float len;
        int j;

        for (j = 0; j < 2; j++)
        {
            Vec3 *p = j ? &back[k] : &corner[k];
            e.X = p->X - hit->X;
            e.Y = p->Y - hit->Y;
            e.Z = p->Z - hit->Z;
            len = sqrtf(Dot(&e, &e));
            if (len > v->radius)
                v->radius = len;
        }
    }
}

int EnvVolume_ToLocal(const EnvVolume *src, MtxPtr world, EnvVolume *dst)
{
    Mtx inv;
    int i;

    if (MTXInverse(world, inv) == 0)
        return 0; // singular joint matrix: skip this joint rather than guess

    for (i = 0; i < src->n_planes; i++)
    {
        Vec3 *ns = (Vec3 *)&src->plane[i].n;
        EnvPlane *p = &dst->plane[i];
        float scale;

        // A plane rides the transpose of the linear part; the translation folds
        // into the constant. dot(n, M*p) + d == dot(M^T*n, p) + dot(n, t) + d.
        p->n.X = world[0][0] * ns->X + world[1][0] * ns->Y + world[2][0] * ns->Z;
        p->n.Y = world[0][1] * ns->X + world[1][1] * ns->Y + world[2][1] * ns->Z;
        p->n.Z = world[0][2] * ns->X + world[1][2] * ns->Y + world[2][2] * ns->Z;
        p->d = src->plane[i].d +
               world[0][3] * ns->X + world[1][3] * ns->Y + world[2][3] * ns->Z;

        // Renormalize so the plane distances stay world-comparable under joint
        // scale, which keeps ENV_PLANE_EPS meaningful.
        scale = Normalize(&p->n);
        if (scale == 0.0f)
            return 0;
        p->d /= scale;
    }

    PSMTXMultVec(inv, (Vec3 *)&src->apex, &dst->apex);
    PSMTXMultVec(inv, (Vec3 *)&src->center, &dst->center);
    for (i = 0; i < src->n_sides; i++)
    {
        PSMTXMultVec(inv, (Vec3 *)&src->corner[i], &dst->corner[i]);
        PSMTXMultVec(inv, (Vec3 *)&src->back[i], &dst->back[i]);
    }
    dst->mouth_t = src->mouth_t;
    dst->lip_t = src->lip_t;
    dst->tail_t = src->tail_t;
    dst->n_sides = src->n_sides;
    dst->n_planes = src->n_planes;
    dst->capped = src->capped;
    dst->radius = src->radius;
    return 1;
}

void EnvVolume_Ring(const EnvVolume *v, float t, Vec3 *out)
{
    int k;

    for (k = 0; k < v->n_sides; k++)
    {
        out[k].X = v->corner[k].X + (v->back[k].X - v->corner[k].X) * t;
        out[k].Y = v->corner[k].Y + (v->back[k].Y - v->corner[k].Y) * t;
        out[k].Z = v->corner[k].Z + (v->back[k].Z - v->corner[k].Z) * t;
    }
}

// The frame a face of the lining is textured in: an origin on the face and two
// perpendicular in-plane axes.
typedef struct LinFrame
{
    Vec3 org, ax0, ax1;
} LinFrame;

// A face of the lining lies in one of the volume's own planes, so its normal is
// that plane's - inverted, because a plane normal points out of the volume and a
// rider in the cavity has to be pushed the other way, back into the material.
static int LinAdd(EnvLinTri *out, int n, const EnvPlane *p, const LinFrame *f,
                  Vec3 *a, Vec3 *b, Vec3 *c)
{
    Vec3 *src[3];
    Vec3 u, w, area;
    int i;

    u.X = b->X - a->X; u.Y = b->Y - a->Y; u.Z = b->Z - a->Z;
    w.X = c->X - a->X; w.Y = c->Y - a->Y; w.Z = c->Z - a->Z;
    Cross(&u, &w, &area);
    if (Dot(&area, &area) < 1e-8f)
        return n; // a pyramid's side quad collapses onto its apex

    src[0] = a;
    src[1] = b;
    src[2] = c;
    for (i = 0; i < 3; i++)
    {
        Vec3 d;

        out[n].v[i] = *src[i];
        d.X = src[i]->X - f->org.X;
        d.Y = src[i]->Y - f->org.Y;
        d.Z = src[i]->Z - f->org.Z;
        out[n].uv[i][0] = Dot(&d, (Vec3 *)&f->ax0);
        out[n].uv[i][1] = Dot(&d, (Vec3 *)&f->ax1);
    }
    out[n].n.X = -p->n.X;
    out[n].n.Y = -p->n.Y;
    out[n].n.Z = -p->n.Z;
    return n + 1;
}

// Two perpendicular unit vectors in the plane of `n`, the first along `along`
// where that is usable. Falls back to whatever is perpendicular to n so a
// degenerate face still gets a frame rather than a division by zero.
static void LinFrameFrom(LinFrame *f, Vec3 *org, Vec3 *n, Vec3 *along)
{
    Vec3 seed;

    f->org = *org;
    f->ax0 = *along;
    if (Normalize(&f->ax0) == 0.0f)
    {
        seed.X = 0.0f; seed.Y = 1.0f; seed.Z = 0.0f;
        if (n->Y > 0.9f || n->Y < -0.9f)
        {
            seed.X = 1.0f; seed.Y = 0.0f;
        }
        Cross(n, &seed, &f->ax0);
        Normalize(&f->ax0);
    }
    Cross(n, &f->ax0, &f->ax1);
    Normalize(&f->ax1);
}

int EnvVolume_Lining(const EnvVolume *v, int with_back, EnvLinTri *out)
{
    Vec3 ring[ENV_SIDES_MAX], tail[ENV_SIDES_MAX];
    const Vec3 *end = v->back;
    LinFrame f;
    Vec3 along;
    int k, n = 0;

    EnvVolume_Ring(v, v->lip_t, ring);

    // A bore is lined only as deep as the wall it went into. Past that its length
    // has no material around it to be the inside of, and standing the tube there
    // anyway is what puts an open box out in the street - the depth setting runs
    // to three times the mouth width, which reaches clear across one.
    if (v->capped && v->tail_t < 1.0f)
    {
        EnvVolume_Ring(v, v->tail_t, tail);
        end = tail;
    }

    for (k = 0; k < v->n_sides; k++)
    {
        int k1 = (k + 1) % v->n_sides;
        const EnvPlane *p = &v->plane[k];

        // Textured along the bore: the first axis runs from the mouth to the
        // back, so the wall's texture is dragged into the hole rather than
        // wrapped around it.
        along.X = end[k].X - ring[k].X;
        along.Y = end[k].Y - ring[k].Y;
        along.Z = end[k].Z - ring[k].Z;
        LinFrameFrom(&f, &ring[k], (Vec3 *)&p->n, &along);

        n = LinAdd(out, n, p, &f, &ring[k], &ring[k1], (Vec3 *)&end[k1]);
        n = LinAdd(out, n, p, &f, &ring[k], (Vec3 *)&end[k1], (Vec3 *)&end[k]);
    }

    // Closed at whatever end the tube actually stops at. A cap left back at the
    // volume's own back face would float clear of the tube it is meant to close.
    if (with_back && v->capped)
    {
        const EnvPlane *p = &v->plane[v->n_sides + 1];

        along.X = end[1].X - end[0].X;
        along.Y = end[1].Y - end[0].Y;
        along.Z = end[1].Z - end[0].Z;
        LinFrameFrom(&f, (Vec3 *)&end[0], (Vec3 *)&p->n, &along);

        for (k = 1; k + 1 < v->n_sides; k++)
            n = LinAdd(out, n, p, &f, (Vec3 *)&end[0],
                       (Vec3 *)&end[k], (Vec3 *)&end[k + 1]);
    }

    return n;
}

int EnvVolume_TriOutside(const EnvVolume *v, Vec3 *a, Vec3 *b, Vec3 *c)
{
    int i;

    for (i = 0; i < v->n_planes; i++)
    {
        const EnvPlane *p = &v->plane[i];
        // On the plane counts as outside. A triangle lying in one of the volume's
        // own faces encloses none of it, and the ones that do lie there are the
        // lining an earlier chunk left behind - treating them as cuttable stands
        // a second copy of every one of them on top of the first.
        if (PlaneDist(p, a) > -ENV_PLANE_EPS && PlaneDist(p, b) > -ENV_PLANE_EPS &&
            PlaneDist(p, c) > -ENV_PLANE_EPS)
            return 1;
    }
    return 0;
}

static void VtxLerp(EnvVtx *a, EnvVtx *b, float t, EnvVtx *out)
{
    int i;

    out->pos.X = a->pos.X + (b->pos.X - a->pos.X) * t;
    out->pos.Y = a->pos.Y + (b->pos.Y - a->pos.Y) * t;
    out->pos.Z = a->pos.Z + (b->pos.Z - a->pos.Z) * t;
    out->nrm.X = a->nrm.X + (b->nrm.X - a->nrm.X) * t;
    out->nrm.Y = a->nrm.Y + (b->nrm.Y - a->nrm.Y) * t;
    out->nrm.Z = a->nrm.Z + (b->nrm.Z - a->nrm.Z) * t;
    out->s = a->s + (b->s - a->s) * t;
    out->t = a->t + (b->t - a->t) * t;

    out->clr = 0;
    for (i = 0; i < 32; i += 8)
    {
        float ca = (float)((a->clr >> i) & 0xFF);
        float cb = (float)((b->clr >> i) & 0xFF);
        out->clr |= ((u32)(ca + (cb - ca) * t) & 0xFF) << i;
    }
}

static void PolyAdd(EnvPoly *p, EnvVtx *v)
{
    if (p->n < ENV_POLY_MAX)
        p->v[p->n++] = *v;
}

// One pass of convex clipping that keeps both halves. Vertices within
// ENV_PLANE_EPS of the plane join both, so the shared edge is exactly coplanar.
static void SplitPoly(EnvPoly *in, const EnvPlane *p, EnvPoly *outside, EnvPoly *inside)
{
    int i;

    outside->n = 0;
    inside->n = 0;

    for (i = 0; i < in->n; i++)
    {
        EnvVtx *a = &in->v[i];
        EnvVtx *b = &in->v[(i + 1) % in->n];
        float fa = PlaneDist(p, &a->pos);
        float fb = PlaneDist(p, &b->pos);

        if (fa <= ENV_PLANE_EPS)
            PolyAdd(inside, a);
        if (fa >= -ENV_PLANE_EPS)
            PolyAdd(outside, a);

        if ((fa < -ENV_PLANE_EPS && fb > ENV_PLANE_EPS) ||
            (fa > ENV_PLANE_EPS && fb < -ENV_PLANE_EPS))
        {
            EnvVtx m;
            VtxLerp(a, b, fa / (fa - fb), &m);
            PolyAdd(inside, &m);
            PolyAdd(outside, &m);
        }
    }
}

int EnvVolume_CarveTri(const EnvVolume *v, EnvVtx *tri, EnvPoly *kept, EnvPoly *removed)
{
    // Static rather than automatic: an EnvPoly is most of a kilobyte and this is
    // called from the bottom of the stage tree walk. Only one carve runs at a time.
    static EnvPoly cur, outside, inside;
    int i, n_kept = 0;

    removed->n = 0;
    if (EnvVolume_TriOutside(v, &tri[0].pos, &tri[1].pos, &tri[2].pos))
        return 0;

    cur.n = 3;
    cur.v[0] = tri[0];
    cur.v[1] = tri[1];
    cur.v[2] = tri[2];

    // Peeling the outside of one plane at a time decomposes tri \ volume into
    // convex pieces; whatever survives every plane is the removed chunk.
    for (i = 0; i < v->n_planes; i++)
    {
        SplitPoly(&cur, &v->plane[i], &outside, &inside);
        if (outside.n >= 3)
            kept[n_kept++] = outside;
        if (inside.n < 3)
        {
            removed->n = 0;
            return 0; // grazes the volume without enclosing area: leave it whole
        }
        cur = inside;
    }

    *removed = cur;
    return n_kept;
}
