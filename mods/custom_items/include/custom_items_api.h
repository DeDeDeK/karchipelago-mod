#ifndef CUSTOM_ITEMS_API_H
#define CUSTOM_ITEMS_API_H

#include "datatypes.h"

// New City Trial item kinds loaded from .dat archives in the FST items/ folder.

#define CUSTOM_ITEMS_MOD_NAME  "custom_items"
#define CUSTOM_ITEMS_API_MAJOR 2
#define CUSTOM_ITEMS_API_MINOR 0

// Each custom-item .dat exports one public symbol named `customItem` whose
// address is a CustomItemDesc. Magic is big-endian ASCII "CITM".
#define CUSTOM_ITEM_SYMBOL        "customItem"
#define CUSTOM_ITEM_MAGIC         0x4349544Du
// v2 adds model_flag, v3 adds scale, v4 adds flags, v5 adds joint_anim; older
// descriptors stay supported (the loader rejects only versions newer than this
// one).
#define CUSTOM_ITEM_DESC_VERSION  5

// CustomItemDesc.flags (v4+).
// NO_MAT_ANIM: the model is not the base kind's, so the base kind's material
// animation - authored against its materials - must not be bound to it. The
// state script still comes from the base kind, as does the joint animation
// unless joint_anim overrides it.
#define CUSTOM_ITEM_FLAG_NO_MAT_ANIM 0x00000001u

// Folder (relative to FST root) and extension scanned for drop-in items.
#define CUSTOM_ITEM_DROPIN_DIR    "items"
#define CUSTOM_ITEM_DROPIN_EXT    ".dat"

// Chance columns of the engine's event_source_drop[] rows; indexes weight_event[].
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

// Descriptor exported under the `customItem` symbol. The new kind inherits
// behavior (state class, trigger, hurt, animation) from a vanilla base_kind and
// optionally overrides model/effect/scale. All pointers resolve inside the
// archive, so they are valid only for the loaded archive's scene.
typedef struct CustomItemDesc
{
    u32 magic;          // 0x00 CUSTOM_ITEM_MAGIC
    u16 version;        // 0x04 CUSTOM_ITEM_DESC_VERSION
    u16 reserved;       // 0x06
    const char *name;   // 0x08 display name (NUL-terminated)

    int base_kind;      // 0x0c ItemKind to clone behavior from (0..ITKIND_NUM-1)
    u32 flags;          // 0x10 (v4+) CUSTOM_ITEM_FLAG_*; 0 in older descriptors

    void *model;        // 0x14 optional JOBJDesc* model override (NULL = inherit base_kind)
    void *effect_info;  // 0x18 optional PatchEffectInfo* stat-grant override (NULL = inherit);
                        //      its group field is the kind's BAD/GOOD/FAKE group

    // The sky picker draws from the union of the three box pools, so weight_box
    // covers sky drops too and weight_free is unused. Box chances are u8 in the
    // engine and saturate at 255; event weights are u16 and used unclamped.
    u16 weight_box[3];  // 0x1c spawn weight in the blue/green/red box pools (0-255; 0 = never)
    u16 weight_free;    // 0x22 reserved
    u16 weight_event[CUSTOM_ITEM_EVSRC_NUM]; // 0x24 weight per event source (0 = never)

    u32 model_flag;     // 0x30 (v2+) itData render flag (0x02000000 flat; 0x03/0x05/0x0b skinned)
    float scale;        // 0x34 (v3+) multiplier over the base kind's scale (0 or 1.0 = inherit)

    // (v5+) optional AnimJointDesc* bound in place of the base kind's joint
    // animation, in every anim slot. A joint animation is authored against one
    // joint tree and binds by tree position, so the base kind's belongs to the
    // base kind's model. NULL = inherit.
    void *joint_anim;   // 0x38
} CustomItemDesc;

// Invoked when a rider collects a custom item; `player` is the 0..4 slot.
typedef void (*CustomItemPickupFn)(u32 id_hash, const char *name, int player);

// Published via Hoshi_ExportMod. Items are addressed by their id hash (derived
// from the .dat's FST path), stable across reboots and folder reordering.
typedef struct CustomItemsAPI
{
    // Number of discovered custom items.
    int (*GetCount)(void);

    // Id hash of the index-th item (0 if out of range).
    u32 (*GetIdHash)(int index);

    // Display name of the index-th item (NULL if out of range).
    const char *(*GetName)(int index);

    // 1 if enabled for spawning: master toggle AND the player's per-item menu
    // toggle AND the consumer gate below.
    int (*IsEnabled)(u32 id_hash);

    // Set this consumer's gate on an item. Independent of the player's menu
    // toggle, which stays theirs - either one off keeps the item out of the
    // round. Gates default open, so an item nobody calls this on spawns freely.
    void (*SetEnabled)(u32 id_hash, int enabled);

    // ItemKind assigned this round, or -1 if not registered yet this scene.
    int (*GetAssignedKind)(u32 id_hash);

    // Subscribe/unsubscribe a pickup handler; every registered handler runs on
    // each pickup. Add is a no-op if present or the list is full.
    void (*AddPickupHandler)(CustomItemPickupFn handler);
    void (*RemovePickupHandler)(CustomItemPickupFn handler);
} CustomItemsAPI;

#endif // CUSTOM_ITEMS_API_H
