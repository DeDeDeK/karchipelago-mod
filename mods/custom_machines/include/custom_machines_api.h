#ifndef CUSTOM_MACHINES_API_H
#define CUSTOM_MACHINES_API_H

#include "datatypes.h"

// New air ride machines loaded from .dat archives in the FST machines/ folder.
//
// The engine addresses machines as a (is_bike, class slot) pair and hard-sizes
// the star class at 19 slots. This mod widens that class, so a custom machine is
// star slot 19 and up. Its MachineKind is appended past VCKIND_NUM, which means
// a custom machine is the only kind whose class slot and MachineKind differ -
// hoshi's MachineKind_ClassIndex / MachineKind_FromClassIndex describe vanilla
// only and give the wrong answer for these. Use KindFromClassIndex /
// ClassIndexFromKind below instead, and fall back to the hoshi inlines when this
// API does not import (the mod is not built, so no custom machine exists).
//
// A machine that also asks for a character gets a CharacterKind appended past
// CKIND_NUM and a cell on both select-screen grids, plus its descriptor's name and
// description under the cursor. The select screens have room for
// GetSelectIconMax() icons in total, so only the machines whose characters fit
// under that are given one; the rest still register, spawn and drive.
//
// This mod owns the packing of both character select screens, because it is what
// widened the grid they are packed from. With no consumer attached it packs the
// engine's own roster plus every appended character, so a drop-in machine is
// selectable on its own. A consumer that gates characters narrows that through
// SetAvailabilityFilter instead of replacing the packing itself.

#define CUSTOM_MACHINES_MOD_NAME  "custom_machines"
#define CUSTOM_MACHINES_API_MAJOR 1
#define CUSTOM_MACHINES_API_MINOR 5

struct JOBJ;
struct MachineData;

// Registry cap.
#define CUSTOM_MACHINE_MAX 4

// Folder (relative to FST root) and extension scanned for drop-in machines.
#define CUSTOM_MACHINE_DROPIN_DIR "machines"
#define CUSTOM_MACHINE_DROPIN_EXT ".dat"

// A machine may drop a sound bank of the same basename beside its archive -
// machines/VcStarAp.dat and machines/VcStarAp.ssm. It is an ordinary HAL .ssm
// holding one record per MachineAudioParams sound slot, in that struct's order;
// a record with a sample rate of 0 is absent and that slot keeps the sound the
// descriptor's clone_kind uses. Build one with scripts/audio/machine_audio.py.
#define CUSTOM_MACHINE_AUDIO_EXT ".ssm"

// A machine may also ask for one of its joints to cycle through a color palette
// on a wall clock. Nothing in an archive can do that on its own: a MatAnim's
// frame is the machine's state, so the moving animation's rate rides on velocity
// and the charge animation's frame is the charge gauge, and both restart when
// the state changes. So the palette travels in the descriptor and this mod
// writes it into the live materials each frame. It reaches a pixel only where
// the joint's materials render with RENDER_CONSTANT and their texture stages
// leave the color alone or modulate it - a stage set to REPLACE or to a
// full-strength BLEND paints over the material and nothing will show.

// Each custom machine .dat exports its engine vcData public plus one named
// `customMachine` whose address is a CustomMachineDesc. Magic is big-endian
// ASCII "CMCH".
#define CUSTOM_MACHINE_SYMBOL       "customMachine"
#define CUSTOM_MACHINE_MAGIC        0x434D4348u
#define CUSTOM_MACHINE_DESC_VERSION 3

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
    // walks `palette` on a `palette_period` second loop. -1 asks for none, which
    // is what a machine without the field reads as.
    int palette_joint;      // 0x28
    float palette_period;   // 0x2c seconds for one full pass
    int palette_count;      // 0x30
    const u32 *palette;     // 0x34 one 0x00RRGGBB per entry
} CustomMachineDesc;

// Gates who gets packed into a select screen's icon list. `default_available` is
// the engine's own answer for that CharacterKind on the screen being packed -
// its checklist unlock for a vanilla character, and 1 for an appended one, so a
// filter that only wants to narrow the roster can return it unchanged. Returns 1
// to give the character an icon.
typedef int (*CustomMachineAvailabilityFilter)(int character_kind, int default_available);

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

    // Claim the engine's own per-kind extension slots on the star class, which
    // Machine_Star_Init and Machine_Star_Think end by calling. The handler the
    // machine's clone_kind inherited still runs first, so this layers on rather
    // than replaces. One handler per slot; NULL clears it. Returns 1 if the kind
    // is a registered custom machine.
    int (*SetStarInitHandler)(int kind, CustomMachineStarHandler fn);
    int (*SetStarThinkHandler)(int kind, CustomMachineStarHandler fn);

    // The live joint for a depth-first index into the machine archive's own joint
    // tree - the same numbering `palette_joint` uses. NULL if the machine has no
    // model loaded or the index is past its tree.
    struct JOBJ *(*GetMachineJoint)(struct MachineData *md, int joint_index);

    // The descriptor's palette, or NULL if it asked for none. Points into the
    // boot-time archive, which is never freed.
    const u32 *(*GetPalette)(int kind, int *out_count);
} CustomMachinesAPI;

#endif
