#ifndef ARCHIPELAGO_MAIN_H
#define ARCHIPELAGO_MAIN_H

#include "structs.h"
#include "event.h"

#define TEXTBOX_QUEUE_SIZE 5
#define TEXTBOX_MESSAGE_SIZE 256
#define ITEM_QUEUE_SIZE 500

// ==========================================================================
// AP Item ID Definitions
// These must match the item IDs defined in the APWorld Python code.
// ==========================================================================

// Items that apply immediately (no map required)
#define AP_ITEM_CHECKBOX_FILLER       1
#define AP_ITEM_PROGRESSIVE_STADIUM   2
#define AP_ITEM_PATCH_CAP_INCREASE    3
#define AP_ITEM_HP_TRAP               4

// Permanent +1 patch items (aligned to PatchKind enum)
#define AP_PERM_PATCH_BASE            5
// 5  = +1 Weight     (PATCHKIND_WEIGHT)
// 6  = +1 Accel      (PATCHKIND_ACCEL)
// 7  = +1 Top Speed  (PATCHKIND_TOPSPEED)
// 8  = +1 Turn       (PATCHKIND_TURN)
// 9  = +1 Charge     (PATCHKIND_CHARGE)
// 10 = +1 Glide      (PATCHKIND_GLIDE)
// 11 = +1 Offense    (PATCHKIND_OFFENSE)
// 12 = +1 Defense    (PATCHKIND_DEFENSE)
// 13 = +1 HP         (PATCHKIND_HP)
#define AP_ITEM_PERM_ALL_UP           14

// City Trial events (aligned to EventKind enum)
#define AP_EVENT_BASE                 15
// 15 = Dyna Blade         (EVKIND_DYNABLADE)
// 16 = TAC                (EVKIND_TAC)
// 17 = Meteor             (EVKIND_METEOR)
// 18 = Pillar             (EVKIND_PILLAR)
// 19 = Run Amok           (EVKIND_RUNAMOK)
// 20 = Restoration Area   (EVKIND_RESTORATIONAREA)
// 21 = Rail Fire          (EVKIND_RAILFIRE)
// 22 = Same Item          (EVKIND_SAMEITEM)
// 23 = Lighthouse         (EVKIND_LIGHTHOUSE)
// 24 = Secret Chamber     (EVKIND_SECRETCHAMBER)
// 25 = Prediction         (EVKIND_PREDICTION)
// 26 = Machine Formation  (EVKIND_MACHINEFORMATION)
// 27 = UFO                (EVKIND_UFO)
// 28 = Bounce             (EVKIND_BOUNCE)
// 29 = Fog                (EVKIND_FOG)
// 30 = Fake Powerups      (EVKIND_FAKEPOWERUPS)

// Direct ITKIND items: AP item ID = AP_ITKIND_BASE + ItemKind value
#define AP_ITKIND_BASE                100
// 100 = ITKIND_BOXBLUE
// 101 = ITKIND_BOXGREEN
// 102 = ITKIND_BOXRED
// 103 = ITKIND_ACCEL
// ...
// 100 + (ITKIND_NUM - 1) = ITKIND_WEIGHTFAKE

typedef struct TemplateSave
{
    uint boot_num;
    uint item_received_index;
} TemplateSave;

#include "textbox.h"
#include "item_queue.h"

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
    uint item_received_index; // Mirror of save_data->item_received_index for client to read
    uint item_queue[ITEM_QUEUE_SIZE];     // Internal FIFO queue of AP item IDs (game-side only)
    uint item_queue_head;     // Game reads from here (consumer)
    uint item_queue_tail;     // Game writes here (producer from mailbox)
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