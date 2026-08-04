#include "game.h"
#include "menu.h"
#include "os.h"
#include "scene.h"
#include "topride.h"
#include "audio.h"
#include "code_patch/code_patch.h"

#include "main.h"
#include "gate_machines.h"
#include "gate_colors.h"
#include "settings_menu.h"
#include "textbox_api.h"
#include "inline.h"

// Machines that don't naturally spawn in CT: Top Ride stars, transformation forms,
// and the Meta Knight / Dedede character forms. All have a 0 base spawn chance, so
// without exclusion the unlocked-but-zero-chance fallback below would leak them
// onto the City Trial field.
#define CT_SPAWN_EXCLUDED_MASK     \
    ((1u << VCKIND_FREE)         | \
     (1u << VCKIND_STEER)        | \
     (1u << VCKIND_WINGKIRBY)    | \
     (1u << VCKIND_WINGMETAKNIGHT) | \
     (1u << VCKIND_WHEELNORMAL)  | \
     (1u << VCKIND_WHEELKIRBY)   | \
     (1u << VCKIND_WHEELDEDEDE)  | \
     (1u << VCKIND_WHEELVSDEDEDE))

// Weight handed to an unlocked machine the vanilla table gives 0 chance, so it can
// still appear on the field. Only these four reach it - every other VCKIND either
// carries a real weight in all three table windows or sits in CT_SPAWN_EXCLUDED_MASK.
// Vanilla per-machine weights run 6-10 out of a ~111-119 table total, so these land
// well under the machines the table actually wants: Compact ~4% of spawns, Flight
// ~1.7%, each legendary ~0.8%.
static float ZeroChanceSpawnWeight(int vckind)
{
    switch (vckind)
    {
    case VCKIND_COMPACT: return 5.0f;
    case VCKIND_FLIGHT:  return 2.0f;
    default:             return 1.0f; // Hydra, Dragoon
    }
}

static int IsCKindUnlocked(CharacterKind ckind)
{
    CharacterDesc *desc = Character_GetDesc(ckind);
    if (!desc)
        return 0;
    MachineKind vckind = CharacterDesc_GetMachineKind(desc);
    return (ap_save->machine_unlocked_mask & (1 << vckind)) ? 1 : 0;
}

// First unlocked City-Trial-spawnable MachineKind, or VCKIND_COMPACT as fallback.
static MachineKind GetFirstUnlockedCTMachine()
{
    u32 mask = ap_save->machine_unlocked_mask;
    for (int i = 0; i < VCKIND_NUM; i++)
    {
        if (CT_SPAWN_EXCLUDED_MASK & (1u << i))
            continue;
        if (mask & (1 << i))
            return i;
    }
    return VCKIND_COMPACT;
}

// Random unlocked Kirby-rider CharacterKind for a City Trial starting machine.
// Excludes CKIND_DEDEDE / CKIND_METAKNIGHT: vanilla's HUD loader skips their
// rider-specific 3D HUD assets in Base CT, so picking them for the free-roam Trial
// start NULL-derefs 3DHud_CreateSpeedometerInner during scene init.
static CharacterKind RandomUnlockedKirbyCKind(void)
{
    int unlocked_count = 0;
    for (int ckind = 0; ckind < CKIND_NUM; ckind++)
    {
        if (ckind == CKIND_DEDEDE || ckind == CKIND_METAKNIGHT)
            continue;
        if (IsCKindUnlocked(ckind))
            unlocked_count++;
    }

    if (unlocked_count == 0)
        return CKIND_COMPACT;

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

// TR's lobby "Control Type" row (Free Star = 0, Steer Star = 1) maps 1:1 to
// MachineKind, so one machine_unlocked_mask covers Air Ride, City Trial, and Top Ride.
static int IsTRMachineUnlocked(TopRideMachineKind tr)
{
    MachineKind vckind = TOPRIDE_MACHINE_TO_VCKIND(tr);
    return (ap_save->machine_unlocked_mask & (1u << vckind)) ? 1 : 0;
}

static TopRideMachineKind GetFirstUnlockedTRMachine()
{
    if (IsTRMachineUnlocked(TR_MACHINE_FREE))
        return TR_MACHINE_FREE;
    if (IsTRMachineUnlocked(TR_MACHINE_STEER))
        return TR_MACHINE_STEER;
    return TR_MACHINE_FREE;
}

static TopRideMachineKind GetRandomUnlockedTRMachine()
{
    TopRideMachineKind unlocked[TR_MACHINE_NUM];
    int count = 0;
    if (IsTRMachineUnlocked(TR_MACHINE_FREE))
        unlocked[count++] = TR_MACHINE_FREE;
    if (IsTRMachineUnlocked(TR_MACHINE_STEER))
        unlocked[count++] = TR_MACHINE_STEER;
    if (count == 0)
        return GetFirstUnlockedTRMachine();
    return unlocked[HSD_Randi(count)];
}

// Post-init fixup for TopRide_InitSelectData (0x8002cfd8), whose per-slot loop
// unconditionally writes panel_machine[slot] = 0 (Free Star). Only the RaceInit site
// (0x8002d748) runs after the panel-kind field is filled, so the other sites see
// non-CPU and fall through to first-unlocked.
void GateMachines_FixupTRInit(u8 *lobby_base)
{
    TopRideMachineKind first = GetFirstUnlockedTRMachine();
    // Relative to lobby base (GameData+0x197): 0x2f = panel_machine[slot], 0x23 = color[slot].
    for (int i = 0; i < 4; i++)
    {
        if (lobby_base[0x1b + i] == 2) // CPU panel
        {
            lobby_base[0x2f + i] = (u8)GetRandomUnlockedTRMachine();
            lobby_base[0x23 + i] = (u8)GateColors_RandomUnlockedColor();
        }
        else
            lobby_base[0x2f + i] = (u8)first;
    }
}

// Hook at 0x8002d070 in TopRide_InitSelectData, just after the per-slot init loop
// (0x8002d06c is already hooked). r31 = lobby base. The three following
// `stb r3, {6,2,3}(r31)` lobby-flag clears rely on r3 = 0, which the C call wipes.
CODEPATCH_HOOKCREATE(0x8002d070,
    "mr 3, 31\n\t",
    GateMachines_FixupTRInit,
    "li 3, 0\n\t",
    0x8002d074
)

// Race-init counterpart. TopRide_RaceInit re-zeros all four panel_machine slots at
// 0x8002d6c4, after InitSelectData's fixup. Hook at 0x8002d748 (`bl gmGetGlobalP`),
// past the panel_pkind CPU-fill loop whose caller-saved iterator r7 rules out landing
// earlier; the re-executed bl restores r3 = GameData*, so no epilogue is needed.
CODEPATCH_HOOKCREATE(0x8002d748,
    "mr 3, 31\n\t",
    GateMachines_FixupTRInit,
    "",
    0
)

// L/R cycler gate for the lobby "Control Type" row: the vanilla 0..1 clamp, minus
// writes onto a locked machine. Both lobby flavors (race TopRide_CSS_PanelThink
// 0x8002b8a8 and solo TopRide_SoloPanelThink 0x8002ca80) carry identical cyclers,
// so one gate serves both hook sites. panel_base[0x2f] = panel_machine[panel];
// input_bits = direction-edge bits (0x80002 = RIGHT, 0x40001 = LEFT).
int GateMachines_CycleTRMachine(u8 *panel_base, u32 input_bits)
{
    u8 current = panel_base[0x2f];
    u8 new_val = current;

    if ((input_bits & 0x80002) != 0)
    {
        if (current < (TR_MACHINE_NUM - 1) && IsTRMachineUnlocked(current + 1))
            new_val = current + 1;
    }
    else if ((input_bits & 0x40001) != 0)
    {
        if (current > 0 && IsTRMachineUnlocked(current - 1))
            new_val = current - 1;
    }

    if (new_val == current)
        return 0;

    panel_base[0x2f] = new_val;
    return 1;
}

// Race-lobby cycler hook at 0x8002be44 in TopRide_CSS_PanelThink, replacing the
// cycler+compare block (..0x8002be94). r26 = panel base, r29 = direction-edge bits.
//   r3 == 0 -> 0x8002c054 (function end); r3 != 0 -> 0x8002be98 (SFX + UI update)
CODEPATCH_HOOKCONDITIONALCREATE(0x8002be44,
    "mr 3, 26\n\t"
    "mr 4, 29\n\t",
    GateMachines_CycleTRMachine,
    "",
    0x8002c054,
    0x8002be98
)

// Solo-lobby (Free Run / Time Attack) cycler hook at 0x8002cb98 in
// TopRide_SoloPanelThink, replacing the cycler+compare through the beq at 0x8002cbec.
// r30 = panel base, r26 = direction-edge bits, both callee-saved so the downstream
// SFX/UI block finds them intact.
//   r3 == 0 -> 0x8002cc18 (function end); r3 != 0 -> 0x8002cbf0 (SFX + UI update)
CODEPATCH_HOOKCONDITIONALCREATE(0x8002cb98,
    "mr 3, 30\n\t"
    "mr 4, 26\n\t",
    GateMachines_CycleTRMachine,
    "",
    0x8002cc18,
    0x8002cbf0
)

// Solo-mode counterpart. TopRide_SoloInit hardcodes all four panel_machine slots to 0
// at 0x8002db70, bypassing InitSelectData. Hook at 0x8002db90 (`add r30, r31, r28`),
// one instruction past the already-hooked `li r28, 0`, so r28 = 0 and r31 = lobby base.
CODEPATCH_HOOKCREATE(0x8002db90,
    "mr 3, 31\n\t",
    GateMachines_FixupTRInit,
    "",
    0
)

// Start-match gate for the TR lobby: with both Free and Steer locked the panel still
// defaults to Free, so Start would launch a machine the player doesn't own. Both hook
// sites reach this only on the Start rising edge, so the buzzer fires once per press.
// Returns 0 = allow start, 1 = block start.
int GateMachines_TRLobbyCanStart(void)
{
    u32 tr_mask = (1u << VCKIND_FREE) | (1u << VCKIND_STEER);
    if (ap_save->machine_unlocked_mask & tr_mask)
        return 0;

    playSoundFX_errorNoise();
    tb_api->EnqueueColoredNoun("Unlock a ", "Top Ride machine", tb_api->MachineColor, " to start!");
    return 1;
}

// Hook at 0x8002c52c in TopRide_PreGameThink, first instruction of the race
// "start match" body (vanilla `bl` menu-confirm sound).
//   r3 == 0 -> run clobbered bl (play sound), fall through to commit+launch
//   r3 != 0 -> jump to 0x8002c878 (next-slot iterator, skip start)
// The gate is non-leaf, so the prologue stashes the caller-saved r4/r5 that the loop
// continuation at 0x8002c878 needs.
CODEPATCH_HOOKCONDITIONALCREATE(0x8002c52c,
    "stwu 1, -16(1)\n\t"
    "stw 4, 8(1)\n\t"
    "stw 5, 12(1)\n\t",
    GateMachines_TRLobbyCanStart,
    "lwz 4, 8(1)\n\t"
    "lwz 5, 12(1)\n\t"
    "addi 1, 1, 16\n\t",
    0,
    0x8002c878
)

// Hook at 0x8002cc80 in TopRide_OnCourseSelect, the solo (Free Run / Time Attack)
// "start match" body; the clobbered instruction is the same `bl` menu-confirm SFX.
//   r3 == 0 -> run clobbered bl (play sound), fall through to commit+launch
//   r3 != 0 -> jump to 0x8002cddc (epilogue, skip start)
CODEPATCH_HOOKCONDITIONALCREATE(0x8002cc80,
    "",
    GateMachines_TRLobbyCanStart,
    "",
    0,
    0x8002cddc
)

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

    for (int i = 0; i < VCKIND_NUM; i++)
    {
        if (CT_SPAWN_EXCLUDED_MASK & (1u << i))
            spawn_chances[i] = 0;
        else if (!(unlocked_mask & (1u << i)))
            spawn_chances[i] = 0;
        else if (spawn_chances[i] == 0)
            spawn_chances[i] = ZeroChanceSpawnWeight(i);
    }

    int spawnable_count = 0;
    for (int i = 0; i < VCKIND_NUM; i++)
    {
        if (spawn_chances[i] > 0)
            spawnable_count++;
    }

    if (spawnable_count == 0)
        return GetFirstUnlockedCTMachine();

    // Shrink history when few machines are spawnable so the only candidate can't be
    // excluded by its own history.
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

    // Vanilla code after the skip target (0x801df220 / 0x801df630) writes r31 to the
    // history buffer.
    return machine_kind;
}

// Replace the spawn selection in CityMachineSpawn_DecideAndSpawn (0x801defac).
// At 0x801df00c: r30 = MachineSpawnData* (-> r3), f1 = match_progress. Result -> r31,
// which feeds the history write and CityMachineSpawn_Create past the skip target.
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

// Replaces the mode 1 (Stadium) and mode 2 (Free Run) counting passes in
// CitySelect_CreateMachineIcons.
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

// Replaces the array-building passes in CitySelect_CreateMachineIcons, packing the
// unlocked characters of the 2x10 icon grid into the two-row locals:
//   char_arr = 20-byte array (row0 at +0, row1 at +10); row_counts = per-row count.
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

// Mode 1 (Stadium) counting pass. Result -> r27 (total count); exit to 0x8002e670,
// past the loop where mode is rechecked before the array-building pass.
CODEPATCH_HOOKCREATE(0x8002e4d0,
    "",
    GateMachines_CountCTSelectAvailable,
    "mr 27, 3\n\t",
    0x8002e670
)

// Mode 1 (Stadium) array-building pass. r29 = char array, r28 = row counts. Exit to
// the flat-copy at 0x8002f0b8, past the vanilla reorder/balance block: that reorder
// assumes vanilla's grid iteration (special chars at fixed col 0/9), and packed arrays
// trigger a duplicate-icon bug when only DEDEDE/METAKNIGHT are unlocked.
CODEPATCH_HOOKCREATE(0x8002e67c,
    "mr 3, 29\n\t"
    "mr 4, 28\n\t",
    GateMachines_BuildCTSelectArray,
    "",
    0x8002f0b8
)

// Mode 2 (Free Run) counting pass. Result -> r27; clobbered `li r24, 0` is harmless
// (r24 is unused after the skipped loop). Same exit as the mode 1 counting pass.
CODEPATCH_HOOKCREATE(0x8002e5c0,
    "",
    GateMachines_CountCTSelectAvailable,
    "mr 27, 3\n\t",
    0x8002e670
)

// Mode 2 (Free Run) array-building pass. r29 = char array, r28 = row counts. Clobbered
// `mr r26, r29` is harmless (the reorder reads from stack). Bypasses the reorder as
// in the mode 1 hook.
CODEPATCH_HOOKCREATE(0x8002e738,
    "mr 3, 29\n\t"
    "mr 4, 28\n\t",
    GateMachines_BuildCTSelectArray,
    "",
    0x8002f0b8
)

// Replaces the respawn machine assignment in Rider_ResetStartingMachine, which
// hardcodes VCKIND_COMPACT.
void GateMachines_ResetStartingMachine(RiderData *rd)
{
    u8 ply = rd->ply;
    MachineKind vckind = rd->starting_machine_idx;

    if (!(ap_save->machine_unlocked_mask & (1 << vckind)))
        vckind = GetFirstUnlockedCTMachine();

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

// CT machine-select slots the player explicitly picked on the grid this session
// (bit = slot). A manual CPU pick suppresses that slot's random-start-machine re-roll.
static u8 ct_machine_manual_pick_mask = 0;

void GateMachines_NoteManualMachinePick(int slot)
{
    if (slot >= 0 && slot < 4)
        ct_machine_manual_pick_mask |= (u8)(1 << slot);
}

// Finalize the City Trial starting machine at the convergence point of
// CitySelect_InitPlayerMachines (0x8002dea0), where the Trial and Stadium / Free Run
// branches merge. Fires once per slot.
//   x215[slot]: 0 = human, 2 = CPU, else inactive.
//   x1d0: 0 = Trial (no machine grid), nonzero = Stadium / Free Run.
void GateMachines_FinalizeCTMachine(int slot)
{
    GameData *gd = Gm_GetGameData();
    if (!gd)
        return;

    // Cleared even on the inactive-slot early return below, so it never leaks into
    // the next match.
    u8 manual_pick = (slot >= 0 && slot < 4) &&
                     (ct_machine_manual_pick_mask & (1 << slot));
    if (slot >= 0 && slot < 4)
        ct_machine_manual_pick_mask &= (u8)~(1 << slot);

    u8 kind = gd->city_select_ply.x215[slot];
    if (kind != 0 && kind != 2)
        return; // inactive slot

    // Independent of the machine toggle; humans keep their CSS color pick.
    if (kind == 2)
        gd->city_select_ply.ply_color[slot] = (u8)GateColors_RandomUnlockedColor();

    if (gd->city_select_ply.x1d0 == 0)
    {
        CharacterKind ck;
        if (ap_menu_settings.ct_random_start_machine)
            ck = RandomUnlockedKirbyCKind();
        else
            ck = IsCKindUnlocked(CKIND_COMPACT) ? CKIND_COMPACT : RandomUnlockedKirbyCKind();
        gd->city_select_ply.ply_icon_ckind[slot] = (u8)ck;
    }
    else if (kind == 2 && !manual_pick && ap_menu_settings.ct_random_start_machine)
    {
        u8 num = gd->city_select_ply.machine_select.num;
        if (num > 0)
            gd->city_select_ply.ply_icon_ckind[slot] =
                gd->city_select_ply.machine_select.c_kind_arr[HSD_Randi(num)];
    }
}

// Hook at the convergence point 0x8002dea0 (`lbz r3, 97(r28)`) in
// CitySelect_InitPlayerMachines. r26 = slot index. Skip target 0 re-executes the
// clobbered lbz, reloading the ckind just written for the Character_GetDesc lookup.
CODEPATCH_HOOKCREATE(0x8002dea0,
    "mr 3, 26\n\t",
    GateMachines_FinalizeCTMachine,
    "",
    0
)

// Hook the icon[slot] store in CitySelect_Cursor1InputThink (0x800315ac,
// `stb r27, 45(r30)`) - the sole player-driven machine-grid pick, since it runs only
// when the chosen grid index changes. r29 = slot.
CODEPATCH_HOOKCREATE(0x800315ac,
    "mr 3, 29\n\t",
    GateMachines_NoteManualMachinePick,
    "",
    0
)

// Hook at 0x801952c8 in Rider_ResetStartingMachine. r31 = RiderData*; replaces the
// two Ply_Set calls (is_bike=0, machine_kind=COMPACT). Skip to 0x801952e0 (epilogue).
CODEPATCH_HOOKCREATE(0x801952c8,
    "mr 3, 31\n\t",
    GateMachines_ResetStartingMachine,
    "",
    0x801952e0
)

// Replaces AirRide_CheckCharacterAvailable (0x8002090c), which decides who appears on
// the Air Ride character select screen from checklist reward indices. Vanilla also
// hardcodes Compact Star, Dragoon, Hydra and Flight Warp Star out of Air Ride whatever
// the save holds; the mask is the only rule here, so an owned machine is selectable in
// every mode whose select screen offers it. The icon archive backs all 20 characters,
// and the CSS reorder already places Dragoon and Hydra at the row ends.
int GateMachines_CheckAirRideCharacterAvailable(CharacterKind ckind)
{
    return IsCKindUnlocked(ckind);
}

// Replaces TitleScreen_CheckMachineUnlocked (0x8000c364), the unlock query for the
// title-screen attract demo's random machine picker (TitleScreen_SelectRandomMachine,
// 0x8000daa0). It does NOT run for CPUs in real Air Ride races, which draw from the
// gated character list in loadCPU.
int GateMachines_CheckTitleDemoMachineUnlocked(s8 machine_class, s8 machine_id)
{
    // machine_class = CharacterDesc.is_bike, machine_id = CharacterDesc.machine_kind,
    // which for bikes is a bike-relative index rather than the VCKIND.
    int vckind;
    if (machine_class)
        vckind = VCKIND_WHEELNORMAL + machine_id;
    else
        vckind = machine_id;

    if (vckind < 0 || vckind >= VCKIND_NUM)
        return 0;

    return (ap_save->machine_unlocked_mask & (1 << vckind)) ? 1 : 0;
}

// Zero the Air Ride CSS available-machine list (airride_select_ply +0x66, a 2x10
// icon grid). AirRide_PopulateSelectIcons rewrites only the first `count` entries each
// frame and never clears the tail, so a mid-session narrowing of machine_unlocked_mask
// leaves stale entries past the new count that the CSS still resolves and commits.
void GateMachines_ClearAirRideList(u8 *base)
{
    for (int i = 0; i < 20; i++)
        base[0x66 + i] = 0;
}

// Hook at 0x80020a88 in AirRide_PopulateSelectIcons (`lbz r0, 123(r31)`), after
// r31 = airride_select_ply is established and before the list is rebuilt. The epilogue
// restores r4 = 0, the function's persistent zero used by the following `stb r4,9(r1)`.
CODEPATCH_HOOKCREATE(0x80020a88,
    "mr 3, 31\n\t",
    GateMachines_ClearAirRideList,
    "li 4, 0\n\t",
    0
)

void GateMachines_OnBoot()
{
    CODEPATCH_HOOKAPPLY(0x801df00c);  // CityMachineSpawn_DecideAndSpawn selection
    CODEPATCH_HOOKAPPLY(0x801df44c);  // cityTrialSpawnFormationStar selection

    CODEPATCH_REPLACEFUNC(AirRide_CheckCharacterAvailable, GateMachines_CheckAirRideCharacterAvailable);
    CODEPATCH_REPLACEFUNC(TitleScreen_CheckMachineUnlocked, GateMachines_CheckTitleDemoMachineUnlocked);

    CODEPATCH_HOOKAPPLY(0x80020a88);  // Air Ride CSS available-machine list clear

    CODEPATCH_HOOKAPPLY(0x8002e4d0);  // CT Stadium (mode 1) counting pass
    CODEPATCH_HOOKAPPLY(0x8002e67c);  // CT Stadium (mode 1) array-building pass
    CODEPATCH_HOOKAPPLY(0x8002e5c0);  // CT Free Run (mode 2) counting pass
    CODEPATCH_HOOKAPPLY(0x8002e738);  // CT Free Run (mode 2) array-building pass

    // CitySelect_Cursor1InputThink splits cursor rows at num>=10 (`cmpwi r3, 9; ble`),
    // but the 2x10 grid renderer keeps up to 10 icons on one line and only wraps at 11,
    // so at num==10 the cursor splits 5+5 across a single drawn row. Vanilla CT only
    // produces counts 15-20; AP gating can land on exactly 10.
    CODEPATCH_REPLACEINSTRUCTION(0x80031350, 0x2c03000a);  // cmpwi r3, 10

    CODEPATCH_HOOKAPPLY(0x8002dea0);  // CT starting-machine finalize
    CODEPATCH_HOOKAPPLY(0x801952c8);  // CT respawn machine validation

    // The TR race and solo (Free Run / Time Attack) lobbies are separate code paths,
    // each with its own init, cycler, and start-match handler.
    CODEPATCH_HOOKAPPLY(0x8002d070);  // TopRide_InitSelectData post-loop fixup (main-menu reset)
    CODEPATCH_HOOKAPPLY(0x8002d748);  // TopRide_RaceInit post-reset fixup (TR Main Game)
    CODEPATCH_HOOKAPPLY(0x8002db90);  // TopRide_SoloInit post-zero fixup (Free Run / Time Attack)
    CODEPATCH_HOOKAPPLY(0x8002be44);  // TopRide_CSS_PanelThink L/R cycler (race lobby)
    CODEPATCH_HOOKAPPLY(0x8002cb98);  // TopRide_SoloPanelThink L/R cycler (Free Run / Time Attack)
    CODEPATCH_HOOKAPPLY(0x8002c52c);  // TopRide_PreGameThink start-match gate (race)
    CODEPATCH_HOOKAPPLY(0x8002cc80);  // TopRide_OnCourseSelect start-match gate (solo)

    OSReport("[GateMachines] Hooks installed\n");
}

int GateMachines_UnlockMachine(MachineKind kind, int announce)
{
    if (kind >= VCKIND_NUM)
        return 0;

    ap_save->machine_unlocked_mask |= (1 << kind);
    OSReport("[GateMachines] Machine %d (%s) unlocked (mask = %s)\n",
             kind, MachineKind_Names[kind], MaskBits(ap_save->machine_unlocked_mask, 32));
    if (announce)
    {
        // VCKIND_WHEELDEDEDE / VCKIND_WINGMETAKNIGHT are the player-facing King Dedede
        // / Meta Knight unlocks, announced to match the checklist reward path.
        const char *prefix = "Unlocked Machine: ";
        const char *name   = MachineKind_Names[kind];
        if (kind == VCKIND_WHEELDEDEDE)
        {
            prefix = "Unlocked Character: ";
            name   = "King Dedede";
        }
        else if (kind == VCKIND_WINGMETAKNIGHT)
        {
            prefix = "Unlocked Character: ";
            name   = "Meta Knight";
        }
        tb_api->EnqueueColoredNoun(prefix, name, tb_api->MachineColor, NULL);
    }
    return 1;
}

// Legendary machines assembled in the current City Trial scene: bit 0 = Dragoon,
// bit 1 = Hydra. The piece archives (VsDragoon.dat / VsHydra.dat) are freed when the
// assembly cinematic finishes, so a second cinematic in the same scene loads a
// dangling joint and crashes in HSD_JObjLoadJoint.
static u8 legendary_assembled_mask;

void GateMachines_On3DLoadEnd(void)
{
    legendary_assembled_mask = 0;
}

// Give a player the assembled legendary machine via the cinematic. machine_index:
// 0 = Dragoon, 1 = Hydra. Returns 1 if started (consume the item), 0 if it can't run
// yet (keep queued and retry).
//
// The cinematic loads legendary piece models and drives the CT sky/area-light setup,
// which only exist on the open City Trial map - running it in a stadium or AR/TR
// dereferences a null jobj or hits the area-light assert.
int GateMachines_GiveLegendaryMachine(int machine_index)
{
    if (!Gm_IsInCity())
        return 0;

    u8 bit = (u8)(1 << machine_index);
    if (legendary_assembled_mask & bit)
        return 0;

    // A second concurrent cinematic (GObj at GameData+0xA8C) tears down the running
    // one's piece GObjs and leaves a dangling jobj that crashes on the next update.
    if (Gm_IsLegendaryAssembling())
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

    if (given)
        legendary_assembled_mask |= bit;

    return given;
}
