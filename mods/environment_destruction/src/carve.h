#ifndef ENVIRONMENT_DESTRUCTION_CARVE_H
#define ENVIRONMENT_DESTRUCTION_CARVE_H

#include "datatypes.h"

#define ENV_SIDES_MAX 8
#define ENV_PLANES_MAX (ENV_SIDES_MAX + 2)
#define ENV_POLY_MAX 16
#define ENV_KEPT_MAX ENV_PLANES_MAX

// Triangles the lining of one chunk can come to: two per side face, plus a fan
// across the back face when the volume has one.
#define ENV_LINING_MAX (ENV_SIDES_MAX * 2 + (ENV_SIDES_MAX - 2))

// Coplanarity tolerance, world units. Vertices within this of a plane count as
// lying on it.
#define ENV_PLANE_EPS 0.05f

// How far outside the impact point the front cap sits. It only has to be enough
// that a contact point reported off the surface still leaves the surface inside
// the volume. On a prism it costs nothing else - the sides run parallel to the
// axis, so pushing the cap out does not widen the mouth.
#define ENV_MOUTH_CLEARANCE 4.0f

// The same, for a pyramid. Its sides flare over this distance, so a surface
// carved in front of the impact point loses a wider disc than the wall behind it
// and grows crater walls that stand out of the building instead of running into
// it. Short enough that the mouth is close to the half-width that was asked for.
#define ENV_TAPER_CLEARANCE 2.0f

// How far in front of the impact surface the lining starts. The lining is built
// on the volume's own side planes, so it meets the cut rim exactly; the lip is
// what covers the gap where the real surface sits proud of the impact plane.
#define ENV_LINING_LIP 1.0f

// How far behind the surface the lining of a bore that broke through may run.
// City Trial's walls are single planes: past the face there is no material for
// the tube to be the inside of, so every unit of it beyond a plausible wall
// thickness is a box standing in the open. Only a bore buried in material runs
// its full depth, where the tube is the inside of something and nothing sees it.
// Measured against the stage, walls with a face behind them sit at 3 to 13 world
// units thick.
#define ENV_LINING_DEPTH 6.0f

// Half-space with the carve volume on the negative side: a point is inside when
// dot(n, p) + d <= 0. n is unit length.
typedef struct EnvPlane
{
    Vec3 n;
    float d;
} EnvPlane;

// The chunk a single impact takes out: a convex frustum driven into the surface
// along -axis. plane[0 .. n_sides-1] are the sides, plane[n_sides] is the front
// cap just outside the surface, and plane[n_sides+1] is the back cap when the
// volume is capped. Inside means inside all of them.
//
// A capped volume is a prism - constant cross-section - so a rider can drive its
// whole length at the width it opened, and a back face buried in material gives
// them something square to the axis to bottom out against, which lets the next
// chunk carry the tunnel straight on. An uncapped one is a pyramid: `back`
// collapses onto `apex` and the bore tapers to a point, which wanders as it
// deepens.
typedef struct EnvVolume
{
    Vec3 apex;                   // centre of the back face
    Vec3 center;                 // impact point on the surface
    Vec3 corner[ENV_SIDES_MAX];  // mouth ring, out beyond the surface
    Vec3 back[ENV_SIDES_MAX];    // back ring; every entry is apex when uncapped
    float mouth_t;               // fraction along corner->back where the surface sits
    float lip_t;                 // same for the plane the lining starts on
    float tail_t;                // same for where a broken-through bore's lining ends
    float radius;                // bounding sphere about center, covers the whole volume
    int n_sides;
    int n_planes;                // n_sides + 1, or n_sides + 2 when capped
    int capped;
    EnvPlane plane[ENV_PLANES_MAX];
} EnvVolume;

// The direction the chunk was driven from, i.e. out of the surface toward
// whatever struck it. Unit length in whatever space the volume is expressed in.
static inline const Vec3 *EnvVolume_Axis(const EnvVolume *v)
{
    return &v->plane[v->n_sides].n;
}

// One generated vertex. Attribute set is a superset of what any source mesh
// carries; the emitter writes back only the attributes its target POBJ declares.
typedef struct EnvVtx
{
    Vec3 pos;
    Vec3 nrm;
    float s, t;
    u32 clr;
} EnvVtx;

typedef struct EnvPoly
{
    int n;
    EnvVtx v[ENV_POLY_MAX];
} EnvPoly;

// One face of the lining, with its normal already pointing into the cavity so a
// rider inside is pushed back out into the material.
typedef struct EnvLinTri
{
    Vec3 v[3];
    Vec3 n;
    // Distance along two perpendicular in-plane axes of the face this triangle
    // belongs to, from a shared origin on that face. The visual emitter scales
    // these by the source wall's texel density to carry its texture into the
    // hole; every face has its own frame, so the texture runs true across each
    // one and only breaks at the creases between them.
    float uv[3][2];
} EnvLinTri;

// Build the carve volume for an impact. hit is the surface point, into is the
// direction the impact drives (pointing into the surface, need not be unit), half
// is the mouth half-width, depth is how far the back face sits behind hit.
// capped picks a prism over a pyramid.
void EnvVolume_Build(EnvVolume *v, Vec3 *hit, Vec3 *into, float half, float depth,
                     int n_sides, int capped);

// Re-express a world-space volume in a joint's local space. world is the joint
// matrix; plane normals ride its transpose, points ride its inverse.
int EnvVolume_ToLocal(const EnvVolume *src, MtxPtr world, EnvVolume *dst);

// Cheap reject: 1 when the triangle cannot touch the volume.
int EnvVolume_TriOutside(const EnvVolume *v, Vec3 *a, Vec3 *b, Vec3 *c);

// Split a triangle into the parts that survive the carve and the part inside it.
// kept must hold ENV_KEPT_MAX polygons. Returns the kept count; removed->n is 0
// when the triangle misses the volume entirely (in which case kept is empty and
// the caller must leave the source triangle alone).
int EnvVolume_CarveTri(const EnvVolume *v, EnvVtx *tri, EnvPoly *kept, EnvPoly *removed);

// The ring where the volume crosses the plane `t` of the way from the mouth to
// the back face. out must hold ENV_SIDES_MAX entries.
void EnvVolume_Ring(const EnvVolume *v, float t, Vec3 *out);

// The surface of the hole: a tube from the lining ring down to the back ring,
// plus the back face itself when with_back is set. Built from the volume rather
// than from the triangles it cut, so the hole is lined however coarsely the wall
// was tessellated and the visual and collision linings are the same geometry.
//
// A bore that broke through stops its tube at `tail_t` and leaves it open, since
// past the wall there is nothing for it to be the inside of. A pyramid always
// runs to its apex: it closes on itself, so whatever of it lies beyond the wall
// is a dent from the only side anyone sees it from.
//
// out must hold ENV_LINING_MAX entries. Returns how many were written.
int EnvVolume_Lining(const EnvVolume *v, int with_back, EnvLinTri *out);

#endif // ENVIRONMENT_DESTRUCTION_CARVE_H
