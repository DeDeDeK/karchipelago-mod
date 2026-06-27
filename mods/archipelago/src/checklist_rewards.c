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
#include "textbox_api.h"

// Per-mode reward table sizes (REWARD_COUNT_AIRRIDE / TOPRIDE / CITYTRIAL).
static const int reward_counts[GMMODE_NUM] = {
    [GMMODE_AIRRIDE]   = REWARD_COUNT_AIRRIDE,
    [GMMODE_TOPRIDE]   = REWARD_COUNT_TOPRIDE,
    [GMMODE_CITYTRIAL] = REWARD_COUNT_CITYTRIAL,
};

// ap_to_game_ri[mode][ap_reward_index] = game reward-table index. The apworld
// numbers rewards in clear_kind-sorted order; the game's internal table is in a
// different ROM-defined order that all mod machinery is keyed on. We keep game
// order internally and translate only at the AP-client wire boundaries (incoming
// item IDs in ap_item_handler.c, the locations[] array in ApplyLocations). Built
// at boot by ranking each reward's native clear_kind.
static u8 ap_to_game_ri[GMMODE_NUM][REWARD_COUNT_MAX];

// Build ap_to_game_ri from the original reward tables' native clear_kinds.
// Must run before RebuildRewardTablesFromShuffle overwrites the clear_kind
// field with placement/sentinel values.
static void BuildRewardIndexMaps(void)
{
    for (int mode = 0; mode < GMMODE_NUM; mode++)
    {
        int count = reward_counts[mode];
        const RewardEntry *tbl = stc_reward_table_ptrs[mode];

        // order[r] = the game index with the r-th smallest native clear_kind.
        u8 order[REWARD_COUNT_MAX];
        for (int i = 0; i < count; i++)
            order[i] = (u8)i;
        for (int a = 0; a < count - 1; a++)
        {
            int lo = a;
            for (int b = a + 1; b < count; b++)
                if (tbl[order[b]].clear_kind < tbl[order[lo]].clear_kind)
                    lo = b;
            u8 t = order[a]; order[a] = order[lo]; order[lo] = t;
        }
        for (int ap_ri = 0; ap_ri < count; ap_ri++)
            ap_to_game_ri[mode][ap_ri] = order[ap_ri];
    }
}

// Translate an AP (clear_kind-sorted) reward index to the game reward-table
// index. Out-of-range inputs pass through unchanged - callers range-check
// against ChecklistRewards_GetRewardCount first.
u8 ChecklistRewards_ApToGameIndex(GameMode mode, u8 ap_reward_index)
{
    if ((unsigned)mode >= GMMODE_NUM || ap_reward_index >= reward_counts[mode])
        return ap_reward_index;
    return ap_to_game_ri[mode][ap_reward_index];
}

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
// every vanilla-facing read of RewardEntry.clear_kind must gate on - cross-mode
// source rows store the 0 sentinel in their clear_kind field and must never
// cause a read of the source mode's clear[0].
static int IsSameModeLocalPlacement(u8 mode, u8 reward_index)
{
    u16 loc = ap_save->shuffled_rewards[mode][reward_index];
    return loc != 0xFFFF && (u8)(loc >> 8) == mode;
}

// Replacement for ClearChecker_CheckUnlocked (0x80049E24), via CODEPATCH_REPLACEFUNC.
// AP delivery is the sole authority: true iff the reward's received bit is set. No
// has_reward fallback - has_reward is also raised by in-game completion, which would
// unlock the non-gated cosmetic rewards before the AP server delivers them.
int ChecklistRewards_CheckUnlocked(GameMode mode, u8 reward_index)
{
    return (ap_save->received_checklist_rewards[mode] & (1ULL << reward_index)) != 0;
}

// Replacement for ClearChecker_GetRewardFromClearKind (0x80049EC4). Sole caller
// is the audio/ending preview path in Checklist_Think (0x801804dc). A vanilla scan
// of RewardEntry.clear_kind would alias the cross-mode/remote clear_kind=0 sentinel
// and return the wrong row; we resolve via shuffled_rewards instead and return the
// source row's reward_index + reward_param. Cross-mode music previews are redirected
// to the source mode's audio table by the AudioPreview hook (via hover.source_mode);
// cross-mode ending previews need no special handling (vanilla's ending "preview"
// only sets a UI state byte and plays a menu click).
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
// in vanilla's Checklist_ProcessUnlock (0x8017e490) - reward_param is 0 for
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

// Route a vanilla checklist reward into the mod's gate masks. Called from
// ChecklistRewards_Grant so the gate bit flips whether the reward arrived as an AP
// item or by earning the checkbox; without it, gated rewards write only to the dead
// in-game cache our gate hooks bypass. Non-gated types (fillers, sound test, music,
// endings, extra rules, bonus movies, pause power-ups) are left to vanilla.
static void ApplyVanillaRewardUnlock(GameMode mode, u8 reward_index, u8 reward_type)
{
    switch (reward_type)
    {
        case REWARD_COURSE: // Nebula Belt (Air Ride)
            GateAirRideStages_UnlockStage(AIRRIDE_NEBULA_BELT, /*announce=*/0);
            break;

        case REWARD_STADIUM:
            if (mode == GMMODE_CITYTRIAL)
            {
                int st = CtRewardIndexToStadium(reward_index);
                if (st >= 0)
                    GateStadiums_UnlockStadium((StadiumKind)st, /*announce=*/0);
            }
            break;

        case REWARD_MACHINE_WINGED_STAR:     GateMachines_UnlockMachine(VCKIND_WINGED, 0);         break;
        case REWARD_MACHINE_WAGON_STAR:      GateMachines_UnlockMachine(VCKIND_WAGON, 0);          break;
        case REWARD_MACHINE_SWERVE_STAR:     GateMachines_UnlockMachine(VCKIND_SWERVE, 0);         break;
        case REWARD_MACHINE_BULK_STAR:       GateMachines_UnlockMachine(VCKIND_BULK, 0);           break;
        case REWARD_MACHINE_WHEELIE_BIKE:    GateMachines_UnlockMachine(VCKIND_WHEELIEBIKE, 0);    break;
        case REWARD_MACHINE_SLICK_STAR:      GateMachines_UnlockMachine(VCKIND_SLICK, 0);          break;
        case REWARD_MACHINE_FORMULA_STAR:    GateMachines_UnlockMachine(VCKIND_FORMULA, 0);        break;
        case REWARD_MACHINE_SHADOW_STAR:     GateMachines_UnlockMachine(VCKIND_SHADOW, 0);         break;
        case REWARD_MACHINE_WHEELIE_SCOOTER: GateMachines_UnlockMachine(VCKIND_WHEELIESCOOTER, 0); break;
        case REWARD_MACHINE_ROCKET_STAR:     GateMachines_UnlockMachine(VCKIND_ROCKET, 0);         break;
        case REWARD_MACHINE_TURBO_STAR:      GateMachines_UnlockMachine(VCKIND_TURBO, 0);          break;
        case REWARD_MACHINE_JET_STAR:        GateMachines_UnlockMachine(VCKIND_JET, 0);            break;
        case REWARD_MACHINE_REX_WHEELIE:     GateMachines_UnlockMachine(VCKIND_REXWHEELIE, 0);     break;

        // Character rewards resolve through CharacterDesc_GetMachineKind in the
        // AR-character availability gate, so unlocking the machine unlocks the
        // character. WHEELDEDEDE is the player-facing Dedede (WHEELVSDEDEDE is
        // stadium CPU-only, no AP unlock).
        case REWARD_KING_DEDEDE:
            GateMachines_UnlockMachine(VCKIND_WHEELDEDEDE, 0);
            break;
        case REWARD_META_KNIGHT:
            GateMachines_UnlockMachine(VCKIND_WINGMETAKNIGHT, 0);
            break;

        case REWARD_DRAGOON: GateMachines_UnlockMachine(VCKIND_DRAGOON, 0); break;
        case REWARD_HYDRA:   GateMachines_UnlockMachine(VCKIND_HYDRA, 0);   break;

        case REWARD_COLOR_GREEN:  GateColors_UnlockColor(KIRBYCOLOR_GREEN, 0);  break;
        case REWARD_COLOR_PURPLE: GateColors_UnlockColor(KIRBYCOLOR_PURPLE, 0); break;
        case REWARD_COLOR_BROWN:  GateColors_UnlockColor(KIRBYCOLOR_BROWN, 0);  break;
        case REWARD_COLOR_WHITE:  GateColors_UnlockColor(KIRBYCOLOR_WHITE, 0);  break;

        // TR "New Item" rewards: TopRide_OnCourseSelect reads the checklist
        // has_reward for indices 8/9/10 to drive ItemMgr.enabled_mask bits 20/18/15.
        case REWARD_ITEM_CHICKIE:   GateTopRideItems_UnlockItem(TRITEM_CHICKIE, 0);   break;
        case REWARD_ITEM_WHO_PAINT: GateTopRideItems_UnlockItem(TRITEM_WHO_PAINT, 0); break;
        case REWARD_ITEM_LANTERN:   GateTopRideItems_UnlockItem(TRITEM_LANTERN, 0);   break;

        default:
            // Not gated, left to vanilla: fillers, bonus movie, extra rule, sound
            // test, music, ending, pause power-ups, and DRAGOON_PART_*/HYDRA_PART_*
            // (checklist-internal "all parts" markers, distinct from the in-round
            // ITUNLOCK_DRAGOON*/HYDRA* piece-spawn gates).
            break;
    }
}

// Announce a checkbox filler grant: "Received: Checkbox Filler (<Mode>)". Shared
// by the direct AP filler-item path (ap_item_handler) and the checklist-reward
// filler path, so wording/coloring stay identical however a filler arrives.
void Checklist_AnnounceFiller(GameMode mode)
{
    const char *mode_name;
    GXColor mode_color;
    switch (mode)
    {
        case GMMODE_AIRRIDE:   mode_name = "Air Ride";   mode_color = tb_api->ModeColors[GMMODE_AIRRIDE];   break;
        case GMMODE_TOPRIDE:   mode_name = "Top Ride";   mode_color = tb_api->ModeColors[GMMODE_TOPRIDE];   break;
        case GMMODE_CITYTRIAL: mode_name = "City Trial"; mode_color = tb_api->ModeColors[GMMODE_CITYTRIAL]; break;
        default:               mode_name = "Checklist";  mode_color = tb_api->DefaultColor;                 break;
    }

    TextSegment segs[5] = {
        {"Received: ",      tb_api->DefaultColor},
        {"Checkbox Filler", tb_api->FillerColor},
        {" (",              tb_api->DefaultColor},
        {mode_name,         mode_color},
        {")",               tb_api->DefaultColor},
    };
    tb_api->EnqueueSegments(segs, 5);
}

// Per-(mode, reward_index) display names for the textbox noun shown when a
// checklist reward is granted - the single source of truth, including gated
// categories (their gate handlers run announce=0 for checklist grants). Filler
// slots are NULL; AnnounceChecklistReward handles REWARD_FILLER before the lookup.
static const char *const stc_checklist_reward_names[GMMODE_NUM][REWARD_COUNT_MAX] = {
    [GMMODE_AIRRIDE] = {
        // 0-4 Fillers (handled specially)
        NULL, NULL, NULL, NULL, NULL,
        // 5-14 Sound Test
        "Fantasy Meadows", "Magma Flows", "Sky Sands", "Frozen Hillside", "Beanstalk Park",
        "Celestial Valley", "Machine Passage", "Checker Knights", "Nebula Belt", "Results Screen",
        // 15-18 Color
        "Green Kirby", "Purple Kirby", "Brown Kirby", "White Kirby",
        // 19-31 Machine
        "Winged Star", "Wagon Star", "Swerve Star", "Bulk Star", "Wheelie Bike",
        "Slick Star", "Formula Star", "Shadow Star", "Wheelie Scooter", "Rocket Star",
        "Turbo Star", "Jet Star", "Rex Wheelie",
        // 32-33 Character
        "King Dedede", "Meta Knight",
        // 34 Course
        "Nebula Belt",
        // 35 Bonus Movie, 36 Ending
        "Bonus Movie", "Ending",
        // 37-45 Music
        "Meadows", "Magma", "Sky Sands", "Hillside", "Beanstalk",
        "Celestial", "Machine", "Checker", "Nebula",
    },
    [GMMODE_TOPRIDE] = {
        // 0-4 Fillers
        NULL, NULL, NULL, NULL, NULL,
        // 5-7 Extra Rule
        "Diagonal Camera Angle", "Side Camera Angle", "Device Quantity",
        // 8-10 TR Item
        "Chickie", "Who? Paint", "Lantern",
        // 11-12 Extra Rule
        "Mystery Item Set", "Attack Item Set",
        // 13-20 Sound Test
        "Grass", "Sand", "Sky", "Fire", "Light", "Water", "Metal", "Results Screen",
        // 21-24 Color
        "Green Kirby", "Purple Kirby", "Brown Kirby", "White Kirby",
        // 25 Ending
        "Ending",
        // 26-32 Music
        "Grass", "Sky", "Fire", "Water", "Metal", "Sand", "Light",
    },
    [GMMODE_CITYTRIAL] = {
        // 0-4 Fillers
        NULL, NULL, NULL, NULL, NULL,
        // 5-20 Sound Test
        "City Trial", "Legendary Air Ride Machine", "Dyna Blade Intro", "Tac Challenge",
        "Flying Meteor", "Huge Pillar", "Station Fire", "What's in the Box?",
        "The Lighthouse Light Burns", "Rowdy Charge Tank", "Item Bounce", "Dense Fog Today",
        "Drag Race", "Air Glider", "Target Flight", "Kirby Melee",
        // 21-24 Color
        "Green Kirby", "Purple Kirby", "Brown Kirby", "White Kirby",
        // 25 Music, 26 Ending
        "City", "Ending",
        // 27-29 Dragoon Parts, 30 Dragoon
        "Dragoon Part A", "Dragoon Part B", "Dragoon Part C", "Dragoon",
        // 31-33 Hydra Parts, 34 Hydra
        "Hydra Part X", "Hydra Part Y", "Hydra Part Z", "Hydra",
        // 35-36 Character
        "King Dedede", "Meta Knight",
        // 37-42 Stadium
        "Drag Race 4", "Kirby Melee 2", "Destruction Derby 3", "Destruction Derby 4",
        "Destruction Derby 5", "Single Race: Nebula Belt",
        // 43 Pause Power-ups
        "Pause Power-ups",
    },
};

static const char *ChecklistRewardName(GameMode mode, u8 reward_index)
{
    if ((unsigned)mode >= GMMODE_NUM || reward_index >= reward_counts[mode])
        return NULL;
    return stc_checklist_reward_names[mode][reward_index];
}

// Textbox prefix + noun color for a checklist reward, keyed by reward type.
// Gate-unlock categories (Stadium, Course, Machine, Character, Color, TR Item) read
// "Unlocked <category>: <name>" in their gate handler's noun color; everything else
// reads "Received…" (extras keep a category, single-instance features don't).
static void ChecklistRewardStyle(u8 reward_type, const char **out_prefix, GXColor *out_color)
{
    *out_prefix = "Received: ";
    *out_color  = tb_api->RewardColor;

    if (reward_type == REWARD_SOUND_TEST)
        *out_prefix = "Received Sound Test: ";
    else if (reward_type == REWARD_MUSIC)
        *out_prefix = "Received Music: ";
    else if (reward_type == REWARD_EXTRA_RULE)
        *out_prefix = "Received Extra Rule: ";
    else if (reward_type == REWARD_STADIUM)
    {
        *out_prefix = "Unlocked Stadium: ";
        *out_color  = tb_api->StadiumColor;
    }
    else if (reward_type == REWARD_COURSE)
    {
        *out_prefix = "Unlocked Course: ";
        *out_color  = tb_api->StageColor;
    }
    else if ((reward_type >= REWARD_MACHINE_WINGED_STAR && reward_type <= REWARD_MACHINE_REX_WHEELIE) ||
             reward_type == REWARD_DRAGOON || reward_type == REWARD_HYDRA)
    {
        *out_prefix = "Unlocked Machine: ";
        *out_color  = tb_api->MachineColor;
    }
    else if (reward_type == REWARD_KING_DEDEDE || reward_type == REWARD_META_KNIGHT)
    {
        *out_prefix = "Unlocked Character: ";
        *out_color  = tb_api->MachineColor;
    }
    else if (reward_type >= REWARD_COLOR_GREEN && reward_type <= REWARD_COLOR_WHITE)
    {
        *out_prefix = "Unlocked Color: ";
        *out_color  = tb_api->KirbyColors[KIRBYCOLOR_GREEN + (reward_type - REWARD_COLOR_GREEN)];
    }
    else if (reward_type >= REWARD_ITEM_CHICKIE && reward_type <= REWARD_ITEM_LANTERN)
    {
        *out_prefix = "Unlocked TR Item: ";
        *out_color  = tb_api->TopRideItemColor;
    }
    // DRAGOON_PART_*/HYDRA_PART_*, BONUS_MOVIE, ENDING, and PAUSE_POWERUPS all
    // fall through to the "Received: " + RewardColor default.
}

// Announce a checklist reward on the TextBox - the single announce site (the gate
// handlers ApplyVanillaRewardUnlock invokes run announce=0). Fillers keep their own
// wording via Checklist_AnnounceFiller.
static void AnnounceChecklistReward(GameMode mode, u8 reward_index, u8 reward_type)
{
    if (reward_type == REWARD_FILLER)
    {
        Checklist_AnnounceFiller(mode);
        return;
    }

    const char *name = ChecklistRewardName(mode, reward_index);
    if (!name)
        return; // No display name (unexpected for a non-filler) - stay silent.

    const char *prefix;
    GXColor color;
    ChecklistRewardStyle(reward_type, &prefix, &color);
    tb_api->EnqueueColoredNoun(prefix, name, color, NULL);
}

// Grant a checklist reward received from the AP server. Sets the AP bitfield and
// marks the local checklist slot if one is assigned. The unlock_cache at
// GameData+0xD50 is rebuilt by Checklist_BuildUnlockBitfields via our REPLACEFUNC'd
// ClearChecker_CheckUnlocked, so it picks up the new bit without a write here.
void ChecklistRewards_Grant(GameMode mode, u8 reward_index, int announce)
{
    ap_save->received_checklist_rewards[mode] |= (1ULL << reward_index);

    // reward_type survives all cross-mode / shuffle remapping (only clear_kind
    // is overwritten), so this lookup is always valid.
    u8 reward_type = stc_reward_table_ptrs[mode][reward_index].reward_type;
    OSReport("[Checklist] Granted mode=%d ri=%d type=%s (%d) announce=%d\n",
             mode, reward_index, Reward_TypeName(reward_type), reward_type, announce);
    if (announce)
        AnnounceChecklistReward(mode, reward_index, reward_type);
    ApplyVanillaRewardUnlock(mode, reward_index, reward_type);

    // shuffled u16 = (target_mode << 8) | target_clear_kind, 0xFFFF = remote. We
    // write has_reward only (display badge); is_unlocked is reserved for "player
    // completed this in gameplay" and owned by check_detection.c.
    u16 loc = ap_save->shuffled_rewards[mode][reward_index];
    if (loc != 0xFFFF)
    {
        GameClearData *cd = gmGetClearcheckerTypeP((GameMode)(loc >> 8));
        cd->clear[loc & 0xFF].has_reward = 1;
    }

    // A FILLER reward grants one filler token to the reward's OWN mode, on a real
    // receipt only (announce=1): the replay path (announce=0) must not re-grant, as
    // checkbox_filler_num lives in GameClearData and persists across boots. This is
    // the sole grant site - vanilla's reward-loop grant is neutralized at 0x8017e00c.
    if (reward_type == REWARD_FILLER && announce)
        Checklist_GrantFiller(mode);
}

// Reward types with no gate mask of their own - their unlocked state lives entirely
// in received_checklist_rewards. These are the non-progression rewards the AP world
// drops from the pool when checklist_rewards_gated is off. Excludes the gated
// categories (machines/colors/stadiums/etc.) and the DRAGOON/HYDRA part markers.
static int IsCosmeticRewardType(u8 reward_type)
{
    switch (reward_type)
    {
        case REWARD_FILLER:
        case REWARD_BONUS_MOVIE:
        case REWARD_EXTRA_RULE:
        case REWARD_SOUND_TEST:
        case REWARD_MUSIC:
        case REWARD_ENDING:
        case REWARD_PAUSE_POWERUPS:
            return 1;
        default:
            return 0;
    }
}

// checklist_rewards_gated off: mark every cosmetic reward received at connect so the
// content is available from the start and its box is freed for an ordinary AP item.
// Not routed through Grant - no textbox, and no filler-token bump for REWARD_FILLER.
void ChecklistRewards_GrantAllCosmetic(void)
{
    int total = 0;
    for (int mode = 0; mode < GMMODE_NUM; mode++)
    {
        int count = reward_counts[mode];
        for (int ri = 0; ri < count; ri++)
        {
            if (IsCosmeticRewardType(stc_reward_table_ptrs[mode][ri].reward_type))
            {
                ap_save->received_checklist_rewards[mode] |= (1ULL << ri);
                total++;
            }
        }
    }
    OSReport("[Checklist] Checklist rewards ungated - auto-granted %d cosmetic reward(s)\n", total);
}

// Filter for the reward loop in Checklist_SetRewardFlagOnUnlocks (0x8017DF5C).
// Returns 1 to skip every reward that isn't a same-mode local placement - remote
// and cross-mode-source rows carry the clear_kind=0 sentinel, which would spuriously
// set has_reward on clear[mode][0]. Cross-mode is handled by ApplyCrossModeHasReward.
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

// Checklist audio preview (Checklist_Think, 0x80180508). Replaces the vanilla scan
// of audio_table_ptrs[current_mode]. Under shuffle a cross-mode music reward's source
// reward_index lives in another mode's table, so we select the table via
// hover.source_mode (set by the FindRewardForCell hook) before looking up the song,
// playing it, and persisting to MainMenuData.soundtest_bgm_kind (GameData+0x4E).
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
// Always alt-exits to 0x80180560 - past the vanilla scan, BGM_Play call, and
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

// Hook at 0x80181ee4 in Checklist_UpdateCellInfo: replaces the vanilla reward_index
// reverse-lookup scan entirely (which would alias cross-mode sentinel rows against
// clear_kind=0). Resolves cross-mode via cross_mode_slots and same-mode via the
// shuffled encoding, and snapshots the hovered cell into `hover`. Returns
// reward_index + 1 if a reward is visible, -1 otherwise.
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

// Hook at 0x8018201c: reward text display in Checklist_UpdateCellInfo. Replaces the
// vanilla lwz/addi/bl Text_InitPremadeText sequence (text_index = reward_index +
// 0x7D) to switch sis_id to the source mode's slot for cross-mode rewards.
static void ChecklistRewards_DisplayRewardText(Text *text, int reward_index, u8 current_mode)
{
    // Temporarily switch sis_id to the source mode's slot to read the correct
    // text command data, then restore to slot 0 so Text_GX uses slot 0's
    // glyph/font data for rendering (all checklist SIS files share the same font).
    u8 source_slot = mode_to_sis_slot[hover.source_mode];
    text->sis_id = source_slot;
    Text_InitPremadeText(text, reward_index + 0x7D);
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

// Hook at 0x80181f8c (blank text path): reset sis_id to 0 in case a prior cross-mode
// hover left it pointing at the source mode's slot, then show the blank text (0x7C).
static void ChecklistRewards_SetBlankTextSisId(Text *text, u8 current_mode)
{
    text->sis_id = 0; // Slot 0 is always the current mode
    Text_InitPremadeText(text, 0x7C);
}

CODEPATCH_HOOKCREATE(
    0x80181f8c,
    "lwz 3, 0x0c(29)\n\t"   // text object -> r3
    "lbz 4, 0x14(30)\n\t",  // current_mode -> r4
    ChecklistRewards_SetBlankTextSisId,
    "",
    0x80181f98               // skip past vanilla lwz + li + bl
)

// Hook at 0x80182170: reward type icon lookup (icon display fn 0x801820B4). Vanilla
// reads reward_type from stc_reward_table_ptrs[current_mode][reward_index], but for
// cross-mode rewards reward_index belongs to the source mode. We return source_mode
// in r3 and skip the vanilla mode load (0x80182174) so the lookup uses the right table.
static u8 ChecklistRewards_GetHoverSourceMode(void)
{
    return hover.source_mode;
}

CODEPATCH_HOOKCREATE(
    0x80182170,
    "",                         // no prologue needed
    ChecklistRewards_GetHoverSourceMode,
    "",                         // no epilogue needed - r3 = source_mode from return
    0x80182178                  // skip vanilla mode load at 0x80182174, go straight to bl
)

// Post-reward-loop hook: mirror has_reward onto cross-mode reward checkboxes for
// their display badge. Vanilla's reward loop only iterates the current mode's table
// and skips cross-mode placements (ShouldSkipReward), so they're set here. Mirrors
// has_reward only; the filler token is granted at AP receipt in ChecklistRewards_Grant.
static void ChecklistRewards_ApplyCrossModeHasReward(u8 current_mode)
{
    // The AP checklist (mode 3) has no cross-mode slots and cross_mode_slots is sized
    // [GMMODE_NUM]; without this guard the AP tab would read cross_mode_slots[3] OOB
    // and badge any unlocked AP cell with a spurious has_reward.
    if (current_mode >= GMMODE_NUM)
        return;

    GameClearData *cd = gmGetClearcheckerTypeP(current_mode);
    for (int ck = 0; ck < CLEAR_KIND_NUM; ck++)
    {
        CrossModeSlot *slot = &cross_mode_slots[current_mode][ck];
        if (slot->source_mode == 0xFF)
            continue;
        if (cd->clear[ck].has_reward)
            continue;  // Already processed on a prior pass.
        if (!(cd->clear[ck].is_unlocked || cd->clear[ck].is_filler))
            continue;

        cd->clear[ck].has_reward = 1;
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

// Legendary-machine part assembly (Checklist_ProcessUnlock, 0x8017e490, City Trial
// branch only - mode guard cmplwi r3,2 at 0x8017f00c). Vanilla decides "all 3 parts
// collected" (mark cell 0x6D Dragoon / 0x6E Hydra) by ANDing the has_reward bits of
// the part reward cells, which breaks under shuffle (cross-mode/remote parts resolve
// to the clear_kind=0 sentinel). These hooks key the decision off
// received_checklist_rewards instead; on "all received" they fall into vanilla's own
// set-cell logic. reward_index here is the game reward-table index.
#define CT_RI_DRAGOON_PART_A 27
#define CT_RI_DRAGOON_PART_B 28
#define CT_RI_DRAGOON_PART_C 29
#define CT_RI_HYDRA_PART_X   31
#define CT_RI_HYDRA_PART_Y   32
#define CT_RI_HYDRA_PART_Z   33

static int AllCtRewardsReceived(u8 a, u8 b, u8 c)
{
    u64 need = (1ULL << a) | (1ULL << b) | (1ULL << c);
    return (ap_save->received_checklist_rewards[GMMODE_CITYTRIAL] & need) == need;
}

static int Legendary_DragoonPartsReceived(void)
{
    return AllCtRewardsReceived(CT_RI_DRAGOON_PART_A, CT_RI_DRAGOON_PART_B, CT_RI_DRAGOON_PART_C);
}

static int Legendary_HydraPartsReceived(void)
{
    return AllCtRewardsReceived(CT_RI_HYDRA_PART_X, CT_RI_HYDRA_PART_Y, CT_RI_HYDRA_PART_Z);
}

// Hook at 0x8017f044 (top of the Dragoon part-collection check; clobbered insn
// `li r4,28`). Returns nonzero → branch to vanilla's set-clear[0x6D] logic at
// 0x8017f098; returns 0 → branch to the Hydra check at 0x8017f0b4 (skipping the
// has_reward reads entirely).
CODEPATCH_HOOKCONDITIONALCREATE(
    0x8017f044,
    "",
    Legendary_DragoonPartsReceived,
    "",
    0x8017f0b4,   // not all received -> Hydra check
    0x8017f098    // all received     -> vanilla set-clear[0x6D]
)

// Hook at 0x8017f0b4 (top of the Hydra part-collection check; clobbered insn
// `lbz r3,20(r31)`). Returns nonzero → branch to vanilla's set-clear[0x6E] logic
// at 0x8017f10c; returns 0 → branch past it to 0x8017f128.
CODEPATCH_HOOKCONDITIONALCREATE(
    0x8017f0b4,
    "",
    Legendary_HydraPartsReceived,
    "",
    0x8017f128,   // not all received -> continue past Hydra
    0x8017f10c    // all received     -> vanilla set-clear[0x6E]
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
            ChecklistRewards_Grant(mode, (u8)idx, /*announce=*/0);
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
    // Capture the AP<->game reward-index bijection now, while the copies still
    // hold native clear_kinds (RebuildRewardTablesFromShuffle clobbers them).
    BuildRewardIndexMaps();
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
                // Remote - no local slot. Sentinel clear_kind=0 is safe: our
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

// Debug: simulate the AP client sending location data - fill APData locations with
// a random shuffle (~1/3 same-mode, ~1/3 cross-mode, ~1/3 remote) and apply
// immediately via ApplyLocations so callers see the result without a frame wait.
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

// Full checklist reset for debugging - returns the mod to fresh-boot state with no
// location assignment and no progress: every GameClearData checkbox flag + header
// counter cleared (grid_mapping left alone), ap_save received/sent/goal/shuffled
// zeroed (shuffled filled with the 0xFFFF remote sentinel), ap_data mirrors cleared,
// and the mod reward tables + cross_mode_slots reset to empty.
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
    // The unlock_cache at GameData+0xD50 is left alone - Checklist_BuildUnlockBitfields
    // rebuilds it from the now-empty received_checklist_rewards on its next run.
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

// Reveal every checkbox across all modes (sets is_visible only - unlock state
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

    // Neutralize vanilla's reward-loop filler grant: replace `li r0,5` at 0x8017e00c
    // with `b 0x8017e064` (0x48000058) to skip the grant block (which bumps
    // checkbox_filler_num for the {0,1,2,3,4} filler rows) while leaving the preceding
    // has_reward store intact. Filler tokens are granted solely at AP receipt in Grant.
    CODEPATCH_REPLACEINSTRUCTION(0x8017e00c, 0x48000058);

    // Legendary-machine part assembly: decide "all parts collected" from
    // received_checklist_rewards instead of vanilla's placement-dependent
    // has_reward read (see the hooks above).
    CODEPATCH_HOOKAPPLY(0x8017f044);  // Dragoon parts → cell 0x6D
    CODEPATCH_HOOKAPPLY(0x8017f0b4);  // Hydra parts → cell 0x6E

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
    // The client writes locations[m][] indexed by AP (clear_kind-sorted)
    // reward_index; shuffled_rewards is keyed by game reward-table index, so
    // translate as we copy. ap_to_game_ri is a bijection over [0, count), so
    // every game index in range is written exactly once.
    for (int m = 0; m < GMMODE_NUM; m++)
    {
        int count = reward_counts[m];
        for (int ap_ri = 0; ap_ri < count; ap_ri++)
            ap_save->shuffled_rewards[m][ap_to_game_ri[m][ap_ri]] = ap_data->locations[m][ap_ri];
    }

    RebuildRewardTablesFromShuffle();
    RegrantAllReceivedRewards();

    ap_data->location_data_valid = 0;
    Hoshi_WriteSave();
    OSReport("[Checklist] AP location assignment applied to checklist reward tables\n");
}
