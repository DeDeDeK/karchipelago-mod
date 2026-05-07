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
#include "textbox.h"
#include "textbox_colors.h"
#include "traplink.h"
#include "ap_item_handler.h"
#include "gate_topride_items.h"

// Send a traplink if enabled.
void TrapLink_Send(void)
{
    if (!ap_menu_settings.traplink_enabled)
        return;

    OSReport("[TrapLink] Traplink send triggered\n");
    ap_data->traplink_send = 1;
}

// Trap items that can be randomly selected when traplink is triggered
static uint trap_items[] = {
    AP_ITKIND_COPYSLEEP,
    AP_ITKIND_SPEEDMIN,
    AP_ITKIND_CHARGENONE,
    AP_ITKIND_ACCELDOWN,
    AP_ITKIND_TOPSPEEDDOWN,
    AP_ITKIND_OFFENSEDOWN,
    AP_ITKIND_DEFENSEDOWN,
    AP_ITKIND_TURNDOWN,
    AP_ITKIND_GLIDEDOWN,
    AP_ITKIND_CHARGEDOWN,
    AP_ITKIND_WEIGHTDOWN,
    AP_EVENT_METEOR,
    AP_EVENT_RAILFIRE,
    AP_EVENT_BOUNCE,
    AP_EVENT_FAKEPOWERUPS,
    AP_EVENT_RUNAMOK,
    AP_ITEM_1_HP_TRAP,
    AP_ITEM_DROP_PATCHES_TRAP,
    AP_ITKIND_ACCELFAKE,
    AP_ITKIND_TOPSPEEDFAKE,
    AP_ITKIND_OFFENSEFAKE,
    AP_ITKIND_DEFENSEFAKE,
    AP_ITKIND_TURNFAKE,
    AP_ITKIND_GLIDEFAKE,
    AP_ITKIND_CHARGEFAKE,
    AP_ITKIND_WEIGHTFAKE,
};
#define TRAP_ITEM_COUNT (sizeof(trap_items) / sizeof(trap_items[0]))

// Check if a trap item is an event that requires the event unlock mask.
// Returns 1 if the item is a locked event (should be excluded), 0 otherwise.
static int IsTrapItemLocked(uint item_id)
{
    if (item_id >= AP_EVENT_BASE && item_id < AP_EVENT_BASE + EVKIND_NUM)
    {
        EventKind kind = item_id - AP_EVENT_BASE;
        return !(ap_save->event_unlocked_mask & (1 << kind));
    }
    return 0;
}

// City Trial receive: try every eligible trap in shuffled order until one
// applies. APItems_HandleItem returns 0 for items that can't apply in the
// current scene/mode (e.g. ITKIND spawns outside actual City Trial, AP_EVENTs
// in stadium-style city modes), so iterating in one tick avoids the per-frame
// dispatcher loop where a single random pick fails and the receive flag stays
// set until many frames later when chance lands on an applicable item.
// Returns 1 if any trap applied, 0 otherwise.
static int ApplyCityTrialTrap(void)
{
    // Build filtered list excluding locked events
    uint candidates[TRAP_ITEM_COUNT];
    int count = 0;
    for (int i = 0; i < TRAP_ITEM_COUNT; i++)
    {
        if (!IsTrapItemLocked(trap_items[i]))
            candidates[count++] = trap_items[i];
    }

    if (count == 0)
    {
        OSReport("[TrapLink] no eligible trap items, discarding\n");
        return 1; // treat as handled so we clear the flag
    }

    // Fisher–Yates shuffle so the attempt order is randomized across frames.
    for (int i = count - 1; i > 0; i--)
    {
        int j = HSD_Randi(i + 1);
        uint tmp = candidates[i];
        candidates[i] = candidates[j];
        candidates[j] = tmp;
    }

    for (int i = 0; i < count; i++)
    {
        if (APItems_HandleItem(candidates[i]))
        {
            OSReport("[TrapLink] Applied trap item (AP ID %d)\n", candidates[i]);
            return 1;
        }
    }
    return 0;
}

// Air Ride receive: give sleep copy ability to every human rider.
// Most City Trial trap items are CT-only (ITKIND / EVENT dispatch requires
// Gm_IsInCity), so we bypass APItems_HandleItem and call Rider_GiveAbility
// directly. Uses the raw rider API (not Rider_CheckAndGiveAbility) so the
// ability gate and sleep-send hook in gate_abilities.c do not re-trigger.
static int ApplyAirRideTrap(void)
{
    int applied = 0;
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        GOBJ *rg = Ply_GetRiderGObj(i);
        if (!rg)
            continue;
        RiderData *rd = rg->userdata;
        if (!rd || rd->kind != RDKIND_KIRBY)
            continue;
        // Off-vehicle riders crash in the sleep anim's MObj callback, which
        // calls Rider_CopyInputToMachine and derefs a null machine_gobj.
        if (!Rider_IsOnMachine(rd))
            continue;
        OSReport("[TrapLink] giving sleep ability to ply %d\n", i);
        Rider_GiveAbility(rd, COPYKIND_SLEEP);
        applied = 1;
    }
    return applied;
}

// Top Ride bad items that penalize the picker-up via TopRide_KirbyApplyItem
// (which installs a self-affecting state on the kirby that "used" the item).
// Most TR items either buff the user or arm them with an attack against
// other players; only items whose dispatcher installs a self-debuff state
// belong here.
//
//   TRITEM_SPEEDDOWN — installs KirbySpeedDown (state ID 18, AC_RUN_LOOP
//                      with reduced speed_factor). Reliable trap.
//
// TRITEM_BACKWARD was tried but its 0x802d9188 dispatcher only resets to
// KirbyNormal and swaps in the KirbyUshiroyurerun vtable; the velocity-flip
// portion of the vanilla effect is set up by the absorber-pickup path that
// TopRide_KirbyApplyItem skips, so applied via this path it produces only
// the smoke-trail visual without slowing the picker. Excluded.
static const TopRideItemKind tr_trap_items[] = {
    TRITEM_SPEEDDOWN,
};
#define TR_TRAP_ITEM_COUNT (sizeof(tr_trap_items) / sizeof(tr_trap_items[0]))

// Top Ride receive: pick a random bad item and apply it to every human Kirby
// via the shared item-give path (TopRide_KirbyApplyItem under the hood).
static int ApplyTopRideTrap(void)
{
    TopRideItemKind kind = tr_trap_items[HSD_Randi(TR_TRAP_ITEM_COUNT)];
    return GateTopRideItems_GiveItem(kind);
}

// Read from the traplink_receive location and dispatch a mode-appropriate trap.
// The GObj is only installed in 3D / Top Ride scenes, so the major is always
// CITY/AIR/TOP here.
static void TrapLink_PerFrame(GOBJ *g)
{
    if (!ap_data->traplink_receive)
        return;

    // Intro state gate only applies to 3D (Air Ride / City Trial). Top Ride
    // has no intro sequence and Gm_GetIntroState defaults to GMINTRO_END.
    if (Gm_GetIntroState() != GMINTRO_END)
        return;

    MajorKind major = Scene_GetCurrentMajor();
    int handled = 0;
    switch (major)
    {
        case MJRKIND_CITY:
            // Free Run and stadiums don't load item data; most CT traps
            // would crash inside SpawnItem / enemy / fake-patch spawn.
            // Drop Free Run; fall back to the AR sleep trap for stadiums
            // since stadiums still have rider GOBJs to apply it to.
            if (Gm_GetCityMode() == CITYMODE_FREERUN)
            {
                OSReport("[TrapLink] Dropping CT trap in Free Run (item data not loaded).\n");
                handled = 1;
            }
            else if (Gm_IsStadiumMode())
            {
                OSReport("[TrapLink] Stadium — falling back to sleep ability trap.\n");
                handled = ApplyAirRideTrap();
            }
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

    if (handled)
    {
        TextBox_EnqueueColoredNoun(NULL, "Trap", TextBox_TrapColor, " received!");
        ap_data->traplink_receive = 0;
    }
}

void TrapLink_On3DLoadEnd()
{
    OSReport("[TrapLink] Active\n");
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, TrapLink_PerFrame, 0, 0, 0, 0);
}

void TrapLink_OnTopRideLoad()
{
    OSReport("[TrapLink] Active (Top Ride)\n");
    // Top Ride has no rider GObjs, so install a standalone per-frame proc.
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, TrapLink_PerFrame, 0, 0, 0, 0);
}

// Hook in Machine_OnTouchItem after CityItem_IsGoodPatch returns 0 (bad patch).
// Catches SPEEDMIN, CHARGENONE, and fake patches.
// At this point r20 = MachineData*.
// Clobbered instruction: lwz r0, 0xA10(r20)
static void TrapLink_OnBadPatch(MachineData *md)
{
    int ply = Machine_GetRiderPly(md);
    if (Ply_CheckIfCPU(ply))
        return;
    TrapLink_Send();
}
CODEPATCH_HOOKCREATE(0x801DB504,
    "mr 3, 20\n\t",
    TrapLink_OnBadPatch,
    "",
    0)

// Hook inside TopRideItem_Update (0x8034c130) at the moment the collision
// check has just succeeded and the item is about to be marked "absorbed".
// At 0x8034c7dc:
//   r31 = item list node (item at +8, kind byte at node+0x68)
//   r29 = absorber (kirby's item receiver)
//   r26 = absorber position Vec3* (loaded earlier at 0x8034c6e4 from
//         absorber.f18 vtable[2])
//   Clobbered instruction: lbz r4, 104(r31)
//
// We load the item kind and absorber position into r3/r4 for the C handler,
// then the framework re-executes the clobbered lbz to restore r4.
static int IsTopRideBadItem(u8 kind)
{
    for (int i = 0; i < (int)TR_TRAP_ITEM_COUNT; i++)
    {
        if (tr_trap_items[i] == kind)
            return 1;
    }
    return 0;
}

static void TrapLink_OnTopRideItemPickup(u8 item_kind, Vec3 *absorber_pos)
{
    if (!IsTopRideBadItem(item_kind))
        return;

    TopRideKirbyMgr *mgr = *stc_topride_kirbymgr;
    if (!mgr || !absorber_pos)
        return;

    // The absorber's position and the TopRideKirby's charge-component
    // position should coincide while the kirby is in pickup range of an
    // item. Find the kirby whose charge position is closest to the
    // absorber position that just absorbed — that's the one that picked up.
    int closest = -1;
    float closest_dist = 1.0e30f;
    for (int i = 0; i < 4; i++)
    {
        TopRideKirby *k = mgr->kirbys[i];
        if (!k)
            continue;
        float dx = k->charge.position.X - absorber_pos->X;
        float dy = k->charge.position.Y - absorber_pos->Y;
        float dz = k->charge.position.Z - absorber_pos->Z;
        float dist = dx * dx + dy * dy + dz * dz;
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
        return; // CPU picked it up — don't send

    OSReport("[TrapLink] TR ply %d picked up bad item %d\n", closest, item_kind);
    TrapLink_Send();
}

CODEPATCH_HOOKCREATE(0x8034C7DC,
    "lbz 3, 104(31)\n\t"
    "mr 4, 26\n\t",
    TrapLink_OnTopRideItemPickup,
    "",
    0)

void TrapLink_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x801DB504);
    CODEPATCH_HOOKAPPLY(0x8034C7DC);
    OSReport("[TrapLink] Hooks installed\n");
}
