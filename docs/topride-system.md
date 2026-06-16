# Top Ride System

## Overview

Top Ride is a 2D mode with its own engine, completely separate from the 3D mode's Rider/Machine/Player system. It does **not** use `Player_Create`, `Rider_Create`, `Machine_Create`, `stc_playerdata`, `RiderData`, or `MachineData`. The scene loads through **minor 19** (not minor 18), so `On3DLoadEnd` does not fire for Top Ride.

## Scene Flow

```
Main Menu (minor 2)
  -> MJRKIND_TOP (major 5)
    -> minor 24: Top Ride settings
    -> minor 7: Course select (TopRide_CourseSelectInit)
    -> minor 9: Player select (TopRide_LobbyInit)
    -> minor 19: Gameplay (cb_Load = 0x80008df8)
```

### Minor 19 Callbacks (MinorSceneDesc)

| Callback | Address | Purpose |
|----------|---------|---------|
| cb_Load | 0x80008df8 | Scene load: RNG seed, speed init, calls `TopRide_GameInit` |
| cb_Exit | 0x80008fe4 | Scene exit |
| cb_ThinkPreGObjProc | 0x80009004 | Empty (returns immediately) |
| cb_ThinkPostGObjProc | 0x80009008 | `TopRide_SceneInit` — checks for exit conditions |
| cb_ThinkPreRender | 0x80009070 | Pre-render |
| cb_ThinkPostRender | 0x80009074 | `TopRide_PostRenderCallback` |

### Post-render second pass wipes screen overlays

`TopRide_PostRenderCallback` (cb_ThinkPostRender, 0x80009074) calls `TopRide_CustomRenderer`, which kicks off an **entirely new `HSD_StartRender` pass** for the 2D engine. That pass overwrites the EFB *after* the standard frame render — so anything a mod drew through a hoshi screen-space canvas (HUD text, notifications, the textbox) is wiped every frame in Top Ride.

To keep a screen overlay visible in TR, re-issue its render *after* the post-render pass returns. The textbox mod hooks `0x80009084` (the instruction right after the `bl TopRide_CustomRenderer`) and, for each `TextCanvas` in the `stc_textcanvas_first` list, re-calls `CObjThink_Common(canvas->cam_gobj)` — redrawing on top of the second pass. Any mod with a Top Ride HUD/text overlay needs the same re-render.

## Object Hierarchy

```
GameSession (TopRide_GameSessionInit, per-scene)
  └── KirbyMgr singleton (TopRide_FielderInit, 0x802dafb4)
        ├── Kirby[0..3] (TopRide_KirbyInit, per-player)
        │     ├── StateHandler (+0x7C ptr, vtable 0x804d6f5c)
        │     ├── ChargeComponent (+0x80, initialized by TopRide_KirbyChargeInit)
        │     └── Absorber (+0xD00, vtable 0x804bdc70, RTTI "ItemMgr::Absorber")
        ├── TopRideItem_Mgr (+0x3DE8)
        ├── EnemyMgr (+0x3F58)
        ├── MineMgr (+0x3F74)
        ├── EmberMgr (+0x3F90)
        ├── SmokeMgr (+0x3FA8)
        ├── MissileMgr (+0x3FC4)
        ├── SoundHandles (+0x4020)
        └── round_state (+0x4028, u8: 0=pre-init, 1=countdown, 2=race active)
```

## Globals

| Address | Name | Description |
|---------|------|-------------|
| 0x805ddb44 | KirbyMgr | Fielder singleton. NULL when not in Top Ride gameplay. |
| 0x805ddb48 | ChickMgr | Sub-object of KirbyMgr |
| 0x805ddb4c | ItemBall | Sub-object of KirbyMgr (item-ball manager) |
| 0x805ddb50 | KurakkoMgr | Sub-object of KirbyMgr |
| 0x805ddb54 | MissileMgr | Sub-object of KirbyMgr |
| 0x805ddb58 | SmokeMgr | Sub-object of KirbyMgr |
| 0x805ddb5c | EmberMgr | Sub-object of KirbyMgr (clear-checker reads field +0x18) |
| 0x805ddb60 | MineMgr | Sub-object of KirbyMgr |
| 0x805ddb64 | GrenadeMgr | Sub-object of KirbyMgr (clear-checker reads field +0x1c) |
| 0x805ddb68 | EnemyMgr | Sub-object of KirbyMgr |
| 0x805ddb84 | GameSession | Session-level singleton (contains KirbyMgr) |
| 0x805ddb38 | CpuObstacleMgr | Created in `TopRide_GameSessionInit` |
| 0x805ddb8c | SoundHandles | Sub-object of KirbyMgr |
| 0x805ddbec | GameSession sub | Scene-level sub-object |

(Manager classes are identified by RTTI `dynamic_cast` typeinfo and the construction
stores in `TopRide_FielderInit` / `TopRide_GameSessionInit`. The hierarchy diagram
above lists KirbyMgr-struct member offsets, a separate mapping.)

## Round State

The TR round phase is tracked by a single u8 at **`KirbyMgr+0x4028`**. Values progress 0→1→2 over the lifetime of a match:

| Value | Phase | Effects |
|-------|-------|---------|
| 0 | Pre-init (scene loading) | Physics, item spawning, and `TopRideItem_Update` are all skipped |
| 1 | Countdown | Physics runs (`+0x4028 != 0` gate at 0x802db850); item spawning + `TopRideItem_Update` still skipped |
| 2 | Race active | Everything runs |

### Read sites (gates)

| Address | Function | Gate | Effect |
|---------|----------|------|--------|
| 0x802db850 | `TopRide_KirbyMgrUpdate` | `+0x4028 != 0` | Skip per-Kirby physics + tracking block (incl. `TopRide_KirbyPhysUpdate`) |
| 0x802db8b0 | `TopRide_KirbyMgrUpdate` | `+0x4028 == 2` | Run per-Kirby weighted item-spawn picker |
| 0x802dc570 | `TopRide_KirbyMgrUpdate` | `+0x4028 == 2` | Call `TopRideItem_Update` (item lifetime/render tick) |
| 0x8029c714 / 0x8029c7ec | `TopRide_FielderUpdate` | `+0x4028 == 2` | Gameplay branch |

### Write sites (transitions)

| Address | Function | Value | Trigger |
|---------|----------|-------|---------|
| 0x8029ec04 | `zz_8029e334_` | 1 | Countdown begins |
| 0x8029efa4 | `zz_8029eda4_` | 2 | Race starts (countdown ends) |

### Implication for mod code

When spawning a TR item from outside the per-frame engine flow (AP item give, traplink, etc.), the spawn must wait for `round_state == 2`. Spawning during countdown (`round_state == 1`) puts the item on the list but `TopRideItem_Update` doesn't tick it, and the item gets culled at race-start. `GateTopRideItems_GiveItem` returns 0 in that window so `APItems_PerFrame` retries the item next frame.

### Secondary state bytes

- `GameSession+0x38` (where `GameSession = KirbyMgr->game_config`): sub-mode flag, not the master state. Read in `TopRideItem_SpawnTimed` to pick spawn variant.
- `GameSession+0x40`: static config field set from `ConfigDesc` in `zz_802c50ac_` at scene load. `+0x40 == 3` blocks all item spawning (likely "no items" stadium variant). Stable across the match.

## Player Kind (Human / CPU / Empty)

The per-slot human/CPU/empty discriminator is stored at **`GameData[slot*9 + 0xD20]`** (a 9-byte-stride config block, 4 entries for slots 0–3). Values:

| Value | Meaning |
|-------|---------|
| 0 | `TR_PKIND_HMN` — human, controller bound |
| 1 | `TR_PKIND_CPU` — CPU |
| 2 | `TR_PKIND_NONE` — empty slot |

Vanilla provides accessors:

| Address | Name | Signature |
|---------|------|-----------|
| 0x8000bd6c | `TopRide_GetPlayerKind` | `TopRidePlayerKind(int slot)` |
| 0x8000bda8 | `TopRide_SetPlayerKind` | `void(int slot, TopRidePlayerKind kind)` |

`TopRide_PreGameThink` (0x8002c06c) populates these bytes from menu input each round. `TopRide_FielderInit` (0x802dafb4) reads them at construction time to decide which kirbys to spawn.

**Filtering humans in mod code**: iterate `kirby_mgr->kirbys[i]` and check `TopRide_GetPlayerKind(kirby->player_slot) == TR_PKIND_HMN`. **Do not** use `kirby->start_position` (Kirby+0x0E) — that's the per-round shuffled grid position, not a CPU flag.

## Kirby Object Layout

Each player is represented by a Kirby object (>0x1400 bytes — an `Absorber`
sub-object lives at +0xD00). Vtable at `0x804d2304`, RTTI name "Kirby". Created
by `TopRide_KirbyInit` (0x802d4d64, size 0x788). The struct `TopRideKirby` in
`topride.h` maps the head plus the inline charge component (the 0x00..~0x530
region); the rest of the object is unmapped.

`session_data` (+0x04) points at the inline charge component (kirby+0x80) — this
is how `TopRide_KirbyModelThink` reaches `model_jobj` / `model_scale` via
`session_data[+0x460]` / `session_data[+0x4A4]` (absolute kirby+0x4E0 / +0x524).

| Offset | Type | Field | Description |
|--------|------|-------|-------------|
| 0x00 | ptr | vtable | `0x804d2304` |
| 0x04 | ptr | session_data | Aliases the inline charge component (= kirby+0x80) |
| 0x0C | u8 | player_slot | Controller slot (0–3). Pass to `TopRide_GetPlayerKind` to discriminate human vs CPU vs empty. |
| 0x0D | u8 | char_type | Character kind |
| 0x0E | u8 | start_position | Fisher-Yates shuffled grid position (0–3), reset each round in `TopRide_FielderInit`. **Not** a CPU level — every kirby (human or CPU) is assigned a starting position 0–3. |
| 0x0F | u8 | place | Current race placement / finish rank, written each frame by the ranking pass in `TopRide_KirbyMgrUpdate`. 0 while still racing; gated `== 0` as "not yet finished". |
| 0x10 | u8 | is_active | Final standings byte set by the same ranking pass on race start; stays 0 in Time Attack and Free Run even while the human is playing. **Do not gate solo-mode mod code on this bit** — use `round_state == 2` + `TopRide_GetPlayerKind() == TR_PKIND_HMN` instead (see `gate_topride_items.c` / `energylink.c`). |
| 0x14 | int | lap_progress | Accumulates the per-frame CheckLine cross result (init -1); going positive completes a lap/segment. |
| 0x18 | u8 | lap_pending | Set when a checkpoint is crossed backward; gates the lap-completion branch. |
| 0x1C | u32 | finish_time | Total frame counter; latched to the master race timer (`KirbyMgr+0x402C`) when `finished` is set. |
| 0x20 | u32 | prev_lap_frames | Snapshot of `cur_lap_frames` at lap completion. |
| 0x24 | u32 | cur_lap_frames | Current-lap frame counter; reset to 0 on lap completion, incremented every frame. |
| 0x2C | float | mass | Per-character mass / scale base (read constantly in physics; scales knockback). |
| 0x30 | float | gravity | Gravity / vertical-accel base. |
| 0x34 | float | accel_param | Frame-scaled acceleration parameter. |
| 0x38 | float | decel_param | Frame-scaled deceleration parameter. |
| 0x3E | u8 | finished | Set to 1 when the kirby crosses the finish line; gates the per-frame counter increments in `TopRide_KirbyPhysUpdate`. |
| 0x40 | u8 | direction_sign | Movement-direction sign flag, refreshed each frame from the run mode. |
| 0x42 | u16 | screen_w | Viewport width (init 320). |
| 0x44 | u16 | screen_h | Viewport height (init 240). |
| 0x48 | ptr | input_reader | Controller / input source object (vt+0x14 polls the stick). |
| 0x4C | Vec3 | position | Spawn / default pos — **not** tracked per frame. Use `charge.position` (0x88) for the live in-world position. |
| 0x58 | Vec3 | target_pos | Initial camera target / lookat. |
| 0x64 | u8[0x18] | history | 10-entry circular history ring (head index + paired byte values), pushed by `TopRide_KirbyHistoryPush` (0x80311f88) and queried by `TopRide_KirbyHistoryQuery` (0x80312000) for an anti-jitter snap. Constructed by `TopRide_KirbyHistoryInit` (0x80311f2c). |
| 0x7C | ptr | state_handler | Input/state handler (charge state machine) |
| 0x80 | — | **charge_component** | Start of charge component (inline sub-object) |

## Charge Component Layout (at Kirby+0x80)

Initialized by `TopRide_KirbyChargeInit` (0x802d1fe8, size 0x1918).

All offsets listed both from component base and from Kirby base.

| Comp | Kirby | Type | Field | Description |
|------|-------|------|-------|-------------|
| 0x00 | 0x80 | ptr | kirby_ptr | Pointer back to Kirby base |
| 0x08 | 0x88 | Vec3 | position | Current 2D position |
| 0x14 | 0x94 | Vec3 | facing_dir | Facing direction unit vector |
| 0x20 | 0xA0 | Vec3 | velocity | Current velocity vector |
| **0x2C** | **0xAC** | **u8** | **is_charging** | **1 = A button held (charging), 0 = idle** |
| **0x2D** | **0xAD** | **u8** | **charge_ready** | **1 = charge fully depleted to 0.0, can accumulate again** |
| 0x30 | 0xB0 | float | speed_factor | Per-frame speed scaling |
| **0x34** | **0xB4** | **float** | **charge_value** | **Current charge level. 0.0 to ~1.0. The Top Ride equivalent of MachineData.charge_value (0x78C).** |
| 0x38 | 0xB8 | float | prev_charge | Previous frame's charge value |
| **0x3C** | **0xBC** | **float** | **charge_at_release** | **Snapshot of charge_value at moment of A release** |
| 0x40 | 0xC0 | float | angular_velocity | Rotation rate from steering |
| 0x54 | 0xD4 | float | boost_speed | Calculated boost speed from charge + angle tables |
| 0x58 | 0xD8 | u32 | total_frames | Increments every frame |
| 0x5C | 0xDC | u32 | frame_counter_1 | Swapped with +0x60 on charge start |
| 0x60 | 0xE0 | u32 | frame_counter_2 | Stores previous frame_counter_1 |
| 0x64 | 0xE4 | u32 | frame_counter_3 | Cleared on max charge |
| 0x6C | 0xEC | u32 | aerial_frames | Frames since airborne |
| 0x9C | 0x11C | float | distance_traveled | Accumulated distance |
| 0xA0 | 0x120 | float | wobble_scale_x | Spring-damped visual oscillation |
| 0xA4 | 0x124 | float | wobble_scale_z | Spring-damped visual oscillation |
| 0xE0 | 0x160 | — | effects_system | Visual effects sub-object (~0x2B0 bytes) |
| 0x3CC | 0x44C | ptr | rumble_controller | Rumble feedback |
| 0x424 | 0x4A4 | ptr | charge_sfx_ctrl | Charge sound effect controller |
| 0x450 | 0x4D0 | Vec3 | position_offset | Position adjustment, cleared each frame |
| 0x460 | 0x4E0 | ptr | model_jobj | Main model JObj root |
| 0x468 | 0x4E8 | ptr | arrow_jobj | Direction arrow JObj |
| 0x480 | 0x500 | ptr | charge_anim_1 | AC_S_CHARGE animation object |
| 0x490 | 0x510 | ptr | charge_anim_2 | Second charge animation object |
| 0x4A4 | 0x524 | float | model_scale | Initialized to 1.0 |
| 0x4AC | 0x52C | ptr | anim_controller | Animation state controller |

## Charge System

### Charge Lifecycle

**1. Start Charging (A pressed)**
- Condition: `button_A != 0` AND `is_charging == 0`
- Sets `is_charging = 1`
- Swaps frame counters, starts rumble

**2. Per-Frame Accumulation (while A held and charge_ready)**
```
base_rate = frame_scale^2
angle_factor = lookup_charge_rate(steering_angle, speed)  // from data table
charge_rate = angle_factor * 0.01 * frame_scale * base_rate
charge_multiplier = vtable+0xAC()  // returns 1.0
charge_value += charge_rate * charge_multiplier
charge_value = min(charge_value, 1.0)  // clamped to max
```

**3. Max Charge Reached**
- When `charge_value` reaches ~1.0 (100.0 * 0.01 * frame_scale)
- Triggers max-charge rumble and visual effect
- Charge stays at max (no auto-discharge timer like 3D mode)

**4. Release / Boost (A released)**
- `charge_at_release = charge_value` (snapshot)
- `is_charging = 0`
- `charge_ready = 0`
- Calculates `boost_speed` from charge level + angle lookup tables
- Starts release rumble proportional to charge level

**5. Charge Depletion (after release)**
```
rate = (100.0 / charge_tier_count) * 0.01 * frame_scale^3
charge_value -= rate
charge_value = max(charge_value, 0.0)
if charge_value == 0.0:
    charge_ready = 1  // can start accumulating again
```

> **Branch condition.** `TopRide_ChargeUpdate` (0x802df900) accumulates (step 2) only when `A_held && charge_ready == 1`; **every other frame it runs this depletion branch** — i.e. whenever `!A_held || charge_ready == 0`. So charge decays toward 0 not just after a boost release but on *any* idle frame where A isn't held. The depletion rate (~`1.0/charge_tier_count`, ≈0.3/frame) is much larger than a single charge step. This is why EnergyLink Auto-Charge in TR must gate injection on `is_charging` (A held), not just `charge_ready` — see `docs/energylink.md` "Top Ride". Idle injection is wiped by this branch the next frame.

### Comparison with 3D Mode

| Aspect | 3D Mode (MachineData) | Top Ride (ChargeComponent) |
|--------|----------------------|---------------------------|
| Charge value | MachineData+0x78C | Component+0x34 (Kirby+0xB4) |
| Range | float 0.0-1.0 | float 0.0-~1.0 |
| Charge rate | base_charge_rate + angle * (turning - base) | Data table lookup by angle/speed |
| Max behavior | Auto-discharge timer, then cooldown | Stays at max, no auto-discharge |
| Grounded check | MachineData+0xC30 bit 0x40 | Not applicable (always grounded in 2D) |
| Charge stat | PATCHKIND_CHARGE modifies rate | Not applicable (no patches) |

## Key Functions

| Address | Size | Name | Description |
|---------|------|------|-------------|
| 0x80008df8 | 0x1C8 | TopRide_SceneLoad | Minor 19 cb_Load |
| 0x80009008 | 0x68 | TopRide_SceneInit | Minor 19 post-GObj think |
| 0x80286be8 | 0x170 | TopRide_GameInit | Main game init (allocs 5MB buffer, creates session) |
| 0x80284298 | 0x2D0 | TopRide_SessionInit | Session constructor |
| 0x8028c010 | 0x820 | TopRide_GameSessionInit | Game session init (creates KirbyMgr etc.) |
| 0x802dafb4 | 0x538 | TopRide_FielderInit | KirbyMgr constructor |
| 0x802d4d64 | 0x788 | TopRide_KirbyInit | Per-player Kirby constructor |
| 0x802d1fe8 | 0x1918 | TopRide_KirbyChargeInit | Charge component constructor |
| 0x802df900 | 0xEA0 | TopRide_ChargeUpdate | **Per-frame charge state machine** |
| 0x802d5ec0 | 0xDCC | TopRide_KirbyPhysUpdate | Per-Kirby physics update |
| 0x802db74c | 0xEBC | TopRide_KirbyMgrUpdate | KirbyMgr per-frame (iterates all 4 Kirbys) |
| 0x8029c650 | 0x8D4 | TopRide_FielderUpdate | Fielder main update loop |
| 0x802d9a24 | 0x8 | TopRide_GetChargeMultiplier | Returns 1.0f (charge rate multiplier) |
| 0x802d98f0 | 0x134 | TopRide_GetChargeTierCount | Charge tier count for depletion rate |
| 0x802de0e4 | 0xC | TopRide_GetDataTable | Returns data table ptr (0x804d40f0) |
| 0x80296264 | 0x8 | TopRide_GetFrameScale | Returns frame rate scale (1.0 at 60fps) |
| 0x802d1d84 | 0x264 | TopRide_VelocityDecay | Post-boost velocity decay |
| 0x80311f2c | 0x5C | TopRide_KirbyHistoryInit | Constructs the per-kirby anti-jitter history ring (kirby+0x64) |
| 0x80311f88 | 0x78 | TopRide_KirbyHistoryPush | Pushes the current paired sign values into the ring |
| 0x80312000 | 0xC0 | TopRide_KirbyHistoryQuery | Queries the ring (used for the spline-snap / direction smoothing) |

### Data Table

Configuration data at `0x804d40f0` (returned by `TopRide_GetDataTable`):

| Offset | Description |
|--------|-------------|
| +0x000 | Charge rate lookup tables (angle vs speed) |
| +0x0F0 | Velocity decay parameters |
| +0x200 | Boost speed lookup tables |
| +0x2F0 | Speed-based angle factor tables |
| +0x4D0 | Charge start speed threshold parameters |
| +0x1FC0 | Speed curve data |
| +0x2100 | Velocity decay base rate |
| +0x2104 | Velocity decay multiplier |
| +0x2108 | Ground distance threshold for charge start |
