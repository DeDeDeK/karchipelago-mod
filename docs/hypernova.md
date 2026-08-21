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
  matches the item name and calls `ActivatePlayer` for the player who picked it up. The item's
  archive is this mod's, at `mods/hypernova/assets/items/MiracleFruit.dat`, so it is staged to
  the disc's `items/` folder and discovered only when this mod is in the build.
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
`copy_kind == COPYKIND_NONE`). So Hypernova is mutually exclusive with the rider's copy ability
and City Trial power-up (`powerup_kind`: firecracker, Sensor Bomb, Gordo, Panic Spin) - exactly
as those two are mutually exclusive with each other (`Rider_GivePowerUpByKind` `0x801a8304`
calls `Rider_AbilityRemoveModel` before granting). The vanilla "last pickup wins" rule is
applied to Hypernova both ways:

- **Activating** Hypernova strips whatever the rider was holding: `Hypernova_ActivatePlayer`
  calls `Rider_AbilityRemoveModel` (`0x80191554`) - which clears **both** `copy_kind` and
  `powerup_kind` by invoking the held kind's installed remove callback - then
  `Rider_LoseAbilityState_Enter` (`0x801b0adc`) for the spit-out to neutral. Without this strip
  the guard below would read the pre-existing state and cancel Hypernova on its first frame.
- **While active**, the frame the rider *gains* a copy ability or power-up (`PlayerHoldsAbility`),
  `Hypernova_OnFrameEnd` ends Hypernova for that player. This runs **before `DriveInhale`** -
  the grant has already moved the rider into the ability/power-up action-state during the
  frame's game procs, so ending here stops the inhale drive from stomping the new state on the
  next frame. The giant + rainbow + vacuum tear down cleanly while the new ability takes over.

This is why the vacuum can suck up copy-ability and power-up items freely: collecting one ends
Hypernova and hands the rider that state, rather than the two fighting over the rider's
action-state and animation.

## Kirby 2x Scale

The rider model matrix is rebuilt every frame by `Rider_ApplyModelMatrix` (`0x80190848`, via
`Rider_ModelMatrixThink` `0x8018f79c`, GObj proc priority 6), which feeds
`gmLanMenu_Scale3DObject` (`0x80054414`) the **product** of `RiderData.base_scale` (`+0x2c8`)
and `RiderData.model_scale` (`+0x348`) along with the rider's forward/up/pos.

Writing `model_scale` is the entire mechanism - no JObj poking; the engine consumes it every
frame, and it resets to 1.0 on scene change because models are recreated. `TickScale`
(`hypernova.c`) smoothsteps it from `HYPERNOVA_SCALE_NEUTRAL` (1.0) toward
`HYPERNOVA_SCALE_TARGET` (2.0) over `HYPERNOVA_SCALE_ANIM_FRAMES` (30) on activate and back on
deactivate, writing the field directly each frame. A settled, inactive player's `model_scale` is
left alone entirely so the mod never fights other scale writers.

**Do not also bump `base_scale` for the 2x look** - the model uses the product, so raising both
compounds to 4x. `base_scale` is also the vanilla inhale's range knob (`HurtVolume_OverlapTest`
`0x80189784` scales the mouth sphere by it), but that sphere is only tested against EventActor
candidates, of which City Trial has none, so widening it buys nothing. The vacuum's reach is an
independent mod constant.

## The Vacuum Pipeline

`Hypernova_VacuumPlayer` runs for a player on the frames an inhale is actually being driven,
and only *claims* targets; the three `Hypernova_VacuumProcessClaimed*` passes move them. Claims
therefore keep flying in regardless of the drive phase - only *new* claims require an active
suck.

### Cone scan

Origin and aim come from the rider: `Ply_GetRiderGObj(player_idx)` (`0x8022cb74`) gives the
rider GObj, `origin = RiderData.pos`, `aim = normalize(RiderData.forward)`. A facing shorter
than `0.01` is unusable and the whole scan is skipped that frame.

`Hypernova_InCone` is squared-magnitude throughout (no sqrt): a candidate at `p` is in when
`|p - origin|^2 <= RANGE^2` and `dot(p - origin, aim) >= 0` and
`dot^2 >= HALF_ANGLE_COS^2 * dist^2`. A candidate essentially on top of the rider
(`dist^2 < 0.0001`) is always in.

The shipped reach is `HYPERNOVA_RANGE` 175 world units with a 30-degree half-angle
(`HYPERNOVA_HALF_ANGLE_COS` = 0.8660254), fully decoupled from the vanilla mouth volume.

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

`Hypernova_ClaimItems` walks the item p_link bucket (`(*stc_gobj_lookup)[GAMEPLINK_ITEM]`, 13)
rather than the global GObj list - it skips everything that is not an item. A candidate must
have `entity_class == 22`. **Boxes are skipped** by `ItemData.item_category == 0` (0 = box,
non-0 = power-up). Position is `ItemData.pos` - the field the matrix proc draws from
(`CityItem_UpdatePosition` `0x802503c0`, priority-6 proc `0x8024f848`), *not* `land_pos`.

Claims (up to `HYPERNOVA_MAX_ITEM_CLAIMS`, 128) record the `ItemData` pointer plus its owner
slot. `Hypernova_VacuumProcessClaimedItems` re-validates each claim by walking the bucket for
the pointer before dereferencing it, so a collected/despawned item (or a reused pointer)
self-heals out of the set; a claim whose owner's rider GObj is gone is dropped too.

Per frame, `Hypernova_PullItem` steps `pos` with the shared step, spins the item, and
neutralizes the physics that would fight it:

- zero `vel` - `CityItem_PhysicsThink` (`0x8024f778`) integrates `pos += vel` every frame;
- set `is_airborne` to `-1` so `Item_GenericEnvColl` (`0x80255438`) skips the per-frame ground
  raycast/snap, and clear the grounded flag (`ItemData.x35a` bit 4).

Collection is left to the engine: the item is pulled all the way into the rider/machine so the
**vanilla pickup trigger** fires naturally, giving proper credit, pickup SFX, and effects. A raw
`GObj_Destroy` would vacuum the item away **without** crediting it.

### Yakumono: claim -> pull/shrink -> break

CT breakables are **multi-instance**: one `YakumonoData` GObj manages N placed props, and the
parent itself has no usable transform - its `pos`, `hsd_object`, model JObj and `xform_jobj` are
all NULL/zero. The visible geometry lives in the **ground scene-instance pool**: each placed
prop is a `GrCollRecord` (`Gr_GetCollRecords`) carrying its own `jobj` (world matrix
`JOBJ.rotMtx`) and a `yaku_gobj` back-pointer to its owning parent GObj. The vacuum therefore
moves the per-prop **records**, not the GObj, sidestepping the per-family layout differences of
`YakumonoData.region_audio_arr`. `on_damage` is NULL for these families, so the break is
collision-force driven, not damage-driven.

**Skeleton-joint families.** The static families (houses 38, walls 36, holes 37, floor 32,
BigStar 29) have plain JObjs whose world matrix can be written directly. The **weak** families
(trees 34, rocks 35, coral 33) are `JOBJ_SKELETON` joints: `HSD_JObjSetupMatrixSub`
(`0x8040d6b4`) rebuilds their world matrix from the joint SRT every frame, clobbering a plain
write. `Hypernova_PullInstance` sets `JOBJ_USER_DEFINED_MTX` on each joint first, which makes
that setup keep our matrix (idempotent for the static families).

**Enumeration** is two steps: collect the breakable *parent* GObjs from the yakumono p_link
bucket (`GAMEPLINK_YAKUMONO`, 8; `entity_class == YAKUMONO_GOBJ_KIND` 15; breakable `desc_id`;
up to 32 parents); then scan the scene-instance pool and keep each record whose `yaku_gobj` is
one of those parents. Matching by pointer means a non-break record's owner field is never
trusted.

**How props break (collision force vs HP).** `collideWithObject` (`0x800f5004`) reads the prop's
`desc_id`, indexes the 70-entry descriptor table `stc_yaku_descs` (`0x804a5be8`), and calls that
descriptor's **`coll_func`**: `coll_func(yaku_gobj, otherCollData, gcp, tri_idx, contact)`. The
handler (`hitWeakObject` `0x80107914` / `hitStrongObject` `0x801086d0` / `hitBreakableFloor`
`0x80106bd0` / `hitBigStar` `0x80103eb8`) computes a **force** = `otherCollData.radius *
impactSpeed^2` (`GrYaku_TestImpactBreak` `0x80104cd4` / `GrYaku_ApplyImpactDamage` `0x80104be0`)
and breaks when `force > HP` (weak and BigStar: one-shot threshold; strong: subtractive per-
triangle HP; floor: one crack-stage per hit). There is no "deal N damage" entry that bypasses a
collider, and seeding `HurtData` does nothing because `on_damage` is NULL for these families.

**`impactSpeed` is a normal projection, not `|delta|`.** `grScene_GetImpactSpeed` (`0x800d8edc`)
takes the collider's frame delta (`CollData.pos_delta`), projects it onto the contacted
triangle's outward normal, **negates** the projection, and clamps a non-positive result to **0**.
So an impacting body's delta must point **into** the surface; a delta whose dot with the outward
normal is `>= 0` reads as zero impact, giving `force = radius*0 - HP < 0` and no break.

**The triangles cannot be moved, so they are retired.** The engine break needs a *real*
collision: the rider's `mpColl_UpdateCollision` (`0x802485e0`) queries its coll sphere against
the **baked static map-collision mesh**, and its hit dispatchers
(`mpResponse_DispatchSceneObjColl` `0x80248bb4`, `mpColl_SphereSceneObjColl` `0x8024d414`) call
`collideWithObject` per overlapping triangle. A prop's collision triangles are baked at its
origin and are **not** repositioned when its render matrix moves, and they cannot be relocated
either: they are a fixed contiguous slice of the **global triangle array** (`GrCollParam.tri`,
via `Gr_GetCollTris`), found by spatial broadphase. So a moved prop's collision is **retired**
instead (`GRCOLL_STATE_COLLIDABLE` cleared via `grScene_SetInstanceColl(record, 0)`): the rider
response drops any contact whose collidable bit is clear, and nothing re-arms it per frame, so
the clear sticks and no invisible wall is left at the origin. (The sweep path additionally gates
`collideWithObject` on `CollData.flags` bit 2 and `GrCollRecord.desc_kind == 3`.)

**Claiming.** `Hypernova_ClaimYakumono` claims an in-cone record (position read from its JObj
world matrix) if it is not already claimed and still fully collidable
(`grScene_IsInstanceCollAll(record, 1)`), and retires its collision at *claim* time so the
player cannot run into a swept-up prop mid-flight. The claim cap is `HYPERNOVA_MAX_CLAIMS`
(200), sized above CT's ~130 breakables so a wide cone cannot starve later props of a slot.

**Per-frame advance.** `Hypernova_PullInstance`:

- Re-retires the prop's collision every frame of the flight.
- Sets `JOBJ_USER_DEFINED_MTX` and pulls the world-matrix translation with the shared step.
- **Shrinks** the world-matrix 3x3 by `HYPERNOVA_YAKU_SHRINK` (0.70) per frame **only once
  within `HYPERNOVA_YAKU_SHRINK_RADIUS`** (22.0). The record's cached load-time `world` matrix
  keeps the original scale as a sqrt-free reference (squared row-0 magnitude).
- **Breaks** when the prop arrives (within `HYPERNOVA_YAKU_BREAK_RADIUS`, 8.0) **or** has shrunk
  past `HYPERNOVA_YAKU_BREAK_SCALE` (0.20) of its original size.

`HYPERNOVA_YAKU_CLAIM_TTL` (300 frames) force-releases any claim that never breaks, re-arming
its collision rather than gluing the prop to the rider.

**The synthesized break.** `Hypernova_BreakInstanceNative` re-arms the collision
(`grScene_SetInstanceColl(record, 1)`, so the family tail's "still collidable?" guard passes)
and invokes `collideWithObject(yaku_gobj, coll, gcp, tri_idx, contact)` with a fabricated
collider:

- **`gcp`** = the stage's live `GrCollParam` (`Gr_GetCollParam`), which owns the global vertex,
  triangle and record arrays.
- **`tri_idx`** = the global index of the prop's **first triangle with a usable (non-degenerate)
  normal**, computed as `(record->tri_begin - Gr_GetCollTris()) / sizeof(GrCollTri) + k` over the
  record's `tri_num`-long slice. A prop with no usable normal is skipped.
- **`coll`** = a zeroed `CollData`. `radius` is `HYPERNOVA_BREAK_FORCE_RADIUS` (`1.0e9`) so
  `force = radius * impactSpeed^2` far exceeds any prop HP (one-hit break). `pos_delta` is
  `-normalize(tri.normal) * HYPERNOVA_BREAK_FORCE_DELTA` (100) so it projects to a *positive*
  impact speed. `g` is the human rider GObj so `GrYakuBreak_GetAttackerPly` (`0x80105cb0`)
  credits the right player; `coll_info` is a zeroed `mpCollInfo` with `contact_tri_id = -1` ("no
  BigStar contact") so `destroyBigStar` (`0x800d7b8c`) returns 0 and the break proceeds.
- The target triangle's `GRCOLL_FLAG_MOVING` bit is temporarily cleared so
  `grScene_GetImpactSpeed` skips its moving-record path (which runs the collider through the
  record's `prev_inv`/`world` and rewrites a synthetic delta), and restored after the call.
- The contact point passed in is the prop's current (pulled-in) world position.

Break success is detected directly: if `grScene_IsInstanceCollAll(record, 1)` is false after the
call, the family tail fired and the claim is dropped; otherwise the prop is left re-retired and
the flight continues.

**Two visible break paths.** `collideWithObject` dispatches to the prop's family `coll_func`:

- **`hitStrongObject`** (walls 36 / holes 37 / houses 38) does the whole visible break
  **inline** at the passed contact point - retires collision, hides the mesh
  (`HSD_JObjSetFlagsAll`), spawns debris, and calls the per-desc **item-drop** handler from the
  `0x804a70b4` table (`GrYakuBreak*_DropItems`). A pulled-in house therefore breaks correctly at
  the rider with nothing left behind.
- **`hitWeakObject`** (coral 33 / trees 34 / rocks 35) does **not** hide the original mesh
  inline, and its broken **state** (`GrYakuBreakColl_BrokenProc` `0x80107798`) is teardown only
  (waits for every prop in the family to break, then `GObj_Destroy`s the parent). So the
  *entire* visible break for a weak prop is its **debris effects** (`GrYaku_SpawnBreakEffect`
  `0x800f7044` -> `Effect_SpawnSync`, one per sub-part), each positioned by `Gr_GetNodeWorldPos`
  (`0x800d4bf4`), which resolves a **node id** (per-instance, from the family's placement table)
  through the stage's joint registry and reads *that* JObj's world translation - a separate node
  from the dragged instance JObj, pinned at the prop's baked spot. The weak path also drops no
  items; only the strong path runs the drop table.

In every case the tail calls `grScene_SetInstanceColl(record, 0)`, `GrYaku_IncrementBreakCount`
(`0x80105d80`) credits the checklist, and the family's remaining-prop counter decrements. The
multi-stage floor (desc 32) advances one crack-stage per call, so it may take a few frames in
the break zone to fully open.

**Weak-family rubble.** Because the weak rubble is debris pinned to a separate stage joint,
`Hypernova_BreakInstanceNative` does two extra things when
`Yaku_GetDescCollFunc(desc_id) == hitWeakObject` (`Hypernova_IsWeakBreakFamily`):

1. **Relocates the debris-anchor node onto the rider for the break instant.**
   `Hypernova_WeakDebrisNode` walks the same chain `hitWeakObject` uses - family break data
   (`YakumonoData.data_ptr->break_family->placement`) -> per-instance `YakuBreakEntry` ->
   `node_id`, resolved through `Gr_GetJoint` - matching the instance by its record pointer in the
   family's parallel record array (`YakumonoData.region_audio_arr`). It sets the node's
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

**Targeting.** `Hypernova_IsBreakableYaku` admits these `desc_id`s: 29 star pole, 32 forest
pitfall, 33 coral, 34 forest trees, 35 volcano + high-plains rocks, 36 volcano rock walls, 37
volcano-base holes, 38 dilapidated houses. Not targets: 17/18 (passive zones), 46 (gondola), 61
(decorative), 68 (Lighthouse), 69 (WhispyWoods).

### Machines: claim -> pull -> KO

City Trial litters the map with loose machines (stars/bikes). The vacuum claims the ones
**nobody is riding** and pulls them into the rider, then **KOs each on arrival** so it plays its
own vanilla break/explosion - the same destruction a machine shows when its HP is depleted in a
brawl. This is behind the "Suck Machines" menu toggle (default on).

`Hypernova_ClaimMachines` walks the machine p_link bucket (`GAMEPLINK_MACHINE`, 9;
`entity_class == GAMEENTITY_MACHINE` 16). A machine is a target only while:

- **unridden** - `MachineData.rider_gobj` is NULL, which naturally excludes **both** human and
  CPU riders, so only parked machines are swept;
- **not already dying** - `is_dead` and `is_fall_dead` clear.

Claims (cap `HYPERNOVA_MAX_MACHINE_CLAIMS`, 32) are keyed by the `MachineData` pointer and
re-validated against the live bucket every frame, so a machine that gets mounted, despawns, or
dies self-heals out of the claim set and a dangling pointer is never dereferenced.

**Pull.** `Hypernova_PullMachine` writes `MachineData.pos` with the shared step.
`Machine_PhysicsThink` (`0x801c6368`, proc priority 4) integrates `pos += accel + velocity +
...` every frame, so the pull zeroes `accel` (`+0x318`) and `velocity` each frame to keep the
position override from being fought. The write lands in `OnFrameEnd`, after the machine's procs,
and is picked up by the next frame's `Machine_ApplyModelMatrix` (`0x801c9074`, priority 6).
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
from the priority-10 `Machine_DmgApply` proc) whenever `MachineData.hp` reaches 0 and
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
   GObj list: the EventActor bucket (p_link 12). Items (p_link 13) and yakumono (p_link 8) are
   never visited.
2. **The predicate admits EventActors only.** `EventActor_IsInhalable` (`0x802041c8`) rejects
   GObj classes `0xE` / `0x26` / `0x3E` (rider/player/projectile) and then requires the
   candidate to pass the EventActor test.
3. **The overlap test reads an EventActor volume.** `HurtVolume_OverlapTest` (`0x80189784`)
   compares the rider's mouth volume (`RiderData+0x828`) against the candidate's volume at
   `EnemyData+0x45c`, which items/yakumono do not have.
4. **Capture is EventActor-specific.** `EventActor_OnCapture` (`0x802038c4`) puts the actor into
   the captured state and writes EnemyData fields.
5. **The captured driver reads/writes EnemyData.** `EnemyState_InhaledFunc4` (`0x80203b64`,
   mouth-follow + shrink + destroy past 120 frames) and `EnemyState_AnimTick` (`0x8020be1c`)
   read attraction/scale fields. Feeding an `ItemData` / `YakumonoData` pointer here reads the
   wrong struct.

And City Trial has nothing for it to inhale anyway: CT spawns no per-type AI enemies, only event
actors (Dyna Blade / TAC / Meteor) and CPU players, none flagged inhalable. Since
`Rider_TryStartInhale` only enters the inhale state *when the overlap test already found a
target in range*, the vanilla inhale never starts on its own in CT - which is also why the mod
must call `Rider_StartInhale` directly for the visual.

### Native inhale reference

**The gate is unusable for a continuous drive.** `Rider_CanStartInhale` (`0x801a617c`) requires
the attack bit (`RiderData+0x820` bit 2), `copy_kind == COPYKIND_NONE`, and a mouth that is not
full (`Rider_IsInhaleMouthFull` `0x801adf40`, capture count < 3). It is checked only by the
*entry probe* - `Rider_StartInhale` itself does not check it, so a forced power-up can drive the
visual even when Kirby holds an ability. The attack bit is also **transient**, set only on the
single frame the attack input registers, so the gate reads false on nearly every frame.

**Force lever: `Rider_StartInhale` (`0x801ad2c4`).** No gate, no target needed. It plays the
suck-START anim `0x2f` (action-state `0x76`), spawns the native suction **whirlwind**
(`Effect_SpawnSync(..., 0x3a982, ...)`) anchored to the mouth bone, plays the inhale **SFX**
(`0x20037`), and installs the per-frame capture callbacks (which, in CT, harmlessly scan the
empty EventActor bucket and capture nothing).

**The inhale is three action-states that do NOT chain automatically.** `RiderData.state_idx` is
the anim id; the parallel action-state runs `0x76`/`0x77`/`0x78`. Anims: `0x2f` suck START,
`0x30` suck LOOP, `0x31` suck END.

- **START (`0x2f`) is a one-shot gulp.** `Rider_InhaleStartProc` (`0x801ad1dc`) holds the anim
  while it plays, then on `Rider_IsBodyAnimDone` (`0x80198b00`) hands off to the generic
  ability-resolve / star-wait path, returning the rider to **neutral**. It does **not** advance
  to the LOOP.
- **LOOP (`0x30`) is entered ONLY via `Rider_StartInhaleLoop` (`0x801ad4cc`).**
  `Rider_InhaleLoopProc` (`0x801ad3a0`) re-enters the loop itself every time the body anim
  finishes, so the suck sustains and animates on its own. It self-terminates via
  `RiderData.inhale_timer` (`+0x93C`, per-action-state scratch that **aliases**
  `copy_wheel_result`): `Rider_InhaleLoopTick` (`0x801ad550`) decrements it each LOOP frame
  while the mouth is empty (`Rider_InhaleCaptureCount` `0x801adf5c` == 0) and, at `0`, calls
  `Rider_EndInhale` (`0x801adf98`).
- **END (`0x31`)** closes the mouth and spawns a close puff (Effect `0x5a557`), then neutral.

Because the engine never advances START -> LOOP, a driven hold must call `Rider_StartInhaleLoop`
to enter the loop after the gulp, top up `inhale_timer` to keep the engine from timing it out,
and call `Rider_EndInhale` on release. `inhale_timer` is not a trustworthy timer when driven
(other systems write `copy_wheel_result`), so the end is driven explicitly.

`Rider_StartInhale` also installs four per-frame callback slots the native state machine calls
while the inhale state is live: volume-update at `RiderData+0x7d0`, scan at `+0x7d4` (which
routes to `Rider_InhaleScanThink` `0x801adc78` -> `Rider_InhaleCaptureScan` while the capture
state at `+0x918` is 0), and exit/cleanup at `+0x7e4` / `+0x7f8`. Held captures live in
`RiderData+0x8f0/+0x8f4/+0x8f8` (treated as `EnemyData`). The mouth volume at `RiderData+0x828`
is a HurtData sphere/capsule re-anchored to a mouth bone each frame by `Trigger_UpdatePosition`
(`0x8018a188`) - not a cone.

### Driving the gesture

`DriveInhale` (`hypernova.c`) owns the whole IDLE -> GULP -> LOOP gesture per player and runs
every frame Hypernova is active. Tapping the trigger plays the full vanilla gulp; holding it
sustains an open-mouth suck loop (with native whirlwind + SFX and the cone vacuum running
underneath); releasing lets it end like a vanilla inhale. Its return value is what gates the
cone scan.

It drives the inhale from whatever riding state the rider is in. Kirby's neutral riding is
**not** a single action-state - `state_idx` cycles through a wide cluster of lean/turn/idle
riding states (e.g. `0x21`-`0x2a`, `0x6a`), and vanilla lets you inhale from all of them - so
the start is not gated on any one state, and GULP is promoted to LOOP by detecting that the gulp
has simply **left its START state**:

- **IDLE + held** -> `Rider_StartInhale`, phase GULP.
- **GULP** -> the frame `state_idx` is no longer `0x2f`, call `Rider_StartInhaleLoop` and go to
  LOOP. Promoting on that same frame (before render) keeps the open mouth from flickering back
  to neutral.
- **LOOP** -> while `state_idx == 0x30`, write `HYPERNOVA_INHALE_TIMER_HOLD` (8) into
  `inhale_timer` so the engine's countdown never expires; if the engine dropped the rider out of
  the loop entirely (not START, not END) while the button is still held, re-enter the loop.
- **Released** -> if a LOOP was running, `Rider_EndInhale` for the engine's own close-mouth ->
  puff -> neutral; a mere tap's gulp is left to finish on its own. Phase returns to IDLE.

`HYPERNOVA_INHALE_TIMER_HOLD` must be `>= 2` (one decrement lands before the next write) and
only matters if the unreliable countdown is honored at all.

Gaining a copy ability or power-up needs no handling here: `OnFrameEnd` ends that player's
Hypernova (phase IDLE, `stc_active` cleared) *before* `DriveInhale` runs, so the drive is never
called for a rider mid-handoff and never fights the pickup animation.

## Rainbow Recolor

Kirby cycles through a rainbow hue for the **entire** active duration (not only while the
trigger is held) - it is the power-up's signature look. The recolor is real-time and does not
touch the `.dat`; it drives live model state.

**Kirby's body color is texture-swap, not a material color register.** The rider keeps a flat
array at `RiderData+0x2c0` (one entry per material slot) whose entries are **`hsd_tobj`**
texture objects. Each TObj holds an array of `ImageDesc` pointers (one per color variant) and an
`AObj` whose playhead selects which variant is shown. The 8 player colors
(pink/yellow/blue/red/green/purple/brown/white) + wing/fire are just entries in that texture
array; `RiderKirby_SetMaterialColorAndUpdate` (`0x80198d3c`) walks the array and drives every
TObj's AObj to a variant index. Texture selection is discrete, so there is no continuous body
color register to sweep - driving the AObj continuously snaps between baked textures rather than
blending.

### The ColAnim color overlay

Kirby has a **second** color system: the **ColAnim** overlay (the candy/invincibility flash).
It is a per-rider color the renderer blends over the textured model through a TEV color stage,
so it tints whatever texture is showing, with arbitrary RGBA. Each rider has three overlay slots
(`RiderData+0x5c` body, `+0x108` glow, `+0x1b4`); a per-frame selector (`ColAnim_Resolve`,
`0x8006ae7c`) picks the highest-priority active slot (`ColAnim_GetActiveSlot`, `0x8006ad20`),
copies its color into the slot's render-context, and a TEV-setup renderer (`ColAnim_SetupTev`,
`0x8006aaa4`) applies it.

The fields the mod drives inside the body slot (all named as `HYPERNOVA_COLANIM_*` in
`hypernova.h`) are: the anim-data pointer at `+0x08` (NULL it to freeze the per-frame tick), the
current anim index at `+0x28` (0 = inactive), the packed RGBA the selector copies out at `+0x2c`,
the live float RGBA at `+0x30`, the priority byte at `+0xa9`, the state-flag byte at `+0xaa`
(bit `0x80` = color-override active), the render-context RGBA at `+0x224`, the ratio/blend enable
at `+0x234` and the draw-flag byte at `+0x235`.

Every frame the selector re-clears the render draw-flag and only re-sets it (and re-copies
`+0x2c` into `+0x224`) when `+0xaa` bit `0x80` is set. The candy tick is what normally sets that
bit, so with the tick frozen the mod must hold it itself or the overlay stops drawing after one
frame.

The built-in candy flash (ColAnim index 3) is a green pulse (RGBA approx `128,255,128` at low
alpha) and it **loops**: its per-frame tick keeps re-stamping the green into the slot color and
maintaining the override bit for as long as the slot is active. Left running it would re-stamp
its green over any color written to the slot and produce a blink.

### Driven HSV rainbow

`DriveRainbow` applies the candy ColAnim **once** - purely to set the overlay slot up, first
flooring `+0xa9` because `ColAnim_Apply` (`0x8006a3f0`) is priority-gated and would otherwise
reject the request - then NULLs the anim-data pointer to freeze the candy tick and owns every
color field itself each frame. Per frame it pins `+0xa9` to `0xff`, holds `+0xaa` bit `0x80`,
converts the shared hue to full-saturation RGB, and writes the packed value into both `+0x224`
(what renders this frame) and `+0x2c` (what the selector copies out on following frames), plus
the float mirror at `+0x30`, then forces the color-override render path (`+0x235` bit `0x80`)
with the ratio path off (`+0x234` = `0xff`).

With the tick frozen and `+0xaa` bit `0x80` held, the selector copies the mod's color into the
render-context every frame - a continuous rainbow, no flash, no gameplay invincibility.
`HYPERNOVA_RAINBOW_ALPHA` (100) sets tint strength; `HYPERNOVA_RAINBOW_PERIOD` (120) sets frames
per full hue wheel. The hue itself is a single module-level phase shared by every rider.

**Surviving item-pickup flashes.** A pickup flash can wipe the rainbow two ways: it either
out-prioritizes the body state in the selector, or (same body state) the priority-gated
`ColAnim_Apply` lets a higher-priority flash overwrite it. Pinning the priority byte to `0xff`
every frame prevents both. The pin is undone when Hypernova ends (`StopRainbowPlayer` calls
`ColAnim_Reset` `0x8006a250` on the body slot, which zeroes the priority), so normal
hurt/invincibility flashes resume.

## The Inhale Whirlwind

The body rainbow recolors *Kirby*. The swirling whirlwind/cone in front of the mouth during
inhale is a **separate object**, recolored by its own per-frame pass (`RecolorWhirlwinds`) to a
hue offset from the bodies' by `HYPERNOVA_WHIRLWIND_HUE_OFFSET` (0.5 of the wheel, i.e.
complementary) and softened toward white by `HYPERNOVA_WHIRLWIND_TINT` (0.45), so it lands as a
soft wash over the swirl rather than a solid-color repaint.

**What it is.** `Rider_StartInhale` spawns it via `Effect_SpawnSync(parent = rider GObj,
id = 0x3a982, ...)` (`0x80236c40`) and **discards the handle** - nothing on the rider points
back to it. It is a standalone **GObj carrying a JObj model tree**, positioned at the mouth bone
each frame:

- render callback `GObj+0x1c (gx_cb) = 0x8023dfe0` (a thin wrapper around `GObj_RenderJObj` `0x8042a258`), destructor
  `GObj+0x30 = 0x80233ddc` - both in the Effect module.
- It has no point-particle component, so the engine's per-particle color fields do not reach it.
- It is a real JObj model: scaling local scale or the world matrix visibly grows it. It has at
  least two sub-parts (an outer body + a fainter central cone), each its own MObj. It re-spawns
  at a **new heap address every inhale**, so any address must be re-derived, not cached.

**Finding the live instances.** `RecolorWhirlwinds` walks the model-effect GObj bucket (p_link
16), requires `entity_class == 25`, and matches the engine's `Effect` state (GObj userdata) on
`kind == 0x3a982` - exactly the inhale whirlwinds, one per inhaling rider, all driven to the
same hue. The model root is `GObj+0x28`.

**Lifetime is left to the engine.** `Effect.life` is not a plain despawn countdown - it drives
the effect's animation looping, so writing it freezes the whirlwind. The mod only recolors. If a
sustained suck ever outlives the native whirlwind, spawn a fresh one rather than pinning `life`.

**How the recolor works.** The color is not in the material color registers - writing the
`HSD_Material` ambient/diffuse/specular has no visible effect. The rendered color comes from a
compiled TEV color expression that `MObjSetupTev` (`0x803faba0`) rebuilds every render frame,
but the literal RGBA does **not** live in that expression tree; the tree's constant node just
holds a *pointer* to the color, targeting the TObj's `_HSD_TObjTev` struct (`TObj+0xA8`). So the
color lives in plain `GXColor` value fields the texture-animation system never touches:
`tev->constant`, `tev->tev0` and `tev->tev1`. The inhale model's combiner is
`out = ZERO + lerp(tev0, constant, texC)` - texture brightness blends `tev0` (dark texels) with
`constant` (bright texels) - and `tev1` is unused by this asset; `RecolorEffectTree` rewrites the
RGB of all three on every part, **preserving each register's alpha**. The alpha equation is
`constant.a * TEXA` (the texture's per-texel alpha is the swirl shape, `constant.a` a global
multiplier), so leaving alpha untouched keeps the whirlwind at its vanilla opacity. The walk
recurses the whole JObj tree (child + sibling) since the sub-parts are separate joints, each with
its own MObj and `tev`. It never writes into the `HSD_TExp` node tree itself - the node's list
link and its color *pointer* are live, and clobbering either crashes the walk.

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

**Same inputs as the suction**, so what you see is what gets vacuumed: apex = `RiderData.pos`,
axis = normalized `RiderData.forward`, reach = `HYPERNOVA_RANGE`, half-angle from
`HYPERNOVA_HALF_ANGLE_COS`.

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
blends over already-rendered opaque world geometry; the render loop invokes the GX callback once
per pass (0 = OPA, 1 = XLU, 2 = additional).

**Lifecycle.** A standalone render GObj (`GObj_Create` + `GObj_AddGXLink`, no proc/model)
carries the GX callback. It is created lazily once per City Trial session (from `OnFrameEnd`)
and persists; the callback is a no-op while the toggle is off. World GObjs are freed by the
engine on scene teardown, so the mod only caches the handle to avoid recreating it and forgets
it (never destroys it) on the scene/leave-CT reset path - a manual destroy would risk a double
free. Tuning constants live in `hypernova.h` (`HYPERNOVA_DEBUG_CONE_RGBA`, `..._CONE_SEGS`,
`..._GX_LINK`).
