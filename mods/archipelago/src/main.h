#ifndef ARCHIPELAGO_MAIN_H
#define ARCHIPELAGO_MAIN_H

#include "structs.h"
#include "event.h"

typedef struct TemplateSave
{
    uint boot_num;
    uint item_received_index;
} TemplateSave;

typedef enum APItemKind
{
    ITEM_KIND_PROGRESSIVE_STADIUM,
    ITEM_KIND_PATCH,
    ITEM_KIND_EFFECT,
    ITEM_KIND_CHECKBOX_FILLER,
    ITEM_KIND_PATCH_CAP_INCREASE,
    ITEM_KIND_FILLER,
    ITEM_KIND_CITY_TRIAL_EVENT,
} APItemKind;

typedef enum APItemClassification
{
    ITEM_CLASSIFICATION_PROGRESSION,
    ITEM_CLASSIFICATION_USEFUL,
    ITEM_CLASSIFICATION_FILLER,
    ITEM_CLASSIFICATION_TRAP,
} APItemClassification;

typedef struct APItem {
    uint id;
    APItemKind kind;
    APItemClassification classification;
} APItem;

// Shared data struct that is stored at a static location in memory.
typedef struct ArchipelagoData
{
    float energy;
    uint deathlink_receive;
    uint deathlink_send;
    APItem item_queue[500];   // FIFO queue for items
    uint item_queue_head;     // Index to dequeue from (read position)
    uint item_queue_tail;     // Index to enqueue to (write position)
    uint item_queue_count;    // Number of items currently in queue
} ArchipelagoData;

// Global variables for menu bindings. These are automatically loaded in by Hoshi on SaveLoaded.
typedef struct HoshiMenuSettings {
    int deathlink_enabled;
    int energylink_enabled;
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
void OnFrame();

#endif // ARCHIPELAGO_MAIN_H