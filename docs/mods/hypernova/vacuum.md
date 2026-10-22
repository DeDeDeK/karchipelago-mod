# Hypernova Vacuum

The suction half of the Hypernova power-up: while a player holds the inhale, everything inside a
cone in front of them is claimed and pulled in - power-up items to be collected, breakable props
to shatter at the rider, unridden machines to explode. None of it goes through the vanilla inhale,
which only ever captures EventActor enemies. Each target class is moved by writing its position
directly, against whatever engine code would move it back, and finished through the engine's own
path so the game credits it normally.

## Claims and the shared pull

The vacuum separates claiming from moving. On a frame a player's inhale is being driven, every
target inside that player's cone is **claimed** - recorded with the claiming player as its owner.
Every frame Hypernova is active, every claim is advanced, whether or not it is still in the cone
and whether or not the button is still held. A swept target is never stranded halfway, and only new
claims need an active suck.

The cone's apex is `RiderData.pos` and its axis the normalized `RiderData.forward`; a facing
shorter than 0.01 skips the scan for that frame. A target is in when it lies within 175 units, in
front of the rider, and within 30 degrees of the axis. The test is done on squared magnitudes
(`dot^2 >= cos^2 * dist^2`) with no square root, and a target essentially on top of the rider is
always in.

All three classes approach through one step: each frame a target moves toward the rider by
`max(0.030 * dist, 2.0)` units, so it closes fast from far away and the 2.0 floor outruns a fleeing
rider. It aims not at the rider but at a point lifted above them by a parabolic hump of the
horizontal distance, peaking at 30 units halfway out and falling to zero at the rider, so a target
arcs up and over the terrain and still lands on the rider rather than hovering above them.

Items and props also tumble end over end, 0.08 radians a frame about the horizontal axis
perpendicular to their travel, in a direction hashed from the target's pointer.
`Vec3_RotateAboutUnitAxis` (0x800638f8) does the rotation: an item's `forward` and `up`, which
`CityItem_UpdatePosition` (0x802503c0) rebuilds its render matrix from, and a prop's world-matrix
basis columns. Rotation preserves length, so it does not disturb the prop shrink below. Machines
do not tumble.

An item or machine claim is revalidated every frame by finding its pointer in the live p_link
list before anything reads it, so a collected, despawned or reused target drops out of the claim
set rather than being dereferenced; a prop claim names a record in the stage's pool, which lives
for the whole scene. A claim whose owner's rider GObj is gone is released.

## Items

Items are walked on the item p_link (`GAMEPLINK_ITEM`) as `GAMEENTITY_ITEM` GObjs. Boxes are
skipped: `ItemData.item_category` is 0 for a box and non-zero for a power-up. The position is
`ItemData.pos`, the field the item's matrix proc draws from, not `land_pos`.

Each frame the pull steps `pos`, then neutralizes what would fight it: velocity is zeroed, since
`CityItem_PhysicsThink` (0x8024f778) integrates `pos += vel`; `is_airborne` is set to -1, which
makes `Item_GenericEnvColl` (0x80255438) skip its ground raycast and snap; and the grounded flag is
cleared.

Collection is left to the engine. The item is pulled all the way into the rider, so the vanilla
pickup trigger fires with its credit, sound and effect; destroying the item instead would remove it
uncredited. The `-1` has to be undone, since nothing in the engine writes `is_airborne` back: every
path that drops a live item claim calls `Item_SetAirborne` (0x80254cd8), so the item falls and lands
rather than hanging in the air where it was let go.

## Breakable props

### Where a prop lives

City Trial's breakables are multi-instance. One `YakumonoData` GObj manages every placed prop of a
family and has no usable transform of its own. Each placed prop is a `GrCollRecord` in the stage's
collision record pool (`Gr_GetCollRecords`), carrying its own `jobj` - whose world matrix is
`JOBJ.rotMtx` - a `world` matrix copy, its slice of the global triangle array (`tri_begin`,
`tri_num`) and a `yaku_gobj` pointer back to its family. The vacuum moves records, not GObjs.

Candidates are found in two steps: the breakable family GObjs are collected from the yakumono
p_link (`GAMEPLINK_YAKUMONO`, entity class `YAKUMONO_GOBJ_KIND`), then the record pool is scanned
for records whose `yaku_gobj` is one of them. Matching by pointer means the owner field of a
record that belongs to no breakable is never trusted. The cone test runs before the claimed-set
lookup and the collidable check, since it is the only cheap one of the three.

| `YakuKind` | Family | `coll_func` |
|---|---|---|
| 29 `YAKUKIND_STARPOLE` | Star pole | `hitBigStar` (0x80103eb8) |
| 32 `YAKUKIND_BREAKFLOOR` | Forest pitfall | `hitBreakableFloor` (0x80106bd0) |
| 33 `YAKUKIND_CORAL`, 34 `YAKUKIND_TREE`, 35 `YAKUKIND_ROCK` | Coral, trees, rocks | `hitWeakObject` (0x80107914) |
| 36 `YAKUKIND_BREAKHPCOLLDOOR`, 37 `..._HOLE`, 38 `..._HOUSE` | Rock walls, holes, houses | `hitStrongObject` (0x801086d0) |

Nothing else is a target - passive zones, the gondola, decoration, the lighthouse and Whispy Woods
are left alone.

### Moving one

A prop's joint is pulled by writing the translation of its world matrix, after setting
`JOBJ_USER_DEFINED_MTX` so the per-frame rebuild keeps the written matrix. The weak families'
joints are `JOBJ_SKELETON` joints that `JObj_SetupMtxSub` (0x8040d6b4) would otherwise rebuild
from their SRT every frame; for the static families the flag changes nothing.

A prop's collision cannot follow it. Its triangles are a fixed slice of the global triangle array,
baked at its placed spot and found by spatial broadphase, so they can be neither moved nor
relocated. The claim therefore **retires** them - `grScene_SetInstanceColl(record, 0)` clears
`GRCOLL_STATE_COLLIDABLE` - at claim time and again every frame of the flight, so the player cannot
run into a swept prop in flight and no invisible wall is left at its old spot. A record is only
claimed while `grScene_IsInstanceCollAll(record, 1)` says it is still fully collidable.

Within 22 units of the rider the prop shrinks to 70% a frame. Only the translation of
`GrCollRecord.world` is ever overwritten, so its 3x3 keeps the load-time scale and serves as the
reference. The prop breaks once it is within 8 units of the rider or has shrunk below 20% of that
size.

### Breaking one

A prop breaks only through a collision. `collideWithObject` (0x800f5004) looks up the prop's
`YakuDesc` in `stc_yaku_descs` by its kind and calls the descriptor's
`coll_func(yaku_gobj, coll, gcp, tri_idx, contact)`. The family handlers weigh a force,
`CollData.radius * impactSpeed^2` (`GrYaku_TestImpactBreak` (0x80104cd4),
`GrYaku_ApplyImpactDamage` (0x80104be0)), against the prop's HP: a one-shot threshold for the weak
families and the star pole, subtractive HP for the strong ones, one crack stage per hit for the
pitfall. There is no damage entry that skips the collider - `on_damage_callback` is NULL for these
families.

`impactSpeed` comes from `grScene_GetImpactSpeed` (0x800d8edc): the collider's frame delta
(`CollData.pos_delta`) projected onto the contacted triangle's outward normal and negated, with a
non-positive result clamped to 0. A delta must point into the surface to count.

So the vacuum breaks a prop with hoshi's `Gr_BreakInstance`, which calls `collideWithObject`
itself with a fabricated collider:

- the prop's collision is re-armed for the call, so the family's "still collidable?" guard passes;
- `gcp` is the stage's live `GrCollParam` (`Gr_GetCollParam`), and `tri_idx` the global index of
  the prop's first triangle with a usable normal - a prop with none is not broken;
- the collider is a zeroed `CollData` with a `radius` of 1e9, so the force beats any HP in one
  hit, and a `pos_delta` of 100 units against the triangle's normal, so the impact speed is
  positive;
- `coll.g` is the claiming player's rider GObj, so `GrYakuBreak_GetAttackerPly` (0x80105cb0)
  credits that player, and its `mpCollInfo` has `contact_tri_id = -1`, meaning no star-pole
  contact, so `destroyBigStar` (0x800d7b8c) returns 0 and the break proceeds;
- the triangle's `GRCOLL_FLAG_MOVING` bit is cleared for the call and restored after, so
  `grScene_GetImpactSpeed` skips its moving-record path, which would rewrite the delta from the
  record's matrices;
- the contact point is the prop's current, pulled-in position.

On a break the family's own tail retires the collision, credits the break count
(`YakumonoGObj_IncrementBreakCount` (0x80105d80)) and decrements the family's remaining count, so
the vacuum detects success as `grScene_IsInstanceCollAll(record, 1)` turning false. Otherwise the
collision is retired again and the flight goes on; the pitfall opens one stage per call, so it may
take a few frames.

The two visible break paths differ. `hitStrongObject` does the whole break inline at the contact
point - hides the mesh, spawns debris, runs the family's item drop - so a house breaks at the
rider with nothing left behind. `hitWeakObject` hides nothing and drops no items: its whole visible
break is debris effects from `GrYaku_SpawnBreakEffect` (0x800f7044), each placed by
`Gr_GetNodeWorldPos` (0x800d4bf4) at a separate stage node pinned to the prop's placed spot. For a
weak family the vacuum therefore moves that debris node onto the contact point for the call -
resolved the way `hitWeakObject` resolves it, through the family's break placement and the
per-prop record array, and written with `JOBJ_USER_DEFINED_MTX` so `Gr_GetNodeWorldPos` reads the
written matrix - and restores the node's matrix and flags right after, since the effects spawn
synchronously inside the call and the node may be shared. After a weak break it also clears
`JOBJ_USER_DEFINED_MTX` on the dragged joint, which collapses it to its degenerate local SRT
instead of leaving it frozen at the rider.

### Letting one go

A claim can end with the prop intact: its owner's rider is gone, it has no joint, or 300 frames
pass without a break. Left where it was dragged, the prop would be a model standing beside an
invisible wall at its baked spot, so it is put back. Each claim records the prop's world
translation and joint flags at claim time; the release copies `GrCollRecord.world`'s 3x3 back into
the joint, re-seats both translations, clears `JOBJ_USER_DEFINED_MTX` unless the joint had it
before, and re-arms the collision.

## Machines

Machines are walked on the machine p_link (`GAMEPLINK_MACHINE`) as `GAMEENTITY_MACHINE` GObjs. A
machine is a target only while `MachineData.rider_gobj` is NULL, which excludes human and CPU
riders alike, and `is_dead` and `is_fall_dead` are clear; a claim is dropped the frame either stops
holding.

The pull writes `MachineData.pos` and zeroes `accel` and `velocity`, which
`Machine_PhysicsThink` (0x801c6368) integrates into the position every frame;
`Machine_ApplyModelMatrix` (0x801c9074) picks the position up next frame. Machines are not shrunk,
and they finish at 45 units rather than 8, so the machine is released before its model clips into
the rider.

On arrival the vacuum calls `Machine_OnKO` (0x801e568c), which the engine reaches through
`Machine_CheckKO` (0x801e5628) only once HP is 0 with damage enabled; called directly it needs
neither. It records the rider ply (5, the riderless value, for a parked machine), sets `is_dead`
and enters the break-down state. That state's animation callback runs `Machine_KOExplode`
(0x801e5838) - explosion, debris, sound, `GObj_Destroy` - once `MACHINE_MSTATUS_ENDED` is set in
`MachineData.mstatus_flags`, which happens when the non-looping break-down motion reaches its end.
The machine therefore explodes about two seconds later, exactly as after a vanilla HP-0 break. The
vacuum sets the bit before the call, but starting the break-down motion clears it, so the pre-set
has no effect. Every rider access in the teardown is guarded on the riderless value, so a parked
machine explodes and frees cleanly. The claim is dropped at the call.

## When a player's Hypernova ends

The ending player's claims are finished and everyone else's stay in flight. Props get one last
synthesized break and are put back if it does not fire; items get `Item_SetAirborne` and return to
vanilla physics; machine claims are simply dropped, since the pull only zeroed velocity, which
physics rebuilds. A scene change drops every claim with no restore, since the scene heap takes the
targets with it.
