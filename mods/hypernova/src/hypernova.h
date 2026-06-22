#ifndef HYPERNOVA_H
#define HYPERNOVA_H

#include "datatypes.h"
#include "hsd.h"
#include "rider.h"

// Visual size while active, eased in/out.
#define HYPERNOVA_SCALE_TARGET      2.0f
#define HYPERNOVA_SCALE_NEUTRAL     1.0f
#define HYPERNOVA_SCALE_ANIM_FRAMES 30      // ~0.5s ease @ 60fps

// Suction cone: RANGE = reach (world units); half-angle as its cosine (dot threshold).
// 0.8660254 = cos(30deg) -> a 60deg-wide cone.
#define HYPERNOVA_RANGE             175.0f
#define HYPERNOVA_HALF_ANGLE_COS    0.8660254f

// tan(half-angle), for the debug cone's base radius. Compile-time constant for the fixed
// half-angle; keep in sync with the cosine: tan(acos(0.8660254)) = tan(30deg).
#define HYPERNOVA_HALF_ANGLE_TAN    0.5773503f

// Committed pull: a swept target closes on the rider by max(SPEED*dist, MIN) units/frame.
// Items and yakumono share both constants so they pull at an identical rate.
#define HYPERNOVA_PULL_SPEED        0.030f
#define HYPERNOVA_PULL_MIN          2.0f

// Arc lift peak (world units): the pull aims above the rider by a parabolic hump of the
// target's horizontal distance, so it rises up and over instead of scraping the ground.
#define HYPERNOVA_ARC_HEIGHT        30.0f

// In-flight tumble, radians/frame (spin direction is hashed per-target).
#define HYPERNOVA_SPIN_RATE         0.08f

// Yakumono shrink-and-break: shrink by SHRINK/frame only within SHRINK_RADIUS; break once
// shrunk past BREAK_SCALE of original size OR within BREAK_RADIUS of the rider.
#define HYPERNOVA_YAKU_SHRINK_RADIUS 22.0f
#define HYPERNOVA_YAKU_BREAK_RADIUS  8.0f
#define HYPERNOVA_YAKU_SHRINK        0.70f
#define HYPERNOVA_YAKU_BREAK_SCALE   0.20f

// Frames a claim may live before it is force-released (collision re-armed) if it never breaks.
#define HYPERNOVA_YAKU_CLAIM_TTL    300

// Held to run the vacuum (B, not A: A is the boost/charge button in Air Ride).
#define HYPERNOVA_TRIGGER_BUTTON    PAD_BUTTON_B

// Vanilla inhale action-states (RiderData.state_idx). They do NOT chain on their own;
// DriveInhale owns the IDLE -> GULP -> LOOP gesture across them.
#define HYPERNOVA_INHALE_START      0x2f
#define HYPERNOVA_INHALE_LOOP       0x30
#define HYPERNOVA_INHALE_END        0x31

// Inhale LOOP self-terminate countdown (RiderData+0x93C): topped up to HOLD each frame to
// keep the suck alive. HOLD must be >= 2. Unreliable (aliases copy_wheel_result), so the
// suck is ended explicitly via Rider_EndInhale rather than by letting this lapse.
#define HYPERNOVA_INHALE_TIMER_OFF  0x93C
#define HYPERNOVA_INHALE_TIMER_HOLD 8

// Rainbow body overlay (ColAnim slot at RiderData+0x5c). DriveRainbow applies candy ColAnim 3
// to set up the slot, freezes its tick, pins priority, then drives the color each frame.
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

// Rainbow tuning.
#define HYPERNOVA_RAINBOW_ALPHA      100    // overlay strength (0..255)
#define HYPERNOVA_RAINBOW_PERIOD     120    // frames per full hue cycle

// Whirlwind hue offset from the bodies' (0..1 of the wheel); 0.5 = complementary.
#define HYPERNOVA_WHIRLWIND_HUE_OFFSET 0.5f

// Whirlwind rainbow strength (0..1): the hue is blended toward white before it is written, so
// 1.0 = full rainbow, 0.0 = white. Opacity is left at vanilla.
#define HYPERNOVA_WHIRLWIND_TINT       0.45f

// Inhale whirlwind. RecolorWhirlwinds finds live instances on the
// model-effect p_link bucket and rewrites each part's TEV color registers, alpha untouched.
#define HYPERNOVA_INHALE_EFFECT_ID   0x3a982  // group 24 / entry 2 (inhale suction cone)
#define HYPERNOVA_EFFECT_PLINK       16       // model-effect GObj p_link bucket
#define HYPERNOVA_EFFECT_GOBJ_KIND   25       // model-effect entity_class

// Yakumono break collider (Hypernova_BreakInstanceNative -> collideWithObject). FORCE_RADIUS
// is the force lever (force = radius * impactSpeed^2): huge for a one-hit break, under FLT_MAX.
// FORCE_DELTA's magnitude is arbitrary (normalized); only its direction (into the surface) matters.
#define HYPERNOVA_BREAK_FORCE_RADIUS 1.0e9f
#define HYPERNOVA_BREAK_FORCE_DELTA  100.0f

// Debug cone overlay: a translucent red cone mirroring the suction cone exactly. Drawn only
// while the "Debug Cone" menu toggle is on, otherwise a no-op.
#define HYPERNOVA_DEBUG_CONE_RGBA   RGBA(255, 0, 0, 64) // lightly opaque red
#define HYPERNOVA_DEBUG_CONE_SEGS   24                  // base-circle subdivisions (15deg steps)

// Render GObj carrying the cone's GX callback. class/p_link are arbitrary (never enumerated);
// the GX link is the world camera's 3D-scene link, so the cone is occluded by world geometry.
#define HYPERNOVA_DEBUG_GOBJ_CLASS  200
#define HYPERNOVA_DEBUG_GOBJ_PLINK  25
#define HYPERNOVA_DEBUG_GX_LINK     0
#define HYPERNOVA_DEBUG_GX_PRI      0

// Default duration when Activate(0) is called and the menu default is used.
#define HYPERNOVA_DURATION_NUM 3
extern const int hypernova_duration_table[HYPERNOVA_DURATION_NUM];

// Menu-backed settings
extern int hypernova_enabled;
extern int hypernova_duration_sel;  // index into hypernova_duration_table
extern int hypernova_suck_yaku;
extern int hypernova_selftest;      // hold D-Pad Up in CT
extern int hypernova_debug_cone;

// Lifecycle / callbacks
void Hypernova_OnBoot(void);
void Hypernova_OnFrameEnd(void);
void Hypernova_OnSceneChange(void);

// Public API implementation
int  Hypernova_Activate(int duration_frames);
void Hypernova_Deactivate(void);
int  Hypernova_IsActive(void);
int  Hypernova_FramesRemaining(void);

// Claim every in-cone item and breakable prop for this rider. Moves nothing; claimed targets
// are pulled in the process passes below, so they keep flying in after leaving the cone.
void Hypernova_VacuumPlayer(int player, RiderData *rd);

// Pull every claimed item one frame toward its owner (the vanilla pickup trigger collects it).
void Hypernova_VacuumProcessClaimedItems(void);

// Pull every claimed prop one frame toward its owner; shrink when close, break on arrival.
void Hypernova_VacuumProcessClaimed(void);

// Break every claimed prop now and drop all claims (call when Hypernova ends mid-run).
void Hypernova_VacuumFinishClaimed(void);

// Drop all claims without touching the targets (call on scene change / leaving City Trial).
void Hypernova_VacuumReset(void);

// Lazily install the cone-visualizer render GObj (no-op if the toggle is off or it exists).
void Hypernova_DebugConeEnsure(void);
// Forget the cached overlay GObj (the engine frees it on scene teardown).
void Hypernova_DebugConeReset(void);

#endif // HYPERNOVA_H
