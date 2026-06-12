# Gravity Change Event — Research & Findings

> **Status: IMPLEMENTED & ACTIVE.** Verified 2026-06-08 against
> `mods/custom_events/src/event_gravity_change.c` / `.h` and
> `mods/custom_events/src/custom_events.c`. The event is registered in the
> custom-events framework as `CUSTOM_EVKIND_GRAVITY_CHANGE` (17) with weight 20,
> wired into both `custom_params[]` and `custom_functions[]`, and reachable
> through the extended event roll (`CustomEvents_ExtendedRoll`, which replaces
> the `Gm_Roll` call at `0x800ee098`). It is *not* dead/unregistered code.

## Concept

A custom City Trial event that modifies stage gravity for the duration of the event. Fully functional — modifying the gravity vector at runtime affects all machine and object physics immediately.

## Entry Points

| Function | File | Role |
|----------|------|------|
| `GravityChange_Start`  | `event_gravity_change.c` | Save original `gravity_force`, multiply by `GRAVITY_MULTIPLIER`. Called from `CustomEvent_State1Wrapper`. |
| `GravityChange_Active` | `event_gravity_change.c` | Re-apply scaled gravity each frame. Called from `CustomEvent_State2Wrapper`. |
| `GravityChange_End2`   | `event_gravity_change.c` | Restore original `gravity_force`. Called from `CustomEvent_State3Wrapper`. |
| `GetStageGravity` (static) | `event_gravity_change.c` | Resolves `*stc_grobj → gr_data → stage_node → &gravity_force`, with NULL guards. |

There is no `check` callback registered for this event, so it is always
eligible when its weight is non-zero. There is no `end` (per-frame state-3)
callback either — only `start`, `active`, and `end2`.

## Gravity System

### Data Location

The gravity force vector is stored in `GrData.stage_node->gravity_force` (Vec3 at offset 0x10 within stage_node):

```c
// Access chain: stc_grobj_ptr -> gr_data (+0x8) -> stage_node (+0x4) -> gravity_force (+0x10)
GrObj *grobj = *stc_grobj;
Vec3 *grav = &grobj->gr_data->stage_node->gravity_force;
```

### Confirmed Values

City Trial default gravity: `(0.0, -1.0, 0.0)` — pure downward gravity, unit magnitude.

### stage_node Layout (Relevant Fields)

```
+0x00  int    x0
+0x04  float  machine_accel
+0x08  float  scale
+0x0C  float  gravity_unk          (unknown gravity-related float, purpose TBD)
+0x10  Vec3   gravity_force        (X, Y, Z gravity vector)
+0x1C  int    fog_flags
```

### How Gravity Is Applied

The exact code path from `gravity_force` to machine physics was not fully traced. What we know:

- `Machine_PhysicsThink` (0x801c6368) accumulates several Vec3 components into position each frame: acceleration (0x318) → velocity (0x324) → position (0x3e8), plus additional vectors at 0x330, 0x33c, 0x348, 0x3a8, 0x3c8. One of these carries the gravity contribution.
- The machine physics chain (`MachinePhys_Charge` → `zz_801d85c0_` → sub-functions) reads from `DAT_805dd848` (vehicle common data pointer, NOT stage_node) for many physics parameters.
- `DAT_805dd848` is loaded from `VcCommon.dat` by `vcLoadCommon` (0x801c6d0c). It is massively referenced (~140 xrefs) across the machine code for acceleration, friction, damage, knockback, etc.
- The gravity_force vector is read separately from the stage_node by some part of the physics pipeline, but the exact reader function was not identified. It is likely read each frame since modifying it at runtime has immediate effect.
- `gravity_unk` (stage_node+0xC) is adjacent to gravity_force — purpose unknown, may be a gravity scaling factor or damping parameter.

### Gravity Zones (Separate System)

The stage also supports spatial gravity zones, loaded via `loadGravityLocations?` (0x800d0de4) and counted by `grGetGravityposNum` (0x800d0dcc). These are position/direction/extent tuples stored at `stc_grobj_ptr+0x13C` (stride 0x24 per zone). They are NOT the global gravity — they are localized gravity wells/overrides in specific areas of the stage. Only used by `debug_Race3D_loadLocations` for debug visualization.

### Actor/Enemy Gravity

Enemies have a separate gravity parameter at `EnemyData+0x3A4` (default 0.02, sourced from per-type actor data at `*actor_data+0x3C`). This is independent of the stage gravity_force and is applied in the enemy physics proc (`vel += accel` each frame). The stage gravity_force likely does NOT affect enemies directly — they use their own gravity scalar with GroundSnap for Y positioning.

### Item Gravity

Items have a `fall_dir` (Vec3 at ItemData+0x1C8) used for ground raycasting direction. This may or may not be derived from stage gravity_force. Items also have `accel` (Vec3 at 0xB8) and `vel` (Vec3 at 0xC4) for physics simulation.

## Implementation

### Current State

- `event_gravity_change.c/h` — fully implemented, registered as `CUSTOM_EVKIND_GRAVITY_CHANGE` (17)
- Modifies `stage_node->gravity_force` directly at runtime
- Saves original values at event start, re-applies scaled values each active frame, restores at event end
- Currently uses `GRAVITY_MULTIPLIER 0.2f` (20% gravity — very low)

### Registration (`custom_params[CUSTOM_EVKIND_GRAVITY_CHANGE - EVKIND_NUM]` in `custom_events.c`)

| Field | Value |
|-------|-------|
| `duration`   | `900` frames (~15 s) |
| `is_siren`   | `1` (siren SFX + music fade + sky transition) |
| `sky_preset` | `8` (Pink Sky) |
| `bgm_file`   | `0x31` (Meteor BGM) |
| `weight`     | `20` |
| `label`      | `"Gravity Change"` |
| `hud_text`   | `"Gravity is changing!"` |

### Observed Behavior

- **Ground physics**: Works well. Machines feel floaty, take longer to decelerate, drift more.
- **Airborne physics**: Machines behave "very weirdly" in the air at 0.2x. The physics code likely has additional gravity-dependent calculations for air control, landing detection, and trajectory that expect normal gravity magnitude. Air time is greatly extended.
- **Higher multiplier** (>1.0, e.g., 1.5x–2.0x) would likely be more stable since machines spend less time airborne.

### Possible Enhancements

- **Randomize multiplier**: Pick between low gravity (0.4–0.6) and high gravity (1.5–2.0) each trigger
- **Tilt gravity vector**: Slightly rotate the gravity direction (e.g., add a small X or Z component) for diagonal gravity
- **Gradual transition**: Lerp gravity over several frames at start/end instead of instant switch
- **Investigate gravity_unk**: The float at stage_node+0xC may control additional gravity behavior — worth testing
- **Tune multiplier**: 0.2x is extreme; 0.5x might give the low-gravity feel without the air weirdness

## Symbols

| Symbol | Address | Size | Notes |
|--------|---------|------|-------|
| `grGetGravityposNum` | 0x800d0dcc | 0x18 | Gravity zone count (NOT global gravity) |
| `loadGravityLocations?` | 0x800d0de4 | 0xD4 | Loads gravity zone spatial data |
| `Machine_PhysicsThink` | 0x801c6368 | 0x240 | Main machine physics accumulator |
| `MachinePhys_Charge` | 0x801ef364 | 0xF0 | Per-frame machine physics chain |
| `zz_801d85c0_` | 0x801d85c0 | ~0x240 | Drag/friction, first call in physics chain |
| `zz_801d8108_` | 0x801d8108 | ~0x90 | Velocity damping on MachineData+0x33c Vec3 |
| `vcLoadCommon` | 0x801c6d0c | ~0x60 | Loads VcCommon.dat → DAT_805dd848 |
| `DAT_805dd848` | 0x805dd848 | ptr | Vehicle common data (r13+0x768), ~140 xrefs |
| `grGetStageScale` | 0x800d3058 | 0x24 | Reads stage_node->scale (adjacent field) |

All symbols and addresses in the table above were re-verified against
`externals/hoshi/GKYE01.map` on 2026-06-08. Note `zz_801d85c0_` is `0x238`
bytes and `zz_801d8108_` is `0x88` bytes in the map (the table's `~0x240` /
`~0x90` are approximate). `DAT_805dd848` = r13 (`0x805dd0e0`) + `0x768`.

## See Also

- `custom-events.md` — the framework that registers and dispatches this event (state-machine wrappers, weighted selection, SIS text).
- `city-trial-event-system.md` — vanilla event system this builds on.
- `sky-lighting-system.md` — `Sky_TransitionGlobal` / `Sky_RestoreGlobal` used for the Pink Sky siren transition.
- `event-scale-change.md` — sibling custom event with an analogous start/active/end2 structure (modifies `stage_node->scale`).
