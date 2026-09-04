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

// Palette entries copied out of a descriptor. The archive is freed once discovery
// is done, so the colors cannot be left pointing into it.
#define CUSTOM_MACHINE_PALETTE_MAX 16

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
    u32 palette[CUSTOM_MACHINE_PALETTE_MAX];
    int trail_count;                       // tinted RGB triples in the vehicle particle bank
    u8 trail_gen[8];                       // generator each offset belongs to
    u16 trail_rgb[8];                      // byte offset of an RGB triple in that generator
    int trail_clone_count;                 // spare bank slots this machine claims
    u8 trail_clone_src[4];                 // generator copied
    u8 trail_clone_dst[4];                 // slot it is copied into
    int cine_machine_index;                // vanilla legendary the cutscene runs under, -1 if none
    char cine_glow_file[CUSTOM_MACHINE_NAME_MAX];
    char cine_glow_symbol[CUSTOM_MACHINE_NAME_MAX];
    char cine_cam_symbol[CUSTOM_MACHINE_NAME_MAX];
    char cine_parts_file[CUSTOM_MACHINE_NAME_MAX];
    char cine_parts_symbol[CUSTOM_MACHINE_NAME_MAX];
} CustomMachineEntry;

void CustomMachines_OnBoot(void);
void CustomMachines_On3DLoadStart(void);

int                 CustomMachines_GetCount(void);
int                 CustomMachines_GetKindCeiling(void);
int                 CustomMachines_GetCharacterKindCeiling(void);
CustomMachineEntry *CustomMachines_GetEntry(int index);
CustomMachineEntry *CustomMachines_FindByKind(int machine_kind);
CustomMachineEntry *CustomMachines_FindByCharacterKind(int character_kind);
void                CustomMachines_CopyStr(char *dst, const char *src, int max);

// Point an accessor's `lis` / `addi` pair at a relocated copy of its table.
void CustomMachines_RepointTable(u32 lis_addr, u32 addi_addr, const void *table);

// The live joint for a depth-first index into the machine archive's own joint
// tree. The index is resolved against the JObjDesc tree and the joint found by
// the back-pointer JObjLoad (0x8040add4) leaves at JOBJ+0x84, which holds however
// the engine roots the instance.
JOBJ *CustomMachines_GetMachineJoint(MachineData *md, int joint_index);

// Read an archive off the disc during OnBoot, before the HSD heap exists. The
// storage comes from hoshi's persistent arena and stays resident; a caller that
// only reads the file and drops it brackets the load in ArenaMark/ArenaRelease,
// which is valid only while nothing allocated in between is still held.
HSD_Archive *CustomMachines_LoadArchiveAtBoot(char *path);
void *CustomMachines_ArenaMark(void);
void CustomMachines_ArenaRelease(void *mark);

// A machine's side-car path: its own with the extension swapped. Returns 0 if it
// does not fit or the source has no extension to swap.
int CustomMachines_SideCarPath(char *dst, int max, const char *src, const char *ext);

// FST scan of machines/, validating each descriptor. Returns the count.
int CustomMachines_Discover(void);

// machine_registry.c - the widened star class.
void CustomMachineRegistry_OnBoot(void);
// Layer a handler onto a custom kind's slot in one of the two per-kind tables the
// star class dispatches through: 0 = Machine_Star_Init, 1 = Machine_Star_Think.
int  CustomMachineRegistry_SetStarHandler(int table, int machine_kind,
                                          void (*fn)(MachineData *));

// character_registry.c - appended CharacterDesc rows and select-grid cells.
void CustomMachineCharacter_OnBoot(void);
int  CustomMachineCharacter_GetGridCols(void);
int  CustomMachineCharacter_GetSentinel(void);

// select_text.c - the select screens' machine name and description text.
void CustomMachineText_OnBoot(void);

// machine_audio.c - the star class's audio parameter array and drop-in banks.
void CustomMachineAudio_OnBoot(void);

// trail_bank.c - private copies of shared vehicle particle bank generators.
void CustomMachineTrail_OnBoot(void);

// machine_palette.c - the wall-clock material cycle a descriptor may ask for.
void CustomMachinePalette_OnBoot(void);

// machine_stats.c - the widened per-machine counter arrays.
void CustomMachineStats_OnBoot(void);
void CustomMachineStats_On3DLoadStart(void);

// machine_blip.c - the City Trial blip a custom machine borrows.
void CustomMachineBlip_OnBoot(void);

// machine_spawn.c - the City Trial field spawn roll.
void CustomMachineSpawn_OnBoot(void);
void CustomMachineSpawn_SetWeightFilter(CustomMachineSpawnWeightFilter filter);

// machine_preload.c - files this mod adds to City Trial's preload set.
void CustomMachinePreload_OnBoot(void);
int  CustomMachinePreload_Add(const char *path);

// machine_cinematic.c - the vanilla legendary cutscene, driven by a descriptor.
void CustomMachineCinematic_OnBoot(void);
void CustomMachineCinematic_On3DLoadStart(void);
int  CustomMachineCinematic_Start(int machine_kind, int ply);
int  CustomMachineCinematic_IsRunning(void);

// ui_frames.c - the 21st frame spliced into each character-indexed art bank.
void CustomMachineUiFrames_OnBoot(void);

// select_screen.c - the widened UI art banks, icon grids and icon-list packing.
void CustomMachineSelect_OnBoot(void);
int  CustomMachineSelect_GetIconMax(void);
void CustomMachineSelect_SetAirRideRowSplit(void *select_base, int two_rows);
void CustomMachineSelect_SetAvailabilityFilter(CustomMachineAvailabilityFilter filter);

#endif
