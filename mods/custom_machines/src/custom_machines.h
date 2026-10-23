#ifndef CUSTOM_MACHINES_H
#define CUSTOM_MACHINES_H

#include "datatypes.h"
#include "hsd.h"
#include "game.h"
#include "menu.h"
#include "machine.h"

#include "custom_machines_api.h"
#include "custom_machine_desc.h"

// Registry cap across both classes. Each class, the UI art banks and the per-machine
// counters grow by this many.
#define CUSTOM_MACHINE_MAX 13

// Icons a select screen can carry: the vanilla roster plus a character per machine.
#define SELECT_ICON_MAX (CKIND_NUM + CUSTOM_MACHINE_MAX)

#define CUSTOM_MACHINE_NAME_MAX 32
#define CUSTOM_MACHINE_PATH_MAX 64

// Two lines of about 24 characters, which is what the description box holds.
#define CUSTOM_MACHINE_DESCRIPTION_MAX 64

// Widened class slot counts. Either class can take every registered machine, and both
// rows of the relocated vcData lookup are as wide as the star row.
#define CUSTOM_VCSTAR_NUM  (VCSTAR_NUM + CUSTOM_MACHINE_MAX)
#define CUSTOM_VCWHEEL_NUM (VCWHEEL_NUM + CUSTOM_MACHINE_MAX)
#define CUSTOM_VCKIND_NUM  (VCKIND_NUM + CUSTOM_MACHINE_MAX)
#define CUSTOM_CKIND_NUM   (CKIND_NUM + CUSTOM_MACHINE_MAX)

// Stat rows a descriptor lists for its class, at most.
#define CUSTOM_MACHINE_STAT_ROW_MAX CUSTOM_MACHINE_BIKE_STAT_ROW_NUM

#define CUSTOM_MACHINE_PARTICLE_SLOT_MAX VCSTAR_PARTICLE_SLOT_NUM

static inline int CustomMachines_VanillaSlotNum(int is_bike)
{
    return is_bike ? VCWHEEL_NUM : VCSTAR_NUM;
}

static inline const char *CustomMachines_ClassName(int is_bike)
{
    return is_bike ? "bike" : "star";
}

// The class's animation bank particle slots, each a vehicle bank generator id or -1.
static inline int *CustomMachines_ParticleSlots(int is_bike, void *anim, int *out_num)
{
    if (is_bike)
    {
        *out_num = VCWHEEL_PARTICLE_SLOT_NUM;
        return ((vcAnimationWheel *)anim)->particle;
    }
    *out_num = VCSTAR_PARTICLE_SLOT_NUM;
    return ((vcAnimationStar *)anim)->particle;
}

typedef struct CustomMachineEntry
{
    char name[CUSTOM_MACHINE_NAME_MAX];               // descriptor name, or the filename if it has none
    char path[CUSTOM_MACHINE_PATH_MAX];               // full FST path, handed to the engine loader
    char symbol[CUSTOM_MACHINE_NAME_MAX];             // the archive's vcData public
    char description[CUSTOM_MACHINE_DESCRIPTION_MAX]; // select-screen blurb, empty if none
    int machine_kind;                                 // appended MachineKind
    int character_kind;                               // appended CharacterKind, -1 if none
    int is_bike;                                      // machine class
    int class_slot;                                   // slot in its widened class
    int rider_kind;                                   // RiderKind for the CharacterDesc row
    int audio_kind;                                   // same-class kind voicing what its bank lacks
    float spawn_weight;                               // City Trial spawn weight
    float blip_height;                                // field blip height, in the engine table's units
    float radar_drop;                                 // stat radar screen model drop, in the same units
    int generator_count;                              // vehicle particle bank generators it brings
    int generator_base;                               // bank id its first generator is installed at
    u16 generator_size[CUSTOM_MACHINE_GENERATOR_MAX]; // 0 for one discovery dropped
    u8 generator[CUSTOM_MACHINE_GENERATOR_MAX][CUSTOM_MACHINE_GENERATOR_SIZE];
    int particle[CUSTOM_MACHINE_PARTICLE_SLOT_MAX];   // its class's animation bank particle slots, as installed
    CustomMachineCpu cpu;
    int cine_machine_index;                           // vanilla legendary the cutscene runs under, -1 if none
    char cine_file[CUSTOM_MACHINE_NAME_MAX];
    char cine_symbol[CUSTOM_MACHINE_NAME_MAX];
    float stat_rows[CUSTOM_MACHINE_STAT_ROW_MAX][2];  // its class's rows, in that class's order
} CustomMachineEntry;

void CustomMachines_OnBoot(void);
void CustomMachines_On3DLoadStart(void);

int                 CustomMachines_GetCount(void);
int                 CustomMachines_GetClassCount(int is_bike);
int                 CustomMachines_GetKindCeiling(void);
int                 CustomMachines_GetCharacterKindCeiling(void);
CustomMachineEntry *CustomMachines_GetEntry(int index);
CustomMachineEntry *CustomMachines_FindByKind(int machine_kind);
// NULL for every vanilla slot, which is the common case on the per-frame paths.
CustomMachineEntry *CustomMachines_FindByClassSlot(int is_bike, int class_slot);
void                CustomMachines_CopyStr(char *dst, const char *src, int max);

// Registry order, which is also the order the appended kinds were handed out in.
static inline int CustomMachines_Index(const CustomMachineEntry *e)
{
    return e->machine_kind - VCKIND_NUM;
}

// (is_bike, class slot) <-> MachineKind, custom slots included.
int CustomMachines_KindFromClassIndex(int is_bike, int class_index);
int CustomMachines_ClassIndexFromKind(int kind, int *out_is_bike);

// A machine's side-car path: its own with the extension swapped. Returns 0 if it
// does not fit or the source has no extension to swap.
int CustomMachines_SideCarPath(char *dst, int max, const char *src, const char *ext);

void CustomMachineRegistry_OnBoot(void);
int  CustomMachineRegistry_SetInitHandler(int machine_kind, CustomMachineHandler fn);
int  CustomMachineRegistry_SetThinkHandler(int machine_kind, CustomMachineHandler fn);
int  CustomMachineRegistry_SetAnimHandler(int machine_kind, CustomMachineHandler fn);

void CustomMachineStatScaling_OnBoot(void);

void CustomMachineCharacterRegistry_OnBoot(void);
int  CustomMachineCharacterRegistry_GetGridCols(void);

void CustomMachineSelectText_OnBoot(void);

void CustomMachineAudio_OnBoot(void);
// Widen a class's sound table, just loaded with its shared archive.
void CustomMachineAudio_OnClassLoad(int is_bike);

void CustomMachineTrailBank_OnBoot(void);

void CustomMachineCpu_OnBoot(void);

void CustomMachineStats_OnBoot(void);
void CustomMachineStats_On3DLoadStart(void);
void CustomMachineStats_AddDeathHandler(CustomMachineDeathHandler handler);

void CustomMachineHud_OnBoot(void);

void CustomMachineSpawn_OnBoot(void);
void CustomMachineSpawn_SetWeightFilter(CustomMachineSpawnWeightFilter filter);

void CustomMachineCinematic_OnBoot(void);
void CustomMachineCinematic_On3DLoadStart(void);
int  CustomMachineCinematic_Start(int machine_kind, int ply);

int  CustomMachineMount_Queue(int machine_kind, int ply);
void CustomMachineMount_OnSceneChange(void);
int  CustomMachineMount_GetRespawnKind(int ply);
void CustomMachineMount_SetRespawnKind(int ply, int machine_kind);

void CustomMachineUiFrames_OnBoot(void);
// Whether every bank in the archive with this basename grew on its latest load.
int  CustomMachineUiFrames_IsGrown(const char *name);

void CustomMachineSelectScreen_OnBoot(void);
void CustomMachineSelectScreen_SetAvailabilityFilter(CustomMachineAvailabilityFilter filter);

#endif
