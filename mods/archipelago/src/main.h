#ifndef ARCHIPELAGO_MAIN_H
#define ARCHIPELAGO_MAIN_H

#include "structs.h"
#include "event.h"

#define TEXTBOX_QUEUE_SIZE 6
#define TEXTBOX_MESSAGE_SIZE 256
#define MAX_RECEIVED_ITEMS 1000

// ==========================================================================
// AP Item ID Definitions
// These must match the item IDs defined in the APWorld Python code.
// ID 0 is reserved as the "empty" sentinel for the mailbox.
// ==========================================================================
typedef enum APItemId {
    // Standalone items (1-99)
    AP_ITEM_CHECKBOX_FILLER = 1,
    AP_ITEM_PROGRESSIVE_STADIUM,
    AP_ITEM_PATCH_CAP_INCREASE,
    AP_ITEM_1_HP_TRAP,
    AP_ITEM_PERM_PATCH_ALL_UP,
    AP_ITEM_ALL_UP,
    AP_ITEM_ALL_DOWN,

    // Permanent +1 patch items (100-199, aligned to PatchKind)
    AP_PERM_PATCH_BASE = 100,
    AP_PERM_PATCH_WEIGHT = 100,     // PATCHKIND_WEIGHT
    AP_PERM_PATCH_ACCEL,            // PATCHKIND_ACCEL
    AP_PERM_PATCH_TOPSPEED,         // PATCHKIND_TOPSPEED
    AP_PERM_PATCH_TURN,             // PATCHKIND_TURN
    AP_PERM_PATCH_CHARGE,           // PATCHKIND_CHARGE
    AP_PERM_PATCH_GLIDE,            // PATCHKIND_GLIDE
    AP_PERM_PATCH_OFFENSE,          // PATCHKIND_OFFENSE
    AP_PERM_PATCH_DEFENSE,          // PATCHKIND_DEFENSE
    AP_PERM_PATCH_HP,               // PATCHKIND_HP

    // City Trial events (200-299, aligned to EventKind)
    AP_EVENT_BASE = 200,
    AP_EVENT_DYNABLADE = 200,       // EVKIND_DYNABLADE
    AP_EVENT_TAC,                   // EVKIND_TAC
    AP_EVENT_METEOR,                // EVKIND_METEOR
    AP_EVENT_PILLAR,                // EVKIND_PILLAR
    AP_EVENT_RUNAMOK,               // EVKIND_RUNAMOK
    AP_EVENT_RESTORATIONAREA,       // EVKIND_RESTORATIONAREA
    AP_EVENT_RAILFIRE,              // EVKIND_RAILFIRE
    AP_EVENT_SAMEITEM,              // EVKIND_SAMEITEM
    AP_EVENT_LIGHTHOUSE,            // EVKIND_LIGHTHOUSE
    AP_EVENT_SECRETCHAMBER,         // EVKIND_SECRETCHAMBER
    AP_EVENT_PREDICTION,            // EVKIND_PREDICTION
    AP_EVENT_MACHINEFORMATION,      // EVKIND_MACHINEFORMATION
    AP_EVENT_UFO,                   // EVKIND_UFO
    AP_EVENT_BOUNCE,                // EVKIND_BOUNCE
    AP_EVENT_FOG,                   // EVKIND_FOG
    AP_EVENT_FAKEPOWERUPS,          // EVKIND_FAKEPOWERUPS

    // Direct game items (300+, aligned to ItemKind)
    AP_ITKIND_BASE = 300,
    AP_ITKIND_BOXBLUE = 300,        // ITKIND_BOXBLUE
    AP_ITKIND_BOXGREEN,             // ITKIND_BOXGREEN
    AP_ITKIND_BOXRED,               // ITKIND_BOXRED
    AP_ITKIND_ACCEL,                // ITKIND_ACCEL
    AP_ITKIND_ACCELDOWN,            // ITKIND_ACCELDOWN
    AP_ITKIND_TOPSPEED,             // ITKIND_TOPSPEED
    AP_ITKIND_TOPSPEEDDOWN,         // ITKIND_TOPSPEEDDOWN
    AP_ITKIND_OFFENSE,              // ITKIND_OFFENSE
    AP_ITKIND_OFFENSEDOWN,          // ITKIND_OFFENSEDOWN
    AP_ITKIND_DEFENSE,              // ITKIND_DEFENSE
    AP_ITKIND_DEFENSEDOWN,          // ITKIND_DEFENSEDOWN
    AP_ITKIND_TURN,                 // ITKIND_TURN
    AP_ITKIND_TURNDOWN,             // ITKIND_TURNDOWN
    AP_ITKIND_GLIDE,                // ITKIND_GLIDE
    AP_ITKIND_GLIDEDOWN,            // ITKIND_GLIDEDOWN
    AP_ITKIND_CHARGE,               // ITKIND_CHARGE
    AP_ITKIND_CHARGEDOWN,           // ITKIND_CHARGEDOWN
    AP_ITKIND_WEIGHT,               // ITKIND_WEIGHT
    AP_ITKIND_WEIGHTDOWN,           // ITKIND_WEIGHTDOWN
    AP_ITKIND_HP,                   // ITKIND_HP
    AP_ITKIND_ALLUP,                // ITKIND_ALLUP
    AP_ITKIND_SPEEDMAX,             // ITKIND_SPEEDMAX
    AP_ITKIND_SPEEDMIN,             // ITKIND_SPEEDMIN
    AP_ITKIND_OFFENSEMAX,           // ITKIND_OFFENSEMAX
    AP_ITKIND_DEFENSEMAX,           // ITKIND_DEFENSEMAX
    AP_ITKIND_CHARGEMAX,            // ITKIND_CHARGEMAX
    AP_ITKIND_CHARGENONE,           // ITKIND_CHARGENONE
    AP_ITKIND_CANDY,                // ITKIND_CANDY
    AP_ITKIND_COPYBOMB,             // ITKIND_COPYBOMB
    AP_ITKIND_COPYFIRE,             // ITKIND_COPYFIRE
    AP_ITKIND_COPYICE,              // ITKIND_COPYICE
    AP_ITKIND_COPYSLEEP,            // ITKIND_COPYSLEEP
    AP_ITKIND_COPYTIRE,             // ITKIND_COPYTIRE
    AP_ITKIND_COPYBIRD,             // ITKIND_COPYBIRD
    AP_ITKIND_COPYPLASMA,           // ITKIND_COPYPLASMA
    AP_ITKIND_COPYTORNADO,          // ITKIND_COPYTORNADO
    AP_ITKIND_COPYSWORD,            // ITKIND_COPYSWORD
    AP_ITKIND_COPYSPIKE,            // ITKIND_COPYSPIKE
    AP_ITKIND_COPYMIC,              // ITKIND_COPYMIC
    AP_ITKIND_FOODMAXIMTOMATO,      // ITKIND_FOODMAXIMTOMATO
    AP_ITKIND_FOODENERGYDRINK,      // ITKIND_FOODENERGYDRINK
    AP_ITKIND_FOODICECREAM,         // ITKIND_FOODICECREAM
    AP_ITKIND_FOODRICEBALL,         // ITKIND_FOODRICEBALL
    AP_ITKIND_FOODCHICKEN,          // ITKIND_FOODCHICKEN
    AP_ITKIND_FOODCURRY,            // ITKIND_FOODCURRY
    AP_ITKIND_FOODRAMEN,            // ITKIND_FOODRAMEN
    AP_ITKIND_FOODOMELET,           // ITKIND_FOODOMELET
    AP_ITKIND_FOODHAMBURGER,        // ITKIND_FOODHAMBURGER
    AP_ITKIND_FOODSUSHI,            // ITKIND_FOODSUSHI
    AP_ITKIND_FOODHOTDOG,           // ITKIND_FOODHOTDOG
    AP_ITKIND_FOODAPPLE,            // ITKIND_FOODAPPLE
    AP_ITKIND_FIREWORKS,            // ITKIND_FIREWORKS
    AP_ITKIND_PANICSPIN,            // ITKIND_PANICSPIN
    AP_ITKIND_TIMEBOMB,             // ITKIND_TIMEBOMB
    AP_ITKIND_GORDO,                // ITKIND_GORDO
    AP_ITKIND_HYDRA1,               // ITKIND_HYDRA1
    AP_ITKIND_HYDRA2,               // ITKIND_HYDRA2
    AP_ITKIND_HYDRA3,               // ITKIND_HYDRA3
    AP_ITKIND_DRAGOON1,             // ITKIND_DRAGOON1
    AP_ITKIND_DRAGOON2,             // ITKIND_DRAGOON2
    AP_ITKIND_DRAGOON3,             // ITKIND_DRAGOON3
    AP_ITKIND_ACCELFAKE,            // ITKIND_ACCELFAKE
    AP_ITKIND_TOPSPEEDFAKE,         // ITKIND_TOPSPEEDFAKE
    AP_ITKIND_OFFENSEFAKE,          // ITKIND_OFFENSEFAKE
    AP_ITKIND_DEFENSEFAKE,          // ITKIND_DEFENSEFAKE
    AP_ITKIND_TURNFAKE,             // ITKIND_TURNFAKE
    AP_ITKIND_GLIDEFAKE,            // ITKIND_GLIDEFAKE
    AP_ITKIND_CHARGEFAKE,           // ITKIND_CHARGEFAKE
    AP_ITKIND_WEIGHTFAKE,           // ITKIND_WEIGHTFAKE
} APItemId;

typedef struct TemplateSave
{
    uint boot_num;
    uint item_received_count;                           // Total items received from AP client
    uint received_items[MAX_RECEIVED_ITEMS];            // Ordered list of all received AP item IDs
    uint unprocessed_items[MAX_RECEIVED_ITEMS];         // AP item IDs waiting to be applied
    uint unprocessed_count;                             // Number of items in the unprocessed list
} TemplateSave;

#include "textbox.h"
#include "ap_item_handler.h"

// Shared data struct that is stored at a static location in memory.
// The Python AP client reads/writes to this via dolphin-memory-engine.
typedef struct ArchipelagoData
{
    float energy_give;
    float energy_receive;
    uint deathlink_receive;
    uint deathlink_send;
    uint traplink_receive;
    uint traplink_send;
    uint incoming_item_id;    // Mailbox: client writes AP item ID, game reads and clears to 0
    uint item_received_index; // Mirror of save_data->item_received_count for client to read
    TextBoxMessage textbox_queue[TEXTBOX_QUEUE_SIZE];  // FIFO queue for TextBoxMessage objects
    uint textbox_queue_head;      // Index to dequeue from (read position)
    uint textbox_queue_tail;      // Index to enqueue to (write position)
    uint textbox_framecounter;    // Frame counter for textbox fade timing
} ArchipelagoData;

// Global variables for menu bindings. These are automatically loaded in by Hoshi on SaveLoaded.
typedef struct HoshiMenuSettings {
    int deathlink_enabled;
    int energylink_enabled;
    int traplink_enabled;
    int textbox_enabled;
} HoshiMenuSettings;

extern ArchipelagoData *archipelago_data;
extern HoshiMenuSettings hoshi_menu_settings;
extern TemplateSave *save_data;

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
void OnFrameStart();
void OnFrameEnd();

#endif // ARCHIPELAGO_MAIN_H