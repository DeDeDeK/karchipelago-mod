#include <string.h>

#include "os.h"
#include "game.h"
#include "rider.h"
#include "projectile.h"

#include "spawn_projectile.h"

// Spawn one vanilla projectile of `kind` in front of `ply_idx`'s machine.
// Uses the machine's world pos/forward/up rather than the rider's hand
// bone, so there is no dependency on any copy ability being active — this
// is the whole point of bypassing spawnBomb and friends.
//
// Position is offset forward by `distance` so the projectile visually spawns
// in front of the machine rather than clipping through it. The projectile's
// initial velocity inherits md->velocity so it carries the machine's motion.
static int SpawnProjectileForPlayer(int ply_idx, ProjectileKind kind, float distance)
{
    GOBJ *mg = Ply_GetMachineGObj(ply_idx);
    if (!mg)
        return 0;
    MachineData *md = mg->userdata;
    if (!md)
        return 0;

    // Pull the rider's owner id if available — vanilla writes rider->x0 into
    // desc.owner_unk1 / unk2. Falling back to 0 is probably fine (the field
    // is used for hit attribution and team checks) but grabbing the real
    // value mirrors vanilla exactly.
    int owner = 0;
    GOBJ *rg = Ply_GetRiderGObj(ply_idx);
    if (rg && rg->userdata)
        owner = *(int *)rg->userdata;  // rd->x0

    ProjectileDesc desc;
    memset(&desc, 0, sizeof(desc));
    desc.kind = kind;
    desc.owner_unk1 = owner;
    desc.owner_unk2 = owner;
    desc.owner_byte = 0;

    // Spawn at md->pos + md->forward * distance.
    desc.position.X = md->pos.X + md->forward.X * distance;
    desc.position.Y = md->pos.Y + md->forward.Y * distance;
    desc.position.Z = md->pos.Z + md->forward.Z * distance;

    desc.forward = md->forward;
    desc.up = md->up;

    desc.velocity_scale = 1.0f;   // FLOAT_805e1348 in vanilla

    // Inherit machine velocity so the projectile isn't suddenly stationary
    // in world space. Using md->velocity (0x324) rather than rider->self_vel
    // because we're placing at the machine, not the rider.
    desc.velocity = md->velocity;

    desc.type_flag = 1;
    desc.charge = 1.0f;           // vanilla uses md->projectile_charge_scale

    Projectile_Create(&desc);
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

