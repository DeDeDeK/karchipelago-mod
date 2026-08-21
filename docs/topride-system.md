# Top Ride System

Top Ride is a 2D mode with its own engine, completely separate from the 3D mode's Rider/Machine/Player system. It does **not** use `Player_Create`, `Rider_Create`, `Machine_Create`, `stc_playerdata`, `RiderData`, or `MachineData` - a Top Ride player is a `TopRideKirby` object with an inline charge component and a polymorphic state handler. The scene loads through **minor 19** (`MNRKIND_19`), not the shared minor 18 (`MNRKIND_3D`), so `On3DLoadEnd` never fires for Top Ride; hoshi's `OnTopRideLoadEnd` mod callback (hooked at 0x80008fac, inside `TopRide_SceneLoad`) is the load notification instead.

The structs, enums, accessors and state-transition helpers are declared in `externals/hoshi/include/topride.h`.

## Scene Flow

```
Main Menu (minor 2)
  -> MJRKIND_TOP (major 5)
    -> minor 24: Top Ride settings
    -> minor 7: Course select (TopRide_CourseSelectInit, 0x8003d0dc)
    -> minor 9: Player select (TopRide_LobbyInit, 0x8002dc9c)
    -> minor 19: Gameplay (cb_Load = 0x80008df8)
```

Minor 19's `MinorSceneDesc` callbacks: `cb_Load` = `TopRide_SceneLoad` (0x80008df8), which seeds the RNG, initializes speed, and calls `TopRide_GameInit`; `cb_Exit` = 0x80008fe4 (a thin wrapper around 0x80286df0; the map's `fn_synthAddStudioInput` name for it is bogus); `cb_ThinkPostGObjProc` = `TopRide_SceneInit` (0x80009008), which checks exit conditions; `cb_ThinkPostRender` = `TopRide_PostRenderCallback` (0x80009074). The two pre-think callbacks (0x80009004, 0x80009070) are single-`blr` stubs.

## Post-Render Second Pass

`TopRide_PostRenderCallback` is a bare wrapper whose only body is `bl TopRide_CustomRenderer` (0x80286d7c) at 0x80009080. `TopRide_CustomRenderer` kicks off an **entirely new `HSD_StartRender` pass** for the 2D engine. That pass overwrites the EFB *after* the standard frame render - so anything a mod drew through a hoshi screen-space canvas (HUD text, notifications, the textbox) is wiped every frame in Top Ride.

To keep a screen overlay visible in TR, re-issue its render *after* the post-render pass returns. `mods/textbox/src/main.c` hooks **0x80009084** (the instruction right after the `bl`) and, for each `TextCanvas` in the `stc_textcanvas_first` list, re-calls `CObjThink_Common(canvas->cam_gobj)` - redrawing on top of the second pass. Any mod with a Top Ride HUD/text overlay needs the same re-render.

## Object Hierarchy

```
GameSession (TopRide_GameSessionInit, per-scene)
  +-- KirbyMgr singleton (TopRide_KirbyMgrInit, 0x802dafb4)
        +-- Kirby[0..3] (TopRide_KirbyInit, per-player)
        |     +-- StateHandler (+0x7C ptr, vtable 0x804d6f5c)
        |     +-- ChargeComponent (+0x80, initialized by TopRide_KirbyChargeInit)
        |     +-- Absorber (+0xD00, vtable 0x804bdc70, RTTI "ItemMgr::Absorber")
        +-- TopRideItem_Mgr (+0x3DE8)
        +-- EnemyMgr (+0x3F58)
        +-- MineMgr (+0x3F74)
        +-- EmberMgr (+0x3F90)
        +-- SmokeMgr (+0x3FA8)
        +-- MissileMgr (+0x3FC4)
        +-- SoundHandles (+0x4020)
        +-- round_state (+0x4028)
```

Manager classes are identified by RTTI `dynamic_cast` typeinfo and the construction stores in `TopRide_KirbyMgrInit` / `TopRide_GameSessionInit`. The offsets above are KirbyMgr-struct member offsets - a separate mapping from the SDA globals below, which hold pointers to the same objects.

## Globals

| Address | Object |
|---------|--------|
| 0x805ddb44 | KirbyMgr (`stc_topride_kirbymgr`, r13+0xA64). NULL when not in Top Ride gameplay. |
| 0x805ddb48 | ChickMgr |
| 0x805ddb4c | ItemBall manager |
| 0x805ddb50 | KurakkoMgr |
| 0x805ddb54 | MissileMgr |
| 0x805ddb58 | SmokeMgr |
| 0x805ddb5c | EmberMgr (clear-checker reads field +0x18) |
| 0x805ddb60 | MineMgr |
| 0x805ddb64 | GrenadeMgr (clear-checker reads field +0x1c) |
| 0x805ddb68 | EnemyMgr |
| 0x805ddb84 | GameSession (contains KirbyMgr) |
| 0x805ddb38 | CpuObstacleMgr (created in `TopRide_GameSessionInit`) |
| 0x805ddb8c | SoundHandles |
| 0x805ddba4 | TopRideItemMgr (`stc_topride_itemmgr`, r13+0xAC4) |
| 0x805ddbec | GameSession sub-object |

## Round State

The TR round phase is a single u8 at **`KirbyMgr+0x4028`**, and it is the master gate for essentially all per-frame gameplay work. Values progress 0 -> 1 -> 2 over the lifetime of a match:

| Value | Phase | Effects |
|-------|-------|---------|
| 0 | Pre-init (scene loading) | Physics, item spawning, and `TopRideItem_Update` are all skipped |
| 1 | Countdown | Physics runs; item spawning + `TopRideItem_Update` still skipped |
| 2 | Race active | Everything runs |

Read sites:

| Address | Function | Gate | Effect |
|---------|----------|------|--------|
| 0x802db850 | `TopRide_KirbyMgrUpdate` | `!= 0` | Runs the per-Kirby physics + tracking block (incl. `TopRide_KirbyPhysUpdate`) |
| 0x802db8b0 | `TopRide_KirbyMgrUpdate` | `== 2` | Runs the per-Kirby weighted item-spawn picker |
| 0x802dc570 | `TopRide_KirbyMgrUpdate` | `== 2` | Calls `TopRideItem_Update` (item lifetime/render tick) |
| 0x8029c714, 0x8029c7ec | `zz_8029c650_` (session per-frame update) | `== 2` | Gameplay branch |

Write sites: `zz_8029e334_` writes 1 at 0x8029ec04 when the countdown begins; `zz_8029eda4_` writes 2 at 0x8029efa4 when the countdown ends and the race starts.

### Implication for mod code

Anything that spawns a TR item or drives a Kirby state from outside the per-frame engine flow (AP item give, traplink, deathlink) must wait for `round_state == 2`. Before that, `state_handler` may be only partially wired, and the item list isn't ticked: spawning during countdown puts the item on the list, but `TopRideItem_Update` doesn't tick it and the item is culled at race start. `GateTopRideItems_GiveItem` (`mods/archipelago/src/gate_topride_items.c`) returns 0 in that window so `APItems_PerFrame` retries the item on the next frame.

### Secondary state bytes

- `GameSession+0x38` (where `GameSession = KirbyMgr->game_config`): sub-mode flag, not the master state. Read in `TopRideItem_SpawnTimed` to pick the spawn variant.
- `GameSession+0x40`: static config field set from `ConfigDesc` in `zz_802c50ac_` at scene load. `+0x40 == 3` blocks all item spawning (a "no items" stadium variant). Stable across the match.

## Player Kind (Human / CPU / Empty)

The per-slot human/CPU/empty discriminator is the first byte of the 9-byte `TopRideSlot` config block at **`GameData[slot*9 + 0xD20]`** (= `GameData.topride_config.slots[slot]`, 4 entries for slots 0-3): 0 = `TR_PKIND_HMN` (human, controller bound), 1 = `TR_PKIND_CPU`, 2 = `TR_PKIND_NONE` (empty).

Vanilla accessors `TopRide_GetPlayerKind` (0x8000bd6c) and `TopRide_SetPlayerKind` (0x8000bda8) read/write that byte. The rest of the 9-byte block holds color (+1), an inert "CPU level" byte (+2), the handicap / CPU-skill byte that actually drives the AI (+3), the controller port (+6), and the machine kind (+8) - accessors for all of them are in `topride.h`.

`TopRide_PreGameThink` (0x8002c06c) populates these bytes from menu input each round. `TopRide_KirbyMgrInit` (0x802dafb4) reads them at construction time to decide which kirbys to spawn.

**Filtering humans in mod code**: iterate `kirby_mgr->kirbys[i]` and check `TopRide_GetPlayerKind(kirby->player_slot) == TR_PKIND_HMN`. **Do not** use `kirby->start_position` (Kirby+0x0E) - that's the per-round shuffled grid position, not a CPU flag - and do not use `kirby->is_active` (Kirby+0x10), which stays 0 in the solo modes (Time Attack, Free Run) even while a human is playing.

## Kirby Object

Each player is a Kirby object (>0x1400 bytes; an `Absorber` sub-object lives at +0xD00). Vtable at `0x804d2304`, RTTI name "Kirby" (name string at `0x805d9134`, typeinfo record at `0x805d913c`). Created by `TopRide_KirbyInit` (0x802d4d64). `TopRideKirby` in `topride.h` maps the head plus the inline charge component (0x00..~0x530); the rest is unmapped.

`session_data` (+0x04) aliases the inline charge component at kirby+0x80 - that indirection is how `TopRide_KirbyModelThink` (0x802e26dc) reaches `model_jobj` / `model_scale` via `session_data[+0x460]` / `session_data[+0x4A4]` (absolute kirby+0x4E0 / +0x524).

Non-obvious fields worth knowing before reading the struct:

- **`position` (+0x4C) is the spawn/default position only** - it is not updated per frame. The live in-world position is `charge.position` (kirby+0x88).
- **`place` (+0x0F)** is written every frame by the ranking pass in `TopRide_KirbyMgrUpdate` and is 0 while still racing, so `place == 0` reads as "not yet finished".
- **`is_active` (+0x10)** is a final-standings byte set on race start in Race rounds only. It stays 0 through all of Time Attack and Free Run - never gate solo-mode mod code on it; use `round_state == 2` plus `TopRide_GetPlayerKind() == TR_PKIND_HMN`.
- **`active_item_kind` (+0x11)** is written by `TopRide_KirbyApplyItem` (0x802d8cb4) but **not** cleared on natural expiry, so it is not a reliable "power active" flag.
- **`input_reader` (+0x48)** is polymorphic: human slots get a pad reader (vtable `0x804d25e0`), CPU slots the AI brain reader (vtable `0x804d8710`). Polled each frame via vt[0x14].
- **`history` (+0x64)** is a 10-entry circular stick-input ring, pushed by `TopRide_KirbyHistoryPush` (0x80311f88) and summed by `TopRide_KirbyHistoryQuery` (0x80312000). A summed |delta| above 200 is a flick, which sets `charge.angular_velocity` and enters the spin-attack state - so that one query call at 0x802d5f90 gates the entire voluntary quick-spin move.
- **`charge.model_scale`** (kirby+0x524) is multiplied into the model JObj scale every frame by `TopRide_KirbyModelThink`, so a mod's write persists until the kirby is recreated.

## Charge System

The charge state lives in the inline charge component at kirby+0x80, constructed by `TopRide_KirbyChargeInit` (0x802d1fe8) and driven each frame by `TopRide_ChargeUpdate` (0x802df900). The three fields a mod normally cares about are `is_charging` (kirby+0xAC, A held), `charge_value` (kirby+0xB4, 0.0..~1.0 - Top Ride's equivalent of `MachineData.charge_value` at +0x78C), and `charge_at_release` (kirby+0xBC).

### Lifecycle

1. **A pressed** (`button_A != 0` and `is_charging == 0`): sets `is_charging = 1`, swaps the frame counters, starts rumble.
2. **Accumulation** while A is held and `charge_ready == 1`. The per-frame step is an angle/speed lookup out of the data table, scaled by frame rate and by the vtable+0xAC multiplier (`TopRide_GetChargeMultiplier`, which just returns 1.0), then clamped to 1.0:
   ```
   charge_rate = lookup_charge_rate(steering_angle, speed) * 0.01 * frame_scale^3
   charge_value = min(charge_value + charge_rate, 1.0)
   ```
3. **Max charge**: at ~1.0 the max-charge rumble and visual effect fire. Charge then *stays* at max - there is no auto-discharge timer as in 3D mode.
4. **Release**: snapshots `charge_at_release = charge_value`, clears `is_charging` and `charge_ready`, derives `boost_speed` from the charge level plus the angle lookup tables, and starts a release rumble proportional to charge.
5. **Depletion**: `charge_value -= (100.0 / charge_tier_count) * 0.01 * frame_scale^3`, floored at 0.0; reaching 0.0 sets `charge_ready = 1`.

### The decay branch runs on every idle frame

`TopRide_ChargeUpdate` runs the accumulation branch only when `A_held && charge_ready == 1`; **every other frame it runs the depletion branch** - i.e. whenever `!A_held || charge_ready == 0`. Charge therefore decays toward 0 not just after a boost release but on *any* idle frame where A isn't held. The depletion rate (~`1.0/charge_tier_count`, roughly 0.3/frame) dwarfs a single accumulation step, so anything a mod injects into `charge_value` on an idle frame is wiped the next frame. EnergyLink Auto-Charge in TR consequently gates injection on `is_charging` (A actually held), not on `charge_ready`.

### Differences from 3D mode

| Aspect | 3D Mode (MachineData) | Top Ride (charge component) |
|--------|----------------------|-----------------------------|
| Charge value | MachineData+0x78C | Kirby+0xB4 |
| Charge rate | base_charge_rate + angle * (turning - base) | data-table lookup by angle/speed |
| Max behavior | auto-discharge timer, then cooldown | stays at max, no auto-discharge |
| Grounded check | MachineData+0xC30 bit 0x40 | not applicable (always grounded in 2D) |
| Charge stat | PATCHKIND_CHARGE modifies rate | not applicable (TR has no patches) |

## Key Functions

| Address | Size | Name | Description |
|---------|------|------|-------------|
| 0x80008df8 | 0x1C8 | TopRide_SceneLoad | Minor 19 cb_Load |
| 0x80009008 | 0x68 | TopRide_SceneInit | Minor 19 post-GObj think |
| 0x80009074 | 0x20 | TopRide_PostRenderCallback | Minor 19 post-render; calls `TopRide_CustomRenderer` |
| 0x80286d7c | 0x74 | TopRide_CustomRenderer | Second `HSD_StartRender` pass for the 2D engine |
| 0x80286be8 | 0x170 | TopRide_GameInit | Main game init (allocates the TR heap, creates the session) |
| 0x80284298 | 0x2D0 | TopRide_SessionInit | Session constructor |
| 0x8028c010 | 0x820 | TopRide_GameSessionInit | Game session init (creates KirbyMgr etc.) |
| 0x802dafb4 | 0x538 | TopRide_KirbyMgrInit | KirbyMgr constructor |
| 0x802db518 | 0x234 | TopRide_SpawnKirby | Per-slot kirby spawn helper |
| 0x802d4d64 | 0x788 | TopRide_KirbyInit | Per-player Kirby constructor |
| 0x802d1fe8 | 0x1918 | TopRide_KirbyChargeInit | Charge component constructor |
| 0x802df900 | 0xEA0 | TopRide_ChargeUpdate | Per-frame charge state machine |
| 0x802d5ec0 | 0xDCC | TopRide_KirbyPhysUpdate | Per-Kirby physics update |
| 0x802e26dc | 0x1E8 | TopRide_KirbyModelThink | Per-frame model JObj transform (applies `model_scale`) |
| 0x802db74c | 0xEBC | TopRide_KirbyMgrUpdate | KirbyMgr per-frame (iterates all 4 kirbys) |
| 0x8029c650 | 0x8D4 | `zz_8029c650_` | Session main update loop (unnamed in the map) |
| 0x802d8cb4 | 0x5D8 | TopRide_KirbyApplyItem | Applies a `TopRideItemKind` to a kirby |
| 0x802d9a24 | 0x8 | TopRide_GetChargeMultiplier | Returns 1.0f |
| 0x802d98f0 | 0x134 | TopRide_GetChargeTierCount | Charge tier count (sets the depletion rate) |
| 0x802de0e4 | 0xC | TopRide_GetDataTable | Returns the data table ptr (0x804d40f0) |
| 0x80296264 | 0x8 | TopRide_GetFrameScale | Frame-rate scale (1.0 at 60fps) |
| 0x802d1d84 | 0x264 | TopRide_VelocityDecay | Post-boost velocity decay |
| 0x80311f2c | 0x5C | TopRide_KirbyHistoryInit | Constructs the per-kirby stick-input ring (kirby+0x64) |
| 0x80311f88 | 0x78 | TopRide_KirbyHistoryPush | Pushes the current paired sign values into the ring |
| 0x80312000 | 0xC0 | TopRide_KirbyHistoryQuery | Queries the ring (direction smoothing, quick-spin flick test) |
| 0x802cf83c | 0xEB4 | TopRide_ApexGridScan | Tile/grid collision + per-sector apex evaluation |

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
