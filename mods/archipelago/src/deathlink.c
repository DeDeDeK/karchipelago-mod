#include "game.h"
#include "machine.h"
#include "topride.h"
#include "inline.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "settings_menu.h"
#include "deathlink.h"
#include "textbox_api.h"

// Reentrancy guard — prevents send hooks from echoing when we call
// Machine_SetFallDead or Ply_SetHP from the receive path.
static int applying_deathlink = 0;

// Common send gate. Returns 1 if the caller should proceed to set
// deathlink_send. Both 3D and TR send paths use this; the human-vs-CPU check
// is mode-specific and stays at the call site (3D uses Ply_CheckIfCPU,
// TR uses TopRide_GetPlayerKind).
static int DeathLinkSendAllowed(void)
{
    if (applying_deathlink)
        return 0;
    if (!ap_menu_settings.deathlink_enabled)
        return 0;
    return 1;
}

// 3D-mode helper — gates on Ply_CheckIfCPU then sends.
static void SendDeathLink(int ply)
{
    if (!DeathLinkSendAllowed())
        return;
    if (Ply_CheckIfCPU(ply))
        return;

    OSReport("[DeathLink] Death detected for human player [%d]. Sending deathlink...\n", ply);
    ap_data->deathlink_send = 1;
}

// Hook inside Rider_CheckToDieOnMachine (0x801a06a8) at 0x801a06d0 — fires when Machine_IsDead returns
// true (HP death). Does NOT fire for fall deaths (different bit in md->x0C35).
static void DeathLink_OnHpDeath(RiderData *rd)
{
    SendDeathLink(rd->ply);
}
CODEPATCH_HOOKCREATE(0x801a06d0, "mr 3, 31\n\t", DeathLink_OnHpDeath, "", 0)

// Hook inside Machine_SetFallDead (0x801e6540) — fires when a machine falls out of
// bounds. At this point r31 = MachineData* and rider_gobj is known non-null.
// Clobbered instruction: stw r4, 0x1b48(r31)
static void DeathLink_OnFallDeath(MachineData *md)
{
    int ply = Machine_GetRiderPly(md);
    OSReport("[DeathLink] Fall death detected for player [%d]\n", ply);
    SendDeathLink(ply);
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

// Kill a player via HP death (City Trial + Destruction Derby + VS King
// Dedede) or fall death (Air Ride / Top Ride / other stadiums). The HP-death
// stadiums run outside Gm_IsInCity but use CT-style HP-based death; fall
// death there either no-ops or misbehaves since there's no out-of-bounds
// spline to respawn to.
static void KillPlayer(RiderData *rd, MachineData *md)
{
    OSReport("[DeathLink] Deathlink received! Killing player %d\n", rd->ply);

    StadiumKind stadium = Gm_GetCurrentStadiumKind();
    int hp_death = Gm_IsInCity()
                || Gm_IsDestructionDerby()
                || stadium == STKIND_VSKINGDEDEDE
                || stadium == STKIND_MELEE1
                || stadium == STKIND_MELEE2;
    if (hp_death)
    {
        // Zero HP to trigger normal death flow
        DmgLog dl = md->dmg_log;
        dl.attacker_ply = 0;
        Ply_AddDeath(rd->ply, &dl, md->is_bike, md->kind);
        Ply_SetHP(rd->ply, 0);
    }
    else
    {
        // Air Ride / Top Ride: trigger fall-off-course death.
        // respawn_pos contains spline params {segment, progress, y_offset}
        // updated per-frame by the checkpoint system. Use backup_respawn_pos
        // when xc37 bit 6 is set (spline lookup failed), matching the vanilla
        // Machine_CheckFallDeath OOB-distance path. Pass -1 for ground_handle
        // (vanilla does this when no dead zone surface is found).
        float *pos = (md->xc37 & 0x40) ? md->backup_respawn_pos : md->respawn_pos;
        Machine_SetFallDead(md, -1, pos);
    }
}

// Check for deathlink receive and kill all human players
static void DeathLink_PerFrame(GOBJ *g)
{
    if (Gm_GetIntroState() != GMINTRO_END)
        return;

    if (ap_data->deathlink_receive != 1)
        return;

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

        applying_deathlink = 1;
        KillPlayer(rd, md);
        applying_deathlink = 0;
    }

    tb_api->EnqueueColoredNoun(NULL, "Deathlink", tb_api->DeathColor, " received!");
    ap_data->deathlink_receive = 0;
}

void DeathLink_On3DLoadEnd()
{
    OSReport("[DeathLink] Active\n");
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, DeathLink_PerFrame, 0, 0, 0, 0);
}

// Top Ride send hook for the sand-pit enemy on the SAND course. The pit
// has an enemy at its center that swallows kirby and spits them back out;
// the spit-out animation goes through `KirbyDoodlebugOut` (vtable wrapper
// at +0xD0 — the same one used when a Doodlebug item ejects its rider,
// hence the name). Two call sites for vt[+0xD0] exist: 0x802e2804 (the
// Doodlebug item) and 0x80331a94 (this enemy, in a per-frame function
// that loops over all 4 kirby slots). Hooking 0x80331a94 catches only
// the sand-pit interaction, not generic Doodlebug-item ejection.
//
// At the hook site:
//   r31 = kirby           (set at 0x80331a7c via mr r3, r31 above)
//   r4  = stack+0x90      (kirby_pos arg)
//   r5  = stack+0x84      (src_pos arg — center of the pit)
//   r6  = 30, r7 = 60     (immediate u16 args)
//   r12 = kirby->vtable   (loaded at 0x80331a84)
// Clobbered: `lwz r12, 0xd0(r12)`. r1 untouched by our prologue (no stack
// frame allocated), so the stack-relative arg pointers stay valid.
static void DeathLink_OnTopRideSandPit(TopRideKirby *kirby)
{
    if (!DeathLinkSendAllowed())
        return;
    if (TopRide_GetPlayerKind(kirby->player_slot) != TR_PKIND_HMN)
        return;
    TopRideKirbyMgr *mgr = *stc_topride_kirbymgr;
    if (!mgr || mgr->round_state != 2)
        return;

    OSReport("[DeathLink] TR sand pit (slot %d). Sending deathlink...\n",
             kirby->player_slot);
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

// Top Ride deathlink receive. TR has no rider/machine/HP/fall-death system,
// so the AR/CT receive path can't apply. Pick a damage-class state wrapper
// from the pool and apply it to each human kirby. Same state for every human
// keeps the visual coherent. SpeedDown is reserved for traplink. Burn, Spin,
// Crush, Strike, Explode, Elec excluded — see docs/topride-kirby-states.md
// "Caveats & Open Items" for per-state reasoning.
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

        // Zero ChargeComponent.velocity (kirby+0xA0) before AND after apply()
        // to neutralize the AC_TOBASARE per-frame rescale callback. Pre-zero
        // pre-empts setters that read kirby+0xA0 and scale it (Strike/Explode);
        // post-zero overrides Crush, whose setter ignores kirby+0xA0 and
        // PSVECNormalizes its Vec3 arg (we pass &zero) into NaN which it
        // writes to kirby+0xA0. See docs/topride-kirby-states.md.
        Vec3 *vel = (Vec3 *)((char *)kirby + 0xA0);
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

void DeathLink_OnTopRideLoad()
{
    OSReport("[DeathLink] Active (Top Ride)\n");
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, DeathLink_TopRidePerFrame, 0, 0, 0, 0);
}

// Apply patches needed for deathlink
void DeathLink_OnBoot()
{
    // HP death: hook in Rider_CheckToDieOnMachine
    CODEPATCH_HOOKAPPLY(0x801a06d0);
    // Fall death: hook in Machine_SetFallDead
    CODEPATCH_HOOKAPPLY(0x801e6540);
    // TR scenery: sand-pit enemy on the SAND course (DoodlebugOut wrapper)
    CODEPATCH_HOOKAPPLY(0x80331a94);
    OSReport("[DeathLink] Hooks installed\n");
}
