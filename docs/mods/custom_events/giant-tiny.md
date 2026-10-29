# Giant Mode and Tiny Mode

Two custom City Trial events that resize every player for the length of the event: Giant Mode
(`CUSTOM_EVKIND_GIANT_MODE`, kind 21, "Everyone is Giant!") grows them to 6x and Tiny Mode
(`CUSTOM_EVKIND_TINY_MODE`, kind 22, "Everyone is Tiny!") shrinks them to 0.25x. They run through
the `custom_events` framework as two kinds sharing one implementation, each with a siren intro, a
900-frame duration and roll weight 10, so together they take one event's share of the roll.

| Kind | Factor | Sky preset | BGM |
|---|---|---|---|
| Giant Mode | 6.0 | `CITYSKY_DUSK` | 0x30, `event_kyoseki` |
| Tiny Mode | 0.25 | `CITYSKY_DAY_2` | 0x2f, `event_itembound` |

The rider model, the ridden machine model and both of their map collision bodies scale together,
so a giant machine rides on top of the floor rather than sinking into it and a tiny one fits where
a normal one does not. Speed, damage and the world are untouched.

## The Levers

Every frame, every player slot - CPUs included - is driven by one eased `factor`.

| Lever | Field | Mechanism |
|---|---|---|
| Rider model | `RiderData.model_scale` (+0x348) | Baked into the rider's model matrix every frame by `Rider_ApplyModelMatrix` (0x80190848), the field Big/Small Kirby uses |
| Machine model | `MachineData.model_scale` (+0x310), written as `model_scale_default` (+0x30C) x `factor` | `Machine_ApplyModelMatrix` (0x801c9074) bakes `model_scale x model_scale_base` into the model matrix about `MachineData.pos` |
| Collision body | The rider's and machine's `CollData` shape: `radius`, `radius2`, `offset` and the machine's three `probes` | Written by the event each frame, `radius2` also through a hook in `mpColl_Update` |
| Camera | Each player camera's eye and interest | Scaled by `sqrt(factor)` about the body the camera tracks |

## Scaling the Collision Body

A rider's or machine's map collision is one sphere described in the owner's body frame: the frame
`mpColl_Update` (0x80245f70) builds from the owner's position and two axes. `CollShapeData.offset`
places the sphere center in that frame, and a machine also carries three `CollProbe` points that
`mpColl_RoughProbe` (0x8024c5b4) sweeps. A machine's center sits one radius above its position -
offset (0, 1.2, 0) with radius 1.2 on a star - so the position is the sphere's ground contact
point, and that is also the point the machine model scales about. Scaling the radius, the offset and
the probes by the same factor therefore scales the whole body about the point the model scales
about, and the two stay matched at any size. Scaling the radius alone would move the body's contact
point away from the model's origin: a giant would hover and a tiny one would sink.

`radius`, `offset` and the probes are written only by `mpColl_Init` (0x80245c10) and
`mpColl_SetProbes` (0x802463a8), so a write from the event holds. `radius2` is different: every
frame each grounded state passes its unscaled radius to `mpColl_Update`, at proc priority 5, after
the event proc at priority 0, while airborne, jumping and rail states pass -1 and leave it alone. A
hook at 0x802460a0 inside `mpColl_Update`, on the store that only runs for a radius2 other than -1,
looks the `CollData` up among the scaled bodies, records the passed value as the new unscaled
radius2 and stores it multiplied by the body's factor. Every other body passes through untouched.

**Re-initialization.** `Machine_SetTransform` (0x801cb50c) re-runs a machine's collision init,
which calls `mpColl_Init` with a rider-dependent radius and rewrites the shape unscaled. Each body
remembers the radius it last wrote; finding any other value there means the game re-initialized it,
so the event captures the new shape as the unscaled one before scaling it again, and the `radius2`
hook stops scaling that body until it has.

## Camera

A hook at 0x800b38e8 in `PlyCamGObj_Think` (0x800b3540), just before the solved interest and eye
go to `CObj_SetInterest` and `CObj_SetEyePosition`, scales both points about the position of the
body the camera tracks, `CamData.target->pos`, as `pos + (point - pos) x camera_factor`. The hook
sits after the whole camera pipeline, so it composes with camera kinds, C-stick zoom and rail
transitions.

Scaling the interest along with the eye is what keeps the player in frame. The camera aims at a
point above and ahead of the body, at offsets sized for a normal player; moving only the eye
toward that point would leave a tiny player below the bottom of the view. Scaled about the body,
the whole rig shrinks or grows around it and the body stays where it normally sits on screen.

The camera scales by `sqrt(factor)` rather than `factor`: 2.45x for a giant and 0.5x for a tiny
player. A giant's camera clears its own model, and on screen the player still looks 2.45x larger
or half the size, which is what sells the effect - a camera scaled by the full factor would make
the player look normal-sized and the world shrink or grow instead.

## Lifecycle

Per player slot the event remembers the ridden machine and, for that machine and the rider, the
unscaled shape. When a slot's machine changes - a dismount, a swap, a lost machine - the previous
machine is restored on the spot to its default model scale and unscaled shape, if it is still on
the machine GObj list, so a machine left behind never stays resized.

| Callback | Does |
|---|---|
| `start` | Sets `factor` to 1.0 and the target to 6.0 or 0.25 |
| `active` | Eases `factor` toward the target by 0.03 a frame and applies every lever |
| `end` | Eases back toward 1.0 through the cleanup phase |
| `end2` | Restores every rider, every slot's machine and the camera |
| `abort` | Forgets every scaled body and resets the camera, without touching the dying objects |

Growing to 6x takes about 167 frames and shrinking to 0.25x about 25, inside the 300-frame
cleanup phase on the way back. The abort matters for both hooks: the next scene's pooled
`CollData` can reuse a remembered pointer, and `PlyCamGObj_Think` runs in every minor-18 scene.

## Limitations

- **Hit collision keeps its size.** Hurtboxes and attack regions are separate from the map
  collision body and are not scaled, so a giant is no easier to hit and a tiny player's ram reaches
  as far as a normal one's.
- **A giant can get stuck.** A 6x star is a 7.2-radius sphere; it cannot fit through narrow gaps,
  and growing under a low ceiling squeezes it between floor and ceiling.
- **The camera can pass through walls.** A giant's camera sits further back than the camera's own
  logic placed it.
- **Only ridden machines resize**, and empty machines keep their size.
- **`RiderData.model_scale` is shared** with every other user of the field, such as a Big/Small
  Kirby effect: the event overwrites it while it runs and leaves 1.0 behind.
