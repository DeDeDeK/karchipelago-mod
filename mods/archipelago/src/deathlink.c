#include "game.h"
#include "machine.h"
#include "topride.h"
#include "inline.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "settings_menu.h"
#include "deathlink.h"
#include "textbox_api.h"

#define DEATHLINK_PLY_MAX 5

// Stops the send hooks echoing the receive path's own kills back out. This is a
// countdown rather than a guard around the kill call because the HP-death path
// is asynchronous: Ply_SetHP only zeroes HP, the machine is not flagged dead
// until a later machine-think frame sets is_dead, and the send hook inside
// Rider_CheckToDieOnMachine only trips after that. 60 frames is far short of the
// 150-frame respawn timer, so it cannot swallow a genuine death.
#define DEATHLINK_SUPPRESS_FRAMES 60

static u8 deathlink_suppress[DEATHLINK_PLY_MAX];

static void SuppressSend(int ply)
{
    if ((u32)ply < DEATHLINK_PLY_MAX)
        deathlink_suppress[ply] = DEATHLINK_SUPPRESS_FRAMES;
}

static void TickSuppress(void)
{
    for (int i = 0; i < DEATHLINK_PLY_MAX; i++)
    {
        if (deathlink_suppress[i])
            deathlink_suppress[i]--;
    }
}

static void ClearSuppress(void)
{
    for (int i = 0; i < DEATHLINK_PLY_MAX; i++)
        deathlink_suppress[i] = 0;
}

// The human-vs-CPU check is mode-specific and stays at the call site
// (3D: Ply_CheckIfCPU, TR: TopRide_GetPlayerKind).
static int DeathLinkSendAllowed(int ply)
{
    if (!ap_menu_settings.deathlink_enabled)
        return 0;
    if ((u32)ply < DEATHLINK_PLY_MAX && deathlink_suppress[ply])
    {
        deathlink_suppress[ply] = 0;
        return 0;
    }
    return 1;
}

static void SendDeathLink(int ply, const char *cause)
{
    if (!DeathLinkSendAllowed(ply))
        return;
    if (Ply_CheckIfCPU(ply))
        return;

    OSReport("[DeathLink] Player %d died (%s) - sending\n", ply + 1, cause);
    ap_data->deathlink_send = 1;
}

// Hook inside Rider_CheckToDieOnMachine (0x801a06a8) at 0x801a06d0, where
// Machine_IsDead returns true. Fall deaths use a different bit in md->x0C35 and
// do not reach here.
static void DeathLink_OnHpDeath(RiderData *rd)
{
    SendDeathLink(rd->ply, "HP");
}
CODEPATCH_HOOKCREATE(0x801a06d0, "mr 3, 31\n\t", DeathLink_OnHpDeath, "", 0)

// Hook inside Machine_SetFallDead (0x801e6540), where a machine falls out of
// bounds. r31 = MachineData*, rider_gobj known non-null.
// Clobbered: stw r4, 0x1b48(r31)
static void DeathLink_OnFallDeath(MachineData *md)
{
    SendDeathLink(Machine_GetRiderPly(md), "fall");
}
CODEPATCH_HOOKCREATE(0x801e6540,
    "stwu 1, -16(1)\n\t"
    "stw 4, 0x8(1)\n\t"
    "stw 5, 0xc(1)\n\t"
    "mr 3, 31\n\t",
    DeathLink_OnFallDeath,
    "mr 3, 31\n\t"
    "lwz 4, 0x8(1)\n\t"
    "lwz 5, 0xc(1)\n\t"
    "addi 1, 1, 16\n\t",
    0)

// Kill a player via HP death (City Trial / Destruction Derby / VS King Dedede /
// Melee) or fall death (Air Ride / Top Ride). The HP-death stadiums use CT-style
// HP death; fall death there misbehaves (no out-of-bounds respawn spline).
static void KillPlayer(RiderData *rd, MachineData *md)
{
    StadiumKind stadium = Gm_GetCurrentStadiumKind();
    int hp_death = Gm_IsInCity()
                || Gm_IsDestructionDerby()
                || stadium == STKIND_VSKINGDEDEDE
                || stadium == STKIND_MELEE1
                || stadium == STKIND_MELEE2;
    if (hp_death)
    {
        DmgLog dl = md->dmg_log;
        dl.attacker_ply = 0;
        Ply_AddDeath(rd->ply, &dl, md->is_bike, md->kind);
        Ply_SetHP(rd->ply, 0);
    }
    else
    {
        // respawn_pos holds the checkpoint spline params; backup_respawn_pos
        // covers a failed lookup. -1 ground_handle matches vanilla's
        // no-dead-zone-surface case.
        float *pos = md->use_backup_checkpoint ? md->backup_respawn_pos : md->respawn_pos;
        Machine_SetFallDead(md, -1, pos);
    }
}

static void DeathLink_PerFrame(GOBJ *g)
{
    if (Gm_GetIntroState() != GMINTRO_END)
        return;

    TickSuppress();

    if (ap_data->deathlink_receive != 1)
        return;

    int killed = 0;
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;

        GOBJ *rg = Ply_GetRiderGObj(i);
        if (!rg)
            continue;
        RiderData *rd = rg->userdata;

        if (!Rider_IsOnMachine(rd))
            continue;

        GOBJ *mg = Ply_GetMachineGObj(i);
        if (!mg)
            continue;
        MachineData *md = mg->userdata;

        SuppressSend(i);
        KillPlayer(rd, md);
        killed++;
    }

    OSReport("[DeathLink] Received - killed %d human(s)\n", killed);
    tb_api->EnqueueColoredNoun(NULL, "Deathlink", tb_api->DeathColor, " received!");
    ap_data->deathlink_receive = 0;
}

void DeathLink_On3DLoadEnd()
{
    ClearSuppress();
    OSReport("[DeathLink] Active\n");
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, DeathLink_PerFrame, 0, 0, 0, 0);
}

// Top Ride send hook for the SAND-course sand-pit enemy, which swallows a kirby
// and spits it out via the KirbyDoodlebugOut wrapper (vt+0xD0). This call site
// catches only the sand-pit eject, not Doodlebug-item ejection (same wrapper at
// 0x802e2804). r31 = kirby.
static void DeathLink_OnTopRideSandPit(TopRideKirby *kirby)
{
    if (!DeathLinkSendAllowed(kirby->player_slot))
        return;
    if (TopRide_GetPlayerKind(kirby->player_slot) != TR_PKIND_HMN)
        return;
    TopRideKirbyMgr *mgr = *stc_topride_kirbymgr;
    if (!mgr || mgr->round_state != 2)
        return;

    OSReport("[DeathLink] Player %d died (TR sand pit) - sending\n",
             kirby->player_slot + 1);
    ap_data->deathlink_send = 1;
}
CODEPATCH_HOOKCREATE(0x80331a94,
    "mr 3, 31\n\t",
    DeathLink_OnTopRideSandPit,
    "mr 3, 31\n\t"
    "addi 4, 1, 0x90\n\t"
    "addi 5, 1, 0x84\n\t"
    "li 6, 30\n\t"
    "li 7, 60\n\t"
    "lwz 12, 0(31)\n\t",
    0)

// TR has no HP/fall-death system, so the receive picks one damage-class state and
// applies it to every human kirby. SpeedDown is reserved for traplink;
// Burn/Spin/Crush/Strike/Explode/Elec are excluded.
typedef void (*KirbyStateFn)(TopRideKirby *);
static const KirbyStateFn deathlink_states[] = {
    TopRide_KirbyPress,
    TopRide_KirbyFreeze,
    TopRide_KirbyNumb,
    TopRide_KirbyConfuse,
};
static const char *const deathlink_state_names[] = {
    "Press",
    "Freeze",
    "Numb",
    "Confuse",
};
#define DEATHLINK_STATE_COUNT (sizeof(deathlink_states) / sizeof(deathlink_states[0]))

static void DeathLink_TopRidePerFrame(GOBJ *g)
{
    TickSuppress();

    if (ap_data->deathlink_receive != 1)
        return;

    TopRideKirbyMgr *mgr = *stc_topride_kirbymgr;
    if (!mgr || mgr->round_state != 2)
        return;

    int idx = HSD_Randi(DEATHLINK_STATE_COUNT);
    KirbyStateFn apply = deathlink_states[idx];
    int hits = 0;

    for (int i = 0; i < 4; i++)
    {
        TopRideKirby *kirby = mgr->kirbys[i];
        if (!kirby)
            continue;
        if (TopRide_GetPlayerKind(kirby->player_slot) != TR_PKIND_HMN)
            continue;

        SuppressSend(kirby->player_slot);

        // Zero charge.velocity before AND after apply() so the state produces a
        // static stun with no knockback: pre-zero pre-empts setters that scale
        // it, post-zero overrides setters that overwrite it (e.g. a NaN from
        // normalizing a zero vector).
        Vec3 *vel = &kirby->charge.velocity;
        vel->X = vel->Y = vel->Z = 0.0f;
        apply(kirby);
        vel->X = vel->Y = vel->Z = 0.0f;
        hits++;
    }

    OSReport("[DeathLink] Received (TR) - applied %s to %d humans\n",
             deathlink_state_names[idx], hits);
    tb_api->EnqueueColoredNoun(NULL, "Deathlink", tb_api->DeathColor, " received!");
    ap_data->deathlink_receive = 0;
}

void DeathLink_OnTopRideLoadEnd()
{
    ClearSuppress();
    OSReport("[DeathLink] Active (Top Ride)\n");
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, DeathLink_TopRidePerFrame, 0, 0, 0, 0);
}

void DeathLink_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x801a06d0); // HP death
    CODEPATCH_HOOKAPPLY(0x801e6540); // Fall death
    CODEPATCH_HOOKAPPLY(0x80331a94); // TR sand pit
    OSReport("[DeathLink] Hooks installed\n");
}
