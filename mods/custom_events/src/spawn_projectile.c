#include <string.h>

#include "os.h"
#include "game.h"
#include "projectile.h"

#include "spawn_projectile.h"

// Outbound impulse added to the machine's world velocity. Inheriting only
// md->velocity leaves the projectile co-moving with Kirby until it drops.
#define THROW_SPEED 30.0f

// Builds the spawn from the machine's pos/forward/up instead of the rider's
// hand bone, so no copy ability has to be active.
//
// Bomb and sensor bomb leave Projectile_Create in state 0 (held) and just need
// Projectile_SetState(1) to start flying. Gordo also needs the per-kind scratch
// (rotation cache, accel, lifetime) that only Gordo_EnterThrownState sets up.
static int SpawnProjectileForPlayer(int ply_idx, ProjectileKind kind, float distance)
{
    GOBJ *mg = Ply_GetMachineGObj(ply_idx);
    if (!mg)
        return 0;
    MachineData *md = mg->userdata;
    if (!md)
        return 0;

    // Projectile_Create copies these into proj->owner_gobj. The gordo path needs
    // a real rider GObj there - it reads bone basis vectors through it.
    int owner = 0;
    GOBJ *rg = Ply_GetRiderGObj(ply_idx);
    if (rg && rg->userdata)
        owner = *(int *)rg->userdata;  // rd->x0

    Vec3 throw_pos;
    throw_pos.X = md->pos.X + md->forward.X * distance;
    throw_pos.Y = md->pos.Y + md->forward.Y * distance;
    throw_pos.Z = md->pos.Z + md->forward.Z * distance;

    Vec3 throw_vel;
    throw_vel.X = md->velocity.X + md->forward.X * THROW_SPEED;
    throw_vel.Y = md->velocity.Y + md->forward.Y * THROW_SPEED;
    throw_vel.Z = md->velocity.Z + md->forward.Z * THROW_SPEED;

    ProjectileDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.kind = kind;
    desc.owner_gobj = (void *)owner;
    desc.owner_unk2 = owner;
    desc.owner_byte = 0;
    desc.position = throw_pos;
    desc.forward = md->forward;
    desc.up = md->up;
    desc.velocity_scale = 1.0f;
    desc.velocity = throw_vel;
    desc.type_flag = 1;
    desc.charge = 1.0f;

    GOBJ *handle = Projectile_Create(&desc);
    if (!handle)
        return 0;

    ProjectileData *proj = (ProjectileData *)handle->userdata;
    if (!proj)
        return 0;

    // The trapped player is the owner, so owner-exclusion would drop the hit
    // unless both scan paths opt in to self-hit.
    proj->flag_a |= PROJ_ALLOW_SELF_HIT_INBOUND;
    proj->flag_b |= PROJ_ALLOW_SELF_HIT_OUTBOUND;

    if (kind == PROJKIND_GORDO)
    {
        Gordo_EnterThrownState(handle, &throw_vel, &throw_pos);
    }
    else
    {
        int throw_state = (kind == PROJKIND_BOMB)
            ? BOMB_STATE_THROWN
            : SENSOR_BOMB_STATE_ARMED_FLYING;
        // Projectile_Create only snapshots desc.velocity at proj+0x88; per-frame
        // physics reads proj+0x94, which vanilla throw seeds before SetState.
        proj->velocity = throw_vel;
        Projectile_SetState(proj, throw_state, 1.0f, 1.0f, 1);
    }
    return 1;
}

static int SpawnForAllHumans(ProjectileKind kind, float distance, const char *label)
{
    int spawned = 0;
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        if (SpawnProjectileForPlayer(i, kind, distance))
            spawned++;
    }

    OSReport("[SpawnProjectile] %s: spawned on %d player(s)\n", label, spawned);
    return spawned > 0;
}

int SpawnProjectile_BombTrap(void)
{
    return SpawnForAllHumans(PROJKIND_BOMB, 60.0f, "BombTrap");
}

int SpawnProjectile_GordoTrap(void)
{
    return SpawnForAllHumans(PROJKIND_GORDO, 60.0f, "GordoTrap");
}

int SpawnProjectile_SensorBombTrap(void)
{
    return SpawnForAllHumans(PROJKIND_SENSORBOMB, 60.0f, "SensorBombTrap");
}
