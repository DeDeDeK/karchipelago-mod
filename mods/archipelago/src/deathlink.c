#include "game.h"
#include "machine.h"
#include "topride.h"
#include "inline.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "settings_menu.h"
#include "deathlink.h"
#include "ap_announce.h"
#include "textbox_api.h"
#include "ap_colors.h"
#include "ap_check_detect.h"

// A countdown rather than a guard around the kill: the HP death lands frames after
// Ply_SetHP. Far short of the 150-frame respawn timer, so it can't swallow a real death.
#define DEATHLINK_SUPPRESS_FRAMES 60

static u8 deathlink_suppress[PLY_NUM];

static void Announce(const char *suffix)
{
    if (APAnnounce_LocalEnabled(APLOCAL_LINK))
        tb_api->EnqueueColoredNoun(NULL, "DeathLink", APColor_Death, suffix);
}

static void SuppressSend(int ply)
{
    if ((u32)ply < PLY_NUM)
        deathlink_suppress[ply] = DEATHLINK_SUPPRESS_FRAMES;
}

static void TickSuppress(void)
{
    for (int i = 0; i < PLY_NUM; i++)
    {
        if (deathlink_suppress[i])
            deathlink_suppress[i]--;
    }
}

static void ClearSuppress(void)
{
    memset(deathlink_suppress, 0, sizeof(deathlink_suppress));
}

// The human check stays with the caller: Top Ride has its own player kinds.
static int DeathLinkSendAllowed(int ply)
{
    if (!ap_menu_settings.deathlink_enabled)
        return 0;
    if ((u32)ply < PLY_NUM && deathlink_suppress[ply])
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
    if (Ply_GetDescPKind(ply) != PKIND_HMN)
        return;

    OSReport("[DeathLink] Player %d died (%s), sent\n", ply + 1, cause);
    ap_data->deathlink_send = 1;
    Announce(" sent!");
}

// Hook at 0x801a06d0 in Rider_CheckToDieOnMachine (0x801a06a8), where Rider_IsMachineDead
// returned true. Fall deaths set is_fall_dead instead and don't reach here.
static void DeathLink_OnHpDeath(RiderData *rd)
{
    SendDeathLink(rd->ply, "HP");
}
CODEPATCH_HOOKCREATE(0x801a06d0, "mr 3, 31\n\t", DeathLink_OnHpDeath, "", 0)

// Hook at 0x801e6540 in Machine_SetFallDead (0x801e6520), where a machine falls out of
// bounds. r31 = MachineData, rider_gobj non-null. Clobbered: stw r4, 0x1b48(r31).
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

// HP death in City Trial and the HP stadiums, where a fall death has no out-of-bounds
// respawn spline; a fall death everywhere else.
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
        dl.attacker_ply = PLY_NUM; // nobody is credited the KO

        // Ply_AddDeath tallies by vanilla kind, and a registered machine's slot would index
        // past its table; custom_machines' own KO seam swaps in the same kind.
        int is_bike = md->is_bike;
        int class_slot = md->kind;
        if (CustomMachines_ResolveKind(cm_api, is_bike, class_slot) >= VCKIND_NUM)
            class_slot = CustomMachines_ClassIndexOf(cm_api, VCKIND_WHEELVSDEDEDE, &is_bike);
        Ply_AddDeath(rd->ply, &dl, is_bike, class_slot);
        Ply_SetHP(rd->ply, 0);
        APCheckDetect_OnKnockedOut(rd->ply);
    }
    else
    {
        // backup_respawn_pos covers a failed checkpoint lookup; a -1 ground handle is
        // vanilla's no-dead-zone-surface case.
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
    for (int i = 0; i < PLY_NUM; i++)
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

    // Nobody killable this frame: the flag stays set for a later one.
    if (!killed)
        return;

    OSReport("[DeathLink] Received - killed %d human(s)\n", killed);
    Announce(" received!");
    ap_data->deathlink_receive = 0;
}

void DeathLink_On3DLoadEnd()
{
    ClearSuppress();
    OSReport("[DeathLink] Active\n");
    GOBJ_EZCreator(0, GAMEPLINK_SYS, 0, 0, 0, HSD_OBJKIND_NONE, 0, DeathLink_PerFrame, 0, 0, 0, 0);
}

// Hook at 0x80331a94, the sand-pit eject call (KirbyDoodlebugOut, vt+0xD0) in
// TopRideSandPit_Update (0x80331564); r31 = kirby. Doodlebug-item ejects share the wrapper
// at 0x802e2804 and are not caught.
static void DeathLink_OnTopRideSandPit(TopRideKirby *kirby)
{
    if (!DeathLinkSendAllowed(kirby->player_slot))
        return;
    if (TopRide_GetPlayerKind(kirby->player_slot) != TR_PKIND_HMN)
        return;
    TopRideKirbyMgr *mgr = *stc_topride_kirbymgr;
    if (!mgr || mgr->round_state != 2)
        return;

    OSReport("[DeathLink] Player %d died (TR sand pit), sent\n",
             kirby->player_slot + 1);
    ap_data->deathlink_send = 1;
    Announce(" sent!");
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

// Top Ride has no HP or fall death, so a receive puts every human kirby in one stun state.
// Speed Down is TrapLink's.
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
#define DEATHLINK_STATE_COUNT GetElementsIn(deathlink_states)

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

        // Zeroed on both sides: some setters scale velocity, others overwrite it.
        Vec3 *vel = &kirby->charge.velocity;
        vel->X = vel->Y = vel->Z = 0.0f;
        apply(kirby);
        vel->X = vel->Y = vel->Z = 0.0f;
        hits++;
    }

    if (!hits)
        return;

    OSReport("[DeathLink] Received (TR) - applied %s to %d human(s)\n",
             deathlink_state_names[idx], hits);
    Announce(" received!");
    ap_data->deathlink_receive = 0;
}

void DeathLink_OnTopRideLoadEnd()
{
    ClearSuppress();
    OSReport("[DeathLink] Active (Top Ride)\n");
    GOBJ_EZCreator(0, GAMEPLINK_SYS, 0, 0, 0, HSD_OBJKIND_NONE, 0, DeathLink_TopRidePerFrame, 0, 0, 0, 0);
}

void DeathLink_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x801a06d0);
    CODEPATCH_HOOKAPPLY(0x801e6540);
    CODEPATCH_HOOKAPPLY(0x80331a94);
    OSReport("[DeathLink] Hooks installed\n");
}
