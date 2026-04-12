#include "game.h"
#include "machine.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "deathlink.h"
#include "text.h"
#include "textbox.h"

// Reentrancy guard — prevents send hooks from echoing when we call
// Machine_SetFallDead or Ply_SetHP from the receive path.
static int applying_deathlink = 0;

// Shared helper — sends deathlink for a human player.
static void SendDeathLink(int ply)
{
    if (applying_deathlink)
        return;
    if (!ap_menu_settings.deathlink_enabled)
        return;
    if (Ply_CheckIfCPU(ply))
        return;

    OSReport("[DeathLink] Death detected for human player [%d]. Sending deathlink...\n", ply);
    ap_data->deathlink_send = 1;
}

// Hook on Rider_CheckToDieOnMachine (0x801a06d0) — fires when Machine_IsDead returns
// true (HP death). Does NOT fire for fall deaths (different bit in md->x0C35).
void Rider_OnDeath(RiderData *rd)
{
    SendDeathLink(rd->ply);
}
CODEPATCH_HOOKCREATE(0x801a06d0, "mr 3, 31\n\t", Rider_OnDeath, "", 0)

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

// Kill a player via HP death (City Trial) or fall death (Air Ride / Top Ride).
static void KillPlayer(RiderData *rd, MachineData *md)
{
    OSReport("[DeathLink] Deathlink received! Killing player %d\n", rd->ply);

    if (Gm_IsInCity())
    {
        // City Trial: zero HP to trigger normal death flow
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
void DeathLink_PerFrame(GOBJ *g)
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

    TextBox_Enqueue("Deathlink received!");
    ap_data->deathlink_receive = 0;
}

void DeathLink_On3DLoadEnd()
{
    OSReport("[DeathLink] Active\n");
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, DeathLink_PerFrame, 0, 0, 0, 0);
}

// Apply patches needed for deathlink
void DeathLink_OnBoot()
{
    OSReport("[DeathLink] Applying deathlink patches...\n");
    // HP death: hook in Rider_CheckToDieOnMachine
    CODEPATCH_HOOKAPPLY(0x801a06d0);
    // Fall death: hook in Machine_SetFallDead
    CODEPATCH_HOOKAPPLY(0x801e6540);
}
