# Hypernova

A City Trial power-up named after Kirby: Triple Deluxe's super-inhale. While it lasts, the rider
grows to twice its size and cycles through a rainbow tint, and holding B turns the inhale into a
wide cone vacuum that pulls in items, breakable props and unridden machines. The power-up is
granted per player - through the exported `HypernovaAPI`, the Miracle Fruit custom item, or a
debug self-test - and reuses the vanilla inhale for everything the player sees while replacing the
suction underneath it.

## Where it runs

Everything the vacuum touches lives in City Trial's open city, so the mod is City Trial only. A
player can be activated, and the per-frame proc does anything, only while the major scene is
`MJRKIND_CITY`, the minor is `MNRKIND_3D`, `CityTrial_IsInStadium` is false and the intro has
ended (`Gm_GetIntroState() == GMINTRO_END`). The stadium test matters: stadium events also run as
`MJRKIND_CITY` + `MNRKIND_3D`.

`On3DLoadEnd` creates one GObj on the City Trial map when **Enabled** is on, with its proc at
priority 23 - the last one `GObj_UpdateAll` runs - on p_link 1. The late priority lets the
vacuum's position writes win over item physics (4), ground snap (5) and the model-matrix appliers
(6). p_link 1 freezes with the match pause, match end and the hitstops, as do the model-scale
applier, the ColAnim selector and effect models the power-up cooperates with, so Hypernova holds
still with them. The GObj is freed with the scene heap, and `OnSceneChange` drops every player's
state and every vacuum claim, snapping scale to neutral since the models are recreated at 1.0.

## Activation

| `HypernovaAPI` | What it does |
|---|---|
| `ActivatePlayer(player, duration)` | Start, or refresh the timer of, one slot; 1 if it took |
| `Activate(duration)` | `ActivatePlayer` for every human slot; 1 if any took |
| `Deactivate()` | End every active player; each eases back to normal size |
| `IsActive()` | 1 while any player is active |
| `FramesRemaining()` | Frames left on the longest-running player, 0 with none |

A `duration` of 0 or less means the menu's **Duration**: 300, 600 or 1200 frames. Only a human
slot (`PKIND_HMN`) can be activated, and only while **Enabled** is on and the scene check above
passes.

The **Miracle Fruit** is a `custom_items` item whose archive ships in this mod's own staged
assets, so it exists only in a build that includes the mod. The mod imports `CustomItemsAPI` from
its scene-change callback - mods boot in FST order, so the export may not exist at this mod's
`OnBoot` - finds the item by its descriptor name, `Miracle Fruit`, keeps its id hash and adds a
pickup handler that calls `ActivatePlayer` for the collector. The lookup repeats at each scene
change until it succeeds. At `On3DLoadStart` the mod opens the fruit's spawn gate only when
**Enabled** is on, the scene is the open city (`Gm_IsInCity`) and it is not the title screen's
attract demo (`Gm_IsAutoDemo`); `custom_items` assigns kinds later in the same load and skips a
closed item, so otherwise the fruit never gets an `ItemKind` and nothing can spawn it.

The fruit is a vanilla item model (the Bomb copy panel's, retextured) that clones the Maxim
Tomato's behavior, so it reads and is collected as food, with equal weight in all three box pools
plus Tac and destructible drops.

## Each frame

For each active player, the proc first decrements the timer, ending the player at zero, and ends
them at once if they now hold a copy ability or power-up. It then advances the shared rainbow hue,
eases every scaling player's `model_scale`, writes the rainbow tint, and drives the inhale gesture;
on a frame the gesture reports an active suck, the vacuum claims what is in that player's cone.
Last, it advances every claimed item, prop and machine, and recolors the live inhale whirlwinds.

Ending a player (expiry, `Deactivate`, or gaining an ability) closes a running suck with
`Rider_EndInhale` (0x801adf98) - the per-player loop stops visiting an inactive slot, so nothing
else would - finishes that player's vacuum claims, starts the scale ease back to 1.0 and clears the
rainbow. Other players' claims stay in flight.

## Copy abilities and power-ups

The inhale is the engine's no-ability attack: `Rider_CanStartInhale` (0x801a617c) requires
`copy_kind == COPYKIND_NONE`. Hypernova is therefore mutually exclusive with a copy ability and
with a City Trial power-up (`powerup_kind`), the way those two exclude each other
(`Rider_GivePowerUpByKind` (0x801a8304) strips the held ability before granting). The vanilla
"last pickup wins" rule applies both ways:

- **Activating** strips what the rider holds: `Rider_AbilityRemoveModel` (0x80191554), which
  clears both `copy_kind` and `powerup_kind` through the held kind's teardown, then
  `RiderState_LoseAbilityEnter` (0x801b0adc) for the spit-out. Without the strip the ability check
  below would end Hypernova on its first frame.
- **Gaining** an ability or power-up while active ends Hypernova that frame, before the inhale is
  driven. The grant has already moved the rider into its new action state during the frame's game
  procs, so driving the inhale again would stomp it.

So the vacuum can pull in copy-ability and power-up items freely: collecting one hands the rider
that state and ends Hypernova, rather than the two fighting over the rider's animation.

## Twice the size

`Rider_ApplyModelMatrix` (0x80190848), run from `Rider_ModelMatrixThink` (0x8018f79c) at proc
priority 6, rebuilds the rider's model matrix every frame through `JObj_SetFromBasis`
(0x80054414) with a scale that is the product of `RiderData.x2c8` (+0x2c8, a copy of the rider
scale) and `RiderData.model_scale` (+0x348). Writing `model_scale` is the whole mechanism. The mod
smoothsteps it from 1.0 to 2.0 over 30 frames on activation and back on deactivation, and leaves a
settled, inactive player's `model_scale` alone so it never fights another writer.

Raising `x2c8` as well would compound to 4x. `x2c8` also scales the vanilla inhale's mouth sphere
in `Hit_CheckRegionOverlap` (0x80189784), but that sphere is tested only against EventActors, so
widening it reaches nothing in City Trial; the vacuum's reach is a mod constant.

## Driving the inhale

The vanilla suction cannot be reused. Every stage of it is written for EventActor enemies:
`Rider_TryStartInhale` (0x8019c5ac) and the per-frame `Rider_InhaleCaptureScan` (0x8019c63c) walk
only the EventActor p_link (12), `EventActorGObj_IsInhalable` (0x802041c8) admits only EventActors,
the overlap test reads an EventActor hit region, and capture and the captured driver
(`EventActor_OnCapture` (0x802038c4), `EventActor_InhaledPri6` (0x80203b64)) read and write
`EnemyData`. City Trial has nothing inhalable either - its event actors and CPU players are not
flagged - and since `Rider_TryStartInhale` enters the inhale only once the overlap test has found a
target, the vanilla inhale never starts in City Trial at all.

The presentation is reused instead. `Rider_StartInhale` (0x801ad2c4) has no gate and needs no
target: it enters the suck-start status, spawns the suction whirlwind (`Effect_SpawnSync` kind
`0x3a982`) at the mouth, plays the inhale sound and installs the capture callbacks, which in City
Trial find nothing inhalable and capture nothing. The inhale is three rider statuses
(`RiderData.status`, +0x1c) that do not chain on their own:

| Status | Name | Behavior |
|---|---|---|
| `0x2f` | `RDSTATE_DRAWSTART` | A one-shot gulp; when its animation ends the rider returns to neutral, not to the loop |
| `0x30` | `RDSTATE_DRAW` | The open-mouth loop, entered only through `Rider_StartInhaleLoop` (0x801ad4cc); it re-enters itself and ends when `RiderData.inhale_timer` runs out with the mouth empty |
| `0x31` | `RDSTATE_DRAWEND` | Closes the mouth with a puff, then neutral; entered through `Rider_EndInhale` |

The mod drives the gesture per player, from whatever riding state the rider is in - Kirby's
neutral riding is a cluster of lean and turn states, and vanilla inhales from all of them:

- **Idle, B held:** `Rider_StartInhale`; the phase becomes gulp.
- **Gulp:** on the frame `status` leaves `0x2f`, `Rider_StartInhaleLoop`, before the frame renders,
  so the open mouth does not flicker back to neutral; the phase becomes loop.
- **Loop:** while `status` is `0x30`, top `inhale_timer` up to 8 so the engine's countdown never
  ends the loop; if the engine dropped the rider out of the loop to anything but start or end while
  B is still held, re-enter it.
- **B released:** a running loop ends through `Rider_EndInhale`, the engine's own close; a tapped
  gulp finishes by itself.

`inhale_timer` (+0x93c) aliases `copy_wheel_result`, so other systems may write it; that is why the
loop is ended explicitly rather than by letting the timer lapse. B counts as held only while
`Rider_IsOnMachine`: the inhale state procs dereference the rider's machine, so dismounting reads
as a release.

## The rainbow tint

Kirby's body color cannot be swept. It is a texture swap: `RiderKirby_SetMaterialColorAndUpdate`
(0x80198d3c) seeks each body TObj's animation to a baked color variant, so a continuous color
would only snap between textures. The tint is drawn through the **ColAnim** overlay instead - the
color behind the invincibility and intangibility flashes, which a TEV stage blends over whatever
texture is showing.

`RiderData.col_anim` is a `ColAnimState`: three `ColAnimSlot`s followed by the render state they
resolve into. Each frame `ColAnim_Resolve` (0x8006ae7c) copies the highest-priority active slot's
`color` into the render state, but only while that slot's `flags` holds `COLANIM_FLAG_TINT`, which
a running animation tick normally keeps set. `Rider_ApplyColAnim` (0x8019bfb4) always targets the
body slot, `slot[0]`.

The mod takes over ColAnim index 3, the invincibility flash `Rider_GiveInvincibility`
(0x80195f28) applies. It applies it once - flooring the slot's `pri` first, since `ColAnim_Apply`
(0x8006a3f0) is priority-gated - then nulls the slot's `cmd_data` to freeze the tick, which would
otherwise re-stamp the flash's own green. From then on it owns the slot every frame: `pri` pinned
to `COLANIM_PRI_MAX` so no pickup flash can win the resolver or the apply gate,
`COLANIM_FLAG_TINT` held, and the hue written into the slot's `color` and `color_f` and into the
render state's `color`, with the render state's tint flag set and its ratio blend off
(`COLANIM_RATIO_NONE`). The tint runs at alpha 100 and turns the wheel every 120 frames, one hue
shared by every rider. Ending calls `ColAnim_Reset` (0x8006a250) on the slot, so the normal flashes
resume.

## The whirlwind

The swirl in front of the mouth is a separate model effect, recolored each frame to the
complementary hue softened 55% toward white. `Rider_StartInhale` discards the effect's handle, and
the effect respawns at a new address every inhale, so the live ones are found by walking the
model-effect p_link (`GAMEPLINK_EFFECTMODEL`) for `GAMEENTITY_EFFECT` GObjs whose `Effect.kind` is
`0x3a982`. Writing `Effect.life` would freeze the swirl's animation, so its lifetime is left alone.

Its color is not in the material colors. `MObjSetupTev` (0x803faba0) rebuilds the TEV expression
every frame, but the expression's constant node only points at the TObj's `_HSD_TObjTev` block
(`TObj+0xa8`), whose `constant`, `tev0` and `tev1` are plain `GXColor` values the texture animation
never touches. The swirl's combiner lerps `tev0` to `constant` by texture brightness, and its alpha
is `constant.a` times the texture alpha, so the mod rewrites the RGB of all three on every TObj of
the effect's joint tree and keeps each alpha, which keeps the vanilla opacity. The expression nodes
themselves are never written: their list links and color pointers are live.

## Settings

The **Hypernova** page:

| Option | Default | Effect |
|---|---|---|
| Enabled | On | Creates the per-frame proc and opens the Miracle Fruit's spawn gate |
| Duration | Medium | Short / Medium / Long = 300 / 600 / 1200 frames |
| Suck Props | On | The vacuum also claims breakable props |
| Suck Machines | On | The vacuum also claims unridden machines |
| D Pad self test | Off | D-Pad Up on port 1 activates every human; not saved |
| Debug Cone | Off | Draws the suction cone; not saved |

Options change only in the main menu, so a round sees fixed values; `On3DLoadEnd` reads
**Enabled** and **Debug Cone** once to decide which GObjs to create, and activation still checks
**Enabled** because an API caller can reach it with no proc running.

The debug cone is a GX-link-only GObj created at `On3DLoadEnd` on the City Trial map (not in a
stadium). On the translucent pass it draws, for every human rider with a usable facing, a
translucent red cone from the rider's position along its forward vector, with the vacuum's reach
and half-angle, so what it shows is what gets claimed. It needs neither an active Hypernova nor
**Enabled**, and since it has no proc it keeps drawing through a pause.
