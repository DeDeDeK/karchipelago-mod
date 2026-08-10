#include <string.h>

#include "os.h"
#include "hsd.h"
#include "scene.h"
#include "game.h"
#include "rider.h"

#include "environment_destruction.h"
#include "carve.h"
#include "patch_mesh.h"
#include "coll_pool.h"

int env_destruct_enabled   = 0;
int env_destruct_selftest  = 0;
int env_destruct_size_sel  = 1;
int env_destruct_depth_sel = 0;
int env_destruct_shape_sel = ENV_SHAPE_BORE;
int env_destruct_fill      = ENV_FILL_TEXTURE;
int env_destruct_debug_log = 0;

const EnvShapeDesc env_shape_table[ENV_SHAPE_NUM] =
{
    { 4, 1 },  // ENV_SHAPE_BORE
    { 4, 0 },  // ENV_SHAPE_PYRAMID4
    { 3, 0 },  // ENV_SHAPE_PYRAMID3
};

// Mouth half-width. A machine is on the order of twenty units, so Medium opens a
// gap a rider can just fit through and the sizes either side of it are a scuff
// and a doorway. Stage triangles are far larger than any of these, which is fine
// - the clip cuts the exact chunk out of whatever triangle it lands on.
const float env_size_table[ENV_SIZE_NUM] = { 6.0f, 12.0f, 20.0f, 32.0f, 50.0f };

// Depth as a multiple of the mouth half-width. Past ~2x the pyramid runs out the
// far side of a normal wall and the chunk reads as a tunnel.
const float env_depth_table[ENV_DEPTH_NUM] = { 0.8f, 1.6f, 3.0f };

// Reach of the self-test's wall search. Only the D-Pad trigger uses it; the real
// damage sources are told exactly which wall they struck.
#define ENV_SELFTEST_REACH 35.0f

static int InCityTrialGameplay(void)
{
    if (Scene_GetCurrentMajor() != MJRKIND_CITY)
        return 0;
    if (Scene_GetCurrentMinor() != MNRKIND_3D)
        return 0;
    if (Gm_GetIntroState() != GMINTRO_END)
        return 0;
    return 1;
}

float EnvDestruct_CurrentSize(void)
{
    int i = env_destruct_size_sel;
    if (i < 0 || i >= ENV_SIZE_NUM)
        i = 0;
    return env_size_table[i];
}

static float CurrentDepthScale(void)
{
    int i = env_destruct_depth_sel;
    if (i < 0 || i >= ENV_DEPTH_NUM)
        i = 0;
    return env_depth_table[i];
}

static const EnvShapeDesc *CurrentShape(void)
{
    int i = env_destruct_shape_sel;
    if (i < 0 || i >= ENV_SHAPE_NUM)
        i = ENV_SHAPE_BORE;
    return &env_shape_table[i];
}

// A wall a carve retires is no longer carveable, so a source leaning on one
// stops producing chunks on its own. This only has to keep two sources landing
// on the same spot in the same instant from each paying for the other's work.
// It stays short and tight: a machine covers about one world unit a frame at top
// speed, so anything wider than a few units would forbid the second bite of a
// tunnel as well.
#define ENV_RECENT_NUM   16
#define ENV_RECENT_HOLD  20     // frames a chunk keeps its spot reserved
#define ENV_RECENT_SPAN  0.34f  // fraction of the mouth width that counts as the same spot
#define ENV_RECENT_MIN   3.0f   // floor on that, so small chunks still cannot drill

typedef struct EnvRecent
{
    Vec3 pos;
    int frame;
} EnvRecent;

static EnvRecent stc_recent[ENV_RECENT_NUM];
static int stc_recent_at;
static int stc_frame;

static int RecentlyCarved(Vec3 *p, float radius)
{
    float span = radius * ENV_RECENT_SPAN;
    float span2;
    int i;

    if (span < ENV_RECENT_MIN)
        span = ENV_RECENT_MIN;
    span2 = span * span;

    for (i = 0; i < ENV_RECENT_NUM; i++)
    {
        float dx, dy, dz;

        if (stc_recent[i].frame == 0 || stc_frame - stc_recent[i].frame > ENV_RECENT_HOLD)
            continue;
        dx = p->X - stc_recent[i].pos.X;
        dy = p->Y - stc_recent[i].pos.Y;
        dz = p->Z - stc_recent[i].pos.Z;
        if (dx * dx + dy * dy + dz * dz <= span2)
            return 1;
    }
    return 0;
}

static void NoteCarve(Vec3 *p)
{
    stc_recent[stc_recent_at].pos = *p;
    stc_recent[stc_recent_at].frame = stc_frame;
    stc_recent_at = (stc_recent_at + 1) % ENV_RECENT_NUM;
}

int EnvDestruct_ApplyImpact(Vec3 *hit, Vec3 *nrm, float radius, float force)
{
    const EnvShapeDesc *shape = CurrentShape();
    EnvVolume vol;
    Vec3 into;
    float depth;
    int tris, coll, with_back;

    if (!env_destruct_enabled)
        return 0;
    if (!InCityTrialGameplay())
        return 0;
    // No collision budget means any hole made here would be one a rider drives
    // straight through, so nothing is carved at all.
    if (!EnvPool_Installed())
        return 0;
    if (RecentlyCarved(hit, radius))
        return 0;
    NoteCarve(hit);

    into.X = -nrm->X;
    into.Y = -nrm->Y;
    into.Z = -nrm->Z;

    depth = radius * CurrentDepthScale() * force;
    if (depth < radius * 0.25f)
        depth = radius * 0.25f;

    EnvVolume_Build(&vol, hit, &into, radius, depth, shape->sides, shape->capped);

    // A generated surface is a real wall as far as every damage source is
    // concerned, so a rider who has bored out the far side of a building goes on
    // striking the back of the last chunk and standing a new one in front of it,
    // all the way across the map. Nothing of the stage's own inside the volume
    // means there is nothing here to take out.
    if (!EnvColl_HasStageMaterial(&vol))
    {
        if (env_destruct_debug_log)
            OSReport("[EnvDestruct] No stage material @ (%d,%d,%d) r=%d, skipped\n",
                     (int)hit->X, (int)hit->Y, (int)hit->Z, (int)radius);
        return 0;
    }

    // The back face is what a rider bottoms out against, and what the next chunk
    // is then driven off. It only belongs there while the bore is still inside
    // material: capping one that came out the far side stands a wall nothing
    // draws in open air at the end of the tunnel.
    with_back = shape->capped ? !EnvColl_BoreExits(&vol) : 0;

    tris = EnvGeom_CarveVolume(&vol, env_destruct_fill, with_back);

    // Collision follows the visible hole and never leads it. Cutting collision
    // the mesh did not lose would let a rider drive through a building that
    // still looks whole.
    coll = (env_geom_stat_retired > 0) ? EnvColl_CarveVolume(&vol, with_back) : 0;

    if (env_destruct_debug_log)
        OSReport("[EnvDestruct] Chunk @ (%d,%d,%d) r=%d d=%d %s -> %d tris (%d cut), coll cut %d "
                 "skip %d placed %d pool %d, arena %d/%d\n",
                 (int)hit->X, (int)hit->Y, (int)hit->Z, (int)radius, (int)depth,
                 shape->capped ? (with_back ? "capped" : "through") : "tapered", tris,
                 env_geom_stat_retired, env_coll_stat_cut, env_coll_stat_skip, coll,
                 EnvPool_Free(), EnvPatch_ArenaUsed(), EnvPatch_ArenaSize());

    // The mesh is cut before the collision is, so a carve that only managed the
    // visual half still counts as a chunk taken. Reporting it as nothing done
    // would leave the source free to strike the same spot again next frame and
    // spend the mesh arena on a hole no rider can drive into.
    return (coll > 0 || env_geom_stat_retired > 0);
}

void EnvDestruct_OnBoot(void)
{
    EnvPatch_OnBoot();
    // Must be in place before any stage builds its collision arrays.
    EnvPool_OnBoot();
    OSReport("[EnvDestruct] Booted (%s), mesh arena %d bytes\n",
             env_destruct_enabled ? "on" : "off", EnvPatch_ArenaSize());
}

void EnvDestruct_On3DLoadEnd(void)
{
    // Fresh stage archive: everything carved into the old one went with it.
    EnvGeom_Reset();
    EnvColl_Reset();
    EnvDamage_Reset();
    memset(stc_recent, 0, sizeof(stc_recent));
    stc_recent_at = 0;
    stc_frame = 0;

    if (Scene_GetCurrentMajor() == MJRKIND_CITY && Scene_GetCurrentMinor() == MNRKIND_3D)
        EnvPool_Install();
}

void EnvDestruct_On3DExit(void)
{
    EnvGeom_Reset();
    EnvColl_Reset();
}

// Manual trigger: D-Pad Up carves at player 1's position, for validating the
// pipeline in Dolphin independently of the damage sources.
static void SelfTest(void)
{
    GOBJ *rg;
    RiderData *rd;
    Vec3 hit, nrm;
    float radius;

    if (!env_destruct_selftest)
        return;
    if ((stc_engine_pads[0].down & PAD_BUTTON_DPAD_UP) == 0)
        return;

    rg = Ply_GetRiderGObj(0);
    if (rg == NULL)
        return;
    rd = (RiderData *)rg->userdata;
    if (rd == NULL)
        return;

    // Unlike the damage sources this reaches for whatever wall is nearest, so
    // the pipeline can be fired without lining up a real hit first.
    radius = EnvDestruct_CurrentSize();
    if (!EnvColl_FindSurface(&rd->pos, radius + ENV_SELFTEST_REACH, &hit, &nrm))
        return;
    EnvDestruct_ApplyImpact(&hit, &nrm, radius, 1.0f);
}

void EnvDestruct_OnFrameEnd(void)
{
    if (!env_destruct_enabled)
        return;
    if (!InCityTrialGameplay())
        return;

    // Measured off the joints' live matrices, so the first gameplay frame is the
    // earliest this can run - and running it here rather than on the first carve
    // keeps its one-off cost out of the middle of the action.
    EnvGeom_BuildBounds();

    stc_frame++;
    SelfTest();
    EnvDamage_Poll();
}
