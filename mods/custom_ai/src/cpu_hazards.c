#include "game.h"
#include "hsd.h"
#include "rider.h"
#include "machine.h"
#include "yakumono.h"
#include "stage.h"
#include "hurt.h"
#include "trigger.h"
#include "os.h"
#include "inline.h"
#include "code_patch/code_patch.h"

#include "cpu_fixes.h"
#include "cpu_hazards.h"

#define HAZARD_SPHERE_MAX 32
// Rider_CPUCollectHazards builds its scan basis from the CPU's velocity and gives up when
// that is too short to have a direction; a stopped CPU scans along its facing instead.
#define HAZARD_STOPPED_SPEED 0.01f

// This tick's spheres in world units, then in the stage units the static pass reads.
static CpuSphere stc_spheres[HAZARD_SPHERE_MAX];
static CpuSphere stc_sphere_stage[HAZARD_SPHERE_MAX];
static int stc_sphere_num;
static u32 stc_sphere_frame;

static CpuThreat stc_threat[PLY_NUM];

// Yakumono attack regions that can hurt a rider or its machine. Vanilla never lists them.
static void CpuHazards_AddYakumono(void)
{
    int victims = HITFLAG_VICTIM(HURTKIND_MACHINE) | HITFLAG_VICTIM(HURTKIND_RIDER);
    HitRegion *r;
    HurtData *hd;
    GOBJ *g;
    int i;

    for (g = (*stc_gobj_lookup)[GAMEPLINK_YAKUMONO]; g && stc_sphere_num < HAZARD_SPHERE_MAX; g = g->next)
    {
        hd = YakumonoGObj_GetHurtData(g);
        if (!hd)
            continue;
        for (i = 0; i < hd->region_count && stc_sphere_num < HAZARD_SPHERE_MAX; i++)
        {
            r = &hd->regions[i];
            if (r->state != TRIGGERSTATUS_ACTIVE || r->params.base_damage <= 0 || !(r->params.hit_flags & victims))
                continue;
            stc_spheres[stc_sphere_num++] = (CpuSphere){.pos = r->pos, .radius = r->radius};
        }
    }
}

// The spheres the CPUs last steered around, for the debug view.
int CpuHazards_GetSpheres(const CpuSphere **out)
{
    *out = stc_spheres;
    return stc_sphere_num;
}

// Rider_CPUCollectStaticHazards' (0x802695ec) sphere source, the call at 0x8026964c to a
// stub that is always empty in retail: {x, y, z, radius} records in stage units, built once
// per tick.
static int CpuHazards_GetStaticSpheres(CpuSphere **out)
{
    float inv_scale;
    int i;

    *out = stc_sphere_stage;
    if (stc_sphere_frame == stc_hsd_update->engine_frames)
        return stc_sphere_num;
    stc_sphere_frame = stc_hsd_update->engine_frames;
    stc_sphere_num = 0;
    if (cpu_fixes)
        CpuHazards_AddYakumono();

    inv_scale = 1.0f / grGetStageScale();
    for (i = 0; i < stc_sphere_num; i++)
    {
        VECScale(&stc_spheres[i].pos, &stc_sphere_stage[i].pos, inv_scale);
        stc_sphere_stage[i].radius = stc_spheres[i].radius * inv_scale;
    }
    return stc_sphere_num;
}

static float CpuHazards_Key(CpuHazard *h)
{
    return h->imminent ? h->time_to_impact : 1000.0f + VECMag(&h->rel_pos);
}

// Soonest imminent hazard first, then the rest nearest first, so
// Rider_CPUResolveAvoidVector's first eligible entry is the one that matters most.
static void CpuHazards_Sort(CpuHazardList *list)
{
    CpuHazard tmp;
    int i, j;

    for (i = 1; i < list->num; i++)
    {
        tmp = list->entries[i];
        for (j = i; j > 0 && CpuHazards_Key(&list->entries[j - 1]) > CpuHazards_Key(&tmp); j--)
            list->entries[j] = list->entries[j - 1];
        list->entries[j] = tmp;
    }
}

// Attack and Patrol ram their rival, which the rider pass lists as a hazard to steer around.
static void CpuHazards_DropRival(RiderData *rd, CpuHazardList *list)
{
    int rival = rd->cpu->rival_ply;
    GOBJ *g = rival < PLY_NUM ? Ply_GetRiderGObj(rival) : NULL;
    RiderData *rival_rd;
    MachineData *rival_md;
    int i, n = 0;

    if (!g)
        return;
    rival_rd = g->userdata;
    rival_md = rival_rd->machine_gobj ? rival_rd->machine_gobj->userdata : NULL;

    for (i = 0; i < list->num; i++)
    {
        if (list->entries[i].rider == rival_rd || (rival_md && list->entries[i].machine == rival_md))
            continue;
        if (n != i)
            list->entries[n] = list->entries[i];
        n++;
    }
    list->num = n;
}

static void CpuHazards_RecordThreat(RiderData *rd, CpuHazardList *list)
{
    CpuThreat *t = &stc_threat[rd->ply];
    CpuHazard *h;
    MachineData *md;
    int i;

    t->kind = CPU_THREAT_NONE;
    t->ply = PLY_NUM;
    t->frame = stc_hsd_update->engine_frames;

    for (i = 0; i < list->num; i++)
    {
        h = &list->entries[i];
        if (!h->imminent)
            continue;
        if (t->kind != CPU_THREAT_NONE && h->time_to_impact >= t->time)
            continue;
        t->time = h->time_to_impact;
        if (h->rider)
        {
            t->kind = CPU_THREAT_RIDER;
            t->ply = h->rider->ply;
        }
        else if (h->machine)
        {
            md = h->machine;
            t->kind = CPU_THREAT_MACHINE;
            t->ply = md->rider_gobj ? ((RiderData *)md->rider_gobj->userdata)->ply : PLY_NUM;
        }
        else if (h->enemy)
        {
            t->kind = CPU_THREAT_ENEMY;
            t->ply = PLY_NUM;
        }
        else if (h->weapon)
        {
            t->kind = CPU_THREAT_WEAPON;
            t->ply = PLY_NUM;
        }
        else
        {
            t->kind = CPU_THREAT_STATIC;
            t->ply = PLY_NUM;
        }
    }
}

const CpuThreat *CpuHazards_GetThreat(int ply)
{
    CpuThreat *t = &stc_threat[ply];

    if (t->frame != stc_hsd_update->engine_frames)
        t->kind = CPU_THREAT_NONE;
    return t;
}

// Every decide handler's Rider_CPUCollectHazards call.
static void CpuHazards_Collect(RiderData *rd)
{
    CpuHazardList *list = stc_cpu_hazards;
    int state = rd->cpu->ai_state;

    Rider_CPUCollectHazards(rd);
    if (cpu_fixes)
    {
        if (state == CPUSTATE_ATTACK || state == CPUSTATE_PATROL)
            CpuHazards_DropRival(rd, list);
        CpuHazards_Sort(list);
    }
    CpuHazards_RecordThreat(rd, list);
}

// Rider_CPUCollectHazards (0x80269928) runs its passes in a chain that stops once the
// 8-entry list is full: riders, machines, static spheres, enemies, then weapons. Riders and
// their machines alone can fill it, so with the fixes on weapons and enemies go first. A pass
// returns 0 once the list is full; the caller's r7 is read by no pass.
static int CpuHazards_Pass1(RiderData *rd, CpuHazardList *list, Vec3 *pos, Vec3 *vel, float radius)
{
    if (!cpu_fixes)
        return Rider_CPUCollectRiderHazards(rd, list, pos, vel, radius);
    if (!(rd->cpu->behavior_flags & CPUBEHAVE_EXTRA_HAZARDS))
        return 1;
    return Rider_CPUCollectWeaponHazards(rd, list, pos, vel, radius);
}

static int CpuHazards_Pass2(RiderData *rd, CpuHazardList *list, Vec3 *pos, Vec3 *vel, float radius)
{
    if (!cpu_fixes)
        return Rider_CPUCollectMachineHazards(rd, list, pos, vel, radius);
    return Rider_CPUCollectEnemyHazards(rd, list, pos, vel, radius);
}

static int CpuHazards_Pass4(RiderData *rd, CpuHazardList *list, Vec3 *pos, Vec3 *vel, float radius)
{
    if (!cpu_fixes)
        return Rider_CPUCollectEnemyHazards(rd, list, pos, vel, radius);
    return Rider_CPUCollectRiderHazards(rd, list, pos, vel, radius);
}

// Behind the weapon gate, which the fixes skip.
static int CpuHazards_Pass5(RiderData *rd, CpuHazardList *list, Vec3 *pos, Vec3 *vel, float radius)
{
    if (!cpu_fixes)
        return Rider_CPUCollectWeaponHazards(rd, list, pos, vel, radius);
    return Rider_CPUCollectMachineHazards(rd, list, pos, vel, radius);
}

static void CpuHazards_FixStoppedVel(RiderData *rd, Vec3 *vel)
{
    if (!cpu_fixes || VECMag(vel) >= HAZARD_STOPPED_SPEED)
        return;
    VECScale(&rd->forward, vel, HAZARD_STOPPED_SPEED);
}

// Rider_CPUCollectHazards (0x80269928) `lfs f31,...` once the CPU's velocity is at sp+0x38:
// r29 is the RiderData and nothing volatile is live.
CODEPATCH_HOOKCREATE(0x80269998,
    "mr 3, 29\n\t"
    "addi 4, 1, 56\n\t",
    CpuHazards_FixStoppedVel,
    "",
    0)

static const int stc_collect_calls[] = {
    0x80271af0, // Rider_CPUDecideCruise (0x80271790)
    0x80271e78, // Rider_CPUDecideCruisePlus (0x80271b24)
    0x802724d0, // Rider_CPUDecideNavigate (0x80271eb4)
    0x8027285c, // Rider_CPUDecideRouteFollow (0x802726fc)
    0x80272bac, // Rider_CPUDecideRouteFollowCity (0x80272888)
    0x80273178, // Rider_CPUDecideCharge (0x80272dd0)
    0x802735a0, // Rider_CPUDecideReposition (0x80273228)
    0x80273878, // Rider_CPUDecideAttack (0x802735dc)
    0x80273cf0, // Rider_CPUDecidePatrol (0x80273b48)
};

void CpuHazards_InstallHooks(void)
{
    int i;

    for (i = 0; i < GetElementsIn(stc_collect_calls); i++)
        CODEPATCH_REPLACECALL(stc_collect_calls[i], CpuHazards_Collect);
    CODEPATCH_REPLACECALL(0x80269b48, CpuHazards_Pass1);
    CODEPATCH_REPLACECALL(0x80269b6c, CpuHazards_Pass2);
    CODEPATCH_REPLACECALL(0x80269bb4, CpuHazards_Pass4);
    CODEPATCH_REPLACECALL(0x80269be4, CpuHazards_Pass5);
    CODEPATCH_REPLACECALL(0x8026964c, CpuHazards_GetStaticSpheres);
    CODEPATCH_HOOKAPPLY(0x80269998);
}
