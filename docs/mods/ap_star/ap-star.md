# Archipelago Star

The `ap_star` mod is the Archipelago Star: a custom star machine, the sphere it fires on a
full-charge release, and the six-sphere City Trial set that assembles it, packaged as one
drop-in mod. Nothing in it is Archipelago-specific. Built on its own it is a complete
Hydra-style legendary machine - every sphere spawns, the set assembles, the star drives - and a
consumer that wants the spheres behind its own progression narrows them through the mod's API
rather than replacing any of the machinery.

The machine itself is an ordinary `custom_machines` registration, authored whole in its own
archive. What this mod adds is the behavior only this star has: the platform color cycle, the
pod ring and its shot, and the sphere set.

## The machine

| Thing | Value |
|---|---|
| Archive | `machines/VcStarAp.dat`, public `vcDataStarAp`, with its UI art side-car `machines/VcStarAp.art` |
| Display name | `"Archipelago Star"` (`AP_STAR_MACHINE_NAME`), the handle everything finds it by |
| Class | star, with a `CharacterKind` and a select-screen cell |
| `MachineKind` and class slot | whatever discovery order hands it: `MachineKind` 26, star slot 19 and `CharacterKind` 20 when it is the only registered machine |
| Voice | `audio_kind` 6, the Slick Star's engine, charge and air noise; no `.ssm` ships beside it |
| Cutscene | `ApStarAssembly.dat`, public `apStarAssembly`, run under legendary index 1 (Hydra) |
| Field spawn weight | 1.0 |

Its attribute and handling blocks carry the Slick Star's values - a ground grip of 0.01, an
`air_impulse` of 0.01 over a 600-frame `air_recover_len` - so it slides the way the Slick Star
does and is steered by charge-drifting. The descriptor's stat rows, CPU rows and two exhaust
generators are its own.

The companion archives - the shot model, the cutscene, the tracker icons - sit at the disc root
rather than under `machines/`. Discovery probes every `.dat` in `machines/` for a
`customMachine` public and reports the ones that have none, so a companion parked there would be
reported as a broken machine on every boot. The root is flat and shared, so a distinctive
filename prefix (ApStarShot.dat, ApPieceIcons.dat) is the only namespacing.

## The model

Six engine pods sit on an even ring around a seat over a solid platform disc, one pod per sphere
color. The joint tree, in `JObj_LoadJoint` preorder:

| Joint | Role |
|---|---|
| 0-5 | armature root chain every star opens with, no animation keys |
| 6 | body root: the platform disc, as three LOD DObjs |
| 7 | seat hub |
| 8 | ring pivot - the Moving FigaTree's only animated node, whose spin all six pods inherit |
| 9-14 | the six pods, children of joint 8, each three LODs plus an always-drawn XLU glow sprite |
| 15 | exhaust particle joint (`vcAnimationStar.particle_bone[0]`) |
| 16 | rider seat (`vcAttributes` sit bone) |

The bone count is 17. Four things outside the joint tree are keyed to that numbering, and an
edit to the tree has to keep them in step: the model data's bone count, the three main LOD
tables (flat DObj indices in preorder), the Moving FigaTree's per-node track-count table, and
the MatAnimJoint trees, which are walked in lockstep with the joints. The code depends on joint 6
for the platform cycle and joints 9-14 for the pod ring.

**The pods.** Slot 0 sits on +Z, the machine's front, and the rest run from there toward +X -
counterclockwise seen from above. That order is `APStarPieceKind`'s, and the code's six-entry
color table, indexed by it, is what the shot and the platform cycle paint with. A pod's color is
animated rather than static: the Moving, Charge and Stop MatAnims drive each pod material's
diffuse R/G/B, ramping warm while charging, and each pod has its own tinted copy of the body
texture. Recoloring a pod means rewriting those keyframes, the material color and the texture
together. No Charge key sits more than 10% of the way toward white, so a charging pod climbs to
a hotter version of its own color.

**The platform disc.** Its color is its material's diffuse, so the platform cycle can write it.
The material renders with `RENDER_CONSTANT` and no lighting; its first texture stage is a single
opaque texel with both color and alpha set to `PASS`, and its second a gray sphere map
`MODULATE`d over it. That shape is forced by the TEV setup: `MObjMakeTExp` (0x803fa0b4) builds
the first stage from the material's diffuse only under `RENDER_CONSTANT` without
`RENDER_DIFFUSE`, and `TObjMakeTExp` (0x803f6860) then runs every texture over it, where a
`REPLACE` stage (or `BLEND` at 1.0) discards what came before while `PASS` keeps it and
`MODULATE` shades it.

**The exhaust.** The descriptor brings two generators, identical but for the byte at `+0x05`:
generator 0 while cruising and generator 1 on boost, which the animation bank names as ids 52
and 53. The platform cycle recolors both at the same four program offsets, `0x53`, `0x5a`,
`0x60` and `0x89` - the two colors a particle spawns holding, the one it ramps to over its first
frames, and the one it fades out through. The sparkle emits off the machine's axis and drifts,
which suits a machine that spends much of its time not facing the way it is going.

## The platform color cycle

The platform disc walks through the six sphere colors on a 12-second wall clock, and the exhaust
trail wears the same color.

Nothing in the archive can do this on its own. A star's `vcAnimationStar` pairs each joint
animation with a material animation, and their frame is machine state, not elapsed time: the
Moving animation's rate rides on velocity, the Charge animation's frame is the 0-100 charge
gauge, and both restart on a state change. So the colors live in code and are written into the
live materials.

The write runs from the star's Anim handler (`CustomMachinesAPI.SetAnimHandler`), which
`custom_machines` calls at the end of `MachineGObj_AnimThink` (0x801c618c), after
`Machine_ColAnimThink` (0x801d60d8) has reapplied the ColAnim overlays - so the color written
is the color drawn. It sets `HSD_Material.diffuse` on every DObj of joint 6, reached through
`CustomMachinesAPI.GetMachineJoint`. `MObjLoad` (0x803f9f04) gives every model instance its own
material copy, so each star on the field is written separately.

The phase advances on the `OSGetTick` delta rather than a frame count, so the period holds
through slowdown. Every star on the field shares one phase, the unsigned subtraction carries the
tick counter's wrap, and a gap of a whole cycle or more - no star on the field - resumes where it
left off. The crossfade between neighbors is smoothstepped so each color holds before giving
way.

**The trail.** A particle's color is read out of its generator on the frame it spawns, so
overwriting the operands of the generator's color opcodes paints the particles born that frame
and leaves the ones in flight alone: the trail comes out as the cycle stretched along it. The
generators are the registry's copies, which the vehicle particle bank points at, reached through
`CustomMachinesAPI.GetGenerator` - nothing else on the field emits them. Each operand is resolved
once and checked to sit just past a `PTCL_OP_COLOR` (`0xc0`) or `PTCL_OP_COLOR2` (`0xd0`)
opcode; one that does not is left alone, and the bind logs how many took.

The trail color is first stretched to full saturation, `(c - min) * max / (max - min)` per
channel. Particles blend additively, so where a trail overlaps itself the channels sum and clamp,
and a pastel arrives at that sum as white. Saturating gives up the lightness the blend would
destroy anyway and keeps the ratio between channels, so the platform and its trail share a hue
rather than an exact value.

## Binding to the registry

`custom_machines` assigns appended kinds in disc scan order, so the star's `MachineKind` and
class slot are whatever the registry handed it this boot. The only tie between the archive and
this code is the display name: `AP_STAR_MACHINE_NAME` is the `CustomMachineDesc.name` authored
into the descriptor, resolved through `CustomMachinesAPI.FindKindByName`. Discovery refuses a
second machine under a taken name, so the answer is unambiguous. Changing the string in one place
and not the other unbinds the machine, and the code then runs as if the archive were absent.

The binding is taken at `OnSaveLoaded`, not `OnBoot`. Mods boot in disc order, `ap_star` sorts
before `custom_machines`, and a mod's export only exists once its own `OnBoot` has run, so an
`OnBoot` import would find nothing. At `OnSaveLoaded` the mod imports the registry, caches the
kind and the star's class slot, and claims the star's Init, Think and Anim handler slots; all of
it is fixed for the run. Without `custom_machines`, or with no machine under the name, the mod
logs once and the shot and the platform cycle stay off. The spheres still deliver and collect;
a completed set then mounts nothing.

## The API

Consumers import it with `Hoshi_ImportMod(AP_STAR_MOD_NAME, AP_STAR_API_MAJOR,
AP_STAR_API_MINOR)`, after `OnBoot`.

| Call | What it does |
|---|---|
| `GetMachineKind` | the registered `MachineKind`, or -1 (also -1 before `OnSaveLoaded`) |
| `GetPieceName` | one sphere's display name, which is its archive's `CustomItemDesc.name` |
| `SetPieceMask` | the sphere gate, one bit per `APStarPieceKind` |
| `AddAssembleHandler` | add a callback run as a player assembles the star |
| `AssembledThisRound` | whether a player assembled the star in the loaded round |
| `SpawnPiece` | drop one sphere in front of a player's machine |
| `CollectPiece` | add one sphere to a player's set with no pickup |
| `Assemble` | put a player through the assembly without the set |

**The gate.** All six bits start set. A closed sphere is held out of the item registry
entirely: the gate is applied at `On3DLoadStart`, before `custom_items` registers its items in
`CityItemSpawn_Init`'s epilogue, so a closed sphere never receives an `ItemKind` and no path can
spawn it. A round arms only the open spheres, so a partial set still delivers but cannot
complete. Because the gate is read at this mod's load start, a consumer that sorts after
`ap_star` must push mask changes as they happen rather than from its own load-start callback,
which runs too late.

The gate is about what spawns, not what a player can be handed. `CollectPiece` and `Assemble`
ignore it: a sphere with no `ItemKind` this round can still be collected, and the star can be
awarded with all six closed. `CollectPiece` leaves a sphere the player already holds as it is and
still answers 1, as a duplicate pickup would; the sixth completes the set. `Assemble` clears the
player's collected set the way a completed set does, then runs the assembly.

**City Trial only.** `SpawnPiece`, `CollectPiece` and `Assemble` answer 0 outside a live City
Trial round. That is a flag raised at the end of a `CITYMODE_TRIAL` round's 3D load and dropped
at every scene change, rather than the game-mode reads alone: `Gm_IsInCity` and the city mode
persist after a round ends, so a menu or Top Ride scene entered from a quit round would otherwise
pass. The flag also stays down for the title screen's attract demo (`Gm_IsAutoDemo`), a real
City Trial round with a CPU in every slot. Each call also answers 0 for an empty player slot, and
`Assemble` with the star unregistered.

**Ownership.** The API is a gate, not an unlock: whether a sphere is earned, bought or awarded is
the consumer's decision, so this mod says nothing to the player about one arriving. It does own
the sphere names, so a consumer naming a sphere takes it from `GetPieceName`. A consumer awarding
the star goes through `Assemble` rather than the machine registry, so everything watching the
assembly still sees one. The `archipelago` mod is one such consumer: it narrows the gate with
`SetPieceMask` and latches its objective from an assemble handler.

**Shot hits.** The header's `AP_STAR_SHOT_ATTACK_CAUSE` (`0x03`) is the attack cause a sphere
shot's hit leaves in the low byte of the victim's `DmgLog.credited_attack`. No vanilla attack
uses it, so a consumer reading a KO's damage log can tell a sphere shot made the credited hit
without importing anything.

## Settings

The mod has its own settings page, **Archipelago Star**, saved by hoshi with the mod. It holds
one toggle, **Sphere Shot** (default on): whether a full-charge release fires a pod. Nothing
pushes a consumer's option into it, so the choice stays the player's.

## Limitations

- The star's kind and class slot depend on which other machines are on the disc; code finds it
  by name and never by number.
- Only one assembly cutscene can run per star per round; a second assembly in the same round,
  a CPU's or an awarded one, mounts the player with no cutscene.
- A machine that wants behavior of its own follows the same shape: its archive under
  `machines/`, companion archives at the disc root under a distinctive prefix, and a
  `FindKindByName` bind at `OnSaveLoaded` that claims the registry's per-kind handler slots.
