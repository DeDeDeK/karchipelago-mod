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
// forms, and the Dedede / Meta Knight character forms. Force these to 0 chance
// regardless of mask.
//
// VCKIND_WINGMETAKNIGHT and VCKIND_WHEELDEDEDE are the machine forms behind the
// Meta Knight and King Dedede character unlocks: unlocking either sets its bit
// in machine_unlocked_mask. Both have a 0 base spawn chance in vanilla, so
// without exclusion the unlocked-but-zero-chance fallback (weight 10) below
// would leak these debug character machines onto the City Trial field.
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

// Get the first unlocked City-Trial-spawnable MachineKind, or VCKIND_COMPACT as
// absolute fallback. Skips CT_SPAWN_EXCLUDED_MASK machines (Free/Steer Star,
// transformation forms, debug wheelie kinds) so a sparse unlock state - e.g.
// only the Top Ride Free/Steer Star unlocked - never falls back to spawning a
// TR-only or transform machine on the City Trial field.
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
// machine, excluding CKIND_DEDEDE / CKIND_METAKNIGHT.
//
// Those two are excluded because their riders rely on rider-specific HUD assets
// (`ScInfSpeedd*`, `ScInfHpd*`, etc.) that vanilla's 3D HUD loader explicitly
// skips in Base CT (`zz_8011878c_` and friends short-circuit when major==CITY &&
// cityMode==TRIAL). Picking those riders for the free-roam Trial start would
// NULL-deref `3DHud_CreateSpeedometerInner` during scene init. Vanilla only uses
// Dedede / Meta Knight in stadium contexts where the conversion happens
// stadium-side. Compact is always a safe fallback when nothing else is unlocked.
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

// ===== Top Ride machine gating =====
//
// TR's lobby panel exposes two machines on its middle ("Control Type") row:
// Free Star (TR_MACHINE_FREE = 0) and Steer Star (TR_MACHINE_STEER = 1). The
// committed value lives at GameData.topride_config.slots[slot].machine_kind; the
// pre-confirmation lobby value lives at GameData.topride_select_ply.panel_machine[slot].
// Both map 1:1 to MachineKind via TOPRIDE_MACHINE_TO_VCKIND, so a single
// machine_unlocked_mask covers Air Ride, City Trial, and Top Ride.
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

// Post-init fixup for TopRide_InitSelectData (0x8002cfd8). Vanilla's per-slot
// init loop unconditionally writes panel_machine[slot] = 0 (Free Star). Walk
// the 4 panels and set each one's default: CPU panels get a random unlocked
// control type so the CPUs don't all share Free Star, while human panels get
// the first unlocked machine (a human's own L/R pick is gated separately). This
// keeps the lobby from ever starting on a locked option.
//
// Panel kind lives at lobby_base[0x1b + slot] (GameData+0x1b2 panel_pkind:
// 0=open, 1=HMN, 2=CPU, 3=OFF). Only the RaceInit hook site (0x8002d748) runs
// after that field is filled, so the CPU branch only meaningfully fires there;
// the InitSelectData / SoloInit sites see non-CPU kinds and fall through to
// first-unlocked, unchanged from before. RaceInit also runs after the TR color
// validator (0x8002d704), so the CPU color set below is the final value.
void GateMachines_FixupTRInit(u8 *lobby_base)
{
    TopRideMachineKind first = GetFirstUnlockedTRMachine();
    // 0x2f = offsetof(topride_select_ply, panel_machine) - 0x37 (lobby base
    // is GameData+0x197, not GameData+0x160). color[] is at +0x23 (0x1ba). See game.h.
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

// Hook at 0x8002d070 in TopRide_InitSelectData, immediately after the per-slot
// init loop finishes. (0x8002d06c, the first post-loop instruction, is already
// claimed by gate_colors.c.) At entry: r31 = lobby base (GameData + 0x197,
// callee-saved). Vanilla post-loop relies on r3 = 0 (set by `li r3, 0` at
// 0x8002d06c) for the three `stb r3, {6,2,3}(r31)` lobby-flag clears that
// follow our hook site. r3 is caller-saved so the C call wipes it - the
// epilogue must restore r3 = 0 before those stores execute, otherwise the
// lobby's active_pad_mask / x199 / x19a get written with garbage and the
// panel UI fails to render until the next scene entry. The clobbered
// `li r0, 1` is re-executed automatically by the hook framework.
CODEPATCH_HOOKCREATE(0x8002d070,
    "mr 3, 31\n\t",
    GateMachines_FixupTRInit,
    "li 3, 0\n\t",
    0x8002d074
)

// Race-init counterpart to GateMachines_FixupTRInit. TopRide_RaceInit (the
// lobby init dispatched from TopRide_LobbyInit when TopRide_GetMode() == 0,
// i.e. TR Main Game / multiplayer race) has its own panel_machine reset
// block at 0x8002d6c4..0x8002d700 that overwrites all four slots with 0
// (Free Star), after InitSelectData's earlier fixup. Without a parallel
// hook here, entering TR Main Game with Free locked leaves the lobby panel
// pointing at a locked Free Star icon as its current selection until the
// player L/R-cycles off it. The reset block is conditional (a `beq` at
// 0x8002d6a4 can skip it when the slot's player_kind matches the iterator),
// so the bug only manifests on slots that hit the reset path - but the
// SoloInit fixup at 0x8002db90 already covers the same logic, so closing
// the asymmetry here restores parity with TR Main Game.
//
// Hook at 0x8002d748 (`bl 0x80006c14` = `bl gmGetGlobalP`), the first
// instruction after the panel_pkind CPU-fill loop at 0x8002d710..0x8002d744
// ends. We can't land any earlier inside that loop because the iterator
// r7 is caller-saved and our C call would clobber it. By landing on the
// post-loop bl that re-fetches GameData, the framework's auto-re-execution
// of the bl naturally restores r3 = GameData* for the addi at 0x8002d74c -
// no epilogue needed. Nothing between 0x8002d6c4 (the panel_machine reset)
// and our hook reads panel_machine, so the fixup window is intact.
CODEPATCH_HOOKCREATE(0x8002d748,
    "mr 3, 31\n\t",
    GateMachines_FixupTRInit,
    "",
    0
)

// L/R cycler gate for the lobby "Control Type" row. Vanilla cycles
// panel_machine[panel] between 0 (Free) and 1 (Steer) unconditionally when the
// sub-cursor is on the middle row and an L/R bit is present. Our replacement
// applies the same 0..1 clamp, but also skips writes that would land on a
// locked machine.
//
// Generic over both lobby flavors - the race lobby (TopRide_CSS_PanelThink,
// 0x8002b8a8) and the solo Free Run / Time Attack lobby (TopRide_SoloPanelThink,
// 0x8002ca80) each carry their own copy of this cycler with identical RIGHT
// (0x80002) / LEFT (0x40001) edge bits and the same panel_machine offset (0x2f),
// so a single gate function serves both hook sites.
//
// Inputs (set by each hook's asm prologue from the regs live at that site):
//   panel_base = lobby base + panel - panel_base[0x2f] = panel_machine[panel]
//   input_bits = controller direction-edge bits (0x80002 = RIGHT, 0x40001 = LEFT)
// Return: 1 if the value changed (caller plays SFX + updates the icon UI),
//         0 if the value did not change (caller skips straight to function end).
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

// Race-lobby cycler hook at 0x8002be44 in TopRide_CSS_PanelThink, the start of
// the "Control Type" L/R cycler block. By this point the outer guard at
// 0x8002be2c has already filtered out frames with no L/R bits set, and
// r26 = panel base (lobby + slot), r29 = direction-edge bits. The hook
// replaces the entire cycler+compare block (0x8002be44..0x8002be94):
//   r3 == 0 → no change, skip to 0x8002c054 (function end)
//   r3 != 0 → change, fall through to 0x8002be98 (SFX + UI update)
CODEPATCH_HOOKCONDITIONALCREATE(0x8002be44,
    "mr 3, 26\n\t"
    "mr 4, 29\n\t",
    GateMachines_CycleTRMachine,
    "",
    0x8002c054,
    0x8002be98
)

// Solo-lobby cycler hook for Free Run / Time Attack. TopRide_SoloPanelThink
// (0x8002ca80, dispatched from TopRide_OnCourseSelect for ply_state != 1) is
// the solo counterpart to TopRide_CSS_PanelThink and carries its OWN ungated
// "Control Type" cycler at 0x8002cb88..0x8002cbec. Solo never routes through
// TopRide_CSS_PanelThink, so the race cycler hook above does not cover it -
// without this hook, Free Run / Time Attack lets the player L/R straight onto
// a locked machine (and then launch it, since the start gate only checks that
// *some* TR machine is unlocked).
//
// Hook at 0x8002cb98 (`and. r0, r26, r0`, the RIGHT-bit test). This lands
// AFTER the outer 0xC0003 L/R guard at 0x8002cb80 and AFTER the cycler sets up
// r29 = panel index and r30 = lobby + panel (0x8002cb8c / 0x8002cb94), so the
// downstream SFX + UI block at 0x8002cbf0 finds those callee-saved regs intact
// across our C call - no epilogue needed. The replaced span runs through the
// post-write compare `beq 0x8002cc18` at 0x8002cbec:
//   r3 == 0 → no change, exit to 0x8002cc18 (function end)
//   r3 != 0 → change, fall through to 0x8002cbf0 (SFX + UI update)
// At entry r30 = lobby + panel (panel base, panel_machine at +0x2f) and
// r26 = direction-edge bits, matching GateMachines_CycleTRMachine's contract.
CODEPATCH_HOOKCONDITIONALCREATE(0x8002cb98,
    "mr 3, 30\n\t"
    "mr 4, 26\n\t",
    GateMachines_CycleTRMachine,
    "",
    0x8002cc18,
    0x8002cbf0
)

// Solo-mode counterpart to GateMachines_FixupTRInit. TopRide_SoloInit (the
// init for Free Run / Time Attack, dispatched from TopRide_LobbyInit when
// TopRide_GetMode() != 0) hardcodes all four panel_machine slots to 0 (Free
// Star) at 0x8002db70..0x8002db88 - bypassing TopRide_InitSelectData and our
// InitSelectData hook entirely. Without a parallel fixup, the player's panel
// starts on Free regardless of unlock state, and pressing Start launches
// straight into a Free Star race even when Free is locked. The cycler gate
// in TopRide_CSS_PanelThink would let them L/R off to an unlocked machine
// if they manually entered editing, but the default is wrong.
//
// Hook at 0x8002db90 (`add r30, r31, r28`, the first instruction of the
// per-slot init loop). The immediately-prior `li r28, 0` at 0x8002db8c is
// already claimed by gate_colors's TR Solo color fixup - landing one
// instruction later means our hook fires *after* the colors fixup and the
// `li r28, 0` re-execution, so r28 = 0 and r31 = lobby base on entry. The
// hook framework re-executes the clobbered `add r30, r31, r28` after the
// epilogue, so the per-slot loop's r30 base register is intact.
CODEPATCH_HOOKCREATE(0x8002db90,
    "mr 3, 31\n\t",
    GateMachines_FixupTRInit,
    "",
    0
)

// Start-match gate for the TR lobby. The L/R cyclers already prevent the
// player from moving onto a locked machine, but with both Free and Steer
// locked the panel still defaults to Free (the GetFirstUnlockedTRMachine
// fallback) and the player can press Start to launch into a TR session with
// a machine they don't own. This gate refuses the launch entirely.
//
// On the block path it also gives feedback: the menu "denied" buzzer plus a
// textbox explaining why. Both start-gate hook sites reach this only on the
// Start-press rising edge (the pad `down` word at HSD_Pad+0x8 is tested for
// bit 0x1000 before the hook), so the buzzer + notification fire once per
// press rather than every frame Start is held - no debounce needed.
//
// Returns 0 = allow start, 1 = block start (no TR machine in the unlock mask).
// Convention matches GateTopRideStages_CourseSelectCanLaunch.
int GateMachines_TRLobbyCanStart(void)
{
    u32 tr_mask = (1u << VCKIND_FREE) | (1u << VCKIND_STEER);
    if (ap_save->machine_unlocked_mask & tr_mask)
        return 0;

    playSoundFX_errorNoise();
    tb_api->EnqueueColoredNoun("Unlock a ", "Top Ride machine", tb_api->MachineColor, " to start!");
    return 1;
}

// Hook at 0x8002c52c in TopRide_PreGameThink, the first instruction of the
// "start match" body (multiplayer race). The vanilla bytes are
// `bl 0x80061658` (menu confirm sound). The preceding `andi. r0, pad_bits,
// 0x1000` (test Start) and `cmpwi ply_state, 1` / `bne next-slot` already
// gate this site to "a player with a Ready panel just pressed Start" - we
// just need to additionally require that some TR machine is unlocked. The
// clobbered `bl` is re-emitted by the hook framework on the allow path.
//   r3 == 0 → run clobbered bl (play sound), fall through to commit+launch
//   r3 != 0 → jump to 0x8002c878 (next-slot iterator, skip start)
//
// Register preservation: this hook lives INSIDE the function's 4-slot scan
// loop, whose continuation at 0x8002c878 recomputes the slot base from r4/r5.
// Once the gate calls the SFX/textbox helpers it's no longer a leaf and
// clobbers those caller-saved volatiles, so the prologue stashes r4/r5 on a
// scratch frame and the epilogue restores them, keeping the loop register-clean.
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

// Hook at 0x8002cc80 in TopRide_OnCourseSelect, the first instruction of the
// solo-mode "start match" body (Free Run / Time Attack). Same shape as the
// PreGameThink gate above: the preceding `is_all_ready != 0` and pad bit
// 0x1000 (Start) tests already gate the site. The clobbered instruction is
// the same `bl 0x80061658` menu-confirm SFX.
//   r3 == 0 → run clobbered bl (play sound), fall through to commit+launch
//   r3 != 0 → jump to 0x8002cddc (function epilogue, skip start)
CODEPATCH_HOOKCONDITIONALCREATE(0x8002cc80,
    "",
    GateMachines_TRLobbyCanStart,
    "",
    0,
    0x8002cddc
)

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
// positions) - our packed arrays violate that assumption and trigger a
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
// Clobbered: or r26, r29, r29 (mr r26, r29 - harmless for the reordering code
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

// Per-slot bitmask of CT machine-select slots the player explicitly picked a
// machine for on the grid this session (bit = slot). Set by the icon[slot]
// write in CitySelect_Cursor1InputThink, consumed (and cleared) per slot when
// GateMachines_FinalizeCTMachine commits the starting machine. A manual pick on
// a CPU slot suppresses the random-start-machine re-roll for that slot.
static u8 ct_machine_manual_pick_mask = 0;

// Record that the player explicitly chose a machine for a CT select slot via the
// grid. Called from the icon[slot] write in CitySelect_Cursor1InputThink, the
// sole player-driven machine-grid pick (the auto / random-seed and cancel-restore
// writes live in other functions, so they never reach this hook).
void GateMachines_NoteManualMachinePick(int slot)
{
    if (slot >= 0 && slot < 4)
        ct_machine_manual_pick_mask |= (u8)(1 << slot);
}

// Finalize the City Trial starting machine at the convergence point of
// CitySelect_InitPlayerMachines (0x8002dea0), where the Trial branch (wrote
// Compact) and the Stadium / Free Run branch (wrote c_kind_arr[icon]) merge to
// look up the CharacterDesc. Fires once per slot.
//
// The "Random Start Machine" toggle is the single master and applies identically
// to humans and CPUs wherever neither makes an explicit grid pick:
//   - x215[slot]: 0 = human, 2 = CPU, else inactive (leave vanilla, return).
//   - Trial (x1d0 == 0): the free-roam start has no grid, so the toggle drives
//     every active slot the same way. ON -> random unlocked Kirby machine;
//     OFF -> Compact when unlocked, else a random unlocked Kirby machine.
//   - Stadium / Free Run (x1d0 != 0): humans actively pick on the grid, so a
//     human's selection is always kept; auto-assigned CPU machines follow the
//     toggle (ON -> random entry from the gated c_kind_arr; OFF -> leave the
//     vanilla seed). But a CPU the player explicitly picked a machine for (flagged
//     in ct_machine_manual_pick_mask) keeps that pick - the re-roll is skipped.
//     Dedede / Meta Knight are valid here, so the CPU pick draws straight from
//     the gated grid.
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
// CitySelect_InitPlayerMachines. r26 = slot index, r28 = city_select_ply + slot
// (both callee-saved). The prologue passes the slot; with skip target 0 the
// framework re-executes the clobbered `lbz r3, 97(r28)`, reloading the ckind we
// just wrote before the Character_GetDesc lookup that follows.
CODEPATCH_HOOKCREATE(0x8002dea0,
    "mr 3, 26\n\t",
    GateMachines_FinalizeCTMachine,
    "",
    0
)

// Hook the icon[slot] store in CitySelect_Cursor1InputThink (0x800315ac,
// `stb r27, 45(r30)`) - the sole player-driven machine-grid pick (it runs only
// when the chosen grid index actually changes). r29 = slot, callee-saved, so it
// survives the C call. Flags the slot as a manual pick, then skip target 0
// re-executes the clobbered store so the engine still commits icon[slot].
CODEPATCH_HOOKCREATE(0x800315ac,
    "mr 3, 29\n\t",
    GateMachines_NoteManualMachinePick,
    "",
    0
)

// Hook at 0x801952c8 in Rider_ResetStartingMachine.
// At entry: r31 = RiderData*. Replaces the two Ply_Set calls (is_bike=0,
// machine_kind=COMPACT) with our validated selection.
// Vanilla signature is Rider_ResetStartingMachine(RiderData *rd, int unk_arg2);
// our replacement only consumes rd - the prologue gating (which uses arg2)
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

// Zero the Air Ride CSS available-machine list (airride_select_ply +0x66, the
// 2x10 = 20-entry icon grid). AirRide_PopulateSelectIcons runs every CSS frame
// but only (re)writes the first `count` entries; it never clears the tail. When
// machine_unlocked_mask is narrowed mid-session (e.g. a debug-menu lock) the
// count drops, yet stale entries from an earlier fill linger past the new count.
// Every slot's icon index defaults to 0 and the CSS resolves the displayed/
// committed machine as list[icon], so a stale list[0] makes both the icon and
// the in-game machine a vehicle that is no longer unlocked (the symptom was the
// whole lobby defaulting to Winged Star after locking everything). Zeroing the
// list before each rebuild guarantees any entry past the live count reads
// CKIND_COMPACT (0); since populate runs per-frame the lobby self-heals the next
// frame instead of needing a full CSS re-entry. base = airride_select_ply.
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

    // Title-screen attract demo: gate the random machine picker's unlock check
    // so the idle demo never shows a locked machine. (Real Air Ride CPU machine
    // selection is gated upstream via the character list; see loadCPU.)
    CODEPATCH_REPLACEFUNC(TitleScreen_CheckMachineUnlocked, GateMachines_CheckTitleDemoMachineUnlocked);

    // Air Ride CSS: clear the cached available-machine list each frame in
    // AirRide_PopulateSelectIcons so a narrowed unlock mask (e.g. a mid-session
    // debug lock) can't leave stale entries that the per-slot icon index then
    // resolves to a now-locked machine.
    CODEPATCH_HOOKAPPLY(0x80020a88);

    // City Trial Stadium select screen: replace both the counting pass and
    // array-building pass in CitySelect_CreateMachineIcons (0x8002e3c4) for
    // mode 1. Vanilla mode 1 only checks ckind ranges (0-14 available,
    // 15/16/18/19 special characters unavailable) - no unlock mask check.
    CODEPATCH_HOOKAPPLY(0x8002e4d0);  // mode 1 counting pass
    CODEPATCH_HOOKAPPLY(0x8002e67c);  // mode 1 array-building pass

    // City Trial Free Run select screen: same treatment for mode 2.
    CODEPATCH_HOOKAPPLY(0x8002e5c0);  // mode 2 counting pass
    CODEPATCH_HOOKAPPLY(0x8002e738);  // mode 2 array-building pass

    // City Trial machine-select navigation off-by-one (Free Run + Stadium).
    // CitySelect_Cursor1InputThink (0x800312fc) gates two-row (up/down) cursor
    // movement on machine_select.num with `cmpwi r3, 9; ble` at 0x80031350 ->
    // num<=9 single-row, num>=10 two-row (split at ceil(num/2)). But the icon
    // grid is a 2x10 layout whose box animation (keyed on the count) keeps up
    // to 10 icons on a SINGLE line and only wraps to two rows at 11. So at
    // exactly num==10 the renderer draws one line of 10 while the cursor splits
    // it 5+5 and up/down jumps between the halves. Vanilla CT only ever
    // produces counts 15-20 on these screens, so the off-by-one was never
    // exercised; AP machine gating can land on exactly 10 unlocked machines.
    // Verified live: at num==10 every icon slot shares one Y (single row).
    // Patch the threshold to `cmpwi r3, 10` (num<=10 single-row) so navigation
    // matches the renderer's single-line layout at 10.
    CODEPATCH_REPLACEINSTRUCTION(0x80031350, 0x2c03000a);  // cmpwi r3, 10

    // City Trial starting machine: finalize each active slot's machine at the
    // CSS convergence point per the Random Start Machine toggle (Trial: humans
    // and CPUs alike; Stadium / Free Run: CPUs only, humans keep their pick).
    CODEPATCH_HOOKAPPLY(0x8002dea0);

    // Respawn machine validation: use starting machine instead of hardcoded Compact
    CODEPATCH_HOOKAPPLY(0x801952c8);

    // Top Ride lobby: gate the L/R "Control Type" cycler and override the
    // panel_machine init default so locked Free/Steer is never shown. The race
    // lobby and the solo Free Run / Time Attack lobby are separate code paths
    // with separate cyclers, inits, and start-match handlers - each needs its
    // own hook. Three init paths (general InitSelectData, multiplayer RaceInit,
    // solo SoloInit), two cyclers (CSS_PanelThink for race, SoloPanelThink for
    // solo) and two start-match paths (PreGameThink for race, OnCourseSelect
    // for solo).
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
        // Dedede / Meta Knight unlocks - to the player these are characters, not
        // just a machine, so they announce as "Unlocked Character: <name>" to
        // read identically to the REWARD_KING_DEDEDE / REWARD_META_KNIGHT
        // checklist path (which also uses MachineColor).
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
// VsHydra.dat) are a one-shot preloaded resource - the assembly cinematic frees
// the archive when it finishes, so a second cinematic in the same scene loads a
// dangling joint and crashes in HSD_JObjLoadJoint. Vanilla only ever assembles
// each legendary once per round; we mirror that. Reset on each 3D scene load
// (where the archives are preloaded fresh) via GateMachines_On3DLoadEnd.
static u8 legendary_assembled_mask;

void GateMachines_On3DLoadEnd(void)
{
    legendary_assembled_mask = 0;
}

// Give a player the assembled legendary machine via the cinematic.
// machine_index: 0 = Dragoon, 1 = Hydra.
// Returns 1 if started (consume the item), 0 if it can't run yet (keep the item
// queued and retry).
//
// The assembly cinematic loads the legendary machine's piece models and drives
// the City Trial sky/area-light setup, all of which only exist on the open City
// Trial map. Running it anywhere else - any stadium (which shares the City
// major), the trial-ending stadium, or Air/Top Ride - makes the cinematic
// dereference a null jobj / hit the area-light assert and crash. Gm_IsInCity()
// is stage-based (true only on the open CT map, stage_kind 9/52), so it excludes
// every stadium as well as AR/TR. Returning 0 keeps the item queued so it
// assembles once the player is back on the open map.
int GateMachines_GiveLegendaryMachine(int machine_index)
{
    if (!Gm_IsInCity())
        return 0;

    // This legendary was already assembled in this scene. Its piece archive has
    // been freed, so re-running the cinematic would crash. Keep the item queued
    // (return 0) rather than consuming it - it assembles on the next scene load,
    // where the archives are preloaded fresh and the one-shot guard is reset.
    u8 bit = (u8)(1 << machine_index);
    if (legendary_assembled_mask & bit)
        return 0;

    // A legendary assembly cinematic is already running (its GObj lives at
    // GameData+0xA8C for the cinematic's lifetime). Starting a second one tears
    // down the in-flight cinematic's piece GObjs and leaves a dangling jobj that
    // crashes on the next update. Wait for it to finish, then re-evaluate.
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
