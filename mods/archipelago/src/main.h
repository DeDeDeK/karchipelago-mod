#include "structs.h"

typedef struct TemplateSave
{
    int boot_num;
    int item_received_index;
} TemplateSave;

typedef struct PerFrameFuncData
{
    int timer;
} PerFrameFuncData;


typedef struct ArchipelagoData
{
    float energy;
    int deathlink_give; 
    // Global variables for menu bindings. These are automatically loaded in by Hoshi on SaveLoaded
    int deathlink_enabled;
    int energylink_enabled;
    int textbox_enabled;
} ArchipelagoData;

extern ArchipelagoData archipelago_data;

void OnBoot();
void OnSaveInit();
void OnSaveLoaded();
void OnMainMenuLoad();
void OnPlayerSelectLoad();
void On3DLoad();
void On3DPause(int pause_ply);
void On3DUnpause(int pause_ply);
void On3DExit();
void OnSceneChange();