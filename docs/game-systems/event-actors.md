# Event Actors

The three actors City Trial's events bring into the city: TAC, Dyna Blade and the Meteor. They
are ordinary enemy-system actors - an `EnemyData` built by `EventActor_Create` (0x801fbb50) and
driven by a per-type state table - but no spawn pool places them: their event's per-frame
function creates them, and their behavior is mostly scripted rather than reactive. A modder
needs this to spawn one outside its event, to change what it drops, or to understand why it
ignores the usual enemy rules.

## What sets them apart

All three are kinds 0x4C and up, which the enemy code singles out:

- `EventActorGObj_ProcPhys` skips the floor kill that destroys a regular enemy below the stage.
- `EventActor_SetVisibility` (0x801fed74) clears the invisible bit but then *disables* rendering
  for kinds >= 0x4C; each actor turns rendering back on itself when it is ready.
- Their hit-collision pass runs regardless of the spawn-slot gates regular enemies use, and TAC
  and Dyna Blade install their own hit reaction instead of the default knockback.

They come in through the event engine: `event_tac` (0x801104fc), `event_dynablade` (0x8011024c)
and `event_meteor` (0x80110c0c) each call `EventActor_Create` at a position from the event's
location table. Their archives (`EmTacData.dat`, `EmDynaData.dat`, `EmMeteoData.dat`) are loaded
with the city's enemy list at stage load. TAC and Dyna Blade affect items only by throwing fresh
pickups into the world (`CityItem_GetEventItem` 0x80254114, `CityItem_Throw` 0x80253ce4),
drawing from their column of the event-source drop table; neither touches a rider's inventory.

## TAC (0x4C)

Descriptor 0x804b4178, 12 states from 0x804b4088, init `Tac_Init` (0x8021a534). From spawn it
randomly chases or wanders. While chasing, its `pri6_cb` (0x8021aa6c) probes ahead with
`EventActor_FindPlayerInRangeFwd` (0x801fe764); with a rider in range and in front it dashes and
grabs, steering toward the target each frame. After a grab it can dash again or flee, climbing
until it is off screen and destroying itself. A hit drops it into its own hit reaction.

The "steal" is not a steal. `Tac_ScatterItems` (0x8021c8ec) throws new pickups in a fan around
TAC's forward, mid-dash on a timer and again when hit; nothing in TAC writes a player's
collected items. Its contact damage is an ordinary attack region. Its tuning (detect range, dash
speed, item counts) is per-type data in the archive.

## Dyna Blade (0x4D)

Descriptor 0x804b4288, 8 states from 0x804b41e8, init `DynaBlade_Init` (0x8021c9dc), with 8
attack regions where other enemies have 2. It descends to a cruise altitude, passes over -
rumbling any rider within range, the only player-aware code it has - then swoops. The swoop's
path is the baked model animation: `DynaBlade_StateDive_Proc` (0x8021d500) samples the root
joint's world position each frame and differentiates it into velocity, and the swoop's first
frame calls `DynaBlade_ThrowItems` (0x8021db44). Once it climbs out of the arena it stops its
audio and destroys itself. A strong hit sends it into a recoil that also throws items; once
enough damage has landed, one throw is a hardcoded All Up rather than a table draw. It never
searches for a player: whoever is underneath gets rumbled, hit by its body or showered with
items.

## Meteor (0x4E)

The meteor falls out of the sky and damages whatever it lands on. What makes it unusual is that
its fall parameters are not in its descriptor but in two globals owned by the meteor event,
which `event_meteor_start` (0x80110b74) sets up: `stc_meteor_data` (r13+0x650, a 0x218- byte
state block, NULL outside the event) and `stc_meteor_event_data` (r13+0x654, the event's data
block). The event then creates meteors at random positions inside its zones.

The spawner packs the fall into the descriptor's `variant`, which lands in
`EnemyData.tier_flags`:

| Bits | Field | Resolves through |
|---|---|---|
| 0-7 | zone index | `stc_meteor_event_data->+0x0C`, stride 12: height offset, fall speed, angle |
| 8-15 | speed index | `stc_meteor_event_data->+0x04`, stride 8: overrides the angle |
| 22-31 | approach angle | degrees from vertical; 0 is straight down |

The descriptor (0x804b4310) has four states, 14-17:

| State | Role |
|---|---|
| 14 | Entry. Its `anim_cb` (0x8021e15c) calls `Meteor_BehaviorInit` and leaves at once |
| 15 | Falling. Plain physics - `Meteor_BehaviorInit` set `vel` and the priority-4 proc integrates it. `Meteor_State15_BoundsAndHit` (0x8021e3f8) waits for the meteor to enter the play area, then checks for contact |
| 16 | Impact, entered by `Meteor_HitTransition` (0x8021e7c4): impact effect, damage sphere, audio fade |
| 17 | Landing, entered by `Meteor_Landing` (0x8021ea5c): landing effect and damage sphere; once the effect ends it frees its spheres and effect groups and destroys itself |

`Meteor_BehaviorInit` (0x8021e1a0) is the only reader of the event globals. It bails when either
is NULL; otherwise it zeroes velocity, enters state 15, looks up the zone and speed entries from
`tier_flags`, and either drops the meteor straight down (`pos.Y = initial_pos.Y + zone offset`,
`vel.Y = -speed`) or tilts its basis by the approach angle and spreads the speed across all
three axes.

Creation leaves the meteor hidden three independent ways, and showing it means clearing all
three:

- **`render_flags` bit 4** (`ed+0xB08`), set by `EventActorGObj_DisableRendering` (0x802041b0),
  cleared by `EventActorGObj_EnableRendering` (0x80204198).
- **`render_flags` bit 7**, set by `EventActor_Hide` (0x801fed40). `EventActor_SetVisibility`
  clears it but disables rendering again for this kind, so clear the bit by hand and call
  `EventActorGObj_EnableRendering`.
- **`JOBJ_HIDDEN`** on the model tree, set by `EventActorGObj_HideModel` (0x802042fc) in the
  init callback; clear it with `JObj_ClearFlagsAll(root, JOBJ_HIDDEN)` on the GObj's root JObj.

### Dropping one outside the event

A meteor can be spawned anywhere by standing in for the event globals just long enough for its
init to read them. Build the descriptor with the standalone values (`spawn_index` and
`spawn_slot` -1, `leash_radius` -1.0), then, in one straight stretch of code:

1. Save `*stc_meteor_data` and `*stc_meteor_event_data`.
2. Write a non-zero value to `*stc_meteor_data` and point `*stc_meteor_event_data` at a fake
   event block laid out like the two tables above (one zone with the wanted fall speed, angles
   0).
3. `EventActor_Create`, which leaves the meteor in state 14.
4. Call `Meteor_BehaviorInit` on it directly, putting it in state 15 with its velocity set.
5. Restore both globals.
6. Clear the three hidden flags.

The call in step 4 cannot wait for state 14's own `anim_cb`: that runs on the next frame, after
the real globals are back. The restore matters because during a live meteor event the real
globals point at event data. To land on a moving rider, lead the position by its velocity times
the fall time. The vanilla chain then destroys the meteor the same way it destroys an event one,
so no cleanup is needed.
