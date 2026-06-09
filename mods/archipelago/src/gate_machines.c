#include "game.h"
#include "menu.h"
#include "os.h"
#include "scene.h"
#include "topride.h"
#include "audio.h"
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

// Get the first unlocked City-Trial-spawnable MachineKind, or VCKIND_COMPACT as
// absolute fallback. Skips CT_SPAWN_EXCLUDED_MASK machines (Free/Steer Star,
// transformation forms, debug wheelie kinds) so a sparse unlock state — e.g.
// only the Top Ride Free/Steer Star unlocked — never falls back to spawning a
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

// ===== Top Ride machine gating =====
//
// TR's lobby panel exposes two machines on its middle ("Control Type") row:
// Free Star (TR_MACHINE_FREE = 0) and Steer Star (TR_MACHINE_STEER = 1). The
// committed value lives at GameData.topride_slot[slot].machine_kind; the
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

// Post-init fixup for TopRide_InitSelectData (0x8002cfd8). Vanilla's per-slot
// init loop unconditionally writes panel_machine[slot] = 0 (Free Star). When
// Free is locked, walk the 4 panels and force them onto the first unlocked
// TR machine so the lobby never starts on a locked option.
void GateMachines_FixupTRInit(u8 *lobby_base)
{
    TopRideMachineKind machine = GetFirstUnlockedTRMachine();
    // 0x2f = offsetof(topride_select_ply, panel_machine) - 0x37 (lobby base
    // is GameData+0x197, not GameData+0x160). See game.h.
    for (int i = 0; i < 4; i++)
        lobby_base[0x2f + i] = (u8)machine;
}

// Hook at 0x8002d070 in TopRide_InitSelectData, immediately after the per-slot
// init loop finishes. (0x8002d06c, the first post-loop instruction, is already
// claimed by gate_colors.c.) At entry: r31 = lobby base (GameData + 0x197,
// callee-saved). Vanilla post-loop relies on r3 = 0 (set by `li r3, 0` at
// 0x8002d06c) for the three `stb r3, {6,2,3}(r31)` lobby-flag clears that
// follow our hook site. r3 is caller-saved so the C call wipes it — the
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
// so the bug only manifests on slots that hit the reset path — but the
// SoloInit fixup at 0x8002db90 already covers the same logic, so closing
// the asymmetry here restores parity with TR Main Game.
//
// Hook at 0x8002d748 (`bl 0x80006c14` = `bl gmGetGlobalP`), the first
// instruction after the panel_pkind CPU-fill loop at 0x8002d710..0x8002d744
// ends. We can't land any earlier inside that loop because the iterator
// r7 is caller-saved and our C call would clobber it. By landing on the
// post-loop bl that re-fetches GameData, the framework's auto-re-execution
// of the bl naturally restores r3 = GameData* for the addi at 0x8002d74c —
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
// Generic over both lobby flavors — the race lobby (TopRide_CSS_PanelThink,
// 0x8002b8a8) and the solo Free Run / Time Attack lobby (TopRide_SoloPanelThink,
// 0x8002ca80) each carry their own copy of this cycler with identical RIGHT
// (0x80002) / LEFT (0x40001) edge bits and the same panel_machine offset (0x2f),
// so a single gate function serves both hook sites.
//
// Inputs (set by each hook's asm prologue from the regs live at that site):
//   panel_base = lobby base + panel — panel_base[0x2f] = panel_machine[panel]
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
// TopRide_CSS_PanelThink, so the race cycler hook above does not cover it —
// without this hook, Free Run / Time Attack lets the player L/R straight onto
// a locked machine (and then launch it, since the start gate only checks that
// *some* TR machine is unlocked).
//
// Hook at 0x8002cb98 (`and. r0, r26, r0`, the RIGHT-bit test). This lands
// AFTER the outer 0xC0003 L/R guard at 0x8002cb80 and AFTER the cycler sets up
// r29 = panel index and r30 = lobby + panel (0x8002cb8c / 0x8002cb94), so the
// downstream SFX + UI block at 0x8002cbf0 finds those callee-saved regs intact
// across our C call — no epilogue needed. The replaced span runs through the
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
// Star) at 0x8002db70..0x8002db88 — bypassing TopRide_InitSelectData and our
// InitSelectData hook entirely. Without a parallel fixup, the player's panel
// starts on Free regardless of unlock state, and pressing Start launches
// straight into a Free Star race even when Free is locked. The cycler gate
// in TopRide_CSS_PanelThink would let them L/R off to an unlocked machine
// if they manually entered editing, but the default is wrong.
//
// Hook at 0x8002db90 (`add r30, r31, r28`, the first instruction of the
// per-slot init loop). The immediately-prior `li r28, 0` at 0x8002db8c is
// already claimed by gate_colors's TR Solo color fixup — landing one
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
// press rather than every frame Start is held — no debounce needed.
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
// gate this site to "a player with a Ready panel just pressed Start" — we
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

    // City Trial mode 0: replace hardcoded Compact Star default with first unlocked machine
    CODEPATCH_HOOKAPPLY(0x8002de80);

    // Respawn machine validation: use starting machine instead of hardcoded Compact
    CODEPATCH_HOOKAPPLY(0x801952c8);

    // Top Ride lobby: gate the L/R "Control Type" cycler and override the
    // panel_machine init default so locked Free/Steer is never shown. The race
    // lobby and the solo Free Run / Time Attack lobby are separate code paths
    // with separate cyclers, inits, and start-match handlers — each needs its
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
        // Dedede / Meta Knight unlocks — to the player these are characters, not
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

// Give a player the assembled legendary machine via the cinematic.
// machine_index: 0 = Dragoon, 1 = Hydra.
// Returns 1 if started, 0 if in an unsupported mode or no machine to swap from.
// City Trial only: Top Ride has no MachineData / Rider pipeline, and the
// assembly cinematic crashes on Air Ride courses (legendary machines have no
// AR course support).
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
