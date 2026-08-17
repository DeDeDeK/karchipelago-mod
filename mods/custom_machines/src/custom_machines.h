#ifndef CUSTOM_MACHINES_H
#define CUSTOM_MACHINES_H

#include "datatypes.h"
#include "hsd.h"
#include "machine.h"

#include "custom_machines_api.h"

#define CUSTOM_MACHINE_NAME_MAX 32
#define CUSTOM_MACHINE_PATH_MAX 64

// Two lines of about 24 characters, which is what the description box holds.
#define CUSTOM_MACHINE_DESCRIPTION_MAX 64

// Widened class slot counts. Only the star class grows; the bike row keeps the
// same width so both rows of the relocated lookup are indexed alike.
#define CUSTOM_VCSTAR_NUM (VCSTAR_NUM + CUSTOM_MACHINE_MAX)
#define CUSTOM_VCKIND_NUM (VCKIND_NUM + CUSTOM_MACHINE_MAX)
#define CUSTOM_CKIND_NUM  (CKIND_NUM + CUSTOM_MACHINE_MAX)

typedef struct CustomMachineEntry
{
    char name[CUSTOM_MACHINE_NAME_MAX];    // descriptor name, or the filename if it has none
    char path[CUSTOM_MACHINE_PATH_MAX];    // full FST path, handed to the engine loader
    char symbol[CUSTOM_MACHINE_NAME_MAX];  // the archive's vcData public
    char description[CUSTOM_MACHINE_DESCRIPTION_MAX]; // select-screen blurb, empty if none
    int machine_kind;                      // appended MachineKind, -1 until registered
    int character_kind;                    // appended CharacterKind, -1 if none
    int star_slot;                         // class slot in the widened star class
    int rider_kind;                        // RiderKind for the CharacterDesc row
    int clone_kind;                        // star kind whose per-kind engine rows it inherits
    float spawn_weight;                    // City Trial spawn weight
    int palette_joint;                     // joint whose materials cycle, -1 if none
    float palette_period;                  // seconds for one pass through the palette
    int palette_count;
    const u32 *palette;                    // into the boot-time archive, which is never freed
} CustomMachineEntry;

void CustomMachines_OnBoot(void);

int                 CustomMachines_GetCount(void);
int                 CustomMachines_GetCharacterKindCeiling(void);
CustomMachineEntry *CustomMachines_GetEntry(int index);
CustomMachineEntry *CustomMachines_FindByKind(int machine_kind);
CustomMachineEntry *CustomMachines_FindByCharacterKind(int character_kind);
void                CustomMachines_CopyStr(char *dst, const char *src, int max);

// Point an accessor's `lis` / `addi` pair at a relocated copy of its table.
void CustomMachines_RepointTable(u32 lis_addr, u32 addi_addr, const void *table);

// Read an archive off the disc during OnBoot, before any scene heap exists. The
// buffer is never freed, so the archive stays resident for the run of the game.
HSD_Archive *CustomMachines_LoadArchiveAtBoot(char *path);

// FST scan of machines/, validating each descriptor. Returns the count.
int CustomMachines_Discover(void);

// machine_registry.c - the widened star class.
void CustomMachineRegistry_OnBoot(void);

// character_registry.c - appended CharacterDesc rows and select-grid cells.
void CustomMachineCharacter_OnBoot(void);
int  CustomMachineCharacter_GetGridCols(void);
int  CustomMachineCharacter_GetSentinel(void);

// select_text.c - the select screens' machine name and description text.
void CustomMachineText_OnBoot(void);

// machine_audio.c - the star class's audio parameter array and drop-in banks.
void CustomMachineAudio_OnBoot(void);

// machine_palette.c - the wall-clock material cycle a descriptor may ask for.
void CustomMachinePalette_OnBoot(void);

// ui_frames.c - the 21st frame spliced into each character-indexed art bank.
void CustomMachineUiFrames_OnBoot(void);

// select_screen.c - the widened UI art banks, icon grids and icon-list packing.
void CustomMachineSelect_OnBoot(void);
int  CustomMachineSelect_GetIconMax(void);
void CustomMachineSelect_SetAirRideRowSplit(void *select_base, int two_rows);
void CustomMachineSelect_SetAvailabilityFilter(CustomMachineAvailabilityFilter filter);

#endif
