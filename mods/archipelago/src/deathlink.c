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

// Find a valid dead-zone ground handle for Machine_SetFallDead.
// Ground entries at *(GrObj+0x74), 0x140 bytes each, count at GrObj+0x78.
// Type at entry+0x24: (value & 0x1FFFFFF) == 0x19 means dead zone.
// Returns the first dead-zone handle found, or -1 if none exist.
static int FindDeadZoneGroundHandle(void)
{
    char *grobj = (char *)*stc_grobj;
    if (!grobj)
        return -1;

    char *entries_base = *(char **)(grobj + 0x74);
    int max_handles = *(int *)(grobj + 0x78);
    if (!entries_base || max_handles <= 0)
        return -1;

    for (int i = 0; i < max_handles; i++)
    {
        int type = *(int *)(entries_base + i * 0x140 + 0x24) & 0x1FFFFFF;
        if (type == 0x19)
            return i;
    }

    return -1;
}

// Shared helper — sends deathlink for a human player.
static void SendDeathLink(int ply)
{
    if (applying_deathlink)
        return;
    if (!hoshi_menu_settings.deathlink_enabled)
        return;
    if (Ply_CheckIfCPU(ply))
        return;

    OSReport("Death detected for human player [%d]. Sending deathlink...\n", ply);
    archipelago_data->deathlink_send = 1;
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
    OSReport("Fall death detected for player [%d]\n", ply);
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
    OSReport("Deathlink received! Killing player %d\n", rd->ply);

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
        // Try surface-specific handle first, then any dead zone on the stage.
        // Fallback to -1 is valid — the game does this in Machine_CheckFallDeath's
        // OOB distance branch (global dead zone system handles respawn).
        int ground_handle = Machine_GetGroundHandle(md->surface_id);
        if (ground_handle < 0)
            ground_handle = FindDeadZoneGroundHandle();
        Machine_SetFallDead(md, ground_handle, md->respawn_pos);
    }
}

// Check for deathlink receive and kill all human players
void DeathLink_PerFrame(GOBJ *g)
{
    if (Gm_GetIntroState() != GMINTRO_END)
        return;

    if (archipelago_data->deathlink_receive != 1)
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
    archipelago_data->deathlink_receive = 0;
}

void DeathLink_On3DLoadEnd()
{
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, DeathLink_PerFrame, 0, 0, 0, 0);
}

// Apply patches needed for deathlink
void DeathLink_OnBoot()
{
    OSReport("Applying deathlink patches...\n");
    // HP death: hook in Rider_CheckToDieOnMachine
    CODEPATCH_HOOKAPPLY(0x801a06d0);
    // Fall death: hook in Machine_SetFallDead
    CODEPATCH_HOOKAPPLY(0x801e6540);
}
