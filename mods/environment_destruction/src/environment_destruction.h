#ifndef ENVIRONMENT_DESTRUCTION_H
#define ENVIRONMENT_DESTRUCTION_H

#include "datatypes.h"

#include "carve.h"

// Menu-bound settings (defined in environment_destruction.c).
extern int env_destruct_enabled;
extern int env_destruct_selftest;
extern int env_destruct_size_sel;    // index into env_size_table
extern int env_destruct_depth_sel;   // index into env_depth_table
extern int env_destruct_shape_sel;   // index into env_shape_table
extern int env_destruct_fill;        // EnvFillMode
extern int env_destruct_debug_log;

// Damage-source toggles (defined in damage_sources.c).
extern int env_src_spin;
extern int env_src_projectile;
extern int env_src_machine;
extern int env_src_charge;

// How the faces a carve exposes are shaded.
typedef enum EnvFillMode
{
    ENV_FILL_TEXTURE,  // stretch the surface's own texture down into the crater
    ENV_FILL_COLOR,    // flat untextured material on the crater walls
    ENV_FILL_NUM,
} EnvFillMode;

// The solid one impact takes out. A capped shape is a prism: constant bore all
// the way down, so a rider drives its full length at the width it opened, and
// where the back face is buried in material it stands as a flat wall square to
// the axis, which carries the next chunk on in the same direction. An uncapped
// one tapers to a point, so it narrows to nothing a rider cannot follow and the
// sloped face they wedge against sends the next chunk off-axis. That is a worse
// tunnel and a more organic hole, which is the whole reason to offer both.
typedef enum EnvShape
{
    ENV_SHAPE_BORE,      // square prism - straight tunnels
    ENV_SHAPE_PYRAMID4,  // square pyramid
    ENV_SHAPE_PYRAMID3,  // triangular pyramid - the least regular of the three
    ENV_SHAPE_NUM,
} EnvShape;

typedef struct EnvShapeDesc
{
    int sides;
    int capped;
} EnvShapeDesc;

extern const EnvShapeDesc env_shape_table[ENV_SHAPE_NUM];

#define ENV_SIZE_NUM 5
#define ENV_DEPTH_NUM 3
extern const float env_size_table[ENV_SIZE_NUM];    // mouth half-width, world units
extern const float env_depth_table[ENV_DEPTH_NUM];  // depth as a multiple of the half-width

// A surface is treated as floor/ceiling (walkable - never carved) when its
// normal sits within ~60 degrees of vertical, i.e. |ny|/|n| >= cos(60deg).
// Magnitude-independent so an unnormalized normal still classifies. This is what
// keeps riders from falling through the ground the impact overlaps; only
// near-vertical geometry (walls) is carved. Raise toward 1.0 to protect only
// near-flat ground; lower to protect steeper slopes too.
#define ENV_FLOOR_COS2 0.25f   // (cos 60deg)^2

static inline int EnvIsHorizontalSurface(float nx, float ny, float nz)
{
    float len2 = nx * nx + ny * ny + nz * nz;
    return len2 > 0.0f && (ny * ny) >= ENV_FLOOR_COS2 * len2;
}

// Lifecycle (wired to the ModDesc callbacks in main.c).
void EnvDestruct_OnBoot(void);
void EnvDestruct_On3DLoadEnd(void);
void EnvDestruct_On3DExit(void);
void EnvDestruct_OnFrameEnd(void);

// Current menu-selected chunk mouth half-width, in world units.
float EnvDestruct_CurrentSize(void);

// Poll every enabled damage source this frame and carve at their impacts.
// Called from the frame-end hook once inside City Trial gameplay.
void EnvDamage_Poll(void);

// Drop per-player cooldowns and remembered speeds for a fresh stage.
void EnvDamage_Reset(void);

// Take a chunk out of a wall a source has actually struck. hit is the contact
// point and nrm the wall's normal there, pointing back at whatever hit it; the
// mouth opens at hit and the pyramid is driven along -nrm. force scales its
// depth, so a boosted ram punches deeper than a spin. Returns non-zero when the
// impact took anything out. No-op outside City Trial gameplay.
int EnvDestruct_ApplyImpact(Vec3 *hit, Vec3 *nrm, float radius, float force);

// Smallest remnant the visual carve bothers to emit, in square world units.
// Without it a spot carved over and over accumulates slivers that cost arena and
// draw calls while covering no pixels; the collision side has the same rule for
// the same reason, at a much larger threshold because a gap there is one a
// machine drives through.
#define ENV_GEOM_SLIVER_AREA 0.25f

// Visual carve (geometry.c): clip the loaded stage mesh against the volume,
// retire the source triangles it cuts, and build the replacement surface plus
// the lining of the hole as a new mesh hanging off the same joints. with_back
// controls the far face of the lining, exactly as on the collision side, so the
// two agree. Returns triangles generated.
int EnvGeom_CarveVolume(const EnvVolume *vol, int fill_mode, int with_back);

// Source primitives the last visual carve actually took out of the stage mesh.
// Zero means the volume found nothing to cut - an invisible collision wall, or a
// mesh the display-list walker could not decode - and the collision carve has to
// stand down, or it opens a hole in a building that still looks solid.
extern int env_geom_stat_retired;

// Measure and cache a world-space box for every stage mesh, so a carve can skip
// the meshes it does not reach instead of decoding all ~20,000 terrain triangles
// looking for the handful it cuts. Reads the joints' live matrices, so it has to
// run on a gameplay frame; it does nothing after the first call per stage.
void EnvGeom_BuildBounds(void);

// Drop the cached stage-model root, the bounds and every generated patch so the
// next stage starts clean.
void EnvGeom_Reset(void);

// Collision (collision.c): find the wall nearest pos that faces it, within
// radius. Returns 0 when there is nothing to carve.
int EnvColl_FindSurface(Vec3 *pos, float radius, Vec3 *hit_out, Vec3 *nrm_out);

// Collision (collision.c): the wall a contact record names, if the mod may cut
// it. toward points from the wall back at whatever struck it, and orients
// nrm_out. Returns 0 for a triangle out of range, one already retired, a floor,
// or one of the mod's own crater faces.
int EnvColl_WallAt(int tri_id, Vec3 *toward, Vec3 *nrm_out);

// Collision (collision.c): fire the engine's own raycast along dir and report
// the carveable wall it runs into, if any. dir must be unit length. This is what
// makes a carve mean "something hit this wall" rather than "something passed
// near it" - the map's broadphase decides, not a distance guess. nrm_out is
// returned facing back along the ray.
int EnvColl_RayHit(Vec3 *from, Vec3 *dir, float len, Vec3 *hit_out, Vec3 *nrm_out);

// Collision (collision.c): does the bore come out the far side of what it was
// driven into? Walks the segment from the mouth to the back face counting
// surface crossings; the answer decides whether the hole gets a back face at
// all. Capping a bore that broke through would leave an invisible wall standing
// in open air at the end of the tunnel.
int EnvColl_BoreExits(const EnvVolume *vol);

// Collision (collision.c): is there anything of the stage's own left inside the
// volume? A surface the mod generated is not material - it is what it stood in
// the hole where material used to be - so a chunk that reaches nothing else is
// one taken out of open space.
int EnvColl_HasStageMaterial(const EnvVolume *vol);

// Collision carve (collision.c): clip the map-collision triangles against the
// same volume, retire the ones it cuts, and re-add their surviving parts plus
// the lining as live triangles so riders can drive into the hole. Returns
// triangles added.
int EnvColl_CarveVolume(const EnvVolume *vol, int with_back);

// Per-stage collision bookkeeping: clear the slot bank and the vertex pool.
void EnvColl_Reset(void);

// What the last collision carve managed: triangles cut, triangles left solid
// because their remnants had nowhere to go, and slots still banked.
extern int env_coll_stat_cut;
extern int env_coll_stat_skip;

#endif // ENVIRONMENT_DESTRUCTION_H
