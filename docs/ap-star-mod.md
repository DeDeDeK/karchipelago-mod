# The Archipelago Star Mod

`mods/ap_star/` is the Archipelago Star: the machine archive, the charge-release sphere
shot, the six-sphere City Trial assembly and its cinematic, packaged as one drop-in mod.
Nothing Archipelago-specific is built into it. Built on its own it is a complete
Hydra-style legendary machine - every sphere spawns, the set assembles, the star drives -
and a consumer that wants the spheres behind its own progression narrows that through the
mod's API rather than replacing any of the machinery.

The machine itself is an ordinary `custom_machines` registration, authored whole: a star
of its own, not a copy of a vanilla one standing in. What this mod adds on top is the
behavior only this star has.

## Layout

```
mods/ap_star/
  include/ap_star_api.h            what consumers import
  assets/
    machines/VcStarAp.dat          the machine, discovered by custom_machines
    machines/VcStarAp.art          its UI art, wherever the game draws a machine
    items/ApSphere*.dat            the six spheres, discovered by custom_items
    ApStarShot.dat                 the fired sphere's model
    ApStarAssembly.dat             the assembly cutscene's models and camera,
                                   named by the machine descriptor and run by custom_machines
    ApPieceIcons.dat               the collection tracker's art
  src/
    main.c                         ModDesc, hoshi callbacks, settings page
    ap_star.c / .h                 kind lookup, sphere gate, sphere colors, handler lists, API export
    ap_star_palette.c / .h         the platform color cycle and exhaust tint
    ap_star_shot.c / .h            the charge-release projectile
    ap_star_pieces.c / .h          sphere delivery, collection, tracker, assembly
```

`mods/*/assets/` is copied to the FST root by the ordinary asset step, so a machine mod
needs no packaging change to be found: `assets/machines/VcStarAp.dat` lands at
`machines/VcStarAp.dat`, which is the folder `custom_machines` scans at boot.

The three loose archives sit at the FST root rather than under `machines/`, and have to.
The discovery scan probes every `.dat` in `machines/` for a `customMachine` public and
reports the ones that have none, so a companion archive parked there would be reported as
a broken machine every boot. The root is flat and shared, which makes a distinctive
filename prefix - `ApStar*`, `ApPiece*` - the only namespacing available; the engine's own
loaders reach these by bare filename in any case, through `Gm_LoadGameFile`,
`lbLoadArchive` and `Preload_CreateEntry`.

## The machine

| Thing | Value |
|---|---|
| Archive | `machines/VcStarAp.dat`, public `vcDataStarAp` |
| UI art | `machines/VcStarAp.art` |
| Cutscene | `ApStarAssembly.dat`, public `apStarAssembly`, run as Hydra |
| Descriptor | star class with a `CharacterKind`; stat rows, CPU rows and two exhaust generators, all authored; `audio_kind` 6, blip height 2.2, radar drop 5.87 |
| Display name | `"Archipelago Star"` (`AP_STAR_MACHINE_NAME`) - the handle everything finds it by |
| Class slot and `MachineKind` | whichever appended star slot and kind discovery order hands it: star slot 19, `MachineKind` 26 and `CharacterKind` 20 when it is the only machine registered |
| Logo colors | `#C97682` rose, `#75C275` green, `#CA94C2` violet, `#D9A07D` tan, `#767EBD` blue, `#EEE391` yellow |

`VcStarAp.dat` is the source of truth and is edited in place, the model through
`scripts/hsd/builder.py`'s in-place graph editing over a parsed archive and the descriptor
through `scripts/hsd/machine_descriptor.py`, which prints it - the two exhaust generators its
animation bank emits included - and edits its stat and CPU rows and its heights.
`scripts/hsd/make_machine_art.py` builds the `.art` side-car from two renders, and
`scripts/authoring/make_ap_star_assembly.py` re-carves the cutscene's parts out of the model into
`ApStarAssembly.dat`, so an edit to the star needs that rerun. Every `assets/` tree is `.gitignore`d, so the archive carries
retail-derived data without the repo doing so.

Its attribute and handling blocks carry the Slick Star's values - a ground grip of 0.01 and
an `air_impulse` of 0.01 over a 600-frame `air_recover_len` - so it slides the way the Slick
Star does and is steered by charge-drifting. No `.ssm` ships beside it, so the machine
speaks with its `audio_kind`'s voice - the Slick Star's engine, charge and boost sounds,
unpitched, and the Slick Star's air noise.

**The model.** Six engine pods on an even ring, one per logo color, around a seat over a
solid platform disc. The ring sits at radius 2.20 and height 1.05, each pod scaled to 1.4375;
a charging pod climbs toward white but no more than 10% of the way, and the platform rests at
`#BFF5BF` shaded no darker than 0.75 of it, walking around the pod palette every 12 seconds.

The pods share their geometry: each is its own JObj off the ring pivot whose DObjs point at
common POBJs through DObj/MObj/TObj records of its own, so the archive carries 27 DObjs over
15 pieces of geometry. The joint layout:

| Joint | Role |
|---|---|
| 0-5 | armature root chain, no animation keys |
| 6 | body root, the platform disc as three LOD DObjs |
| 7 | seat hub |
| 8 | ring pivot - the Moving FigaTree's only animated node, three ROT tracks driving the idle spin of all six pods |
| 9-14 | the six pods, children of joint 8, four DObjs each: three LODs plus an always-drawn XLU glow sprite |
| 15 | boost/exhaust particle joint |
| 16 | rider seat joint |

Bone count is 17. Four things outside the JObj tree are keyed to that numbering, and an edit
to the tree has to keep all of them in step: `ModelData.BoneCount`; the three main LOD tables,
whose bytes are flat DObj indices in JObj preorder; the Moving FigaTree's per-node track-count
table; and the three MatAnimJoint trees, which are walked in lockstep with the JObj tree.
`VehicleAttributes+0x00` (the rider sit bone) is 16 and `AnimationBank+0x4c` (the particle
spawn bone) is 15. The shot's pod ring finds the pods by that same numbering, as joints 9-14.

A pod's color is animated rather than static: each of the Moving, Charge and Stop MatAnims -
50, 104 and 3 frames - drives its material's DIFFUSE_R/G/B, black under the body texture and
magenta on the glow sprite, ramping warm while charging, and each pod has its own tinted copy
of the 64x64 body texture. Recoloring a pod therefore rewrites those keyframes as well as the
material color and the texture, keeping each key's intensity and swapping the hue. The Charge
tracks are the only ones with desaturated keys, and none sits more than 10% of the way toward
white, so a charging pod climbs to a slightly hotter version of its own color. A track whose
fixed-point exponent cannot hold a brighter value takes a lower exponent rather than a new
format, which keeps every keyframe buffer the same length.

The six colors and their ring order are the assembly pieces' own list in
`scripts/authoring/make_ap_star_pieces.py`, imported rather than restated, so a pod and the
sphere the player collects for it can never drift apart. Slot 0 sits on +Z, the machine's
front, and the rest run clockwise seen from above. The code carries the same six as
`ap_star_piece_colors`, indexed by `APStarPieceKind`, whose order is the ring's; the shot and the
platform cycle both read it, so a change to the list is made there as well.

The platform disc's color is its material's, so that the platform cycle can animate it. Its
first texture stage is a single opaque texel with both channels set to `PASS`, and its second a
gray sphere map with its luminance on `[0.75, 1.0]`, `MODULATE`d over the material. The
material renders with `RENDER_CONSTANT` and no lighting, so its diffuse is the disc's whole
color and the gloss is all the shading it has.

The exhaust is the four-point sparkle at texture group 0 image 1, and only its color moves. It
emits off the machine's axis and drifts, which is what a machine that slides needs: a generator
that emits down a tight cone pins its trail to where the machine is pointed, and the star spends
much of its time not facing the way it is going.

The descriptor brings both generators, 143 bytes each and identical but for the byte at
`+0x05`: generator 0 while cruising and generator 1 on boost, which the animation bank names as
ids 52 and 53. The trail tint addresses both at the same four offsets, `0x53`, `0x5a`, `0x60`
and `0x89`: the two colors a particle spawns holding, the one it ramps up to over its first three
frames, and the one it fades out through.

## The platform cycle

`ap_star_palette.c` walks the platform disc's materials through the six sphere colors on a
12-second wall clock, and writes the same color into the exhaust generators.

Nothing in the archive can do this on its own. A `MatAnim`'s frame is the machine's state, not
elapsed time: a star's `vcAnimationStar` pairs each joint animation with the material animation
played alongside it, the moving animation's rate rides on velocity, the charge animation's frame
is the 0-100 charge gauge, and both restart when the state changes. So the colors live in the code
and are written into the live materials each frame.

The write goes to `HSD_Material.diffuse` on every DObj of joint 6, and it reaches a pixel only
where the pipeline lets it. `MObjMakeTExp` (`0x803fa0b4`) builds the first TEV stage from the
material's diffuse only when the MObj renders with `RENDER_CONSTANT` and without
`RENDER_DIFFUSE`; every texture on the MObj then runs through `TObjMakeTExp` (`0x803f6860`) in
lightmap groups - diffuse and ambient first, then specular, then ext - and a stage whose colormap
is `REPLACE`, or `BLEND` with a `blending` of 1.0, discards whatever came before it. `PASS` leaves
it alone and `MODULATE` shades it, which is why the disc's two stages are built the way they are.

`MObjLoad` (`0x803f9f04`) gives every model instance its own `HSD_Material` copy, so each star on
the field is written separately rather than through the archive. The write runs from the machine's
Anim handler (`CustomMachinesAPI.SetAnimHandler`), at the end of `Machine_AnimThink` after
`Machine_ColAnimThink` has reapplied the ColAnim overlays, so it is the color that draws. The
handler is claimed from `OnSceneChange` by the first scene change that finds the star registered.
Phase advances on the time-base delta, so the period holds through slowdown; every star on the
field shares one phase, and a gap of a whole cycle or more - no star on the field, or the 32-bit
tick counter wrapping at about 106 seconds - resumes where it left off instead of jumping.

A particle's color is read out of its generator on the frame it spawns, so overwriting the
operands of the generator's color opcodes paints the particles born that frame and leaves the ones
in flight alone - the trail comes out as the cycle stretched along it rather than the whole trail
flashing at once. The generators are the star's own, reached through
`CustomMachinesAPI.GetGenerator`, which hands back the registry's copy the vehicle bank points at,
so nothing else on the field emits a program this paints. Each of the eight operands is resolved
once and checked to sit two bytes past a `0xc0` or `0xd0` color opcode; one that does not is
reported and left alone.

The color is pushed to full saturation before it goes into the trail. Particles blend additively,
so wherever a trail overlaps itself the channels sum and clamp, and a pastel palette arrives at
that sum as white with a thin colored fringe. Stretching each color to full saturation -
`(c - min) * max / (max - min)` per channel - gives up the lightness the blend was going to
destroy anyway and keeps the ratio between channels, which is the part that still reads as a
color. The platform and its trail therefore share a hue rather than an exact value.

## Binding to the machine

`custom_machines` assigns appended kinds in FST scan order, so the star's `MachineKind` and
class slot are whatever the registry handed it that boot. `AP_STAR_MACHINE_NAME`
("Archipelago Star") is the `CustomMachineDesc.name` authored into the archive's descriptor,
and the only thing tying `machines/VcStarAp.dat` to this code - `ApStar_MachineKind()`
resolves it through `CustomMachinesAPI.FindKindByName`, which discovery keeps unambiguous by
refusing a second machine under a taken name. The string lives in `ap_star_api.h` and in the
archive; changing one without the other unbinds the machine, and the code then runs as if the
archive were absent.

Resolution is lazy, not done at `OnBoot`. Mods run in the order their `.bin` files sit in
the FST, `ap_star` sorts before `custom_machines`, and a mod's export is not available
until its own `OnBoot` has run - so an `OnBoot` lookup always answers -1. Everything that
needs the kind asks for it at `OnSceneChange`, `On3DLoadEnd` or later, and every entry point
tolerates -1 by doing nothing.

## The API

Consumers import it with
`Hoshi_ImportMod(AP_STAR_MOD_NAME, AP_STAR_API_MAJOR, AP_STAR_API_MINOR)`.

| call | what it does |
|---|---|
| `GetMachineKind` | the registered `MachineKind`, or -1 |
| `GetPieceName` | one sphere's display name, which is its archive's `CustomItemDesc.name` |
| `SetPieceMask` | the sphere gate, one bit per `APStarPieceKind` |
| `AddAssembleHandler` | called as a player completes a set |
| `AssembledThisRound` | per-player, cleared on every 3D load |
| `SpawnPiece` | drop one sphere in front of a machine |
| `CollectPiece` | add one sphere to a player's set with no pickup |
| `Assemble` | award the star outright, spheres not required |

The gate starts with all six bits set. A closed sphere is held out of the item registry
entirely, so it never receives an `ItemKind` and no path can spawn it; a round arms only
the open ones and a partial set delivers but cannot complete. The gate is read at 3D load
start, because `custom_items` registers its items in `CityItemSpawn_Init`'s epilogue and
that is the last moment a held item can be skipped.

A sphere with no `ItemKind` also stays out of the drop pool. `CollectPiece` ignores the
gate, so a player can hold a sphere that was never registered this round; there is no item
to throw for it, and putting it in the pool would throw `ITKIND_GORDO` and leave the bit
set, so the rider's drop quota would never drain.

`CollectPiece` and `Assemble` are the collection path entered from outside it, and neither
consults the gate: a sphere with no `ItemKind` this round can still be collected, and the
star can be awarded with all six closed. That is what a consumer awarding a sphere or the
machine as a prize goes through, and it is why the gate is about what spawns in a round
rather than about what a player can be handed.

`Assemble` is the set-completion path itself, entered without the set: the cutscene, or
when it cannot run the plain mount `custom_machines` owns (`CustomMachinesAPI.MountMachine`)
and the completion sounds; then the assembled flags and the assemble handlers, with the
player's collected spheres cleared the way a completed set clears them. It answers 0 outside
a City Trial round - the title screen's attract demo included, since that is a real City
Trial round with a CPU in every slot - with the star unregistered, or with the player not
riding. A consumer awarding the star as an item goes through it rather than through the
machine registry, so everything watching the assembly still sees one.

The API is a gate, not an unlock: whether a sphere is earned, bought or awarded is the
consumer's idea, and all this mod knows is which spheres are in play. It says nothing to
the player about one arriving for the same reason, and leaves the textbox alone. It does
own the sphere names, though - they are the `CustomItemDesc.name` of the archives it binds
by, so a consumer naming a sphere takes it from `GetPieceName` rather than keeping a copy
that can drift out of step with `items/ApSphere*.dat`.

## The Archipelago consumer

`mods/archipelago/src/gate_ap_star.c` is the whole of the Archipelago side, and the only
file in that mod that imports this one. It holds `APSave.ap_star_piece_unlocked_mask` -
reached from the client through the ordinary `AP_UNLOCK_AP_STAR_PIECE` category - announces
an arriving sphere with the same `"Unlocked Item: "` textbox every other unlock uses, and
latches `APCK_ASSEMBLE_AP_STAR` from an assemble handler. It also carries the two give
items Hydra and Dragoon have: AP IDs 980-985 collect one sphere into every human rider's
set, and ID 14 runs the assembly outright. Both are sold in the Energy Link shop's
Legendary Pieces menu, and neither is gated on the sphere unlocks, the way a Hydra or
Dragoon part give ignores that part's unlock. Archipelago numbers the spheres
itself, as `APStarPiece` alongside the 820-825 item IDs in `archipelago_api.h`, so its
public header stands on its own; the two orders are a contract, since a sphere unlock item
is applied by index.

The mask it pushes is 0 whenever Archipelago's Red Box unlock is missing, whatever the
sphere bits say. Spheres arrive in the legendary-piece carrier, which is a red box that
hardcodes its color instead of going through the box color picker, so the sphere gate is
the only place box gating can reach them.

It **pushes** the mask into the gate on every write rather than letting `ap_star` read it
back. `ap_star` sorts before `archipelago` in the FST, so by the time `archipelago`'s own
load-start callback runs, `ap_star` has already armed the round. Every writer of the mask
therefore ends in `GateApStar_PushMask()`: the boot restore in `OnSaveLoaded`, the
per-sphere unlock, and `Unlock_SetMask`, which is the single choke point the client and the
ungated pre-fill both go through. The box mask feeds it too, so its writers push as well -
`GateBoxes_UnlockBox` on Red and `Unlock_SetMask` on `AP_UNLOCK_BOX`.

The star is gated like any vanilla machine. Its unlock item is 856
(`AP_MACHINE_UNLOCK_AP_STAR`) and its save bit is bit 26 of `machine_unlocked_mask`
(`AP_MACHINE_BIT_AP_STAR`), both bound to the star through `GateApStar_MachineKind()` - which
asks this mod's `GetMachineKind`, binds at `OnSaveLoaded` and caches the answer - rather than
to a `MachineKind`, since another drop-in machine sorting ahead of `VcStarAp.dat` would move
the star's kind. `GateMachines_SpawnWeight` in `gate_machines.c` weighs the star on that bit
the way it weighs every vanilla kind, so an assembled star turns up loose on the field only
once 856 has arrived; assembling it mounts the player whatever the bit says. Any other
machine `custom_machines` registers has no bit and is left ungated.

**Title screen.** Archipelago also chooses the star as the title screen's idle demo machine,
in `main_menu.c`. The title scene's demo player is set up at `0x8000d300` from three `li r4`
operands: `RiderKind` at `0x8000d340`, `is_bike` at `0x8000d34c`, class slot at `0x8000d358`.
`main_menu.c` rewrites the rider and the class slot on each title entry, since the registry
only resolves after every mod has booted, pointing them at Kirby on the Archipelago Star
through `GateApStar_MachineKind()` and falling back to King Dedede on the Wagon Star when no
such machine is registered. The ride must stay star-class: the demo init uses hardcoded
star-only state ids and a wheel-class machine crashes there. The Wagon Star's idle volume
floor of 20.0 is why the title think and exit callbacks zero the floor for whichever kind the
demo uses and put it back on the way out; a kind whose floor is already 0.0 passes through
unchanged, and machine loops are only ever created at volume 0.0 and ramped up, so this never
lets an audible frame through.

## Settings

The mod carries its own `ModDesc` settings page, **Archipelago Star**, with one toggle and
its own hoshi save slot. **Sphere Shot** (default on) is whether a full-charge release fires a
pod.

Nothing pushes an Archipelago slot option into the page - the toggle is the player's, so a
standalone build can turn it off and an Archipelago build does not override the player's
choice on connect.

## Shipping a second machine

A machine that needs no code of its own is not a mod at all - dropping its `.dat` into any
built mod's `assets/machines/` registers it, star or bike, and it spawns, drives and (if its
descriptor asks) takes a select-screen cell. `CUSTOM_MACHINE_MAX` caps the registry at 13.

A machine that wants behavior copies this mod's shape: a folder under `mods/`, its archive
under `assets/machines/`, its companion archives at `assets/` root under a distinctive
prefix, a `ModDesc` in `src/main.c`, and a lazy `FindKindByName` bind on its own
descriptor name. Per-machine behavior hangs off `CustomMachinesAPI.SetInitHandler`,
`SetThinkHandler` and `SetAnimHandler`, the per-kind handlers a registered machine of either class
runs. The only
build registration needed is adding `include/` to the Makefile's `INCLUDES` list, and only if
the mod publishes an API - sources under `src/` are globbed.
