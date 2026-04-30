#include "game.h"
#include "hsd.h"
#include "os.h"
#include "text.h"
#include "audio.h"
#include "inline.h"
#include "code_patch/code_patch.h"
#include "hoshi/func.h"

#include "main.h"
#include "checklist_rewards.h"
#include "check_detection.h"
#include "gate_machines.h"
#include "gate_colors.h"
#include "gate_airride_stages.h"
#include "gate_topride_items.h"
#include "gate_stadiums.h"
#include "textbox.h"

// Per-mode reward table sizes (REWARD_COUNT_AIRRIDE / TOPRIDE / CITYTRIAL).
static const int reward_counts[GMMODE_NUM] = {
    [GMMODE_AIRRIDE]   = REWARD_COUNT_AIRRIDE,
    [GMMODE_TOPRIDE]   = REWARD_COUNT_TOPRIDE,
    [GMMODE_CITYTRIAL] = REWARD_COUNT_CITYTRIAL,
};

// Cross-mode reward mapping: for each (target_mode, clear_kind), stores which
// reward from another mode is placed there. source_mode == 0xFF means none.
typedef struct CrossModeSlot
{
    u8 source_mode;
    u8 source_reward_index;
} CrossModeSlot;

// Populated by ApplyLocations, rebuilt on save load.
static CrossModeSlot cross_mode_slots[GMMODE_NUM][CLEAR_KIND_NUM];

// Snapshot of the cell currently under the cursor in the checklist UI,
// updated by the FindRewardForCell hook. `source_mode` is the mode whose
// reward table the text/icon hooks should read (differs from `mode` for
// cross-mode placements). `valid` is 0 before any cell has been hovered.
static struct {
    u8 mode;
    u8 clear_kind;
    u8 source_mode;
    u8 valid;
} hover;

int ChecklistRewards_GetHoveredCell(u8 *out_mode, u8 *out_clear_kind)
{
    if (!hover.valid)
        return 0;
    *out_mode = hover.mode;
    *out_clear_kind = hover.clear_kind;
    return 1;
}

// Resolve the reward placed at (mode, clear_kind). On success writes the
// source mode + reward_index to the out params and returns 1. Returns 0 if no
// reward is placed here (remote, or no placement yet).
//
// Order: cross_mode_slots first, then scan this mode's save_shuffled for a
// (mode << 8) | clear_kind match. Matching the full u16 encoding avoids the
// clear_kind=0 sentinel aliasing that a RewardEntry.clear_kind scan would hit.
int ChecklistRewards_ResolveCell(u8 mode, u8 clear_kind,
                                 u8 *out_source_mode, u8 *out_source_reward_index)
{
    if (mode >= GMMODE_NUM || clear_kind >= CLEAR_KIND_NUM)
        return 0;

    CrossModeSlot *slot = &cross_mode_slots[mode][clear_kind];
    if (slot->source_mode != 0xFF)
    {
        *out_source_mode = slot->source_mode;
        *out_source_reward_index = slot->source_reward_index;
        return 1;
    }

    u16 target = ((u16)mode << 8) | clear_kind;
    int count = reward_counts[mode];
    for (int ri = 0; ri < count; ri++)
    {
        if (ap_save->shuffled_rewards[mode][ri] != target)
            continue;
        *out_source_mode = mode;
        *out_source_reward_index = (u8)ri;
        return 1;
    }
    return 0;
}

int ChecklistRewards_CellHasReceivedReward(u8 mode, u8 clear_kind)
{
    u8 src_mode, src_ri;
    if (!ChecklistRewards_ResolveCell(mode, clear_kind, &src_mode, &src_ri))
        return 0;
    return (ap_save->received_checklist_rewards[src_mode] & (1ULL << src_ri)) != 0;
}

static void ClearCrossModeSlots(void)
{
    for (int m = 0; m < GMMODE_NUM; m++)
        for (int k = 0; k < CLEAR_KIND_NUM; k++)
            cross_mode_slots[m][k].source_mode = 0xFF;
}

// Returns 1 iff reward_index in mode's table has a local placement in THAT
// mode's checklist (not cross-mode, not remote). This is the predicate that
// every vanilla-facing read of RewardEntry.clear_kind must gate on — cross-mode
// source rows store the 0 sentinel in their clear_kind field and must never
// cause a read of the source mode's clear[0].
static int IsSameModeLocalPlacement(u8 mode, u8 reward_index)
{
    u16 loc = ap_save->shuffled_rewards[mode][reward_index];
    return loc != 0xFFFF && (u8)(loc >> 8) == mode;
}

// Replacement for ClearChecker_CheckUnlocked (0x80049E24).
// Order of checks:
//   1. AP received bitfield — covers same-mode, cross-mode, and remote rewards
//      that the player has already received.
//   2. Same-mode local placement — read clear_kind directly from the saved
//      shuffle u16 and check the cell's has_reward bit. Cross-mode source rows
//      and remote rewards fail the same-mode test and return 0 without ever
//      reading clear[].
// Installed via CODEPATCH_REPLACEFUNC in ChecklistRewards_OnBoot.
int ChecklistRewards_CheckUnlocked(GameMode mode, u8 reward_index)
{
    if (ap_save->received_checklist_rewards[mode] & (1ULL << reward_index))
        return 1;
    u16 loc = ap_save->shuffled_rewards[mode][reward_index];
    if (loc == 0xFFFF || (u8)(loc >> 8) != (u8)mode)
        return 0;
    GameClearData *cd = gmGetClearcheckerTypeP(mode);
    return cd->clear[(u8)loc].has_reward;
}

// Replacement for ClearChecker_GetRewardFromClearKind (0x80049EC4).
//
// Vanilla scans the mode's reward table for a row whose .clear_kind matches
// the target, returning the first match. Under shuffle, ~2/3 of rows carry
// the clear_kind=0 sentinel (cross-mode + remote placements), so a vanilla
// call with target=0 returns the first sentinel — not the legitimate same-
// mode placement at clear_kind=0 (if any). The reward_param on the returned
// row is the original vanilla value, so the sole caller (audio/ending preview
// path inside Checklist_Think at 0x801804dc) can end up playing the wrong
// music or ending preview when the user hovers the clear_kind=0 cell.
//
// We resolve via ap_save->shuffled_rewards — the canonical placement store —
// and hand back the source row's reward_index. For cross-mode music previews
// (src_param == REWARDPARAM_AUDIO), the caller's per-mode audio scan at
// 0x80180508 would otherwise look up our source_ri in the CURRENT mode's
// audio table (wrong). Our AudioPreview hook reads hover.source_mode (set by
// the Checklist_UpdateCellInfo hook on the prior frame for the same hovered
// cell) to redirect that scan to the source mode's table, so cross-mode
// music rewards preview correctly.
//
// Cross-mode ending previews (src_param == REWARDPARAM_ENDING) are safe to
// route through the vanilla path at 0x80180554 — vanilla's ending "preview"
// only sets the UI state byte to 14 and plays a menu click sound; no actual
// ending movie is played (state=14 is a terminal UI-pause state, not
// dispatched by Checklist_Think). So cross-mode parity with vanilla needs
// nothing beyond returning src_ri here.
void ChecklistRewards_GetRewardFromClearKind(GameMode mode, u8 clear_kind,
                                             u8 *out_reward_index,
                                             u8 *out_reward_param)
{
    if ((unsigned)mode >= GMMODE_NUM || clear_kind >= CLEAR_KIND_NUM)
    {
        *out_reward_index = 0xFF;
        return;
    }

    GameClearData *cd = gmGetClearcheckerTypeP(mode);
    // Vanilla early-exit: scan only runs when the cell is unlocked or filler'd.
    if (!cd || !(cd->clear[clear_kind].is_unlocked || cd->clear[clear_kind].is_filler))
    {
        *out_reward_index = 0xFF;
        return;
    }

    u8 src_mode, src_ri;
    if (!ChecklistRewards_ResolveCell((u8)mode, clear_kind, &src_mode, &src_ri))
    {
        *out_reward_index = 0xFF;
        return;
    }

    *out_reward_index = src_ri;
    *out_reward_param = stc_reward_table_ptrs[src_mode][src_ri].reward_param;
}

// City Trial REWARD_STADIUM reward_index -> StadiumKind. Mapping is hardcoded
// in vanilla's Checklist_ProcessUnlock (0x8017e490) — reward_param is 0 for
// every REWARD_STADIUM entry, so the target stadium has to be re-derived here.
// Indices outside this range have no stadium unlock.
static int CtRewardIndexToStadium(u8 reward_index)
{
    switch (reward_index)
    {
        case 37: return STKIND_DRAG4;
        case 38: return STKIND_MELEE2;
        case 39: return STKIND_DESTRUCTION3;
        case 40: return STKIND_DESTRUCTION4;
        case 41: return STKIND_DESTRUCTION5;
        case 42: return STKIND_SINGLERACE9; // Nebula Belt
        default: return -1;
    }
}

// Route a vanilla checklist reward into the mod's gate system. Called from
// ChecklistRewards_Grant so the corresponding gate mask bit flips regardless of
// whether the reward came from an AP unlock item or from earning the checkbox.
// Without this, vanilla rewards write only to dead storage and never actually
// unlock anything through our replaced unlock-check functions.
//
// Handles all reward_types that map to a currently-gated category. Fillers,
// bonus movies, sound test, music, endings, extra rules, and pause power-ups
// are left to vanilla (not gated, no mask to flip).
static void ApplyVanillaRewardUnlock(GameMode mode, u8 reward_index, u8 reward_type)
{
    switch (reward_type)
    {
        case REWARD_COURSE: // Nebula Belt (Air Ride)
            GateAirRideStages_UnlockStage(AIRRIDE_NEBULA_BELT);
            break;

        case REWARD_STADIUM:
            if (mode == GMMODE_CITYTRIAL)
            {
                int st = CtRewardIndexToStadium(reward_index);
                if (st >= 0)
                    GateStadiums_UnlockStadium((StadiumKind)st);
            }
            break;

        case REWARD_MACHINE_WINGED_STAR:     GateMachines_UnlockMachine(VCKIND_WINGED);         break;
        case REWARD_MACHINE_WAGON_STAR:      GateMachines_UnlockMachine(VCKIND_WAGON);          break;
        case REWARD_MACHINE_SWERVE_STAR:     GateMachines_UnlockMachine(VCKIND_SWERVE);         break;
        case REWARD_MACHINE_BULK_STAR:       GateMachines_UnlockMachine(VCKIND_BULK);           break;
        case REWARD_MACHINE_WHEELIE_BIKE:    GateMachines_UnlockMachine(VCKIND_WHEELIEBIKE);    break;
        case REWARD_MACHINE_SLICK_STAR:      GateMachines_UnlockMachine(VCKIND_SLICK);          break;
        case REWARD_MACHINE_FORMULA_STAR:    GateMachines_UnlockMachine(VCKIND_FORMULA);        break;
        case REWARD_MACHINE_SHADOW_STAR:     GateMachines_UnlockMachine(VCKIND_SHADOW);         break;
        case REWARD_MACHINE_WHEELIE_SCOOTER: GateMachines_UnlockMachine(VCKIND_WHEELIESCOOTER); break;
        case REWARD_MACHINE_ROCKET_STAR:     GateMachines_UnlockMachine(VCKIND_ROCKET);         break;
        case REWARD_MACHINE_TURBO_STAR:      GateMachines_UnlockMachine(VCKIND_TURBO);          break;
        case REWARD_MACHINE_JET_STAR:        GateMachines_UnlockMachine(VCKIND_JET);            break;
        case REWARD_MACHINE_REX_WHEELIE:     GateMachines_UnlockMachine(VCKIND_REXWHEELIE);     break;

        // Character rewards resolve through CharacterDesc_GetMachineKind in
        // GateMachines_CheckAirRideCharacterAvailable, so unlocking the
        // character's machine also unlocks selecting the character.
        // VCKIND_WHEELDEDEDE (24) is the player-facing Dedede machine.
        // VCKIND_WHEELVSDEDEDE (25) is the Vs. King Dedede stadium CPU-only
        // machine; no game code reads its unlock bit, so we don't set it.
        case REWARD_KING_DEDEDE:
            GateMachines_UnlockMachine(VCKIND_WHEELDEDEDE);
            break;
        case REWARD_META_KNIGHT:
            GateMachines_UnlockMachine(VCKIND_WINGMETAKNIGHT);
            break;

        case REWARD_DRAGOON: GateMachines_UnlockMachine(VCKIND_DRAGOON); break;
        case REWARD_HYDRA:   GateMachines_UnlockMachine(VCKIND_HYDRA);   break;

        case REWARD_COLOR_GREEN:  GateColors_UnlockColor(KIRBYCOLOR_GREEN);  break;
        case REWARD_COLOR_PURPLE: GateColors_UnlockColor(KIRBYCOLOR_PURPLE); break;
        case REWARD_COLOR_BROWN:  GateColors_UnlockColor(KIRBYCOLOR_BROWN);  break;
        case REWARD_COLOR_WHITE:  GateColors_UnlockColor(KIRBYCOLOR_WHITE);  break;

        // TR "New Item" rewards. Vanilla TopRide_OnCourseSelect (0x8002cc30)
        // reads the checklist has_reward bit for reward indices 8/9/10
        // (CHICKIE/WHO_PAINT/LANTERN) and passes the flags to
        // TopRide_SetExtraUnlocks (0x8000b5dc) → GameData+0x37e/+0x37f/+0x380.
        // Those propagate into TopRideItem_MgrInit's config (+0x4a/+0x4b/+0x4c),
        // which conditionally clears bits 20/18/15 of ItemMgr.enabled_mask.
        // Result: Chickie→bit 20 (piyo), Who? Paint→bit 18 (meta), Lantern→bit 15 (lanthanum).
        case REWARD_ITEM_CHICKIE:   GateTopRideItems_UnlockItem(TRITEM_CHICKIE);   break;
        case REWARD_ITEM_WHO_PAINT: GateTopRideItems_UnlockItem(TRITEM_WHO_PAINT); break;
        case REWARD_ITEM_LANTERN:   GateTopRideItems_UnlockItem(TRITEM_LANTERN);   break;

        default:
            // Not gated, left to vanilla: REWARD_FILLER, REWARD_BONUS_MOVIE,
            // REWARD_EXTRA_RULE, REWARD_SOUND_TEST, REWARD_MUSIC, REWARD_ENDING,
            // REWARD_PAUSE_POWERUPS, and REWARD_DRAGOON_PART_*/HYDRA_PART_*
            // (checklist-internal "all parts" progress markers — distinct from
            // ITUNLOCK_DRAGOON*/HYDRA*, which gate in-round piece spawns; setting
            // has_reward is enough since vanilla Checklist_ProcessUnlock flips
            // the assembled checkbox once all 3 of a set are marked).
            break;
    }
}

// Grant a checklist reward received from the AP server.
// Sets the AP bitfield and marks the local checklist slot if one is assigned
// (same-mode or cross-mode). The unlock_cache at GameData+0xD50 is rebuilt by
// Checklist_BuildUnlockBitfields the next time it runs, and that function
// calls our REPLACEFUNC'd ClearChecker_CheckUnlocked — so it picks up the
// new bit automatically without an explicit cache write here.
void ChecklistRewards_Grant(GameMode mode, u8 reward_index)
{
    ap_save->received_checklist_rewards[mode] |= (1ULL << reward_index);

    // reward_type survives all cross-mode / shuffle remapping (only clear_kind
    // is overwritten), so this lookup is always valid.
    u8 reward_type = stc_reward_table_ptrs[mode][reward_index].reward_type;
    OSReport("[Checklist] Granted mode=%d ri=%d type=%s (%d)\n",
             mode, reward_index, Reward_TypeName(reward_type), reward_type);
    TextBox_Enqueue("Received: %s", Reward_TypeName(reward_type));
    ApplyVanillaRewardUnlock(mode, reward_index, reward_type);

    // The shuffled u16 encodes the placement cell directly: high byte = target
    // mode, low byte = target clear_kind. 0xFFFF = remote (no local cell).
    // Works uniformly for same-mode and cross-mode placements. We write
    // has_reward only — is_unlocked is reserved for "player completed this in
    // gameplay" and drives outbound check detection (check_detection.c).
    u16 loc = ap_save->shuffled_rewards[mode][reward_index];
    if (loc != 0xFFFF)
    {
        GameClearData *cd = gmGetClearcheckerTypeP((GameMode)(loc >> 8));
        cd->clear[loc & 0xFF].has_reward = 1;
    }
    else if (reward_type == REWARD_FILLER)
    {
        // Remote placement: vanilla's reward loop and ApplyCrossModeHasReward
        // never fire for this reward (no cell in our world holds it), so
        // increment the filler counter ourselves. Same-mode local and
        // cross-mode local FILLERs are already covered by those two paths.
        Checklist_GrantFiller(mode);
    }
}

// Filter for the reward loop in Checklist_SetRewardFlagOnUnlocks (0x8017DF5C).
// Returns 1 to skip, 0 to process normally. We skip every reward that isn't a
// same-mode local placement — remote rewards and cross-mode-source rows both
// have their stored clear_kind set to the 0 sentinel, and letting vanilla read
// clear[0] via GetClearKindFromRewardIndex would spuriously set has_reward on
// clear[mode][0] whenever the player completes or fillers that checkbox.
// Cross-mode placements are handled post-loop by ApplyCrossModeHasReward.
int ChecklistRewards_ShouldSkipReward(GameMode mode, u8 reward_index)
{
    return !IsSameModeLocalPlacement((u8)mode, reward_index);
}

// Hook at 0x8017dfd8: top of the reward loop body in Checklist_SetRewardFlagOnUnlocks.
// Clobbered instruction: lbz r3, 0x14(r30)  (loads mode from checklist UI struct).
// Normal exit (filter returns 0): execute clobbered insn, continue loop body at 0x8017dfdc.
// Alt exit (filter returns 1): skip to next iteration at 0x8017e064.
CODEPATCH_HOOKCONDITIONALCREATE(
    0x8017dfd8,
    "lbz 3, 0x14(30)\n\t"
    "mr 4, 27\n\t",
    ChecklistRewards_ShouldSkipReward,
    "",
    0,
    0x8017e064
)

// Checklist audio preview (Checklist_Think, 0x80180508).
//
// Vanilla flow: caller has reward_index in r4, r18 = audio_table_ptrs[current_mode].
// Loop compares r4 against each table entry's first byte until a match or 0xFF
// terminator, then calls BGM_Play(song) and persists the song to
// MainMenuData.soundtest_bgm_kind (GameData+0x4E).
//
// Under shuffle with cross-mode placements, a music reward at a cell in mode X
// may have its source reward_index in mode Y's table. Looking up Y's source_ri
// in X's audio table returns the wrong song (or nothing). Our hook replaces
// the entire scan with one that uses hover.source_mode (set by the
// FindRewardForCell hook in Checklist_UpdateCellInfo when the cursor lands on
// the cell) to select the correct audio table.
//
// For same-mode placements hover.source_mode == current_mode, so this path
// picks the same table vanilla would have picked. If the cursor has not
// hovered any cell yet (hover.valid == 0), the audio preview path is
// unreachable in practice — the audio-preview button is only effective on a
// cell whose reward_index was just resolved by GetRewardFromClearKind, which
// can only succeed for a cell the player has navigated to.
static int ChecklistRewards_AudioPreview(u8 reward_index)
{
    if (!hover.valid || hover.source_mode >= GMMODE_NUM)
        return 1;  // alt-exit (skip)

    u8 *table = stc_audio_preview_tables[hover.source_mode];
    if (!table)
        return 1;

    for (int i = 0; table[i * 2] != 0xFF; i++)
    {
        if (table[i * 2] != reward_index)
            continue;
        s8 song_id = (s8)table[i * 2 + 1];
        BGM_Play((u8)song_id);
        GameData *gd = Gm_GetGameData();
        if (gd)
            gd->main_menu.soundtest_bgm_kind = song_id;
        break;
    }
    return 1;  // always alt-exit past the vanilla scan
}

// Hook at 0x80180508. Reached only when reward_param == REWARDPARAM_AUDIO.
// r4 = reward_index (set by the GetRewardFromClearKind call at 0x801804dc).
// Always alt-exits to 0x80180560 — past the vanilla scan, BGM_Play call, and
// persist helper, landing on the next state-transition check in Checklist_Think.
CODEPATCH_HOOKCONDITIONALCREATE(
    0x80180508,
    "clrlwi 3, 4, 24\n\t",  // reward_index → r3 (arg 1)
    ChecklistRewards_AudioPreview,
    "",
    0,
    0x80180560
)

// SIS file names per mode (indexed by GameMode).
static const char *sis_filenames[GMMODE_NUM] = {
    "SisClrChk3D.dat",  // GMMODE_AIRRIDE
    "SisClrChk2D.dat",  // GMMODE_TOPRIDE
    "SisClrChkCT.dat",  // GMMODE_CITYTRIAL
};

// Maps GameMode -> SIS slot index. Slot 0 is always the current mode (so all
// vanilla code with default sis_id=0 works). The other two modes get slots 1-2.
static u8 mode_to_sis_slot[GMMODE_NUM];

// Load all 3 checklist SIS files. The current mode goes into slot 0 (so vanilla
// code works), the other two into slots 1 and 2.
static void LoadAllChecklistSIS(u8 current_mode)
{
    Text_LoadSisFile(0, (char *)sis_filenames[current_mode], "SIS_Clearchecker");
    mode_to_sis_slot[current_mode] = 0;

    int slot = 1;
    for (int m = 0; m < GMMODE_NUM; m++)
    {
        if (m == current_mode)
            continue;
        Text_LoadSisFile(slot, (char *)sis_filenames[m], "SIS_Clearchecker");
        mode_to_sis_slot[m] = (u8)slot;
        slot++;
    }
}

// Hook at 0x801823c4: convergence point after the mode-specific SIS load in
// checklist init (zz_801822f4_). We NOP the 3 original Text_LoadSisFile calls
// and load all 3 SIS files here instead.
// r23 = current checklist mode (set at function entry)
// Clobbered instruction: lwz r3, 0x0ecc(r30)
CODEPATCH_HOOKCREATE(
    0x801823c4,
    "clrlwi 3, 23, 24\n\t",  // r3 = mode (from r23)
    LoadAllChecklistSIS,
    "lwz 3, 0x0ecc(30)\n\t",
    0
)

// Hook at 0x80181ee4 in Checklist_UpdateCellInfo: intercept the reward_index
// reverse lookup. Replaces the vanilla scan entirely — we always alt-exit past
// it so vanilla never reads RewardEntry.clear_kind (which would alias cross-mode
// sentinel rows against clear_kind=0). Handles cross-mode via cross_mode_slots
// and same-mode via the save_shuffled encoding.
// Clobbered instruction: li r25, 0  (init loop counter before vanilla scan).
// Returns: positive = reward_index + 1 (unlocked, display this reward)
//          negative = -1 (no reward visible at this cell)
static int ChecklistRewards_FindRewardForCell(u8 current_mode, u8 clear_kind)
{
    // Snapshot the hovered cell for any outside consumer (e.g. debug X-unlock).
    // This hook fires when the cursor lands on a new cell in UpdateCellInfo.
    hover.mode = current_mode;
    hover.clear_kind = clear_kind;
    hover.valid = 1;

    u8 src_mode, src_ri;
    if (!ChecklistRewards_ResolveCell(current_mode, clear_kind, &src_mode, &src_ri))
    {
        hover.source_mode = current_mode;
        return -1;
    }
    hover.source_mode = src_mode;

    // AP received bit → show the reward regardless of local unlock state.
    if (ap_save->received_checklist_rewards[src_mode] & (1ULL << src_ri))
        return (int)src_ri + 1;

    // Not received yet. Fall back to local cell state.
    //   same-mode:  show if has_reward is set (normal flow)
    //   cross-mode: show if unlocked OR has_reward (unlocked handles the
    //               newly-completed-this-session case before the post-loop
    //               hook has had a chance to mirror has_reward).
    GameClearData *cd = gmGetClearcheckerTypeP(current_mode);
    int visible = (src_mode == current_mode)
        ? cd->clear[clear_kind].has_reward
        : (cd->clear[clear_kind].is_unlocked || cd->clear[clear_kind].has_reward);
    return visible ? (int)src_ri + 1 : -1;
}

// Hook at 0x80181ee4: replaces the entire vanilla reverse-lookup scan.
// Clobbered instruction: li r25, 0
// Args: r3 = mode (from r30+0x14), r4 = clear_kind (r26)
// Return handling: r3 = reward_index + 1 if unlocked, -1 if locked/empty.
// Epilogue sets r0 = reward_index (or -1), then alt-exits to 0x80181f5c where
// vanilla does `mr r27, r0`.
CODEPATCH_HOOKCREATE(
    0x80181ee4,
    "lbz 3, 0x14(30)\n\t"
    "mr 4, 26\n\t",
    ChecklistRewards_FindRewardForCell,
    "cmpwi 3, 0\n\t"
    "blt 0f\n\t"
    "addi 0, 3, -1\n\t"  // r0 = reward_index (undo the +1)
    "b 1f\n\t"
    "0:\n\t"
    "li 0, -1\n\t"       // r0 = -1 (no visible reward)
    "1:\n\t",
    0x80181f5c           // alt exit: skip past vanilla loop + unlock check
)

// Hook at 0x8018201c: reward text display in Checklist_UpdateCellInfo.
// Vanilla code:
//   8018201c: lwz r3, 12(r29)       -- text object
//   80182020: addi r4, r27, 0x7D    -- text_index = reward_index + 125
//   80182024: bl Text_FinalizeSisText
// We replace this entire sequence to handle cross-mode sis_id switching.
// r27 = reward_index (set by our lookup hook or vanilla loop)
// r29 = UI data struct (text object at +0x0C)
// r30 = checklist UI struct (mode at +0x14)
static void ChecklistRewards_DisplayRewardText(Text *text, int reward_index, u8 current_mode)
{
    // Temporarily switch sis_id to the source mode's slot to read the correct
    // text command data, then restore to slot 0 so Text_GX uses slot 0's
    // glyph/font data for rendering (all checklist SIS files share the same font).
    u8 source_slot = mode_to_sis_slot[hover.source_mode];
    text->sis_id = source_slot;
    Text_FinalizeSisText(text, reward_index + 0x7D);
    text->sis_id = 0;
}

CODEPATCH_HOOKCREATE(
    0x8018201c,
    "lwz 3, 0x0c(29)\n\t"   // text object -> r3
    "mr 4, 27\n\t"           // reward_index -> r4
    "lbz 5, 0x14(30)\n\t",  // current_mode -> r5
    ChecklistRewards_DisplayRewardText,
    "",
    0x80182028               // skip past the vanilla lwz + addi + bl sequence
)

// Hook for blank text sis_id: ensure sis_id is reset to 0 (current mode's slot)
// after a cross-mode reward may have changed it. The reward text hook sets sis_id
// to the source mode's slot, so we must restore it here.
// Hook at 0x80181f8c: blank text path (lwz r3, 12(r29) before li r4, 0x7c).
static void ChecklistRewards_SetBlankTextSisId(Text *text, u8 current_mode)
{
    text->sis_id = 0; // Slot 0 is always the current mode
    Text_FinalizeSisText(text, 0x7C);
}

CODEPATCH_HOOKCREATE(
    0x80181f8c,
    "lwz 3, 0x0c(29)\n\t"   // text object -> r3
    "lbz 4, 0x14(30)\n\t",  // current_mode -> r4
    ChecklistRewards_SetBlankTextSisId,
    "",
    0x80181f98               // skip past vanilla lwz + li + bl
)

// Hook at 0x80182170: reward type icon lookup in the icon display function
// (0x801820B4). This function runs every frame to update the reward type icon
// shown next to the reward text.
//
// Vanilla sequence:
//   80182170: lbz r4, 0x14(r29)    -- reward_index
//   80182174: lbz r3, 0x14(r31)    -- mode (current checklist mode — WRONG for cross-mode)
//   80182178: bl 0x80049d10         -- returns reward_type from stc_reward_table_ptrs[mode][reward_index]
//
// For cross-mode rewards, reward_index is the source mode's index but mode is the
// current checklist mode, causing a lookup in the wrong reward table. We hook at
// 0x80182170, return source_mode in r3, let the clobbered instruction reload r4
// (reward_index), and skip past the vanilla mode load to 0x80182178.
static u8 ChecklistRewards_GetHoverSourceMode(void)
{
    return hover.source_mode;
}

CODEPATCH_HOOKCREATE(
    0x80182170,
    "",                         // no prologue needed
    ChecklistRewards_GetHoverSourceMode,
    "",                         // no epilogue needed — r3 = source_mode from return
    0x80182178                  // skip vanilla mode load at 0x80182174, go straight to bl
)

// Post-reward-loop hook: set has_reward on cross-mode reward checkboxes, and
// grant a checkbox filler when the cross-mode placement is a REWARD_FILLER.
//
// Vanilla's reward loop in Checklist_SetRewardFlagOnUnlocks only iterates the
// current mode's reward table, and only grants a filler when the reward_index
// appears in stc_special_rewards[current_mode] — which in vanilla is just a
// hardcoded {0,1,2,3,4} list pointing at the first 5 rewards of each mode,
// all of which happen to have reward_type == REWARD_FILLER. It's functionally
// equivalent to "grant a filler iff the reward is REWARD_FILLER."
//
// Cross-mode placements are skipped by ShouldSkipReward in the vanilla loop,
// so we replicate both has_reward and the filler grant here. We check
// reward_type directly instead of scanning stc_special_rewards — simpler,
// and the filler goes to current_mode (the mode whose cell was completed)
// per the semantic "complete an objective, earn a filler in that checklist."
static void ChecklistRewards_ApplyCrossModeHasReward(u8 current_mode)
{
    GameClearData *cd = gmGetClearcheckerTypeP(current_mode);
    for (int ck = 0; ck < CLEAR_KIND_NUM; ck++)
    {
        CrossModeSlot *slot = &cross_mode_slots[current_mode][ck];
        if (slot->source_mode == 0xFF)
            continue;
        if (cd->clear[ck].has_reward)
            continue;  // Already processed on a prior pass — don't double-grant.
        if (!(cd->clear[ck].is_unlocked || cd->clear[ck].is_filler))
            continue;

        cd->clear[ck].has_reward = 1;

        RewardEntry *src = &stc_reward_table_ptrs[slot->source_mode][slot->source_reward_index];
        if (src->reward_type == REWARD_FILLER)
            Checklist_GrantFiller((GameMode)current_mode);
    }
}

// Hook after the reward loop in Checklist_SetRewardFlagOnUnlocks.
// The outer loop condition at 0x8017e078 branches back to 0x8017dfd8.
// When the loop exits (reward_index >= count), execution falls through to 0x8017e07c.
// Clobbered instruction: lbz r0, 0(r31)
CODEPATCH_HOOKCREATE(
    0x8017e07c,
    "lbz 3, 0x14(30)\n\t",  // current_mode -> r3
    ChecklistRewards_ApplyCrossModeHasReward,
    "lbz 0, 0(31)\n\t",     // re-execute clobbered instruction
    0
)

// Re-grant all received rewards so their checklist slots are correctly marked.
// Called after restoring from save and after applying new location data.
static void RegrantAllReceivedRewards(void)
{
    for (int mode = 0; mode < GMMODE_NUM; mode++)
    {
        u64 received = ap_save->received_checklist_rewards[mode];
        while (received)
        {
            int idx = __builtin_ctzll(received);
            ChecklistRewards_Grant(mode, (u8)idx);
            received &= received - 1;
        }
    }
}


// Allocate writable copies of the per-mode reward tables and redirect
// stc_reward_table_ptrs at them. Must be called from OnBoot so allocations
// persist for the entire runtime. After this runs, stc_reward_table_ptrs[m]
// is the canonical handle to the mod's mutable copy.
static void AllocateRewardTables(void)
{
    for (int mode = GMMODE_AIRRIDE; mode < GMMODE_NUM; mode++)
    {
        int size = reward_counts[mode] * sizeof(RewardEntry);
        RewardEntry *copy = HSD_MemAlloc(size);
        memcpy(copy, stc_reward_table_ptrs[mode], size);
        stc_reward_table_ptrs[mode] = copy;
    }
    OSReport("[Checklist] Reward tables allocated and pointers redirected\n");
}

// Rebuild stc_reward_table_ptrs[mode][i].clear_kind and cross_mode_slots from
// ap_save->shuffled_rewards. Call after shuffled_rewards changes (save load,
// or after ApplyLocations copies new data in).
static void RebuildRewardTablesFromShuffle(void)
{
    ClearCrossModeSlots();

    for (int source_mode = 0; source_mode < GMMODE_NUM; source_mode++)
    {
        int count = reward_counts[source_mode];
        for (int i = 0; i < count; i++)
        {
            u16 loc = ap_save->shuffled_rewards[source_mode][i];
            if (loc == 0xFFFF)
            {
                // Remote — no local slot. Sentinel clear_kind=0 is safe: our
                // hooks gate every vanilla read on shuffled_rewards != 0xFFFF.
                stc_reward_table_ptrs[source_mode][i].clear_kind = 0;
                continue;
            }

            u8 target_mode = (u8)(loc >> 8);
            u8 clear_kind = (u8)(loc & 0xFF);

            if (target_mode == (u8)source_mode)
            {
                stc_reward_table_ptrs[source_mode][i].clear_kind = clear_kind;
            }
            else
            {
                // Cross-mode: sentinel in source table, real placement tracked
                // in cross_mode_slots.
                stc_reward_table_ptrs[source_mode][i].clear_kind = 0;
                cross_mode_slots[target_mode][clear_kind].source_mode = (u8)source_mode;
                cross_mode_slots[target_mode][clear_kind].source_reward_index = (u8)i;
            }
        }
    }
}

// Debug: simulate the AP client sending location data by filling the
// APData location arrays with a random shuffle. Rewards are
// distributed roughly:
//   ~33% same-mode   (reward stays in its own mode's checklist)
//   ~33% cross-mode  (reward placed in a different mode's checklist)
//   ~33% remote      (sent to another world, no local checkbox)
// Applies immediately via ChecklistRewards_ApplyLocations so callers
// (debug menu, test paths) see the result without waiting a frame.
int ChecklistRewards_GetRewardCount(GameMode mode)
{
    if (mode < 0 || mode >= GMMODE_NUM)
        return 0;
    return reward_counts[mode];
}

u16 ChecklistRewards_GetShuffledReward(GameMode mode, u8 reward_index)
{
    if (mode < 0 || mode >= GMMODE_NUM)
        return 0xFFFF;
    if (reward_index >= reward_counts[mode])
        return 0xFFFF;
    return ap_save->shuffled_rewards[mode][reward_index];
}

void ChecklistRewards_DebugSimulateLocationData(void)
{
    // Build per-mode shuffled pools of clear_kinds.
    u8 pools[GMMODE_NUM][CLEAR_KIND_NUM];
    int pool_idxs[GMMODE_NUM] = {0, 0, 0};
    for (int m = 0; m < GMMODE_NUM; m++)
    {
        for (int i = 0; i < CLEAR_KIND_NUM; i++)
            pools[m][i] = (u8)i;
        for (int i = CLEAR_KIND_NUM - 1; i > 0; i--)
        {
            int j = HSD_Randi(i + 1);
            u8 tmp = pools[m][i];
            pools[m][i] = pools[m][j];
            pools[m][j] = tmp;
        }
    }

    for (int mode = 0; mode < GMMODE_NUM; mode++)
    {
        int count = reward_counts[mode];
        int local_count = 0, cross_count = 0;

        for (int i = 0; i < count; i++)
        {
            int roll = HSD_Randi(3);
            if (roll == 0 && pool_idxs[mode] < CLEAR_KIND_NUM)
            {
                // Same-mode: reward stays in its own checklist
                u8 ck = pools[mode][pool_idxs[mode]++];
                ap_data->locations[mode][i] = ((u16)mode << 8) | ck;
                local_count++;
            }
            else if (roll == 1)
            {
                // Cross-mode: reward placed in a different mode's checklist
                int target = (mode + 1 + HSD_Randi(2)) % GMMODE_NUM;
                if (pool_idxs[target] < CLEAR_KIND_NUM)
                {
                    u8 ck = pools[target][pool_idxs[target]++];
                    ap_data->locations[mode][i] = ((u16)target << 8) | ck;
                    cross_count++;
                }
                else
                {
                    ap_data->locations[mode][i] = 0xFFFF;
                }
            }
            else
            {
                // Remote
                ap_data->locations[mode][i] = 0xFFFF;
            }
        }
        OSReport("[Checklist]   Mode %d: %d same, %d cross, %d remote\n",
                 mode, local_count, cross_count,
                 count - local_count - cross_count);
    }

    ap_data->location_data_valid = 1;
    OSReport("[Checklist] Debug: simulated location data written\n");

    // Apply immediately so the result is visible without waiting for the
    // next frame's AP client poll.
    ChecklistRewards_ApplyLocations();
}

// Full checklist reset for debugging. Returns the mod to the same state as a
// fresh boot with no location assignment and no progress:
//   - Every GameClearData checkbox flag byte cleared (is_visible/is_unlocked/
//     has_reward/is_filler/is_new/etc.), plus the per-mode header counters.
//     grid_mapping is left alone (it's a display layout, not progress).
//   - ap_save: received_checklist_rewards / sent_checks / goal_complete /
//     shuffled_*_rewards all zeroed. shuffled_* arrays are filled with 0xFFFF
//     (the "remote / no placement" sentinel).
//   - ap_data: sent_checks / goal_complete / location_* mirrors cleared,
//     location_data_valid cleared so a stale client write doesn't immediately
//     re-apply.
//   - Mod reward tables reset to clear_kind=0 sentinels, cross_mode_slots emptied.
void ChecklistRewards_DebugClearAll(void)
{
    // 1. Mod-side reward tables and cross-mode slot map.
    for (int mode = GMMODE_AIRRIDE; mode < GMMODE_NUM; mode++)
        for (int i = 0; i < reward_counts[mode]; i++)
            stc_reward_table_ptrs[mode][i].clear_kind = 0;
    ClearCrossModeSlots();
    hover.source_mode = 0;

    // 2. Save-side checklist state (sent_checks + goal delegated to check_detection).
    for (int m = 0; m < GMMODE_NUM; m++)
        ap_save->received_checklist_rewards[m] = 0;
    for (int m = 0; m < GMMODE_NUM; m++)
        for (int i = 0; i < reward_counts[m]; i++)
            ap_save->shuffled_rewards[m][i] = 0xFFFF;
    CheckDetection_ResetAll();

    // 3. Shared-memory mirrors the Python client reads (sent_checks + goal
    // already cleared by ResetAll).
    for (int m = 0; m < GMMODE_NUM; m++)
        for (int i = 0; i < reward_counts[m]; i++)
            ap_data->locations[m][i] = 0xFFFF;
    ap_data->location_data_valid = 0;

    // 4. In-memory GameClearData for each mode: zero checkbox flags + counters.
    // The clear[] entries are packed 8 bitfields into 1 byte each, so a memset
    // of the whole array is a clean full-reset of every flag. The unlock_cache
    // at GameData+0xD50 is left alone — Checklist_BuildUnlockBitfields rebuilds
    // it from the now-empty received_checklist_rewards via our REPLACEFUNC'd
    // ClearChecker_CheckUnlocked the next time it runs.
    for (int m = 0; m < GMMODE_NUM; m++)
    {
        GameClearData *cd = gmGetClearcheckerTypeP((GameMode)m);
        if (!cd)
            continue;
        cd->new_unlock_flag = 0;
        cd->display_state = 0;
        cd->checkbox_filler_num = 0;
        cd->checkbox_filler_list_len = 0;
        memset(cd->clear, 0, sizeof(cd->clear));
    }

    Hoshi_WriteSave();
    OSReport("[Checklist] Debug: cleared ALL checklist data (flags, sent_checks, rewards, shuffle)\n");
}

// Reveal every checkbox across all modes (sets is_visible only — unlock state
// is left alone so the AP flow still drives actual completion).
void RevealAllChecklists(void)
{
    for (int mode = 0; mode < GMMODE_NUM; mode++)
    {
        GameClearData *clear_data = gmGetClearcheckerTypeP(mode);
        for (int i = 0; i < CLEAR_KIND_NUM; i++)
            clear_data->clear[i].is_visible = 1;
    }
    OSReport("[Checklist] All checklist squares revealed (%d modes x %d squares)\n",
             GMMODE_NUM, CLEAR_KIND_NUM);
}

// Install all checklist hooks. Call from OnBoot.
void ChecklistRewards_OnBoot()
{
    AllocateRewardTables();

    // Core reward system hooks
    CODEPATCH_REPLACEFUNC(ClearChecker_CheckUnlocked, ChecklistRewards_CheckUnlocked);
    CODEPATCH_REPLACEFUNC(ClearChecker_GetRewardFromClearKind, ChecklistRewards_GetRewardFromClearKind);
    CODEPATCH_HOOKAPPLY(0x8017dfd8);  // Skip remote rewards in SetRewardFlagOnUnlocks
    CODEPATCH_HOOKAPPLY(0x8017e07c);  // Post-reward-loop: apply cross-mode has_reward
    CODEPATCH_HOOKAPPLY(0x80180508);  // Cross-mode audio preview (source mode's audio table)

    // Multi-SIS loading: NOP the 3 original per-mode Text_LoadSisFile calls,
    // and hook the convergence point to load all 3 SIS files.
    CODEPATCH_REPLACEINSTRUCTION(0x80182378, 0x60000000); // NOP: AR bl Text_LoadSisFile
    CODEPATCH_REPLACEINSTRUCTION(0x8018238c, 0x60000000); // NOP: TR bl Text_LoadSisFile
    CODEPATCH_REPLACEINSTRUCTION(0x801823a0, 0x60000000); // NOP: CT bl Text_LoadSisFile
    CODEPATCH_HOOKAPPLY(0x801823c4);  // Load all 3 SIS files

    // Checklist_UpdateCellInfo hooks for cross-mode reward display
    CODEPATCH_HOOKAPPLY(0x80181ee4);  // Cross-mode reward lookup
    CODEPATCH_HOOKAPPLY(0x8018201c);  // Cross-mode reward text display
    CODEPATCH_HOOKAPPLY(0x80181f8c);  // Blank text sis_id fix
    CODEPATCH_HOOKAPPLY(0x80182170);  // Cross-mode reward type icon
    OSReport("[Checklist] Hooks installed\n");

    ClearCrossModeSlots();
}

// Initialize checklist-owned save fields on fresh save creation.
// 0xFFFF is the "no local placement" sentinel. Zero would alias a valid
// (mode=AR, clear_kind=0) placement, so the shuffle arrays need an explicit
// fill after the top-level memset of ap_save.
void ChecklistRewards_OnSaveInit(void)
{
    for (int m = 0; m < GMMODE_NUM; m++)
        for (int i = 0; i < REWARD_COUNT_MAX; i++)
            ap_save->shuffled_rewards[m][i] = 0xFFFF;
}

// Restore reward tables and received rewards from save data.
// Call from OnSaveLoaded (handles both first boot and subsequent boots).
void ChecklistRewards_OnSaveLoaded(void)
{
    RebuildRewardTablesFromShuffle();
    RegrantAllReceivedRewards();
    OSReport("[Checklist] Checklist rewards restored from save\n");
}

// Apply the AP location assignment written by the Python client to APData.
// Copies the new assignment into save, rebuilds derived state, and re-applies
// grants (so rewards received before the assignment arrived land on their cells).
void ChecklistRewards_ApplyLocations()
{
    for (int m = 0; m < GMMODE_NUM; m++)
    {
        int count = reward_counts[m];
        for (int i = 0; i < count; i++)
            ap_save->shuffled_rewards[m][i] = ap_data->locations[m][i];
    }

    RebuildRewardTablesFromShuffle();
    RegrantAllReceivedRewards();

    ap_data->location_data_valid = 0;
    Hoshi_WriteSave();
    OSReport("[Checklist] AP location assignment applied to checklist reward tables\n");
}
