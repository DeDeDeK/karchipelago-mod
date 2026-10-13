# Scale Change Event

A custom City Trial event (`CUSTOM_EVKIND_SCALE_CHANGE`, kind 18) announced as "The world is
growing!". It runs through the `custom_events` framework with a siren intro, sky preset 3 (Dusk 2),
BGM file 0x32 (`event_monster`), a 900-frame duration and roll weight 20, and it is the only custom
event that uses every callback: `start`, `active`, `end`, `end2` and `abort`.

It makes the world feel bigger by **shrinking every player** rather than scaling the stage: the
rider model, the machine model and the machine's collision sphere shrink, players slow down, and
each player camera pulls in to match. A half-size player in an unchanged world reads exactly like a
normal-size player in a doubled world, and the world's collision is never touched, so the floor
stays solid.

## Why Not Scale the World

Stage collision is baked world-space data that nothing rescales at runtime. `Raycast_Do`
(0x800d9958) reads a triangle array at `(*stc_grobj)+0x5c` and a tree of baked bounding boxes, and
machine sphere collision (`mpColl_UpdateCollision`, 0x802485e0) reads the same triangles directly.
The stage `scale` field (`StageNode+0x08`) only feeds the visual model setup at load
(`CreateStageModel_3D`, 0x800dcbf0); writing it at runtime moves neither mesh nor tree, and
machines fall through the floor. Scaling collision for real would mean rewriting every vertex,
bounding box and plane constant and reversing it exactly afterwards, and transforming rays in
`Raycast_Ground` would miss the sphere path. Shrinking the players reduces the job to a handful of
per-object scalars.

## The Levers

Every frame, each of the four City Trial player slots is driven by one eased `factor`: 1.0 is
normal size, and it eases toward 0.5.

| Lever | Field | Mechanism |
|---|---|---|
| Rider model | `RiderData.model_scale` (+0x348) | Baked into the rider's model matrix every frame by `Rider_ApplyModelMatrix` (0x80190848), the same field Big/Small Kirby uses. Visual only. |
| Machine model | `MachineData.model_scale` (+0x310), written as `model_scale_default` (+0x30C) x `factor` | `Machine_ApplyModelMatrix` (0x801c9074) and its siblings bake `model_scale x model_scale_base` into the model matrix every frame. Visual only. |
| Machine collision | `coll_data->shape_data` `radius` and `radius2` | The sphere `mpColl_GetSphereRadius` (0x802415a8) reads |
| World speed | `MachineData.pos` (+0x3E8) | Each frame the position is pulled back so only `factor` of the movement since the last pass survives |
| Camera | Each player camera's eye | A shim on the `bl CObj_SetEyePosition` in `PlyCamGObj_Think` (0x800b3900) moves the eye toward the interest by `factor` |

**Model and collision move together.** Collision holds the sphere center one radius above the
ground and the model is drawn around that center, so a smaller radius lowers the machine for free.
A shrunk model alone floats inside a full-size sphere; a shrunk sphere alone sinks the full-size
model into the ground.

**What holds on the sphere.** Every frame each vehicle state's collision callback calls
`mpColl_Update` (0x80245f70) at proc priority 5, after the event proc at priority 0. It stores
the base radius into `CollData.radius`, and a second radius into the shape's `radius2` unless it
passes -1 - grounded states pass the vehicle's own radius, flying, jumping and rail states pass -1.
The shape's `radius` is written only by `mpColl_Init` at spawn. So of the event's two writes,
`radius` always holds and `radius2` holds only in the air: airborne the sphere is fully scaled, and
grounded it sits between the scaled `radius` and the full `radius2`, which
`mpColl_GetSphereRadius` lerps between.

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
the camera the same `factor` toward its target. A wider FOV would magnify but warp the perspective.
The shim replaces the final eye-set call because the player camera is recomputed from scratch each
frame after most game logic, so an outside write would be overwritten. Replacing the last call puts
it after the whole camera pipeline - camera kinds, C-stick zoom, rail transitions - and the
interest, written by `CObj_SetInterest` one call earlier, is read straight back off the camera
object. With `factor` at 1.0 the shim is a plain passthrough.

## Lifecycle

Per player slot the event remembers the machine it is shrinking, that machine's unscaled `radius`
and `radius2` (captured when the machine first appears in the slot; they vary by vehicle) and the
last position for the speed lever. When a slot's machine changes - a dismount, a swap, a lost
machine - the previous machine is restored on the spot to its default model scale and captured
radii, if it is still on the machine GObj list. A machine left behind never stays shrunk, and a
later mount captures its true size. A new machine's first frame only seeds the speed lever.

| Callback | Does |
|---|---|
| `start` | Sets `factor` to 1.0 |
| `active` | Eases `factor` toward 0.5 by 0.02 a frame and applies every lever |
| `end` | Eases back toward 1.0 through the cleanup phase |
| `end2` | Restores every rider's scale to 1.0 and every slot's machine |
| `abort` | Resets `factor` to 1.0 and forgets the slots, without touching the dying objects |

The camera shim is installed once at boot and reads the live `factor`, so it follows the same
ease. That is also why the abort matters: `PlyCamGObj_Think` runs in every minor-18 scene - Air
Ride, City Trial, stadiums, the attract demo - and without the reset a round that ended mid-event
would keep the dolly in every scene after it.

## Limitations

- **The grounded sphere is only partly shrunk**, since `mpColl_Update` restores `radius2` every
  grounded frame.
- **Riders on foot keep full-size collision.** Only the machine sphere is scaled, so a rider who
  dismounts mid-event floats slightly.
- **Every view dollies.** The shim adjusts every player camera, split-screen views included, and
  only the final eye, so a player's manual C-stick zoom still applies on top.
- **CPUs shrink too**, and a machine shrinks only while someone rides it.
- **The speed lever scales displacement, not velocity.** A knockback larger than the teleport
  threshold is not slowed that frame, a respawn slides in over a few frames, and anything reading
  velocity sees full speed.
- **`RiderData.model_scale` is shared** with every other user of the field, such as a Big/Small
  Kirby effect: the event overwrites it while it runs and leaves 1.0 behind.
