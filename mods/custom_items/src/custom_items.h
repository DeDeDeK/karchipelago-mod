#ifndef CUSTOM_ITEMS_H
#define CUSTOM_ITEMS_H

#include "datatypes.h"
#include "item.h"

#include "custom_items_api.h"

// Every registered kind takes a slot in each 68-wide box pool, which City Trial
// fills to at most 38.
#define CUSTOM_ITEM_MAX       16
#define CUSTOM_ITEM_NAME_MAX  32

// Each custom-item .dat exports one public symbol named `customItem` whose
// address is a CustomItemDesc. Magic is big-endian ASCII "CITM".
#define CUSTOM_ITEM_SYMBOL        "customItem"
#define CUSTOM_ITEM_MAGIC         0x4349544Du
// Layout stamp: a descriptor of any other version is rejected, never read.
#define CUSTOM_ITEM_DESC_VERSION  7

// The box pools store a chance as a u8, and _CityItem_GetEventItem (0x800ebe44)
// sign-extends the event-source chance, so heavier weights saturate here.
#define CUSTOM_ITEM_BOX_WEIGHT_MAX   0xff
#define CUSTOM_ITEM_EVENT_WEIGHT_MAX 0x7fff

// CustomItemDesc.flags.
// NO_MAT_ANIM: the model is not the base kind's, so the base kind's material
// animation must not be bound to it. Takes precedence over mat_anim.
#define CUSTOM_ITEM_FLAG_NO_MAT_ANIM 0x00000001u

// Chance columns of the engine's event_source_drop[] rows; indexes weight_event[].
typedef enum CustomItemEventSource
{
    CUSTOM_ITEM_EVSRC_DYNABLADE,
    CUSTOM_ITEM_EVSRC_TAC,
    CUSTOM_ITEM_EVSRC_METEOR,
    CUSTOM_ITEM_EVSRC_DESTRUCTIBLE,
    CUSTOM_ITEM_EVSRC_CHAMBER,
    CUSTOM_ITEM_EVSRC_UFO,
    CUSTOM_ITEM_EVSRC_NUM,
} CustomItemEventSource;

// The new kind clones a vanilla base_kind and overrides whatever is set below.
// Pointers resolve inside the archive and live as long as it does; offsets are
// the .dat's wire format.
typedef struct CustomItemDesc
{
    u32 magic;                     // 0x00 CUSTOM_ITEM_MAGIC
    u16 version;                   // 0x04 CUSTOM_ITEM_DESC_VERSION
    u16 pad;                       // 0x06
    const char *name;              // 0x08 display name
    int base_kind;                 // 0x0c ItemKind to clone behavior from
    u32 flags;                     // 0x10 CUSTOM_ITEM_FLAG_*
    JOBJDesc *model;               // 0x14 model override (NULL = inherit)
    PatchEffectInfo *effect_info;  // 0x18 stat grants and group override (NULL = inherit)
    u16 weight_box[BOXKIND_NUM];   // 0x1c blue/green/red box pool weight (0 = never)
    u16 weight_event[CUSTOM_ITEM_EVSRC_NUM]; // 0x22 weight per event source (0 = never)
    u16 pad2;                      // 0x2e
    u32 model_flag;                // 0x30 itData render flag for model
    float scale;                   // 0x34 multiplier over the base kind's scale (0 or 1.0 = inherit)
    AnimJointDesc *joint_anim;     // 0x38 replaces the base kind's in every slot (NULL = inherit)
    MatAnimJointDesc *mat_anim;    // 0x3c replaces the base kind's in every slot (NULL = inherit)
} CustomItemDesc;

typedef struct CustomItemEntry
{
    int  file_entrynum;              // FST entry of the .dat
    u32  id_hash;                    // hash of the full FST path, never 0
    char name[CUSTOM_ITEM_NAME_MAX]; // descriptor's display name
    int  api_enabled;                // spawn gate, default 1
    int  assigned_kind;              // ItemKind this scene, -1 until registered
} CustomItemEntry;

void CustomItems_OnBoot(void);
void CustomItems_On3DLoadStart(void);

int              CustomItems_GetCount(void);
CustomItemEntry *CustomItems_GetEntry(int index);
CustomItemEntry *CustomItems_AppendEntry(void); // the caller checks there is room
void             CustomItems_FirePickup(u32 id_hash, int player);

int CustomItems_Discover(void); // FST scan; fills the registry, returns count

void CustomItemRegistry_InstallHooks(void);
int  CustomItemRegistry_GetItemKind(ItemData *item);
void CustomItemRegistry_ResetScene(void);

#endif // CUSTOM_ITEMS_H
