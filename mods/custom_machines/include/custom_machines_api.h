#ifndef CUSTOM_MACHINES_API_H
#define CUSTOM_MACHINES_API_H

#include "datatypes.h"
#include "menu.h"

// New air ride machines loaded from .dat archives in the FST machines/ folder.
//
// The engine addresses a machine as a (is_bike, class slot) pair and hard-sizes the
// star class at 19 slots. This mod widens that class, so a custom machine is star
// slot 19 and up with a MachineKind appended past VCKIND_NUM - the only kind whose
// class slot and MachineKind differ. hoshi's MachineKind_ClassIndex /
// MachineKind_FromClassIndex describe vanilla only; use KindFromClassIndex /
// ClassIndexFromKind below, falling back to the hoshi inlines when this API does
// not import.
//
// A machine that also asks for a character gets a CharacterKind appended past
// CKIND_NUM and a cell on both select grids, if one fits under GetSelectIconMax().
// This mod owns both screens' packing and the City Trial field spawn roll, because
// it is what widened the tables they read; with no consumer attached both
// reproduce the engine's own behavior, and a consumer narrows them through
// SetAvailabilityFilter and SetSpawnWeightFilter.

#define CUSTOM_MACHINES_MOD_NAME  "custom_machines"
#define CUSTOM_MACHINES_API_MAJOR 1
#define CUSTOM_MACHINES_API_MINOR 11

struct JOBJ;
struct MachineData;

// Registry cap, held here by the select screens: City Trial's packed icon list can
// grow to 33 entries before it reaches a byte CitySelect_Think reads, 20 of which
// belong to the vanilla roster. A consumer with a lower ceiling clamps its own.
#define CUSTOM_MACHINE_MAX 13

// Folder (relative to FST root) and extension scanned for drop-in machines.
#define CUSTOM_MACHINE_DROPIN_DIR "machines"
#define CUSTOM_MACHINE_DROPIN_EXT ".dat"

// A machine may drop a sound bank of the same basename beside its archive -
// machines/VcStarAp.dat and machines/VcStarAp.ssm. It is an ordinary HAL .ssm
// holding one record per MachineAudioParams sound slot, in that struct's order;
// a record with a sample rate of 0 is absent and that slot keeps the sound the
// descriptor's clone_kind uses. Build one with scripts/audio/machine_audio.py.
#define CUSTOM_MACHINE_AUDIO_EXT ".ssm"

// It may drop a second side-car of the same basename holding its UI art -
// machines/VcStarAp.dat and machines/VcStarAp.art - one image per distinct UI bank
// geometry. A machine with no side-car shares the registry's placeholder frame.
// Build one with scripts/hsd/make_machine_art.py.
#define CUSTOM_MACHINE_ART_EXT ".art"

// The art side-car exports one public named `customMachineArt`, a
// CustomMachineArt. Magic is big-endian ASCII "CMAR".
#define CUSTOM_MACHINE_ART_SYMBOL  "customMachineArt"
#define CUSTOM_MACHINE_ART_MAGIC   0x434D4152u
#define CUSTOM_MACHINE_ART_VERSION 1

struct _HSD_ImageDesc;

// One image, claimed by the geometry of the bank frame it stands in for. The
// registry matches on all three, so a side-car built against a different game
// version simply fails to match rather than writing a wrong-sized image.
typedef struct CustomMachineArtEntry
{
    u16 width;                    // 0x00
    u16 height;                   // 0x02
    u32 format;                   // 0x04 GX texture format
    struct _HSD_ImageDesc *image; // 0x08
} CustomMachineArtEntry;

typedef struct CustomMachineArt
{
    u32 magic;                     // 0x00 CUSTOM_MACHINE_ART_MAGIC
    u16 version;                   // 0x04 CUSTOM_MACHINE_ART_VERSION
    u16 count;                     // 0x06
    CustomMachineArtEntry *entry;  // 0x08
} CustomMachineArt;

// Each custom machine .dat exports its engine vcData public plus one named
// `customMachine` whose address is a CustomMachineDesc. Magic is big-endian
// ASCII "CMCH".
#define CUSTOM_MACHINE_SYMBOL       "customMachine"
#define CUSTOM_MACHINE_MAGIC        0x434D4348u
#define CUSTOM_MACHINE_DESC_VERSION 7

typedef struct CustomMachineDesc
{
    u32 magic;              // 0x00 CUSTOM_MACHINE_MAGIC
    u16 version;            // 0x04 CUSTOM_MACHINE_DESC_VERSION
    u16 reserved;           // 0x06
    const char *name;       // 0x08 display name, e.g. "Archipelago Star"
    const char *symbol;     // 0x0c the vcData public in this same archive
    int is_bike;            // 0x10 machine class; only the star class (0) is supported
    int wants_character;    // 0x14 also take a CharacterKind and a select-grid cell
    int rider_kind;         // 0x18 RiderKind for the CharacterDesc row (0 = Kirby)
    int clone_kind;         // 0x1c star MachineKind it inherits per-kind engine rows from
    float spawn_weight;     // 0x20 City Trial spawn weight (0 = never spawns loose)
    const char *description; // 0x24 select-screen blurb, v2 and up; '\n' breaks the line
    // Wall-clock material cycle, v3 and up. Every material hanging off
    // `palette_joint` - a depth-first index into the archive's own joint tree -
    // walks `palette` on a `palette_period` second loop. -1 asks for none.
    int palette_joint;      // 0x28
    float palette_period;   // 0x2c seconds for one full pass
    int palette_count;      // 0x30
    const u32 *palette;     // 0x34 one 0x00RRGGBB per entry
    // Trail tint, v5 and up. Each entry pairs a vehicle particle bank generator with
    // the byte offset inside its descriptor of an RGB triple the palette color is
    // written over. 0 asks for none.
    int trail_count;        // 0x38
    u8 trail_gen[8];        // 0x3c
    u16 trail_rgb[8];       // 0x44
    // Trail generator clones, v6 and up. Each copies one vehicle particle bank
    // generator over another, so a machine can emit and tint a generator no vanilla
    // machine reads. Remade on every bank load. 0 asks for none.
    int trail_clone_count;  // 0x54 up to 4
    u8 trail_clone_src[4];  // 0x58
    u8 trail_clone_dst[4];  // 0x5c
    // Assembly cinematic, v7 and up. FST paths; the camera animation is a second
    // public in the glow archive, as the vanilla VsDragoon.dat / VsHydra.dat pair
    // theirs. A null glow file asks for none.
    const char *cine_glow_file;    // 0x60
    const char *cine_glow_symbol;  // 0x64
    const char *cine_cam_symbol;   // 0x68
    const char *cine_parts_file;   // 0x6c
    const char *cine_parts_symbol; // 0x70
    // Vanilla legendary the cutscene runs under: 0 = Dragoon, 1 = Hydra. It picks
    // the rider's pose, the fanfare and the sky preset; nothing else reads it.
    int cine_machine_index;        // 0x74
} CustomMachineDesc;

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

// A per-kind handler on the star class. `Init` runs once as a machine of that
// kind is created, `Think` once per frame for every one of them on the field.
typedef void (*CustomMachineStarHandler)(struct MachineData *md);

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

    // Display name for a MachineKind, or NULL if it is not a registered custom.
    const char *(*GetName)(int kind);
    // MachineKind of the registered machine with this display name, or -1.
    int (*FindKindByName)(const char *name);
    // City Trial spawn weight for a registered custom kind, or 0.
    float (*GetSpawnWeight)(int kind);

    // Columns per row of the select-screen character grid, widened from the
    // vanilla 10 by one column per two appended characters. Anything iterating
    // the grid must use this rather than a literal 10.
    int (*GetGridCols)(void);
    // The ckind that fills a grid cell no character occupies. It is at or past
    // GetCharacterKindCeiling(), so an availability predicate that rejects
    // out-of-range ckinds already rejects it.
    int (*GetGridSentinel)(void);

    // Icons a select screen can lay out, widened from the vanilla 20. Code that
    // packs a select list has to clamp to this: past it the list runs into the
    // field after it and the icon GObj array into the pointer after that.
    int (*GetSelectIconMax)(void);
    // Air Ride's row-layout flag - 1 when the icons wrap to two rows - which this
    // mod moves out of the packed list's way. Code that rebuilds that list has to
    // write it through here rather than at the vanilla select base +0x7a.
    void (*SetAirRideRowSplit)(void *select_base, int two_rows);

    // One filter at a time; NULL removes it and restores the engine's own roster.
    // Safe to set from any scene - the screens ask per rebuild, not once at boot.
    void (*SetAvailabilityFilter)(CustomMachineAvailabilityFilter filter);

    // The same for the City Trial field spawn roll, asked per spawn. If a filter
    // zeroes every kind the roll is skipped and the first kind it permits is spawned,
    // so a consumer always gets one of its own rather than a machine it refused.
    void (*SetSpawnWeightFilter)(CustomMachineSpawnWeightFilter filter);

    // Claim the engine's own per-kind extension slots on the star class, which
    // Machine_Star_Init and Machine_Star_Think end by calling. The clone_kind's
    // inherited handler still runs first. NULL clears. Returns 1 if the kind is a
    // registered custom machine.
    int (*SetStarInitHandler)(int kind, CustomMachineStarHandler fn);
    int (*SetStarThinkHandler)(int kind, CustomMachineStarHandler fn);

    // The live joint for a depth-first index into the machine archive's own joint
    // tree - the same numbering `palette_joint` uses. NULL if the machine has no
    // model loaded or the index is past its tree.
    struct JOBJ *(*GetMachineJoint)(struct MachineData *md, int joint_index);

    // The descriptor's palette, or NULL if it asked for none. Points into the
    // registry's own copy, which outlives the archive it was read from.
    const u32 *(*GetPalette)(int kind, int *out_count);

    // Put a player through the legendary assembly cutscene riding `kind`, using the
    // archives its descriptor named; VCKIND_DRAGOON and VCKIND_HYDRA run the engine's
    // own. Returns 0 - and leaves the caller owing whatever the cutscene would have
    // done - when the kind has no cutscene, one is already running, the player is
    // riding as anyone but Kirby, the scene is not City Trial, or a vanilla legendary
    // has already assembled this scene (its archive is freed when a run ends).
    int (*StartAssembly)(int kind, int ply);
    int (*IsAssemblyRunning)(void);

    // Queue a file to be preloaded with City Trial, the one scene whose preload seam
    // this mod owns; registered machines' cinematic archives are added for them. The
    // path is not copied. Returns 0 if the table is full.
    int (*AddCityPreload)(const char *path);
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
