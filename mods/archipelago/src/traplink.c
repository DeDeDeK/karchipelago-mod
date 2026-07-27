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
#include "textbox_api.h"
#include "traplink.h"
#include "ap_item_handler.h"
#include "gate_topride_items.h"

// Receive -> send recursion guard: a received trap must not bounce back out. The
// CT receive path re-fires the same hooks that detect organic trap pickups, so
// sends are suppressed for a window covering the apply -> pickup-hook latency.
#define TRAPLINK_RECV_GUARD_FRAMES 120
static int recv_suppress_frames = 0;

void TrapLink_Send(TrapLinkKind kind)
{
    if (!ap_menu_settings.traplink_enabled)
        return;
    if (kind == TRAPLINK_KIND_NONE)
        return;
    if (recv_suppress_frames > 0)
    {
        OSReport("[TrapLink] Suppressing send (kind %d) - within receive guard window\n", kind);
        return;
    }

    OSReport("[TrapLink] Traplink send triggered (kind %d)\n", kind);
    ap_data->traplink_send = (uint)kind;
}

// Trap items that can be randomly selected when traplink is triggered
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
    AP_EVENT_METEOR,
    AP_EVENT_RAILFIRE,
    AP_EVENT_BOUNCE,
    AP_EVENT_FAKEPOWERUPS,
    AP_EVENT_RUNAMOK,
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
#define TRAP_ITEM_COUNT (sizeof(trap_items) / sizeof(trap_items[0]))

// Returns 1 if the item is an event whose unlock bit is unset, so it must be
// excluded from the trap pool.
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
// applies. APItems_HandleItem returns 0 for items that can't apply in the current
// scene/mode, so iterating in one tick avoids waiting frames for a random pick to
// land on an applicable item. Returns 1 if any trap applied.
static int ApplyCityTrialTrap(void)
{
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

    // Fisher-Yates shuffle so the attempt order is randomized across frames.
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

// Air Ride receive: give the sleep copy ability to every human rider. Most City
// Trial trap items need Gm_IsInCity, so this bypasses APItems_HandleItem. Calls
// the raw rider API rather than Rider_CheckAndGiveAbility so the ability gate and
// the sleep-send hook do not re-trigger.
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

// Top Ride bad items that penalize the picker via TopRide_KirbyApplyItem. Only
// items whose dispatcher installs a self-debuff state belong here; most TR items
// buff the user or arm an attack instead.
static const TopRideItemKind tr_trap_items[] = {
    TRITEM_SPEED_DOWN,
};
#define TR_TRAP_ITEM_COUNT (sizeof(tr_trap_items) / sizeof(tr_trap_items[0]))

static int ApplyTopRideTrap(void)
{
    TopRideItemKind kind = tr_trap_items[HSD_Randi(TR_TRAP_ITEM_COUNT)];
    return GateTopRideItems_GiveItem(kind);
}

// Dispatch a mode-appropriate trap on receive. The GObj is only installed in 3D /
// Top Ride scenes, so the major is always CITY/AIR/TOP here.
static void TrapLink_PerFrame(GOBJ *g)
{
    // Before the receive check, so idle frames advance the guard too.
    if (recv_suppress_frames > 0)
        recv_suppress_frames--;

    if (!ap_data->traplink_receive)
        return;

    // Only bites in 3D: Top Ride has no intro sequence and Gm_GetIntroState
    // defaults to GMINTRO_END there.
    if (Gm_GetIntroState() != GMINTRO_END)
        return;

    MajorKind major = Scene_GetCurrentMajor();
    int handled = 0;
    switch (major)
    {
        case MJRKIND_CITY:
            // Free Run and stadiums don't load item data, so most CT traps would
            // crash inside SpawnItem / enemy / fake-patch spawn. Stadiums still
            // have rider GOBJs, so they fall back to the AR sleep trap.
            if (Gm_GetCityMode() == CITYMODE_FREERUN)
            {
                OSReport("[TrapLink] Dropping CT trap in Free Run (item data not loaded).\n");
                handled = 1;
            }
            else if (CityTrial_IsInStadium())
            {
                OSReport("[TrapLink] Stadium - falling back to sleep ability trap.\n");
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
        tb_api->EnqueueColoredNoun(NULL, "Trap", tb_api->TrapColor, " received!");
        ap_data->traplink_receive = 0;
        // The apply is about to trigger our own send hooks.
        recv_suppress_frames = TRAPLINK_RECV_GUARD_FRAMES;
    }
}

void TrapLink_On3DLoadEnd()
{
    OSReport("[TrapLink] Active\n");
    recv_suppress_frames = 0;
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, TrapLink_PerFrame, 0, 0, 0, 0);
}

void TrapLink_OnTopRideLoadEnd()
{
    OSReport("[TrapLink] Active (Top Ride)\n");
    recv_suppress_frames = 0;
    // Top Ride has no rider GObjs, so install a standalone per-frame proc.
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, TrapLink_PerFrame, 0, 0, 0, 0);
}

// Hook in Machine_OnTouchItem on the branch where CityItem_IsGoodPatch returned 0,
// catching SPEEDMIN, CHARGENONE, and fake patches. r20 = MachineData*;
// clobbered: lwz r0, 0xA10(r20).
static void TrapLink_OnBadPatch(MachineData *md)
{
    int ply = Machine_GetRiderPly(md);
    if (Ply_CheckIfCPU(ply))
        return;
    TrapLink_Send(TRAPLINK_KIND_BAD_PATCH);
}
CODEPATCH_HOOKCREATE(0x801DB504,
    "mr 3, 20\n\t",
    TrapLink_OnBadPatch,
    "",
    0)

static int IsTopRideBadItem(u8 kind)
{
    for (int i = 0; i < (int)TR_TRAP_ITEM_COUNT; i++)
    {
        if (tr_trap_items[i] == kind)
            return 1;
    }
    return 0;
}

// Hook inside TopRideItem_Update (0x8034c7dc) where an item is marked absorbed.
// r31 = item list node with the kind byte at +0x68, r26 = absorber position.
static void TrapLink_OnTopRideItemPickup(u8 item_kind, Vec3 *absorber_pos)
{
    if (!IsTopRideBadItem(item_kind))
        return;

    TopRideKirbyMgr *mgr = *stc_topride_kirbymgr;
    if (!mgr || !absorber_pos)
        return;

    // The absorber's position coincides with the TopRideKirby's charge-component
    // position while the kirby is in pickup range, so the nearest kirby is the
    // one that picked up.
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
        return; // CPU picked it up - don't send

    OSReport("[TrapLink] TR ply %d picked up bad item %d\n", closest, item_kind);
    TrapLink_Send(TRAPLINK_KIND_SPEED_DOWN);
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
