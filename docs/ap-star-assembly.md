# Archipelago Star Assembly

The Archipelago Star is assembled in City Trial out of six colored spheres, the way
Hydra and Dragoon are assembled from their three parts. The spheres are custom items
delivered by the same forced-content red box the vanilla pieces ride in on, tracked on
a HUD row of their own, and collecting all six mounts the player on the star.

Assembly is one of two ways to get the star. The other is finding one already built on the
field: the machine's `CustomMachineDesc` carries a City Trial spawn weight of 1.0, the same
token weight `mods/archipelago/src/gate_machines.c` hands Hydra and Dragoon, so once the
Archipelago Star machine item is in it turns up loose at roughly 0.8% of field spawns. That
machine item and the six sphere items are independent of each other.

The collection half is `mods/ap_star/src/ap_star_pieces.c`; the gate it reads and the
handler list it fires are in `mods/ap_star/src/ap_star.c`. Both need `custom_items` for the
sphere items and `custom_machines` for the machine; with either missing nothing resolves, no
schedule is installed, and City Trial is untouched.

## The Six Spheres

`mods/ap_star/assets/items/ApSphere*.dat` are six `customItem` archives, one per
color of the Archipelago logo, ordered the way the logo reads - rose at twelve
o'clock, then clockwise:

| Piece | Color | Item name | File |
|---|---|---|---|
| 0 | `#C97682` rose | `AP Sphere Rose` | `ApSphereRose.dat` |
| 1 | `#75C275` green | `AP Sphere Green` | `ApSphereGreen.dat` |
| 2 | `#CA94C2` violet | `AP Sphere Violet` | `ApSphereViolet.dat` |
| 3 | `#D9A07D` tan | `AP Sphere Tan` | `ApSphereTan.dat` |
| 4 | `#767EBD` blue | `AP Sphere Blue` | `ApSphereBlue.dat` |
| 5 | `#EEE391` yellow | `AP Sphere Yellow` | `ApSphereYellow.dat` |

The display name is the handle: mod code matches a pickup to its piece index by
`CustomItemDesc.name`, so renaming a sphere in the archive unbinds it from the set.
`custom_items` reads that name out of each archive during boot discovery, so the six
hashes resolve before the first round - a sphere held disabled by the gate below is
never registered, and would otherwise never be named.

Each descriptor clones `ITKIND_HYDRA1` (55) for behavior - a legendary piece's physics,
state class and threshold category - and overrides four things:

- **the model**, a generated UV sphere rather than anything carved out of `Item.dat`,
  which holds no sphere, at `scale` 0.525. A Hydra piece's half-extent belongs to a flat,
  hollow shape; a solid sphere of the same extent reads as much bulkier, so it is
  brought down from there;
- **the effect record**, a `PatchEffectInfo` with `count = 0`. That is what keeps the
  clone from crediting a real Hydra piece: `Machine_OnTouchItem` (`0x801db34c`)
  dispatches on the *effect type*, not on `ItemKind`, and types 27-32 are the arms that
  OR a bit into a player's Dragoon or Hydra mask. With no entries the loop never runs,
  the sphere grants no stat, and collection is driven entirely by the `custom_items`
  pickup handler;
- **the material animation**, dropped with the descriptor's `NO_MAT_ANIM` flag. Hydra's
  piece animation drives diffuse R/G/B over a looping 240-frame track, and bound to a
  solid-colored sphere it cycles the piece through colors that are not its own - which
  is exactly the thing a player has to read to know which sphere they are picking up;
- **the joint animation**, replaced through the descriptor's `joint_anim` field. A joint
  animation binds by tree position, so Hydra's - which squashes its second joint's X and
  Y between 1.0 and 0.7 every 30 frames - lands on the sphere's geometry joint and
  throbs the whole ball. The replacement holds the same 30-frame half period and
  240-frame loop but breathes uniformly between 1.0 and 0.94.

The state script still comes from kind 55.

All three box weights and all six event weights are zero, so a sphere never enters a
spawn pool - `PoolAppend` in `mods/custom_items/src/item_registry.c` skips a zero weight
outright. The only way one reaches the field is the schedule below.

Two consequences of cloning kind 55 are worth knowing. `Ply_IncrementItemCollectNum`
(`0x8022fbcc`) is called with the clamped instance kind, so a sphere pickup bumps
`PlayerStats.item_collect[55]`, the Hydra Part X counter - no checklist cell reads it.
And the spawn-position reservation released on destroy is looked up by the same clamped
kind, so a sphere's destruction can free a real Hydra piece's reserved position; the
only effect is that a position may be reused sooner.

## Delivery

Vanilla legendary pieces are in no box pool at all. `CityItemSpawn_UpdateAndCheckToSpawn`
(`0x800ea6e0`) asks `CityItemSpawn_CheckToSpawnLegendaryPiece` (`0x800ed2f0`) whether a
piece is due, and a `2` back makes `CityItemSpawn_Think` (`0x800eb108`) spawn a red box
of the largest size and hand it to `CityItemSpawn_SpawnLegendaryPiece` (`0x800ed384`),
which writes the piece's `ItemKind` into that box's `forced_item` (`ItemData + 0x35c`).
The box then holds exactly that item. The Archipelago set rides the same two seams,
each taken with a `CODEPATCH_REPLACECALL`:

| Site | Replacement |
|---|---|
| `0x800ea7e0`, the `bl` into `CityItemSpawn_CheckToSpawnLegendaryPiece` | runs the vanilla check first; only if it returned `3` (neither vanilla set wants this carrier) and the AP set's next threshold has passed does it claim the carrier and return `2` |
| `0x800eb27c`, the `bl` into `CityItemSpawn_SpawnLegendaryPiece` | forwards to vanilla unless the AP set claimed this carrier, in which case it writes the sphere's kind with `LegendaryPiece_MarkAsSpawned` and clears the request itself |

Vanilla keeps priority at every step, so a round that rolls Hydra or Dragoon on still
delivers their parts on schedule; the AP spheres take the carriers left over.

The schedule is rolled per round in `ApStarPieces_On3DLoadEnd`, mirroring
`LegendaryPieces_Init` (`0x800ecfac`): the unlocked pieces are shuffled into a delivery
order, and each step draws a match-progress threshold out of its own window.

| Step | Window (percent of the round elapsed) |
|---|---|
| 1 | 10-20 |
| 2 | 20-32 |
| 3 | 32-45 |
| 4 | 45-58 |
| 5 | 58-70 |
| 6 | 70-85 |

Vanilla spreads three parts over 15-30 / 25-50 / 50-80 and rolls a flat 30% per machine
for whether that machine's set appears at all. The AP set divides the same span into six
and is always armed, so a full round always offers every sphere the player owns - a
six-piece set behind a per-round coin flip would rarely finish.

Nothing has to be done about three legendary sets competing for carriers.
`CityItemSpawn_Think` runs every frame and a due piece short-circuits the ordinary spawn
timer, so three sets due at once take three carriers on three consecutive frames rather
than starving each other. A box carries exactly one piece either way: `forced_item` is a
single `ItemKind` and `Box_OutcomeLogic` (`0x80250ae8`) spawns one item from it.

The set arms only in `CITYMODE_TRIAL`. Free Run and the stadiums leave it disabled.

For testing, `archipelago_debug` drops one sphere in front of player 1 on each **R + D-Pad
Down**, walking the six in order, so six presses and six drive-overs run the whole assembly
without waiting on the schedule. It goes through `ArchipelagoAPI.DebugSpawnApStarPiece`, which
reads the sphere's `ItemKind` out of the `custom_items` registry - a sphere that was locked when the
scene loaded was never registered and cannot be spawned until it is unlocked and the round
reloads. Deathlink's trigger is **L + D-Pad Down**, and a bare **D-Pad Down** spawns an AP Box.

## Gating

Each sphere carries a gate bit in `ap_star_piece_gate`, and a sphere whose bit is clear is
held out of the item registry entirely. `ApStarPieces_On3DLoadStart` calls
`CustomItemsAPI.SetEnabled` on each sphere with its bit, which is early enough:
`custom_items` registers its items at `CityItemSpawn_Init`'s epilogue, and a gated item is
skipped there, so it never gets an `ItemKind` and no path can spawn it. A round arms only
the spheres that are in, taking the first rows of the delivery schedule, so a partial set
still delivers - it just cannot complete.

Every bit of the gate starts set, so the mod on its own assembles the star the way Hydra
and Dragoon assemble. A consumer narrows it through `ApStarAPI.SetPieceEnabled` or
`SetPieceMask`. The API is phrased as a gate rather than an unlock, because whether a
sphere is earned, bought or awarded is the consumer's idea - all the mod knows is which
spheres are in play, and announcing anything about one arriving belongs to whatever
narrowed the gate.

`archipelago` is such a consumer. Each sphere has its own Archipelago item - IDs 820-825,
one per sphere - and `mods/archipelago/src/gate_ap_star.c` holds
`APSave.ap_star_piece_unlocked_mask` and pushes it into the gate on every write. It pushes
rather than letting `ap_star` read, because mods run in the order their `.bin` files sit in
the FST and `ap_star` sorts before `archipelago`: by the time `archipelago`'s own load-start
callback runs, `ap_star` has already armed the round.

`SetEnabled` writes `custom_items`' consumer gate, which is a field of its own alongside
the per-item settings-menu toggle rather than the same one - so driving it on every scene
load never overwrites the player's saved menu choice. Both gates have to be open for a
sphere to reach the field, which means an unlocked sphere still stays out of the round if
its `custom_items` menu toggle is off.

The Archipelago mask is its own `APSave` field rather than six more bits of
`item_unlocked_mask`, which `ITUNLOCK_NUM` has all but filled, and it is reached through
the ordinary `AP_UNLOCK_AP_STAR_PIECE` category. It follows the City Trial item gate: with
that gate off `archipelago` pre-fills all six at connect, unless `GOALGATE_AP_STAR_PIECES`
says the seed's goal is the assembly, in which case the six stay locked and the apworld
ships them as items.

The Archipelago Star's **machine** item (856) is a separate thing. It decides whether the
assembled star spawns loose on the City Trial field and whether it is selectable, the same
split Hydra and Dragoon have between their piece items and their machine items. Assembling
the star mounts the player on it whatever that bit says, exactly as assembling Hydra from
three parts hands over Hydra.

## Collection

`custom_items` fires the mod's pickup handler from its hook on `Machine_OnTouchItem`,
naming the item and the collecting player. The handler ORs a bit into a per-player
six-bit mask held in the mod (`PlayerData + 0x908` has room for three bits per vanilla
set and two spare, nowhere near six) and cleared on every 3D scene load, so a stadium
trip mid-trial restarts the collection - the same scope the vanilla sets have.

Each pickup climbs `Ply_OnLegendaryPieceCollect` (`0x8027a4e8`), whose ladder is written
for a three-piece set: counts 1, 2 and 3 play rising tones, and 4 plays the pair of
sounds the assembly cinematic uses on completion. Six pieces climb the same three rungs
two at a time, and the sixth plays the completion pair.

Losing a piece is the one vanilla behavior the set does not have.
`Rider_TickDropAllUp` (`0x8019d55c`) builds its candidate list from the two vanilla
masks, so a hard enough hit can knock a Hydra part loose but never a sphere.

## The Tracker

The vanilla piece HUD is a position model, `ScInfSpPos{N}_scene_models` out of
`IfAll21/22/24.dat`, that is nothing but a root plus six geometry-less anchor joints in
one horizontal row - joints 1-3 for the Hydra icons, 4-6 for the Dragoon ones. An icon
is a separate HUD element created when a piece is picked up and placed on the first free
anchor of its half, so a slot says how many pieces are held rather than which. Nothing
is drawn for a piece the player does not have.

The Archipelago row is a second row of the same shape rather than a share of that one,
because all six of its anchors are already spoken for. Anchor positions are read
straight off the position model's `JOBJDesc` - the anchors are children of the root with
no rotation and unit scale, so a world position is the sum of two translations - and the
AP row is that set shifted 3.4 HUD units down. Reading the descriptor rather than an
instance means the AP row needs no position-model element of its own, and it picks up
the per-player-count spacing (2.5 / 2.4 / 2.1 units) for free.

An icon is created exactly the way a vanilla one is: `HUD_CreateElement` on the
collecting player, relinked to `GAMEPLINK_PAUSEHUD` with `GObj_SetPLink`, given element
data of kind `0x3b`, and positioned at its anchor. The vanilla tracker diffs the piece
mask against a cached copy once a frame rather than reacting to the pickup; this does the
same, so no GObj is created from inside the collision call that collected the sphere.
Icons are destroyed on assembly, which is also when the vanilla mount clears its own masks
and the vanilla icons vanish.

The art is `mods/ap_star/assets/ApPieceIcons.dat`, one alpha-cut textured quad per
color under a single `apPieceIcons_scene_models` public, each a 32x32 RGB5A3 shaded ball
on a 2.0-unit quad, inside the 2.5-unit anchor spacing it is hung on.

## Assembly and the Mount

Collecting the sixth sphere clears the player's mask and their icon row, fires the handlers
registered with `ApStarAPI.AddAssembleHandler`, and starts the cinematic, which owns the
mount and plays the completion sounds 150 frames later. `archipelago` is on that handler
list, and what it does there is latch the checklist objective.

The cinematic is not this mod's. `custom_machines` owns the vanilla legendary cutscene and
drives it for any registered machine off the archive names in the machine descriptor - the
star's descriptor names `ApStarGlow.dat` (models plus the camera animation) and
`ApStarParts.dat` (the parts), and both are queued alongside `VsDragoon.dat` and
`VsHydra.dat` when City Trial loads, so the run's synchronous load never hits the disc. The
star gets the same 28-frame lead-in, world freeze, HUD drop, rider pose, scripted camera,
150-frame run, audio bracket and legendary theme Hydra and Dragoon get, with six pods flying
in on six streaks where Hydra has three parts on three.

This mod's whole share is one call, on the frame a player completes the set:
`ApStar_StartAssembly(ply)`, which resolves the star's `MachineKind` and hands it to the
registry. It returns 0 when the cinematic could not run - no machine registered, one already
up, or a rider the vanilla assembly state does not cover, since
`Rider_EnterLegendaryAssembly` (`0x8019248c`) ignores Meta Knight and King Dedede and the
cinematic would play and hand back no machine - and the caller then owes the plain mount and
the completion sounds instead.

It runs under `machine_index` 1, so the rider gets Hydra's pose and the motion script that
fires the machine swap, and the shot gets Hydra's SFX and sky preset. Nothing downstream
reads the index again, so the machine that arrives is entirely the star's own
`(is_bike, class slot)` pair, written over `RiderData + 0x944` / `+ 0x948`.

The mount itself is the tail of the vanilla assembly, and what that tail does is general:
the rider's assembly state stages that pair, and the state's own motion script fires
`Rider_RespawnFullRecreate` (`0x80193900`) on it. The star's class slot comes from
`CustomMachinesAPI.ClassIndexFromKind` rather than a literal, since it is whatever the
registry handed the machine this boot, and the player's `starting_machine_idx` is set to the
star as well so a later respawn keeps it.

`MountStar` is the fallback for the cases the cinematic cannot cover, firing that recreate
directly with no presentation around it. It waits for the frame boundary, since collection
lands inside `Machine_OnTouchItem` and the recreate would tear down the machine that call is
running on; with a cinematic the mount comes out of its own proc instead, which is already
past that call.

A player already riding the star is re-mounted like anyone else, which costs them the
patches on the machine the recreate tears down. That is what vanilla does: the pickup arm of
`Machine_OnTouchItem` (`0x801db6b0`, `0x801db798`) checks only that the count reached three
and that no cinematic is already running, so a player riding Hydra who collects three more
Hydra pieces gets a fresh Hydra with base stats.

## The Cinematic's Archives

The cinematic reads a `vsData`: a glow-model triple `{JOBJDesc*, FigaTree*, MatAnimJoint*}`,
a parts-model triple of the same shape, and a pointer to a word holding a camera-animation
descriptor. The two halves come from different donors, so they are two archives and the
three-pointer block is assembled in mod RAM.

| File | Publics | What it is |
|---|---|---|
| `ApStarParts.dat` | `apStarParts` | the star's own model plus a 150-frame FigaTree that flies the pods in |
| `ApStarGlow.dat` | `apStarGlow`, `apStarCam` | Hydra's streaks and flashes rebuilt for six pods, plus the camera descriptor |

Both are written by `uv run python scripts/authoring/make_ap_star_assembly.py`.

**The parts model** is a carve of `VcStarAp.dat`'s main model, all 17 joints. Each drawn
joint's DObj chain is trimmed to its high LOD, plus the pods' XLU glow quad, because the
cinematic's render callback draws every DObj and the two lower LODs would otherwise z-fight
the one meant to be seen. `MatAnimJoint` is null - the pods' static material colors are
already correct.

The choreography is authored in the ring pivot's frame, where the six pods already sit on a
2.2-unit ring at height 1.05, one per logo color in the same order as the six spheres. Each
pod starts 22 units out at height 12, on an azimuth 85 degrees ahead of its own, and spirals
in: most of the distance early, then a hover, then the last snap, the shape Hydra's parts
use. Pod `i` lands on frame `70 + 4i`, so the six fill the same 70-90 window Hydra's three
land in, and each lands with a damped radial overshoot and a scale pop. The ring pivot then
turns one full revolution over frames 95-150, which is the beat Hydra's wings unfolding
occupies.

**The glow model** is Hydra's. Its three streak groups are cut to one before the carve, then
copied back out to six. A group is four joints - group root, envelope mesh, anchor bone,
head bone - and each copy needs its own DObj, POBJ and envelope array so its two bones
resolve to its own joints; the material, vertex array and display list are shared. The
material-animation tree is rebuilt to mirror the joint tree node for node, with all six
groups sharing one alpha curve, re-keyed to hold a streak up until its own pod lands rather
than fading on Hydra's single 20-50 schedule.

Each streak is aimed by a constant rotation on its mesh joint, chosen so the group's local
+Y runs out along its pod's approach; the head bone then follows the pod through the inverse
of that rotation, so the ribbon traces the pod exactly. The ribbon is a four-vertex quad in
the mesh joint's local XY plane with two verts bound to each bone, and the head bone's
inverse bind matrix sits its pair 7.89 units back down the bone's own Y, so the tip trails.
It is aimed to reach 82% of the pod's distance, about what Hydra's does. The group root
strobes on and off every frame from 20 until its pod lands, as Hydra's does from 20 to 81.

The five flash joints keep Hydra's own tracks. Only the flare's visibility is re-authored,
from three pulses to one per pod.

**The camera** rides along byte for byte: its descriptor block is self-contained in
`VsHydra.dat`, every reloc inside it targeting inside it, with only `vsData[2]` pointing in
from outside. One thing is retuned. The eye path is a spiral held by a JOBJ that carries the
spline, and because the spline lies in its own XY plane that joint's `scale.x` and `scale.y`
are the two horizontal axes of the orbit. Both come in to 0.85, since the star measures
3.01 x 3.46 across where Hydra measures 3.51 x 4.25. The eye height is left alone, which
reads the flat star's ring of pods at a steeper angle than Hydra's bulk needs.

## Authoring the Spheres and the Icons

`uv run --with pillow python scripts/authoring/make_ap_star_pieces.py` writes all seven archives -
the six sphere items and the icon set - from the palette held at the top of the file.

The sphere is generated, not carved: a UV sphere of radius 2.08 (a Hydra piece's
half-extent, which the descriptor's `scale` then trims) at
16 segments by 12 rings, 178 vertices and 352 triangles, with per-vertex normals and a
`CONSTANT | DIFFUSE` untextured material carrying the color as its diffuse and a
darkened copy as its ambient. That is how the vanilla piece parts are lit, minus their
texture. The index-8 vertex arrays cap at 255 entries, which is what bounds the resolution.

It is back-culled: `HSD_PObjDisp` (`0x80407988`) maps `POBJ` flag `0x8000` to
`GX_CULL_BACK`, and the engine's front face is the winding whose right-hand normal points
away from the camera, so an outward-facing surface is wound clockwise. That depends on
the mesh being wound consistently, which the script checks before it writes anything.

The joint animation is authored alongside them: an `AnimJointDesc` pair mirroring the
model's two joints, with the root bare and an `AObjDesc` on the geometry joint holding
one keyframe stream shared by the SCAX/SCAY/SCAZ tracks. Its keys are raw floats
(`value_flag` 0) rather than the packed integers the vanilla tracks use. Looping is not the
animation's own: `CityItem_BindStateAnim` (`0x80251894`) reads bit 30 of the item's
`ItemAnimEntry` flags, inherited from kind 55, and loops every bound `AObj` at runtime.

The icons are rendered with a fixed light from the upper left, supersampled four times
and downsampled so the rim gets a soft alpha edge instead of a stair-step, then encoded
RGB5A3 - the format costs 2 KB per icon and carries the alpha the quad's cutout needs.

## The Archipelago Locations

`APCK_ASSEMBLE_AP_STAR` is `clear_kind` 50 on the Archipelago checklist tab, which makes
it AP location 411. Its predicate is a read of a boot-sticky flag the assembly sets, so
the cell also fills in on a later load rather than only in the session that earned it. On
the apworld side the location takes the City Trial region and requires all six sphere
items whenever City Trial items are gated.

`APCK_ASSEMBLE_ALL_LEGENDARY` is `clear_kind` 51 (AP location 412): Dragoon, Hydra and the
Archipelago Star all assembled by one player in one round. Its three inputs are per-round -
`PlayerStats.flags_84d` bits `0x04` and `0x08`, which `Ply_MarkLegendaryMachineAssembled`
(`0x80231198`) sets and which are zeroed with the rest of `PlayerStats` on scene load, plus
the star's own assembly mask, cleared at the same point. It is polled from
`APCheckDetect_OnFrameStart` in `mods/archipelago/src/ap_check_detect.c` rather than the
per-rider sampler the other City Trial objectives use, because the mount rebuilds the rider
that sampler hangs off.

Two `APGoalKind` values are backed by these cells: `GOAL_ASSEMBLE_AP_STAR` reads the
`APCK_ASSEMBLE_AP_STAR` bit and `GOAL_ALL_LEGENDARIES_CT` reads
`APCK_ASSEMBLE_ALL_LEGENDARY`. Like the other feat goals they are a `sent_checks` read, so
a goal is satisfied by the same latch that sends the location. Each also protects its own
cell from a checkbox filler, since spending one there would win the seed without the
objective.
