#ifndef HYPERNOVA_H
#define HYPERNOVA_H

#include "datatypes.h"
#include "hsd.h"
#include "rider.h"

#define HYPERNOVA_SCALE_TARGET      2.0f
#define HYPERNOVA_SCALE_NEUTRAL     1.0f
#define HYPERNOVA_SCALE_ANIM_FRAMES 30      // ~0.5s ease @ 60fps

// Suction cone: reach in world units, half-angle as its cosine (0.8660254 = cos(30deg)).
#define HYPERNOVA_RANGE             175.0f
#define HYPERNOVA_HALF_ANGLE_COS    0.8660254f

// tan(30deg), for the debug cone's base radius; keep in sync with the cosine.
#define HYPERNOVA_HALF_ANGLE_TAN    0.5773503f

// Pull step shared by items, yakumono and machines: max(SPEED*dist, MIN) units/frame.
#define HYPERNOVA_PULL_SPEED        0.030f
#define HYPERNOVA_PULL_MIN          2.0f

// Peak lift of the pull's arc above the rider (world units).
#define HYPERNOVA_ARC_HEIGHT        30.0f

// In-flight tumble, radians/frame.
#define HYPERNOVA_SPIN_RATE         0.08f

// Yakumono shrink by SHRINK/frame only within SHRINK_RADIUS; break once shrunk past
// BREAK_SCALE of the original size or within BREAK_RADIUS of the rider.
#define HYPERNOVA_YAKU_SHRINK_RADIUS 22.0f
#define HYPERNOVA_YAKU_BREAK_RADIUS  8.0f
#define HYPERNOVA_YAKU_SHRINK        0.70f
#define HYPERNOVA_YAKU_BREAK_SCALE   0.20f

// Frames a claim may live before it is force-released (collision re-armed).
#define HYPERNOVA_YAKU_CLAIM_TTL    300

// Machines are KO'd on arrival; wider break radius than yakumono - machines are large.
#define HYPERNOVA_MACHINE_BREAK_RADIUS 45.0f

// MachineData offsets. accel/velocity are integrated into pos each frame (zeroed so the pos
// override sticks); the KO-gate bit enables the BreakDown explosion + GObj_Destroy in Machine_OnKO.
#define HYPERNOVA_MACHINE_ACCEL_OFF    0x318
#define HYPERNOVA_MACHINE_KO_GATE_OFF  0x78
#define HYPERNOVA_MACHINE_KO_GATE_BIT  0x40

// B, not A: A is the boost/charge button in Air Ride.
#define HYPERNOVA_TRIGGER_BUTTON    PAD_BUTTON_B

// Vanilla inhale action-states (RiderData.state_idx); they do NOT chain on their own.
#define HYPERNOVA_INHALE_START      0x2f
#define HYPERNOVA_INHALE_LOOP       0x30
#define HYPERNOVA_INHALE_END        0x31

// Value RiderData.inhale_timer is topped up to each frame to keep the suck alive; must be >= 2.
// It aliases copy_wheel_result, so the suck is ended explicitly via Rider_EndInhale rather than
// by letting this lapse.
#define HYPERNOVA_INHALE_TIMER_HOLD 8

// Rainbow body overlay: candy ColAnim 3 driven through the ColAnim slot at RiderData+0x5c.
#define HYPERNOVA_OVERLAY_COLANIM    3
#define HYPERNOVA_COLANIM_BODY_OFF   0x5c
#define HYPERNOVA_COLANIM_DATA_OFF   0x08   // anim-data pointer; null to freeze the tick
#define HYPERNOVA_COLANIM_INDEX_W    10     // word index of the current anim index
#define HYPERNOVA_COLANIM_COL2C_OFF  0x2c   // packed RGBA the selector reads
#define HYPERNOVA_COLANIM_COLOR_OFF  0x30   // RGBA floats (0..255)
#define HYPERNOVA_COLANIM_PRI_OFF    0xa9   // priority byte; pin to PRI_MAX to win the selector
#define HYPERNOVA_COLANIM_PRI_MAX    0xff
#define HYPERNOVA_COLANIM_STFLAG_OFF 0xaa   // state flags (bit 0x80 = color-override active)
#define HYPERNOVA_COLANIM_RENDER_OFF 0x224  // packed RGBA bytes the renderer reads
#define HYPERNOVA_COLANIM_FLAGA_OFF  0x234  // ratio enable (0xff = off)
#define HYPERNOVA_COLANIM_FLAGB_OFF  0x235  // draw flags (bit 0x80 = color override)

#define HYPERNOVA_RAINBOW_ALPHA      100    // overlay strength (0..255)
#define HYPERNOVA_RAINBOW_PERIOD     120    // frames per full hue cycle

// Whirlwind hue offset from the bodies' (0..1 of the wheel); 0.5 = complementary.
#define HYPERNOVA_WHIRLWIND_HUE_OFFSET 0.5f

// Whirlwind rainbow strength (0..1): 1.0 = full rainbow, 0.0 = white. Opacity stays vanilla.
#define HYPERNOVA_WHIRLWIND_TINT       0.45f

#define HYPERNOVA_INHALE_EFFECT_ID   0x3a982  // inhale suction whirlwind Effect kind
#define HYPERNOVA_EFFECT_PLINK       16       // model-effect GObj p_link bucket
#define HYPERNOVA_EFFECT_GOBJ_KIND   25       // model-effect entity_class

// Fabricated break collider: force = radius * impactSpeed^2, so FORCE_RADIUS is huge (but under
// FLT_MAX) for a one-hit break. Only FORCE_DELTA's direction matters (it points into the surface).
#define HYPERNOVA_BREAK_FORCE_RADIUS 1.0e9f
#define HYPERNOVA_BREAK_FORCE_DELTA  100.0f

#define HYPERNOVA_DEBUG_CONE_RGBA   RGBA(255, 0, 0, 64) // lightly opaque red
#define HYPERNOVA_DEBUG_CONE_SEGS   24                  // base-circle subdivisions (15deg steps)

// GX link 0 is the world camera's 3D-scene link, so the cone is occluded by world geometry.
// class/p_link are arbitrary (the GObj is never enumerated).
#define HYPERNOVA_DEBUG_GOBJ_CLASS  200
#define HYPERNOVA_DEBUG_GOBJ_PLINK  25
#define HYPERNOVA_DEBUG_GX_LINK     0
#define HYPERNOVA_DEBUG_GX_PRI      0

#define HYPERNOVA_DURATION_NUM 3
extern const int hypernova_duration_table[HYPERNOVA_DURATION_NUM];

// Menu-backed settings
extern int hypernova_enabled;
extern int hypernova_duration_sel;  // index into hypernova_duration_table
extern int hypernova_suck_yaku;
extern int hypernova_suck_machines; // also vacuum unridden machines (KO'd on arrival)
extern int hypernova_selftest;      // hold D-Pad Up in CT
extern int hypernova_debug_cone;

void Hypernova_OnBoot(void);
void Hypernova_OnFrameEnd(void);
void Hypernova_OnSceneChange(void);

int  Hypernova_Activate(int duration_frames);                   // all human players
int  Hypernova_ActivatePlayer(int player, int duration_frames); // one player slot
void Hypernova_Deactivate(void);
int  Hypernova_IsActive(void);
int  Hypernova_FramesRemaining(void);

// Claim every in-cone item and breakable prop for this rider; moves nothing.
void Hypernova_VacuumPlayer(int player, RiderData *rd);

// Pull every claimed item one frame toward its owner (the vanilla pickup trigger collects it).
void Hypernova_VacuumProcessClaimedItems(void);

// Pull every claimed prop one frame toward its owner; shrink when close, break on arrival.
void Hypernova_VacuumProcessClaimed(void);

// Pull every claimed unridden machine one frame toward its owner; KO it on arrival.
void Hypernova_VacuumProcessClaimedMachines(void);

// Break one player's claimed props and drop that player's item claims, leaving other players'
// claims in flight.
void Hypernova_VacuumFinishClaimedPlayer(int player);

// Drop all claims without touching the targets (scene change / leaving City Trial).
void Hypernova_VacuumReset(void);

// Lazily install the cone-visualizer render GObj (no-op if the toggle is off or it exists).
void Hypernova_DebugConeEnsure(void);
// Forget the cached overlay GObj (the engine frees it on scene teardown).
void Hypernova_DebugConeReset(void);

#endif // HYPERNOVA_H
