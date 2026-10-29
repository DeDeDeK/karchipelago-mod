# Archipelago Star Sphere Shot

Releasing a full charge on the Archipelago Star fires one of its six pods as a projectile. The
pod nearest the machine's heading is the one that leaves, so the shot always launches off the
nose, and it wears that pod's color. It glows, leaves a ribbon of its color behind it, bends
gently toward a player ahead of it, and ends on the first surface it meets. The sixth shot
empties the ring and all six pods grow back over a second, during which a full-charge release is
an ordinary boost.

The shot is behind the **Sphere Shot** setting (default on) and does nothing when the star is
not registered. It is live in every 3D mode the star can be ridden in - City Trial, its
stadiums, Air Ride races and Free Run. Top Ride has no `MachineData` and is out regardless.

## Firing

`RiderState_StarChargeReleaseEnter` (0x801abc64) is the rider state entry that spends a machine
charge on a boost. It has exactly two callers, and both calls are replaced
(`CODEPATCH_REPLACECALL`) with one that tries the shot and then makes the vanilla call:

| Call site | Caller | When |
|---|---|---|
| 0x801abc44 | `Rider_IASACheck_ChargeRelease` (0x801abc2c) | the player lets go of A |
| 0x801abecc | `AS_StarChargeFullThink` (0x801abea0) | the full-charge window ends |

Replacing the two calls is equivalent to hooking the entry, which cannot be hooked directly: its
second instruction is the `mflr r0` that saves the link register, and a hook branch at the
entry would destroy it first.

The shot fires only when, in order: the setting is on, this scene's shot model loaded, the
rider is on a machine, `charge_value >= 0.99` (the meter is still full here - the boost is
applied by the state the release enters), that machine is the star (`!is_bike` and `kind` equal
to its class slot), and its ring is built and not regrowing. The pod is spent only once its
projectile exists, so a shot the engine refuses leaves the ring as it was.

## The projectile kind

The shot is a projectile kind of its own, `WPKIND_NUM` (17), appended after the 17 vanilla
kinds. A kind is an entry in two tables, and neither has room for an 18th:

- **The vtable table** (`wp_kind_vtables`, 0x804b4338) is followed directly by the
  `"WnCommon.dat"` string. At boot the mod copies the 17 vanilla pointers into an 18-entry table
  of its own, adds the shot's, and repoints nine of the ten `lis`/`addi` pairs that form the
  table's address: three in `Weapon_Create` (0x8021f428), and one each in
  `Weapon_Proc10_HitReact` (0x8021fcd4), `Weapon_UserDataDtor` (0x8021ff54), `Weapon_Despawn`
  (0x80220364) and `Weapon_LoadKindParams` (0x802205e8), and two in `Weapon_ReloadKindParams`
  (0x80220654). The tenth, `Weapon_SystemInit`'s loop over each kind's `system_init`, stays on
  the vanilla table: it is bounded at 17 and the shot has nothing to run there.
- **The kind data** (`wp_kind_data`, 0x8055a9a8) is followed by a padding word at 0x8055a9ec
  that the engine's clear never zeroes and its registrar never fills, so the mod stores its
  `WeaponKindData` pointer there once, at boot.

Nothing else in the engine is indexed by weapon kind. Every vtable function slot is
NULL-checked where it is called, so the kind fills only what it uses:

| Slot | What it does |
|---|---|
| `state_table` | one state, below |
| `post_init` | enters the state, sets the live velocity to the spawn velocity with no muzzle kick, and seeds the sphere's grow |
| `on_hit` | stops the homing once the shot has hit anything; returns 0, so the shot flies on |
| `aux_a` | run by the dtor on every ending; detaches the trail so it drains |

`despawn` is left NULL, which falls back to `GObj_Destroy`. The one state entry carries the
attack word `0x103` (`WP_ATTACK_ACTIVE | AP_STAR_SHOT_ATTACK_CAUSE`) and four per-frame
callbacks:

| Slot | Proc priority | Job |
|---|---|---|
| `anim_callback` | 1 | grow in and shrink out |
| `phys_callback` | 4 | homing, ahead of integration |
| `envcoll_callback` | 5 | the environment sweep and the surface rule |
| `post_envcoll_callback` | 6 | ground-follow snap, ahead of the root matrix and the HurtData refresh |

The kind data is static in the mod: a model scale of 1.5 (a 3.0-radius mesh drawn at 4.5), a
cull scale of 4.5, a 180-frame lifetime, an environment collider of radius 2.7 with no extents,
and an animation spec with no joint or material animation, only a hitbox script. That script is
plasma spread's single hitbox command with only its size changed - the high half of the
command's second word, in 1/250 units - so the shot deals the Plasma ability's damage and
knockback. `Weapon_AnimThink` still runs the script every frame with no animation bound.

The owner is the firing rider's GObj, as `Rider_SpawnPlasmaSpread` (0x801a9870) passes, so the
vanilla owner exclusion keeps a shot off the player who fired it. Boxes are hit either way:
`Box_CheckWeaponCollision` (0x80252334) does no owner check. Per-shot state - the owner, the
homing target, the ground mode and the trail handle - sits in the kind's scratch at
`WeaponData+0x1b8`, which no shared projectile code reads, written right after `Weapon_Create`
returns and before any of the shot's procs run.

## Size

Two fields size a projectile. `Weapon_SyncRootMtx` (0x80220310) scales the root joint by
`WeaponData.scale * WeaponData.model_scale`. The prio-7 HurtData refresh hands `scale` alone to
`Trigger_UpdatePosition` (0x8018a188), which sets the hitbox radius to the script's radius times
it, and the render cull uses `scale` too. `model_scale` is seeded from the kind data and sizes
the model alone. At full size the drawn sphere and the hitbox are both radius 4.5.

The sphere grows from 0.05 of its size to full over the shot's first 30 frames, through
`model_scale`, while `scale` stays at 1, so the hitbox is full size from the muzzle. A rival's
hurtbox is one sphere around its machine's body, radius 2.27 on the Warp Star, well inside the
machine and its rider, and a ground shot rides with its center 5 units up. A hitbox as small as
the growing sphere would pass through a nearby rival's model without reaching that hurtbox. `post_init` seeds `model_scale` and rebuilds the root matrix, since
`Weapon_Create` built it at full size. Over the last 30 frames the shot shrinks back to 0.05
through `scale`, so the hitbox fades with the sphere. The fade is needed because the kind has no
despawn of its own, so a shot that ran out of life would otherwise vanish between two frames;
prio 1 runs ahead of that frame's lifetime decrement, so a shot with one frame left is already
down. The fade ends at 0.05 rather than 0 because a shot with no extent at all is degenerate for
a frame.

## Model and color

`ApStarShot.dat` holds the model and nothing else: a two-joint tree whose leaf carries one lit,
untextured UV sphere of radius 3.0, public `apStarShot_model`. It is loaded with
`Gm_LoadGameFile` at each 3D load once the star is bound, and the pointer is dropped at every
scene change with the heap that held it - the title screen rides a star with no 3D load at all,
and a model left over from the last round would be built from freed memory. `Weapon_CollectParts`
(0x80221914) asserts on more than 10 joints or a count other than the model block's, so the
kind data's joint count of 2 has to match the archive.

The material ships white, so the mod writes the loaded copy's `diffuse` with the launching pod's
color, and `ambient` with the same color darkened. `MObjLoad` (0x803f9f04) gives every instance
its own material and `HSD_MObjSetup` (0x803fac18) reads the colors out of it on every draw, so
shots in flight are colored independently.

## Trajectory

A ground probe at the muzzle (`Raycast_Ground` from 12 units above to 40 below) decides the mode
at spawn. Speed is a constant 5.0 plus whatever of the machine's velocity already points that
way, so a boosting player cannot catch their own shot; since the camera rides the machine, the
shot also pulls away on screen at that same constant whatever the player is doing.

- **Air.** The velocity is the machine's heading times that speed. Nothing in the projectile
  pipeline applies gravity, so short of homing the shot is a straight ray.
- **Ground.** The velocity is the heading flattened to horizontal. At prio 6 the shot raycasts
  down each frame and snaps its height to the hit plus 5.0, half a unit more than its radius,
  after the environment pushback and before the root matrix, the HurtData and the next frame's
  collider read the position. Off a ledge the probe misses and the shot holds its altitude.

**Ending on a surface.** `Weapon_UpdateEnvColl` (0x80221fd4) pushes a projectile out of whatever
it touched and writes the resolved position back, but leaves its velocity alone, so a shot that
only collided would slide along a wall. The shot's `envcoll_callback` runs that sweep and then
ends the shot with `GObj_Destroy`: an air shot on any contact (`WP_FLAGB_ENV_CONTACT`), a ground
shot - which rides the floor on purpose - only on a wall or ceiling contact
(`coll_info->wall_rec_num` or `top_rec_num`). The collider is radius 2.7 at a ride height of 5.0,
so a ground shot's collider clears the floor by 2.3 units: a lower curb passes under it, a taller
one ends it.

## Homing

From its eighth frame until its first hit, the shot steers at prio 4, ahead of integration. A
target is any other player - their machine while they ride it, the rider on foot - within 320
units and 40 degrees of the shot's heading, compared in the horizontal plane for a ground shot.
The shot locks onto the best-aligned one, holds the lock while that player stays in the cone, and
picks again when they leave it.

Each frame the heading turns toward the target by `speed / 240` radians, at most 0.07. A fixed
turn radius keeps the arc the same shape at any speed, where a fixed turn rate would bend a slow
shot sharply and a boosted one barely at all. Speed is kept, and a ground shot turns only
horizontally. `on_hit` ends the homing, so a shot that passed through its target cannot wheel
back through it.

The engine's own homing helper, `Weapon_HomingSteer` (0x80223298), is not used: it decays speed
back to a base value, homes on enemies as well, and registers a target tracker on the rider that
the kind would have to free.

## Glow and trail

Both are drawn in immediate-mode GX from one GObj per scene, created on the scene's first shot
on p_link 1 with a proc and a GX link the world camera draws, in the translucent pass. p_link 1
freezes with the match pause and the hitstops, as the projectiles do. The scene teardown frees
the GObj with the rest of the world, so the mod only forgets its handle at the scene change. The
GX callback runs once per camera and reads the camera's axes and eye from its view matrix, so
every split-screen viewport faces its own geometry.

Each shot claims a slot from a fixed pool when it fires; `aux_a` detaches it on every ending,
and the proc frees the slot once the tail has drained and the flare has run out.

- **Halo.** An additive, camera-facing disc through the shot's center, 1.9 times its radius,
  bright in the middle and gone at the rim. The sphere hides the middle, so what shows is a glow
  around the edge. It follows the drawn size, so it grows and fades with the sphere.
- **Trail.** A camera-facing ribbon through the shot's last 12 positions, one sampled per frame,
  with the live position as its head so it always meets the sphere. It is three vertices across
  with alpha only on the center line, so the edges are soft without a texture, and it narrows and
  fades with each sample's age. It blends normally rather than additively, which keeps the hue.
- **Ending.** When the shot goes, its last position stays the ribbon's head and the samples keep
  ageing, so the tail drains into where the shot ended. A shot that ended at half size or more
  also flares: the halo grows and fades over 9 frames. A shot that shrank out at the end of its
  lifetime ends without one.

## The pod ring

The pods the shot fires from are per-machine state, kept in a fixed table keyed by
`MachineData *` and claimed on each machine's first Think. The ring runs from the star's
per-kind Init and Think handler slots (`CustomMachinesAPI.SetInitHandler` and
`SetThinkHandler`), which on the star class run at the ends of `Machine_Star_Init` (0x801e7f3c)
and `Machine_Star_Think` (0x801eacbc). Init drops the machine's ring so the next Think rebuilds it
full against the new model; Think claims the ring, advances the regrow and writes the six pod
poses.

**Slots.** Slots are never released explicitly. A slot whose machine has gone a whole frame unseen
is free to reclaim, so a machine destroyed without warning leaves nothing to clean up. A slot
whose machine was seen last frame is never taken even with the table full: its owner would take
another on its own Think, and the cascade would strip the machines first in the proc list of
their rings every frame. "Frame" counts only frames in which some star ran its Think - the first
claim to see a new `HSD_Update.engine_frames` advances it - so a pause does not age every ring at
once. The whole table is cleared at each scene change, with the joints it points at.

**Pods.** The pods are joints 9-14 of the machine's own tree, resolved through
`CustomMachinesAPI.GetMachineJoint` and cached until the machine's model root changes. Their
scale, translation and Y rotation are written every frame, and nothing else touches them: the
pods carry no animation tracks of their own - the ring pivot at joint 8 spins and they inherit
it - so the writes are uncontested and the authored pose is still readable when a fresh model is
first walked. A fired pod collapses to zero scale over 10 frames and a spent pod sits at zero.
Scale is the hiding mechanism, rather than `DOBJ_HIDDEN` on the pod's DObjs, because the LOD
tables address DObjs by flat index and would fight a per-DObj flag; a zero-scale joint collapses
all four, the glow sprite included.

**Closing the gap.** Each time the alive set changes, the survivors are re-solved onto an even
ring of that many, and each eases 18% of the way to its new angle per frame. A pod's swing
rotates its authored translation about the pivot's Y and adds the same delta to its own yaw, so it
keeps facing outward; a collapsing pod holds where it died. The ring spins continuously, so the
absolute phase of the even ring is invisible and only the transient matters: the phase picked is
the one that moves the pods least, the average of each survivor's offset from an even ring.
Five survivors of six swing at most 24 degrees, in mirrored pairs either side of the gap, and a
subset that is already even - two opposite pods, three alternating - does not move.

**Regrow.** Firing the sixth pod starts the regrow with all six at zero. They grow back on an
ease-out-back curve that overshoots slightly before settling, reaching full size after 60 frames,
and firing is locked out for the whole window. The spread resets to the authored ring on the
frame the regrow starts, while every pod is at zero scale and the change cannot be seen.

A ring outlives its machine by a frame, so the firing check compares the machine's class slot
too: a `MachineData` freed and reused by another machine could otherwise still find one.

## Identifying a hit

The state's attack word puts `AP_STAR_SHOT_ATTACK_CAUSE` (`0x03`) in the low byte. When a hit
lands, `Machine_StoreAttacker` (0x80231d90) copies that word to the victim's
`DmgLog.credited_attack` in the same call that writes `attacker_ply`, and cause 3 is inside the
range it credits, so a shot's hit credits the shooter like any other attack. No vanilla attack
uses cause 3, so a consumer reading a KO's damage log knows from the low byte alone that a
sphere shot made the credited hit.
