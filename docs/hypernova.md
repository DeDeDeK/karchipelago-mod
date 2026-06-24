# Hypernova

A City Trial power-up (`mods/hypernova/`): while active, every human Kirby grows to 2× size and
a held trigger turns the inhale into a wide vacuum that pulls items and breakable yakumono out
of a cone in front of the rider. This doc covers the design and the engine mechanics it relies
on. Activation is driven through the exported `HypernovaAPI` (or the debug self-test
toggle).

## Concept

A "Hypernova" power-up (named after Kirby: Triple Deluxe's super-inhale): while active,
Kirby grows to 2× size and his inhale becomes a wide vacuum that pulls in **items** and
breakable **yakumono** (trees, rocks) from a cone in front of the rider. Sucked items are
**collected** (credited to the player like a normal pickup); yakumono in the cone are pulled
in, shrunk, then **broken** through the game's own break path via a synthesized collision.
Holding the attack button keeps the vacuum running for the duration of the power-up. Both
items and props are **claimed** the moment the cone sweeps them and then pulled in every frame
thereafter (even after they leave the cone), through one shared approach step, so a swept
target is never stranded and the two pull at the same rate.

### Design decisions

| Decision | Choice | Consequence |
|---|---|---|
| Activation | **Temporary power-up state** | A trigger (the exported `HypernovaAPI` or the debug self-test) turns Hypernova on for a timed duration. While active + attack button held, the vacuum runs and Kirby is 2×. |
| Modes | **City Trial only** | Items + yakumono live in City Trial. (Air Ride enemies and Top Ride's separate C++ item system are out of scope.) |
| Sucked items | **Claimed, then pulled in and collected** | Like yakumono below, an item is **claimed** when the cone first sweeps it and then pulled toward the rider every frame thereafter — even after it leaves the cone or the trigger is released — so a swept item is never left behind. The pull runs the item all the way into the rider so the **vanilla pickup trigger fires** — proper credit, SFX, and effects for free. Items and props share one per-frame approach step (`max(SPEED×dist, MIN)`) so they pull at an identical rate. Both **arc up and over** (the pull aims at a point lifted above the rider by a hump of horizontal distance, so a swept target never scrapes/clips the ground) and **tumble** end-over-end (random per-target spin direction) while flying in. |
| Sucked yakumono | **Claimed, pulled/shrunk, then broken via synthesized collision** | Yakumono aren't collectibles. A prop is **claimed** when the cone first sweeps it and then pulled to destruction every frame (even after leaving the cone) so nothing is stranded half-shrunk; it shrinks only once close and breaks on arrival. The break is **synthesized**: `collideWithObject` is called with a fabricated high-force collider whose delta points **into** the contacted region's normal (the engine projects+negates+clamps it, so an arbitrary delta reads as zero impact and won't break), running the family's genuine `coll_func` break tail in one hit (collision retire, debris, SFX, break-count credit, broken-state). The visible result differs by family: **strong** (houses/holes/walls) shatter+drop+hide inline at the contact; **weak** (trees/coral/rocks) show their break as **debris effects** the engine pins to a *separate* grobj node at the prop's **baked spot**, and never hide the dragged intact mesh — so Hypernova (a) **relocates that debris node onto the rider** for the break instant so the rubble erupts at Kirby, and (b) collapses the dragged intact mesh after the break so it doesn't linger (see "Yakumono" below). The collision can't be *moved* with the model (a fixed slice of the static map mesh), so it's **retired for the flight** (no stranded wall) and re-armed for the break instant. |

## Reusing the vanilla inhale: suction vs. presentation

Suction and presentation are two separate layers. **The vanilla suction logic cannot be
reused** for items or yakumono — not by flipping a flag and not by hooking the scan. But
**the inhale's presentation layer (animation + suction VFX + SFX) is reused** to make the
custom vacuum look like a real Kirby super-inhale. So the design is a **hybrid**: vanilla
visual, custom suction.

### Why the suction can't be reused (the whole pipeline is EventActor-typed)

The native inhale is a five-stage pipeline and *every* stage is hard-wired to EventActor
enemies. Admitting items/yakumono would mean rewriting all five stages — i.e. fully custom code.

1. **The scan only walks the EventActor bucket.** Both the entry probe
   `Rider_TryStartInhale` (`0x8019c5ac`) and the per-frame `Rider_InhaleCaptureScan`
   (`0x8019c63c`) iterate exactly one GObj list: `*(stc_gobj_lookup + 0x30)` — the
   EventActor bucket. Items (p_link 13) and yakumono (p_link 8) are in different buckets
   and are never even visited.
2. **The predicate admits EventActors only.** `EventActor_IsInhalable` (`0x802041c8`)
   rejects GObj classes `0xE` / `0x26` / `0x3E` (rider/player/projectile) and then requires
   the candidate to pass the EventActor test (`zz_802041fc_`). Items/yakumono fail.
3. **The overlap test reads an EventActor volume.** `HurtVolume_OverlapTest` (`0x80189784`)
   compares the rider's mouth volume (`RiderData+0x828`) against the candidate's volume at
   `EnemyData+0x45c`. Items/yakumono have no volume at that offset.
4. **Capture is EventActor-specific.** `EventActor_OnCapture` (`0x802038c4`) puts the actor
   into the captured state and writes EnemyData fields.
5. **The captured driver reads/writes EnemyData.** `EnemyState_InhaledFunc4` (`0x80203b64`,
   mouth-follow + shrink + destroy>120 frames) and `EnemyState_AnimTick` (`0x8020be1c`)
   read `attraction_mode +0xa10`, inhale scale `+0xb10`, etc. Feeding an `ItemData`/
   `YakumonoData` pointer here reads the wrong struct → garbage / crash.

**And in City Trial there's nothing for it to inhale anyway** — CT spawns no per-type AI
enemies, only event actors (Dyna Blade / TAC / Meteor) and CPU players, none flagged
inhalable. In fact `Rider_TryStartInhale` only enters the inhale state *when the overlap
test already found a target in range*, so in CT the vanilla inhale **never starts on its
own**.

So the suction is **entirely custom**: a cone scan and attraction driven by the mod (the
standalone-steering pattern the Waddle Dee Swarm event uses).

### Reusing the visual (`Rider_StartInhale`)

`Rider_StartInhale` (`0x801ad2c4`) is a clean, dependency-free lever. It does **not** check
the gate or require a target — call it directly on a rider and it:

- plays the active-suction animation (action-state `0x76`, anim `0x2f`),
- spawns the native suction **particle VFX** (`Effect_SpawnSync(..., 0x3a982, ...)`)
  anchored to the mouth bone,
- plays the inhale **SFX** (`0x20037`),
- installs the per-frame capture callbacks (which, in CT, harmlessly scan the empty
  EventActor bucket and capture nothing).

So to get the "hold B and Kirby keeps inhaling with the real open-mouth + suction effect"
look, the mod drives `Rider_StartInhale` while the trigger button is held, and runs its own
cone vacuum in parallel. See "Inhale animation (reuse the vanilla visual)" below.

## Native inhale reference

The vanilla inhale provides the animation/VFX hook (`Rider_StartInhale`) and defines the
engine's notion of "suction range."

- **It is the no-copy-ability default attack.** Gate `Rider_CanStartInhale` (`0x801a617c`):
  ```c
  attack_held   = (*(u8*)(rd + 0x820) >> 2) & 1;   // RiderData+0x820 bit 2 (0x04)
  no_ability    = *(int*)(rd + 0x454) == -1;        // copy-ability kind == -1
  mouth_open    = *(int*)(rd + 0x918) < 3;          // Rider_IsInhaleMouthFull: capture count < 3
  ```
  Note the gate is checked only by the *entry probe* — `Rider_StartInhale` itself does not
  check it, so a forced power-up can drive the visual even when Kirby holds an ability.
- **The inhale never self-starts in City Trial.** The entry probe `Rider_TryStartInhale`
  (`0x8019c5ac`) runs the gate, then scans the EventActor bucket and only calls
  `Rider_StartInhale` **if `HurtVolume_OverlapTest` already reports a target inside the
  mouth volume**. CT has no inhalable EventActors, so the probe always returns having
  captured nothing — the rider never enters the inhale state via the vanilla path. (This is
  why the mod must call `Rider_StartInhale` directly for the visual.)
- **Force lever: `Rider_StartInhale` (`0x801ad2c4`).** No gate, no target needed. Plays
  anim `0x2f`, spawns suction VFX (`Effect_SpawnSync(..., 0x3a982, ...)` at the mouth bone),
  plays SFX (`0x20037`), and installs the per-frame capture callbacks
  (`RiderData[500]/[0x1f5]`). The installed `Rider_InhaleCaptureScan` then runs each frame
  while the capture state `RiderData+0x918 == 0`; in CT it scans the empty EventActor bucket
  and is a no-op.
- **The inhale is three action-states that do NOT chain automatically.** `state_idx`
  (`RiderData+0x1c`, the anim id; the parallel action-state at `+0x28` runs `0x76`/`0x77`/`0x78`):
  `0x2f` suck START, `0x30` suck LOOP, `0x31` suck END.
  - **START (`0x2f`) is a one-shot gulp.** Its process `Rider_InhaleStartProc` (`0x801ad1dc`)
    just holds the anim while it plays, then on `Rider_IsBodyAnimDone` (`0x80198b00`) hands off to
    the generic ability-resolve / star-wait path (`zz_801a8454_` `0x801a8454`) — i.e. it returns
    the rider to **neutral**. It does **not** advance to the LOOP. A native tap is only ever this gulp.
  - **LOOP (`0x30`) is a separate state entered ONLY via `Rider_StartInhaleLoop` (`0x801ad4cc`).**
    Its process `Rider_InhaleLoopProc` (`0x801ad3a0`) re-enters the loop itself every time the body
    anim finishes, so the suck sustains and *animates* on its own. It self-terminates via a
    countdown at `RiderData+0x93C` (per-action-state scratch, **aliases `copy_wheel_result`**):
    `Rider_InhaleLoopTick` (`0x801ad550`) decrements it each LOOP frame while the mouth is empty
    and, at `0`, calls `Rider_EndInhale` (`0x801adf98`).
  - **END (`0x31`)** closes the mouth and spawns a close puff (`Effect 0x5a557`), then neutral.

  Because the engine never advances START → LOOP, a driven hold must call `Rider_StartInhaleLoop`
  to enter the loop after the gulp, top up `+0x93C` to keep the engine from timing it out, and
  call `Rider_EndInhale` on release. **`+0x93C` is not a trustworthy timer when driven** — it
  aliases `copy_wheel_result`, which other systems write, so the end is driven explicitly
  rather than left to the countdown.
- **Range knob = `RiderData+0x2c8` — but it does nothing for our targets.**
  `HurtVolume_OverlapTest` (`0x80189784`) sets the rider sphere's effective radius to
  `base_radius(vol+0x18) × (+0x2c8)`, so in vanilla a bigger Kirby has a bigger inhale.
  **But this sphere is only ever tested against EventActor candidates** (none in CT), so
  raising `+0x2c8` widens nothing we care about — and `+0x2c8` is also the model `base_scale`
  (model size = `base_scale(+0x2c8) × model_scale(+0x348)`), so bumping it would grow the
  model too. **Do not touch `+0x2c8`.** Our "greater range" is the custom cone constant,
  fully decoupled. The volume (`RiderData+0x828`) is a HurtData sphere/capsule re-anchored to
  a mouth bone each frame by `Trigger_UpdatePosition` (`0x8018a188`) from `zz_801a5f2c_`
  (`0x801a5f2c`) — **not** a cone.
- **Scan / capture**: entry probe `Rider_TryStartInhale` (`0x8019c5ac`); per-frame
  multi-capture `Rider_InhaleCaptureScan` (`0x8019c63c`, up to 3/frame, candidate list capped
  at 10) → `EventActor_OnCapture` (`0x802038c4`) → captured driver `EnemyState_InhaledFunc4`
  (`0x80203b64`). Held captures in `RiderData+0x8f0/+0x8f4/+0x8f8` (treated as `EnemyData`).
- **Per-frame callback slots** (installed by `Rider_StartInhale`, called by the native state
  machine while the inhale state is live): scan at `RiderData+0x7d4` (`[0x1f5]`, default
  `Rider_InhaleCaptureScan`), volume-update at `+0x7d0` (`[500]`, default `zz_801ad46c_`), and
  exit/cleanup at `+0x7e4` / `+0x7f8` (defaults `zz_801add18_` / `zz_801add38_`). The scan runs
  each frame while the capture state `RiderData+0x918 == 0`; in CT it walks the empty EventActor
  bucket (`stc_gobj_lookup+0x30`) and is a no-op.
- **Enemy.dat attraction params** (table at `*0x805dd878`, loaded by `Enemy_LoadCommonParams`
  `0x801fd580`): `+0x88` capture-proximity, `+0x8c` attraction range, `+0x7c` pull timing.
  Reference only — our custom pull uses its own tunables.

## Kirby 2× scale (reuse Big Kirby)

The AP "Big Kirby" filler already does exactly the visual we want. The rider model matrix
is rebuilt every frame by `Rider_ApplyModelMatrix` (`0x80190848`, via
`Rider_ModelMatrixThink` `0x8018f79c`, GObj proc priority 6) as:

```c
gmLanMenu_Scale3DObject(base_scale(+0x2c8) * model_scale(+0x348),
                        model_jobj, forward(+0x324), up(+0x330), pos(+0x300));
```

Writing `RiderData.model_scale` (`+0x348`) is the entire mechanism — no JObj poking; the engine's
`Rider_ApplyModelMatrix` consumes it every frame. Hypernova owns the ease itself: `TickScale`
(`hypernova.c`) smoothsteps `model_scale` from `HYPERNOVA_SCALE_NEUTRAL` (1.0) toward
`HYPERNOVA_SCALE_TARGET` (2.0) over `HYPERNOVA_SCALE_ANIM_FRAMES` on activate and back on
deactivate, writing the field directly each frame in `OnFrameEnd` (a direct set, not the AP "Big
Kirby" filler's multiplicative path). The field resets to 1.0 on scene change automatically
(models are recreated).

**Do not also bump `base_scale` (`+0x2c8`) for the 2× look** — because the model uses the
*product*, raising both fields would compound to 4×. Use `model_scale` for the visual; the
custom vacuum range is an independent constant (below), so we never need to touch `+0x2c8`
in City Trial.

## The custom vacuum pipeline

A per-player Hypernova proc runs each frame while the power-up is active. When the attack
button is held, it does a cone scan and pulls matching objects toward the rider.

### Cone scan

Origin and aim come from the rider:

- Rider GObj: `Ply_GetRiderGObj(player_idx)` (`0x8022cb74`); `RiderData = rider_gobj+0x2c`.
- `origin = RiderData.pos` (`+0x300`); `aim = RiderData.forward` (`+0x324`, normalized).

For each candidate at world position `p`:

```c
Vec3 d   = p - origin;
float dist = length(d);
if (dist > HYPERNOVA_RANGE) skip;
if (dist > 0 && dot(d / dist, aim) < cosf(HYPERNOVA_HALF_ANGLE)) skip;  // outside cone
// in cone -> pull this frame
```

`HYPERNOVA_RANGE` and the cone half-angle are tunable mod constants (the shipped values are a
175-unit reach and a 30° half-angle). This is the "greater range" — it is decoupled from the
vanilla `+0x2c8` volume.

### Items: scan → claim → pull → collect

> Items use the **same committed-claim model as yakumono** (below): the cone scan only
> *claims* an item (`Hypernova_ClaimItems` records its `ItemData` pointer + owner); the actual
> pull happens once per frame in `Hypernova_VacuumProcessClaimedItems`, which walks the claim
> set and pulls each item toward its owner regardless of cone membership, then collects it via
> the vanilla pickup. A claim is dropped the frame its item leaves the bucket (collected /
> despawned), so a dangling/reused pointer self-heals. This is what stops swept items being
> **left behind** when the aim drifts off them. Both items and props pull through the one
> shared `Hypernova_StepToward` step (`max(SPEED×dist, MIN)`), so they move at an identical rate.
>
> **Arc + spin (visual polish).** `Hypernova_StepToward` doesn't aim at the rider directly — it
> aims at a point lifted above the rider by a parabolic hump of the **horizontal** distance
> (`HYPERNOVA_ARC_HEIGHT × 4u(1-u)`, `u = hdist²/RANGE²`), so a swept target rises up and over
> instead of scraping along the terrain (always visible, never clipping the ground). The hump is
> keyed to horizontal distance and vanishes at both ends, so the lift falls to zero *at* the
> rider — the target always lands ON it for pickup/break and never hovers. Independently, each
> in-flight target **tumbles**: it rotates about an axis perpendicular to its travel direction
> (`travel × world-up`) by `HYPERNOVA_SPIN_RATE` rad/frame, with a per-target pseudo-random
> sign (hashed from its pointer). Items spin by rotating their `forward`/`up` vectors (which
> `CityItem_UpdatePosition` rebuilds the render matrix from); props spin by rotating the 3×3
> basis columns of their world matrix. Rotation is length-preserving, so it never disturbs the
> prop shrink (which keys off row magnitude). Both use `Vec3_RotateAboutUnitAxis` (`0x800638f8`).

**Enumerate** the live items via the p_link bucket (cleaner than the global GObj list — it
skips everything that isn't an item):

```c
for (GOBJ *g = ((GOBJ**)0x805de334)[13]; g; g = *(GOBJ**)(g + 0x8)) { /* item */ }
```
- Confirm `g->kind == 22 && g->p_link == 13`. `ItemData = g+0x2c`.
- **Skip boxes**: `ItemData.kind` (`+0x1c`) ≤ `ITKIND_BOXRED` (2), or `item_category`
  (`+0x24`) == 0. (Optionally require `CityItem_CanCollect` `0x80252df0`.)
- Live-item count mirror at `*(int*)0x805dd8cc` (for sanity, not required).

**Position** is `ItemData.pos` (`+0xdc`) — the field the matrix proc draws from
(`CityItem_UpdatePosition` `0x802503c0`, priority-6 proc `0x8024f848`). *Not* the land_pos
at `+0x1ac`.

**Pull** (each frame for every *claimed* item, in cone or not): write `pos` (`+0xdc`)
stepped toward the rider by `Hypernova_StepToward`, and neutralize the physics that would
fight it:
- zero `vel` (`+0xc4`) and `accel` (`+0xb8`) — `CityItem_PhysicsThink` (`0x8024f778`)
  integrates `pos += vel` every frame;
- set `is_airborne` (`+0x1d4`) = `-1` so `Item_GenericEnvColl` (`0x80255438`) skips the
  per-frame ground raycast/snap; clear grounded flag (`+0x35a` bit 4);
- optionally shrink `scale` (`+0xac`) toward 0 as it approaches (the matrix scale =
  `scale(+0xac) × attr_scale(+0x118)`); a shrunk item is still collectible.
- Write `pos` **after** the engine's procs for the frame (a late/high-priority proc) to avoid
  ordering fights with envcoll.

**Collect**: the cleanest path is *not* a manual destroy — pull the item all the way into
the rider/machine so the **vanilla pickup trigger fires naturally** (item `TriggerData` at
`+0x250` vs the machine), giving proper credit, pickup SFX, and effects. If a forced
collect is ever needed, fall back to the collect path behind `CityItem_CanCollect`
(`0x80252df0`); raw `GObj_Destroy` would vacuum the item away **without** crediting it.

### Yakumono: scan → break

CT breakables are **multi-instance**: one `YakumonoData` GObj manages N placed props, and the
parent itself has no usable transform — `pos` (`+0x1c`), `hsd_object` (`+0x28`), `model_jobj`
(`+0x64`), and `xform_jobj` (`+0x70`) are all NULL/zero (`GrYaku_Create` NULLs `+0x70` at
creation). The visible geometry lives in the **ground scene-instance pool**, not the GObj: each
placed prop is a `0x98`-byte record (`Yaku_GetInstancePool`, array at `grobj+0x64`) carrying its
own JObj (world matrix at `JObj+0x44`) and a back-pointer to its owning parent GObj at
`record+0x90`. So the vacuum moves the per-prop **records**, not the GObj — sidestepping the
per-family `+0x130` layout differences (trees/houses keep an array there, the star pole a single
pointer). `on_damage` (`+0x100`) is NULL for these families, so the break is collision-force
driven, not damage-driven.

**Skeleton-joint families.** The static families (houses 38, walls 36, holes 37, floor 32,
BigStar 29) have plain JObjs (flags `0x40008` at `JObj+0x14`) whose world matrix can be written
directly. The **weak** families (trees 34, rocks 35, coral 33) are `JOBJ_SKELETON` joints (flags
`0x9`): `HSD_JObjSetupMatrixSub` (`0x8040d6b4`) rebuilds their `JObj+0x44` from the joint SRT every
frame, clobbering a plain write. `Hypernova_PullInstance` sets `JOBJ_USER_DEFINED_MTX` (`0x800000`)
on each joint first (`JObj_SetFlags`), which makes that setup keep our matrix (idempotent for the
static families).

**Enumerate** in two steps: collect the breakable *parent* GObjs from the yakumono p_link bucket
(`(*stc_gobj_lookup)[8]`, chained via `gobj->next`; `gobj->kind == 15`, `YakumonoData = g+0x2c`,
breakable `desc_id`); then scan the scene-instance pool (`Yaku_GetInstancePool`, `grobj+0x64`,
count `grobj+0x68`) and keep each `0x98`-byte record whose owner (`record+0x90`) is one of those
parents. (Matching by pointer means we never deref a non-break record's `+0x90`.) The City Trial
breakables and their `desc_id` (`YakumonoData+0x04`) are listed under "City Trial target
inventory" below.

**How props break (collision force vs HP).** `collideWithObject` (`0x800f5004`)
reads the prop's `desc_id` (`+0x04`), indexes the 70-entry descriptor table at `0x804a5be8`,
and calls that descriptor's **`coll_func` at `+0x04`**: `coll_func(yaku_gobj, otherCollData,
…, regionIdx, …)`. The handler (`hitWeakObject` `0x80107914` / `hitStrongObject`
`0x801086d0` / `hitBreakableFloor` `0x80106bd0` / `hitBigStar` `0x80103eb8`) computes a
**force** = `otherCollData.radius (CollData+0x344) × impactSpeed²` and breaks when `force > HP`
(weak/BigStar: one-shot threshold; strong: subtractive per-region HP; floor: one crack-stage
per hit). There is no clean "deal N damage" entry that bypasses a collider, and seeding
`HurtData` + calling `GrYakumono_Proc10` does nothing because `on_damage` (`+0x100`) is NULL for
these families (it only accumulates damage into `+0xac`).

**`impactSpeed` is a normal projection, not `|delta|`.** `grScene_GetImpactSpeed` (`0x800d8edc`)
takes the collider's frame delta (`CollData+0x14`), projects it onto the contacted region's
outward normal (`region+0x0c`), **negates** the projection (scale const `0x805df634` = −1.0), and
clamps a non-positive result to **0** (the tail at `0x800d9150` returns `0.0` when the value is
`≤ 0`). So an impacting body's delta must point **into** the surface; a delta whose dot with the
outward normal is `≥ 0` reads as zero impact → `force = radius·0 − HP < 0` → **no break**.

**The regions can't be moved, so they're retired.** The engine break needs a *real* collision:
the rider's `mpColl_UpdateCollision` (`0x802485e0`) queries its coll sphere against the **baked
static map-collision mesh**, and its hit dispatchers (`mpResponse_DispatchSceneObjColl`
`0x80248bb4`, `mpColl_SphereSceneObjColl` `0x8024d414`) call `collideWithObject` per overlapping
scene region. A prop's mpColl regions are baked at its origin and are **not** repositioned when
its render matrix (`JObj+0x44`) moves, so a pulled-and-shrunk prop never overlaps the rider. The
regions can't be relocated either: they're a fixed contiguous slice of the **global region array**
(base at `*(stc_grobj+0x5c)`, exposed by the ground scene-collision holder `stc_grobj+0x54` at its
`+0x08`), found by spatial broadphase. So a moved prop's collision is **retired** instead
(`region+0x3c` bit 6 cleared via `grScene_SetInstanceColl(record, 0)`): the rider response
(`mpResponse_DispatchSceneObjColl`) drops any contact whose bit 6 is clear, and nothing re-arms it
per frame, so the clear sticks (no stranded invisible wall at the origin).

**The vacuum synthesizes the collision.** It is **committed**: a breakable prop is *claimed* the
moment the cone sweeps it (`Hypernova_ClaimYakumono`) and then advanced every frame in
`Hypernova_VacuumProcessClaimed` regardless of cone membership, so the cone can never strand a
half-shrunk prop. Per claimed prop, `Hypernova_PullInstance`:

- **Retires the prop's collision for the whole flight** (`grScene_SetInstanceColl(record, 0)`
  each frame) so the moved model leaves **no invisible wall at its origin**. It is re-armed only
  for the break instant (below).
- **Pulls** the prop toward the claiming rider via the shared `Hypernova_StepToward` step —
  `max(HYPERNOVA_PULL_SPEED×dist, HYPERNOVA_PULL_MIN)` world units, the **same constants items
  use** so props and items pull at an identical rate (the `MIN` floor outruns a fleeing rider;
  compared squared so it stays sqrt-free except for one `VECNormalize` on the final approach).
- **Shrinks** the world-matrix 3×3 by `HYPERNOVA_YAKU_SHRINK` per frame **only once within
  `HYPERNOVA_YAKU_SHRINK_RADIUS`** (the cached `record+0x2c` 3×3 keeps the load-time scale as
  the sqrt-free reference).
- **Breaks** when the prop arrives (within `HYPERNOVA_YAKU_BREAK_RADIUS`) **or** has shrunk past
  `HYPERNOVA_YAKU_BREAK_SCALE`× its original size, by calling `Hypernova_BreakInstanceNative`.

`Hypernova_BreakInstanceNative` re-arms the collision (`grScene_SetInstanceColl(record, 1)`,
so the family tail's "still collidable?" guard passes) and invokes
`collideWithObject(yaku_gobj, coll, holder, region_idx, contact)` with a **fabricated collider**:

- **`holder`** = the ground scene-collision holder `stc_grobj+0x54` (`+0x08` = the global
  region-array base `*(stc_grobj+0x5c)`, `+0x10`/`+0x14` = the record pool/count). **`region_idx`**
  = the global index of the prop's **first region with a usable (non-degenerate) normal** =
  `(record+0x0c − *(holder+0x08)) / 0x40 + k` (regions are a contiguous slice of the global
  array; count at `record+0x10`).
- **`coll`** = a zeroed `CollData`. `radius` (`+0x344`) is set astronomically so
  `force = radius × impactSpeed²` (`GrYaku_TestImpactBreak`/`GrYaku_ApplyImpactDamage`) far
  exceeds any prop HP → **one-hit break**. `pos_delta` (`+0x14`) is set to
  **`−normalize(region_normal) × M`** so it projects to a *positive* impact speed (see the
  normal-projection note above). `g` (`+0x04`) = the human rider GObj so `GrYakuBreak_GetAttackerPly`
  (`0x80105cb0`) credits the right player; `coll_info` (`+0x44`) = a zeroed `mpCollInfo` with
  `+0x1d0 = −1` ("no BigStar region") so `destroyBigStar` returns 0 and the break proceeds.
- The target region's `+0x34` bit `0x20` is temporarily cleared so `grScene_GetImpactSpeed`
  skips its geometry-refined path (which can rewrite a synthetic delta from the prop's
  matrices); restored after the call.

`collideWithObject` dispatches to the prop's family `coll_func`, and the visible break splits two
ways:

- **`hitStrongObject`** (walls 36 / holes 37 / **houses** 38): does the whole visible break
  **inline** at the passed contact point — retires collision, hides the mesh
  (`HSD_JObjSetFlagsAll`, gated on its `hp_block[9]`), spawns debris, and calls the per-desc
  **item-drop** handler from the `DAT_804a70b4` table (`GrYakuBreak*_DropItems`). Because it
  shatters *at the contact we pass* and hides its own model, a pulled-in house breaks correctly
  at the rider with nothing left behind.
- **`hitWeakObject`** (coral 33 / trees 34 / rocks 35): does **not** hide the original mesh inline
  (its `HSD_JObjSetFlagsAll` branch is gated on `hp_block[5]`, which is 0 for these), and its
  broken **state** (`GrYakuBreakColl_BrokenProc` `0x80107798`) is only teardown (waits for every
  prop in the family to break, then `GObj_Destroy`s the parent). So the *entire* visible break for
  a weak prop is its **debris effects** (`GrYaku_SpawnBreakEffect` → `Effect_SpawnSync`, one per
  sub-part). Each effect is positioned by `Gr_GetNodeWorldPos`, which resolves a **node id**
  (`entry+0x08`, per-instance) through grobj's node registry to a JObj and reads *that* JObj's
  world translation — a **separate node from the dragged instance JObj**, pinned at the prop's
  baked spot. (`Gr_GetNodeWorldPos` honors `USER_DEF_MTX`, which is the lever for relocating it;
  see below.) The weak `coll_func` also does not drop items — only the strong path runs the
  `DAT_804a70b4` drop table.

In every case `grScene_SetInstanceColl(record, 0)` retires collision, `GrYaku_IncrementBreakCount`
credits the checklist, and the family's `hp_block[remaining]` counter (`+0x134` weak / `+0x140`
strong, == the live intact-record count) decrements. Break success is detected directly: if
`grScene_IsInstanceCollAll(record, 1)` is false after the call, the tail fired (claim dropped);
otherwise the synthesize is left re-retired and the flight continues. A `HYPERNOVA_YAKU_CLAIM_TTL`
cap releases any claim that never breaks. The multi-stage floor (desc 32) advances one crack-stage
per call, so it may take a few frames in the break zone to fully open.

**Weak-family rubble — relocate the debris node.** Because the weak rubble is debris pinned to a
separate grobj node at the prop's baked spot, `Hypernova_BreakInstanceNative` does two extra things
for the weak family (detected via `coll_func == hitWeakObject`, `Hypernova_IsWeakBreakFamily`):
1. **Relocates the debris-anchor node onto the rider for the break instant.** `Hypernova_WeakDebrisNode`
   walks the same chain `hitWeakObject` uses — family break data (`YakumonoData->data_ptr`) →
   per-instance entry table → node id at `entry+0x08`, resolved through grobj's node registry
   (`grobj+0x104`) — to the debris JObj, sets its world-matrix translation to the rider's contact
   point, and sets `JOBJ_USER_DEFINED_MTX` so `Gr_GetNodeWorldPos` reads our translation instead of
   rebuilding it from the baked SRT. The effects spawn **synchronously** inside `collideWithObject`,
   so they capture the relocated position; the node's matrix + flags are saved and **restored**
   immediately after (the node may be shared across the family's instances).
2. **Collapses the dragged intact mesh after the break** by clearing `JOBJ_USER_DEFINED_MTX` on the
   instance JObj — dropping it to its degenerate local SRT so it doesn't linger frozen at the rider
   (the weak break never hides it itself). Strong families need neither step — they shatter+hide
   inline at the contact.

**Targeting.** The vacuum targets the breakable families — star pole (29), forest pitfall (32),
and the rock/tree/house families (33–38) — and skips the passive zones (desc 17/18) and the large
structures (Lighthouse 68, WhispyWoods 69). The full identity table is in
`docs/yakumono-system.md` ("City Trial breakable inventory"). Collision is retired at *claim* time
(not only during flight) and the claim cap is 200, so a swept-up prop goes non-solid immediately
and a wide cone can't starve later props of a claim slot.

## Activation / state model

Hypernova is a **timed power-up state**, tracked **per player** (`stc_active[5]` /
`stc_timer[5]`, with the model-scale ease also per player) so it can be granted to
one rider at a time or to everyone at once:

- A trigger sets a player's timer. The exported `HypernovaAPI` offers
  `ActivatePlayer(player, duration)` for a single slot and `Activate(duration)` for
  every human at once (the debug self-test and AP / EnergyLink triggers use the
  all-players form). The **Miracle Fruit** custom item grants it to its collector:
  `hypernova`'s boot registers a `custom_items` pickup handler that calls
  `ActivatePlayer` for the player who picks it up.
- Each frame while `timer > 0`: ease `model_scale` (`+0x348`) toward 2.0; if the attack
  button is held, run the cone scan + pull. On expiry: ease scale back to 1.0, stop the
  vacuum. The trigger is **`B`** (`HYPERNOVA_TRIGGER_BUTTON`), not `A` — `A` is the
  boost/charge button in Air Ride and would conflict with normal machine control.
- Per the project's mod conventions: a `HypernovaAPI` (header under `mods/hypernova/include/`) so
  other mods can trigger/extend it, and an `OptionDesc`-driven hoshi settings menu.

### Inhale animation (reuse the vanilla visual)

Tapping the trigger plays the full vanilla gulp; holding it sustains an open-mouth suck loop
(with native particle VFX + SFX and the custom cone vacuum running underneath); releasing lets
it end like a vanilla inhale. This is core behavior, not optional — `hypernova.c`'s `DriveInhale`
runs every frame while Hypernova is active.

The state machine doesn't chain the three inhale states by itself (see "Native inhale reference"):
START (`0x2f`) is a one-shot gulp that returns to neutral, the LOOP (`0x30`) is entered only via
`Rider_StartInhaleLoop` and animates itself, and the `+0x93C` countdown that would end the LOOP
aliases `copy_wheel_result` and can't be trusted when driven. So `DriveInhale` owns the whole
gesture with a tiny per-player phase machine (`IDLE → GULP → LOOP`):

```c
// each frame, per human, while Hypernova active (hypernova.c DriveInhale):
if (!held) {
  // released: end an active suck with the engine's own END; let a mere tap finish its gulp
  if (phase[p] == LOOP && rd->state_idx == 0x30) Rider_EndInhale(rd);
  phase[p] = IDLE;
} else switch (phase[p]) {
  case IDLE: Rider_StartInhale(rd);              phase[p] = GULP; break;   // gulp + VFX + SFX
  case GULP: if (rd->state_idx != 0x2f) {        // gulp finished -> promote into the suck loop
               Rider_StartInhaleLoop(rd);        phase[p] = LOOP;
             } break;
  case LOOP: if (rd->state_idx == 0x30)
               *(s32 *)((char *)rd + 0x93C) = 8; // HYPERNOVA_INHALE_TIMER_HOLD: don't time out
             else if (rd->state_idx != 0x31 && rd->state_idx != 0x2f)
               Rider_StartInhaleLoop(rd);        // engine dropped the loop while held; re-enter
}
if (held) Hypernova_VacuumPlayer(rd);            // our cone suction (separate)
```

`DriveInhale` runs in `OnFrameEnd`, after the rider think. The **tap** path: `Rider_StartInhale`
fires the gulp and the GULP phase deliberately leaves it alone, so the vanilla gulp plays its full
duration and the engine returns to neutral on its own. The **hold** path: when the gulp ends (the
engine has left `0x2f`) the GULP phase promotes into the suck LOOP *that same frame, before render*,
so the open mouth carries through with no flicker back to neutral; the LOOP phase then tops `+0x93C`
back up (the engine still animates the loop) and re-enters it if the engine ever drops out. The
**release** path calls `Rider_EndInhale` for the engine's own close-mouth → puff → neutral, so it
expires exactly like a vanilla inhale rather than cutting hard. `HOLD` is small (must be `≥ 2`, one
decrement lands before the next write); it only matters if the unreliable countdown is honored at all.

`Rider_StartInhale` ignores the no-ability gate and needs no target, so it works even when Kirby
holds a copy ability; the native capture scan it installs harmlessly no-ops in CT.

## Rainbow recolor while inhaling

Cycle Kirby through a rainbow hue while the inhale is held. The recolor is real-time and does not
touch the `.dat`; it drives the live model state.

**How Kirby's color works.** Kirby's body color is **texture-swap**, not a material color
register. The rider keeps a flat array at `RiderData+0x2c0` (one entry per material slot) whose
entries are **`hsd_tobj`** (texture objects). Each TObj holds an **array of
`ImageDesc` pointers** at `TObj+0x68` (one per color variant) and an `AObj` at `TObj+0x64`; the
AObj's playhead selects *which* texture variant is shown. The 8 player colors
(pink/yellow/blue/red/green/purple/brown/white) + wing/fire are just entries in that texture
array. `RiderKirby_SetMaterialColorAndUpdate(rd, part, idx)` walks `+0x2c0` and drives every
TObj's AObj to variant `idx`. The model tree itself is `hsd_jobj`; the inhale motion is
`rdMotionKirby[0x2f]` in `RdKirbyMotion.dat`.

The consequence: **there is no continuous color register to sweep.** Texture selection is
discrete — driving the AObj continuously snaps between the baked textures, it doesn't blend.
A true continuous "hue wheel" on the body texture is not natively available.

### The ColAnim color overlay (the recolor lever)

Texture-swap rules out a sweepable body color register, but Kirby has a **second** color
system: the **ColAnim** overlay (the candy/invincibility flash). It's a per-rider color that
the renderer blends over the textured model through a TEV color stage - so it tints whatever
texture is showing, with arbitrary RGBA. Each rider has three overlay slots in a block at
`RiderData+0x5c` (`+0x0` body, `+0xac`, `+0x158`); a per-frame selector (`ColAnim_Resolve`,
`0x8006ae7c`) picks the highest-priority active slot (`ColAnim_GetActiveSlot`, `0x8006ad20`),
copies its color into the slot's render-context, and a TEV-setup renderer (`ColAnim_SetupTev`,
`0x8006aaa4`) applies it. Within the body slot at `RiderData+0x5c`:

- `+0x08` — anim-data pointer (the per-frame animation reads this; NULL it to freeze the tick).
- `+0x28` (word 10) — current anim index (0 = inactive; nonzero = the selector treats it active).
- `+0x2c` — packed RGBA the selector copies into the render-context.
- `+0x30` — the live overlay color as **RGBA floats (0..255)**.
- `+0xa9` — priority byte. The selector renders the highest-priority **active** slot, and
  `ColAnim_Apply` is priority-gated (refuses a new anim while the current `+0xa9` is higher).
  Pinned to `0xff` so our overlay wins the selector and competing flashes are rejected.
- `+0xaa` — state-flags byte; bit `0x80` = **color-override active**. Every frame the selector
  re-clears the render draw-flag and only re-sets it (and re-copies `+0x2c` → `+0x224`) when this
  bit is set. The candy tick was what set it, so with the tick frozen the mod must hold it itself.
- `+0x224` — the packed render-context **RGBA bytes** the renderer reads.
- `+0x234` — ratio/blend enable byte (`0xff` = ratio path off; the selector sets it from `+0xa8`).
- `+0x235` — draw-flags byte (bit `0x80` = color-override render path on; selector-managed).

The built-in candy flash (index 3) is a green pulse (RGBA ≈ `128,255,128` at low alpha). Crucially
it **loops** - its per-frame tick keeps re-stamping the green into the slot color (`+0x2c`) and
maintaining the override bit (`+0xaa`) for as long as the slot is active. Left running, that
animation re-stamps its green over any color written to the slot and produces a blink. So
`DriveRainbow` **freezes the tick**: after applying the ColAnim, it NULLs the anim-data pointer at
`+0x08` (exactly what `ColAnim_Reset` does to that field). With the tick frozen, nothing in the
engine rewrites the slot color, so the mod owns it - but it must then maintain `+0xaa` bit `0x80`
itself, or the selector stops drawing the overlay after one frame (the rainbow vanishes almost
immediately).

**Surviving item-pickup flashes.** A pickup flash can wipe the rainbow two ways: it either
out-prioritizes the body state in the selector, or (same body state) the priority-gated
`ColAnim_Apply` lets a higher-priority flash overwrite it. `DriveRainbow` prevents both by **pinning
the body state's priority byte (`+0xa9`) to `0xff` every frame**: the selector always picks the
rainbow and the apply-gate rejects any competing flash. The pin is undone on Hypernova end
(`StopRainbow`'s `ColAnim_Reset` zeros `+0xa9`), so normal hurt/invincibility flashes resume.

### Driven HSV rainbow

`hypernova.c` (`DriveRainbow`) applies the candy ColAnim (index 3) **once** - purely to set the
overlay slot up - then freezes its tick and drives the color every frame with a smooth HSV hue:

```c
// each frame Hypernova is active (DriveRainbow), independent of the inhale button:
char *slot = (char *)rd + 0x5c;
if (((int *)slot)[10] != 3)        // index 3 not active yet
    Rider_ApplyColAnim(rd, 3, 0);  // set up the overlay slot
*(u32 *)(slot + 0x08) = 0;         // freeze the looping candy tick (idempotent)
slot[0xaa] |= 0x80;                // hold color-override active (selector needs this)
HueToRgb(hue, &r, &g, &b);         // hue advances 1/PERIOD per frame, shared by all riders
u32 packed = (r<<24)|(g<<16)|(b<<8)|ALPHA;
*(u32 *)(slot + 0x2c)  = packed;   // selector copies this -> +0x224 each frame
*(u32 *)(slot + 0x224) = packed;   // belt+suspenders for activation-frame timing
*(float*)(slot+0x30)=r; [+0x34]=g; [+0x38]=b; [+0x3c]=ALPHA;   // live float color
slot[0x235] |= 0x80;               // (selector also sets this; redundant but harmless)
slot[0x234]  = 0xff;               // ratio/blend path off
```

This runs in `OnFrameEnd`. With the candy tick frozen and `+0xaa` bit `0x80` held, the selector
copies our `+0x2c` color into the render-context every frame - a clean continuous rainbow, no flash,
no gameplay invincibility. Alpha (`HYPERNOVA_RAINBOW_ALPHA`) sets tint strength;
`HYPERNOVA_RAINBOW_PERIOD` sets cycle speed (frames per full hue wheel). When Hypernova ends,
`StopRainbow` calls `ColAnim_Reset` on each rider's body slot, switching the overlay off and
restoring the base color.

The rainbow runs for the **entire** active duration (not only while the inhale button is held),
since it's the power-up's signature look.

## The inhale whirlwind (swirl VFX)

The body rainbow above recolors *Kirby*. The swirling whirlwind/cone in front of the mouth
during inhale is a **separate object** not reachable by the same lever, recolored to the shared
rainbow hue by its own per-frame pass (`RecolorWhirlwinds` in `hypernova.c`). What it is and how
the recolor works:

**What it is.** `Rider_StartInhale` spawns it via `Effect_SpawnSync(parent = rider GObj,
id = 0x3a982, …)` (`0x80236c40`) and **discards the handle** (fire-and-forget, nothing on the
rider points back to it). (The `0xda`=218 passed alongside the id is a spawn-variant *selector*
inside `Effect_SpawnSync`, **not** a frame budget.) It is a standalone **GObj carrying a JObj
model tree**, positioned at the mouth bone each frame:

- render callback `GObj+0x1c (gx_cb) = 0x8023dfe0` (a thin wrapper around `JObj_GX`), destructor
  `GObj+0x30 = 0x80233ddc` — both in the Effect module.
- It is **not** a C++ `ModelEffect`/`EffectHandle` (those `ObjCollect` lists are empty during
  inhale) and **not** a point-particle (see below).
- Locate it live: enumerate GObjs (lookup base `*(0x805de334)`, per-`p_link` lists walked via
  `GObj+0x8`) and match `gx_cb == 0x8023dfe0` + proximity to the mouth (rider `GObj+0x28` = root
  JObj; JObj world translation at `+0x50/+0x60/+0x70`). It re-spawns at a **new heap address every
  inhale**, so any address must be re-derived, not cached.
- It is a real JObj model: scaling local scale (`JObj+0x24/+0x28/+0x2c`) and/or the world matrix
  (`JObj+0x44`, 3×4) visibly grows it. It has at least two sub-parts (an outer body + a fainter
  central cone), each its own MObj.

**Finding the live instances.** The spawn discards the handle, so `RecolorWhirlwinds` walks the
model-effect GObj bucket (`(*stc_gobj_lookup)[16]`, `p_link` 16) and matches each effect's kind
word — the engine's `Effect` state (GObj userdata, `GObj+0x2c`) carries the effect id at `+0x04`,
so `Effect.kind == 0x3a982` selects exactly the inhale whirlwinds (one per inhaling rider, all
driven to the same hue). The model root is `GObj+0x28`.

**Lifetime: don't touch it.** The mod only recolors — the whirlwind keeps its own native lifetime
and animation. `Effect.life` (`+0x0c`) is **not** a plain despawn countdown — it drives the
effect's playback, so writing it freezes the whirlwind's animation; `0xda`=218 is a spawn selector,
not a life. The lifetime is left entirely to the engine (do NOT write `Effect.life`). If a
sustained suck ever outlives the native whirlwind, re-spawn a fresh one rather than pinning `life`.

**How the recolor works.** The color is **not** in the material color registers — writing the
`HSD_Material` ambient/diffuse/specular (`MObj+0x0c → mat`) has no visible effect. The rendered
color comes from a compiled TEV color expression that `MObjSetupTev` (`0x803faba0`) rebuilds every
render frame, but the literal RGBA does **not** live in that expression tree — the tree's constant
node just holds a *pointer* to the color, targeting the TObj's `_HSD_TObjTev` struct (`TObj+0xA8`).
So the color lives in plain `GXColor` value fields the texture-animation system never touches:
`tev->constant` (`+0x10`) and `tev->tev0` (`+0x14`). The inhale model's combiner is
`out = ZERO + lerp(tev0, constant, texC)` — the texture brightness blends `tev0` (dark texels) with
`constant` (bright texels), so the mod rewrites the **RGB of both** on every part, **preserving each
register's alpha** (the alpha equation is `constant.a * TEXA` — the texture's per-texel alpha is the
swirl shape and `constant.a` a global multiplier — so leaving alpha untouched keeps the whirlwind at
its **vanilla opacity**; clobbering it changes translucency, which is *not* what we want). To avoid a
solid-color repaint, the rainbow RGB is first blended toward white by `HYPERNOVA_WHIRLWIND_TINT`
(0..1) so it lands as a soft wash over the full-opacity swirl rather than fully replacing its color.
`tev1` is unused by this asset. The walk
recurses the whole JObj tree (child + sibling) since the sub-parts (outer body, central cone) are
separate joints, each its own MObj with its own `tev`. It never writes into the `HSD_TExp` node tree
itself — node `+0x04` (list link) and `+0x08` (the color *pointer*) are live, and clobbering either
crashes the walk. See `effects-system.md` for the full TEV path and the selector encoding.

(The generic point-particle pool — machine exhaust, sparkles — is separately recolorable via its
own per-particle color fields, but the whirlwind has **no** point-particle component, so that
lever does not reach it. See `particle-system.md`.)

**Alternative: custom swirl.** The in-place recolor above gives color but no scale/shape control.
If Hypernova later needs a differently-sized or differently-shaped swirl, the alternative is to
spawn our **own** effect at the mouth bone each frame — reusing the body's `HueToRgb`/`stc_hue`
hue driver and our own small JObj model — which owns its handle and gives full color **and** scale
control.

## Symbols & offsets reference

### Native inhale (rider side)

Named in `GKYE01.map` / Ghidra; `Rider_StartInhale` + the gate/probe/scan/predicate are in
`link.ld` + `rider.h` (callable from mod code).

| Symbol | Address | Role |
|---|---|---|
| `Rider_StartInhale` | `0x801ad2c4` | **force into suck START (anim 0x2f, action-state 0x76)**: + suction VFX (Effect 0x3a982) + SFX (0x20037); installs capture callbacks. No gate/target check. One-shot gulp — returns to neutral when the anim ends, does **not** advance to the LOOP. |
| `Rider_StartInhaleLoop` | `0x801ad4cc` | **enter/re-enter suck LOOP (anim 0x30, action-state 0x77)**: reinstalls scan/volume callbacks. No VFX/SFX respawn, no capture reset. The LOOP process calls this itself on body-anim-done to animate/sustain; this is also the **only** way the LOOP is ever entered (the engine never advances START → LOOP). |
| `Rider_IsBodyAnimDone` | `0x80198b00` | 1 once the body motion has played to its end (per-part HSD check `0x80054798`); gates the LOOP process's per-cycle re-entry of suck-LOOP `0x30`. |
| `Rider_InhaleStartProc` | `0x801ad1dc` | suck-START (`0x76`) action-state process: holds the gulp anim, then on body-anim-done hands off to the generic ability-resolve / star-wait path (`zz_801a8454_`) → neutral. Does **not** go to LOOP. |
| `Rider_InhaleLoopProc` | `0x801ad3a0` | suck-LOOP (`0x77`) action-state process: runs `Rider_InhaleLoopTick`; on body-anim-done re-enters the loop (`Rider_StartInhaleLoop`) unless the tick ended it. |
| `Rider_InhaleLoopTick` | `0x801ad550` | per-LOOP-frame: pulls/swallows captured objects, else decrements the `+0x93C` countdown; returns 1 (→ `Rider_EndInhale`) when the gesture should end. |
| `Rider_EndInhale` | `0x801adf98` | ends the gesture: `RiderStateChange(..., 0x31, ...)` (suck END → neutral). |
| `Rider_InhaleCaptureCount` | `0x801adf5c` | counts the non-null capture slots (`+0x8f0/+0x8f4/+0x8f8`); 0 = mouth empty, so the tick runs the countdown. |
| `Rider_CanStartInhale` | `0x801a617c` | gate: attack bit (`+0x820` bit2) + `copy_kind==-1` + mouth not full (`+0x918 < 3`, via `Rider_IsInhaleMouthFull` `0x801adf40`) |
| `Rider_TryStartInhale` | `0x8019c5ac` | entry probe — only calls `Rider_StartInhale` if a target already overlaps |
| `Rider_InhaleCaptureScan` | `0x8019c63c` | per-frame multi-capture scan (EventActor bucket only) |
| `EventActor_IsInhalable` | `0x802041c8` | candidate predicate (EventActor enemies only) |
| `HurtVolume_OverlapTest` | `0x80189784` | sphere/capsule overlap; rider radius = `base×(+0x2c8)` |
| `Trigger_UpdatePosition` | `0x8018a188` | re-anchors mouth volume |
| `EventActor_OnCapture` | `0x802038c4` | put actor into captured state |
| `EnemyState_AnimTick` | `0x8020be1c` | attraction steering |
| `EnemyState_InhaledFunc4` | `0x80203b64` | mouth-follow + shrink + destroy>120 |

RiderData: input word `+0x820` (bit 2 = attack), copy kind `+0x454` (-1 = none), reach /
base_scale `+0x2c8`, suction volume `+0x828`, capture slots `+0x8f0/+0x8f4/+0x8f8`,
capture state `+0x918`, inhale LOOP countdown `+0x93C` (aliases `copy_wheel_result` — unreliable
when driven; decremented each LOOP frame while the mouth is empty, 0 → `Rider_EndInhale`). Enemy.dat
params `*0x805dd878 +0x7c/+0x88/+0x8c`.

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
to freeze the tick and own the color (see "ColAnim color overlay" above).

RiderData (recolor): body model JObj root `*(*(+0x2b0))`, material/render-object array
`+0x2c0` (`dobj_lookup_arr`), `model_part[3]` at `+0x42a` (`cur_mat_index`/`original_mat_index`),
body ColAnim state `+0x5c`, glow ColAnim state `+0x108`. Material color = `HSD_Material`
(ambient/diffuse/specular `GXColor`) reached via `dobj_lookup_arr[i] → MObj → mat`.

### Whirlwind recolor (model effect)

Uses `obj.h` + `effect.h` structs (no new callable functions). The inhale VFX is a model-effect
GObj: `entity_class` 25, `p_link` 16, render cb `GObj+0x1c == 0x8023dfe0`, model root `GObj+0x28`,
`Effect` userdata `GObj+0x2c` with `kind` at `+0x04` (`== 0x3a982` for the inhale suction). Match
on `kind` only and **recolor** — do not write `Effect.life` (`+0x0c`); it drives the effect's
animation, so pinning it freezes the whirlwind. Color per part: `JObj → DObj → MObj → TObj → tev` (`_HSD_TObjTev` at
`TObj+0xA8`); rewrite the RGB of `tev->constant` (`+0x10`) and `tev->tev0` (`+0x14`), preserving
alpha. Bucket head: `(*stc_gobj_lookup)[16]`, chained via `GObj+0x8`. See `effects-system.md` for
the TEV path.

### Items

| Symbol | Address | Role |
|---|---|---|
| `CityItem_Create` | `0x8024eef4` | spawns item GObj (kind 22, p_link 13) |
| `CityItem_PhysicsThink` | `0x8024f778` | `pos += vel` each frame (pri 4) |
| `CityItem_EnvColl` → `Item_GenericEnvColl` | `0x8024f814` → `0x80255438` | ground snap / collision (pri 5) |
| `CityItem_UpdatePosition` | `0x802503c0` | builds matrix from pos/forward/up/scale (pri-6 proc `0x8024f848`) |
| `CityItem_LifetimeThink` | `0x8024fa38` | timeout/OOB despawn, count bookkeeping |
| `CityItem_Destructor` | `0x8024fc1c` | full cleanup (frees CollData via `0x80254404`) |
| `CityItem_CanCollect` | `0x80252df0` | collectible test |
| `GObj_Destroy` | `0x80428f64` | destroy a GObj |

ItemData (`gobj+0x2c`): item_gobj `0x0`, parent_gobj `0x4`, child_gobjs[4] `0x8`, kind
`0x1c`, item_category `0x24`, lifetime `0x44`, scale `0xac` (base `0xa8`, attr `0x118`),
accel `0xb8`, vel `0xc4`, pos_delta `0xd0`, **pos `0xdc`**, prev_pos `0xe8`, forward
`0x100`, up `0x10c`, coll_data `0x1a4`, is_airborne `0x1d4`, TriggerData `0x250`, coll_kind
`0x359` bits 2-4, flags `0x35a` (bit4 grounded, bits5/6 collect gates). Item list head
`((GOBJ**)0x805de334)[13]`, next `+0x8`, live count `*(int*)0x805dd8cc`.

### Yakumono

| Symbol | Address | Role |
|---|---|---|
| `grYakuCheckGObjYakumono` | `0x800f7a50` | `gobj->kind == 15` test |
| `GrYakumono_GetDescId` | `0x800f7a64` | reads `desc_id` (`+0x04`) — identity / targeting |
| `GrYakumono_GetState` | `0x800f7ab8` | reads `state` (`+0x74`) |
| `collideWithObject` | `0x800f5004` | `coll_func` dispatch: `stc_yaku_descs[desc]->+0x04(yaku, otherCollData, …)` — the real break entry |
| `hitWeakObject` / `hitStrongObject` | `0x80107914` / `0x801086d0` | weak (one-shot, defers visible model to a broken-state swap) / strong (subtractive HP, full visible break inline) `coll_func`s (desc 33-35 / 36-38) |
| `hitBreakableFloor` / `hitBigStar` | `0x80106bd0` / `0x80103eb8` | floor multi-stage (desc 32) / star pole (desc 29) `coll_func`s |
| `GrYaku_SpawnBreakEffect` | `0x800f7044` | per-sub-part break debris/dust emitter (`Effect_SpawnSync` by sub-part kind). Positions each effect via a grobj node (`entry+0x08` id → `Gr_GetNodeWorldPos`), a *separate* node pinned at the prop's baked spot — Hypernova temporarily relocates that node (it honors `USER_DEF_MTX`) onto the rider so the rubble erupts at Kirby. Called by both weak and strong tails |
| `GrYakuBreakColl_BrokenProc` | `0x80107798` | weak family's broken-state (state 1) proc — teardown only: waits for all the family's props to break (`hp_block+0x134 < 1`), then `GObj_Destroy`s the parent. Does NOT animate individual props |
| `mpColl_UpdateCollision` | `0x802485e0` | rider/machine per-frame mpColl query vs the static map mesh; dispatches scene-object hits (the natural break path the synthesis stands in for) |
| `mpResponse_DispatchSceneObjColl` / `mpColl_SphereSceneObjColl` | `0x80248bb4` / `0x8024d414` | the two `collideWithObject` callers. Response **honors `region+0x3c` bit 6** (drops contacts whose bit is clear — the lever that makes a retired prop pass-through); sweep finds contacts geometrically and gates `collideWithObject` on `zz_80241574_` (`collider+0x34c` bit 2 AND `record+0x8c == 3`). Holder is `stc_grobj+0x54` (region base `*(holder+8)` = `*(stc_grobj+0x5c)`); `region_idx = (record+0x0c − *(holder+8))/0x40` |
| `grScene_GetImpactSpeed` | `0x800d8edc` | impact-speed helper inside the force calc: projects `delta` (`coll+0x14`) onto `region+0x0c` normal, **negates** (scale `0x805df634` = −1.0), clamps `≤0` → 0 (so delta must point *into* the surface); asserts `region+0x3c` bit 7; geometry-refined path gated on `region+0x34` bit `0x20` (cleared during synthesis) |
| `destroyBigStar` | `0x800d7b8c` | break gate: walks the collider's `coll+0x44` mpCollInfo for a BigStar region overlap; returns non-zero (skipping the break) iff found. A zeroed collInfo with `+0x1d0 = −1` returns 0 → break proceeds |
| `GrYakuBreak_GetAttackerPly` | `0x80105cb0` | maps the impacting collider's GObj (`coll+0x04`) → player index for the break-count credit (rider/machine/other) |
| `grScene_SetInstanceColl` | `0x800d7ad0` | sets/clears the collidable bit (`region+0x3c` bit 6) on **every** mpColl region of a scene-instance record; the family `coll_func` calls `(record, 0)` to retire collision on break |
| `grScene_IsInstanceCollAll` | `0x800d7b0c` | returns 1 iff every region's collidable bit == the arg (the break path's "still intact?" guard; also our "consumed?" check) |
| `GrYaku_IncrementBreakCount` | `0x80105d80` | `(yaku_gobj, player_idx)` → credits `Ply_IncrementYakumonoBreakCount(player_idx, desc_id)` (the checklist break-count; called inside the family `coll_func`) |
| `GrYaku_InitMatrix` | `0x800f73fc` | rebuilds JObj world matrix from JObj local T/R/S (gated by `GrYakumono_Proc4` `0x800f52e8` on `+0x12c` bit 7) |
| `GrYakumono_Proc7` | `0x800f53a8` | per-frame `HurtData_UpdatePerFrame(scale=+0xa4)` — the live hurtbox-scale consumer |
| `GObj_Destroy` | `0x80428f64` | despawn (runs `GrYaku_DestroyCallback` `0x800f4f0c` → frees HurtData; collision-safe) |

YakumonoData (`gobj+0x2c`): gobj backref `0x0`, **desc_id `0x04`** (= break-count stat-index),
data_ptr `0x08`, **pos `0x1c`** (single-instance world pos; **0,0,0 for break families**),
model JObj `0x64` (**NULL for break families**), `xform_jobj` `0x70` (**NULL for static/break
props**), state `0x74`, **scale `0xa4`** (`Gr_DefaultScale` 1.0 — hurtbox scale, not model),
damage accumulator `0xac`, HurtData `0xec` (hit-gate `+0x24`, damage `+0x28`), proc slots
`0xf0`-`0x108` (**`+0x100` on_damage is NULL for CT break families**), effect group `0x10c`,
**flags byte `0x12c` (bit 7 `0x80` = matrix-dirty)**, **child-prop array `0x130`** (break
families: the per-prop scene-instance records). The "Move = JObj local-T (`JObj+0x10`) + set
`+0x12c` bit 7; visual scale = JObj local scale; hurtbox scale = `+0xa4`" recipe is for
**single-instance** yakumono; for break families operate on each child record's JObj instead.
`stc_grobj+0x6fc` is the live **GObj** count (~31 in CT), not the prop count.

**City Trial target inventory** (desc_id → object; full table + identity sourcing in
`docs/yakumono-system.md`): 29 = **star pole**, 32 = **forest pitfall**, 34 = **forest
trees**, 35 = **volcano + high-plains rocks**, 37 = **volcano-base holes**, 38 = **dilapidated
houses**; 33 = candidate **coral**, 36 = candidate **volcano rock walls**. Exclude as targets:
17/18 (passive zones), 46 (gondola, ×16), 61 (decorative ×2),
68 (Lighthouse), 69 (WhispyWoods).

### Scale (Big Kirby reuse)

| Symbol | Address | Role |
|---|---|---|
| `Rider_ApplyModelMatrix` | `0x80190848` | model matrix from `base_scale × model_scale` |
| `Rider_ModelMatrixThink` | `0x8018f79c` | per-frame proc (pri 6) |
| `Ply_GetRiderGObj` | `0x8022cb74` | player_idx → rider GObj |

RiderData: pos `+0x300`, forward `+0x324`, up `+0x330`, base_scale `+0x2c8`, **model_scale
`+0x348`** (write this for 2×).

## Debug: inhale-cone visualizer

A debug-only overlay (`hypernova_debug.c`, "Debug Cone" menu toggle, off by default)
draws a **lightly opaque red cone** in world space showing the suction region's reach and
angle against the real items and props in front of the rider. It is decoupled from the
power-up: whenever the toggle is on and a human rider has a usable forward vector, the cone
is drawn — Hypernova does not need to be active.

**Same inputs as the suction.** The cone uses exactly what `Hypernova_InCone` uses, so what
you see is what gets vacuumed:

- apex = `RiderData.pos` (`+0x300`), axis = normalized `RiderData.forward` (`+0x324`),
- reach = `HYPERNOVA_RANGE`, half-angle from `HYPERNOVA_HALF_ANGLE_COS`.

The lateral surface is the exact half-angle cone; the flat base sits at the forward reach
(axial distance = `HYPERNOVA_RANGE`), so the tip-to-base length shows the true forward reach.
Base radius = `reach × tan(half-angle)` via the companion constant `HYPERNOVA_HALF_ANGLE_TAN`
(this freestanding build has no linkable `sqrtf`; the half-angle is a fixed design constant,
so its tangent is precomputed and must track the cosine). The base circle is walked by
incremental 2D rotation (15° steps) rather than `sinf`/`cosf`. The far cap of the true volume
is actually spherical (the test is `dist ≤ reach` ∧ `angle ≤ half-angle`); the flat-base cone
is the simple faithful-enough approximation — its lateral surface and on-axis reach are exact,
it only slightly over-extends past the sphere near the rim.

**Rendering.** Immediate-mode GX on the world camera's 3D link (GX link 0 — the link the
stage/rider models draw on, so the cone lives in the scene and is occluded by closer solid
geometry), modelled on hoshi's `GX_DrawLine`/`GX_DrawRect` inlines: `HSD_StateInitDirect`,
flat vertex color via a single `GX_PASSCLR` TEV stage, `GXLoadPosMtxImm(COBJ_GetCurrent()->
view_mtx)` for the world-space position matrix. Added on top for translucency: alpha-blend
(`GX_BL_SRCALPHA`/`GX_BL_INVSRCALPHA`), Z-test on with Z-write off, and `GX_CULL_NONE` so both
faces draw (the cone reads as a see-through volume). Drawn on the **XLU pass** (pass 1) so it
blends over already-rendered opaque world geometry; the render loop (`CObj_RenderGXLinks`)
invokes the GX callback once per pass (0 = OPA, 1 = XLU, 2 = additional).

**Lifecycle.** A standalone render GObj (`GObj_Create` + `GObj_AddGXLink`, no proc/model)
carries the GX callback. It is created lazily once per City Trial session (from `OnFrameEnd`)
and persists; the callback is a no-op while the toggle is off. World GObjs are freed by the
engine on scene teardown, so the mod only caches the handle to avoid recreating it and forgets
it (never destroys it) on the scene/leave-CT reset path — a manual destroy would risk a double
free. Tuning constants live in `hypernova.h` (`HYPERNOVA_DEBUG_CONE_RGBA`, `..._CONE_SEGS`,
`..._GX_LINK`).
