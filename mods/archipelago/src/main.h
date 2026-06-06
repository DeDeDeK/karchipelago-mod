#ifndef ARCHIPELAGO_MAIN_H
#define ARCHIPELAGO_MAIN_H

#include "structs.h"
#include "event.h"

// Public API (APItemId enum lives here so external mods can use it too).
#include "archipelago_api.h"

// Imported textbox API. Resolved in OnSaveLoaded via Hoshi_ImportMod (deferred
// past OnBoot since mods boot alphabetically and textbox boots after us);
// callers dereference directly (e.g. tb_api->Enqueue("..."), tb_api->EventColor).
#include "textbox_api.h"
extern const TextBoxAPI *tb_api;

#define MAX_RECEIVED_ITEMS 512

#define REWARD_COUNT_AIRRIDE   46
#define REWARD_COUNT_TOPRIDE   33
#define REWARD_COUNT_CITYTRIAL 44
#define REWARD_COUNT_MAX       REWARD_COUNT_AIRRIDE  // Largest per-mode reward count (46)

// Number of checkboxes per mode (clear_kind range 0..CLEAR_KIND_NUM-1).
#define CLEAR_KIND_NUM 120

// Hard ceiling on per-stat patch totals. PowerPC `extsb` in the original
// Patch_GetMaxValue caller path sign-extends the low byte, so values >127
// wrap negative — 127 is the firm hardware limit. The runtime cap (see
// PatchCap_GetCap in patch_cap.c) is the per-slot target chosen via the
// city_trial_patch_cap_amount option; PATCH_STAT_MAX is only the absolute
// clamp ceiling for guards and storage widths.
#define PATCH_STAT_MAX 127

typedef enum APGoalKind
{
    GOAL_100_CHECKLIST = 0,     // Complete 100 checklist squares
    GOAL_N_CHECKLIST,           // Complete N checklist squares
    GOAL_HYDRA_AND_DRAGOON,     // City Trial only: assemble both legendary machines
    GOAL_BEAT_KING_DEDEDE,      // City Trial only: defeat King Dedede in stadium
    GOAL_NONE,                  // No goal for this mode
    GOAL_CHECKLIST_LIST,        // Complete all checkboxes specified in goal_checks[mode]
    GOAL_MAX_STATS_CT,          // City Trial only: reach the runtime patch cap (city_trial_patch_cap_amount, up to 127) on every stat in one CT run
} APGoalKind;

typedef struct APSlotOptions
{
    // General
    u32 death_link_enabled;                // 0 or 1 — sets initial deathlink menu toggle
    u32 energy_link_enabled;               // 0 or 1 — sets initial energylink menu toggle
    u32 trap_link_enabled;                 // 0 or 1 — sets initial traplink menu toggle
    u32 reveal_checklists;                 // 0 or 1 — reveal all checklist squares

    // Per-mode goal settings
    u32 goal[GMMODE_NUM];                  // APGoalKind — completion condition per mode
    u32 checklist_amount[GMMODE_NUM];      // 1-120 — threshold for GOAL_N_CHECKLIST per mode

    // City Trial-specific
    u32 city_trial_progressive_patch_caps; // 0 or 1 — if on, cap starts at 1 and Patch Cap Increase items grow it toward the target
    u32 city_trial_patch_cap_amount;       // 1-127 — target patch cap (also the threshold for GOAL_MAX_STATS_CT)

    // Spawn rate floor, percent (100 = vanilla, capped at 500).
    u32 spawn_rate_min;

    // Required checkboxes per mode for GOAL_CHECKLIST_LIST.
    u64 goal_checks[GMMODE_NUM][2];

    // Per-category access gating. 1 = gated (default — players unlock via AP
    // items). 0 = ungated (mod pre-fills the corresponding unlock mask with
    // all-1s at connect time; AP world ships no unlock items for that
    // category). Order matches APUnlockCategory but each is a flat field so
    // it maps 1:1 to a KAROptions.py progression toggle.
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

    // Non-progression checklist rewards (music, sound test, extra rules, endings, filler boxes, …).
    // 1 = gated (default: each reward is an AP item the player finds). 0 = ungated (the mod marks
    // every such reward received at connect via received_checklist_rewards; the AP world ships none).
    // The 6 Dragoon/Hydra part markers are progression and are NOT affected by this flag.
    u32 checklist_rewards_gating_enabled;
} APSlotOptions;

typedef struct APSave
{
    uint boot_num;
    uint item_received_count;                           // Total items received from AP client
    uint unprocessed_count;                             // Number of items in the unprocessed list
    u32 stadium_unlocked_mask;                          // Bitmask of AP-unlocked stadiums (bit N = StadiumKind N)
    u32 event_unlocked_mask;                            // Bitmask of AP-unlocked events (bit N = EventKind N)
    u16 ability_unlocked_mask;                          // Bitmask of AP-unlocked copy abilities (bit N = CopyKind N)
    u8 box_unlocked_mask;                               // Bitmask of AP-unlocked box types (bit N = BoxKind N)
    u16 patch_unlocked_mask;                            // Bitmask of AP-unlocked patch types (bit N = PatchKind N)
    u32 item_unlocked_mask;                              // Bitmask of AP-unlocked items (bit N = ItemUnlockKind N)
    u32 machine_unlocked_mask;                          // Bitmask of AP-unlocked machines (bit N = MachineKind N)
    u16 airride_stage_unlocked_mask;                    // Bitmask of AP-unlocked Air Ride stages (bit N = StageKind N)
    u16 topride_stage_unlocked_mask;                    // Bitmask of AP-unlocked Top Ride courses (bit N = course N)
    u32 topride_item_unlocked_mask;                     // Bitmask of AP-unlocked Top Ride items (bit N = TopRideItemKind N)
    u8 color_unlocked_mask;                             // Bitmask of AP-unlocked Kirby colors (bit N = KirbyColor N)
    u8 patch_cap_count;                                 // Number of Patch Cap Increase items received
    u8 spawn_rate_level;                                // Number of Spawn Rate Up items received
    u8 permanent_patches[PATCHKIND_NUM];                // Accumulated permanent patch count per stat (0-PATCH_STAT_MAX)
    u8 options_received;                                // Nonzero if AP slot options have been saved
    u16 shuffled_rewards[GMMODE_NUM][REWARD_COUNT_MAX]; // Saved location assignment per mode: (target_mode << 8) | clear_kind, 0xFFFF = remote
    u64 received_checklist_rewards[3];                  // [GMMODE_NUM] bit N = reward_index N received for that mode
    u64 sent_checks[3][2];                              // Authoritative completed-checkbox bitmask per mode.
    u8 goal_complete;                                   // Sticky once set; persisted across boots
    u8 max_stats_ct_achieved;                           // Sticky: 1 once any human player hit the runtime patch cap target on all 9 stats during a CT trial round
    APSlotOptions options;                              // AP slot options (copied from APData on first connect)
    uint unprocessed_items[MAX_RECEIVED_ITEMS];         // AP item IDs waiting to be applied
} APSave;

// Shared data struct stored at a static location in memory.
// The Python AP client reads/writes this via dolphin-memory-engine.
// All 32-bit fields are 4-byte aligned and atomic on PPC at this alignment.
// 64-bit fields (energy_balance, energy_send, sent_checks, client_backfill,
// goal_checks) are not atomic on PPC32 — readers may observe a torn value
// during a writer's update. The mailbox handshake for energy_send (write
// only when 0, clear to 0 after consume) limits the race window in practice.
//
// Field offsets are computed by the compiler; the protocol doc
// (docs/client-game-protocol.md) is the canonical reference for the Python
// client. Field order in this struct is the source of truth.
typedef struct APData
{
    // EnergyLink pool balance in raw units (1 raw unit = 1 MJ in the AP pool).
    // Client → game. Game reads for display and purchase validation; may
    // locally decrement for immediate UI feedback on purchases, but the next
    // client write is authoritative. Widened to s64 so multiworld pools that
    // exceed u64 joules (i.e. > ~1.8e19 J) still fit at MJ scale.
    s64 energy_balance;
    // Signed mailbox delta. Game → client. Positive = deposit into pool,
    // negative = withdrawal. Game writes only when current value is 0; client
    // clears to 0 after consume. Per-tick deltas are small (well below s32);
    // s64 just matches energy_balance's width for consistent alignment.
    s64 energy_send;
    uint deathlink_receive;
    uint deathlink_send;
    uint traplink_receive;
    // Game → client. Value is a TrapLinkKind enum (0 = no pending send,
    // >0 = pending send with this kind). Game writes the kind at the send
    // site; client reads, maps to a trap_name string for the outgoing Bounce,
    // and clears to 0. See traplink.h for the kind enum.
    uint traplink_send;
    uint incoming_item_id;    // Mailbox: client writes AP item ID, game reads and clears to 0
    uint item_received_index; // Mirror of ap_save->item_received_count for client to read
    u32 game_ready;           // Game sets to 1 after save data is loaded and mod is initialized
    u32 options_valid;        // Client sets to 1 after writing all options fields
    APSlotOptions options;    // Slot options from AP server

    // Location assignment: which checklist slot each reward_index is placed at.
    // Written by the AP client once per session after the server reveals placement.
    // Encoding: (target_mode << 8) | clear_kind. target_mode selects which mode's
    // checklist the reward appears in (enables cross-mode reward shuffling).
    // 0xFFFF means the reward has no local slot (it will arrive from another world via the mailbox).
    u32 location_data_valid;                         // Client sets to 1; game clears after applying
    u16 locations[GMMODE_NUM][REWARD_COUNT_MAX];      // location per reward_index, indexed by mode

    // Check detection: mod-side authoritative record of completed checkboxes.
    // sent_checks mirrors ap_save->sent_checks; bit (k%64) of word (k/64)
    // is set when the player completes checkbox clear_kind k in mode m.
    // The Python client polls this and forwards new bits as AP location checks.
    u64 sent_checks[3][2];

    // Client backfill: AP client writes bits here to back-fill checks the
    // server already knows about (e.g., fresh-save / slot-takeover). Mod ORs
    // them into sent_checks each frame and clears this field. Additive only.
    u64 client_backfill[3][2];

    // Goal completion: mod sets to 1 when the player satisfies the active
    // goal. Sticky and persisted to save. Client reads and forwards victory.
    u8 goal_complete;

    // Live mirrors of the Settings menu toggles. Written by the game on boot,
    // on first-connect option transfer, and on every menu change. Client polls
    // to forward deathlink/traplink/energylink enable/disable to the AP server
    // whenever the player changes them mid-session. Game-owned: client reads
    // only, never writes.
    u32 deathlink_menu_enabled;
    u32 energylink_menu_enabled;
    u32 traplink_menu_enabled;
} APData;

extern APData *ap_data;
extern APSave *ap_save;

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
void OnTopRideLoad();
void OnFrameStart();
void OnFrameEnd();

// Register the public API instance with hoshi so other mods can import it
// via Hoshi_ImportMod(). Call once from OnBoot. Defined in archipelago_api.c.
void ArchipelagoAPI_Export(void);

// Per-category unlock-mask access. Backs the ArchipelagoAPI Get/SetUnlockMask
// entry points; also called directly from main.c for connect-time pre-fill.
// Set truncates to the underlying width (u8/u16/u32).
u32  Unlock_GetMask(APUnlockCategory cat);
void Unlock_SetMask(APUnlockCategory cat, u32 mask);

#endif // ARCHIPELAGO_MAIN_H
