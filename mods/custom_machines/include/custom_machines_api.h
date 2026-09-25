#ifndef CUSTOM_MACHINES_API_H
#define CUSTOM_MACHINES_API_H

#include "datatypes.h"
#include "menu.h"

// New air ride machines loaded from .dat archives in the FST machines/ folder.
//
// A custom machine is a star or a bike appended past its class's vanilla slots - star
// slot 19 and up, bike slot 7 and up - with a MachineKind appended past VCKIND_NUM in
// registration order, whatever its class. hoshi's MachineKind_ClassIndex /
// MachineKind_FromClassIndex describe vanilla only; resolve through KindFromClassIndex /
// ClassIndexFromKind below.
//
// This mod owns both select screens' packing and the City Trial field spawn roll,
// because it widened the tables they read. With no consumer attached both
// reproduce the engine's own behavior; a consumer narrows them through
// SetAvailabilityFilter and SetSpawnWeightFilter.

#define CUSTOM_MACHINES_MOD_NAME  "custom_machines"
#define CUSTOM_MACHINES_API_MAJOR 4
#define CUSTOM_MACHINES_API_MINOR 0

struct JOBJ;
struct MachineData;
struct DmgLog;

// Gates who gets packed into a select screen's icon list. `default_available` is the
// engine's own answer for that CharacterKind on the screen being packed - its
// checklist unlock for a vanilla character, 1 for an appended one. Returns 1 to give
// the character an icon.
typedef int (*CustomMachineAvailabilityFilter)(int character_kind, int default_available);

// Weighs one MachineKind in the City Trial field spawn roll. `default_weight` is
// VcCommon.dat's own chance for a vanilla kind in the match-progress window being
// rolled, and the descriptor's spawn_weight for a registered one; 0 keeps the kind
// off the field. The vanilla table's per-machine entries run 6-10 out of ~111-119.
typedef float (*CustomMachineSpawnWeightFilter)(int machine_kind, float default_weight);

// Told about every KO the engine records, after its own bookkeeping has run. This
// mod owns the one bl Ply_AddDeath and so the only seam a KO can be seen from.
// `machine_kind` is the widened kind the victim was riding; `dmg_log` is the
// victim's, whose attacker_ply names the killer.
typedef void (*CustomMachineDeathHandler)(int victim, struct DmgLog *dmg_log,
                                          int machine_kind);

// A per-kind handler. `Init` runs once as a machine of that kind is created, `Think`
// once per frame for every one of them on the field, each at the end of its class's own.
// `Anim` also runs once per frame per machine, at the end of Machine_AnimThink once the
// ColAnim overlays are applied, so a material written there is what draws.
typedef void (*CustomMachineHandler)(struct MachineData *md);

typedef struct CustomMachinesAPI
{
    // Registered machines this boot.
    int (*GetCount)(void);
    // One past the highest MachineKind / CharacterKind in use, customs included.
    int (*GetKindCeiling)(void);
    int (*GetCharacterKindCeiling)(void);

    // (is_bike, class slot) <-> MachineKind, custom slots included. Both fall
    // back to vanilla behavior for kinds and slots the registry does not own.
    int (*KindFromClassIndex)(int is_bike, int class_index);
    int (*ClassIndexFromKind)(int kind, int *out_is_bike);

    // MachineKind of the registered machine with this display name, or -1. Names are
    // unique across the registry.
    int (*FindKindByName)(const char *name);

    // One filter at a time; NULL removes it and restores the engine's own roster.
    // Safe to set from any scene - the screens ask per rebuild, not once at boot.
    void (*SetAvailabilityFilter)(CustomMachineAvailabilityFilter filter);

    // The same for the City Trial field spawn roll, asked per spawn. If a filter
    // zeroes every kind the roll is skipped and the first kind it permits is spawned,
    // so a consumer always gets one of its own rather than a machine it refused.
    void (*SetSpawnWeightFilter)(CustomMachineSpawnWeightFilter filter);

    // Up to four handlers, each told about every KO; adding one already present is a
    // no-op.
    void (*AddDeathHandler)(CustomMachineDeathHandler handler);

    // Claim a registered kind's Init, Think and Anim handler slots. NULL clears. Returns
    // 1 if the kind is a registered custom machine.
    int (*SetInitHandler)(int kind, CustomMachineHandler fn);
    int (*SetThinkHandler)(int kind, CustomMachineHandler fn);
    int (*SetAnimHandler)(int kind, CustomMachineHandler fn);

    // The live joint for a depth-first index into the machine archive's own joint
    // tree. NULL if the machine has no model loaded or the index is past its tree.
    struct JOBJ *(*GetMachineJoint)(struct MachineData *md, int joint_index);

    // One of the kind's own generators, by its index in the descriptor's `generators`:
    // the registry's copy, which the vehicle particle bank points at for the run of the
    // game, so a write reaches every particle it spawns afterward. NULL for an index the
    // machine does not bring or one discovery dropped.
    u8 *(*GetGenerator)(int kind, int index, int *out_size);

    // Put a player through the legendary assembly cutscene riding `kind`, using the
    // archive its descriptor named; VCKIND_DRAGOON and VCKIND_HYDRA run the engine's
    // own. Returns 0 when the kind has no cutscene, one is already running, the player
    // has no rider or rides as anyone but Kirby, the scene is not City Trial or is the
    // title demo, or the same cutscene already ran this scene (the engine frees its
    // archive when a run ends). What the player gets instead is the caller's decision;
    // MountMachine is the plain mount.
    int (*StartAssembly)(int kind, int ply);

    // Put a player straight onto `kind` with no presentation, through the same recreate
    // the cutscene ends in. It happens at the start of the next frame, so it is safe
    // from inside a collision or item callback, and the player's starting machine
    // follows it. Returns 0 for a kind past the ceiling or a player with no rider; a
    // mount still owed when a scene loads is dropped.
    int (*MountMachine)(int kind, int ply);
} CustomMachinesAPI;

// The widened kind space, for a consumer that has to write both a gated and an
// ungated build. A NULL `api` means this mod is not in the build, so no kind
// exists past the vanilla ceilings and each of these is hoshi's own answer.

static inline int CustomMachines_KindNum(const CustomMachinesAPI *api)
{
    return api != NULL ? api->GetKindCeiling() : VCKIND_NUM;
}

static inline int CustomMachines_CharacterKindNum(const CustomMachinesAPI *api)
{
    return api != NULL ? api->GetCharacterKindCeiling() : CKIND_NUM;
}

static inline MachineKind CustomMachines_ResolveKind(const CustomMachinesAPI *api,
                                                     int is_bike, int class_index)
{
    if (api != NULL)
        return (MachineKind)api->KindFromClassIndex(is_bike, class_index);
    return MachineKind_FromClassIndex(is_bike, class_index);
}

static inline int CustomMachines_ClassIndexOf(const CustomMachinesAPI *api,
                                              MachineKind kind, int *is_bike)
{
    if (api != NULL)
        return api->ClassIndexFromKind(kind, is_bike);
    *is_bike = MachineKind_IsBike(kind);
    return MachineKind_ClassIndex(kind);
}

#endif
