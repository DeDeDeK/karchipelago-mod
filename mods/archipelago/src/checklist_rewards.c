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
#include "ap_checks.h"
#include "ap_checklist.h"
#include "ap_check_detect.h"
#include "gate_machines.h"
#include "gate_colors.h"
#include "gate_airride_stages.h"
#include "gate_topride_items.h"
#include "gate_stadiums.h"
#include "textbox_api.h"
#include "ap_colors.h"
#include "ap_announce.h"
#include "ap_patches.h"

static const int reward_counts[GMMODE_NUM] = {
    [GMMODE_AIRRIDE]   = AIRRIDE_REWARD_NUM,
    [GMMODE_TOPRIDE]   = TOPRIDE_REWARD_NUM,
    [GMMODE_CITYTRIAL] = CITYTRIAL_REWARD_NUM,
};

// AP reward_index (the apworld's clear_kind-sorted order) -> game reward-table index,
// which every other path keys on. Translated only at the client wire boundary.
static u8 ap_to_game_ri[GMMODE_NUM][REWARD_COUNT_MAX];

// Before RebuildRewardTablesFromShuffle overwrites the native clear_kinds.
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
        memcpy(ap_to_game_ri[mode], order, count * sizeof(order[0]));
    }
}

u8 ChecklistRewards_ApToGameIndex(GameMode mode, u8 ap_reward_index)
{
    if ((unsigned)mode >= GMMODE_NUM || ap_reward_index >= reward_counts[mode])
        return ap_reward_index;
    return ap_to_game_ri[mode][ap_reward_index];
}

// The reward from another mode placed at a (row, clear_kind); source_mode 0xFF = none.
typedef struct CrossModeSlot
{
    u8 source_mode;
    u8 source_reward_index;
} CrossModeSlot;

// Indexed by checklist-mode row. The AP tab has no native rewards, so all its placements
// live here.
static CrossModeSlot cross_mode_slots[CHECKLIST_MODE_NUM][CLEAR_KIND_NUM];

// Source mode of the hovered cell's reward, for the text, icon and audio hooks. Set by
// ChecklistRewards_FindRewardForCell; 0xFF = unresolved.
static u8 hover_source_mode = 0xFF;

int ChecklistRewards_GetHoveredCell(u8 *out_mode, u8 *out_clear_kind)
{
    GOBJ *gobj = Gm_GetMenuData()->clearchecker.bg_gobj;
    if (!gobj)
        return 0;
    ClearCheckerUI *ui = (ClearCheckerUI *)gobj->userdata;
    if (!ui || ui->cursor_col < 0 || ui->cursor_col >= CHECKLIST_GRID_COLS ||
        ui->cursor_row < 0 || ui->cursor_row >= CHECKLIST_GRID_ROWS)
        return 0;

    GameClearData *cd = gmGetClearcheckerTypeP(ui->mode);
    if (!cd)
        return 0;

    u8 phys_slot = (u8)(ui->cursor_col + ui->cursor_row * CHECKLIST_GRID_COLS);
    for (int k = 0; k < CLEAR_KIND_NUM; k++)
    {
        if (cd->grid_mapping[k] != phys_slot)
            continue;
        *out_mode = (u8)ui->mode;
        *out_clear_kind = (u8)k;
        return 1;
    }
    return 0;
}

// Matches the full u16 encoding; a clear_kind scan would alias the 0 sentinel.
int ChecklistRewards_ResolveCell(u8 mode, u8 clear_kind,
                                 u8 *out_source_mode, u8 *out_source_reward_index)
{
    int row = ChecklistModeRow(mode);
    if (row < 0 || clear_kind >= CLEAR_KIND_NUM)
        return 0;

    CrossModeSlot *slot = &cross_mode_slots[row][clear_kind];
    if (slot->source_mode != 0xFF)
    {
        *out_source_mode = slot->source_mode;
        *out_source_reward_index = slot->source_reward_index;
        return 1;
    }

    if (row >= GMMODE_NUM)
        return 0;

    u16 target = ((u16)row << 8) | clear_kind;
    int count = reward_counts[row];
    for (int ri = 0; ri < count; ri++)
    {
        if (ap_save->shuffled_rewards[row][ri] != target)
            continue;
        *out_source_mode = (u8)row;
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
    for (int r = 0; r < CHECKLIST_MODE_NUM; r++)
        for (int k = 0; k < CLEAR_KIND_NUM; k++)
            cross_mode_slots[r][k].source_mode = 0xFF;
}

// Every vanilla-facing read of RewardEntry.clear_kind gates on this: remote and
// cross-mode rows hold the 0 sentinel there.
static int IsSameModeLocalPlacement(u8 mode, u8 reward_index)
{
    u16 loc = ap_save->shuffled_rewards[mode][reward_index];
    return loc != 0xFFFF && (u8)(loc >> 8) == mode;
}

// Replaces ClearChecker_CheckUnlocked (0x80049e24). AP receipt only: has_reward is also
// raised by in-game completion, which would unlock a reward before the server sends it.
static int ChecklistRewards_CheckUnlocked(GameMode mode, u8 reward_index)
{
    return (ap_save->received_checklist_rewards[mode] & (1ULL << reward_index)) != 0;
}

// Replaces ClearChecker_GetRewardFromClearKind (0x80049ec4), called from the audio and
// ending preview in Checklist_Think at 0x801804dc. Resolves through ResolveCell, since a
// clear_kind scan hits the 0 sentinel.
static void ChecklistRewards_GetRewardFromClearKind(GameMode mode, u8 clear_kind,
                                             u8 *out_reward_index,
                                             u8 *out_reward_param)
{
    if ((unsigned)mode >= GMMODE_NUM || clear_kind >= CLEAR_KIND_NUM)
    {
        *out_reward_index = 0xFF;
        return;
    }

    GameClearData *cd = gmGetClearcheckerTypeP(mode);
    // Vanilla scans only an unlocked or filler'd cell.
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

// City Trial REWARD_STADIUM reward_index -> StadiumKind. reward_param is 0 for every
// REWARD_STADIUM entry; this mirrors the mapping in Checklist_ProcessUnlock (0x8017e490).
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

// Routes a received checklist reward into the gate masks; the gate hooks bypass the
// in-game unlock cache.
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

        // A character gates on its machine. WHEELDEDEDE is the player-facing Dedede;
        // WHEELVSDEDEDE is stadium CPU-only.
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

        case REWARD_ITEM_CHICKIE:   GateTopRideItems_UnlockItem(TRITEM_CHICKIE, 0);   break;
        case REWARD_ITEM_WHO_PAINT: GateTopRideItems_UnlockItem(TRITEM_WHO_PAINT, 0); break;
        case REWARD_ITEM_LANTERN:   GateTopRideItems_UnlockItem(TRITEM_LANTERN, 0);   break;

        // No gate mask; DRAGOON_PART_* / HYDRA_PART_* are checklist-only markers.
        default:
            break;
    }
}

void ChecklistRewards_AnnounceFiller(GameMode mode)
{
    int row = ChecklistModeRow(mode);
    TextSegment segs[5] = {
        {"Received: ",                 tb_api->DefaultColor},
        {"Checkbox Filler",            APColor_Filler},
        {" (",                         tb_api->DefaultColor},
        {APChecklist_RowName(row),     APChecklist_RowColor(row)},
        {")",                          tb_api->DefaultColor},
    };
    APAnnounce_GrantSegments(segs, 5);
}

// Textbox noun per reward; NULL for fillers, which announce separately.
static const char *const stc_checklist_reward_names[GMMODE_NUM][REWARD_COUNT_MAX] = {
    [GMMODE_AIRRIDE] = {
        // 0-4 Fillers
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

static void ChecklistRewardStyle(u8 reward_type, const char **out_prefix, GXColor *out_color)
{
    *out_prefix = "Received: ";
    *out_color  = APColor_Reward;

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
}

// The one announce site: the gate unlockers ApplyVanillaRewardUnlock calls run silent.
static void AnnounceChecklistReward(GameMode mode, u8 reward_index, u8 reward_type)
{
    if (reward_type == REWARD_FILLER)
    {
        ChecklistRewards_AnnounceFiller(mode);
        return;
    }

    const char *name = ChecklistRewardName(mode, reward_index);
    if (!name)
        return;

    const char *prefix;
    GXColor color;
    ChecklistRewardStyle(reward_type, &prefix, &color);
    APAnnounce_Grant(prefix, name, color, NULL);
}

// Sets the badge (has_reward) on the reward's cell; is_unlocked belongs to check
// detection. The target is a checklist-mode row, and the AP tab's clear data exists only
// once it registers.
static void MarkRewardCell(GameMode mode, u8 reward_index)
{
    u16 loc = ap_save->shuffled_rewards[mode][reward_index];
    if (loc == 0xFFFF)
        return;

    u8 row = (u8)(loc >> 8);
    u8 clear_kind = (u8)(loc & 0xFF);
    if (row >= CHECKLIST_MODE_NUM || clear_kind >= CLEAR_KIND_NUM)
        return;
    if (row == AP_CHECKLIST_ROW && !APChecklist_IsRegistered())
        return;

    GameClearData *cd = gmGetClearcheckerTypeP((GameMode)ChecklistRowMode(row));
    if (!cd)
        return;
    cd->clear[clear_kind].has_reward = 1;
}

// No unlock_cache write: Checklist_BuildUnlockBitfields (0x80007af0) rebuilds it through
// ChecklistRewards_CheckUnlocked.
void ChecklistRewards_Grant(GameMode mode, u8 reward_index, int announce)
{
    if ((unsigned)mode >= GMMODE_NUM || reward_index >= reward_counts[mode])
    {
        OSReport("[ChecklistRewards] Grant out of range (mode=%d ri=%d) - ignored\n",
                 mode, reward_index);
        return;
    }

    ap_save->received_checklist_rewards[mode] |= (1ULL << reward_index);

    // The shuffle remaps only clear_kind, so reward_type is always valid.
    u8 reward_type = stc_reward_table_ptrs[mode][reward_index].reward_type;
    if (!ap_regrant_quiet)
        OSReport("[ChecklistRewards] Granted mode=%d ri=%d type=%s (%d)\n",
                 mode, reward_index, Reward_TypeName(reward_type), reward_type);
    if (announce)
        AnnounceChecklistReward(mode, reward_index, reward_type);
    ApplyVanillaRewardUnlock(mode, reward_index, reward_type);

    MarkRewardCell(mode, reward_index);

    // Real receipts only: checkbox_filler_num persists in GameClearData, so a re-grant
    // must not bump it. Vanilla's reward-loop grant is branched over at 0x8017e00c.
    if (reward_type == REWARD_FILLER && announce)
        Checklist_GrantFiller(mode);
}

// Reward types with no gate mask, which the AP world places per `checklist_rewards`
// category.
static int IsPlaceableRewardType(u8 reward_type)
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

// Bypasses Grant: no textbox and no filler token.
void ChecklistRewards_GrantUnplaced(u32 placed_types)
{
    int total = 0;
    for (int mode = 0; mode < GMMODE_NUM; mode++)
    {
        int count = reward_counts[mode];
        for (int ri = 0; ri < count; ri++)
        {
            u8 reward_type = stc_reward_table_ptrs[mode][ri].reward_type;
            int bit = mode * CHECKLIST_REWARD_MODE_BITS + reward_type;
            if (IsPlaceableRewardType(reward_type) && !((placed_types >> bit) & 1))
            {
                ap_save->received_checklist_rewards[mode] |= (1ULL << ri);
                total++;
            }
        }
    }
    OSReport("[ChecklistRewards] Auto-granted %d unplaced reward(s) (placed = %s)\n",
             total, MaskBits(placed_types, GMMODE_NUM * CHECKLIST_REWARD_MODE_BITS));
}

// Reward-loop filter in Checklist_SetRewardFlagOnUnlocks (0x8017df5c): skips remote and
// cross-mode rows, whose 0 sentinel would set has_reward on clear[0].
// ChecklistRewards_ApplyCrossModeHasReward covers the cross-mode ones.
static int ChecklistRewards_ShouldSkipReward(GameMode mode, u8 reward_index)
{
    return !IsSameModeLocalPlacement((u8)mode, reward_index);
}

// Top of the loop body; clobbers lbz r3, 0x14(r30) (the UI mode). Nonzero skips to the
// next iteration at 0x8017e064.
CODEPATCH_HOOKCONDITIONALCREATE(
    0x8017dfd8,
    "lbz 3, 0x14(30)\n\t"
    "mr 4, 27\n\t",
    ChecklistRewards_ShouldSkipReward,
    "",
    0,
    0x8017e064
)

// Replaces the vanilla audio-preview scan, reading the source mode's
// stc_audio_preview_tables entry so a cross-mode music reward plays its own song.
static void ChecklistRewards_AudioPreview(u8 reward_index)
{
    if (hover_source_mode >= GMMODE_NUM)
        return;

    u8 *table = stc_audio_preview_tables[hover_source_mode];
    if (!table)
        return;

    for (int i = 0; table[i * 2] != 0xFF; i++)
    {
        if (table[i * 2] != reward_index)
            continue;
        s8 song_id = (s8)table[i * 2 + 1];
        BGM_Play((u8)song_id);
        Gm_GetGameData()->main_menu.soundtest_bgm_kind = song_id;
        break;
    }
}

// Hook at 0x80180508 in Checklist_Think (0x8017f3bc), on the REWARDPARAM_AUDIO path with
// r4 = reward_index. Exits to 0x80180560, past the vanilla scan, BGM_Play and persist
// helper; the relocated mr r3, r18 is dead there.
CODEPATCH_HOOKCREATE(
    0x80180508,
    "clrlwi 3, 4, 24\n\t",
    ChecklistRewards_AudioPreview,
    "",
    0x80180560
)

static const char *sis_filenames[GMMODE_NUM] = {
    [GMMODE_AIRRIDE]   = "SisClrChk3D.dat",
    [GMMODE_TOPRIDE]   = "SisClrChk2D.dat",
    [GMMODE_CITYTRIAL] = "SisClrChkCT.dat",
};

// GameMode -> SIS slot. Slot 0 is always the current mode, so vanilla code on its default
// sis_id of 0 works; the other two modes take slots 1-2.
static u8 mode_to_sis_slot[GMMODE_NUM];

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

// Hook at 0x801823c4 in Checklist_Init (0x801822f4), after its NOPed per-mode SIS loads.
// r23 = the checklist mode; clobbers lwz r3, 0x0ecc(r30).
CODEPATCH_HOOKCREATE(
    0x801823c4,
    "clrlwi 3, 23, 24\n\t",
    LoadAllChecklistSIS,
    "",
    0
)

// Replaces the reward_index scan in Checklist_UpdateCellInfo, which would alias the 0
// sentinel, and snapshots hover_source_mode. Returns reward_index + 1 if a reward is
// visible, else -1.
static int ChecklistRewards_FindRewardForCell(u8 current_mode, u8 clear_kind)
{
    u8 src_mode, src_ri;
    if (!ChecklistRewards_ResolveCell(current_mode, clear_kind, &src_mode, &src_ri))
    {
        hover_source_mode = current_mode;
        return -1;
    }
    hover_source_mode = src_mode;

    if (ap_save->received_checklist_rewards[src_mode] & (1ULL << src_ri))
        return (int)src_ri + 1;

    // Cross-mode also shows on is_unlocked: the post-loop hook may not have mirrored
    // has_reward yet.
    GameClearData *cd = gmGetClearcheckerTypeP(current_mode);
    if (!cd)
        return -1;
    int visible = (src_mode == current_mode)
        ? cd->clear[clear_kind].has_reward
        : (cd->clear[clear_kind].is_unlocked || cd->clear[clear_kind].has_reward);
    return visible ? (int)src_ri + 1 : -1;
}

// Hook at 0x80181ee4 in Checklist_UpdateCellInfo (0x80181d70), clobbering li r25, 0.
// r3 = mode, r4 = clear_kind (r26). The epilogue turns the result into r0 and exits to
// 0x80181f5c, where vanilla does mr r27, r0.
CODEPATCH_HOOKCREATE(
    0x80181ee4,
    "lbz 3, 0x14(30)\n\t"
    "mr 4, 26\n\t",
    ChecklistRewards_FindRewardForCell,
    "cmpwi 3, 0\n\t"
    "blt 0f\n\t"
    "addi 0, 3, -1\n\t"
    "b 1f\n\t"
    "0:\n\t"
    "li 0, -1\n\t"
    "1:\n\t",
    0x80181f5c
)

// Reads the text from the source mode's SIS slot, then restores slot 0 so Text_GX renders
// with its glyph data (every checklist SIS file shares a font).
static void ChecklistRewards_DisplayRewardText(Text *text, int reward_index)
{
    text->sis_id = hover_source_mode < GMMODE_NUM ? mode_to_sis_slot[hover_source_mode] : 0;
    Text_InitPremadeText(text, CLEARCHECKER_SIS_REWARD_BASE + reward_index);
    text->sis_id = 0;
}

// Hook at 0x8018201c in Checklist_UpdateCellInfo (0x80181d70), replacing the vanilla
// lwz / addi / bl Text_InitPremadeText; r29 + 0x0c = the text, r27 = reward_index.
CODEPATCH_HOOKCREATE(
    0x8018201c,
    "lwz 3, 0x0c(29)\n\t"
    "mr 4, 27\n\t",
    ChecklistRewards_DisplayRewardText,
    "",
    0x80182028
)

// A prior cross-mode hover may have left sis_id on the source mode's slot.
static void ChecklistRewards_SetBlankTextSisId(Text *text)
{
    text->sis_id = 0;
    Text_InitPremadeText(text, CLEARCHECKER_SIS_NO_REWARD);
}

// Hook at 0x80181f8c in Checklist_UpdateCellInfo (0x80181d70), replacing its blank-text
// lwz / li / bl.
CODEPATCH_HOOKCREATE(
    0x80181f8c,
    "lwz 3, 0x0c(29)\n\t",
    ChecklistRewards_SetBlankTextSisId,
    "",
    0x80181f98
)

static u8 ChecklistRewards_GetHoverSourceMode(void)
{
    return hover_source_mode;
}

// Hook at 0x80182170 in Checklist_RewardIconProc (0x801820b4): hands the following bl
// ClearChecker_GetRewardType the source mode, since a cross-mode reward_index indexes
// that mode's table. Exits past the vanilla mode load at 0x80182174.
CODEPATCH_HOOKCREATE(
    0x80182170,
    "",
    ChecklistRewards_GetHoverSourceMode,
    "",
    0x80182178
)

// Replaces the bl ClearChecker_GetRewardParam at 0x8018213c in the same function, which
// decides whether the icon is drawn. It is passed ClearCheckerUI.mode, which on a custom
// tab trips the callee's mode assert.
static u8 ChecklistRewards_GetHoverRewardParam(GameMode mode, u8 reward_index)
{
    if (hover_source_mode >= GMMODE_NUM)
        return 0;
    return stc_reward_table_ptrs[hover_source_mode][reward_index].reward_param;
}

// Mirrors has_reward onto cross-mode cells: the vanilla loop covers only this mode's
// table, and ShouldSkipReward drops cross-mode rows.
static void ChecklistRewards_ApplyCrossModeHasReward(u8 current_mode)
{
    // A custom tab builds under GMMODE_CITYTRIAL while the clear data is already the
    // tab's, so the UI mode would pair City Trial's slots with its board.
    int build_mode = APChecklist_GetBuildMode();
    if (build_mode >= 0)
        current_mode = (u8)build_mode;

    // Custom tabs other than the AP tab have no row.
    int row = ChecklistModeRow(current_mode);
    if (row < 0)
        return;

    GameClearData *cd = gmGetClearcheckerTypeP(current_mode);
    if (!cd)
        return;
    for (int ck = 0; ck < CLEAR_KIND_NUM; ck++)
    {
        CrossModeSlot *slot = &cross_mode_slots[row][ck];
        if (slot->source_mode == 0xFF)
            continue;
        if (cd->clear[ck].has_reward)
            continue;
        if (!(cd->clear[ck].is_unlocked || cd->clear[ck].is_filler))
            continue;

        cd->clear[ck].has_reward = 1;
    }
}

// The reward-loop exit in Checklist_SetRewardFlagOnUnlocks (0x8017df5c); clobbers
// lbz r0, 0(r31).
CODEPATCH_HOOKCREATE(
    0x8017e07c,
    "lbz 3, 0x14(30)\n\t",
    ChecklistRewards_ApplyCrossModeHasReward,
    "",
    0
)

// Checklist_ProcessUnlock (0x8017e490) marks cells 0x6D / 0x6E once all three Dragoon /
// Hydra parts are in, by the part cells' has_reward, which hits the 0 sentinel under
// shuffle. These read received_checklist_rewards instead.
static int AllCtRewardsReceived(u8 a, u8 b, u8 c)
{
    u64 need = (1ULL << a) | (1ULL << b) | (1ULL << c);
    return (ap_save->received_checklist_rewards[GMMODE_CITYTRIAL] & need) == need;
}

static int Legendary_DragoonPartsReceived(void)
{
    return AllCtRewardsReceived(CT_REWARD_DRAGOON_PART_A, CT_REWARD_DRAGOON_PART_B, CT_REWARD_DRAGOON_PART_C);
}

static int Legendary_HydraPartsReceived(void)
{
    return AllCtRewardsReceived(CT_REWARD_HYDRA_PART_X, CT_REWARD_HYDRA_PART_Y, CT_REWARD_HYDRA_PART_Z);
}

// Top of the Dragoon parts check; clobbers li r4, 28.
CODEPATCH_HOOKCONDITIONALCREATE(
    0x8017f044,
    "",
    Legendary_DragoonPartsReceived,
    "",
    0x8017f0b4,   // not all received: on to the Hydra check
    0x8017f098    // all received: vanilla's set-clear[0x6D]
)

// Top of the Hydra parts check; clobbers lbz r3, 20(r31).
CODEPATCH_HOOKCONDITIONALCREATE(
    0x8017f0b4,
    "",
    Legendary_HydraPartsReceived,
    "",
    0x8017f128,   // not all received: past the Hydra check
    0x8017f10c    // all received: vanilla's set-clear[0x6E]
)

// Quietly re-grants every received reward: re-marks its cell and re-applies its gate
// unlock.
static void RegrantAllReceivedRewards(void)
{
    int total = 0;

    ap_regrant_quiet = 1;
    for (int mode = 0; mode < GMMODE_NUM; mode++)
    {
        u64 received = ap_save->received_checklist_rewards[mode];
        while (received)
        {
            int idx = __builtin_ctzll(received);
            ChecklistRewards_Grant(mode, (u8)idx, /*announce=*/0);
            received &= received - 1;
            total++;
        }
    }
    ap_regrant_quiet = 0;

    OSReport("[ChecklistRewards] Re-applied %d received reward(s)\n", total);
}

// Writable copies, since the shuffle rewrites clear_kind, allocated at boot so they
// outlive every scene.
static void AllocateRewardTables(void)
{
    for (int mode = GMMODE_AIRRIDE; mode < GMMODE_NUM; mode++)
    {
        int size = reward_counts[mode] * sizeof(RewardEntry);
        RewardEntry *copy = HSD_MemAlloc(size);
        memcpy(copy, stc_reward_table_ptrs[mode], size);
        stc_reward_table_ptrs[mode] = copy;
    }
    BuildRewardIndexMaps();
}

// Call after any change to shuffled_rewards.
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
                // Remote: the 0 sentinel, which every vanilla read gates out.
                stc_reward_table_ptrs[source_mode][i].clear_kind = 0;
                continue;
            }

            // The wire target is a checklist-mode row, not a runtime mode, so it only
            // needs a bounds check.
            u8 target_row = (u8)(loc >> 8);
            u8 clear_kind = (u8)(loc & 0xFF);

            if (target_row >= CHECKLIST_MODE_NUM || clear_kind >= CLEAR_KIND_NUM)
            {
                stc_reward_table_ptrs[source_mode][i].clear_kind = 0;
                continue;
            }

            if (target_row == (u8)source_mode)
            {
                stc_reward_table_ptrs[source_mode][i].clear_kind = clear_kind;
            }
            else
            {
                stc_reward_table_ptrs[source_mode][i].clear_kind = 0;
                cross_mode_slots[target_row][clear_kind].source_mode = (u8)source_mode;
                cross_mode_slots[target_row][clear_kind].source_reward_index = (u8)i;
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

void ChecklistRewards_DebugSimulateLocationData(void)
{
    int same[GMMODE_NUM] = {0, 0, 0};
    int cross[GMMODE_NUM] = {0, 0, 0};
    u8 pools[GMMODE_NUM][CLEAR_KIND_NUM];
    int pool_idxs[GMMODE_NUM] = {0, 0, 0};
    for (int m = 0; m < GMMODE_NUM; m++)
    {
        for (int i = 0; i < CLEAR_KIND_NUM; i++)
            pools[m][i] = (u8)i;
        RandomShuffle(pools[m], CLEAR_KIND_NUM, sizeof(pools[m][0]));
    }

    for (int mode = 0; mode < GMMODE_NUM; mode++)
    {
        for (int i = 0; i < reward_counts[mode]; i++)
        {
            int roll = HSD_Randi(3);
            int target = (roll == 1) ? (mode + 1 + HSD_Randi(2)) % GMMODE_NUM : mode;
            if (roll < 2 && pool_idxs[target] < CLEAR_KIND_NUM)
            {
                u8 ck = pools[target][pool_idxs[target]++];
                ap_data->locations[mode][i] = ((u16)target << 8) | ck;
                if (target == mode)
                    same[mode]++;
                else
                    cross[mode]++;
            }
            else
            {
                ap_data->locations[mode][i] = 0xFFFF;
            }
        }
    }

    ap_data->location_data_valid = 1;
    OSReport("[ChecklistRewards] Debug: simulated location data (same/cross/remote "
             "AR %d/%d/%d, TR %d/%d/%d, CT %d/%d/%d)\n",
             same[0], cross[0], reward_counts[0] - same[0] - cross[0],
             same[1], cross[1], reward_counts[1] - same[1] - cross[1],
             same[2], cross[2], reward_counts[2] - same[2] - cross[2]);

    ChecklistRewards_ApplyLocations();
}

// grid_mapping is left alone.
static void ClearBoard(GameClearData *cd)
{
    if (!cd)
        return;
    cd->new_unlock_flag = 0;
    cd->display_state = 0;
    cd->checkbox_filler_num = 0;
    cd->checkbox_filler_list_len = 0;
    memset(cd->clear, 0, sizeof(cd->clear));
}

void ChecklistRewards_DebugClearAll(void)
{
    for (int mode = GMMODE_AIRRIDE; mode < GMMODE_NUM; mode++)
        for (int i = 0; i < reward_counts[mode]; i++)
            stc_reward_table_ptrs[mode][i].clear_kind = 0;
    ClearCrossModeSlots();
    hover_source_mode = 0xFF;

    memset(ap_save->received_checklist_rewards, 0, sizeof(ap_save->received_checklist_rewards));
    for (int m = 0; m < GMMODE_NUM; m++)
        for (int i = 0; i < reward_counts[m]; i++)
            ap_save->shuffled_rewards[m][i] = 0xFFFF;
    APChecks_ResetAll();
    APPatches_ResetAll();

    for (int m = 0; m < GMMODE_NUM; m++)
        for (int i = 0; i < reward_counts[m]; i++)
            ap_data->locations[m][i] = 0xFFFF;
    ap_data->location_data_valid = 0;

    // unlock_cache rebuilds from received_checklist_rewards on its own.
    for (int m = 0; m < GMMODE_NUM; m++)
        ClearBoard(gmGetClearcheckerTypeP((GameMode)m));
    if (APChecklist_IsRegistered())
        ClearBoard(gmGetClearcheckerTypeP((GameMode)ap_checklist_mode));
    APCheckDetect_ResetProgress();

    Hoshi_WriteSave();
    OSReport("[ChecklistRewards] Debug: cleared all checklist data (flags, sent_checks, rewards, shuffle, AP progress)\n");
}

// The AP tab reveals only the cells backed by a check.
void ChecklistRewards_Reveal(int mode)
{
    if (mode == AP_CHECKLIST_ROW)
    {
        APChecklist_RevealAll();
        return;
    }
    if (mode < 0 || mode >= GMMODE_NUM)
        return;

    GameClearData *clear_data = gmGetClearcheckerTypeP((GameMode)mode);
    if (!clear_data)
        return;
    for (int i = 0; i < CLEAR_KIND_NUM; i++)
        clear_data->clear[i].is_visible = 1;
}

void ChecklistRewards_RevealAll(void)
{
    for (int mode = 0; mode < CHECKLIST_MODE_NUM; mode++)
        ChecklistRewards_Reveal(mode);

    OSReport("[ChecklistRewards] Debug: revealed all squares (%d rows x %d)\n",
             CHECKLIST_MODE_NUM, CLEAR_KIND_NUM);
}

void ChecklistRewards_OnBoot()
{
    AllocateRewardTables();

    CODEPATCH_REPLACEFUNC(ClearChecker_CheckUnlocked, ChecklistRewards_CheckUnlocked);
    CODEPATCH_REPLACEFUNC(ClearChecker_GetRewardFromClearKind, ChecklistRewards_GetRewardFromClearKind);
    CODEPATCH_HOOKAPPLY(0x8017dfd8);
    CODEPATCH_HOOKAPPLY(0x8017e07c);
    CODEPATCH_HOOKAPPLY(0x80180508);

    // Branches over vanilla's reward-loop filler grant in Checklist_SetRewardFlagOnUnlocks
    // (li r0, 5 -> b 0x8017e064), keeping the has_reward store before it; tokens come only
    // from ChecklistRewards_Grant.
    CODEPATCH_REPLACEINSTRUCTION(0x8017e00c, 0x48000058);

    CODEPATCH_HOOKAPPLY(0x8017f044);
    CODEPATCH_HOOKAPPLY(0x8017f0b4);

    // The per-mode SIS loads in Checklist_Init, replaced by the 0x801823c4 hook.
    CODEPATCH_REPLACEINSTRUCTION(0x80182378, PPC_NOP); // AR bl Text_LoadSisFile
    CODEPATCH_REPLACEINSTRUCTION(0x8018238c, PPC_NOP); // TR bl Text_LoadSisFile
    CODEPATCH_REPLACEINSTRUCTION(0x801823a0, PPC_NOP); // CT bl Text_LoadSisFile
    CODEPATCH_HOOKAPPLY(0x801823c4);

    CODEPATCH_HOOKAPPLY(0x80181ee4);
    CODEPATCH_HOOKAPPLY(0x8018201c);
    CODEPATCH_HOOKAPPLY(0x80181f8c);
    CODEPATCH_HOOKAPPLY(0x80182170);
    CODEPATCH_REPLACECALL(0x8018213c, ChecklistRewards_GetHoverRewardParam);
    OSReport("[ChecklistRewards] Reward tables reallocated, hooks installed\n");

    ClearCrossModeSlots();
}

// 0xFFFF = no local placement; 0 would alias (Air Ride, clear_kind 0).
void ChecklistRewards_OnSaveInit(void)
{
    memset(ap_save->shuffled_rewards, 0xFF, sizeof(ap_save->shuffled_rewards));
}

void ChecklistRewards_OnSaveLoaded(void)
{
    RebuildRewardTablesFromShuffle();
    RegrantAllReceivedRewards();
}

// Re-grants, so rewards received before the assignment land on their cells.
void ChecklistRewards_ApplyLocations()
{
    // locations[] is in AP reward_index order, shuffled_rewards in game order.
    for (int m = 0; m < GMMODE_NUM; m++)
    {
        int count = reward_counts[m];
        for (int ap_ri = 0; ap_ri < count; ap_ri++)
            ap_save->shuffled_rewards[m][ap_to_game_ri[m][ap_ri]] = ap_data->locations[m][ap_ri];
    }

    RebuildRewardTablesFromShuffle();
    RegrantAllReceivedRewards();

    // No card write: the client resends the assignment on every connect.
    ap_data->location_data_valid = 0;
    OSReport("[ChecklistRewards] AP location assignment applied\n");
}
