# Top Ride

Top Ride is a 2D mode with its own engine, separate from the 3D mode's players, riders and
machines: it never creates a `PlayerData`, `RiderData` or `MachineData`. A Top Ride player is a
`TopRideKirby` object with an inline charge component and a polymorphic state handler, owned by a
KirbyMgr singleton. A mod that does anything in Top Ride - give items, apply effects, scale Kirby,
draw an overlay - works with these objects and their timing rather than the 3D ones.

## Scenes

Top Ride is major `MJRKIND_TOP` (5). Its screens are minors of that major: the rules screen (4),
the course select (7, `TopRide_CourseSelectInit` 0x8003d0dc), the player select lobby (9,
`TopRide_LobbyInit` 0x8002dc9c) and the round, **minor 19** (`MNRKIND_TOPRIDE`) - not the shared
minor 18 that Air Ride and City Trial rounds use. The round never goes through `SceneLoad_3D`, so
code hooked into the 3D load, pause or exit does not run in it.

Minor 19's callbacks:

| Slot | Function |
|---|---|
| `cb_Load` | `TopRide_SceneLoad` (0x80008df8): seeds the RNG, sets the speed, calls `TopRide_GameInit` (0x80286be8), which allocates the Top Ride heap and creates the session |
| `cb_Exit` | 0x80008fe4, a thin wrapper around 0x80286df0 (the map's `fn_synthAddStudioInput` name is wrong) |
| `cb_ThinkPostGObjProc` | `TopRide_SceneInit` (0x80009008), which checks the exit conditions |
| `cb_ThinkPostRender` | `TopRide_PostRenderCallback` (0x80009074) |
| pre-GObj and pre-render | Single-`blr` stubs (0x80009004, 0x80009070) |

**The second render pass.** `TopRide_PostRenderCallback`'s only body is `bl TopRide_CustomRenderer`
(0x80286d7c) at 0x80009080, and `TopRide_CustomRenderer` starts an entirely new `HSD_StartRender`
pass for the 2D engine. That pass overwrites the frame after the standard render, so anything drawn
through a screen-space text canvas is wiped every frame in a Top Ride round. An overlay has to be
re-rendered after the pass returns - for instance from a hook at 0x80009084, the instruction after
the `bl`, that calls `CObjThink_Common` on each canvas camera again.

## Objects

```
GameSession (TopRide_GameSessionInit 0x8028c010)
  KirbyMgr (TopRide_KirbyMgrInit 0x802dafb4; pointer at 0x805ddb44, NULL outside a round)
    kirbys[4]           +0x08, one TopRideKirby per occupied slot (TopRide_KirbyInit 0x802d4d64)
    item manager        +0x3DE8, built in place by TopRideItem_MgrInit (pointer also at 0x805ddba4)
    round_state         +0x4028
```

Each `TopRideKirby` (more than 0x1400 bytes, vtable 0x804d2304, RTTI name "Kirby") holds its state
handler at +0x7C, the inline charge component at +0x80 and an item `Absorber` at +0xD00.

## Round State

`KirbyMgr.round_state` (+0x4028) is the master gate for per-frame gameplay. It goes 0 -> 1 -> 2
over a match:

| Value | Phase | Effect |
|---|---|---|
| 0 | Scene loading | Physics, item spawning and `TopRideItem_Update` all skipped |
| 1 | Countdown | Physics runs; item spawning and `TopRideItem_Update` still skipped |
| 2 | Race active | Everything runs |

`TopRide_KirbyMgrUpdate` (0x802db74c) reads it at 0x802db850 (`!= 0`: the per-kirby physics and
tracking, including `TopRide_KirbyPhysUpdate`), 0x802db8b0 (`== 2`: the item spawn picker) and
0x802dc570 (`== 2`: `TopRideItem_Update`); `TopRide_GameModeNormalUpdate` (0x8029c650) gates its
gameplay branch on 2. It is set to 1 at 0x8029ec04 when the countdown starts and to 2 at 0x8029efa4
when the race starts.

Anything that spawns an item or drives a kirby state from outside the engine's own flow - an item
given by a mod, a status effect - must wait for 2. Before that the state handler may be only partly
wired, and an item spawned during the countdown sits on the list unticked and is culled at the
start. Two other session bytes matter: `GameSession+0x38` picks the item spawn variant, and
`GameSession+0x40 == 3` (a no-items configuration, set at scene load) blocks item spawning.

## Player Kind

The per-slot human/CPU/empty byte is the first byte of the 9-byte `TopRideSlot` at
`GameData+0xD20 + slot * 9` (`GameData.topride_config.slots[slot]`): `TR_PKIND_HMN` (0),
`TR_PKIND_CPU` (1), `TR_PKIND_NONE` (2). `TopRide_GetPlayerKind` (0x8000bd6c) and
`TopRide_SetPlayerKind` (0x8000bda8) read and write it. The rest of the block holds the color
(+1), an unused CPU level byte (+2), the handicap that actually drives the AI (+3), the controller
port (+6) and the machine (+8). The lobby fills these each round and `TopRide_KirbyMgrInit` reads
them to decide which kirbys to create.

To pick out humans, iterate `kirbys[i]` and test `TopRide_GetPlayerKind(kirby->player_slot)`. The
kirby's `start_position` (+0x0E) is its shuffled grid slot, not a CPU flag, and `standing` (+0x10)
is never written in the solo modes.

## Kirby Fields

- **`position` (+0x4C) is the spawn position only.** The live position is `charge.position`
  (kirby+0x88).
- **`place` (+0x0F) and `standing` (+0x10)** are 0-based ranks rewritten every frame by the ranking
  pass in `TopRide_KirbyMgrUpdate`. `place` sorts by course progress, the live race order;
  `standing` sorts by `finish_time`, the start grid breaking ties, so `standing == 0` on the frame a
  kirby finishes is what the 1st-place checklist cells test. In Time Attack and Free Run the pass is
  skipped and `standing` reads 0 for everyone, so solo-mode code gates on `round_state == 2` and the
  player kind instead.
- **`finish_time` (+0x1C)** is the master race timer latched when `finished` (+0x3E) is set; until
  then it holds a projected total time, which is what lets `standing` rank unfinished kirbys.
- **`lap_progress` (+0x14)** counts completed laps: -1 until the line is first crossed, +1 per
  forward crossing, never decreasing. Each increase from 1 up moves `cur_lap_frames` (+0x24) into
  `prev_lap_frames` (+0x20). The lap and kirby timers tick in `TopRide_KirbyPhysUpdate` only while
  `round_state` is 2 and the kirby has not finished, so lap 1 is timed from the start signal.
  `finished` is set when `lap_progress` reaches the lap total, in Race and Time Attack only.
- **`active_item_kind` (+0x11)** is written by `TopRide_KirbyApplyItem` (0x802d8cb4) with every
  item it applies. For a human the per-course objective check resets it to 0xFF each frame, so it
  means "the item applied since last frame"; a CPU's keeps the last kind. It is not a "power
  active" flag.
- **`input_reader` (+0x48)** is a pad reader for humans and the AI reader for CPUs, polled each
  frame.
- **`history` (+0x64)** is a 10-entry stick-input ring (`TopRide_KirbyHistoryPush` 0x80311f88,
  `TopRide_KirbyHistoryQuery` 0x80312000). A summed change above 200 is a flick, which starts the
  spin attack, so the one query call at 0x802d5f90 gates the whole voluntary quick spin.
- **`charge.model_scale`** (kirby+0x524) is multiplied into the model scale every frame by
  `TopRide_KirbyModelThink` (0x802e26dc), so a write persists until the kirby is recreated.

## Charge

The charge component at kirby+0x80 is built by `TopRide_KirbyChargeInit` (0x802d1fe8) and run
each frame by `TopRide_ChargeUpdate` (0x802df900). The fields a mod reads are `is_charging`
(kirby+0xAC, A held), `charge_ready` (+0xAD), `charge_value` (+0xB4, 0.0 to about 1.0, Top Ride's
counterpart of `MachineData.charge_value`) and `charge_at_release` (+0xBC).

1. **A pressed** while not charging: `is_charging` is set and rumble starts.
2. **Accumulation**, while A is held and `charge_ready` is 1: each frame adds a rate looked up by
   steering angle and speed from the data table, times 0.01 and the cube of the frame-rate scale,
   clamped at 1.0.
3. **Full charge** fires the max-charge rumble and effect and stays at 1.0; there is no automatic
   discharge as in the 3D mode.
4. **Release** snapshots `charge_at_release`, clears `is_charging` and `charge_ready`, derives the
   boost speed and starts a release rumble.
5. **Depletion** subtracts `(100 / tier count) * 0.01` times the frame-rate scale cubed per
   frame, floored at 0, and sets `charge_ready` again at 0.

The accumulation branch runs only when A is held and `charge_ready` is 1; **every other frame runs
the depletion branch**, including any idle frame with A up. Depletion (`TopRide_GetChargeTierCount`
0x802d98f0) far outpaces one accumulation step, so a value written into `charge_value` on an idle
frame is gone the next frame; injected charge only sticks while `is_charging` is set.

## Data Table

`TopRide_GetDataTable` (0x802de0e4) returns the engine's tuning block at 0x804d40f0: charge rate
tables by angle and speed (+0x000), velocity decay parameters (+0x0F0), boost speed tables
(+0x200), speed-based angle factors (+0x2F0), charge start thresholds (+0x4D0), the speed curve
(+0x1FC0), and the velocity decay base rate, multiplier and charge-start ground distance
(+0x2100, +0x2104, +0x2108). `TopRide_GetFrameScale` (0x80296264) is 1.0 at 60 fps, and
`TopRide_VelocityDecay` (0x802d1d84) applies post-boost slowdown.
