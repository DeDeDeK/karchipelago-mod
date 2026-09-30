# Archipelago Star Sphere Shot

Releasing a full charge on the Archipelago Star fires one of its six pods as a projectile. The pod
nearest the machine's heading is the one that leaves, so the shot always launches off the nose, and
it wears that pod's color. It glows, leaves a ribbon of its color behind it, bends gently toward a
player ahead of it, and ends on the first surface it meets. The sixth shot empties the ring and
starts all six growing back over a second, during which a full-charge release is an ordinary boost
with no shot and no cue.

The feature is `mods/ap_star/src/ap_star_shot.c` (the ring, the firing and the projectile kind) and
`mods/ap_star/src/ap_star_shot_fx.c` (the glow and the trail), behind the **Sphere Shot** toggle in
the Archipelago Star settings menu (default on). It no-ops entirely when the `custom_machines`
registry is absent or when no machine named `Archipelago Star` is registered. It is live in every 3D
mode the star is rideable in - City Trial, its stadiums, Air Ride races and Free Run. Top Ride has no
`MachineData` and is out regardless.

## Firing

`RiderState_StarChargeReleaseEnter` (`0x801abc64`) is the state entry that spends a machine charge on a boost. It
has exactly two callers:

| Address | Caller | When |
|---------|--------|------|
| `0x801abc44` | `Rider_IASACheck_ChargeRelease` (`0x801abc2c`) | the player lets go of A |
| `0x801abecc` | `AS_StarChargeFullThink` (`0x801abea0`) | the full-charge window ends |

Both are taken as `CODEPATCH_REPLACECALL`, not as a hook on the function. `CODEPATCH_HOOKCREATE`
clobbers the link register with its own `bl`, which a function's first instruction cannot survive -
`RiderState_StarChargeReleaseEnter` reads `mflr r0` at entry+4. Both call sites pass `r3 = RiderData`,
and the two are the complete caller set, so replacing them is equivalent to hooking the entry.

`AS_StarChargeRelease2` (`0x801ac2c4`) and `AS_StarChargeRelease3` (`0x801ac488`) are unrelated rider
states and are not touched.

The shot is gated on, in order: the menu toggle, this scene's model being loaded, the rider being on
a machine, that machine being the Archipelago Star (`!md->is_bike && md->kind == star slot`),
`md->charge_value >= 0.99` - still holding the charge at this point, since the boost is applied by
the state the release transitions into - and the ring holding at least one pod and not regrowing.

## The projectile kind

The shot is a projectile kind of its own, `WPKIND_NUM` (17), appended after the 17 vanilla kinds.
A kind is two table entries, and neither vanilla table has room for an 18th:

- **The vtable table** (`wp_kind_vtables`, `0x804b4338`) is followed directly by the
  `"WnCommon.dat"` string. At boot the mod copies the 17 vanilla pointers into an 18-entry table of
  its own, adds the shot's, and repoints the `lis`/`addi` pairs that form the table's address: three
  in `Weapon_Create`, and one each in `Weapon_Proc10_HitReact`, `Weapon_UserDataDtor`,
  `Weapon_Despawn`, `Weapon_LoadKindParams` and two in `Weapon_ReloadKindParams`
  (`0x80220654`). The tenth site,
  `Weapon_SystemInit`'s loop over each kind's `system_init`, stays on the vanilla table - it is
  bounded at 17 and the shot has no `system_init` to run.
- **The kind data** (`wp_kind_data`, `0x8055a9a8`) is followed by a padding word at `0x8055a9ec`
  that `Weapon_ClearKindDataTable` never zeroes and `Weapon_RegisterKindDataList` never
  fills, so the mod stores its `WeaponKindData` pointer there once, at boot.

Nothing else in the engine is indexed by kind: no switch runs on it, and the few
`WeaponGObj_GetKind` readers compare against fixed vanilla kinds.

Every vtable function slot is NULL-checked where it is called, so the kind fills only what it uses:

| Slot | What it does |
|------|--------------|
| `state_table` | one state, below |
| `post_init` | enters the state (`Weapon_StateChange(proj, 0, 0, 1, 0)`) and sets the live velocity to the spawn velocity, with no muzzle kick of its own |
| `on_hit` | stops the homing once the shot has hit anything; returns 0, so the shot flies on |
| `aux_a` | run by the dtor on every ending; detaches the trail so it drains |

`despawn` is left NULL, which falls back to `GObj_Destroy`, and `init` and the render-state loaders
are left NULL because no state reads `proj+0x104`.

The one state entry carries the attack word `0x103` and four per-frame callbacks:

| Slot | Prio | Callback | Job |
|------|------|----------|-----|
| `anim_callback` | 1 | `ShotThink` | grow in and shrink out |
| `phys_callback` | 4 | `ShotSteer` | homing, ahead of integration |
| `envcoll_callback` | 5 | `ShotEnvCollide` | environment sweep and the surface rule |
| `post_envcoll_callback` | 6 | `ShotFollowGround` | ground-follow snap, ahead of `Weapon_SyncRootMtx` and the prio-7 HurtData refresh |

The kind data is all static in the mod:

| Field | Value |
|-------|-------|
| `params` | `model_scale` 1.5 (the 3.0 mesh drawn at 4.5), `cull_scale` 4.5, lifetime 180 frames |
| `model_desc` | `ApStarShot.dat`'s tree, loaded on every 3D load, with a joint count of 2 |
| `state_anim_spec_array` | no `AnimJoint` and no `MatAnimJoint`, only the hitbox script |
| `mpcoll_desc` | radius 2.7, no extents |
| `render_state_tmpl`, `vuln_region_spec` | NULL |

The hitbox script is plasma spread's single hitbox command with only its size changed: the size is
the high half of the command's second word in 1/250 units, 1125 for a 4.5 radius. Damage and
knockback are the Plasma ability's. With no animation to bind, `Weapon_AnimThink` still runs the
script every frame, and nothing moves the sphere joint.

Owner exclusion stays on - `desc.owner_gobj` is the rider GObj, as `Rider_SpawnPlasmaSpread` (`0x801a9870`)
passes - so a shot never hits the player who fired it. Boxes are hit either way:
`Box_CheckWeaponCollision` (`0x80252334`) does no owner check.

Per-shot state (owner, homing target, ground mode, the trail handle) sits in the kind's scratch at
`proj+0x1b8`, which no shared projectile code reads or writes. `Weapon_Create` zeroes it, and
`Fire` fills it before any of the shot's procs run.

### Size

`scale` is the shot's only size. `Weapon_SyncRootMtx` scales the root joint by
`scale * params.model_scale`, the prio-7 HurtData refresh sizes the hitbox by `scale`, and
the render cull uses `scale * params.cull_scale + 5`. At `scale` 1 the drawn sphere and the
hitbox are both radius 4.5, and they stay equal all the way through the grow and the fade.

The shot is created at `scale` 0.05, passed as `desc.scale`, which
`Weapon_InitRuntimeState` copies into `scale`. `ShotThink` grows it to 1 over the first 30
frames and takes it back to 0.05 over the last 30. The fade is needed because the kind has no
despawn of its own, so a shot that simply ran out of life would vanish between two frames. Prio 1
runs ahead of that frame's lifetime decrement, so a shot with one frame left is already down.

The ends of the ramp sit at 0.05 rather than 0 because a shot with no extent at all is a degenerate
one for a frame. They sit that low so the growth reads: the shot recedes from the camera at roughly
the rate it grows, and the two cancel on screen.

The environment collider does not scale with `scale` and is smaller than the sphere on purpose,
so a ground shot clears a curb (below).

### Model and color

`mods/ap_star/assets/ApStarShot.dat` holds the model and nothing else: a two-joint tree whose leaf
carries one lit, untextured UV sphere of radius 3.0, public `apStarShot_model`. It is written by
`scripts/authoring/make_ap_star_shot.py` from the same mesh generator the six assembly spheres use,
and it is loaded per scene with `Gm_LoadGameFile` at `On3DLoadEnd`. The pointer is dropped at every
scene change, since the heap reset has just freed the archive. That matters beyond the 3D modes: the
title screen rides a star with no 3D load at all, and a model left over from the last round would be
built into a shot from freed memory. `Weapon_CollectParts` (`0x80221914`) asserts on more than 10
joints or a count other than the model block's, so the block's count byte tracks the archive.

The material ships white, lit and untextured, so the mod writes the loaded copy's `ambient` and
`diffuse` per shot with the color of the pod that launched it. `HSD_MObjSetup` (`0x803fac18`) reads
all four material colors out of the live struct on every draw, and `MObjLoad` (`0x803f9f04`) gives
every instance its own copy, so shots in flight are colored independently. The color comes from
`ap_star_piece_colors`, the same six values the pods and the assembly spheres are painted from, in
pod order.

## Trajectory

A ground probe at the muzzle (`Raycast_Ground` down from 12 units above to 40 below) decides the mode
at spawn. Speed is a constant 5.0 plus whatever of the machine's velocity is already pointing that
way, so a boosting player cannot catch their own shot. Because the carry term is the full component,
the shot pulls away from the machine at exactly that constant - and since the camera rides the
machine, that is also how fast it reads on screen, whatever the player is doing.

**Air.** The velocity is the machine's heading times that speed. Nothing in the projectile pipeline
applies gravity, so short of homing the shot is a straight ray from wherever the machine was
pointing.

**Ground.** The velocity is the heading flattened to horizontal. `ShotFollowGround` raycasts down
each frame at prio 6 and snaps `position.Y` to the hit plus 5.0, half a unit more than the sphere's
radius, so the sphere rides just clear of the surface. Prio 6 runs after the environment pushback and
before `Weapon_SyncRootMtx`, so the model, the HurtData and the next frame's collider all start
from the snapped position. Off a ledge the probe misses and the shot holds its altitude, flying flat
until its lifetime expires.

### Ending on a surface

`Weapon_UpdateEnvColl` (`0x80221fd4`) pushes a projectile back out of whatever it touched and
writes the resolved position back, but leaves its velocity alone - so a shot that only collided would
slide along the wall every frame. `ShotEnvCollide` runs the sweep, then ends the shot with
`GObj_Destroy` on a contact:

- An air shot ends on any contact (`flag_b` bit 0, `WP_FLAGB_ENV_CONTACT`).
- A ground shot rides the floor on purpose, so only a wall or ceiling contact ends it
  (`coll_info->wall_rec_num` or `top_rec_num`). The city's building sides are wall triangles.

The collider is radius 2.7 at a ride height of 5.0, so the bottom of a ground shot's collider sits
2.3 units above the floor: a curb lower than that passes under it, and a taller one ends it. The sweep
also drives the destructible city geometry, so a shot keeps breaking the props it flies into.

### Homing

`ShotSteer` runs at prio 4, ahead of integration, from the shot's eighth frame until its first hit.
A target is any other player - their machine while they ride it, their rider while on foot - within
320 units and 40 degrees of the shot's heading, compared in the horizontal plane for a ground shot.
The shot locks onto the best-aligned one and holds the lock while that player stays in the cone,
and picks again when they leave it.

Each frame the heading turns toward the target by `speed / 240` radians, at most 0.07. A fixed turn
radius means the arc is the same shape at any speed, where a fixed turn rate would bend a slow shot
sharply and a boosted one barely at all. Speed is kept, so the carry from the machine stays. A ground
shot only turns horizontally. The engine's own homing helper (`Weapon_HomingSteer`, `0x80223298`)
is not used: it decays speed back to a base value, homes on enemies as well, and its target tracker
registers on the target rider and must be freed by the kind.

`on_hit` ends the homing, so a shot that passed through its target cannot wheel back through it.

## Glow and trail

`ap_star_shot_fx.c` draws both in immediate-mode GX from one GObj per scene, created on the scene's
first shot on p_link 1 with a proc and a GX link on the world camera's link 0, translucent pass. The
scene teardown frees it with the rest of the world, so the handle is only forgotten at the next
scene change, never destroyed. p_link 1 freezes with the pause and with the assembly hitstop, as the
projectiles do.

Each shot claims one of 24 slots when it is fired. `aux_a`, which the dtor runs on every ending,
detaches the shot from its slot, and the proc frees the slot once the tail has drained and the flare
has run out, so the 24 cover live shots and draining tails together. The GX callback runs once per camera, so every split-screen viewport faces its own
geometry; it reads the camera's axes and eye from its view matrix.

- **Halo.** An additive, camera-facing disc through the shot's center, radius 1.9 times the shot's,
  bright in the middle and gone at the rim, in the shot's color. The sphere hides its middle, so what
  shows is a glow around the edge. It follows the weapon's `scale`, so it grows and fades with the shot.
- **Trail.** A camera-facing ribbon in the shot's color through its last 12 positions, one a frame,
  sampled by the proc; the head is the live position, so the ribbon always meets the sphere. It is
  three vertices across with alpha only on the center line, so the edges are soft without a
  texture, and it narrows and fades with each sample's age. It blends normally rather than
  additively, which keeps the sphere's hue.
- **Ending.** When the shot goes, its position is kept as the ribbon's head and the samples keep
  ageing, so the tail drains into where the shot ended over 12 frames. A shot that ended at half
  size or more also flares: the halo grows toward 2.2 times its size and fades over 9 frames. A shot
  that shrank out at the end of its lifetime ends without one.

## The ring

Per-machine state is a table keyed by `MachineData *`, 32 rings deep, claimed on each machine's
first per-frame tick. Slots are never released: a machine destroyed without warning leaves nothing
to clean up, because a slot whose machine has gone a whole frame unseen is free to reclaim. A slot
whose machine was seen last frame is never taken, even with the table full - its owner would take
another on its own tick, that owner another, and the cascade would strip the machines first in the
proc list of their rings every frame, so they could never fire. A machine that finds no slot simply
has no ring and no shot until one frees. "Frame" here counts only frames in which some star ran its
Think (advanced from `OnFrameStart`), so a pause does not age every ring at once. The whole table is
cleared at every scene change, where the joints it points at have just been freed with the scene heap.

Pod joints are indices 9 through 14 of the machine archive's own joint tree, resolved through the
registry and cached until the machine's model root changes. Scale, translation and Y rotation are
written per frame, and nothing else touches them: the pods carry no FigaTree tracks of their own -
the Moving animation's one animated node is the ring pivot at joint 8, whose spin they inherit - so
the writes are uncontested and the authored pose is still readable the first time a fresh model is
walked. A fired pod collapses to zero over 10 frames; a spent pod sits at zero. Scale alone is the
hiding mechanism, rather than `DOBJ_HIDDEN` on the pod's four DObjs, because the LOD tables address
DObjs by flat index and would fight a per-DObj flag; a zero-scale joint collapses all four, the
always-drawn XLU glow sprite included.

### Closing the gap

Survivors do not stay where they were. Each time the alive set changes, the remaining pods are
re-solved onto an even ring of that many, and each eases 18% of the way to its new angle per frame.
A pod's swing is a rotation of its authored translation about the pivot's Y plus the same delta on
its own yaw, so it keeps facing outward; the collapsing pod holds where it died.

The ring spins continuously, so the absolute phase of the even ring is invisible and only the
transient matters. The phase picked is therefore the one that moves the pods least: each survivor's
offset from an even ring is taken in turns, the offsets are unwrapped against the first and averaged,
and that average is the phase. Five survivors of a six-ring end up swinging at most 24 degrees, in
mirrored pairs either side of the gap. Subsets that are already even - two opposite pods, three
alternating - solve to no movement at all.

Regrow starts on the frame the sixth pod launches, with all six at zero, and runs an
ease-out-back curve that overshoots slightly before settling, reaching full size on the 61st
tick (`regrow_timer` 0 through 60). Firing is locked out for the whole
window. The spread resets to the authored ring on that same frame, while every pod is at zero scale
and the change cannot be seen.

## Where the per-machine work runs

The mod claims the machine's per-kind Init and Think handler slots through two `CustomMachinesAPI`
entries, which on the star class are the engine's own extension tables. It claims them once, at the
first scene change - past every mod's `OnBoot`, so `custom_machines` has registered the star - along
with the star's class slot, which the firing gate compares `md->kind` against. Both are fixed for the
run from there.

| Slot | Dispatch tail | Table | Used for |
|------|---------------|-------|----------|
| `SetInitHandler` | `Machine_Star_Init` (`0x801e7f3c`), tail at `0x801e80d8` | `0x804b15c0` | drop this machine's ring, so the next tick rebuilds it full against the new model |
| `SetThinkHandler` | `Machine_Star_Think` (`0x801eacbc`), tail at `0x801eb520` | `0x804b160c` | claim the ring, advance the regrow, write the six pod scales |

Both tails index their table by `md->kind` and call the entry only if it is non-NULL. The registry
already relocates both tables into arrays it owns and starts a custom slot's entries at NULL, so
installing a handler is a store. The third slot, Anim, belongs to the platform color cycle.

## Identifying a shot's hit

The state's attack word is `WP_ATTACK_ACTIVE | AP_STAR_SHOT_ATTACK_CAUSE` (`0x103`), with the cause `0x03` published in
`ap_star_api.h`. No vanilla attack uses cause 3, and it is inside the 1..0x1a range
`Machine_StoreAttacker` (`0x80231d90`) bounds-checks, so a shot's hit credits the shooter like any
other attack, under a stat index no vanilla cell reads. `Machine_StoreAttacker` writes the word to
the victim's `DmgLog.credited_attack` in the same call that writes `attacker_ply`, so a consumer reading
a KO's damage log knows a sphere shot made the credited hit from the word's low byte alone.

## Tuning

The shot's radius, speed, lifetime, grow and fade lengths, seed scale, collider radius, ride height,
probe reach, the homing delay, range, cone and turn radius, and the ring's collapse, regrow and
respread rates are named constants at the top of `ap_star_shot.c`; the trail's length, width and
alpha, and the halo's and flare's sizes, at the top of `ap_star_shot_fx.c`. Range is speed times
lifetime. The mesh's radius and resolution are constants in `scripts/authoring/make_ap_star_shot.py`;
changing the radius there means changing `SHOT_MODEL_RADIUS` to match, and changing the joint count
means changing `AP_STAR_SHOT_JOINTS`, or the walker asserts.
