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

#define PATCH_STAT_MAX 18

// AP Slot Options
// Written by the Python client to APData on connect, then copied
// to save data once. All fields are u32 for 4-byte alignment (atomic on PPC).

typedef enum APGoalKind
{
    GOAL_100_CHECKLIST = 0,     // Complete 100 checklist squares
    GOAL_N_CHECKLIST,           // Complete N checklist squares
    GOAL_HYDRA_AND_DRAGOON,     // City Trial only: assemble both legendary machines
    GOAL_BEAT_KING_DEDEDE,      // City Trial only: defeat King Dedede in stadium
    GOAL_NONE,                  // No goal for this mode
    GOAL_CHECKLIST_LIST,        // Complete all checkboxes specified in goal_checks[mode]
    GOAL_MAX_STATS_CT,          // City Trial only: reach PATCH_STAT_MAX on every stat in one CT run
} APGoalKind;

typedef struct APSlotOptions
{
    // General
    u32 death_link_enabled;                // 0 or 1 — sets initial deathlink menu toggle
    u32 energy_link_enabled;               // 0 or 1 — sets initial energylink menu toggle
    u32 trap_link_enabled;                 // 0 or 1 — sets initial traplink menu toggle
    u32 reveal_checklists;                 // 0 or 1 — reveal all checklist squares

    // Per-mode goal settings (indexed by GameMode)
    u32 goal[GMMODE_NUM];                  // APGoalKind — completion condition per mode
    u32 checklist_amount[GMMODE_NUM];      // 1-120 — threshold for GOAL_N_CHECKLIST per mode

    // City Trial-specific
    u32 city_trial_progressive_patch_caps; // 0 or 1 — patch cap starts low, items raise it
    u32 city_trial_patch_cap_amount;       // 1-17 — starting cap when progressive is on
    u32 city_trial_progressive_stadiums;   // 0 or 1 — stadiums locked until items received

    // Spawn rate floor (applies to CT items + TR items; AR has no spawn rate to scale).
    // Percent: 100 = vanilla baseline, 200 = 2.0x, capped at 500 (5x — the mod's hard cap).
    // Each Spawn Rate Up item received adds +10% on top of this floor. The AP world picks
    // a (min, max) and ships (max - min) / 10 items so collecting all reaches max.
    // 0 (default when options not yet received) is treated as 100.
    u32 spawn_rate_min;

    // GOAL_CHECKLIST_LIST: bitmask of required checkboxes per mode.
    // Same encoding as sent_checks: bit (k % 64) of word (k / 64) for clear_kind k.
    // Only consulted when the mode's goal is GOAL_CHECKLIST_LIST.
    u64 goal_checks[GMMODE_NUM][2];
} APSlotOptions;

typedef struct APSave
{
    // Small critical fields first (ensure they fit within card save limits)
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
    u8 permanent_patches[PATCHKIND_NUM];                // Accumulated permanent patch count per stat (0-18)
    u8 options_received;                                // Nonzero if AP slot options have been saved
    u16 shuffled_rewards[GMMODE_NUM][REWARD_COUNT_MAX];     // Saved location assignment per mode: (target_mode << 8) | clear_kind, 0xFFFF = remote
    u64 received_checklist_rewards[3];                  // [GMMODE_NUM] bit N = reward_index N received for that mode
    // Authoritative record of which checkboxes the player has completed (in
    // gameplay or via filler placement). Indexed [mode][word], 2 u64s per mode
    // covers the 0..119 clear_kind range (bit (k%64) of word (k/64)).
    u64 sent_checks[3][2];
    u8 goal_complete;                                   // Sticky once set; persisted across boots
    u8 max_stats_ct_achieved;                           // Sticky: 1 once any human player hit PATCH_STAT_MAX on all 9 stats during a CT trial round
    APSlotOptions options;                              // AP slot options (copied from APData on first connect)
    // Large arrays last
    uint received_items[MAX_RECEIVED_ITEMS];            // Ordered list of all received AP item IDs
    uint unprocessed_items[MAX_RECEIVED_ITEMS];         // AP item IDs waiting to be applied
} APSave;

// Shared data struct stored at a static location in memory.
// The Python AP client reads/writes this via dolphin-memory-engine.
// All fields are 4-byte aligned. Reads/writes are atomic on PPC at this alignment.
//
// Field offsets are computed by the compiler; the protocol doc
// (docs/client-game-protocol.md) is the canonical reference for the Python
// client. Field order in this struct is the source of truth.
typedef struct APData
{
    float energy_balance;
    float energy_send;
    uint deathlink_receive;
    uint deathlink_send;
    uint traplink_receive;
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

#endif // ARCHIPELAGO_MAIN_H
