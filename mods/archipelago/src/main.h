#ifndef ARCHIPELAGO_MAIN_H
#define ARCHIPELAGO_MAIN_H

#include "structs.h"
#include "game.h"
#include "event.h"

#include "archipelago_api.h"

// Never null: starts at a stub that drops every message.
#include "textbox_api.h"
extern const TextBoxAPI *tb_api;

// NULL only in a build without custom_machines, where machine gating is off.
#include "custom_machines_api.h"
extern const CustomMachinesAPI *cm_api;

#define MAX_RECEIVED_ITEMS 512

#define REWARD_COUNT_MAX AIRRIDE_REWARD_NUM // the largest of the three modes

// Runtime checklist mode of the AP tab - not necessarily AP_CHECKLIST_ROW if another
// custom tab registered first. GMMODE_NUM until APChecklist_Register.
extern int ap_checklist_mode;

// Set while checklist rewards are re-applied, so the unlockers skip their log line and
// textbox.
extern int ap_regrant_quiet;

// AP_PATCH_MAX must be a multiple of 64.
#define AP_PATCH_WORDS (AP_PATCH_MAX / 64)

// Bits per mode in checklist_reward_placed_types; the placeable RewardTypes stop at
// REWARD_PAUSE_POWERUPS (8).
#define CHECKLIST_REWARD_MODE_BITS 9

typedef struct APSlotOptions
{
    u32 death_link_enabled;                // Initial menu toggle
    u32 energy_link_enabled;               // Initial menu toggle
    u32 trap_link_enabled;                 // Initial menu toggle

    u32 reveal_checklists[CHECKLIST_MODE_NUM]; // Per row: 1 = every square starts revealed

    u32 goal[CHECKLIST_MODE_NUM];             // APGoalKind per row
    u32 checklist_amount[CHECKLIST_MODE_NUM]; // 1-120 squares for GOAL_N_CHECKLIST

    u32 city_trial_patch_cap_min;          // 1-127, the per-stat cap the player starts at
    u32 city_trial_patch_cap_max;          // 1-127, the cap ceiling; min == max is a flat cap

    u32 spawn_rate_min;                    // Starting item spawn rate, 10-100 percent

    u64 goal_checks[CHECKLIST_MODE_NUM][2];   // Checkboxes GOAL_CHECKLIST_LIST requires per row

    // 1 = gated, 0 = ungated (the unlock mask is pre-filled at connect). APUnlockCategory
    // order; the AP Star spheres follow item_gating_enabled.
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

    // Bit mode * CHECKLIST_REWARD_MODE_BITS + RewardType set = the AP world placed that
    // reward type as items. A placeable type left clear is granted at connect.
    u32 checklist_reward_placed_types;

    // GOALGATE_* bits: unlocks shipped as items even though their category is ungated,
    // because this seed's goal depends on them.
    u32 goal_forced_gates;

    u32 ap_patches; // AP Patch location count, up to AP_PATCH_MAX; 0 = feature off
} APSlotOptions;

#define GOALGATE_LEGENDARY_PIECES 0x1 // ITUNLOCK_HYDRA1-3 / ITUNLOCK_DRAGOON1-3
#define GOALGATE_VS_KING_DEDEDE   0x2 // STKIND_VSKINGDEDEDE
#define GOALGATE_AP_STAR_PIECES   0x4 // AP_STAR_PIECE_ROSE..YELLOW

#define LEGENDARY_PIECE_ITEM_BITS                                                  \
    ((1u << ITUNLOCK_HYDRA1) | (1u << ITUNLOCK_HYDRA2) | (1u << ITUNLOCK_HYDRA3) | \
     (1u << ITUNLOCK_DRAGOON1) | (1u << ITUNLOCK_DRAGOON2) | (1u << ITUNLOCK_DRAGOON3))

// AP checklist objectives whose progress spans more than one session.
typedef struct APCheckProgress
{
    u16 allup_collect_total; // APCK_ALLUPS_5: lifetime human All Up pickups in City Trial
    u8 purple_sr1_wins;      // APCK_SR1_PURPLE_3X
    u8 race_color_mask;      // APCK_AIRRIDE_ALL_COLORS, bit per KirbyColor
    u8 tr_color_mask;        // APCK_TR_ALL_COLORS, bit per KirbyColor
    u8 tr_steer_win_mask;    // APCK_TR_ALL_COURSES_STEER, bit per TopRideCourse
    u8 drag_win_mask;        // APCK_DRAG_ALL_1ST, bit per DRAG RACE
    u16 ar_course_win_mask;  // APCK_AIRRIDE_ALL_COURSES_1ST, bit per Air Ride course
    u32 tr_item_mask;        // APCK_TR_EVERY_ITEM, bit per TopRideItemKind
} APCheckProgress;

// hoshi matches a card block by mod-name hash and size only, so a layout change would be
// read back as data. Change the stamp whenever APSave changes; a mismatch reinitializes.
#define APSAVE_STAMP 0x41505301

typedef struct APSave
{
    u32 stamp;                                          // APSAVE_STAMP; first so a resize cannot displace it
    uint item_received_count;                           // Items taken from the AP mailbox
    uint unprocessed_count;
    u32 stadium_unlocked_mask;                          // Bit N = StadiumKind N unlocked
    u32 event_unlocked_mask;                            // Bit N = EventKind N unlocked
    u16 ability_unlocked_mask;                          // Bit N = CopyKind N unlocked
    u8 box_unlocked_mask;                               // Bit N = BoxKind N unlocked
    u16 patch_unlocked_mask;                            // Bit N = PatchKind N unlocked
    u32 item_unlocked_mask;                             // Bit N = ItemUnlockKind N unlocked
    u32 machine_unlocked_mask;                          // Bit N = MachineKind N, then AP_MACHINE_BIT_AP_STAR
    u16 airride_stage_unlocked_mask;                    // Bit N = AirRideCourse N unlocked
    u16 topride_stage_unlocked_mask;                    // Bit N = TopRideCourse N unlocked
    u32 topride_item_unlocked_mask;                     // Bit N = TopRideItemKind N unlocked
    u8 color_unlocked_mask;                             // Bit N = KirbyColor N unlocked
    u8 base_ability_unlocked_mask;                      // Bit N = BaseAbilityKind N unlocked
    u8 ap_star_piece_unlocked_mask;                     // Bit N = APStarPiece N unlocked
    u8 patch_cap_count;                                 // Patch Cap Increase items received
    u8 spawn_rate_level;                                // Spawn Rate Up items received
    u8 permanent_patches[PATCHKIND_NUM];                // Permanent patches per stat, 0-PATCH_STAT_MAX
    u8 options_received;                                // Set once the slot options are copied in
    u16 shuffled_rewards[GMMODE_NUM][REWARD_COUNT_MAX]; // (target row << 8) | clear_kind, 0xFFFF = remote
    u64 received_checklist_rewards[GMMODE_NUM];         // Bit N = reward_index N received
    u64 sent_checks[CHECKLIST_MODE_NUM][2];             // Completed checkboxes per row
    u64 ap_patch_collected[AP_PATCH_WORDS];             // Bit N = AP Patch N collected
    u8 goal_latched;                                    // Bit r = row r's goal met; sticky
    u8 max_stats_ct_achieved;                           // Sticky: a human capped all 9 stats in one CT round
    APSlotOptions options;                              // Copied from APData on first connect
    uint unprocessed_items[MAX_RECEIVED_ITEMS];         // AP item IDs waiting to be applied
    APCheckProgress checks;
} APSave;

// Client-authored textbox messages: the client composes the whole line, the mod only
// renders it.

#define AP_TEXT_SEG_NUM 8
_Static_assert(AP_TEXT_SEG_NUM == TEXTBOX_MAX_SEGMENTS, "AP_TEXT_SEG_NUM must match TEXTBOX_MAX_SEGMENTS");
// seg_count NUL-terminated strings back to back, sized past what the textbox can show
// (three lines, then truncated) so the mod decides the fit.
#define AP_TEXT_BLOB_LEN 244

// Archipelago's CommonClient palette, plus a default that follows the textbox's own.
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

// OnBoot parks the APData pointer here for the client to find. The word is the "ram\0"
// tail of the .sdata string "pvparam" (0x805d52d0), read only as the expression text of
// one __assert in zz_80064770_, so overwriting it is harmless.
#define AP_DATA_ANCHOR 0x805d52d4

// Shared with the Python AP client through dolphin-memory-engine. Field order is the
// wire contract.
typedef struct APData
{
    s64 energy_balance; // Client -> game, raw MJ; a local spend is overwritten by the next client write

    // Raw MJ in and out this boot, game-written rising counters the client diffs: a u32
    // store is atomic where an s64 is not, and a rising counter never reads half-old.
    u32 energy_deposit_total;
    u32 energy_withdraw_total;
    uint deathlink_receive;
    uint deathlink_send;
    uint traplink_receive;
    uint traplink_send;       // Game -> client. TrapLinkKind, 0 = none pending; client clears it
    uint incoming_item_id;    // Mailbox: client writes an AP item ID, game reads and clears to 0
    uint item_received_index; // Mirror of ap_save->item_received_count
    u32 game_ready;           // Set once save data is loaded and the mod is initialized
    u32 options_valid;        // Client sets once every options field is written
    APSlotOptions options;

    u32 location_data_valid;                     // Client sets; game clears after applying
    u16 locations[GMMODE_NUM][REWARD_COUNT_MAX]; // Per AP reward_index: (target row << 8) | clear_kind, 0xFFFF = remote

    u64 sent_checks[CHECKLIST_MODE_NUM][2];      // Game -> client, bit k = checkbox k complete
    u64 client_backfill[CHECKLIST_MODE_NUM][2];  // Client -> game, ORed into sent_checks under backfill_valid
    u8 goal_complete;                            // Game -> client, sticky once every goal is met
    u8 goal_satisfied_mask;                      // Game -> client, bit r = row r's goal met

    // Live mirrors of the Settings menu toggles, for the client to forward to the server.
    u32 deathlink_menu_enabled;
    u32 energylink_menu_enabled;
    u32 traplink_menu_enabled;

    // Text mailbox, the same handshake as incoming_item_id; held while no screen canvas
    // exists.
    u32 text_pending;
    u32 text_menu_mask; // Game -> client, bit (1 << APTextKind) = that kind is shown
    APTextMessage text_msg;

    // AP Patch locations, bit w*64+i of word w. The game owns ap_patch_checks, the client
    // ap_patch_backfill.
    u64 ap_patch_checks[AP_PATCH_WORDS];
    u64 ap_patch_backfill[AP_PATCH_WORDS];

    // Publishes both backfill arrays at once; the game clears it last, after consuming and
    // zeroing them, so a half-written u64 is never read.
    u32 backfill_valid;
} APData;

extern APData *ap_data;
extern APSave *ap_save;

// Row in the per-checklist-mode arrays for a runtime mode, or -1 for a mode this mod does
// not record. The single answer to "which row is this mode".
static inline int ChecklistModeRow(int mode)
{
    if (mode >= 0 && mode < GMMODE_NUM)
        return mode;
    if (mode == ap_checklist_mode)
        return AP_CHECKLIST_ROW;
    return -1;
}

// Inverse, for handing a row back to game code that indexes by runtime mode.
static inline int ChecklistRowMode(int row)
{
    return row == AP_CHECKLIST_ROW ? ap_checklist_mode : row;
}

// Is checkbox `k` of row `r` recorded complete?
#define SENT_CHECK_BIT(r, k)  ((ap_save->sent_checks[(r)][(k) >> 6] >> ((k) & 63)) & 1ULL)

// Attaches proc to every human rider's GObj, after hit collision. Returns how many.
int AP_AttachHumanRiderProcs(void (*proc)(GOBJ *));

void ArchipelagoAPI_Export(void);

#endif // ARCHIPELAGO_MAIN_H
