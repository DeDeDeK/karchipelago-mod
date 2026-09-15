#ifndef CUSTOM_MACHINE_DESC_H
#define CUSTOM_MACHINE_DESC_H

#include "datatypes.h"

// The on-disc formats a drop-in machine ships in: its descriptor and its art side-car.

// Folder (relative to FST root) and extension scanned for drop-in machines.
#define CUSTOM_MACHINE_DROPIN_DIR "machines"
#define CUSTOM_MACHINE_DROPIN_EXT ".dat"

// A machine may drop a sound bank of the same basename beside its archive -
// machines/VcMine.dat and machines/VcMine.ssm. It is an ordinary HAL .ssm
// holding one record per MachineAudioParams sound slot, in that struct's order;
// a record with a sample rate of 0 is absent and that slot keeps the sound the
// descriptor's audio_kind uses.
#define CUSTOM_MACHINE_AUDIO_EXT ".ssm"

// It may drop a second side-car of the same basename holding its UI art -
// machines/VcStarAp.dat and machines/VcStarAp.art - one image per distinct UI bank
// geometry. A machine with no side-car shares the registry's placeholder frame.
#define CUSTOM_MACHINE_ART_EXT ".art"

// The art side-car exports one public named `customMachineArt`, a
// CustomMachineArt. Magic is big-endian ASCII "CMAR".
#define CUSTOM_MACHINE_ART_SYMBOL  "customMachineArt"
#define CUSTOM_MACHINE_ART_MAGIC   0x434D4152u
#define CUSTOM_MACHINE_ART_VERSION 1

struct _HSD_ImageDesc;

// One image, claimed by the geometry of the bank frame it stands in for. The
// registry matches on all three, so a side-car built against a different game
// version fails to match rather than writing a wrong-sized image.
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
#define CUSTOM_MACHINE_DESC_VERSION 13

// The patch-stat scale pairs a class keeps one row per machine for, which a descriptor's
// stat_rows lists in its class's order. Each is {low, high}: `low` is weighed by a stat
// below zero patches, `high` by one above.
//
// A star's Top Speed pairs multiply the two top speeds; its Weight and Glide pairs, times
// the stat ratio, are added to vcHandlingAttr.air_impulse and air_recover_len.
typedef enum CustomMachineStatRow
{
    CUSTOM_MACHINE_STAT_ROW_TOP_SPEED_GROUND,
    CUSTOM_MACHINE_STAT_ROW_TOP_SPEED_AIR,
    CUSTOM_MACHINE_STAT_ROW_WEIGHT_AIR_IMPULSE,
    CUSTOM_MACHINE_STAT_ROW_WEIGHT_AIR_RECOVER,
    CUSTOM_MACHINE_STAT_ROW_GLIDE_AIR_IMPULSE,
    CUSTOM_MACHINE_STAT_ROW_GLIDE_AIR_RECOVER,
    CUSTOM_MACHINE_STAT_ROW_NUM,
} CustomMachineStatRow;

// A bike's Top Speed pairs multiply the two top speeds, and its Turn pairs multiply eight
// fields of the bike class's shared attribute block, named by their offset in it.
typedef enum CustomMachineBikeStatRow
{
    CUSTOM_MACHINE_BIKE_STAT_ROW_TOP_SPEED_GROUND,
    CUSTOM_MACHINE_BIKE_STAT_ROW_TOP_SPEED_AIR,
    CUSTOM_MACHINE_BIKE_STAT_ROW_TURN_074,
    CUSTOM_MACHINE_BIKE_STAT_ROW_TURN_078,
    CUSTOM_MACHINE_BIKE_STAT_ROW_TURN_084,
    CUSTOM_MACHINE_BIKE_STAT_ROW_TURN_088,
    CUSTOM_MACHINE_BIKE_STAT_ROW_TURN_094,
    CUSTOM_MACHINE_BIKE_STAT_ROW_TURN_098,
    CUSTOM_MACHINE_BIKE_STAT_ROW_TURN_09C,
    CUSTOM_MACHINE_BIKE_STAT_ROW_TURN_0A0,
    CUSTOM_MACHINE_BIKE_STAT_ROW_NUM,
} CustomMachineBikeStatRow;

// A vehicle particle bank generator the machine brings. The bank's own generators are
// ids 0 up to CUSTOM_MACHINE_GENERATOR_BASE, and the machine's animation bank names the
// k-th one it brings as CUSTOM_MACHINE_GENERATOR_BASE + k; the registry moves those ids to
// wherever the machine's generators are installed. `desc` is the generator descriptor as
// a bank holds it on disc: header, then the program through its terminator.
#define CUSTOM_MACHINE_GENERATOR_BASE 52
#define CUSTOM_MACHINE_GENERATOR_MAX  4
#define CUSTOM_MACHINE_GENERATOR_SIZE 256

typedef struct CustomMachineGenerator
{
    u16 size;       // 0x00 bytes through the terminator
    u16 reserved;   // 0x02
    const u8 *desc; // 0x04
} CustomMachineGenerator;

typedef enum CustomMachineCpuPitch
{
    CUSTOM_MACHINE_CPU_PITCH_NONE,
    CUSTOM_MACHINE_CPU_PITCH_CLIMB, // stick held up, as CPUs fly the Hydra
    CUSTOM_MACHINE_CPU_PITCH_DIVE,  // stick down once moving, as CPUs fly Winged and Jet Star
} CustomMachineCpuPitch;

// How CPU riders treat the machine: what the engine keeps per kind in DOL tables and
// a few switches on the kind, which a custom machine authors instead.
typedef struct CustomMachineCpu
{
    int swap_score;             // 0x00 desirability as a field machine; no CPU boards one at 0
    u8 flags;                   // 0x04 CpuMachineCapFlag
    u8 stick_pitch;             // 0x05 CustomMachineCpuPitch
    u8 has_charge_hold_gate;    // 0x06
    u8 has_release_level;       // 0x07
    float charge_release;       // 0x08 a charge-holding CPU holds while the gauge is <= this
    float charge_hold_gate[2];  // 0x0c Machine_CPUGetChargeHoldGate's two outputs, in order
    float release_level;        // 0x14 Machine_CPUGetChargeReleaseOverride's output
    float align_cos_near;       // 0x18 heading dot above which steering needs no correction
    float align_cos_far;        // 0x1c heading dot below which it needs the larger one
    float turn_tolerance;       // 0x20 radians
    float stuck_angle;          // 0x24 radians past which the CPU counts itself stuck
    float air_glider_pitch;     // 0x28 radians
    float air_glider_min_len;   // 0x2c
    float high_jump_pitch;      // 0x30 radians
    float high_jump_min_len;    // 0x34
} CustomMachineCpu;             // 0x38

typedef struct CustomMachineDesc
{
    u32 magic;                                // 0x00 CUSTOM_MACHINE_MAGIC
    u16 version;                              // 0x04 CUSTOM_MACHINE_DESC_VERSION
    u16 reserved;                             // 0x06
    const char *name;                         // 0x08 display name, the handle consumers bind by
    const char *symbol;                       // 0x0c the vcData public in this same archive
    int is_bike;                              // 0x10 machine class: 0 = star, 1 = bike
    int wants_character;                      // 0x14 also take a CharacterKind and a select-grid cell
    int rider_kind;                           // 0x18 RiderKind for the CharacterDesc row
    int audio_kind;                           // 0x1c same-class MachineKind voicing what its .ssm lacks
    float spawn_weight;                       // 0x20 City Trial spawn weight, 0 never spawns loose
    const char *description;                  // 0x24 select-screen blurb; '\n' breaks the line
    int generator_count;                      // 0x28 up to CUSTOM_MACHINE_GENERATOR_MAX
    const CustomMachineGenerator *generators; // 0x2c
    const CustomMachineCpu *cpu;              // 0x30
    const char *cine_file;                    // 0x34 FST path of the assembly cutscene archive
    const char *cine_symbol;                  // 0x38 its public, shaped like VsHydra.dat's vsDataHydra
    int cine_machine_index;                   // 0x3c vanilla legendary it runs under: 0 Dragoon, 1 Hydra
    const float *stat_rows;                   // 0x40 {low, high} per row of its class's stat row enum
    float blip_height;                        // 0x44 field blip lift, before the engine's 0.175 scale
    float radar_drop;                         // 0x48 stat radar model drop, in the same units
} CustomMachineDesc;                          // 0x4c

#endif
