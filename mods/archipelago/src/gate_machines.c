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

// Machines that don't naturally spawn in CT: Top Ride stars, transformation
// forms, and the Meta Knight / Dedede character forms (VCKIND_WINGMETAKNIGHT /
// VCKIND_WHEELDEDEDE, whose bits are set by those character unlocks). All have a
// 0 base spawn chance, so without exclusion the unlocked-but-zero-chance fallback
// (weight 10) below would leak them onto the City Trial field.
#define CT_SPAWN_EXCLUDED_MASK     \
    ((1u << VCKIND_FREE)         | \
     (1u << VCKIND_STEER)        | \
     (1u << VCKIND_WINGKIRBY)    | \
     (1u << VCKIND_WINGMETAKNIGHT) | \
     (1u << VCKIND_WHEELNORMAL)  | \
     (1u << VCKIND_WHEELKIRBY)   | \
     (1u << VCKIND_WHEELDEDEDE)  | \
     (1u << VCKIND_WHEELVSDEDEDE))

static int IsCKindUnlocked(CharacterKind ckind)
{
    CharacterDesc *desc = Character_GetDesc(ckind);
    if (!desc)
        return 0;
    MachineKind vckind = CharacterDesc_GetMachineKind(desc);
    return (ap_save->machine_unlocked_mask & (1 << vckind)) ? 1 : 0;
}

// First unlocked City-Trial-spawnable MachineKind, or VCKIND_COMPACT as absolute
// fallback. Skips CT_SPAWN_EXCLUDED_MASK so a sparse unlock (e.g. only the Top
// Ride Free/Steer Star) never falls back to a TR-only or transform machine.
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

// Pick a random unlocked Kirby-rider CharacterKind for a City Trial starting
// machine, excluding CKIND_DEDEDE / CKIND_METAKNIGHT: their riders need
// rider-specific 3D HUD assets that vanilla's HUD loader skips in Base CT, so
// picking them for the free-roam Trial start NULL-derefs
// 3DHud_CreateSpeedometerInner during scene init. Compact is a safe fallback.
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

// TR's lobby "Control Type" row exposes Free Star (TR_MACHINE_FREE = 0) and Steer
// Star (TR_MACHINE_STEER = 1). Both map 1:1 to MachineKind via
// TOPRIDE_MACHINE_TO_VCKIND, so a single machine_unlocked_mask covers Air Ride,
// City Trial, and Top Ride.
static int IsTRMachineUnlocked(TopRideMachineKind tr)
{
    MachineKind vckind = TOPRIDE_MACHINE_TO_VCKIND(tr);
    return (ap_save->machine_unlocked_mask & (1u << vckind)) ? 1 : 0;
}

// Return the first unlocked TR machine, or TR_MACHINE_FREE as a defensive
// fallback (the player cannot enter TR mode at all if both are locked, so
// this only fires when the menu has not yet been gated upstream).
static TopRideMachineKind GetFirstUnlockedTRMachine()
{
    if (IsTRMachineUnlocked(TR_MACHINE_FREE))
        return TR_MACHINE_FREE;
    if (IsTRMachineUnlocked(TR_MACHINE_STEER))
        return TR_MACHINE_STEER;
    return TR_MACHINE_FREE;
}

// Pick a random unlocked TR control type (Free / Steer) for a CPU panel.
// Falls back to GetFirstUnlockedTRMachine when nothing qualifies.
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
// unconditionally writes panel_machine[slot] = 0 (Free Star). CPU panels get a
// random unlocked control type (so CPUs don't all share Free Star) plus a random
// unlocked color; human panels get the first unlocked machine (a human's L/R pick
// is gated separately). Panel kind is at lobby_base[0x1b + slot] (2 = CPU); only
// the RaceInit site (0x8002d748) runs after that field is filled, so the other
// sites see non-CPU and fall through to first-unlocked.
void GateMachines_FixupTRInit(u8 *lobby_base)
{
    TopRideMachineKind first = GetFirstUnlockedTRMachine();
    // Relative to lobby base (GameData+0x197): 0x2f = panel_machine[slot],
    // 0x23 = color[slot].
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
// (0x8002d06c is claimed by gate_colors.c). r31 = lobby base. The three following
// `stb r3, {6,2,3}(r31)` lobby-flag clears rely on r3 = 0, which the C call
// wipes, so the epilogue restores `li 3, 0`.
CODEPATCH_HOOKCREATE(0x8002d070,
    "mr 3, 31\n\t",
    GateMachines_FixupTRInit,
    "li 3, 0\n\t",
    0x8002d074
)

// Race-init counterpart (TR Main Game / multiplayer race). TopRide_RaceInit has
// its own panel_machine reset block (0x8002d6c4) that re-zeros all four slots
// after InitSelectData's fixup, so without a parallel fixup the lobby panel
// starts on a possibly-locked Free Star.
//
// Hook at 0x8002d748 (`bl gmGetGlobalP`), just past the panel_pkind CPU-fill loop
// (whose caller-saved iterator r7 rules out landing earlier). The framework's
// re-execution of that bl restores r3 = GameData* for the following addi, so no
// epilogue is needed.
CODEPATCH_HOOKCREATE(0x8002d748,
    "mr 3, 31\n\t",
    GateMachines_FixupTRInit,
    "",
    0
)

// L/R cycler gate for the lobby "Control Type" row. Vanilla cycles
// panel_machine[panel] between 0 (Free) and 1 (Steer) unconditionally; we apply
// the same 0..1 clamp but skip writes onto a locked machine. Both lobby flavors
// (race TopRide_CSS_PanelThink 0x8002b8a8 and solo TopRide_SoloPanelThink
// 0x8002ca80) carry identical cyclers, so one gate serves both hook sites.
//   panel_base[0x2f] = panel_machine[panel]; input_bits = direction-edge bits
//   (0x80002 = RIGHT, 0x40001 = LEFT). Returns 1 if changed (caller plays SFX +
//   updates the icon), 0 if not (caller skips to function end).
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
// "Control Type" cycler+compare block (..0x8002be94). The outer guard already
// filtered no-L/R frames; r26 = panel base, r29 = direction-edge bits.
//   r3 == 0 -> skip to 0x8002c054 (function end)
//   r3 != 0 -> fall through to 0x8002be98 (SFX + UI update)
CODEPATCH_HOOKCONDITIONALCREATE(0x8002be44,
    "mr 3, 26\n\t"
    "mr 4, 29\n\t",
    GateMachines_CycleTRMachine,
    "",
    0x8002c054,
    0x8002be98
)

// Solo-lobby cycler hook for Free Run / Time Attack. TopRide_SoloPanelThink
// (0x8002ca80) carries its own ungated "Control Type" cycler that the race hook
// above doesn't cover; without this, solo lets the player L/R onto a locked
// machine and launch it (the start gate only checks that *some* TR machine is
// unlocked). Hook at 0x8002cb98, replacing the cycler+compare through the
// `beq 0x8002cc18` at 0x8002cbec; r30 = panel base (panel_machine at +0x2f),
// r26 = direction-edge bits (both callee-saved, so the downstream SFX/UI block
// finds them intact).
//   r3 == 0 -> exit to 0x8002cc18 (function end)
//   r3 != 0 -> fall through to 0x8002cbf0 (SFX + UI update)
CODEPATCH_HOOKCONDITIONALCREATE(0x8002cb98,
    "mr 3, 30\n\t"
    "mr 4, 26\n\t",
    GateMachines_CycleTRMachine,
    "",
    0x8002cc18,
    0x8002cbf0
)

// Solo-mode counterpart to GateMachines_FixupTRInit. TopRide_SoloInit (Free Run
// / Time Attack) hardcodes all four panel_machine slots to 0 (Free Star) at
// 0x8002db70, bypassing InitSelectData, so without a fixup the panel starts on
// Free even when locked. Hook at 0x8002db90 (`add r30, r31, r28`), one
// instruction after the `li r28, 0` claimed by gate_colors's TR Solo color
// fixup, so it fires after that fixup with r28 = 0 and r31 = lobby base.
CODEPATCH_HOOKCREATE(0x8002db90,
    "mr 3, 31\n\t",
    GateMachines_FixupTRInit,
    "",
    0
)

// Start-match gate for the TR lobby. The L/R cyclers stop the player moving onto
// a locked machine, but with both Free and Steer locked the panel still defaults
// to Free and Start would launch a machine the player doesn't own. This refuses
// the launch and, on the block path, plays the "denied" buzzer + a textbox. Both
// start-gate hook sites reach this only on the Start rising edge, so feedback
// fires once per press. Returns 0 = allow start, 1 = block start.
int GateMachines_TRLobbyCanStart(void)
{
    u32 tr_mask = (1u << VCKIND_FREE) | (1u << VCKIND_STEER);
    if (ap_save->machine_unlocked_mask & tr_mask)
        return 0;

    playSoundFX_errorNoise();
    tb_api->EnqueueColoredNoun("Unlock a ", "Top Ride machine", tb_api->MachineColor, " to start!");
    return 1;
}

// Hook at 0x8002c52c in TopRide_PreGameThink, first instruction of the
// multiplayer-race "start match" body (vanilla `bl` menu-confirm sound). The
// preceding Start-bit and Ready-panel tests already gate the site; we just add
// the "some TR machine is unlocked" requirement.
//   r3 == 0 -> run clobbered bl (play sound), fall through to commit+launch
//   r3 != 0 -> jump to 0x8002c878 (next-slot iterator, skip start)
// The gate calls SFX/textbox helpers (non-leaf), clobbering the caller-saved
// r4/r5 the loop continuation at 0x8002c878 needs, so the prologue stashes them
// and the epilogue restores them.
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

// Hook at 0x8002cc80 in TopRide_OnCourseSelect, the solo-mode (Free Run / Time
// Attack) "start match" body. Preceding is_all_ready and Start-bit tests gate
// the site; the clobbered instruction is the same `bl` menu-confirm SFX.
//   r3 == 0 -> run clobbered bl (play sound), fall through to commit+launch
//   r3 != 0 -> jump to 0x8002cddc (epilogue, skip start)
CODEPATCH_HOOKCONDITIONALCREATE(0x8002cc80,
    "",
    GateMachines_TRLobbyCanStart,
    "",
    0,
    0x8002cddc
)

// Replace the vanilla spawn selection entirely: zero locked machines, give
// unlocked-but-zero-chance machines a minimum weight, shrink history for low
// unlock counts, then weighted-random.
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
        return GetFirstUnlockedCTMachine();

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

    // The vanilla code after our skip target (0x801df220 / 0x801df630) writes
    // r31 to the history buffer.
    return machine_kind;
}

// Replace the spawn selection in CityMachineSpawn_DecideAndSpawn (0x801defac).
// At 0x801df00c: r30 = MachineSpawnData* (-> r3), f1 = match_progress (float arg).
// Result -> r31; skip to 0x801df220, past vanilla selection where r31 feeds the
// history write and CityMachineSpawn_Create.
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

// Build the filtered character array for City Trial select screens (mode 1
// Stadium / mode 2 Free Run) in CitySelect_CreateMachineIcons. Iterates the 2x10
// icon grid, writing only unlocked characters into the two-row locals:
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

// Hook at 0x8002e4d0: mode 1 (Stadium) counting pass in CitySelect_CreateMachineIcons.
// Replaces the counting loop; result -> r27 (total count). Exit to 0x8002e670,
// past the loop where mode is rechecked before the array-building pass.
CODEPATCH_HOOKCREATE(0x8002e4d0,
    "",
    GateMachines_CountCTSelectAvailable,
    "mr 27, 3\n\t",
    0x8002e670
)

// Hook at 0x8002e67c: mode 1 (Stadium) array-building pass. r29 = char array,
// r28 = row counts. Exit to 0x8002f0b8, past the vanilla reorder/balance block:
// the reorder assumes vanilla's grid iteration (special chars at fixed col 0/9),
// and our packed arrays trigger a duplicate-icon bug when only DEDEDE/METAKNIGHT
// are unlocked. The flat-copy at 0x8002f0b8 reads our arrays directly.
CODEPATCH_HOOKCREATE(0x8002e67c,
    "mr 3, 29\n\t"
    "mr 4, 28\n\t",
    GateMachines_BuildCTSelectArray,
    "",
    0x8002f0b8
)

// Hook at 0x8002e5c0: mode 2 (Free Run) counting pass in CitySelect_CreateMachineIcons.
// Replaces the counting loop; result -> r27 (total count). Clobbered `li r24, 0`
// is harmless (r24 unused after the skipped loop). Exit to 0x8002e670, past the
// loop where mode is rechecked before the array-building pass.
CODEPATCH_HOOKCREATE(0x8002e5c0,
    "",
    GateMachines_CountCTSelectAvailable,
    "mr 27, 3\n\t",
    0x8002e670
)

// Hook at 0x8002e738: mode 2 (Free Run) array-building pass. r29 = char array,
// r28 = row counts. Clobbered `mr r26, r29` is harmless (the reorder reads from
// stack). Exit to 0x8002f0b8, bypassing the reorder as in the mode-1 hook.
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

// Per-slot bitmask of CT machine-select slots the player explicitly picked on
// the grid this session (bit = slot). Set by the icon[slot] write in
// CitySelect_Cursor1InputThink, consumed and cleared per slot in
// GateMachines_FinalizeCTMachine (a manual CPU pick suppresses that slot's
// random-start-machine re-roll).
static u8 ct_machine_manual_pick_mask = 0;

// Record that the player explicitly chose a machine for a CT select slot via the
// grid. Called from the icon[slot] write in CitySelect_Cursor1InputThink, the
// sole player-driven machine-grid pick.
void GateMachines_NoteManualMachinePick(int slot)
{
    if (slot >= 0 && slot < 4)
        ct_machine_manual_pick_mask |= (u8)(1 << slot);
}

// Finalize the City Trial starting machine at the convergence point of
// CitySelect_InitPlayerMachines (0x8002dea0), where the Trial and Stadium / Free
// Run branches merge. Fires once per slot. The "Random Start Machine" toggle
// applies to humans and CPUs wherever neither makes an explicit grid pick:
//   x215[slot]: 0 = human, 2 = CPU, else inactive.
//   Trial (x1d0 == 0): no grid, so the toggle drives every active slot -
//     ON = random unlocked Kirby machine; OFF = Compact if unlocked, else random.
//   Stadium / Free Run (x1d0 != 0): humans keep their grid pick; CPU machines
//     follow the toggle (ON = random from the gated c_kind_arr; OFF = vanilla
//     seed), except a CPU flagged in ct_machine_manual_pick_mask keeps its pick.
void GateMachines_FinalizeCTMachine(int slot)
{
    GameData *gd = Gm_GetGameData();
    if (!gd)
        return;

    // Consume this slot's manual-pick flag for the run (cleared even on the
    // inactive-slot early return below, so it never leaks into the next match).
    u8 manual_pick = (slot >= 0 && slot < 4) &&
                     (ct_machine_manual_pick_mask & (1 << slot));
    if (slot >= 0 && slot < 4)
        ct_machine_manual_pick_mask &= (u8)~(1 << slot);

    u8 kind = gd->city_select_ply.x215[slot];
    if (kind != 0 && kind != 2)
        return; // inactive slot

    // CPUs get a random unlocked color (humans keep their CSS color pick). This
    // is independent of the machine toggle - it always applies to CPU slots.
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
// CitySelect_InitPlayerMachines. r26 = slot index (passed in), r28 = city_select_ply
// + slot. Skip target 0 re-executes the clobbered `lbz r3, 97(r28)`, reloading the
// ckind we just wrote before the following Character_GetDesc lookup.
CODEPATCH_HOOKCREATE(0x8002dea0,
    "mr 3, 26\n\t",
    GateMachines_FinalizeCTMachine,
    "",
    0
)

// Hook the icon[slot] store in CitySelect_Cursor1InputThink (0x800315ac,
// `stb r27, 45(r30)`) - the sole player-driven machine-grid pick (runs only when
// the chosen grid index changes). r29 = slot (passed in). Flags the slot as a
// manual pick; skip target 0 re-executes the clobbered store to commit icon[slot].
CODEPATCH_HOOKCREATE(0x800315ac,
    "mr 3, 29\n\t",
    GateMachines_NoteManualMachinePick,
    "",
    0
)

// Hook at 0x801952c8 in Rider_ResetStartingMachine. r31 = RiderData*, replacing
// the two Ply_Set calls (is_bike=0, machine_kind=COMPACT) with our validated
// selection. Skip to 0x801952e0 (epilogue).
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

// Replace TitleScreen_CheckMachineUnlocked (0x8000c364). This is the machine
// unlock query for the title-screen attract demo's random machine picker
// (TitleScreen_SelectRandomMachine, 0x8000daa0) - it does NOT run for CPUs in
// real Air Ride races (those draw from the gated character list in loadCPU).
// Gating it keeps the idle demo from showing locked machines. The second
// parameter (machine_id) is the MachineKind.
int GateMachines_CheckTitleDemoMachineUnlocked(s8 machine_class, s8 machine_id)
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

// Zero the Air Ride CSS available-machine list (airride_select_ply +0x66, a
// 2x10 = 20-entry icon grid). AirRide_PopulateSelectIcons rewrites only the first
// `count` entries each frame and never clears the tail, so when
// machine_unlocked_mask narrows mid-session (e.g. a debug lock) stale entries
// linger past the new count and the CSS resolves them (icon index defaults to 0)
// to a now-locked machine. Zeroing first makes any entry past the live count read
// CKIND_COMPACT (0); the per-frame rebuild then self-heals. base = airride_select_ply.
void GateMachines_ClearAirRideList(u8 *base)
{
    for (int i = 0; i < 20; i++)
        base[0x66 + i] = 0;
}

// Hook at 0x80020a88 in AirRide_PopulateSelectIcons (`lbz r0, 123(r31)`), after
// r31 = airride_select_ply is established and before the list is rebuilt. The
// re-executed `lbz r0,123(r31)` reloads r0; the epilogue restores r4 = 0 (the
// function's persistent zero, used by the `stb r4,9(r1)` immediately after).
CODEPATCH_HOOKCREATE(0x80020a88,
    "mr 3, 31\n\t",
    GateMachines_ClearAirRideList,
    "li 4, 0\n\t",
    0
)

void GateMachines_OnBoot()
{
    // City Trial spawn hooks
    CODEPATCH_HOOKAPPLY(0x801df00c);
    CODEPATCH_HOOKAPPLY(0x801df44c);

    // Air Ride select screen: replace character availability check
    CODEPATCH_REPLACEFUNC(AirRide_CheckCharacterAvailable, GateMachines_CheckAirRideCharacterAvailable);

    // Title-screen attract demo: gate the random machine picker so the idle demo
    // never shows a locked machine. (Real Air Ride CPU selection is gated upstream
    // via the character list in loadCPU.)
    CODEPATCH_REPLACEFUNC(TitleScreen_CheckMachineUnlocked, GateMachines_CheckTitleDemoMachineUnlocked);

    // Air Ride CSS: clear the cached available-machine list each frame so a
    // narrowed unlock mask can't leave stale, now-locked entries.
    CODEPATCH_HOOKAPPLY(0x80020a88);

    // City Trial Stadium select screen: replace mode 1's counting and
    // array-building passes in CitySelect_CreateMachineIcons (0x8002e3c4).
    // Vanilla mode 1 only checks ckind ranges, no unlock mask.
    CODEPATCH_HOOKAPPLY(0x8002e4d0);  // mode 1 counting pass
    CODEPATCH_HOOKAPPLY(0x8002e67c);  // mode 1 array-building pass

    // City Trial Free Run select screen: same treatment for mode 2.
    CODEPATCH_HOOKAPPLY(0x8002e5c0);  // mode 2 counting pass
    CODEPATCH_HOOKAPPLY(0x8002e738);  // mode 2 array-building pass

    // City Trial machine-select navigation off-by-one (Free Run + Stadium).
    // CitySelect_Cursor1InputThink (0x800312fc) splits cursor rows at num>=10
    // (`cmpwi r3, 9; ble` at 0x80031350), but the 2x10 grid renderer keeps up to
    // 10 icons on a single line and only wraps at 11 - so at num==10 the cursor
    // splits 5+5 while one row is drawn. Vanilla CT only makes counts 15-20, so
    // this was never hit; AP gating can land on exactly 10. Patch to `cmpwi r3, 10`.
    CODEPATCH_REPLACEINSTRUCTION(0x80031350, 0x2c03000a);  // cmpwi r3, 10

    // City Trial starting machine: finalize each active slot's machine at the
    // CSS convergence point per the Random Start Machine toggle (Trial: humans
    // and CPUs alike; Stadium / Free Run: CPUs only, humans keep their pick).
    CODEPATCH_HOOKAPPLY(0x8002dea0);

    // Respawn machine validation: use starting machine instead of hardcoded Compact
    CODEPATCH_HOOKAPPLY(0x801952c8);

    // Top Ride lobby: gate the L/R "Control Type" cycler and override the
    // panel_machine init default so locked Free/Steer is never shown. The race
    // and solo (Free Run / Time Attack) lobbies are separate code paths, each
    // with its own init, cycler, and start-match handler - hence the per-site hooks.
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
        // VCKIND_WHEELDEDEDE / VCKIND_WINGMETAKNIGHT are the player-facing King
        // Dedede / Meta Knight unlocks - announce them as "Unlocked Character:"
        // to match the REWARD_KING_DEDEDE / REWARD_META_KNIGHT checklist path.
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

// Set when a legendary machine has been assembled in the current City Trial
// scene: bit 0 = Dragoon, bit 1 = Hydra. The piece archives (VsDragoon.dat /
// VsHydra.dat) are freed when the assembly cinematic finishes, so a second
// cinematic in the same scene loads a dangling joint and crashes in
// HSD_JObjLoadJoint. Reset per 3D scene load via GateMachines_On3DLoadEnd.
static u8 legendary_assembled_mask;

void GateMachines_On3DLoadEnd(void)
{
    legendary_assembled_mask = 0;
}

// Give a player the assembled legendary machine via the cinematic.
// machine_index: 0 = Dragoon, 1 = Hydra. Returns 1 if started (consume the item),
// 0 if it can't run yet (keep queued and retry).
//
// The cinematic loads legendary piece models and drives the CT sky/area-light
// setup, which only exist on the open City Trial map - running it in any stadium
// or AR/TR dereferences a null jobj / hits the area-light assert and crashes.
// Gm_IsInCity() (stage_kind 9/52) excludes those, so returning 0 keeps the item
// queued until the player is back on the open map.
int GateMachines_GiveLegendaryMachine(int machine_index)
{
    if (!Gm_IsInCity())
        return 0;

    // Already assembled this scene: the piece archive is freed, so re-running the
    // cinematic would crash. Keep the item queued until the next scene load.
    u8 bit = (u8)(1 << machine_index);
    if (legendary_assembled_mask & bit)
        return 0;

    // A legendary assembly cinematic is already running (GObj at GameData+0xA8C).
    // Starting a second tears down its piece GObjs and leaves a dangling jobj that
    // crashes on the next update. Wait for it to finish.
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
