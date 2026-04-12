#include "game.h"
#include "menu.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_machines.h"
#include "textbox.h"

static int IsCKindUnlocked(CharacterKind ckind);
CharacterKind GateMachines_GetDefaultCKind();

static const char *machine_names[VCKIND_NUM] = {
    [VCKIND_WARP]           = "Warp Star",
    [VCKIND_COMPACT]        = "Compact Star",
    [VCKIND_WINGED]         = "Winged Star",
    [VCKIND_SHADOW]         = "Shadow Star",
    [VCKIND_HYDRA]          = "Hydra",
    [VCKIND_BULK]           = "Bulk Star",
    [VCKIND_SLICK]          = "Slick Star",
    [VCKIND_FORMULA]        = "Formula Star",
    [VCKIND_DRAGOON]        = "Dragoon",
    [VCKIND_WAGON]          = "Wagon Star",
    [VCKIND_ROCKET]         = "Rocket Star",
    [VCKIND_SWERVE]         = "Swerve Star",
    [VCKIND_TURBO]          = "Turbo Star",
    [VCKIND_JET]            = "Jet Star",
    [VCKIND_FLIGHT]         = "Flight Warp Star",
    [VCKIND_FREE]           = "Free Star",
    [VCKIND_STEER]          = "Steer Star",
    [VCKIND_WINGKIRBY]      = "Wing Kirby",
    [VCKIND_WINGMETAKNIGHT] = "Wing Meta Knight",
    [VCKIND_WHEELNORMAL]    = "Wheel",
    [VCKIND_WHEELKIRBY]     = "Wheel Kirby",
    [VCKIND_WHEELIEBIKE]    = "Wheelie Bike",
    [VCKIND_REXWHEELIE]     = "Rex Wheelie",
    [VCKIND_WHEELIESCOOTER] = "Wheelie Scooter",
    [VCKIND_WHEELDEDEDE]    = "Dedede Wheelie",
    [VCKIND_WHEELVSDEDEDE]  = "VS Dedede Wheelie",
};

// Convert CharacterDesc fields to the actual MachineKind (VCKIND).
// For non-bikes, machine_kind IS the VCKIND directly.
// For bikes, machine_kind is a bike-relative index; the actual VCKIND
// is VCKIND_WHEELNORMAL + machine_kind.
static MachineKind CharacterDesc_GetMachineKind(CharacterDesc *desc)
{
    if (desc->is_bike)
        return VCKIND_WHEELNORMAL + desc->machine_kind;
    return desc->machine_kind;
}

// Replace the vanilla spawn selection logic entirely.
// Adapted from KAR Deluxe (UnclePunch/KAR-Deluxe, machines.c).
// Reads the spawn chance table, zeros out locked machines, gives unlocked
// machines with 0 base chance a minimum weight, reduces history size for
// low unlock counts, and does a weighted random selection.
int GateMachines_SelectSpawn(MachineSpawnData *msd, float match_progress)
{
    u32 unlocked_mask = ap_save->machine_unlocked_mask;
    vcDataCommon *vc_data_common = (*stc_vcDataCommon);

    // Find spawn table entry for current match progress
    int spawn_table_idx = 0;
    while (match_progress > vc_data_common->spawn_data->spawn_desc[spawn_table_idx].match_progress)
        spawn_table_idx++;

    // Make local copy of spawn chances
    float spawn_chances[VCKIND_NUM];
    for (int i = 0; i < VCKIND_NUM; i++)
        spawn_chances[i] = vc_data_common->spawn_data->spawn_desc[spawn_table_idx].chance[i];

    // Zero locked machines, give unlocked-but-zero-weight machines a base chance
    for (int i = 0; i < VCKIND_NUM; i++)
    {
        if (!(unlocked_mask & (1 << i)))
            spawn_chances[i] = 0;          // locked: zero out
        else if (spawn_chances[i] == 0)
            spawn_chances[i] = 10;         // unlocked but zero weight: give base chance
    }

    // Exclude machines that don't naturally spawn in CT (Top Ride stars,
    // wing transformations, debug wheelie kinds). Must be after the unlock
    // loop to override the base chance fallback.
    spawn_chances[VCKIND_FREE] = 0;
    spawn_chances[VCKIND_STEER] = 0;
    spawn_chances[VCKIND_WINGKIRBY] = 0;
    spawn_chances[VCKIND_WHEELNORMAL] = 0;
    spawn_chances[VCKIND_WHEELKIRBY] = 0;
    spawn_chances[VCKIND_WHEELDEDEDE] = 0;
    spawn_chances[VCKIND_WHEELVSDEDEDE] = 0;

    // Count spawnable machines (nonzero chance after exclusions)
    int spawnable_count = 0;
    for (int i = 0; i < VCKIND_NUM; i++)
    {
        if (spawn_chances[i] > 0)
            spawnable_count++;
    }

    // No spawnable machines — return Compact Star as a safe fallback
    if (spawnable_count == 0)
        return VCKIND_COMPACT;

    // Reduce history size when few machines are available to prevent
    // the only unlocked machine from being excluded by its own history
    int history_size = (spawnable_count <= 4) ? (spawnable_count - 1) : 4;

    // Remove recently spawned machines from candidates
    for (int i = 0; i < VCKIND_NUM; i++)
    {
        for (int j = 0; j < history_size; j++)
        {
            if (i == msd->prev_machine_kind[j])
                spawn_chances[i] = 0;
        }
    }

    // Weighted random selection
    int machine_kind = VCKIND_COMPACT;
    float chance_total = 0;
    for (int i = 0; i < VCKIND_NUM; i++)
        chance_total += spawn_chances[i];

    float random_chance = HSD_Randf() * chance_total;
    chance_total = 0;
    for (int i = 0; i < VCKIND_NUM; i++)
    {
        chance_total += spawn_chances[i];
        if (random_chance < chance_total)
        {
            machine_kind = i;
            break;
        }
    }

    // History writes are handled by the vanilla code after our skip target
    // (0x801df220 / 0x801df630), which writes r31 to the history buffer.
    return machine_kind;
}

// Filter the City Trial select screen machine list to only include
// unlocked machines. Call this after the machine list is populated.
void GateMachines_FilterSelectList()
{
    GameData *gd = Gm_GetGameData();
    if (!gd)
        return;

    u8 *arr = gd->city_select_ply.machine_select.c_kind_arr;
    u8 num = gd->city_select_ply.machine_select.num;
    u32 mask = ap_save->machine_unlocked_mask;
    u8 write = 0;

    for (u8 read = 0; read < num; read++)
    {
        CharacterDesc *desc = Character_GetDesc(arr[read]);
        if (!desc)
            continue;

        MachineKind vckind = CharacterDesc_GetMachineKind(desc);
        if (!(mask & (1 << vckind)))
            continue;

        if (write != read)
            arr[write] = arr[read];
        write++;
    }

    // Ensure at least one machine is selectable
    if (write == 0 && num > 0)
    {
        arr[0] = arr[0]; // keep first entry as fallback
        write = 1;
    }

    gd->city_select_ply.machine_select.num = write;

    // City Trial mode 0: fix the initial ply_icon_ckind for all players.
    // CitySelect_LoadCityTrial hardcodes it to CKIND_COMPACT (0); override
    // with the correct default so the CSS icon matches the starting machine.
    // Note: Gm_GetCityMode() reads GameData[0x399] which isn't set during CSS;
    // use city_select_ply.x1d0 (GameData[0x1D0]) which is set by the CSS loader.
    if (gd->city_select_ply.x1d0 == CITYMODE_TRIAL)
    {
        CharacterKind default_ckind = GateMachines_GetDefaultCKind();
        for (int i = 0; i < 4; i++)
        {
            if (!IsCKindUnlocked(gd->city_select_ply.ply_icon_ckind[i]))
                gd->city_select_ply.ply_icon_ckind[i] = default_ckind;
        }
    }
}

// Replace the spawn selection in CityMachineSpawn_DecideAndSpawn (0x801defac).
// At 0x801df00c: r30 = MachineSpawnData*, f1 = match_progress.
// Prologue: pass r30 as r3 (first arg); f1 passes through as float arg.
// Epilogue: result (machine_kind) in r3 → r31 for subsequent code.
// Skip to 0x801df220: past vanilla selection, where r31 is used for history
// write and CityMachineSpawn_Create.
CODEPATCH_HOOKCREATE(0x801df00c,
    "mr 3, 30\n\t",
    GateMachines_SelectSpawn,
    "mr 31, 3\n\t",
    0x801df220
)

// Replace the spawn selection in cityTrialSpawnFormationStar (0x801df408).
// Same register layout as DecideAndSpawn at its hook point.
CODEPATCH_HOOKCREATE(0x801df44c,
    "mr 3, 30\n\t",
    GateMachines_SelectSpawn,
    "mr 31, 3\n\t",
    0x801df630
)

// Check if a CharacterKind's machine is unlocked.
static int IsCKindUnlocked(CharacterKind ckind)
{
    CharacterDesc *desc = Character_GetDesc(ckind);
    if (!desc)
        return 0;
    MachineKind vckind = CharacterDesc_GetMachineKind(desc);
    return (ap_save->machine_unlocked_mask & (1 << vckind)) ? 1 : 0;
}

// Count unlocked characters for City Trial select screens.
// Replaces the mode 1 (Stadium) and mode 2 (Free Run) counting passes
// in CitySelect_CreateMachineIcons.
// Iterates all 20 CharacterKinds and counts those whose machines are unlocked.
int GateMachines_CountCTSelectAvailable()
{
    int count = 0;
    for (int ckind = 0; ckind < CKIND_NUM; ckind++)
    {
        if (IsCKindUnlocked(ckind))
            count++;
    }
    return count;
}

// Build the filtered character array for City Trial select screens.
// Replaces the mode 1 (Stadium) and mode 2 (Free Run) array-building passes
// in CitySelect_CreateMachineIcons.
// Iterates the 2x10 icon grid and writes only unlocked characters into the
// two-row local arrays used by the subsequent reordering code.
// Parameters are pointers to the function's stack locals:
//   char_arr  = local_41 (r29, 20-byte array: row0[0..9] at +0, row1[0..9] at +10)
//   row_counts = local_48 (r28, row_counts[0] and row_counts[1])
void GateMachines_BuildCTSelectArray(u8 *char_arr, u8 *row_counts)
{
    row_counts[0] = 0;
    row_counts[1] = 0;

    for (int row = 0; row < 2; row++)
    {
        for (int col = 0; col < 10; col++)
        {
            CharacterKind ckind = SelIcon_GetCKind(row, col);
            if (IsCKindUnlocked(ckind))
            {
                char_arr[row * 10 + row_counts[row]] = (u8)ckind;
                row_counts[row]++;
            }
        }
    }
}

// Hook at 0x8002e4d0: mode 1 (Stadium) counting pass in CitySelect_CreateMachineIcons.
// At entry: r3 = 0 (loop iterator init), r27 = 0 (count init).
// We replace the entire counting loop. Result goes to r27 (total count).
// Exit to 0x8002e670: past the counting loop, where mode is rechecked before
// the array-building pass.
CODEPATCH_HOOKCREATE(0x8002e4d0,
    "",
    GateMachines_CountCTSelectAvailable,
    "mr 27, 3\n\t",
    0x8002e670
)

// Hook at 0x8002e67c: mode 1 (Stadium) array-building pass.
// At entry: r29 = local_41 (char array), r28 = local_48 (row counts).
// r26 and r31 are set from r29/r28 inside the loop, but we skip the loop entirely.
// Exit to 0x8002e820: into the reordering code that reads from the stack arrays.
CODEPATCH_HOOKCREATE(0x8002e67c,
    "mr 3, 29\n\t"
    "mr 4, 28\n\t",
    GateMachines_BuildCTSelectArray,
    "",
    0x8002e820
)

// Hook at 0x8002e5c0: mode 2 (Free Run) counting pass in CitySelect_CreateMachineIcons.
// At entry: r24 = loop iterator (about to be initialized to 0).
// We replace the entire counting loop. Result goes to r27 (total count).
// Clobbered: li r24, 0 (harmless, r24 is not needed after we skip the loop).
// Exit to 0x8002e670: past the counting loop, where mode is rechecked before
// the array-building pass.
CODEPATCH_HOOKCREATE(0x8002e5c0,
    "",
    GateMachines_CountCTSelectAvailable,
    "mr 27, 3\n\t",
    0x8002e670
)

// Hook at 0x8002e738: mode 2 (Free Run) array-building pass.
// At entry: r29 = local_41 (char array), r28 = local_48 (row counts).
// We replace the entire array-building loop with our filtered version.
// Clobbered: or r26, r29, r29 (mr r26, r29 — harmless for the reordering code
// which reads from stack, not r26).
// Exit to 0x8002e820: into the reordering code that reads from the stack arrays.
CODEPATCH_HOOKCREATE(0x8002e738,
    "mr 3, 29\n\t"
    "mr 4, 28\n\t",
    GateMachines_BuildCTSelectArray,
    "",
    0x8002e820
)

// Get the first unlocked MachineKind, or VCKIND_COMPACT as absolute fallback.
static MachineKind GetFirstUnlockedMachine()
{
    u32 mask = ap_save->machine_unlocked_mask;
    for (int i = 0; i < VCKIND_NUM; i++)
    {
        if (mask & (1 << i))
            return i;
    }
    return VCKIND_COMPACT;
}

// Get the default CharacterKind for City Trial mode 0.
// Prefers Compact Star (vanilla default); falls back to a random unlocked machine.
CharacterKind GateMachines_GetDefaultCKind()
{
    if (IsCKindUnlocked(CKIND_COMPACT))
        return CKIND_COMPACT;

    // Build list of unlocked CKinds and pick one at random
    CharacterKind unlocked[CKIND_NUM];
    int count = 0;
    for (int ckind = 0; ckind < CKIND_NUM; ckind++)
    {
        if (IsCKindUnlocked(ckind))
            unlocked[count++] = ckind;
    }

    if (count > 0)
    {
        CharacterKind chosen = unlocked[HSD_Randi(count)];
        OSReport("GateMachines_GetDefaultCKind: Compact locked, chose ckind %d from %d unlocked (mask=0x%08x)\n",
                 chosen, count, ap_save->machine_unlocked_mask);
        return chosen;
    }

    OSReport("GateMachines_GetDefaultCKind: no machines unlocked, fallback to Compact\n");
    return CKIND_COMPACT;
}

// Replace the respawn machine assignment in Rider_ResetStartingMachine.
// Vanilla hardcodes VCKIND_COMPACT; we use the rider's starting_machine_idx
// if unlocked, otherwise the first unlocked machine.
void GateMachines_ResetStartingMachine(RiderData *rd)
{
    u8 ply = rd->ply;
    MachineKind vckind = rd->starting_machine_idx;

    if (!(ap_save->machine_unlocked_mask & (1 << vckind)))
        vckind = GetFirstUnlockedMachine();

    if (vckind >= VCKIND_WHEELNORMAL)
    {
        Ply_SetMachineIsBike(ply, 1);
        Ply_SetMachineKind(ply, vckind - VCKIND_WHEELNORMAL);
    }
    else
    {
        Ply_SetMachineIsBike(ply, 0);
        Ply_SetMachineKind(ply, vckind);
    }
}

// Hook at 0x8002de80 in CitySelect_InitPlayerMachines.
// Vanilla hardcodes ply_icon_ckind = 0 (CKIND_COMPACT) for City Trial mode 0.
// We replace it with the first unlocked CharacterKind so players start on an
// unlocked machine. r28 = city_select_ply + player_offset (callee-saved).
// Skipped instructions: stb r0,97(r28) and b 0x8002dea0 — we handle both.
CODEPATCH_HOOKCREATE(0x8002de80,
    "",
    GateMachines_GetDefaultCKind,
    "stb 3, 97(28)\n\t",
    0x8002dea0
)

// Hook at 0x801952c8 in Rider_ResetStartingMachine.
// At entry: r31 = RiderData*. Replaces the two Ply_Set calls (is_bike=0,
// machine_kind=COMPACT) with our validated selection.
// Skip to 0x801952e0: the function epilogue.
CODEPATCH_HOOKCREATE(0x801952c8,
    "mr 3, 31\n\t",
    GateMachines_ResetStartingMachine,
    "",
    0x801952e0
)

// Replace AirRide_CheckCharacterAvailable (0x8002090c).
// Called from AirRide_PopulateSelectIcons (0x80020a08) to determine which
// characters appear on the Air Ride character select screen.
// Vanilla checks checklist reward indices; we check machine_unlocked_mask.
int GateMachines_CheckAirRideCharacterAvailable(CharacterKind ckind)
{
    // Dragoon, Hydra, and Flight Warp Star are City Trial-only (never selectable)
    if (ckind == CKIND_DRAGOON || ckind == CKIND_HYDRA || ckind == CKIND_FLIGHT)
        return 0;

    CharacterDesc *desc = Character_GetDesc(ckind);
    if (!desc)
        return 0;

    MachineKind vckind = CharacterDesc_GetMachineKind(desc);
    return (ap_save->machine_unlocked_mask & (1 << vckind)) ? 1 : 0;
}

// Replace AirRide_CheckMachineUnlocked (0x8000c364).
// Called from AirRide_SelectRandomMachine (0x8000daa0) for CPU random
// machine assignment. The second parameter (machine_id) is the MachineKind.
int GateMachines_CheckAirRideMachineUnlocked(s8 machine_class, s8 machine_id)
{
    // machine_class = CharacterDesc.is_bike, machine_id = CharacterDesc.machine_kind.
    // For bikes, machine_kind is a bike-relative index, not the VCKIND.
    int vckind;
    if (machine_class)
        vckind = VCKIND_WHEELNORMAL + machine_id;
    else
        vckind = machine_id;

    if (vckind < 0 || vckind >= VCKIND_NUM)
        return 0;

    return (ap_save->machine_unlocked_mask & (1 << vckind)) ? 1 : 0;
}

void GateMachines_OnBoot()
{
    // City Trial spawn hooks
    CODEPATCH_HOOKAPPLY(0x801df00c);
    CODEPATCH_HOOKAPPLY(0x801df44c);

    // Air Ride select screen: replace character availability check
    CODEPATCH_REPLACEFUNC(AirRide_CheckCharacterAvailable, GateMachines_CheckAirRideCharacterAvailable);

    // Air Ride random machine: replace machine unlock check
    CODEPATCH_REPLACEFUNC(AirRide_CheckMachineUnlocked, GateMachines_CheckAirRideMachineUnlocked);

    // City Trial Stadium select screen: replace both the counting pass and
    // array-building pass in CitySelect_CreateMachineIcons (0x8002e3c4) for
    // mode 1. Vanilla mode 1 only checks ckind ranges (0-14 available,
    // 15/16/18/19 special characters unavailable) — no unlock mask check.
    CODEPATCH_HOOKAPPLY(0x8002e4d0);  // mode 1 counting pass
    CODEPATCH_HOOKAPPLY(0x8002e67c);  // mode 1 array-building pass

    // City Trial Free Run select screen: same treatment for mode 2.
    CODEPATCH_HOOKAPPLY(0x8002e5c0);  // mode 2 counting pass
    CODEPATCH_HOOKAPPLY(0x8002e738);  // mode 2 array-building pass

    // City Trial mode 0: replace hardcoded Compact Star default with first unlocked machine
    CODEPATCH_HOOKAPPLY(0x8002de80);

    // Respawn machine validation: use starting machine instead of hardcoded Compact
    CODEPATCH_HOOKAPPLY(0x801952c8);

    OSReport("Machine gating hooks installed (CT spawn + CT mode 0 default + CT Stadium + CT Free Run + AR select + respawn)\n");
}

int GateMachines_UnlockMachine(MachineKind kind)
{
    if (kind >= VCKIND_NUM)
        return 0;

    ap_save->machine_unlocked_mask |= (1 << kind);
    OSReport("Machine %d (%s) unlocked (mask = 0x%08x)\n",
             kind, machine_names[kind], ap_save->machine_unlocked_mask);
    TextBox_Enqueue(machine_names[kind]);
    return 1;
}

// Give a player the assembled legendary machine via the cinematic.
// machine_index: 0 = Dragoon, 1 = Hydra.
// Returns 1 if started, 0 if not in City Trial or no machine to swap from.
int GateMachines_GiveLegendaryMachine(int machine_index)
{
    if (!Gm_IsInCity())
        return 0;

    int given = 0;
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;

        GOBJ *machine_gobj = Ply_GetMachineGObj(i);
        if (!machine_gobj)
            continue;

        MachineData *md = (MachineData *)machine_gobj->userdata;
        if (!md)
            continue;

        LegendaryAssemblyParams params;
        params.machine_index = machine_index;
        params.ply = i;
        params.pos = md->pos;
        params.up = md->up;
        params.forward = md->forward;

        LegendaryMachine_StartAssembly(&params);
        OSReport("Legendary machine %s assembly started for player %d\n",
                 machine_index == 0 ? "Dragoon" : "Hydra", i);
        given = 1;
    }

    return given;
}
