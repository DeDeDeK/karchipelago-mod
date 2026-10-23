#include <float.h>

#include "game.h"
#include "scene.h"
#include "inline.h"
#include "item.h"
#include "machine.h"
#include "rider.h"
#include "topride.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "settings_menu.h"
#include "ap_announce.h"
#include "textbox_api.h"
#include "ap_colors.h"
#include "traplink.h"
#include "ap_item_handler.h"
#include "gate_topride_items.h"
#include "ability_item.h"

// Sends are suppressed this long from a receive: applying a trap re-fires the pickup hooks
// that send, some in the same frame and the fake patches a frame or more later.
#define TRAPLINK_RECV_GUARD_FRAMES 120
static int recv_suppress_frames = 0;

// The same strings the client sends as trap_name.
static const char *const traplink_kind_names[] = {
    [TRAPLINK_KIND_BAD_PATCH]  = "Bad Patch",
    [TRAPLINK_KIND_SLEEP]      = "Sleep",
    [TRAPLINK_KIND_SPEED_DOWN] = "Speed Down",
};

void TrapLink_Send(TrapLinkKind kind)
{
    if (!ap_menu_settings.traplink_enabled)
        return;
    if (kind == TRAPLINK_KIND_NONE)
        return;
    if (recv_suppress_frames > 0)
        return;

    OSReport("[TrapLink] Sent (kind %d)\n", kind);
    ap_data->traplink_send = (uint)kind;

    if (APAnnounce_LocalEnabled(APLOCAL_LINK))
        tb_api->EnqueueColoredNounFmt(NULL, "TrapLink", APColor_Trap, " sent! (%s)",
                                      traplink_kind_names[kind]);
}

static uint trap_items[] = {
    AP_ITKIND_COPYSLEEP,
    AP_ITKIND_SPEEDMIN,
    AP_ITKIND_CHARGENONE,
    AP_ITKIND_BOOSTDOWN,
    AP_ITKIND_TOPSPEEDDOWN,
    AP_ITKIND_OFFENSEDOWN,
    AP_ITKIND_DEFENSEDOWN,
    AP_ITKIND_TURNDOWN,
    AP_ITKIND_GLIDEDOWN,
    AP_ITKIND_CHARGEDOWN,
    AP_ITKIND_WEIGHTDOWN,
    AP_ITEM_1_HP_TRAP,
    AP_ITEM_DROP_PATCHES_TRAP,
    AP_ITKIND_BOOSTFAKE,
    AP_ITKIND_TOPSPEEDFAKE,
    AP_ITKIND_OFFENSEFAKE,
    AP_ITKIND_DEFENSEFAKE,
    AP_ITKIND_TURNFAKE,
    AP_ITKIND_GLIDEFAKE,
    AP_ITKIND_CHARGEFAKE,
    AP_ITKIND_WEIGHTFAKE,
};
#define TRAP_ITEM_COUNT GetElementsIn(trap_items)

// Tries every trap in shuffled order in one tick; APItems_HandleItem refuses the ones that
// can't apply right now. No event is a candidate: a received event would outlast the
// receive guard, and its pickups would send traps back out.
static int ApplyCityTrialTrap(void)
{
    uint candidates[TRAP_ITEM_COUNT];
    int count = TRAP_ITEM_COUNT;
    memcpy(candidates, trap_items, sizeof(candidates));

    RandomShuffle(candidates, count, sizeof(candidates[0]));

    for (int i = 0; i < count; i++)
    {
        if (APItems_HandleItem(candidates[i]) != AP_ITEM_RETRY)
        {
            OSReport("[TrapLink] Applied trap item (AP ID %d)\n", candidates[i]);
            return 1;
        }
    }
    return 0;
}

// Sleep on every mounted human Kirby, straight through the rider API so the ability gate
// and its sleep send don't fire.
static int ApplyAirRideTrap(void)
{
    int applied = Ability_GiveHumans(COPYKIND_SLEEP);
    if (applied)
        OSReport("[TrapLink] Applied sleep-ability trap to %d player(s)\n", applied);
    return applied;
}

// The one Top Ride item whose TopRide_KirbyApplyItem dispatch debuffs the picker.
#define TR_TRAP_ITEM TRITEM_SPEED_DOWN

static int ApplyTopRideTrap(void)
{
    return GateTopRideItems_GiveItem(TR_TRAP_ITEM);
}

// Other majors - the title attract demo installs this proc too - leave the flag held for
// a real round.
static void TrapLink_PerFrame(GOBJ *g)
{
    if (recv_suppress_frames > 0)
        recv_suppress_frames--;

    if (!ap_data->traplink_receive)
        return;

    // Top Ride has no intro; Gm_GetIntroState reads GMINTRO_END there.
    if (Gm_GetIntroState() != GMINTRO_END)
        return;

    // Armed before the apply, which can re-fire our own send hooks in this same call.
    int prev_suppress = recv_suppress_frames;
    recv_suppress_frames = TRAPLINK_RECV_GUARD_FRAMES;

    int handled = 0;
    switch (Scene_GetCurrentMajor())
    {
        case MJRKIND_CITY:
            // Free Run and stadiums load no item data, so most CT traps would crash.
            // Stadium riders are always mounted, so they take the Air Ride trap.
            if (Gm_GetCityMode() == CITYMODE_FREERUN)
            {
                OSReport("[TrapLink] CT trap dropped in Free Run (item data not loaded)\n");
                handled = 1;
            }
            else if (CityTrial_IsInStadium())
                handled = ApplyAirRideTrap();
            else
                handled = ApplyCityTrialTrap();
            break;
        case MJRKIND_AIR:
            handled = ApplyAirRideTrap();
            break;
        case MJRKIND_TOP:
            handled = ApplyTopRideTrap();
            break;
    }

    if (!handled)
    {
        recv_suppress_frames = prev_suppress;
        return;
    }

    if (APAnnounce_LocalEnabled(APLOCAL_LINK))
        tb_api->EnqueueColoredNoun(NULL, "TrapLink", APColor_Trap, " received!");
    ap_data->traplink_receive = 0;
}

void TrapLink_On3DLoadEnd()
{
    OSReport("[TrapLink] Active\n");
    recv_suppress_frames = 0;
    GOBJ_EZCreator(0, GAMEPLINK_SYS, 0, 0, 0, HSD_OBJKIND_NONE, 0, TrapLink_PerFrame, 0, 0, 0, 0);
}

void TrapLink_OnTopRideLoadEnd()
{
    OSReport("[TrapLink] Active (Top Ride)\n");
    recv_suppress_frames = 0;
    GOBJ_EZCreator(0, GAMEPLINK_SYS, 0, 0, 0, HSD_OBJKIND_NONE, 0, TrapLink_PerFrame, 0, 0, 0, 0);
}

// Hook at 0x801db504 in Machine_OnTouchItem (0x801db34c), on the branch where
// CityItem_IsGoodPatch returned 0: the stat-downs, SPEEDMIN, CHARGENONE (group BAD) and
// the fakes (group FAKE). r20 = MachineData;
// clobbered: lwz r0, 0xA10(r20).
static void TrapLink_OnBadPatch(MachineData *md)
{
    // Machine_GetRiderPly returns 5 for a riderless machine.
    int ply = Machine_GetRiderPly(md);
    if ((u32)ply >= PLY_NUM || Ply_GetPKind(ply) != PKIND_HMN)
        return;
    TrapLink_Send(TRAPLINK_KIND_BAD_PATCH);
}
CODEPATCH_HOOKCREATE(0x801db504,
    "mr 3, 20\n\t",
    TrapLink_OnBadPatch,
    "",
    0)

// Hook at 0x8034c7dc in TopRideItem_Update (0x8034c130), where an item is marked absorbed.
// r31 = the item list node, kind byte at +0x68; r26 = the absorber position.
static void TrapLink_OnTopRideItemPickup(u8 item_kind, Vec3 *absorber_pos)
{
    if (item_kind != TR_TRAP_ITEM)
        return;

    TopRideKirbyMgr *mgr = *stc_topride_kirbymgr;
    if (!mgr || !absorber_pos)
        return;

    // The absorber sits at the picker's charge position, so the nearest kirby picked it up.
    int closest = -1;
    float closest_dist = FLT_MAX;
    for (int i = 0; i < 4; i++)
    {
        TopRideKirby *k = mgr->kirbys[i];
        if (!k)
            continue;
        float dist = VECSquareDistance(&k->charge.position, absorber_pos);
        if (dist < closest_dist)
        {
            closest_dist = dist;
            closest = i;
        }
    }

    if (closest < 0)
        return;

    TopRideKirby *picker = mgr->kirbys[closest];
    if (TopRide_GetPlayerKind(picker->player_slot) != TR_PKIND_HMN)
        return;

    TrapLink_Send(TRAPLINK_KIND_SPEED_DOWN);
}

CODEPATCH_HOOKCREATE(0x8034c7dc,
    "lbz 3, 104(31)\n\t"
    "mr 4, 26\n\t",
    TrapLink_OnTopRideItemPickup,
    "",
    0)

void TrapLink_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x801db504);
    CODEPATCH_HOOKAPPLY(0x8034c7dc);
    OSReport("[TrapLink] Hooks installed\n");
}
