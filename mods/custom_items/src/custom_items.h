#ifndef CUSTOM_ITEMS_H
#define CUSTOM_ITEMS_H

#include "datatypes.h"

#include "custom_items_api.h"

// Registry cap. The engine's box/event spawn-weight arrays hold ITKIND_NUM-1
// (68) entries, so only a few new kinds fit without growing them.
#define CUSTOM_ITEM_MAX       16
#define CUSTOM_ITEM_NAME_MAX  32

// Folder (relative to FST root) and extension scanned for drop-in items.
#define CUSTOM_ITEM_DROPIN_DIR    "items"
#define CUSTOM_ITEM_DROPIN_EXT    ".dat"

// Each custom-item .dat exports one public symbol named `customItem` whose
// address is a CustomItemDesc. Magic is big-endian ASCII "CITM".
#define CUSTOM_ITEM_SYMBOL        "customItem"
#define CUSTOM_ITEM_MAGIC         0x4349544Du
// Layout stamp. The .dat and the DOL are separate Riivolution files, so a
// descriptor whose version is not exactly this one is rejected rather than read
// against the wrong layout.
#define CUSTOM_ITEM_DESC_VERSION  7

// CustomItemDesc.flags.
// NO_MAT_ANIM: the model is not the base kind's, so the base kind's material
// animation must not be bound to it. Takes precedence over mat_anim.
#define CUSTOM_ITEM_FLAG_NO_MAT_ANIM 0x00000001u

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
// behavior from a vanilla base_kind and optionally overrides model/effect/scale.
// All pointers resolve inside the archive, so they are valid only for the
// loaded archive's scene. Every offset here is the .dat's wire format.
typedef struct CustomItemDesc
{
    u32 magic;          // 0x00 CUSTOM_ITEM_MAGIC
    u16 version;        // 0x04 CUSTOM_ITEM_DESC_VERSION
    u16 pad;            // 0x06
    const char *name;   // 0x08 display name (NUL-terminated)

    int base_kind;      // 0x0c ItemKind to clone behavior from (0..ITKIND_NUM-1)
    u32 flags;          // 0x10 CUSTOM_ITEM_FLAG_*

    void *model;        // 0x14 optional JOBJDesc* model override (NULL = inherit base_kind)
    void *effect_info;  // 0x18 optional PatchEffectInfo* stat-grant override (NULL = inherit);
                        //      its group field is the kind's BAD/GOOD/FAKE group

    u16 weight_box[3];  // 0x1c spawn weight in the blue/green/red box pools (0-255; 0 = never)
    u16 weight_event[CUSTOM_ITEM_EVSRC_NUM]; // 0x22 weight per event source (0 = never)
    u16 pad2;           // 0x2e

    u32 model_flag;     // 0x30 itData render flag (0x02000000 flat; 0x03/0x05/0x0b skinned)
    float scale;        // 0x34 multiplier over the base kind's scale (0 or 1.0 = inherit)
    void *joint_anim;   // 0x38 AnimJointDesc* replacing the base kind's, every slot (NULL = inherit)
    void *mat_anim;     // 0x3c MatAnimJointDesc* replacing the base kind's, every slot (NULL = inherit)
} CustomItemDesc;

_Static_assert(sizeof(CustomItemDesc) == 0x40, "CustomItemDesc layout is the .dat wire format");

typedef struct CustomItemEntry
{
    int  file_entrynum;             // FST entry of the .dat (re-openable across scenes)
    u32  id_hash;                   // stable identity = hash of the full FST path
    char name[CUSTOM_ITEM_NAME_MAX]; // descriptor's display name, read at discovery; filename if unreadable
    int  api_enabled;               // consumer gate, default 1; closed keeps the item out of the round
    int  assigned_kind;             // ItemKind in the extended itData[] this scene; -1 until registered
    int  load_reported;             // a round-time load failure has been reported once
} CustomItemEntry;

void CustomItems_OnBoot(void);
void CustomItems_On3DLoadStart(void);

int              CustomItems_GetCount(void);
CustomItemEntry *CustomItems_GetEntry(int index);
CustomItemEntry *CustomItems_FindByHash(u32 id_hash);
CustomItemEntry *CustomItems_AppendEntry(void); // NULL if registry full
void             CustomItems_CopyName(char *dst, const char *src);

// Fires every subscribed pickup handler (no-op if none).
void             CustomItems_FirePickup(u32 id_hash, const char *name, int player);

int CustomItems_Discover(void);                 // FST scan; fills the registry, returns count

// Loads and validates a candidate .dat, returning its descriptor or NULL. The
// descriptor is valid only for the current scene. `report` prints why it failed.
const CustomItemDesc *CustomItems_LoadDescriptor(int file_entrynum, int report);

void CustomItemRegistry_InstallHooks(void);  // install the engine splice hooks (once at boot)
void CustomItemRegistry_RegisterAll(void);   // per-round: load + validate + splice itData/weights
void CustomItemRegistry_ResetScene(void);    // drop the previous round's assignments

#endif // CUSTOM_ITEMS_H
