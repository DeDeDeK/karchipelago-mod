# Archipelago Star Sphere Shot

Releasing a full charge on the Archipelago Star fires one of its six pods as a projectile. The pod
nearest the machine's heading is the one that leaves, so the shot always launches off the nose, and
it wears that pod's color. The sixth shot empties the ring and starts all six growing back over a
second, during which a full-charge release is an ordinary boost with no shot and no cue.

The whole feature is `mods/ap_star/src/ap_star_shot.c`, behind the **Star Shot** toggle in the
Archipelago Star settings menu (default on). It no-ops entirely when the `custom_machines` registry is
absent or when no machine named `Archipelago Star` is registered. It is live in every 3D mode the
star is rideable in - City Trial, its stadiums, Air Ride races and Free Run. Top Ride has no
`MachineData` and is out regardless.

## Firing

`AS_StarChargeRelease` (`0x801abc64`) is the state entry that spends a machine charge on a boost. It
has exactly two callers:

| Address | Caller | When |
|---------|--------|------|
| `0x801abc44` | the A-release interrupt check (`zz_801abc2c_`) | the player lets go of A |
| `0x801abecc` | the full-charge hold state's think (`zz_801abea0_`) | the full-charge window ends |

Both are taken as `CODEPATCH_REPLACECALL`, not as a hook on the function. `CODEPATCH_HOOKCREATE`
clobbers the link register with its own `bl`, which a function's first instruction cannot survive -
`AS_StarChargeRelease` reads `mflr r0` two instructions in. Both call sites pass `r3 = RiderData`,
and the two are the complete caller set, so replacing them is equivalent to hooking the entry.

`AS_StarChargeRelease2` (`0x801ac2c4`) and `AS_StarChargeRelease3` (`0x801ac488`) are unrelated rider
states and are not touched.

The shot is gated on, in order: the menu toggle, this scene's model being loaded, the rider being on
a machine, that machine being the Archipelago Star (`!md->is_bike && md->kind == star slot`),
`md->charge_value >= 0.99` - still holding the charge at this point, since the boost is applied by
the state the release transitions into - and the ring holding at least one pod and not regrowing.

## The projectile

The shot is `PROJKIND_PLASMA_SPREAD_MID` (7) with its model swapped at spawn. That kind is the right
host: its vtable `init` (`0x8022691c`) is a bare `blr`, three of its four state callbacks
(`0x802269f4`, `0x802269f8`, `0x80226abc`) are `blr`, and it carries no per-kind scratch that a
custom spawn would have to seed. Nothing about its damage or hitbox is rewritten, so the shot deals
whatever the kind's own animation spec says.

Owner exclusion stays on - `desc.owner_gobj` is `rd->x0`, the rider GObj, which is what
`spawnPlasmaSpread` (`0x801a9870`) itself passes - so a shot never hits the player who fired it.
There is no on-hit callback, so it damages riders and machines and keeps travelling. Boxes are hit
either way: `Box_CheckProjectileCollision` (`0x80252334`) does no owner check.

`lifetime` is overwritten to 233 frames; the kind's own default is 120.

### Growing in and fading out

A single `user_hook_0` sizes the shot at both ends of its life. It swells from 0.05 to full over the
first 30 frames and goes back to 0.05 over the last 30. The fade end is needed because the kind's
despawn vtable slot (`0x80226b1c`) is a bare `GObj_Destroy`: a shot that simply ran out of life would
vanish between two frames. Prio 0 runs ahead of the lifetime decrement at prio 1, so a shot with one
frame left is already down and there is no visible last frame. The seed size is written directly at
spawn as well, since the first prio-0 pass may already have gone by.

The ends of the ramp sit at 0.05 rather than at 0 because the same factor drives the hitbox and the
render cull, and a shot with no extent at all is a degenerate one for a frame. They sit that low so
the growth reads: the shot recedes from the camera at roughly the rate a launch anywhere near full
size would grow, and the two cancel on screen.

Two fields carry the size, and both are 1 on a fresh shot, so the factor is written straight into
each. The model's sphere joint is the visual half - the child of the tree root, not the root, since
the root's matrix is engine-owned while a child's local SRT always composes against its parent.
`cur_scale` (`proj+0x74`) is the other: `Projectile_InitRuntimeState` (`0x8021f2a0`) copies it from
`desc.velocity_scale` at the tail of the create and nothing in the shared pipeline or in this kind's
own code writes it again, while three readers pick it up every frame - the HurtData size at prio 7,
the environment sweep radius, and the render cull radius. So one write moves the damage, the
collision and the sphere together, and a shot that is still swelling hits like the small thing it
looks like.

Ending on an impact rather than on lifetime still pops, which is what an impact should look like.

### Swapping the model

`Projectile_Create` reaches the model through a two-word block at the kind's `ProjKindData+0x08`
(read at `0x8021f5c0`-`0x8021f604`): word 0 is the tree's root `JOBJDesc`, and the top byte of word 1
is how many joints that tree holds, landing at `proj+0x1c`. The joint walker at `0x80221914` counts
the loaded tree in preorder and asserts twice - `weapon parts num over` above 10 joints, and
`weapon parts num not match` on any disagreement with that byte.

So the swap is: point `model_desc` at a static two-word block holding the shot's root and a count
byte of 2, keeping the original word's low three bytes; call `Projectile_Create`; put the original
pointer back. The window is one synchronous call, so no other projectile can see it. `kind_data+0x10`
(the vulnerable-region list, the only thing that indexes the walker's parts array by number) is NULL
for this kind, and none of its vtable slots touch the parts array, so nothing else depends on the
model's shape.

`mods/ap_star/assets/ApStarShot.dat` holds the model and nothing else: a two-joint tree whose
leaf carries one lit, untextured UV sphere of radius 3.0, public `apStarShot_model`. It is written by
`scripts/authoring/make_ap_star_shot.py` from the same mesh generator the six assembly spheres use, and it
is loaded per scene with `Gm_LoadGameFile`, so the pointer is refreshed on every 3D load and cleared
when the load fails.

The material ships white, lit and untextured (`RENDER_CONSTANT | RENDER_DIFFUSE | RENDER_ALPHA_MAT`,
the setup the six assembly spheres use), so the mod writes the loaded copy's `ambient` and `diffuse`
per shot with the color of the pod that launched it. `HSD_MObjSetup` (`0x803fac18`) reads all four
material colors out of the live struct on every draw, and `MObjLoad` (`0x803f9f04`) gives every
instance its own copy, so shots in flight are colored independently and nothing has to be re-loaded.
The color comes from the machine descriptor's own palette - the same six values the pods and the six
assembly spheres are painted from, in pod order.

## Trajectory

A ground probe at the muzzle (`Raycast_Ground` down from 12 units above to 40 below) decides the mode
at spawn. Speed is a constant 6.3 plus whatever of the machine's velocity is already pointing that
way, so a boosting player cannot catch their own shot. Because the carry term is the full component,
the shot pulls away from the machine at exactly that constant - and since the camera rides the
machine, that is also how fast it reads on screen, whatever the player is doing.

**Air.** `velocity = forward * speed`, written once and never touched. Nothing in the projectile
pipeline applies gravity - `proj+0x7c` is zeroed at prio 0 every frame - and this kind's `fn1` is a
`blr`, so the shot is a dead-straight ray from wherever the machine was pointing. The kind's own
`fn2` (`0x802269fc`) stays in place and bursts it on a steep environment contact.

**Ground.** Velocity is the heading flattened to horizontal, and two callbacks are installed after
`Projectile_Create` returns, which is after the kind's `post_init` ran its `Projectile_SetState` -
that call zeroes `proj+0x160`..`0x178`, so anything written earlier would be lost.

- `user_hook_1` (`proj+0x164`, prio 7) raycasts down each frame and snaps `position.Y` to the hit
  plus a 3.5 ride height. Prio 7 runs after prio 4 integrates and before the HurtData position
  refresh, so the snapped position is the one the hit scans see. Off a ledge the probe misses and the
  shot holds its altitude, flying flat until its lifetime expires.
- `state_fn2` replaces the kind's own, which would burst the shot on the first rise a projectile
  deliberately riding the floor drove into. The replacement sweeps `Raycast_Wall` over the frame's
  travel (`position_prev` -> `position`) and destroys the shot on a wall. `position_prev` is seeded
  to the muzzle at spawn, since prio 21 only starts maintaining it at the end of the first frame.

## The ring

Per-machine state is a small table keyed by `MachineData *`, eight rings deep, claimed on each
machine's first per-frame tick. Slots are recycled by age rather than released, so a machine
destroyed without warning leaves nothing to clean up; the whole table is cleared on every 3D load,
where the joints it points at have just been freed with the scene heap.

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

Regrow starts on the frame the sixth pod launches, with all six at zero, and runs 60 frames on an
ease-out-back curve that overshoots slightly before settling. Firing is locked out for the whole
window. The spread resets to the authored ring on that same frame, while every pod is at zero scale
and the change cannot be seen.

The count of surviving pods also selects the machine's handling profile, on a fixed ladder from
the machine as shipped at six down to Jet Star's at one. `UpdateProfile` reads it off
`alive_mask` at the end of each per-machine tick and only acts on a change; an empty ring answers
no profile at all, so the regrow window holds the last one until the refill takes the count back
to six. That is the ring's whole involvement - the profiles themselves live in
`ap_star_handling.c`, behind `ap_star_settings.handling_enabled`, which no settings option is
bound to, so the count is read and discarded.

## Where the per-machine work runs

Neither the fire path nor the scale writes can take `Machine_AnimThink`'s tail (`0x801c6274`) - the
`custom_machines` palette cycle already replaces that call. Instead the mod claims the engine's own
per-kind extension slots on the star class, through two `CustomMachinesAPI` entries:

| Slot | Dispatch tail | Table | Used for |
|------|---------------|-------|----------|
| `SetStarInitHandler` | `Machine_Star_Init` (`0x801e7f3c`), tail at `0x801e80d8` | `0x804b15c0` | drop this machine's ring, so the next tick rebuilds it full against the new model |
| `SetStarThinkHandler` | `Machine_Star_Think` (`0x801eacbc`), tail at `0x801eb520` | `0x804b160c` | claim the ring, advance the regrow, write the six pod scales |

Both tails index their table by `md->kind` and call the entry only if it is non-NULL. The registry
already relocates both tables into arrays it owns, so installing a handler is a store. A consumer's
handler layers over whatever the machine's `clone_kind` inherited rather than replacing it: one
shared dispatcher stands in the slot and recovers the row from `md->kind`, which is the star slot, so
no per-slot trampoline is needed. The Archipelago Star clones `VCKIND_SLICK`, whose entry in both
tables is NULL, so nothing is inherited in practice.

## Tuning

Shot speed, lifetime, grow and fade lengths, the seed scale, ride height, probe reach, collapse and
regrow lengths and the respread ease rate are all named constants at the top of `ap_star_shot.c`.
Range is speed times lifetime. The sphere's radius and mesh resolution are constants in
`scripts/authoring/make_ap_star_shot.py`; changing the joint count there means changing
`AP_STAR_SHOT_JOINTS` to match, or the walker asserts.
