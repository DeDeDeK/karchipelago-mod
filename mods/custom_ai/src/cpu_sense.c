#include "game.h"
#include "rider.h"
#include "machine.h"
#include "enemy.h"
#include "event.h"
#include "collision.h"
#include "scene.h"
#include "os.h"
#include "inline.h"

#include "cpu_ai.h"
#include "cpu_sense.h"

// A falling meteor's blast, by its scale; the event spawns them at scale 5-33.
#define SENSE_METEOR_BLAST_PER_SCALE 25.0f
#define SENSE_METEOR_LOOKAHEAD 600.0f
#define SENSE_VULNERABLE_HP 0.35f

static CpuRiderView stc_riders[PLY_NUM];
static CpuSelfView stc_self[PLY_NUM];
static CpuWorldView stc_world;

const CpuRiderView *CpuSense_Rider(int ply)
{
    return &stc_riders[ply];
}

const CpuSelfView *CpuSense_Self(int ply)
{
    return &stc_self[ply];
}

const CpuWorldView *CpuSense_World(void)
{
    return &stc_world;
}

// Hitting it now hurts the attacker or does nothing.
int CpuSense_IsThreatening(int ply)
{
    const CpuRiderView *v = &stc_riders[ply];

    return v->present && (v->spinning || v->boosting || v->invincible);
}

// A hit now lands, and lands hard.
int CpuSense_IsVulnerable(int ply)
{
    const CpuRiderView *v = &stc_riders[ply];

    return v->present && !v->invincible && (v->stunned || v->on_foot || v->hp_ratio < SENSE_VULNERABLE_HP);
}

static int CpuSense_IsInvincible(HurtData *hd)
{
    return hd && (hd->vuln.kind != 0 || hd->vuln.intang_timer > 0 || hd->vuln.invuln_timer > 0);
}

static void CpuSense_UpdateRider(int ply)
{
    CpuRiderView *v = &stc_riders[ply];
    GameData *gd = Gm_GetGameData();
    GOBJ *g = Ply_GetRiderGObj(ply);
    RiderData *rd;
    MachineData *md;
    float max_hp;

    memset(v, 0, sizeof(*v));
    if (!g)
        return;
    rd = g->userdata;
    md = rd->machine_gobj ? rd->machine_gobj->userdata : NULL;

    v->present = 1;
    v->human = Ply_GetPKind(ply) == PKIND_HMN;
    v->stunned = rd->status >= RDSTATE_DAMAGE && rd->status <= RDSTATE_GETOFF_FALLDEATHWAIT;
    v->invincible = rd->candy_duration > 0 || CpuSense_IsInvincible(rd->hurt_data);
    Ply_GetPosition(ply, &v->pos);
    max_hp = Ply_GetMaxHP(ply);
    v->hp_ratio = max_hp > 0 ? Ply_GetHP(ply) / max_hp : 1.0f;
    if (Scene_GetCurrentMajor() == MJRKIND_AIR)
        v->progress = gd->player_race_distance[ply];

    if (!md)
    {
        v->on_foot = 1;
        v->vel = rd->self_vel;
    }
    else
    {
        v->vel = md->world_velocity;
        v->spinning = MachineGObj_IsQuickSpinning(rd->machine_gobj);
        v->boosting = md->boost_hitbox_timer > 0 || md->dash_frames > 0;
        v->invincible |= CpuSense_IsInvincible(md->hurt_data);
    }
}

// Where each falling meteor will land, from its current velocity.
static void CpuSense_UpdateMeteors(void)
{
    CpuMeteorView *m;
    EnemyData *ed;
    Vec3 end, hit;
    float speed;
    GOBJ *g;

    stc_world.meteor_num = 0;
    for (g = (*stc_gobj_lookup)[GAMEPLINK_ENEMY]; g && stc_world.meteor_num < CPU_SENSE_METEOR_MAX; g = g->next)
    {
        ed = g->userdata;
        if (ed->kind != ENEMYKIND_METEOR || ed->state < METEOR_STATE_FALL || ed->state > METEOR_STATE_LANDING)
            continue;
        m = &stc_world.meteor[stc_world.meteor_num];
        m->radius = ed->final_scale * SENSE_METEOR_BLAST_PER_SCALE;
        speed = VECMag(&ed->vel);
        if (ed->state != METEOR_STATE_FALL || speed <= 0)
        {
            m->impact = ed->pos;
            m->frames = 0;
            stc_world.meteor_num++;
            continue;
        }
        VECScale(&ed->vel, &end, SENSE_METEOR_LOOKAHEAD / speed);
        VECAdd(&end, &ed->pos, &end);
        if (!Raycast_Ground(&ed->pos, &end, &hit))
            continue;
        m->impact = hit;
        m->frames = VECDistance(&ed->pos, &hit) / speed;
        stc_world.meteor_num++;
    }
}

static void CpuSense_UpdateWorld(void)
{
    grBoxGeneInfo *box = *stc_grBoxGeneInfo;

    if (Gm_IsInCity())
    {
        stc_world.round_progress = box ? box->match_progress : 0;
        stc_world.event_kind = CityEvent_GetActiveKind();
        CpuSense_UpdateMeteors();
    }
    else
    {
        stc_world.round_progress = 0;
        stc_world.event_kind = -1;
        stc_world.meteor_num = 0;
    }
}

void CpuSense_UpdateSelf(RiderData *rd)
{
    CpuSelfView *s = &stc_self[rd->ply];
    MachineData *md = rd->machine_gobj ? rd->machine_gobj->userdata : NULL;

    memset(s, 0, sizeof(*s));
    if (!md)
    {
        s->speed = VECMag(&rd->self_vel);
        return;
    }
    s->speed = VECMag(&md->world_velocity);
    s->charge = md->charge_value;
}

void CpuSense_OnTick(void)
{
    int i;

    for (i = 0; i < PLY_NUM; i++)
        CpuSense_UpdateRider(i);
    CpuSense_UpdateWorld();
}
