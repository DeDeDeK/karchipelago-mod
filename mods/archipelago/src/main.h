#ifndef ARCHIPELAGO_MAIN_H
#define ARCHIPELAGO_MAIN_H

#include "structs.h"
#include "event.h"

#include "archipelago_api.h"

// Resolved in OnSaveLoaded, not OnBoot: mods boot alphabetically and textbox
// boots after us, so Hoshi_ImportMod returns NULL during our own OnBoot.
#include "textbox_api.h"
extern const TextBoxAPI *tb_api;

// NULL when custom_machines is not built, in which case no machine or character
// exists past the vanilla ceilings and every caller falls back to them.
#include "custom_machines_api.h"
extern const CustomMachinesAPI *cm_api;

// Import the registry if it has not resolved yet. Idempotent; safe from any scene.
void AP_ResolveCustomMachines(void);

// CustomMachineDesc.name of the machine the Archipelago goal awards. The registry is
// generic, so this string is the only thing that ties a drop-in .dat to the AP wiring.
#define AP_STAR_MACHINE_NAME "Archipelago Star"

// Ceilings that include whatever custom_machines registered this boot.
static inline int MachineKind_Num(void)
{
    return cm_api ? cm_api->GetKindCeiling() : VCKIND_NUM;
}
static inline int CharacterKind_Num(void)
{
    return cm_api ? cm_api->GetCharacterKindCeiling() : CKIND_NUM;
}
// Absolute MachineKind for a (is_bike, class slot) pair, custom slots included.
static inline MachineKind MachineKind_Resolve(int is_bike, int class_index)
{
    if (cm_api)
        return (MachineKind)cm_api->KindFromClassIndex(is_bike, class_index);
    return MachineKind_FromClassIndex(is_bike, class_index);
}

#define MAX_RECEIVED_ITEMS 512

#define REWARD_COUNT_AIRRIDE   46
#define REWARD_COUNT_TOPRIDE   33
#define REWARD_COUNT_CITYTRIAL 44
#define REWARD_COUNT_MAX       REWARD_COUNT_AIRRIDE

// Checkboxes per mode (clear_kind 0..119).
#define CLEAR_KIND_NUM 120

// GMMODE_NUM (3) stays "the three real game modes" and sizes the reward tables.
// Per-checklist-mode recorded state is one row wider (CHECKLIST_MODE_NUM), with the
// AP tab at the fixed row AP_CHECKLIST_ROW.

// Runtime checklist mode the custom_checklist framework assigned to the AP tab.
// Always >= GMMODE_NUM but not necessarily AP_CHECKLIST_ROW - another custom tab
// registering first pushes it higher. GMMODE_NUM until APChecklist_Register.
extern int ap_checklist_mode;

// Absolute clamp ceiling for per-stat patch totals. Patch_GetMaxValue returns
// through extsb, so anything above 127 sign-extends negative.
#define PATCH_STAT_MAX 127

// Targets of the AP checklist objectives backed by APSave.checks counters.
#define AP_ALLUP_TOTAL_NEED 5
#define AP_PURPLE_SR1_NEED  3
// One bit per KirbyColor, all 8 set.
#define AP_RACE_COLOR_MASK_ALL 0xFF

typedef enum APGoalKind
{
    GOAL_100_CHECKLIST = 0,     // Complete 100 checklist squares
    GOAL_N_CHECKLIST,           // Complete N checklist squares
    GOAL_HYDRA_AND_DRAGOON,     // City Trial only: assemble both legendary machines
    GOAL_BEAT_KING_DEDEDE,      // City Trial only: defeat King Dedede in stadium
    GOAL_NONE,                  // No goal for this mode
    GOAL_CHECKLIST_LIST,        // Complete all checkboxes specified in goal_checks[mode]
    GOAL_MAX_STATS_CT,          // City Trial only: hit the cap ceiling on every stat in one run
} APGoalKind;

typedef struct APSlotOptions
{
    u32 death_link_enabled;                // 0 or 1 - initial deathlink menu toggle
    u32 energy_link_enabled;               // 0 or 1 - initial energylink menu toggle
    u32 trap_link_enabled;                 // 0 or 1 - initial traplink menu toggle

    u32 reveal_checklists[CHECKLIST_MODE_NUM]; // Per checklist-mode row: 1 = every square starts revealed

    u32 goal[CHECKLIST_MODE_NUM];             // APGoalKind per checklist-mode row
    u32 checklist_amount[CHECKLIST_MODE_NUM]; // 1-120 - threshold for GOAL_N_CHECKLIST per row

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

    // Non-progression checklist rewards: 1 = each is an AP item, 0 = the mod
    // pre-grants them all at connect. The 6 Dragoon/Hydra part markers are
    // progression and are not affected.
    u32 checklist_rewards_gating_enabled;

    // GOALGATE_* bits. Unlocks the AP world shipped as items even though their
    // category's gate is off, because this seed's goal is the thing they gate;
    // an ungated pre-fill has to leave exactly these bits locked.
    u32 goal_forced_gates;
} APSlotOptions;

// goal_forced_gates bits.
#define GOALGATE_LEGENDARY_PIECES 0x1 // ITUNLOCK_HYDRA1-3 / ITUNLOCK_DRAGOON1-3
#define GOALGATE_VS_KING_DEDEDE   0x2 // STKIND_VSKINGDEDEDE

#define LEGENDARY_PIECE_ITEM_BITS                                                  \
    ((1u << ITUNLOCK_HYDRA1) | (1u << ITUNLOCK_HYDRA2) | (1u << ITUNLOCK_HYDRA3) | \
     (1u << ITUNLOCK_DRAGOON1) | (1u << ITUNLOCK_DRAGOON2) | (1u << ITUNLOCK_DRAGOON3))

// Cross-boot progress for AP checklist objectives whose predicate counts over
// more than one session.
typedef struct APCheckProgress
{
    u16 allup_collect_total; // APCK_ALLUPS_5: lifetime All Ups picked up by a human in City Trial
    u8 purple_sr1_wins;      // APCK_SR1_PURPLE_3X: SINGLE RACE 1 first places taken by a Purple Kirby
    u8 race_color_mask;      // APCK_AIRRIDE_ALL_COLORS: bit N = an Air Ride race finished as KirbyColor N
} APCheckProgress;

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
    u8 patch_cap_count;                                 // Number of Patch Cap Increase items received
    u8 spawn_rate_level;                                // Number of Spawn Rate Up items received
    u8 permanent_patches[PATCHKIND_NUM];                // Accumulated permanent patch count per stat (0-PATCH_STAT_MAX)
    u8 options_received;                                // Nonzero once AP slot options have been saved
    u16 shuffled_rewards[GMMODE_NUM][REWARD_COUNT_MAX]; // (target_mode << 8) | clear_kind, 0xFFFF = remote
    u64 received_checklist_rewards[3];                  // [GMMODE_NUM] bit N = reward_index N received
    u64 sent_checks[CHECKLIST_MODE_NUM][2];             // Authoritative completed-checkbox bitmask per row
    u8 goal_complete;                                   // Sticky once set
    u8 goal_announced[CHECKLIST_MODE_NUM];              // Sticky per row; fires that row's "goal complete" textbox once
    u8 max_stats_ct_achieved;                           // Sticky once a human hit the cap on all 9 stats in a CT round
    APSlotOptions options;                              // Copied from APData on first connect
    uint unprocessed_items[MAX_RECEIVED_ITEMS];         // AP item IDs waiting to be applied
    APCheckProgress checks;
} APSave;

// Shared struct the Python AP client reads and writes with dolphin-memory-engine
// (OnBoot stores the pointer at 0x805d52d4). Field order is the wire contract.
typedef struct APData
{
    s64 energy_balance;    // EnergyLink pool, raw MJ. Client -> game; the game may decrement locally for purchase UI, the next client write wins.
    s64 energy_sent_total; // Cumulative net MJ emitted this session. Game -> client, single-writer; the client reads-and-diffs and never writes. Resets each mod boot.
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

    // Live mirrors of the Settings menu toggles, game-owned. The client polls
    // them to forward mid-session enable/disable to the AP server.
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
void OnTopRideLoadEnd();
void OnFrameStart();
void OnFrameEnd();

// Register the public API instance with hoshi so other mods can import it via
// Hoshi_ImportMod(). Call once from OnBoot.
void ArchipelagoAPI_Export(void);

// Per-category unlock-mask access. Set truncates to the underlying width.
u32  Unlock_GetMask(APUnlockCategory cat);
void Unlock_SetMask(APUnlockCategory cat, u32 mask);

#endif // ARCHIPELAGO_MAIN_H
