#include <string.h>

#include "os.h"
#include "game.h"
#include "projectile.h"

#include "spawn_projectile.h"

// Forward throw impulse on top of the machine's world velocity. Without
// this, the projectile inherits only md->velocity and rides along with
// Kirby — looking "attached" — until gravity drops the bomb onto stage
// geometry and detonates it. A constant outward speed keeps the trap's
// trajectory predictable regardless of how fast Kirby is moving.
#define THROW_SPEED 30.0f

// Spawn one vanilla projectile of `kind` in front of `ply_idx`'s machine.
// Uses the machine's world pos/forward/up rather than the rider's hand
// bone, so there is no dependency on any copy ability being active — this
// is the whole point of bypassing spawnBomb and friends.
//
// Position is offset forward by `distance` so the projectile visually spawns
// in front of the machine rather than clipping through it. Velocity is
// md->velocity + md->forward * THROW_SPEED so the projectile carries the
// machine's motion AND has its own outbound impulse.
//
// Bomb / sensor-bomb / gordo come out of Projectile_Create in state 0
// (HELD) — the post-init callback puts them in the rider's hand and the
// detonation logic never runs until a separate throw transition. Bomb and
// sensor bomb just need a Projectile_SetState(1) to start their flying
// physics. Gordo additionally needs all the per-kind scratch that vanilla
// 0x8022a544 sets up (rotation cache, accel from kind_data, lifetime), so
// for that case we call Gordo_EnterThrownState directly instead of doing
// our own SetState — a bare SetState(1) leaves the gordo invisible, with
// no rotation, no real impulse, and a zero lifetime.
static int SpawnProjectileForPlayer(int ply_idx, ProjectileKind kind, float distance)
{
    GOBJ *mg = Ply_GetMachineGObj(ply_idx);
    if (!mg)
        return 0;
    MachineData *md = mg->userdata;
    if (!md)
        return 0;

    // Pull the rider's owner id if available — vanilla writes rider->x0 into
    // desc.owner_unk1 / unk2, and Projectile_Create copies that into
    // proj->owner_gobj at proj+0x08. Gordo_EnterThrownState reads bone
    // basis vectors via owner_gobj (rd+0x324, rd+0x330), so this needs to
    // be a real rider GObj for the gordo path.
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
    desc.owner_unk1 = owner;
    desc.owner_unk2 = owner;
    desc.owner_byte = 0;
    desc.position = throw_pos;
    desc.forward = md->forward;
    desc.up = md->up;
    desc.velocity_scale = 1.0f;
    desc.velocity = throw_vel;
    desc.type_flag = 1;
    desc.charge = 1.0f;

    void *handle = Projectile_Create(&desc);
    if (!handle)
        return 0;

    ProjectileData *proj = Projectile_GetData(handle);
    if (!proj)
        return 0;

    // The trapped player IS the owner, so vanilla owner-exclusion would
    // drop the explosion / hit on Kirby unless we explicitly opt in to
    // self-hit on both scan paths. Sensor bomb's post_init already sets
    // the inbound bit but never the outbound one; bomb and gordo set
    // neither. Setting both unconditionally keeps the trap reliable when
    // the trapped player is the only target near the detonation.
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
        // Projectile_Create snapshotted desc.velocity at proj+0x88
        // (spawn_velocity, read-only) but per-frame physics reads
        // proj+0x94 — vanilla throw writes that explicitly before
        // Projectile_SetState. flags=1 matches vanilla throw.
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
        {
            OSReport("[SpawnProjectile] %s: spawned on ply %d\n", label, i);
            spawned++;
        }
    }
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
