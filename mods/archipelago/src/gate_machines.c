#include "game.h"
#include "menu.h"
#include "os.h"
#include "scene.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_machines.h"
#include "textbox_api.h"
#include "inline.h"

// Machines that don't naturally spawn in CT: Top Ride stars, transformation
// forms, and debug wheelie kinds. Force these to 0 chance regardless of mask.
#define CT_SPAWN_EXCLUDED_MASK \
    ((1u << VCKIND_FREE)        | \
     (1u << VCKIND_STEER)       | \
     (1u << VCKIND_WINGKIRBY)   | \
     (1u << VCKIND_WHEELNORMAL) | \
     (1u << VCKIND_WHEELKIRBY)  | \
     (1u << VCKIND_WHEELDEDEDE) | \
     (1u << VCKIND_WHEELVSDEDEDE))

static int IsCKindUnlocked(CharacterKind ckind)
{
    CharacterDesc *desc = Character_GetDesc(ckind);
    if (!desc)
        return 0;
    MachineKind vckind = CharacterDesc_GetMachineKind(desc);
    return (ap_save->machine_unlocked_mask & (1 << vckind)) ? 1 : 0;
}

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

// Default CharacterKind for the City Trial mode-0 CSS init. Prefers Compact
// Star (vanilla default); when Compact is locked, picks one of the unlocked
// Kirby-rider CharacterKinds at random.
//
// Excludes CKIND_DEDEDE and CKIND_METAKNIGHT: their riders rely on
// rider-specific HUD assets (`ScInfSpeedd*`, `ScInfHpd*`, etc.) that vanilla's
// 3D HUD loader explicitly skips in Base CT (`zz_8011878c_` and friends short-
// circuit when major==CITY && cityMode==TRIAL). Picking those riders here
// would NULL-deref `3DHud_CreateSpeedometerInner` during scene init. Vanilla
// only uses Dedede/Meta Knight in stadium contexts where the conversion
// happens stadium-side, so excluding them from CT-mode-0 selection matches
// vanilla's own assumption. Compact is always a safe fallback.
CharacterKind GateMachines_GetDefaultCKind()
{
    if (IsCKindUnlocked(CKIND_COMPACT))
        return CKIND_COMPACT;

    int unlocked_count = 0;
    for (int ckind = 0; ckind < CKIND_NUM; ckind++)
    {
        if (ckind == CKIND_DEDEDE || ckind == CKIND_METAKNIGHT)
            continue;
        if (IsCKindUnlocked(ckind))
            unlocked_count++;
    }

    if (unlocked_count == 0)
    {
        OSReport("[GateMachines] GetDefaultCKind: no Kirby-rider machines unlocked, fallback to Compact\n");
        return CKIND_COMPACT;
    }

    int pick = HSD_Randi(unlocked_count);
    for (int ckind = 0; ckind < CKIND_NUM; ckind++)
    {
        if (ckind == CKIND_DEDEDE || ckind == CKIND_METAKNIGHT)
            continue;
        if (IsCKindUnlocked(ckind) && pick-- == 0)
            return ckind;
    }
    return CKIND_COMPACT;
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

    int spawn_table_idx = 0;
    while (match_progress > vc_data_common->spawn_data->spawn_desc[spawn_table_idx].match_progress)
        spawn_table_idx++;

    float spawn_chances[VCKIND_NUM];
    for (int i = 0; i < VCKIND_NUM; i++)
        spawn_chances[i] = vc_data_common->spawn_data->spawn_desc[spawn_table_idx].chance[i];

    // Zero locked machines, give unlocked-but-zero-weight machines a base
    // chance, then force-zero machines that don't naturally spawn in CT.
    for (int i = 0; i < VCKIND_NUM; i++)
    {
        if (CT_SPAWN_EXCLUDED_MASK & (1u << i))
            spawn_chances[i] = 0;
        else if (!(unlocked_mask & (1u << i)))
            spawn_chances[i] = 0;
        else if (spawn_chances[i] == 0)
            spawn_chances[i] = 10;
    }

    int spawnable_count = 0;
    for (int i = 0; i < VCKIND_NUM; i++)
    {
        if (spawn_chances[i] > 0)
            spawnable_count++;
    }

    if (spawnable_count == 0)
        return GetFirstUnlockedMachine();

    // Reduce history size when few machines are spawnable to prevent
    // the only candidate from being excluded by its own history.
    int history_size = (spawnable_count <= 4) ? (spawnable_count - 1) : 4;
    for (int i = 0; i < VCKIND_NUM; i++)
    {
        for (int j = 0; j < history_size; j++)
        {
            if (i == msd->prev_machine_kind[j])
                spawn_chances[i] = 0;
        }
    }

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

// Count unlocked characters for City Trial select screens.
// Replaces the mode 1 (Stadium) and mode 2 (Free Run) counting passes
// in CitySelect_CreateMachineIcons.
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
// Exit to 0x8002f0b8: past the vanilla reorder/balance block. The reorder is
// designed around vanilla's grid iteration (special chars at fixed col 0/9
// positions) — our packed arrays violate that assumption and trigger a
// duplicate-icon bug when only DEDEDE/METAKNIGHT are unlocked. The flat-copy
// at 0x8002f0b8 reads our row_counts + char_arr directly, no reorder needed.
CODEPATCH_HOOKCREATE(0x8002e67c,
    "mr 3, 29\n\t"
    "mr 4, 28\n\t",
    GateMachines_BuildCTSelectArray,
    "",
    0x8002f0b8
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
// Exit to 0x8002f0b8: see the mode-1 hook above for the reorder-bypass rationale.
CODEPATCH_HOOKCREATE(0x8002e738,
    "mr 3, 29\n\t"
    "mr 4, 28\n\t",
    GateMachines_BuildCTSelectArray,
    "",
    0x8002f0b8
)

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
// We replace it with GateMachines_GetDefaultCKind() (Compact if unlocked, else
// random unlocked) so players start on an unlocked machine. r28 = city_select_ply
// + player_offset (callee-saved).
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
// Vanilla signature is Rider_ResetStartingMachine(RiderData *rd, int unk_arg2);
// our replacement only consumes rd — the prologue gating (which uses arg2)
// runs unmodified before this hook point, so we don't need it.
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

    OSReport("[GateMachines] Hooks installed\n");
}

int GateMachines_UnlockMachine(MachineKind kind)
{
    if (kind >= VCKIND_NUM)
        return 0;

    ap_save->machine_unlocked_mask |= (1 << kind);
    OSReport("[GateMachines] Machine %d (%s) unlocked (mask = %s)\n",
             kind, MachineKind_Names[kind], MaskBits(ap_save->machine_unlocked_mask, 32));
    tb_api->EnqueueColoredNoun(NULL, MachineKind_Names[kind], tb_api->MachineColor, NULL);
    return 1;
}

// Give a player the assembled legendary machine via the cinematic.
// machine_index: 0 = Dragoon, 1 = Hydra.
// Returns 1 if started, 0 if in an unsupported mode or no machine to swap from.
// City Trial only: Top Ride has no MachineData / Rider pipeline, and the
// assembly cinematic crashes on Air Ride courses (legendary machines have no
// AR course support — see docs/gate-machines.md).
int GateMachines_GiveLegendaryMachine(int machine_index)
{
    MajorKind major = Scene_GetCurrentMajor();
    if (major != MJRKIND_CITY)
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
        OSReport("[GateMachines] Legendary machine %s assembly started for player %d\n",
                 machine_index == 0 ? "Dragoon" : "Hydra", i);
        given = 1;
    }

    return given;
}
