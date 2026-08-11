# Hypernova

A City Trial power-up (`mods/hypernova/`) named after Kirby: Triple Deluxe's super-inhale.
While active a rider grows to 2x size and cycles through a rainbow tint, and holding the
trigger turns the inhale into a wide cone vacuum that pulls in items, breakable yakumono, and
unridden machines. Activation is per player through the exported `HypernovaAPI`, the Miracle
Fruit custom item, or the debug self-test toggle.

## Game System

Everything the vacuum touches lives in City Trial, so the mod is CT-only (`InCityTrialGameplay`
requires major `MJRKIND_CITY`, minor `MNRKIND_3D`, and `Gm_GetIntroState() == GMINTRO_END`).
Air Ride enemies and Top Ride's separate C++ item system are out of scope.

Targets are **claimed** the moment the cone sweeps them and then pulled in every frame
thereafter - even after they leave the cone or the trigger is released - through one shared
approach step, so a swept target is never stranded and all three target classes pull at the
same rate. What happens on arrival differs:

| Target | On arrival |
|---|---|
| Items | Pulled all the way into the rider so the **vanilla pickup trigger** fires (proper credit, SFX, effects). |
| Yakumono | Shrunk once close, then **broken via a synthesized collision** through the family's own `coll_func`. |
| Machines (unridden only) | **KO'd** via `Machine_OnKO`, running the machine's own vanilla break/explosion. |

Both items and props also **arc up and over** (the pull aims at a point lifted above the rider
by a hump of horizontal distance, so a swept target never scrapes or clips the ground) and
**tumble** end-over-end with a per-target random spin direction.

## Activation and State Model

Hypernova is a **timed power-up state**, tracked per player (`stc_active[5]` / `stc_timer[5]`,
with the model-scale ease and inhale phase also per player), so it can be granted to one rider
or to everyone at once:

- The exported `HypernovaAPI` offers `ActivatePlayer(player, duration)` for a single slot and
  `Activate(duration)` for every human at once, plus `Deactivate`, `IsActive`, and
  `FramesRemaining`.
- The **Miracle Fruit** custom item grants it to its collector: the mod's boot (and every scene
  change, so mod load order does not matter) registers a `custom_items` pickup handler that
  matches the item name and calls `ActivatePlayer` for the player who picked it up.
- `duration <= 0` means "use the menu setting": `hypernova_duration_table` is
  `{300, 600, 1200}` frames (Short / Medium / Long at 60 fps), selected by
  `hypernova_duration_sel`.
- Only human slots (`PKIND_HMN`) can be activated.

Each frame while `timer > 0` (`Hypernova_OnFrameEnd`, which runs after the frame's game procs
so the vacuum's position overrides win over item physics and ground-snap):

1. Expire or cancel: decrement the timer, and end the player immediately if they now hold a
   copy ability or power-up.
2. Advance the shared rainbow hue and ease each player's `model_scale`.
3. Drive the inhale gesture; when it reports an active suck, run that player's cone scan.
4. Advance every claimed item / prop / machine, then recolor the live whirlwinds.

The whole tick is skipped while `Gm_CheckPauseKind(PAUSEKIND_GAME)` is set, because everything
it cooperates with (the model-scale applier, the ColAnim selector, effect models) is frozen
too. The debug cone is installed before that early-out so it still renders while paused.

The trigger button is **B** (`HYPERNOVA_TRIGGER_BUTTON`), not `A` - `A` is the boost/charge
button and would conflict with normal machine control. The trigger only counts while
`Rider_IsOnMachine(rd)`: the inhale action-state procs dereference the rider's machine, so
being off-machine reads as a release (it ends any running suck and skips the vacuum).

Leaving City Trial or changing scene calls `ResetState`, which snaps every player to neutral
scale (models are recreated at 1.0 anyway), drops all claims (`Hypernova_VacuumReset`), and
forgets the debug-cone GObj.

When one player's Hypernova ends, `Hypernova_VacuumFinishClaimedPlayer` breaks that player's
in-flight props (restoring collision on any that will not break), releases their item claims
back to vanilla physics, and drops their machine claims. Other players' claims stay in flight.

### Menu

Six options under "Hypernova" (`main.c`): **Enabled** (default on), **Duration**
(Short/Medium/Long, default Medium), **Suck Yakumono** (default on), **Suck Machines** (default
on), **D Pad self test** (hold D-Pad Up in CT to activate every human; default off), and
**Debug Cone** (default off).

### Copy Abilities and Power-Ups End Hypernova

Hypernova reuses the vanilla inhale action-states for presentation, and the inhale is the
engine's **no-copy-ability default attack** (`Rider_CanStartInhale` requires
`copy_kind == -1`). So Hypernova is mutually exclusive with the rider's copy ability
(`copy_kind` `+0x454`) and City Trial power-up (`powerup_kind` `+0x45c`: firecracker, Sensor
Bomb, Gordo, Panic Spin) - exactly as those two are mutually exclusive with each other
(`Rider_GivePowerUpByKind` `0x801a8304` calls `Rider_AbilityRemoveModel` before granting). The
vanilla "last pickup wins" rule is applied to Hypernova both ways:

- **Activating** Hypernova strips whatever the rider was holding: `Hypernova_ActivatePlayer`
  calls `Rider_AbilityRemoveModel` (`0x80191554`) - which clears **both** `copy_kind` and
  `powerup_kind` by invoking the held kind's installed remove callback (the copy-ability
  teardown clears `+0x454`; the power-up remove callback, e.g. the firecracker's `0x801b5dec`,
  clears `+0x45c`) - then `Rider_LoseAbilityState_Enter` (`0x801b0adc`) for the spit-out to
  neutral. Without this strip the guard below would read the pre-existing state and cancel
  Hypernova on its first frame.
- **While active**, the frame the rider *gains* a copy ability or power-up (`PlayerHoldsAbility`:
  `copy_kind != COPYKIND_NONE || powerup_kind != POWERUPKIND_NONE`), `Hypernova_OnFrameEnd`
  ends Hypernova for that player. This runs **before `DriveInhale`** - the grant has already
  moved the rider into the ability/power-up action-state during the frame's game procs, so
  ending here stops the inhale drive from stomping the new state on the next frame. The giant +
  rainbow + vacuum tear down cleanly while the new ability takes over.

This is why the vacuum can suck up copy-ability and power-up items freely: collecting one ends
Hypernova and hands the rider that state, rather than the two fighting over the rider's
action-state and animation.

## Kirby 2x Scale

The rider model matrix is rebuilt every frame by `Rider_ApplyModelMatrix` (`0x80190848`, via
`Rider_ModelMatrixThink` `0x8018f79c`, GObj proc priority 6) as:

```c
gmLanMenu_Scale3DObject(base_scale(+0x2c8) * model_scale(+0x348),
                        model_jobj, forward(+0x324), up(+0x330), pos(+0x300));
```

Writing `RiderData.model_scale` (`+0x348`) is the entire mechanism - no JObj poking; the engine
consumes it every frame, and it resets to 1.0 on scene change because models are recreated.
`TickScale` (`hypernova.c`) smoothsteps `model_scale` from `HYPERNOVA_SCALE_NEUTRAL` (1.0)
toward `HYPERNOVA_SCALE_TARGET` (2.0) over `HYPERNOVA_SCALE_ANIM_FRAMES` (30) on activate and
back on deactivate, writing the field directly each frame (a direct set, not the AP "Big Kirby"
filler's multiplicative path). A settled, inactive player's `model_scale` is left alone
entirely so the mod never fights other scale writers.

**Do not also bump `base_scale` (`+0x2c8`) for the 2x look** - the model uses the *product*, so
raising both compounds to 4x. `+0x2c8` is also the vanilla inhale's range knob
(`HurtVolume_OverlapTest` scales the mouth sphere by it), but that sphere is only tested against
EventActor candidates, of which City Trial has none, so widening it buys nothing. The vacuum's
reach is an independent mod constant.

## The Vacuum Pipeline

`Hypernova_VacuumPlayer` runs for a player on the frames an inhale is actually being driven,
and only *claims* targets; the three `Hypernova_VacuumProcessClaimed*` passes move them. Claims
therefore keep flying in regardless of the drive phase - only *new* claims require an active
suck.

### Cone scan

Origin and aim come from the rider: `Ply_GetRiderGObj(player_idx)` (`0x8022cb74`) gives the
rider GObj, `RiderData = rider_gobj+0x2c`, `origin = RiderData.pos` (`+0x300`),
`aim = normalize(RiderData.forward)` (`+0x324`). A facing shorter than `0.01` is unusable and
the whole scan is skipped that frame.

`Hypernova_InCone` is squared-magnitude throughout (no sqrt): a candidate at `p` is in when
`|p - origin|^2 <= RANGE^2` and `dot(p - origin, aim) >= 0` and
`dot^2 >= HALF_ANGLE_COS^2 * dist^2`. A candidate essentially on top of the rider
(`dist^2 < 0.0001`) is always in.

The shipped reach is `HYPERNOVA_RANGE` 175 world units with a 30-degree half-angle
(`HYPERNOVA_HALF_ANGLE_COS` = 0.8660254). This is the "greater range", fully decoupled from the
vanilla `+0x2c8` volume.

### The shared pull step

`Hypernova_StepToward` moves one position toward the rider by
`max(HYPERNOVA_PULL_SPEED * dist, HYPERNOVA_PULL_MIN)` = `max(0.030 * dist, 2.0)` world units
per frame (compared squared, so the only sqrt is one `VECNormalize` on the final approach; the
`MIN` floor outruns a fleeing rider). It does not aim at the rider directly - it aims at a
point lifted above the rider by a parabolic hump of the **horizontal** distance
(`HYPERNOVA_ARC_HEIGHT * 4u(1-u)`, `u = hdist^2 / RANGE^2`, clamped to 1), so a swept target
rises up and over instead of scraping the terrain. The hump is keyed to horizontal distance and
vanishes at both ends, so the lift falls to zero *at* the rider: the target always lands ON it
for pickup/break and never hovers.

Independently, each in-flight target **tumbles**: it rotates about an axis perpendicular to its
travel direction (`travel x world-up`, falling back to world X when travel is near-vertical) by
`HYPERNOVA_SPIN_RATE` (0.08) rad/frame, with a per-target sign hashed (xor-folded, so heap
alignment cannot bias it) from its pointer. Items spin by rotating their `forward`/`up` vectors,
which `CityItem_UpdatePosition` rebuilds the render matrix from; props spin by rotating the 3x3
basis columns of their world matrix. Rotation is length-preserving, so it never disturbs the
prop shrink (which keys off row magnitude). Both use `Vec3_RotateAboutUnitAxis` (`0x800638f8`).
Machines are not spun.

### Items: claim -> pull -> collect

`Hypernova_ClaimItems` walks the item p_link bucket (cleaner than the global GObj list - it
skips everything that is not an item):

```c
for (GOBJ *g = ((GOBJ**)0x805de334)[13]; g; g = *(GOBJ**)(g + 0x8)) { /* item */ }
```

`GAMEPLINK_ITEM` is 13; a candidate must have `entity_class == 22` and `ItemData = g+0x2c`.
**Boxes are skipped** by `item_category` (`+0x24`) `== 0` (0 = box, non-0 = power-up), which
also covers `kind` (`+0x1c`) `<= ITKIND_BOXRED`. A live-item count mirror sits at
`*(int*)0x805dd8cc` (informational; not used). Position is `ItemData.pos` (`+0xdc`) - the field
the matrix proc draws from (`CityItem_UpdatePosition` `0x802503c0`, priority-6 proc
`0x8024f848`), *not* the land_pos at `+0x1ac`.

Claims (up to `HYPERNOVA_MAX_ITEM_CLAIMS`, 128) record the `ItemData` pointer plus its owner
slot. `Hypernova_VacuumProcessClaimedItems` re-validates each claim by walking the bucket for
the pointer before dereferencing it, so a collected/despawned item (or a reused pointer)
self-heals out of the set; a claim whose owner's rider GObj is gone is dropped too.

Per frame, `Hypernova_PullItem` steps `pos` (`+0xdc`) with the shared step, spins the item, and
neutralizes the physics that would fight it:

- zero `vel` (`+0xc4`) - `CityItem_PhysicsThink` (`0x8024f778`) integrates `pos += vel` every
  frame;
- set `is_airborne` (`+0x1d4`) to `-1` so `Item_GenericEnvColl` (`0x80255438`) skips the
  per-frame ground raycast/snap, and clear the grounded flag (`+0x35a` bit 4).

Collection is left to the engine: the item is pulled all the way into the rider/machine so the
**vanilla pickup trigger** fires naturally (item `TriggerData` at `+0x250` against the machine),
giving proper credit, pickup SFX, and effects. A raw `GObj_Destroy` would vacuum the item away
**without** crediting it.

### Yakumono: claim -> pull/shrink -> break

CT breakables are **multi-instance**: one `YakumonoData` GObj manages N placed props, and the
parent itself has no usable transform - `pos` (`+0x1c`), `hsd_object` (`+0x28`), `model_jobj`
(`+0x64`), and `xform_jobj` (`+0x70`) are all NULL/zero (`GrYaku_Create` NULLs `+0x70` at
creation). The visible geometry lives in the **ground scene-instance pool**: each placed prop is
a `GrCollRecord` (`Gr_GetCollRecords`, which returns `(*stc_grobj)->coll.record` and its count)
carrying its own `jobj` (world matrix `JOBJ.rotMtx`) and a `yaku_gobj` back-pointer to its owning
parent GObj. The vacuum therefore moves the per-prop **records**, not the GObj, sidestepping
the per-family `+0x130` layout differences. `on_damage` (`+0x100`) is NULL for these families, so
the break is collision-force driven, not damage-driven.

**Skeleton-joint families.** The static families (houses 38, walls 36, holes 37, floor 32,
BigStar 29) have plain JObjs (flags `0x40008` at `JObj+0x14`) whose world matrix can be written
directly. The **weak** families (trees 34, rocks 35, coral 33) are `JOBJ_SKELETON` joints (flags
`0x9`): `HSD_JObjSetupMatrixSub` (`0x8040d6b4`) rebuilds their `JObj+0x44` from the joint SRT
every frame, clobbering a plain write. `Hypernova_PullInstance` sets `JOBJ_USER_DEFINED_MTX`
(`0x800000`) on each joint first, which makes that setup keep our matrix (idempotent for the
static families).

**Enumeration** is two steps: collect the breakable *parent* GObjs from the yakumono p_link
bucket (`(*stc_gobj_lookup)[8]`, `gobj->entity_class == 15`, `YakumonoData = g+0x2c`, breakable
`desc_id`, up to 32 parents); then scan the scene-instance pool and keep each record whose owner
(`record+0x90`) is one of those parents. Matching by pointer means a non-break record's `+0x90`
is never trusted.

**How props break (collision force vs HP).** `collideWithObject` (`0x800f5004`) reads the prop's
`desc_id` (`+0x04`), indexes the 70-entry descriptor table at `0x804a5be8`, and calls that
descriptor's **`coll_func` at `+0x04`**: `coll_func(yaku_gobj, otherCollData, ..., regionIdx,
...)`. The handler (`hitWeakObject` `0x80107914` / `hitStrongObject` `0x801086d0` /
`hitBreakableFloor` `0x80106bd0` / `hitBigStar` `0x80103eb8`) computes a **force** =
`otherCollData.radius (CollData+0x344) * impactSpeed^2` and breaks when `force > HP` (weak and
BigStar: one-shot threshold; strong: subtractive per-region HP; floor: one crack-stage per hit).
There is no "deal N damage" entry that bypasses a collider, and seeding `HurtData` + calling
`GrYakumono_Proc10` does nothing because `on_damage` is NULL for these families (it only
accumulates damage into `+0xac`).

**`impactSpeed` is a normal projection, not `|delta|`.** `grScene_GetImpactSpeed` (`0x800d8edc`)
takes the collider's frame delta (`CollData+0x14`), projects it onto the contacted region's
outward normal (`region+0x0c`), **negates** the projection (scale const `0x805df634` = -1.0),
and clamps a non-positive result to **0** (the tail at `0x800d9150`). So an impacting body's
delta must point **into** the surface; a delta whose dot with the outward normal is `>= 0` reads
as zero impact, giving `force = radius*0 - HP < 0` and no break.

**The regions cannot be moved, so they are retired.** The engine break needs a *real* collision:
the rider's `mpColl_UpdateCollision` (`0x802485e0`) queries its coll sphere against the **baked
static map-collision mesh**, and its hit dispatchers (`mpResponse_DispatchSceneObjColl`
`0x80248bb4`, `mpColl_SphereSceneObjColl` `0x8024d414`) call `collideWithObject` per overlapping
scene region. A prop's mpColl regions are baked at its origin and are **not** repositioned when
its render matrix moves, and they cannot be relocated either: they are a fixed contiguous slice
of the **global region array** (base at `*(stc_grobj+0x5c)`, exposed by the ground
scene-collision holder `stc_grobj+0x54` at its `+0x08`), found by spatial broadphase. So a moved
prop's collision is **retired** instead (`region+0x3c` bit 6 cleared via
`grScene_SetInstanceColl(record, 0)`): the rider response drops any contact whose bit 6 is
clear, and nothing re-arms it per frame, so the clear sticks and no invisible wall is left at
the origin.

**Claiming.** `Hypernova_ClaimYakumono` claims an in-cone record (position read from its JObj
world matrix) if it is not already claimed and still fully collidable
(`grScene_IsInstanceCollAll(record, 1)`), and retires its collision at *claim* time so the
player cannot run into a swept-up prop mid-flight. The claim cap is
`HYPERNOVA_MAX_CLAIMS` (200), sized above CT's ~130 breakables so a wide cone cannot starve
later props of a slot.

**Per-frame advance.** `Hypernova_PullInstance`:

- Re-retires the prop's collision every frame of the flight.
- Sets `JOBJ_USER_DEFINED_MTX` and pulls the world-matrix translation with the shared step.
- **Shrinks** the world-matrix 3x3 by `HYPERNOVA_YAKU_SHRINK` (0.70) per frame **only once
  within `HYPERNOVA_YAKU_SHRINK_RADIUS`** (22.0). The cached `record+0x2c` 3x3 keeps the
  load-time scale as a sqrt-free reference (squared row-0 magnitude).
- **Breaks** when the prop arrives (within `HYPERNOVA_YAKU_BREAK_RADIUS`, 8.0) **or** has shrunk
  past `HYPERNOVA_YAKU_BREAK_SCALE` (0.20) of its original size.

`HYPERNOVA_YAKU_CLAIM_TTL` (300 frames) force-releases any claim that never breaks, re-arming
its collision rather than gluing the prop to the rider.

**The synthesized break.** `Hypernova_BreakInstanceNative` re-arms the collision
(`grScene_SetInstanceColl(record, 1)`, so the family tail's "still collidable?" guard passes)
and invokes `collideWithObject(yaku_gobj, coll, holder, region_idx, contact)` with a fabricated
collider:

- **`holder`** = the ground scene-collision holder `stc_grobj+0x54` (`+0x08` = the global
  region-array base, `+0x10`/`+0x14` = the record pool/count).
- **`region_idx`** = the global index of the prop's **first region with a usable
  (non-degenerate) normal** = `(record+0x0c - *(holder+0x08)) / 0x40 + k` (regions are a
  contiguous 0x40-strided slice of the global array; count at `record+0x10`). A prop with no
  usable normal is skipped.
- **`coll`** = a zeroed `CollData`. `radius` (`+0x344`) is `HYPERNOVA_BREAK_FORCE_RADIUS`
  (`1.0e9`) so `force = radius * impactSpeed^2` far exceeds any prop HP (one-hit break).
  `pos_delta` (`+0x14`) is `-normalize(region_normal) * HYPERNOVA_BREAK_FORCE_DELTA` (100) so it
  projects to a *positive* impact speed. `g` (`+0x04`) is the human rider GObj so
  `GrYakuBreak_GetAttackerPly` (`0x80105cb0`) credits the right player; `coll_info` (`+0x44`) is
  a zeroed `mpCollInfo` with `+0x1d0 = -1` ("no BigStar region") so `destroyBigStar` returns 0
  and the break proceeds.
- The target region's `+0x34` bit `0x20` is temporarily cleared so `grScene_GetImpactSpeed` skips
  its geometry-refined path (which can rewrite a synthetic delta from the prop's matrices), and
  restored after the call.
- The contact point passed in is the prop's current (pulled-in) world position.

Break success is detected directly: if `grScene_IsInstanceCollAll(record, 1)` is false after the
call, the family tail fired and the claim is dropped; otherwise the prop is left re-retired and
the flight continues.

**Two visible break paths.** `collideWithObject` dispatches to the prop's family `coll_func`:

- **`hitStrongObject`** (walls 36 / holes 37 / houses 38) does the whole visible break
  **inline** at the passed contact point - retires collision, hides the mesh
  (`HSD_JObjSetFlagsAll`, gated on its `hp_block[9]`), spawns debris, and calls the per-desc
  **item-drop** handler from the `DAT_804a70b4` table (`GrYakuBreak*_DropItems`). A pulled-in
  house therefore breaks correctly at the rider with nothing left behind.
- **`hitWeakObject`** (coral 33 / trees 34 / rocks 35) does **not** hide the original mesh
  inline (its `HSD_JObjSetFlagsAll` branch is gated on `hp_block[5]`, which is 0 for these), and
  its broken **state** (`GrYakuBreakColl_BrokenProc` `0x80107798`) is teardown only (waits for
  every prop in the family to break, then `GObj_Destroy`s the parent). So the *entire* visible
  break for a weak prop is its **debris effects** (`GrYaku_SpawnBreakEffect` ->
  `Effect_SpawnSync`, one per sub-part), each positioned by `Gr_GetNodeWorldPos`, which resolves
  a **node id** (`entry+0x08`, per-instance) through grobj's node registry to a JObj and reads
  *that* JObj's world translation - a separate node from the dragged instance JObj, pinned at
  the prop's baked spot. The weak path also drops no items; only the strong path runs the
  `DAT_804a70b4` drop table.

In every case the tail calls `grScene_SetInstanceColl(record, 0)`,
`GrYaku_IncrementBreakCount` credits the checklist, and the family's `hp_block[remaining]`
counter (`+0x134` weak / `+0x140` strong, equal to the live intact-record count) decrements. The
multi-stage floor (desc 32) advances one crack-stage per call, so it may take a few frames in
the break zone to fully open.

**Weak-family rubble.** Because the weak rubble is debris pinned to a separate grobj node,
`Hypernova_BreakInstanceNative` does two extra things when
`Yaku_GetDescCollFunc(desc_id) == hitWeakObject` (`Hypernova_IsWeakBreakFamily`):

1. **Relocates the debris-anchor node onto the rider for the break instant.**
   `Hypernova_WeakDebrisNode` walks the same chain `hitWeakObject` uses - family break data
   (`YakumonoData->data_ptr`) -> per-instance entry table (stride `0x10`) -> node id at
   `entry+0x08`, resolved through grobj's node registry - matching the instance by its record
   pointer in the family's per-prop record array (`YakumonoData+0x130`). It sets the node's
   world-matrix translation to the contact point and sets `JOBJ_USER_DEFINED_MTX` so
   `Gr_GetNodeWorldPos` reads the written translation instead of rebuilding it from the baked
   SRT. The effects spawn **synchronously** inside `collideWithObject`, so they capture the
   relocated position; the node's matrix and flags are saved and **restored** immediately after
   (the node may be shared across the family's instances). A resolved node is sanity-checked as
   a 4-aligned MEM1 pointer before anything is written through it.
2. **Collapses the dragged intact mesh after the break** by clearing `JOBJ_USER_DEFINED_MTX` on
   the instance JObj, dropping it to its degenerate local SRT so it does not linger frozen at
   the rider.

Strong families need neither step - they shatter and hide inline at the contact.

**Targeting.** `Hypernova_IsBreakableYaku` admits desc ids 29, 32, 33, 34, 35, 36, 37, 38: star
pole (29), forest pitfall (32), coral (33), trees (34), rocks (35), volcano rock walls (36),
volcano-base holes (37), dilapidated houses (38). Passive zones (17/18) and the large structures
(Lighthouse 68, WhispyWoods 69) are not targets. The full identity table is in
`yakumono-system.md`.

### Machines: claim -> pull -> KO

City Trial litters the map with loose machines (stars/bikes). The vacuum claims the ones
**nobody is riding** and pulls them into the rider, then **KOs each on arrival** so it plays its
own vanilla break/explosion - the same destruction a machine shows when its HP is depleted in a
brawl. This is behind the "Suck Machines" menu toggle (default on).

`Hypernova_ClaimMachines` walks the machine p_link bucket (`(*stc_gobj_lookup)[9]`,
`gobj->entity_class == GAMEENTITY_MACHINE` (16), `MachineData = gobj->userdata`). A machine is a
target only while:

- **unridden** - `MachineData.rider_gobj` (`+0x4`) is NULL, which naturally excludes **both**
  human and CPU riders, so only parked machines are swept;
- **not already dying** - `is_dead` (`+0xc35` bit 0x20) and `is_fall_dead` (`+0xc35` bit 0x80)
  clear.

Claims (cap `HYPERNOVA_MAX_MACHINE_CLAIMS`, 32) are keyed by the `MachineData` pointer and
re-validated against the live bucket every frame, so a machine that gets mounted, despawns, or
dies self-heals out of the claim set and a dangling pointer is never dereferenced.

**Pull.** `Hypernova_PullMachine` writes `MachineData.pos` (`+0x3e8`) with the shared step.
`Machine_PhysicsThink` (`0x801c6368`, proc priority 4) integrates `pos += accel + velocity +
...` every frame, so the pull zeroes `accel` (`+0x318`) and `velocity` (`+0x324`) each frame to
keep the position override from being fought. The write lands in `OnFrameEnd`, after the
machine's procs, and is picked up by the next frame's `Machine_ApplyModelMatrix` (priority 6).
Machines are not shrunk - a full-size machine erupting on a 2x Kirby reads better - and their
break radius (`HYPERNOVA_MACHINE_BREAK_RADIUS`, 45.0) is wider than the yakumono one so the
machine detonates before its model clips into the rider.

**KO.** On arrival, `Hypernova_KOMachine` arms the break gate (`MachineData[0x78] |= 0x40`) and
calls **`Machine_OnKO`** (`0x801e568c`). That captures the rider ply into `+0x1b48` (sentinel
**5** when unridden), sets `is_dead`, disables the machine's hit-collision, and enters the
**BreakDown** state (29); the BreakDown proc (`Machine_KOExplode` `0x801e5838`) runs the
explosion VFX (`Effect_SpawnSync` 0x2799 + a debris effect), plays the break SFX, and
`GObj_Destroy`s the machine. That tail is gated on `+0x78` bit 0x40, which is why Hypernova ORs
it in first. The whole destroy tail is **rider-safe**: every rider dereference guards on the
`+0x1b48 == 5` sentinel, so an unridden machine spawns the VFX and frees cleanly with no rider
eject and no out-of-range player index. The claim is dropped the instant `Machine_OnKO` is
called - the machine tears itself down from there.

A dropped machine claim (Hypernova ending, scene change) needs nothing restored: the pull only
zeroed velocity, which vanilla physics rebuilds, so the machine resumes sitting where it is.

`Machine_OnKO` is also fired automatically each frame by `Machine_CheckKO'd` (`0x801e5628`,
from the priority-10 `Machine_DmgApply` proc) whenever `MachineData.hp` (`+0xa18`) reaches 0 and
`Gm_IsDamageEnabled()`. Hypernova calls it directly instead, which needs neither HP at 0 nor
`Gm_IsDamageEnabled`.

## Inhale Presentation

Suction and presentation are two separate layers. The vanilla **suction** cannot be reused for
items or yakumono, but the inhale's **presentation** layer (animation + suction VFX + SFX) is
reused so the custom vacuum looks like a real Kirby super-inhale: vanilla visual, custom
suction.

### Why the suction is custom

The native inhale is a five-stage pipeline and *every* stage is hard-wired to EventActor
enemies:

1. **The scan only walks the EventActor bucket.** Both the entry probe `Rider_TryStartInhale`
   (`0x8019c5ac`) and the per-frame `Rider_InhaleCaptureScan` (`0x8019c63c`) iterate exactly one
   GObj list: `*(stc_gobj_lookup + 0x30)`, the EventActor bucket (p_link 12). Items (p_link 13)
   and yakumono (p_link 8) are never visited.
2. **The predicate admits EventActors only.** `EventActor_IsInhalable` (`0x802041c8`) rejects
   GObj classes `0xE` / `0x26` / `0x3E` (rider/player/projectile) and then requires the
   candidate to pass the EventActor test (`zz_802041fc_`).
3. **The overlap test reads an EventActor volume.** `HurtVolume_OverlapTest` (`0x80189784`)
   compares the rider's mouth volume (`RiderData+0x828`) against the candidate's volume at
   `EnemyData+0x45c`, which items/yakumono do not have.
4. **Capture is EventActor-specific.** `EventActor_OnCapture` (`0x802038c4`) puts the actor into
   the captured state and writes EnemyData fields.
5. **The captured driver reads/writes EnemyData.** `EnemyState_InhaledFunc4` (`0x80203b64`,
   mouth-follow + shrink + destroy past 120 frames) and `EnemyState_AnimTick` (`0x8020be1c`)
   read `attraction_mode +0xa10`, inhale scale `+0xb10`, etc. Feeding an `ItemData` /
   `YakumonoData` pointer here reads the wrong struct.

And City Trial has nothing for it to inhale anyway: CT spawns no per-type AI enemies, only event
actors (Dyna Blade / TAC / Meteor) and CPU players, none flagged inhalable. Since
`Rider_TryStartInhale` only enters the inhale state *when the overlap test already found a
target in range*, the vanilla inhale never starts on its own in CT - which is also why the mod
must call `Rider_StartInhale` directly for the visual.

### Native inhale reference

- **The inhale is the no-copy-ability default attack.** Gate `Rider_CanStartInhale`
  (`0x801a617c`):
  ```c
  attack_held   = (*(u8*)(rd + 0x820) >> 2) & 1;   // RiderData+0x820 bit 2 (0x04)
  no_ability    = *(int*)(rd + 0x454) == -1;        // copy-ability kind == -1
  mouth_open    = *(int*)(rd + 0x918) < 3;          // Rider_IsInhaleMouthFull: capture count < 3
  ```
  The gate is checked only by the *entry probe*; `Rider_StartInhale` itself does not check it,
  so a forced power-up can drive the visual even when Kirby holds an ability. The attack bit is
  also **transient** - set only on the single frame the attack input registers - so the gate
  reads false on nearly every frame and is unusable as a "can I inhale now" test for a
  continuous drive.
- **Force lever: `Rider_StartInhale` (`0x801ad2c4`).** No gate, no target needed. It plays the
  suck-START anim `0x2f` (action-state `0x76`), spawns the native suction **particle VFX**
  (`Effect_SpawnSync(..., 0x3a982, ...)`) anchored to the mouth bone, plays the inhale **SFX**
  (`0x20037`), and installs the per-frame capture callbacks (which, in CT, harmlessly scan the
  empty EventActor bucket and capture nothing).
- **The inhale is three action-states that do NOT chain automatically.** `state_idx`
  (`RiderData+0x1c`, the anim id; the parallel action-state at `+0x28` runs `0x76`/`0x77`/`0x78`):
  `0x2f` suck START, `0x30` suck LOOP, `0x31` suck END.
  - **START (`0x2f`) is a one-shot gulp.** Its process `Rider_InhaleStartProc` (`0x801ad1dc`)
    holds the anim while it plays, then on `Rider_IsBodyAnimDone` (`0x80198b00`) hands off to
    the generic ability-resolve / star-wait path (`zz_801a8454_` `0x801a8454`), returning the
    rider to **neutral**. It does **not** advance to the LOOP.
  - **LOOP (`0x30`) is entered ONLY via `Rider_StartInhaleLoop` (`0x801ad4cc`).** Its process
    `Rider_InhaleLoopProc` (`0x801ad3a0`) re-enters the loop itself every time the body anim
    finishes, so the suck sustains and animates on its own. It self-terminates via a countdown
    at `RiderData+0x93C` (per-action-state scratch, **aliases `copy_wheel_result`**):
    `Rider_InhaleLoopTick` (`0x801ad550`) decrements it each LOOP frame while the mouth is empty
    and, at `0`, calls `Rider_EndInhale` (`0x801adf98`).
  - **END (`0x31`)** closes the mouth and spawns a close puff (`Effect 0x5a557`), then neutral.

  Because the engine never advances START -> LOOP, a driven hold must call
  `Rider_StartInhaleLoop` to enter the loop after the gulp, top up `+0x93C` to keep the engine
  from timing it out, and call `Rider_EndInhale` on release. `+0x93C` is not a trustworthy timer
  when driven (other systems write `copy_wheel_result`), so the end is driven explicitly.
- **Per-frame callback slots** (installed by `Rider_StartInhale`, called by the native state
  machine while the inhale state is live): volume-update at `RiderData+0x7d0` (`[500]`, default
  `zz_801ad46c_`), scan at `+0x7d4` (`[0x1f5]`, default `zz_801ad48c_` ->
  `Rider_InhaleScanThink` `0x801adc78`, which calls `Rider_InhaleCaptureScan` while the capture
  state `RiderData+0x918 == 0`), and exit/cleanup at `+0x7e4` / `+0x7f8` (defaults
  `zz_801add18_` / `zz_801add38_`).
- **Scan / capture chain**: entry probe `Rider_TryStartInhale` (`0x8019c5ac`); per-frame
  multi-capture `Rider_InhaleCaptureScan` (`0x8019c63c`, up to 3/frame, candidate list capped at
  10) -> `EventActor_OnCapture` (`0x802038c4`) -> captured driver `EnemyState_InhaledFunc4`
  (`0x80203b64`). Held captures live in `RiderData+0x8f0/+0x8f4/+0x8f8` (treated as `EnemyData`).
- **Mouth volume.** `RiderData+0x828` is a HurtData sphere/capsule re-anchored to a mouth bone
  each frame by `Trigger_UpdatePosition` (`0x8018a188`) from `zz_801a5f2c_` (`0x801a5f2c`) - not
  a cone. `HurtVolume_OverlapTest` sets its effective radius to
  `base_radius(vol+0x18) * (+0x2c8)`.
- **Enemy.dat attraction params** (table at `*0x805dd878`, loaded by `Enemy_LoadCommonParams`
  `0x801fd580`): `+0x88` capture-proximity, `+0x8c` attraction range, `+0x7c` pull timing.
  Reference only - the custom pull uses its own tunables.

### Driving the gesture

`DriveInhale` (`hypernova.c`) owns the whole IDLE -> GULP -> LOOP gesture per player and runs
every frame Hypernova is active. Tapping the trigger plays the full vanilla gulp; holding it
sustains an open-mouth suck loop (with native particle VFX + SFX and the cone vacuum running
underneath); releasing lets it end like a vanilla inhale.

It drives the inhale from whatever riding state the rider is in. Kirby's neutral riding is
**not** a single action-state - `state_idx` cycles through a wide cluster of lean/turn/idle
riding states (e.g. `0x21`-`0x2a`, `0x6a`), and vanilla lets you inhale from all of them - so
the start is not gated on any one state, and GULP is promoted to LOOP by detecting that the gulp
has simply **left its START state**.

```c
// each frame, per human, while Hypernova active; returns "inhaling":
if (!held) {
  // released: end an active suck with the engine's own END; let a mere tap finish its gulp
  if (phase[p] == LOOP && rd->state_idx == 0x30) Rider_EndInhale(rd);
  phase[p] = IDLE; return 0;
}
switch (phase[p]) {
  case IDLE: Rider_StartInhale(rd);               // gulp + VFX + SFX (from any riding state)
             phase[p] = GULP; break;
  case GULP: if (rd->state_idx != 0x2f) {         // gulp left START -> promote into the suck loop
               Rider_StartInhaleLoop(rd); phase[p] = LOOP;
             } break;                              // else: gulp still playing
  case LOOP: if (rd->state_idx == 0x30)
               *(s32 *)((char *)rd + 0x93C) = 8;  // HYPERNOVA_INHALE_TIMER_HOLD: don't time out
             else if (rd->state_idx != 0x31 && rd->state_idx != 0x2f)
               Rider_StartInhaleLoop(rd);          // engine dropped the loop while held -> re-enter
             break;
}
return phase[p] != IDLE;                          // caller runs the cone scan only while true
```

The **tap** path lets the vanilla gulp play its full duration and return to neutral on its own.
The **hold** path promotes into the suck LOOP the same frame the gulp leaves START - before
render, so the open mouth carries through with no flicker back to neutral - then tops `+0x93C`
back up each LOOP frame (the engine still animates the loop) and re-enters the loop if the
engine ever drops it while held. The **release** path calls `Rider_EndInhale` for the engine's
own close-mouth -> puff -> neutral. `HYPERNOVA_INHALE_TIMER_HOLD` must be `>= 2` (one decrement
lands before the next write) and only matters if the unreliable countdown is honored at all.

Gaining a copy ability or power-up needs no handling here: `OnFrameEnd` ends that player's
Hypernova (phase IDLE, `stc_active` cleared) *before* `DriveInhale` runs, so the drive is never
called for a rider mid-handoff and never fights the pickup animation.

## Rainbow Recolor

Kirby cycles through a rainbow hue for the **entire** active duration (not only while the
trigger is held) - it is the power-up's signature look. The recolor is real-time and does not
touch the `.dat`; it drives live model state.

**Kirby's body color is texture-swap, not a material color register.** The rider keeps a flat
array at `RiderData+0x2c0` (one entry per material slot) whose entries are **`hsd_tobj`**
texture objects. Each TObj holds an array of `ImageDesc` pointers at `TObj+0x68` (one per color
variant) and an `AObj` at `TObj+0x64` whose playhead selects which variant is shown. The 8
player colors (pink/yellow/blue/red/green/purple/brown/white) + wing/fire are just entries in
that texture array; `RiderKirby_SetMaterialColorAndUpdate(rd, part, idx)` walks `+0x2c0` and
drives every TObj's AObj to variant `idx`. Texture selection is discrete, so there is no
continuous body color register to sweep - driving the AObj continuously snaps between baked
textures rather than blending.

### The ColAnim color overlay

Kirby has a **second** color system: the **ColAnim** overlay (the candy/invincibility flash).
It is a per-rider color the renderer blends over the textured model through a TEV color stage,
so it tints whatever texture is showing, with arbitrary RGBA. Each rider has three overlay slots
(`RiderData+0x5c` body, `+0x108` glow, `+0x1b4`); a per-frame selector (`ColAnim_Resolve`,
`0x8006ae7c`) picks the highest-priority active slot (`ColAnim_GetActiveSlot`, `0x8006ad20`),
copies its color into the slot's render-context, and a TEV-setup renderer (`ColAnim_SetupTev`,
`0x8006aaa4`) applies it. Within the body slot at `RiderData+0x5c`:

| Offset | Field |
|---|---|
| `+0x08` | anim-data pointer (the per-frame animation reads this; NULL it to freeze the tick) |
| `+0x28` (word 10) | current anim index (0 = inactive; nonzero = the selector treats it active) |
| `+0x2c` | packed RGBA the selector copies into the render-context |
| `+0x30` | the live overlay color as RGBA floats (0..255) |
| `+0xa9` | priority byte; the selector renders the highest-priority **active** slot, and `ColAnim_Apply` is priority-gated (refuses a new anim while the current `+0xa9` is higher) |
| `+0xaa` | state-flags byte; bit `0x80` = color-override active |
| `+0x224` | the packed render-context RGBA bytes the renderer reads |
| `+0x234` | ratio/blend enable byte (`0xff` = ratio path off; the selector sets it from `+0xa8`) |
| `+0x235` | draw-flags byte (bit `0x80` = color-override render path on; selector-managed) |

Every frame the selector re-clears the render draw-flag and only re-sets it (and re-copies
`+0x2c` into `+0x224`) when `+0xaa` bit `0x80` is set. The candy tick is what normally sets that
bit, so with the tick frozen the mod must hold it itself or the overlay stops drawing after one
frame.

The built-in candy flash (ColAnim index 3) is a green pulse (RGBA approx `128,255,128` at low
alpha) and it **loops**: its per-frame tick keeps re-stamping the green into the slot color and
maintaining the override bit for as long as the slot is active. Left running it would re-stamp
its green over any color written to the slot and produce a blink.

### Driven HSV rainbow

`DriveRainbow` applies the candy ColAnim **once** - purely to set the overlay slot up - then
freezes its tick and drives the color every frame with a smooth HSV hue:

```c
// each frame Hypernova is active, independent of the trigger button:
char *slot = (char *)rd + 0x5c;
if (((int *)slot)[10] != 3) {      // index 3 not active yet
    slot[0xa9] = 0;                // floor priority, or the priority-gated apply rejects it
    Rider_ApplyColAnim(rd, 3, 0);  // set up the overlay slot
}
*(u32 *)(slot + 0x08) = 0;         // freeze the looping candy tick (idempotent)
slot[0xa9] = 0xff;                 // pin max priority (see below)
slot[0xaa] |= 0x80;                // hold color-override active (selector needs this)
HueToRgb(hue, &r, &g, &b);         // hue advances 1/PERIOD per frame, shared by all riders
u32 packed = (r<<24)|(g<<16)|(b<<8)|ALPHA;
*(u32 *)(slot + 0x224) = packed;   // what the overlay renders this frame
*(u32 *)(slot + 0x2c)  = packed;   // selector color source for following frames
*(float*)(slot+0x30)=r; [+0x34]=g; [+0x38]=b; [+0x3c]=ALPHA;   // live float color
slot[0x235] |= 0x80;               // force the color-override render path
slot[0x234]  = 0xff;               // ratio/blend path off
```

With the tick frozen and `+0xaa` bit `0x80` held, the selector copies the mod's `+0x2c` color
into the render-context every frame - a continuous rainbow, no flash, no gameplay invincibility.
`HYPERNOVA_RAINBOW_ALPHA` (100) sets tint strength; `HYPERNOVA_RAINBOW_PERIOD` (120) sets frames
per full hue wheel.

**Surviving item-pickup flashes.** A pickup flash can wipe the rainbow two ways: it either
out-prioritizes the body state in the selector, or (same body state) the priority-gated
`ColAnim_Apply` lets a higher-priority flash overwrite it. Pinning the priority byte (`+0xa9`)
to `0xff` every frame prevents both. The pin is undone when Hypernova ends
(`StopRainbowPlayer` calls `ColAnim_Reset` on the body slot, which zeroes `+0xa9`), so normal
hurt/invincibility flashes resume.

## The Inhale Whirlwind

The body rainbow recolors *Kirby*. The swirling whirlwind/cone in front of the mouth during
inhale is a **separate object**, recolored by its own per-frame pass (`RecolorWhirlwinds`) to a
hue offset from the bodies' by `HYPERNOVA_WHIRLWIND_HUE_OFFSET` (0.5 of the wheel, i.e.
complementary) and softened toward white by `HYPERNOVA_WHIRLWIND_TINT` (0.45), so it lands as a
soft wash over the swirl rather than a solid-color repaint.

**What it is.** `Rider_StartInhale` spawns it via `Effect_SpawnSync(parent = rider GObj,
id = 0x3a982, ...)` (`0x80236c40`) and **discards the handle** - nothing on the rider points
back to it. (The `0xda` = 218 passed alongside the id is a spawn-variant *selector* inside
`Effect_SpawnSync`.) It is a standalone **GObj carrying a JObj model tree**, positioned at the
mouth bone each frame:

- render callback `GObj+0x1c (gx_cb) = 0x8023dfe0` (a thin wrapper around `JObj_GX`), destructor
  `GObj+0x30 = 0x80233ddc` - both in the Effect module.
- It is not a C++ `ModelEffect`/`EffectHandle` (those `ObjCollect` lists are empty during
  inhale) and has no point-particle component.
- It is a real JObj model: scaling local scale (`JObj+0x24/+0x28/+0x2c`) and/or the world matrix
  (`JObj+0x44`, 3x4) visibly grows it. It has at least two sub-parts (an outer body + a fainter
  central cone), each its own MObj. It re-spawns at a **new heap address every inhale**, so any
  address must be re-derived, not cached.

**Finding the live instances.** `RecolorWhirlwinds` walks the model-effect GObj bucket
(`(*stc_gobj_lookup)[16]`, p_link 16), requires `entity_class == 25`, and matches the engine's
`Effect` state (GObj userdata, `GObj+0x2c`) on `kind` (`+0x04`) `== 0x3a982` - exactly the
inhale whirlwinds, one per inhaling rider, all driven to the same hue. The model root is
`GObj+0x28`.

**Lifetime is left to the engine.** `Effect.life` (`+0x0c`) is not a plain despawn countdown -
it drives the effect's playback, so writing it freezes the whirlwind's animation. The mod only
recolors. If a sustained suck ever outlives the native whirlwind, spawn a fresh one rather than
pinning `life`.

**How the recolor works.** The color is not in the material color registers - writing the
`HSD_Material` ambient/diffuse/specular (`MObj+0x0c -> mat`) has no visible effect. The rendered
color comes from a compiled TEV color expression that `MObjSetupTev` (`0x803faba0`) rebuilds
every render frame, but the literal RGBA does **not** live in that expression tree; the tree's
constant node just holds a *pointer* to the color, targeting the TObj's `_HSD_TObjTev` struct
(`TObj+0xA8`). So the color lives in plain `GXColor` value fields the texture-animation system
never touches: `tev->constant` (`+0x10`), `tev->tev0` (`+0x14`) and `tev->tev1` (`+0x18`). The
inhale model's combiner is `out = ZERO + lerp(tev0, constant, texC)` - texture brightness blends
`tev0` (dark texels) with `constant` (bright texels) - and `tev1` is unused by this asset;
`RecolorEffectTree` rewrites the RGB of all three on every part, **preserving each register's
alpha**. The alpha equation is `constant.a * TEXA` (the texture's per-texel alpha is the swirl
shape, `constant.a` a global multiplier), so leaving alpha untouched keeps the whirlwind at its
vanilla opacity. The walk recurses the whole JObj tree (child + sibling) since the sub-parts are
separate joints, each with its own MObj and `tev`. It never writes into the `HSD_TExp` node tree
itself - node `+0x04` (list link) and `+0x08` (the color *pointer*) are live, and clobbering
either crashes the walk. `effects-system.md` has the full TEV path and selector encoding.

The in-place recolor gives color but no scale/shape control. If Hypernova ever needs a
differently-sized or differently-shaped swirl, the path is to spawn its own effect at the mouth
bone each frame - reusing `HueToRgb`/`stc_hue` and a small custom JObj model - which owns its
handle and gives full color **and** scale control.

## Debug Cone Visualizer

A debug-only overlay (`hypernova_debug.c`, "Debug Cone" menu toggle, off by default) draws a
lightly opaque red cone in world space showing the suction region's reach and angle against the
real items and props in front of the rider. It is decoupled from the power-up: whenever the
toggle is on and a human rider has a usable forward vector (the same `>= 0.01` guard the vacuum
uses), the cone is drawn - Hypernova does not need to be active.

**Same inputs as the suction**, so what you see is what gets vacuumed: apex = `RiderData.pos`
(`+0x300`), axis = normalized `RiderData.forward` (`+0x324`), reach = `HYPERNOVA_RANGE`,
half-angle from `HYPERNOVA_HALF_ANGLE_COS`.

The lateral surface is the exact half-angle cone; the flat base sits at the forward reach (axial
distance = `HYPERNOVA_RANGE`), so tip-to-base length shows the true forward reach. Base radius =
`reach * tan(half-angle)` via the companion constant `HYPERNOVA_HALF_ANGLE_TAN`, which must
track the cosine (the half-angle is a fixed design constant, so its tangent is precomputed). The
base-circle rim is seeded once at install as `HYPERNOVA_DEBUG_CONE_SEGS` (24, i.e. 15-degree
steps) unit `(cos, sin)` pairs, so the per-frame draw does no trig. The far cap of the true
volume is spherical (the test is `dist <= reach` and `angle <= half-angle`); the flat-base cone
is a faithful-enough approximation - its lateral surface and on-axis reach are exact, it only
slightly over-extends past the sphere near the rim.

**Rendering.** Immediate-mode GX on the world camera's 3D link (GX link 0 - the link the
stage/rider models draw on, so the cone lives in the scene and is occluded by closer solid
geometry), modelled on hoshi's `GX_DrawLine`/`GX_DrawRect` inlines: `HSD_StateInitDirect`, flat
vertex color via a single `GX_PASSCLR` TEV stage, `GXLoadPosMtxImm(COBJ_GetCurrent()->view_mtx)`
for the world-space position matrix. Added on top for translucency: alpha-blend
(`GX_BL_SRCALPHA`/`GX_BL_INVSRCALPHA`), Z-test on with Z-write off, and `GX_CULL_NONE` so both
faces draw (the cone reads as a see-through volume). Channel 0 takes color *and* alpha from the
vertex so the per-vertex alpha reaches the blender. Drawn on the **XLU pass** (pass 1) so it
blends over already-rendered opaque world geometry; the render loop (`CObj_RenderGXLinks`)
invokes the GX callback once per pass (0 = OPA, 1 = XLU, 2 = additional).

**Lifecycle.** A standalone render GObj (`GObj_Create` + `GObj_AddGXLink`, no proc/model)
carries the GX callback. It is created lazily once per City Trial session (from `OnFrameEnd`)
and persists; the callback is a no-op while the toggle is off. World GObjs are freed by the
engine on scene teardown, so the mod only caches the handle to avoid recreating it and forgets
it (never destroys it) on the scene/leave-CT reset path - a manual destroy would risk a double
free. Tuning constants live in `hypernova.h` (`HYPERNOVA_DEBUG_CONE_RGBA`, `..._CONE_SEGS`,
`..._GX_LINK`).

## Symbols and Offsets Reference

### Native inhale (rider side)

Named in `GKYE01.map` / Ghidra; `Rider_StartInhale` + the gate/probe/scan/predicate are in
`link.ld` + `rider.h` (callable from mod code).

| Symbol | Address | Role |
|---|---|---|
| `Rider_StartInhale` | `0x801ad2c4` | **force into suck START (anim 0x2f, action-state 0x76)**: + suction VFX (Effect 0x3a982) + SFX (0x20037); installs capture callbacks. No gate/target check. One-shot gulp - returns to neutral when the anim ends, does **not** advance to the LOOP. |
| `Rider_StartInhaleLoop` | `0x801ad4cc` | **enter/re-enter suck LOOP (anim 0x30, action-state 0x77)**: reinstalls scan/volume callbacks. No VFX/SFX respawn, no capture reset. The LOOP process calls this itself on body-anim-done to animate/sustain; this is also the **only** way the LOOP is ever entered (the engine never advances START -> LOOP). |
| `Rider_IsBodyAnimDone` | `0x80198b00` | 1 once the body motion has played to its end (per-part HSD check `0x80054798`); gates the LOOP process's per-cycle re-entry of suck-LOOP `0x30`. |
| `Rider_InhaleStartProc` | `0x801ad1dc` | suck-START (`0x76`) action-state process: holds the gulp anim, then on body-anim-done hands off to the generic ability-resolve / star-wait path (`zz_801a8454_`) -> neutral. Does **not** go to LOOP. |
| `Rider_InhaleLoopProc` | `0x801ad3a0` | suck-LOOP (`0x77`) action-state process: runs `Rider_InhaleLoopTick`; on body-anim-done re-enters the loop (`Rider_StartInhaleLoop`) unless the tick ended it. |
| `Rider_InhaleLoopTick` | `0x801ad550` | per-LOOP-frame: pulls/swallows captured objects, else decrements the `+0x93C` countdown; returns 1 (-> `Rider_EndInhale`) when the gesture should end. |
| `Rider_EndInhale` | `0x801adf98` | ends the gesture: `RiderStateChange(..., 0x31, ...)` (suck END -> neutral). |
| `Rider_InhaleCaptureCount` | `0x801adf5c` | counts the non-null capture slots (`+0x8f0/+0x8f4/+0x8f8`); 0 = mouth empty, so the tick runs the countdown. |
| `Rider_CanStartInhale` | `0x801a617c` | gate: attack bit (`+0x820` bit 2) + `copy_kind == -1` + mouth not full (`+0x918 < 3`, via `Rider_IsInhaleMouthFull` `0x801adf40`). The attack bit is **transient**, so this reads false on nearly every frame - not usable as a "can I inhale now" gate for a continuous drive. |
| `Rider_TryStartInhale` | `0x8019c5ac` | entry probe - only calls `Rider_StartInhale` if a target already overlaps. Riding is a wide cluster of `state_idx` values (`0x21`-`0x2a`, `0x6a`, ...), all inhale-capable, so `DriveInhale` starts from any riding state rather than gating on one. |
| `Rider_InhaleScanThink` | `0x801adc78` | installed scan slot's body (via thunk `zz_801ad48c_`): calls `Rider_InhaleCaptureScan` while `RiderData+0x918 == 0`. |
| `Rider_InhaleCaptureScan` | `0x8019c63c` | per-frame multi-capture scan (EventActor bucket only) |
| `EventActor_IsInhalable` | `0x802041c8` | candidate predicate (EventActor enemies only) |
| `HurtVolume_OverlapTest` | `0x80189784` | sphere/capsule overlap; rider radius = `base * (+0x2c8)` |
| `Trigger_UpdatePosition` | `0x8018a188` | re-anchors mouth volume |
| `EventActor_OnCapture` | `0x802038c4` | put actor into captured state |
| `EnemyState_AnimTick` | `0x8020be1c` | attraction steering |
| `EnemyState_InhaledFunc4` | `0x80203b64` | mouth-follow + shrink + destroy past 120 frames |

RiderData: input word `+0x820` (bit 2 = attack), copy kind `+0x454` (-1 = none), reach /
base_scale `+0x2c8`, suction volume `+0x828`, capture slots `+0x8f0/+0x8f4/+0x8f8`, capture state
`+0x918`, inhale LOOP countdown `+0x93C` (aliases `copy_wheel_result` - unreliable when driven;
decremented each LOOP frame while the mouth is empty, 0 -> `Rider_EndInhale`). Enemy.dat params
`*0x805dd878 +0x7c/+0x88/+0x8c`.

### Recolor (rider side)

In `link.ld` + `rider.h` (callable from mod code).

| Symbol | Address | Role |
|---|---|---|
| `RiderKirby_SetMaterialColor` | `0x80198d1c` | stage `model_part[part].cur_mat_index` + set recolor-dirty bit (`+0x821` bit7) |
| `RiderKirby_SetMaterialColorAndUpdate` | `0x80198d3c` | stage + immediately drive the body MatAnim to baked color `idx` (called by Fire/Bird ability hat code) |
| `Rider_GetColor` | `0x80192758` | returns `PlayerData.color_idx` (KirbyColor 0..7) |
| `Rider_ApplyColAnim` | `0x8019bfb4` | request a baked color-overlay anim into the body ColAnim (`+0x5c`) |
| `ColAnim_Apply` | `0x8006a3f0` | generic priority-gated ColAnim applier (`+0x5c` body / `+0x108` glow) |
| `ColAnim_Reset` | `0x8006a250` | clear a ColAnim slot (zeroes anim-data ptr `+0x08`, index, flags) |

ColAnim render path (engine internals, not called from mod code): `ColAnim_GetActiveSlot`
(`0x8006ad20`) returns the highest-priority active slot; `ColAnim_Resolve` (`0x8006ae7c`) builds
the slot's render-context each frame; `ColAnim_SetupTev` (`0x8006aaa4`) emits the overlay TEV
stage. The body slot's per-frame animation reads the anim-data pointer at slot `+0x08` - NULL it
to freeze the tick and own the color.

RiderData (recolor): body model JObj root `*(*(+0x2b0))`, material/render-object array `+0x2c0`
(`dobj_lookup_arr`), `model_part[3]` at `+0x42a` (`cur_mat_index`/`original_mat_index`), body
ColAnim state `+0x5c`, glow ColAnim state `+0x108`, third ColAnim state `+0x1b4`. Material color
= `HSD_Material` (ambient/diffuse/specular `GXColor`) reached via `dobj_lookup_arr[i] -> MObj ->
mat`.

### Whirlwind recolor (model effect)

Uses `obj.h` + `effect.h` structs (no new callable functions). The inhale VFX is a model-effect
GObj: `entity_class` 25, `p_link` 16, render cb `GObj+0x1c == 0x8023dfe0`, model root
`GObj+0x28`, `Effect` userdata `GObj+0x2c` with `kind` at `+0x04` (`== 0x3a982` for the inhale
suction). Match on `kind` only and **recolor** - do not write `Effect.life` (`+0x0c`); it drives
the effect's animation, so pinning it freezes the whirlwind. Color per part:
`JObj -> DObj -> MObj -> TObj -> tev` (`_HSD_TObjTev` at `TObj+0xA8`); rewrite the RGB of
`tev->constant` (`+0x10`), `tev->tev0` (`+0x14`) and `tev->tev1` (`+0x18`, unused by this
asset), preserving alpha. Bucket head: `(*stc_gobj_lookup)[16]`, chained via `GObj+0x8`. See
`effects-system.md` for the TEV path. The generic point-particle pool (machine exhaust,
sparkles) is separately recolorable via its own per-particle color fields, but the whirlwind has
no point-particle component, so that lever does not reach it (`particle-system.md`).

### Items

| Symbol | Address | Role |
|---|---|---|
| `CityItem_Create` | `0x8024eef4` | spawns item GObj (entity_class 22, p_link 13) |
| `CityItem_PhysicsThink` | `0x8024f778` | `pos += vel` each frame (pri 4) |
| `CityItem_EnvColl` -> `Item_GenericEnvColl` | `0x8024f814` -> `0x80255438` | ground snap / collision (pri 5) |
| `CityItem_UpdatePosition` | `0x802503c0` | builds matrix from pos/forward/up/scale (pri-6 proc `0x8024f848`) |
| `CityItem_LifetimeThink` | `0x8024fa38` | timeout/OOB despawn, count bookkeeping |
| `CityItem_Destructor` | `0x8024fc1c` | full cleanup (frees CollData via `0x80254404`) |
| `CityItem_CanCollect` | `0x80252df0` | collectible test (1 iff `x35a` bits 5 and 6 are clear) |
| `GObj_Destroy` | `0x80428f64` | destroy a GObj |

ItemData (`gobj+0x2c`): item_gobj `0x0`, parent_gobj `0x4`, child_gobjs[4] `0x8`, kind `0x1c`,
item_category `0x24`, lifetime `0x44`, scale `0xac` (base `0xa8`, attr `0x118`), accel `0xb8`,
vel `0xc4`, pos_delta `0xd0`, **pos `0xdc`**, prev_pos `0xe8`, forward `0x100`, up `0x10c`,
coll_data `0x1a4`, is_airborne `0x1d4`, TriggerData `0x250`, coll_kind `0x359` bits 2-4, flags
`0x35a` (bit4 grounded, bits5/6 collect gates). Item list head `((GOBJ**)0x805de334)[13]`, next
`+0x8`, live count `*(int*)0x805dd8cc`. The rendered matrix scale is
`scale(+0xac) * attr_scale(+0x118)`.

### Yakumono

| Symbol | Address | Role |
|---|---|---|
| `grYakuCheckGObjYakumono` | `0x800f7a50` | `gobj->kind == 15` test |
| `GrYakumono_GetDescId` | `0x800f7a64` | reads `desc_id` (`+0x04`) - identity / targeting |
| `GrYakumono_GetState` | `0x800f7ab8` | reads `state` (`+0x74`) |
| `collideWithObject` | `0x800f5004` | `coll_func` dispatch: `stc_yaku_descs[desc]->+0x04(yaku, otherCollData, ...)` - the real break entry |
| `hitWeakObject` / `hitStrongObject` | `0x80107914` / `0x801086d0` | weak (one-shot, visible break is debris only) / strong (subtractive HP, full visible break inline) `coll_func`s (desc 33-35 / 36-38) |
| `hitBreakableFloor` / `hitBigStar` | `0x80106bd0` / `0x80103eb8` | floor multi-stage (desc 32) / star pole (desc 29) `coll_func`s |
| `GrYaku_TestImpactBreak` / `GrYaku_ApplyImpactDamage` | `0x80104cd4` / `0x80104be0` | the force calc: `radius * impactSpeed^2` vs HP |
| `GrYaku_SpawnBreakEffect` | `0x800f7044` | per-sub-part break debris/dust emitter (`Effect_SpawnSync` by sub-part kind). Positions each effect via a grobj node (`entry+0x08` id -> `Gr_GetNodeWorldPos` `0x800d4bf4`), a *separate* node pinned at the prop's baked spot - Hypernova temporarily relocates that node (it honors `USER_DEF_MTX`) onto the rider so the rubble erupts at Kirby. Called by both weak and strong tails |
| `GrYakuBreakColl_BrokenProc` | `0x80107798` | weak family's broken-state (state 1) proc - teardown only: waits for all the family's props to break (`hp_block+0x134 < 1`), then `GObj_Destroy`s the parent. Does NOT animate individual props |
| `mpColl_UpdateCollision` | `0x802485e0` | rider/machine per-frame mpColl query vs the static map mesh; dispatches scene-object hits (the natural break path the synthesis stands in for) |
| `mpResponse_DispatchSceneObjColl` / `mpColl_SphereSceneObjColl` | `0x80248bb4` / `0x8024d414` | the two `collideWithObject` callers. Response **honors `region+0x3c` bit 6** (drops contacts whose bit is clear - the lever that makes a retired prop pass-through); sweep finds contacts geometrically and gates `collideWithObject` on `zz_80241574_` (`collider+0x34c` bit 2 AND `record+0x8c == 3`). Holder is `stc_grobj+0x54` (region base `*(holder+8)` = `*(stc_grobj+0x5c)`); `region_idx = (record+0x0c - *(holder+8))/0x40` |
| `grScene_GetImpactSpeed` | `0x800d8edc` | impact-speed helper inside the force calc: projects `delta` (`coll+0x14`) onto `region+0x0c` normal, **negates** (scale `0x805df634` = -1.0), clamps `<=0` to 0 (so delta must point *into* the surface); asserts `region+0x3c` bit 7; geometry-refined path gated on `region+0x34` bit `0x20` (cleared during synthesis) |
| `destroyBigStar` | `0x800d7b8c` | break gate: walks the collider's `coll+0x44` mpCollInfo for a BigStar region overlap; returns non-zero (skipping the break) iff found. A zeroed collInfo with `+0x1d0 = -1` returns 0 -> break proceeds |
| `GrYakuBreak_GetAttackerPly` | `0x80105cb0` | maps the impacting collider's GObj (`coll+0x04`) -> player index for the break-count credit (rider/machine/other) |
| `grScene_SetInstanceColl` | `0x800d7ad0` | sets/clears the collidable bit (`region+0x3c` bit 6) on **every** mpColl region of a scene-instance record; the family `coll_func` calls `(record, 0)` to retire collision on break |
| `grScene_IsInstanceCollAll` | `0x800d7b0c` | returns 1 iff every region's collidable bit == the arg (the break path's "still intact?" guard; also the mod's "consumed?" check) |
| `GrYaku_IncrementBreakCount` | `0x80105d80` | `(yaku_gobj, player_idx)` -> credits `Ply_IncrementYakumonoBreakCount(player_idx, desc_id)` (the checklist break-count; called inside the family `coll_func`) |
| `GrYaku_InitMatrix` | `0x800f73fc` | rebuilds JObj world matrix from JObj local T/R/S (gated by `GrYakumono_Proc4` `0x800f52e8` on `+0x12c` bit 7) |
| `GrYakumono_Proc7` | `0x800f53a8` | per-frame `HurtData_UpdatePerFrame(scale=+0xa4)` - the live hurtbox-scale consumer |
| `GObj_Destroy` | `0x80428f64` | despawn (runs `GrYaku_DestroyCallback` `0x800f4f0c` -> frees HurtData; collision-safe) |

YakumonoData (`gobj+0x2c`): gobj backref `0x0`, **desc_id `0x04`** (= break-count stat-index),
data_ptr `0x08`, **pos `0x1c`** (single-instance world pos; **0,0,0 for break families**), model
JObj `0x64` (**NULL for break families**), `xform_jobj` `0x70` (**NULL for static/break props**),
state `0x74`, **scale `0xa4`** (`Gr_DefaultScale` 1.0 - hurtbox scale, not model), damage
accumulator `0xac`, HurtData `0xec` (hit-gate `+0x24`, damage `+0x28`), proc slots `0xf0`-`0x108`
(**`+0x100` on_damage is NULL for CT break families**), effect group `0x10c`, **flags byte
`0x12c` (bit 7 `0x80` = matrix-dirty)**, **child-prop array `0x130`** (`region_audio_arr` in
hoshi's struct; for break families it is the per-prop scene-instance record array). The "Move =
JObj local-T (`JObj+0x10`) + set `+0x12c` bit 7; visual scale = JObj local scale; hurtbox scale =
`+0xa4`" recipe is for **single-instance** yakumono; for break families operate on each child
record's JObj instead. `GrObj.yaku_num` is the live **GObj** count (~31 in CT), not the prop
count.

Placed-instance record (`GrCollRecord`, `0x98` bytes): `jobj` `+0x00` (world matrix
`JOBJ.rotMtx`), triangle slice `tri_begin` `+0x0c` / `tri_num` `+0x10`, cached load-time 3x4
matrix `world` `+0x2c`, owning parent GObj `yaku_gobj` `+0x90`. A `GrCollTri` is `0x40` bytes:
`normal` `+0x0c`, `flags` `+0x34` (bit `0x20` = `GRCOLL_FLAG_MOVING`), `state` `+0x3c` (bit
`0x40` = `GRCOLL_STATE_COLLIDABLE`).

**City Trial target inventory** (desc_id -> object; full table + identity sourcing in
`yakumono-system.md`): 29 = **star pole**, 32 = **forest pitfall**, 33 = **coral**, 34 =
**forest trees**, 35 = **volcano + high-plains rocks**, 36 = **volcano rock walls**, 37 =
**volcano-base holes**, 38 = **dilapidated houses**. Not targets: 17/18 (passive zones), 46
(gondola, x16), 61 (decorative x2), 68 (Lighthouse), 69 (WhispyWoods).

### Machines

| Symbol | Address | Role |
|---|---|---|
| `Machine_OnKO` | `0x801e568c` | destroy a machine: captures rider ply -> `+0x1b48` (sentinel 5 = unridden), sets `is_dead`, disables hit-collision, enters BreakDown state (29). Rider-safe (destroy tail guards every rider deref on `+0x1b48 == 5`). Hypernova calls this on a claimed machine's arrival |
| `Machine_CheckKO'd` | `0x801e5628` | per-frame KO detector (from prio-10 `Machine_DmgApply` `0x801c6834`): calls `Machine_OnKO` when `hp == 0.0` and `Gm_IsDamageEnabled()`. Runs for parked machines too - Hypernova bypasses it by calling `Machine_OnKO` directly |
| `Machine_KOExplode` | `0x801e5838` | BreakDown-state destroy tail: spawns explosion VFX (`Effect_SpawnSync` 0x2799 + a debris effect) + break SFX, then `GObj_Destroy`s the machine. **Gated by `MachineData[0x78]` bit 0x40** (Hypernova ORs it in). Rider eject is skipped when `+0x1b48 == 5` |
| `Machine_PhysicsThink` | `0x801c6368` | proc priority 4: integrates `pos += accel(+0x318) + velocity(+0x324) + ...`. The pull zeroes accel/velocity each frame so the `pos` override sticks |
| `Machine_ApplyModelMatrix` | `0x801c9074` | proc priority 6: rebuilds the machine JObj matrix from `pos(+0x3e8)` + rotation(+0x418) + `model_scale(+0x310) * model_scale_base(+0x468)` |

MachineData (`gobj+0x2c`): machine gobj `0x0`, **rider_gobj `0x4`** (NULL = unridden), **kind
`0x24`** (`MachineKind`), **model_scale `0x310`**, accel `0x318`, **velocity `0x324`**, **pos
`0x3e8`**, forward `0x418`, hp `0xa18`, hp_max `0x4cc`, **KO-break gate byte `0x78` bit 0x40**,
**is_dead `0xc35` bit 0x20**, **is_fall_dead `0xc35` bit 0x80**, rider-ply KO sentinel `0x1b48`.
Machine list head `(*stc_gobj_lookup)[GAMEPLINK_MACHINE]` (bucket 9), `gobj->entity_class ==
GAMEENTITY_MACHINE` (16), next `gobj->next`.

### Scale (Big Kirby reuse)

| Symbol | Address | Role |
|---|---|---|
| `Rider_ApplyModelMatrix` | `0x80190848` | model matrix from `base_scale * model_scale` |
| `Rider_ModelMatrixThink` | `0x8018f79c` | per-frame proc (pri 6) |
| `gmLanMenu_Scale3DObject` | `0x80054414` | bakes `scale * orientation` into the model JObj matrix |
| `Ply_GetRiderGObj` | `0x8022cb74` | player_idx -> rider GObj |
| `Rider_AbilityRemoveModel` | `0x80191554` | clears the held copy ability **and** power-up via the installed remove callback |
| `Rider_LoseAbilityState_Enter` | `0x801b0adc` | spit-out state after the strip |
| `Vec3_RotateAboutUnitAxis` | `0x800638f8` | in-flight tumble (items, props) |

RiderData: pos `+0x300`, forward `+0x324`, up `+0x330`, base_scale `+0x2c8`, **model_scale
`+0x348`** (write this for 2x), copy_kind `+0x454`, powerup_kind `+0x45c`.
