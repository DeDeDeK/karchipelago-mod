# Scale Change Event

A custom City Trial event (`CUSTOM_EVKIND_SCALE_CHANGE`, kind 18) announced as "The world is
growing!". It runs through the `custom_events` framework with a siren intro, sky preset 3 (Dusk 2),
BGM file 0x32 (`event_monster`), a 900-frame duration and roll weight 20, and it registers `start`,
`active`, `end`, `end2` and `abort`.

It makes the world feel bigger by **shrinking every player** rather than scaling the stage: the
rider and machine models and their collision bodies shrink, players slow down, and
each player camera pulls in to match. A half-size player in an unchanged world reads exactly like a
normal-size player in a doubled world, and the world's collision is never touched, so the floor
stays solid.

## Why Not Scale the World

Stage collision is baked world-space data that nothing rescales at runtime. `Raycast_Do`
(0x800d9958) reads the triangle array `GrObj.coll.tri` and a tree of baked bounding boxes, and
machine sphere collision (`mpColl_UpdateCollision`, 0x802485e0) reads the same triangles directly.
The stage `scale` field (`StageNode+0x08`) only feeds the visual model setup at load
(`CreateStageModel_3D`, 0x800dcbf0); writing it at runtime moves neither mesh nor tree, and
machines fall through the floor. Scaling collision for real would mean rewriting every vertex,
bounding box and plane constant and reversing it exactly afterwards, and transforming rays in
`Raycast_Ground` would miss the sphere path. Shrinking the players reduces the job to a handful of
per-object scalars.

## The Levers

Every frame, every player slot - CPUs included - is driven by one eased `factor`: 1.0 is normal
size, and it eases toward 0.5.

| Lever | Field | Mechanism |
|---|---|---|
| Rider model | `RiderData.model_scale` (+0x348) | Baked into the rider's model matrix every frame by `Rider_ApplyModelMatrix` (0x80190848), the same field Big/Small Kirby uses. Visual only. |
| Machine model | `MachineData.model_scale` (+0x310), written as `model_scale_default` (+0x30C) x `factor` | `Machine_ApplyModelMatrix` (0x801c9074) and its siblings bake `model_scale x model_scale_base` into the model matrix every frame. Visual only. |
| Collision body | The rider's and machine's `CollData` shape: `radius`, `radius2`, `offset` and the machine's three `probes` | Written each frame, `radius2` also through a hook in `mpColl_Update` |
| World speed | `MachineData.pos` (+0x3E8) | Each frame the position is pulled back so only `factor` of the movement since the last pass survives |
| Camera | Each player camera's eye and interest | A hook at 0x800b38e8 in `PlyCamGObj_Think` (0x800b3540) scales both by `factor` about the body the camera tracks |

**Model and collision move together.** A collision body is one sphere described in the owner's
body frame, the frame `mpColl_Update` (0x80245f70) builds from the owner's position and two axes:
`CollShapeData.offset` places the center, and a machine also carries three `CollProbe` points that
`mpColl_RoughProbe` (0x8024c5b4) sweeps. A star's center sits at offset (0, 1.2, 0) with radius
1.2, so the machine position is the sphere's ground contact point - the same point the model
scales about. Scaling the radius, the offset and the probes together scales the whole body about
that point, so the shrunk machine still rests on the floor. Scaling the radius alone would sink it.

**What holds on the shape.** `radius`, `offset` and the probes are written only by `mpColl_Init`
(0x80245c10) and `mpColl_SetProbes` (0x802463a8), so the event's writes hold. `radius2` is
rewritten every frame by grounded states, which pass their unscaled radius to `mpColl_Update` at
proc priority 5, after the event proc at priority 0; airborne, jumping and rail states pass -1 and
leave it alone. A hook at 0x802460a0 in `mpColl_Update`, on the store that only runs for a radius2
other than -1, records the passed value as the unscaled radius2 of a scaled body and stores it
scaled. `Machine_SetTransform` (0x801cb50c) can re-run `mpColl_Init` and rewrite a shape unscaled;
a body whose radius no longer matches the value last written is recaptured before it is scaled
again.

**Why displacement, not a speed stat.** `StageNode.machine_accel` scales both thrust and drag, so
it changes how fast a machine reaches cruise speed, not the cruise speed. `top_speed_current`
(`MachineData+0x398`) is a real cap, but only some vehicles' controllers consult it. Scaling the
displacement is downstream of every force: the event proc runs before `Machine_PhysicsThink`
(priority 4) and the collision proc, so the engine integrates and resolves collision from the
pulled-back position; the rider and camera follow the machine position; and the velocity
(+0x324) stays untouched, so handling and charging see the true value. A one-frame jump larger
than 5 x `top_speed_current` is taken as a respawn or warp and passed through unscaled.

**Why move the camera, not the FOV.** Scaling a whole scene - geometry and camera rig - gives a
pixel-identical image, so the exact way to fake "the world grew" from a shrunk player is to move
the camera rig the same `factor` about the player. A wider FOV would magnify but warp the
perspective. The player camera is recomputed from scratch each frame after most game logic, so an
outside write would be overwritten; instead a hook at 0x800b38e8 in `PlyCamGObj_Think` (0x800b3540),
after the whole camera pipeline - camera kinds, C-stick zoom, rail transitions - and just before
the solved interest and eye go to `CObj_SetInterest` and `CObj_SetEyePosition`, scales both points
about the tracked body's position, `CamData.target->pos`. Scaling the interest too keeps the player
where it normally sits in frame. With `factor` at 1.0 the hook changes nothing.

## Lifecycle

Per player slot the event remembers the machine it is shrinking, the unscaled collision shape of
that machine and of the rider (captured when each first appears; they vary by vehicle) and the
last position for the speed lever. When a slot's machine changes - a dismount, a swap, a lost
machine - the previous machine is restored on the spot to its default model scale and captured
shape, if it is still on the machine GObj list. A machine left behind never stays shrunk, and a
later mount captures its true size. A new machine's first frame only seeds the speed lever.

| Callback | Does |
|---|---|
| `start` | Sets `factor` to 1.0 and forgets the speed lever's slots |
| `active` | Eases `factor` toward 0.5 by 0.02 a frame and applies every lever |
| `end` | Eases back toward 1.0 through the cleanup phase |
| `end2` | Restores every rider and every slot's machine to full size and resets the camera |
| `abort` | Forgets every scaled body and resets the camera, without touching the dying objects |

Both hooks are installed once at boot and are passthroughs while nothing is scaled. That is why the
abort matters: `PlyCamGObj_Think` runs in every minor-18 scene - Air Ride, City Trial, stadiums,
the attract demo - so without the reset a round that ended mid-event would keep the dolly in every
scene after it, and the next scene's pooled `CollData` can reuse a remembered pointer.

## Limitations

- **Hit collision keeps its size.** Hurtboxes and attack regions are separate from the map
  collision body and are not scaled.
- **Every view dollies.** The hook adjusts every player camera, split-screen views included, and
  only the final eye and interest, so a player's manual C-stick zoom still applies on top.
- **CPUs shrink too**, and a machine shrinks only while someone rides it.
- **The speed lever scales displacement, not velocity.** A knockback larger than the teleport
  threshold is not slowed that frame, a respawn slides in over a few frames, and anything reading
  velocity sees full speed.
- **`RiderData.model_scale` is shared** with every other user of the field, such as a Big/Small
  Kirby effect: the event overwrites it while it runs and leaves 1.0 behind.
