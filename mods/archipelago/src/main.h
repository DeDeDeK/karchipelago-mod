#ifndef ARCHIPELAGO_MAIN_H
#define ARCHIPELAGO_MAIN_H

#include "structs.h"
#include "event.h"

#include "archipelago_api.h"

// Mods boot in FST order, so every Hoshi_ImportMod in this mod is deferred to
// OnSaveLoaded - the first point past every mod's OnBoot, and so the only place an
// absent export really means absent. tb_api is never null: it starts at a stub that
// drops every message.
#include "textbox_api.h"
extern const TextBoxAPI *tb_api;

// Required: it owns the widened kind space and the engine seams that widening
// breaks, and this mod gates through the filters it takes. NULL only in a build
// that left it out, where machine gating is off entirely.
#include "custom_machines_api.h"
extern const CustomMachinesAPI *cm_api;

// The registry's own kind-space helpers, bound to our import.
static inline int MachineKind_Num(void)
{
    return CustomMachines_KindNum(cm_api);
}
static inline int CharacterKind_Num(void)
{
    return CustomMachines_CharacterKindNum(cm_api);
}
static inline MachineKind MachineKind_Resolve(int is_bike, int class_index)
{
    return CustomMachines_ResolveKind(cm_api, is_bike, class_index);
}
static inline int MachineKind_ClassIndexOf(MachineKind kind, int *is_bike)
{
    return CustomMachines_ClassIndexOf(cm_api, kind, is_bike);
}

#define MAX_RECEIVED_ITEMS 512

#define REWARD_COUNT_AIRRIDE   46
#define REWARD_COUNT_TOPRIDE   33
#define REWARD_COUNT_CITYTRIAL 44
#define REWARD_COUNT_MAX       REWARD_COUNT_AIRRIDE

// Runtime checklist mode the custom_checklist framework assigned to the AP tab.
// Always >= GMMODE_NUM but not necessarily AP_CHECKLIST_ROW - another custom tab
// registering first pushes it higher. GMMODE_NUM until APChecklist_Register.
extern int ap_checklist_mode;

// Set while checklist rewards are re-applied from save data, so the gate unlockers
// stay quiet instead of reprinting every already-owned unlock at boot. Read by
// APAnnounce_Grant alongside the Messages -> Local -> Items toggle.
extern int ap_regrant_quiet;

// Targets of the AP checklist objectives backed by APSave.checks counters.
#define AP_ALLUP_TOTAL_NEED 5
#define AP_PURPLE_SR1_NEED  3
// One bit per KirbyColor, all 8 set.
#define AP_RACE_COLOR_MASK_ALL 0xFF

// AP Patch locations get their own bitmask, sized so AP_PATCH_MAX packs into
// whole u64 words.
#define AP_PATCH_WORDS (AP_PATCH_MAX / 64)

// Bits per checklist mode in APSlotOptions.checklist_reward_placed_types. RewardType
// tops out at REWARD_PAUSE_POWERUPS (8), so all three modes pack into 27 bits.
#define CHECKLIST_REWARD_MODE_BITS 9

typedef struct APSlotOptions
{
    u32 death_link_enabled;                // 0 or 1 - initial deathlink menu toggle
    u32 energy_link_enabled;               // 0 or 1 - initial energylink menu toggle
    u32 trap_link_enabled;                 // 0 or 1 - initial traplink menu toggle

    u32 reveal_checklists[CHECKLIST_MODE_NUM]; // Per checklist-mode row: 1 = every square starts revealed

    u32 goal[CHECKLIST_MODE_NUM];             // APGoalKind per checklist-mode row
    u32 checklist_amount[CHECKLIST_MODE_NUM]; // 1-120 squares for GOAL_N_CHECKLIST

    u32 city_trial_patch_cap_min;          // 1-127 - per-stat cap the player starts at
    u32 city_trial_patch_cap_max;          // 1-127 - per-stat cap ceiling; min == max -> flat cap

    u32 spawn_rate_min;                    // Percent floor; 0 means options not yet received

    u64 goal_checks[CHECKLIST_MODE_NUM][2];   // Required checkboxes per row for GOAL_CHECKLIST_LIST

    // Per-category access gating. 1 = gated (AP ships unlock items), 0 = ungated
    // (the mod fills the matching unlock mask at connect). Order matches
    // APUnlockCategory, but each is a flat field so it maps 1:1 to a slot option.
    u32 machine_gating_enabled;
    u32 ability_gating_enabled;
    u32 event_gating_enabled;
    u32 patch_gating_enabled;
    u32 item_gating_enabled;
    u32 box_gating_enabled;
    u32 airride_stage_gating_enabled;
    u32 topride_stage_gating_enabled;
    u32 topride_item_gating_enabled;
    u32 color_gating_enabled;
    u32 stadium_gating_enabled;
    u32 base_ability_gating_enabled;

    // Which checklist rewards the AP world placed as items, one bit per (mode,
    // RewardType) pair at `mode * CHECKLIST_REWARD_MODE_BITS + reward_type`. Every
    // unset pair is pre-granted at connect, so a mode the seed disabled has its
    // rewards unlocked outright. The 6 Dragoon/Hydra part markers are progression
    // and are not affected.
    u32 checklist_reward_placed_types;

    // GOALGATE_* bits. Unlocks the AP world shipped as items even though their
    // category's gate is off, because this seed's goal is the thing they gate;
    // an ungated pre-fill has to leave exactly these bits locked.
    u32 goal_forced_gates;

    // 0-AP_PATCH_MAX AP Patch locations in the seed; 0 = feature off. No goal reads
    // it. APSlotOptions is 8-byte aligned, so the 4 bytes of tail padding past this
    // field keep the block 200 bytes wide and every APData offset below it fixed.
    u32 ap_patches;
} APSlotOptions;

// goal_forced_gates bits.
#define GOALGATE_LEGENDARY_PIECES 0x1 // ITUNLOCK_HYDRA1-3 / ITUNLOCK_DRAGOON1-3
#define GOALGATE_VS_KING_DEDEDE   0x2 // STKIND_VSKINGDEDEDE
#define GOALGATE_AP_STAR_PIECES   0x4 // AP_STAR_PIECE_ROSE..YELLOW

#define LEGENDARY_PIECE_ITEM_BITS                                                  \
    ((1u << ITUNLOCK_HYDRA1) | (1u << ITUNLOCK_HYDRA2) | (1u << ITUNLOCK_HYDRA3) | \
     (1u << ITUNLOCK_DRAGOON1) | (1u << ITUNLOCK_DRAGOON2) | (1u << ITUNLOCK_DRAGOON3))

#define AP_STAR_PIECE_ITEM_BITS ((1u << AP_STAR_PIECE_NUM) - 1)

// Cross-boot progress for AP checklist objectives whose predicate counts over
// more than one session.
typedef struct APCheckProgress
{
    u16 allup_collect_total; // APCK_ALLUPS_5: lifetime All Ups picked up by a human in City Trial
    u8 purple_sr1_wins;      // APCK_SR1_PURPLE_3X: SINGLE RACE 1 first places taken by a Purple Kirby
    u8 race_color_mask;      // APCK_AIRRIDE_ALL_COLORS: bit N = an Air Ride race finished as KirbyColor N
} APCheckProgress;

// Bumped whenever APSave's layout changes, so hoshi discards a stale block instead of
// reinterpreting it. Independent of ARCHIPELAGO_API_MAJOR/MINOR, which version the
// struct other mods import.
#define APSAVE_VERSION_MAJOR 4
#define APSAVE_VERSION_MINOR 0

typedef struct APSave
{
    uint boot_num;
    uint item_received_count;                           // Total items received from AP client
    uint unprocessed_count;                             // Number of items in the unprocessed list
    u32 stadium_unlocked_mask;                          // Bit N = StadiumKind N unlocked
    u32 event_unlocked_mask;                            // Bit N = EventKind N unlocked
    u16 ability_unlocked_mask;                          // Bit N = CopyKind N unlocked
    u8 box_unlocked_mask;                               // Bit N = BoxKind N unlocked
    u16 patch_unlocked_mask;                            // Bit N = PatchKind N unlocked
    u32 item_unlocked_mask;                             // Bit N = ItemUnlockKind N unlocked
    u32 machine_unlocked_mask;                          // Bit N = MachineKind N unlocked
    u16 airride_stage_unlocked_mask;                    // Bit N = StageKind N unlocked
    u16 topride_stage_unlocked_mask;                    // Bit N = Top Ride course N unlocked
    u32 topride_item_unlocked_mask;                     // Bit N = TopRideItemKind N unlocked
    u8 color_unlocked_mask;                             // Bit N = KirbyColor N unlocked
    u8 base_ability_unlocked_mask;                      // Bit N = BaseAbilityKind N unlocked
    u8 ap_star_piece_unlocked_mask;                     // Bit N = APStarPiece N unlocked
    u8 patch_cap_count;                                 // Number of Patch Cap Increase items received
    u8 spawn_rate_level;                                // Number of Spawn Rate Up items received
    u8 permanent_patches[PATCHKIND_NUM];                // Accumulated permanent patch count per stat (0-PATCH_STAT_MAX)
    u8 options_received;                                // Nonzero once AP slot options have been saved
    u16 shuffled_rewards[GMMODE_NUM][REWARD_COUNT_MAX]; // (target_mode << 8) | clear_kind, 0xFFFF = remote
    u64 received_checklist_rewards[3];                  // [GMMODE_NUM] bit N = reward_index N received
    u64 sent_checks[CHECKLIST_MODE_NUM][2];             // Authoritative completed-checkbox bitmask per row
    u64 ap_patch_collected[AP_PATCH_WORDS];             // Bit N = AP Patch N collected
    u8 goal_complete;                                   // Sticky once set
    u8 goal_announced[CHECKLIST_MODE_NUM];              // Sticky per row; fires that row's "goal complete" textbox once
    u8 max_stats_ct_achieved;                           // Sticky once a human hit the cap on all 9 stats in a CT round
    APSlotOptions options;                              // Copied from APData on first connect
    uint unprocessed_items[MAX_RECEIVED_ITEMS];         // AP item IDs waiting to be applied
    APCheckProgress checks;
} APSave;

// Client-authored textbox messages. The client owns every name the mod cannot know -
// other worlds' item and location names, player names - so it composes the whole line
// and the mod only renders it.

// Colored runs per message.
#define AP_TEXT_SEG_NUM 8
_Static_assert(AP_TEXT_SEG_NUM == TEXTBOX_MAX_SEGMENTS, "AP_TEXT_SEG_NUM must match TEXTBOX_MAX_SEGMENTS");
// seg_count NUL-terminated strings back to back, so a whole message is at most
// AP_TEXT_BLOB_LEN - seg_count rendered characters. Sized past anything the textbox
// can show so the fit decision belongs to the mod, which knows the font size: the
// textbox wraps onto three lines and truncates whatever is left over.
#define AP_TEXT_BLOB_LEN 244

// Archipelago's own palette (the CommonClient GUI names), plus a default that follows
// the textbox's own DefaultColor.
typedef enum APTextColor
{
    APTEXTCOLOR_DEFAULT = 0,
    APTEXTCOLOR_BLACK,
    APTEXTCOLOR_RED,
    APTEXTCOLOR_GREEN,
    APTEXTCOLOR_YELLOW,
    APTEXTCOLOR_BLUE,
    APTEXTCOLOR_MAGENTA,
    APTEXTCOLOR_CYAN,
    APTEXTCOLOR_WHITE,
    APTEXTCOLOR_ORANGE,
    APTEXTCOLOR_SLATEBLUE,
    APTEXTCOLOR_PLUM,
    APTEXTCOLOR_SALMON,
    APTEXTCOLOR_NUM,
} APTextColor;

typedef struct APTextMessage
{
    u8 kind;                    // APTextKind
    u8 seg_count;               // 1..AP_TEXT_SEG_NUM
    u8 colors[AP_TEXT_SEG_NUM]; // APTextColor per segment
    u8 pad[2];
    char text[AP_TEXT_BLOB_LEN];
} APTextMessage;

_Static_assert(sizeof(APTextMessage) == 256, "APTextMessage stride is part of the wire contract");

// Where OnBoot parks the APData pointer, so the client can find the struct by
// address. Changing it needs a paired client change.
#define AP_DATA_ANCHOR 0x805d52d4

// Shared struct the Python AP client reads and writes with dolphin-memory-engine.
// Field order is the wire contract.
typedef struct APData
{
    s64 energy_balance; // EnergyLink pool, raw MJ. Client -> game; the game may decrement locally for purchase UI, the next client write wins.

    // Raw MJ in and out this session, game -> client single-writer; the client diffs each
    // and nets them, and never writes either. Both reset each mod boot. Two rising u32s
    // rather than one signed net total because a 64-bit store is not atomic on PPC32: a
    // read straddling a net value crossing zero decodes a small negative as ~4.29e9, and
    // the client's max:0 clamp cannot undo the deposit that follows. A u32 is one store,
    // and a counter that only rises reads either the old or the new value, never a mix.
    u32 energy_deposit_total;
    u32 energy_withdraw_total;
    uint deathlink_receive;
    uint deathlink_send;
    uint traplink_receive;
    uint traplink_send;       // Game -> client. TrapLinkKind value, 0 = no pending send; client clears to 0.
    uint incoming_item_id;    // Mailbox: client writes AP item ID, game reads and clears to 0
    uint item_received_index; // Mirror of ap_save->item_received_count for the client to read
    u32 game_ready;           // Game sets to 1 after save data is loaded and the mod is initialized
    u32 options_valid;        // Client sets to 1 after writing all options fields
    APSlotOptions options;    // Slot options from AP server

    u32 location_data_valid;                     // Client sets to 1; game clears after applying
    u16 locations[GMMODE_NUM][REWARD_COUNT_MAX]; // Per reward_index: (target_mode << 8) | clear_kind, 0xFFFF = remote

    u64 sent_checks[CHECKLIST_MODE_NUM][2];      // Game -> client. Bit (k%64) of word (k/64) = checkbox k complete on that row.
    u64 client_backfill[CHECKLIST_MODE_NUM][2];  // Client -> game, additive. Mod ORs into sent_checks each frame, then clears.
    u8 goal_complete;                            // Game -> client. Sticky once the active goal is satisfied.
    u8 goal_satisfied_mask;                      // Game -> client. Bit r = row r's goal satisfied; 0 for GOAL_NONE rows. Sticky per row.

    // Live mirrors of the Settings menu toggles, game-owned. The client polls
    // them to forward mid-session enable/disable to the AP server.
    u32 deathlink_menu_enabled;
    u32 energylink_menu_enabled;
    u32 traplink_menu_enabled;

    // Text mailbox, the same shape as incoming_item_id: the client fills text_msg and then
    // sets text_pending, the game renders and clears it. The game holds a pending message
    // while the textbox has no canvas, so a scene load backpressures the client.
    u32 text_pending;
    u32 text_menu_mask; // Game -> client. Bit (1 << APTextKind) set = that kind is shown.
    APTextMessage text_msg;

    // AP Patch locations, bit w*64+i of word w. Same single-writer split as
    // sent_checks / client_backfill: the game owns the first, the client the
    // second, and the game ORs the second in and clears it each frame.
    u64 ap_patch_checks[AP_PATCH_WORDS];
    u64 ap_patch_backfill[AP_PATCH_WORDS];

    // Publishes both backfill arrays at once: the client fills client_backfill and
    // ap_patch_backfill, then sets this; the game consumes both, zeroes them, and clears
    // this last. Without it the game can read a half-written u64, consume the bits that
    // arrived and zero away the ones that had not - a permanent loss, since the client
    // has already moved on. Being a u32 it is never itself torn.
    u32 backfill_valid;
} APData;

extern APData *ap_data;
extern APSave *ap_save;

// Map a runtime checklist mode to its row in the per-checklist-mode arrays
// (sent_checks, goal_checks, cross_mode_slots, ...), or -1 for a mode this mod does
// not record. The 3 real game modes map to themselves; the AP checklist tab maps to
// AP_CHECKLIST_ROW wherever the custom_checklist framework placed it. The single
// answer to "which row is this mode" - do not re-derive it per consumer.
static inline int ChecklistModeRow(int mode)
{
    if (mode >= 0 && mode < GMMODE_NUM)
        return mode;
    if (mode == ap_checklist_mode)
        return AP_CHECKLIST_ROW;
    return -1;
}

// Inverse, for handing a row back to game code (gmGetClearcheckerTypeP and friends
// index by runtime mode).
static inline int ChecklistRowMode(int row)
{
    return row == AP_CHECKLIST_ROW ? ap_checklist_mode : row;
}

// Is checkbox `k` of row `r` recorded complete?
#define SENT_CHECK_BIT(r, k)  ((ap_save->sent_checks[(r)][(k) >> 6] >> ((k) & 63)) & 1ULL)

void OnBoot();
void OnSaveInit();
void OnSaveLoaded();
void OnMainMenuLoad();
void OnPlayerSelectLoad();
void On3DLoadStart();
void On3DLoadEnd();
void On3DPause(int pause_ply);
void On3DUnpause(int pause_ply);
void On3DExit();
void OnSceneChange();
void OnTopRideLoadEnd();
void OnFrameStart();

// Register the public API instance with hoshi so other mods can import it via
// Hoshi_ImportMod(). Call once from OnBoot.
void ArchipelagoAPI_Export(void);

// Debug overrides of the slot options the client normally owns. The gating flags,
// the patch cap range and the spawn rate floor are read at connect, so a change to
// any of them needs APOptions_DebugReapply to become observable.
int APOptions_DebugGetGating(APUnlockCategory cat);
void APOptions_GetPatchCapRange(int *out_min, int *out_max);
int APOptions_GetSpawnRateMin(void);
void APOptions_DebugSetGating(APUnlockCategory cat, int enabled);
void APOptions_DebugSetPatchCapMin(int min);
void APOptions_DebugSetPatchCapMax(int max);
void APOptions_DebugSetSpawnRateMin(int percent);
void APOptions_DebugReapply(void);

// Log the whole save and wire state, and roll received-item progression back to
// what a save holds before its first connect.
void APDebug_ReportState(void);
void APDebug_ResetProgression(void);

// Per-category unlock-mask access. Set truncates to the underlying width.
u32  Unlock_GetMask(APUnlockCategory cat);
void Unlock_SetMask(APUnlockCategory cat, u32 mask);

#endif // ARCHIPELAGO_MAIN_H
