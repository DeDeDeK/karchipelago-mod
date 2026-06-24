#ifndef CUSTOM_ITEMS_API_H
#define CUSTOM_ITEMS_API_H

#include "datatypes.h"

// ---------------------------------------------------------------------------
// Custom Items - framework for adding new City Trial item kinds from drop-in
// HSD archives.
//
// A custom item is shipped as a self-contained .dat dropped into the FST's
// `items/` folder (mods/custom_items/assets/items/ at build time, or any file
// added to that folder in the Riivolution mount). At boot the mod scans that
// folder, and per City Trial round it loads each archive, validates it, and
// splices a new ItemKind into the game's item tables so it can spawn from the
// sky, boxes, and events with author-specified weights.
//
// This header is the authoring contract: it defines the public symbol and the
// descriptor layout a custom-item .dat must export, plus the API other mods
// (e.g. archipelago) import to gate/query custom items.
// ---------------------------------------------------------------------------

#define CUSTOM_ITEMS_MOD_NAME  "custom_items"
#define CUSTOM_ITEMS_API_MAJOR 1
#define CUSTOM_ITEMS_API_MINOR 1

// Each custom-item .dat must export one public symbol named `customItem` whose
// address is a CustomItemDesc. (Big-endian magic, ASCII "CITM".)
#define CUSTOM_ITEM_SYMBOL        "customItem"
#define CUSTOM_ITEM_MAGIC         0x4349544Du
// v1: model is a JOBJDesc root; render flag assumed flat (0x02000000).
// v2: adds model_flag, carrying the model's itData render flag so complex /
//     skinned models (Hydra/Dragoon pieces use 0x03/0x05/0x0b) render correctly.
#define CUSTOM_ITEM_DESC_VERSION  2

// Folder (relative to FST root) and extension scanned for drop-in items.
#define CUSTOM_ITEM_DROPIN_DIR    "items"
#define CUSTOM_ITEM_DROPIN_EXT    ".dat"

// Spawn-weight source columns for CustomItemDesc.weight_event[]. These mirror
// the per-kind chance columns of the engine's event_source_drop[] table.
typedef enum CustomItemEventSource
{
    CUSTOM_ITEM_EVSRC_DYNABLADE,    // 0 Dyna Blade feather drops
    CUSTOM_ITEM_EVSRC_TAC,          // 1 Tac (item-thief) drops
    CUSTOM_ITEM_EVSRC_METEOR,       // 2 meteor impact scatter
    CUSTOM_ITEM_EVSRC_DESTRUCTIBLE, // 3 broken yakumono (crates/walls/etc.)
    CUSTOM_ITEM_EVSRC_CHAMBER,      // 4 secret-chamber payouts
    CUSTOM_ITEM_EVSRC_UFO,          // 5 UFO drops
    CUSTOM_ITEM_EVSRC_NUM,
} CustomItemEventSource;

// Descriptor exported by a custom-item .dat under the `customItem` symbol.
//
// A custom item is a "skin/clone": the new kind inherits behavior (state class,
// trigger, hurt, animation) from an existing vanilla ItemKind (`base_kind`) and
// optionally overrides the visual model and/or the stat-grant effect. All
// pointers are resolved within the archive by Archive_GetPublicAddress, so they
// are valid for the loaded archive's lifetime (one scene).
//
// The model may be any item model carved from Item.dat - from a flat patch/copy
// panel up to a multi-material, multi-texture, skinned model (the Hydra/Dragoon
// legendary pieces). `model_flag` carries that model's render flag so skinned
// geometry sets up correctly.
typedef struct CustomItemDesc
{
    u32 magic;          // 0x00 CUSTOM_ITEM_MAGIC
    u16 version;        // 0x04 CUSTOM_ITEM_DESC_VERSION
    u16 reserved;       // 0x06
    const char *name;   // 0x08 display name (NUL-terminated)

    int base_kind;      // 0x0c ItemKind to clone behavior from (0..ITKIND_NUM-1)
    int group;          // 0x10 ItemGroup: 0=BAD, 1=GOOD, 2=FAKE

    void *model;        // 0x14 optional JOBJDesc* model override (NULL = inherit base_kind)
    void *effect_info;  // 0x18 optional PatchEffectInfo* stat-grant override (NULL = inherit)

    // Spawn weights. The sky/free-fall picker draws from the union of the three
    // box pools, so weight_box already governs both box breaks and sky drops;
    // weight_free is reserved (there is no separate free-fall pool to weight).
    u16 weight_box[3];  // 0x1c spawn weight in the blue/green/red box pools (0 = never)
    u16 weight_free;    // 0x22 reserved - sky drops are covered by weight_box
    u16 weight_event[CUSTOM_ITEM_EVSRC_NUM]; // 0x24 weight per event source (0 = never)

    u32 model_flag;     // 0x30 (v2+) itData model render flag for `model`
                        //      (0x02000000 flat; 0x03/0x05/0x0b legendary/skinned)
} CustomItemDesc;

// Handler invoked when a custom item is collected by a rider. `id_hash` and
// `name` identify which custom item was picked up; `player` is the collector's
// 0..4 player slot. Registered via CustomItemsAPI.SetPickupHandler; the handler
// filters by id_hash/name and applies whatever effect it wants (e.g. Hypernova).
typedef void (*CustomItemPickupFn)(u32 id_hash, const char *name, int player);

// API published via Hoshi_ExportMod for other mods to consume. Custom items are
// addressed by their stable id hash (derived from the .dat's FST path), which
// survives reboots and reordering of the drop-in folder.
typedef struct CustomItemsAPI
{
    // Number of discovered custom items (registry entries).
    int (*GetCount)(void);

    // Stable id hash of the index-th discovered item (0 if out of range).
    u32 (*GetIdHash)(int index);

    // Display name of the index-th item (NULL if out of range).
    const char *(*GetName)(int index);

    // 1 if the item is enabled for spawning (master toggle AND per-item gate).
    int (*IsEnabled)(u32 id_hash);

    // Enable/disable an item for spawning (e.g. an AP gate granting it).
    void (*SetEnabled)(u32 id_hash, int enabled);

    // ItemKind assigned to this item in the extended item tables for the current
    // round, or -1 if it has not been registered yet this scene.
    int (*GetAssignedKind)(u32 id_hash);

    // Register a handler fired when any custom item is collected by a rider
    // (NULL clears it). Only one handler at a time. (API minor 1+.)
    void (*SetPickupHandler)(CustomItemPickupFn handler);
} CustomItemsAPI;

#endif // CUSTOM_ITEMS_API_H
