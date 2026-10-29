#ifndef CUSTOM_EVENTS_H
#define CUSTOM_EVENTS_H

#include <math.h>
#include <string.h>

#include "os.h"
#include "hsd.h"
#include "obj.h"
#include "game.h"
#include "enemy.h"
#include "inline.h"
#include "custom_events_api.h"

// HSD heap kept free for the rest of the round. An allocation the heap cannot serve
// crashes the game rather than failing the spawn, so every spawner stops short of it.
#define CUSTOM_EVENTS_HEAP_RESERVE (512 * 1024)

void CustomEvents_OnBoot(void);

// Readies a City Trial round with events on: the custom announcements and the round's
// event system. Call at every such load.
void CustomEvents_OnCityLoad(void);

// Runs the abort callback of a custom event the scene exit cut off.
void CustomEvents_On3DExit(void);

static inline int CustomEvents_HeapOk(void)
{
    return OSCheckHeap(HSD_GetHeapID()) >= CUSTOM_EVENTS_HEAP_RESERVE;
}

static inline int CustomEvents_IsOnPLink(GOBJ *gobj, int plink)
{
    for (GOBJ *g = (*stc_gobj_lookup)[plink]; g; g = g->next)
    {
        if (g == gobj)
            return 1;
    }
    return 0;
}

// Whether a tracked actor is still on the enemy list as that kind. Vanilla destroys
// actors without running the procs mods add to them, and hands a freed GObj to the
// next actor created.
static inline int CustomEvents_IsActorLive(GOBJ *gobj, EnemyKind kind)
{
    return CustomEvents_IsOnPLink(gobj, GAMEPLINK_ENEMY) && ((EnemyData *)gobj->userdata)->kind == kind;
}

// The nearest rider closer than range, CPUs included unless humans_only, or -1.
static inline int CustomEvents_NearestRider(Vec3 *pos, float range, int humans_only)
{
    int best = -1;
    for (int ply = 0; ply < PLY_NUM; ply++)
    {
        if (humans_only && Ply_GetPKind(ply) != PKIND_HMN)
            continue;

        float d = Enemy_DistToPlayer(ply, pos);
        if (d < range)
        {
            range = d;
            best = ply;
        }
    }
    return best;
}

// An event actor at pos facing +Z, with no spawn slot and no leash, so EventActor_Create
// attaches it to the nearest stage spline.
static inline void CustomEvents_InitActorDesc(EventActorDesc *desc, EnemyKind kind, Vec3 *pos, float scale)
{
    memset(desc, 0, sizeof(*desc));
    desc->kind = kind;
    desc->position = *pos;
    desc->forward.Z = 1.0f;
    desc->up.Y = 1.0f;
    desc->scale = scale;
    desc->spawn_index = -1;
    desc->spawn_slot = -1;
    desc->leash_radius = -1.0f;
}

// A random level heading, its yaw measured from +Z.
static inline Vec3 CustomEvents_RandomHeading(void)
{
    float yaw = RandomAngle();
    return (Vec3){sinf(yaw), 0.0f, cosf(yaw)};
}

#endif // CUSTOM_EVENTS_H
