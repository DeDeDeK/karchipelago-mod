# Camera Shake and Rumble

Two kinds of feedback make an impact felt. The quake system shakes the player views, offsetting
each camera along its own right and up axes. The pad rumble queue drives each controller's motor
from short scripts. This doc covers both: how a shake source is built and arbitrated, the ways to
start one, and how a rumble is queued, prioritised and gated by the rumble option.

## Quake sources and views

`Quake_Init` (0x8006c798) runs from `Effect_Init` at every 3D scene load. It resets two pools - 256
**sources** of 0x78 bytes and 32 **view records** (`CamShakeRec`) - and creates the quake GObj
with `Quake_Think` (0x8006d95c) as its priority-11 proc. Each player camera owns one view record
from `Quake_CreateView` (0x8006dc10), at `PlayerCamData+0x74`. Because the pools are rebuilt at
every load, a source id held across a scene change is stale.

A source sits on one of two lists: the list of the view it was created for, or the global list
when it was created with no view. Global sources reach every view. Each frame `Quake_Think` steps
every source by its kind, runs its callback, ages it and retires it by its end mode, then calls
`Quake_UpdateView` (0x8006d690) on every view.

| Kind | Output |
|---|---|
| 0 | only what the callback writes |
| 1 | an oscillator that alternates sign each half-cycle with an eased lerp; nothing creates one |
| 2 | a constant offset, set by `Quake_SetConstantOffset` |
| 3 | the driving GObj's root JObj, `trans.x * scale.x` and `trans.y * scale.y` |

| End mode | The source ends |
|---|---|
| 1 | once its frame counter reaches its duration |
| 2 | once its output magnitude drops below 0.0001 |
| 4 | when its kind-3 GObj is gone |
| anything else | never, until removed |

## How a view picks its shake

`Quake_UpdateView` walks the view's own sources and then the global ones, and applies only the
single source with the highest priority. Sources are not summed, and neither the falloff nor the
offset counts toward winning: a positional source out of range, or one at a zero offset, still
mutes every lower-priority source on that view. On a tie the source walked last
wins; new sources are pushed to the front, so among equal global sources the oldest wins.

For the winner it writes `eye_right = out_right * amp_right * f` and `eye_up = out_up * amp_up * f`,
the same values to the interest unless the source is eye-only, and
`gate = out_mag * f * max(amp_right, amp_up)`. `f` is the distance falloff: 1 for a source with no
position, otherwise 1 within 40 units of the view's eye, falling linearly to 0 at 500 units.
`PlyCamGObj_Think` (0x800b3540, priority 13) then offsets the eye and interest it hands the COBJ
along the camera's right and up axes while the gate is positive. The offsets are world units.

Vanilla priorities:

| Source | Priority | Scope |
|---|---|---|
| machine speed shake | 0 | the machine's own view, eye only, vertical |
| small quake effect | 1 | global or one view |
| medium quake effect | 2 | global or one view |
| large quake effect | 3 | global or one view |
| looping and timed quake effects | 1 | global or one view |

The speed shake is a constant source per view that the camera code retunes every frame with
`Quake_SetConstantOffset` and removes when the machine slows down.

## Starting a shake

**A constant source** is the direct way to run a shake whose amplitude changes every frame.
`Quake_AddConstantSource(view, priority, right, up)` (0x8006de8c) creates a source that never ends
on its own and has no position, and returns its id. NULL as the view shakes every view.
`Quake_SetConstantOffset(id, right, up)` (0x8006df74) retunes it and returns 0 when the id is not a
live constant source. `Quake_RemoveSource(id)` (0x8006ca00) ends it.

**Removing an unknown id writes to address 0.** `Quake_FreeSource` (0x8006c8e0), under
`Quake_RemoveSource`, frees whatever its search ends on, NULL included, which also corrupts the
pool's free list. Remove only an id known to be live; `Quake_SetConstantOffset` returning 1 is that
test for a constant source.

**A raw source.** `Quake_AllocSource` (0x8006dcc0) takes the view, priority, end mode, duration,
an optional falloff position, a callback and the two amplitudes. With kind 0 and end mode 1 the
callback writes `out_right`, `out_up` and a positive `out_mag` from the source's frame counter, and
the engine frees the source itself at the end of its duration. `Quake_SetEyeOnly` (0x8006e03c),
`Quake_SetAmplitude` (0x8006e07c) and `Quake_SetPosition` (0x8006e0c0) retune any source by id.

**A quake effect.** The effect ids 370074-370083 each build the shake model 240018 from
`EfCommon.dat` and a kind-3 source driven by its animation; retiring the effect removes the
source. 370079-370081 are the global small, medium and large quakes (anchor mode 510, varargs
`int, Vec3 *pos`), 370082 loops until the effect is retired and 370083 loops for a number of
frames (mode 511, varargs `int, Vec3 *pos, int frames`). 370074-370078 are the same set for one
player's view (modes 506 and 507, with the player first). A NULL position shakes without falloff.
The spawn node keeps the source id at `+0x60`, and `Effect_SetQuakeAmplitude` (0x8023687c) retunes
it through the spawn handle. Event actors, meteors and bombs use these effects.
`EventActor_SpawnEffect` and `Meteor_HitTransition` pass the handle's low word to
`Quake_SetAmplitude` as a source id; it never matches, so their quakes keep the authored amplitude.

## Rumble

Each pad has a queue of rumble nodes sorted by priority. Every frame `HSD_PadRumbleInterpret`
(0x80414b88) steps each node's script and lets the highest-priority live node set the motor. The
GameCube motor is only on or off, so a rumble's strength is its duty cycle.

`Pad_StartRumble(pad, id, kind, duration)` (0x80071d00) queues row `kind` of `lbRumbleData`
(`LbRumble.dat`, 33 rows of a script pointer and a priority) through `HSD_PadRumbleActiveId`
(0x804148d0). `id` tags the node for `HSD_PadRumbleRemoveId` (0x80414694); rider rumbles use 2.
A duration of 0 plays the script once, N loops it for N frames, and -1 loops it forever.
`Pad_StopRumble` (0x80071d48) clears one pad and `Pad_StopRumbleAll` (0x80071d7c) all four.

`Pad_StartRumble` ignores the rumble option. `Rider_TriggerRumble(rider, kind, duration)`
(0x80192110) checks it first, through `Ply_GetRumbleEnabled` (0x8022cbf4): PlayerData+0x908 bit
0x40, which `Player_Create` sets from the player's saved rumble setting and clears for CPU players.
`Enemy_RumblePlayer(ply, kind, duration)` (0x801ff80c) is the same by player index, returning 1
when the player has no rider. Meteor landings use kind 4 for 30 frames on everyone nearby.

| Kind | Script | Priority |
|---|---|---|
| 0 | 4 frames on | 3 |
| 1, 2 | 2 on, 4 off, repeating | 3 |
| 3 | 12 on | 3 |
| 4 | 20 on | 4 |
| 5 | always on | 10 |
| 6 | about 150 frames of thinning pulses | 2 |
| 7 | always on | 5 |
| 8 | 2 on, 8 off, repeating | 5 |
| 9 | 16 on | 6 |
| 10 | 3 on, 1 off, repeating | 5 |

Kind 5's priority masks every other rumble on that pad while it runs. A long rumble that changes
strength is a run of short overlapping pulses of rising and falling duty cycle - 8, 1, 10, 7 -
which expire on their own, so stopping early leaves at most one pulse's tail.

## Key functions

| Function | Address | Role |
|---|---|---|
| `Quake_Think` | 0x8006d95c | step every source, then update every view |
| `Quake_UpdateView` | 0x8006d690 | pick one view's winning source and write its offsets |
| `Quake_AllocSource` | 0x8006dcc0 | create a source of any kind |
| `Quake_AddConstantSource` | 0x8006de8c | create a constant source that never ends |
| `Quake_AddJObjSource` | 0x8006de2c | create a source driven by a GObj's root JObj |
| `Quake_RemoveSource` | 0x8006ca00 | end a source by id |
| `Quake_DestroyView` | 0x8006ca5c | free a view record and every source it owns |
| `Pad_StartRumble` | 0x80071d00 | queue a rumble kind on a pad |
| `Rider_TriggerRumble` | 0x80192110 | rumble a rider's pad if its player has rumble on |
| `Enemy_RumblePlayer` | 0x801ff80c | the same by player index |
