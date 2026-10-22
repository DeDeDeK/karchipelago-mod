# Drive Backwards Event

A custom City Trial event (`CUSTOM_EVKIND_DRIVE_BACKWARDS`, kind 32) that makes every machine drive
tail-first for 15 seconds. Machines face the way they always do, the camera sits behind the nose as
usual and the stick steers as usual, but thrust, boosts and charge releases push the machine out of
its tail, so a player watches their machine back toward the screen. Riders on foot are untouched.
It runs through the `custom_events` framework with a siren intro, sky preset 14 (Dark Purple), BGM
file 0x2d (`event_fog`), a 900-frame duration and roll weight 20.

## How It Works

A machine's driving code works along its heading, `MachineData.forward` (+0x418), and the matching
`left` (+0x430): the steer rotates it, the thrust and charge boost push along it, the grip drags
velocity onto it, and map collision builds the body frame its probes sit in from it. The model,
the rider's seat, the hitboxes and the chase camera read the same vector after the physics is
done.

`Machine_Create` (0x801c552c) gives every machine its per-frame procs by priority:

| Priority | Proc | Runs |
|---|---|---|
| 0 | `Machine_FrameStartThink` (0x801c5fe0) | Charge tick, frame resets |
| 1 | `MachineGObj_AnimThink` (0x801c618c) | The state's `anim_cb` |
| 3 | `Machine_InputThink` (0x801c628c) | The state's `input_cb` |
| 4 | `Machine_PhysicsThink` (0x801c6368) | The state's `phys_cb`, then velocity and the move |
| 5 | `Machine_EnvCollThink` (0x801c65a8) | The state's `envcoll_cb` and map collision |
| 6 | `Machine_ModelMatrixThink` (0x801c6668) | The model matrix and the rider seat |

At boot the mod repoints the `lis`/`addi` pairs that load the procs at priorities 0-5 to wrappers.
While the event runs, each wrapper negates `forward` and `left` (keeping `up`, so the basis stays
right-handed), runs the original proc, and negates them back. Inside those procs the machine's
heading is its tail, so everything that turns heading into motion drives it tail-first; outside
them, the model, hitboxes and camera see the nose where it always was. Each wrapper flips around
its own call only, because other GObjs' procs at the same priorities - riders, items, the CPU
brain - run between a machine's procs and read its heading too.

Steering is unchanged on screen. Rotating the negated heading by an angle about `up` rotates the
real heading by the same angle, so pushing left still turns the nose left. A write to the heading
inside a wrapped proc - a wall bounce, a rail grab, a respawn - is in the flipped frame and comes
out negated, which keeps it consistent with the reversed travel.

`start` sets the flag and `end` clears it on the first frame of state 3, as the sky and music go
back; `abort` clears it too, since every machine of the next scene runs the same wrappers.

## Behavior

- **The switch is physical.** At the start a moving machine is suddenly travelling along its new
  heading's tail, and its grip and thrust brake it and turn it around; the same happens in reverse
  at the end. A high-grip machine reverses at once, Slick Star takes seconds.
- **Every machine** reverses, ridden or not, CPUs included. The CPU brain still reads the real
  heading, so CPUs aim their nose at what they want and back away from it.
- **Hitboxes stay on the nose.** The ram and attack regions are placed from the heading outside
  the wrapped procs, so a reversing machine rams with its trailing end.
