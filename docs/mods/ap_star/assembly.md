# Archipelago Star Assembly

The Archipelago Star is assembled in City Trial out of six colored spheres, the way Hydra and
Dragoon are assembled from their three parts. The spheres are custom items delivered by the same
forced-content red box the vanilla pieces ride in on, tracked on a HUD row of their own, and can
be knocked out of a rider the way a legendary part can. Collecting all six runs the vanilla
legendary assembly cutscene with the star's own art and mounts the player on the star.

Assembly is one of two ways to get the star: its descriptor also carries a City Trial spawn
weight of 1.0, so it turns up loose on the field as well. The sphere half needs `custom_items`
for the sphere items and `custom_machines` for the machine. Without `custom_items` no sphere has
a kind, no schedule is rolled and City Trial is untouched. Without `custom_machines` the spheres
still deliver and collect, and a completed set still fires the assemble handlers and plays the
completion sounds, but there is no machine to mount.

## The six spheres

The spheres are six `customItem` archives in the disc's `items/` folder, one per color of the
Archipelago logo, in `APStarPieceKind` order:

| Piece | Color | Item name | Archive |
|---|---|---|---|
| 0 | `#C97682` rose | `AP Sphere Rose` | `ApSphereRose.dat` |
| 1 | `#75C275` green | `AP Sphere Green` | `ApSphereGreen.dat` |
| 2 | `#CA94C2` violet | `AP Sphere Violet` | `ApSphereViolet.dat` |
| 3 | `#D9A07D` tan | `AP Sphere Tan` | `ApSphereTan.dat` |
| 4 | `#767EBD` blue | `AP Sphere Blue` | `ApSphereBlue.dat` |
| 5 | `#EEE391` yellow | `AP Sphere Yellow` | `ApSphereYellow.dat` |

The display name is the handle. At `OnSaveLoaded` the mod matches each `CustomItemDesc.name` in
the `custom_items` registry to a piece and keeps its id hash; `custom_items` reads names during
boot discovery, so a sphere held closed by the gate - which is never registered - is still named.
Renaming a sphere in its archive unbinds it from the set.

Each descriptor clones `ITKIND_HYDRA1` (55) for behavior - a legendary piece's physics, state
class and threshold category - and overrides four things:

- **The model**, a generated UV sphere rather than anything from `Item.dat`, which holds no
  sphere, at descriptor `scale` 0.525. A solid sphere at a Hydra piece's half-extent reads much
  bulkier than the flat, hollow piece, so it is brought down from there.
- **The effect record**, a `PatchEffectInfo` with no entries. `Machine_OnTouchItem` (0x801db34c)
  credits legendary pieces by the record's effect type, not by `ItemKind`, so with no entries the
  sphere grants no stat and no Hydra part; collection is driven entirely by the `custom_items`
  pickup handler.
- **The material animation**, dropped with `CUSTOM_ITEM_FLAG_NO_MAT_ANIM`. Hydra's piece
  animation cycles diffuse R/G/B over a 240-frame loop, which on a solid-colored sphere would cycle
  the very color a player reads to tell which sphere it is.
- **The joint animation**, replaced through `joint_anim`. A joint animation binds by tree
  position, so Hydra's - which squashes its second joint between 1.0 and 0.7 every 30 frames -
  would throb the whole ball. The replacement keeps the 30-frame half period but breathes
  uniformly between 1.0 and 0.94. It loops because `ItemGObj_BindStateAnim` (0x80251894) loops
  every bound `AObj` when the inherited `ItemAnimEntry` flags ask for it.

The state script still comes from kind 55. All box and event weights are zero, so a sphere never
enters a spawn pool; the delivery schedule below is the only way one reaches the field. Because
the `custom_items` behavior clamp hands the engine the base kind, a sphere pickup bumps
`PlayerStats.item_collect[55]`, the Hydra Part X counter.

## Delivery

Vanilla legendary pieces are in no box pool. `CityItemSpawn_UpdateAndCheckToSpawn` (0x800ea6e0)
asks `CityItemSpawn_CheckToSpawnLegendaryPiece` (0x800ed2f0) whether a piece is due; a return of
2 makes `CityItemSpawn_Think` (0x800eb108) spawn a red box of the largest size and hand it to
`CityItemSpawn_SpawnLegendaryPiece` (0x800ed384), which writes the piece's `ItemKind` into the
box's `forced_item` (`ItemData+0x35c`). The box then holds exactly that item. The sphere set rides
the same two calls, each replaced with `CODEPATCH_REPLACECALL`:

| Call site | Replacement |
|---|---|
| 0x800ea7e0, the call to `CityItemSpawn_CheckToSpawnLegendaryPiece` | runs the vanilla check first; only when it returned 3 (neither vanilla set wants this carrier) and the sphere set's next threshold has passed does it claim the carrier and return 2 |
| 0x800eb27c, the call to `CityItemSpawn_SpawnLegendaryPiece` | forwards to vanilla unless the sphere set claimed this carrier, in which case it writes the sphere's kind with `LegendaryPiece_MarkAsSpawned` and clears the request itself |

Vanilla keeps priority at every step, so a round that rolls Hydra or Dragoon still delivers their
parts on schedule, and the spheres take the carriers left over.

**The schedule.** Each City Trial round, at the end of its 3D load, the spheres that received an
`ItemKind` are shuffled into a delivery order and each step draws a match-progress threshold from
its own window, mirroring `LegendaryPieces_Init` (0x800ecfac):

| Step | 1 | 2 | 3 | 4 | 5 | 6 |
|---|---|---|---|---|---|---|
| Window, % of the round | 10-20 | 20-32 | 32-45 | 45-58 | 58-70 | 70-85 |

A round with fewer spheres in play takes the first rows, so a short set still delivers early.
Vanilla rolls per round whether each legendary set appears at all; the sphere set is always armed,
because a six-piece set behind a per-round coin flip would rarely finish.

**One delivery each.** Vanilla sends each part exactly once, and so does the sphere set, which
works because the carrier never expires. `LegendaryPiece_ClearSpawnRequest` (0x80252e74) writes
-1 into the box's `ItemData.lifetime` (`+0x44`), and `CityItem_LifetimeThink` (0x8024fa38) only
decrements a positive lifetime and only expires one that reaches 0, so a carrier stands for the
rest of the round until a player breaks it. The sphere inside is not immortal: `Box_SpawnContents`
(0x80253378) builds it through `Item_InitDesc` (0x802509a0), which gives every item an ordinary
random lifetime, so a broken-open sphere left on the ground expires and is gone for the round.

**Finding one.** Nothing announces a carrier: no sound, no marker, no message, on the vanilla
path or this one. What identifies it is its size. City Trial's box chance table is
`[20, 15, 10, 5, 4, 3, 7, 7, 0]`, color-major over blue, green and red by small, medium and large
(`GrBoxGeneratorDetermine`, 0x800ebc04), so an ordinary red box is never large and a large red
box is always a legendary or sphere carrier. Sets due at once do not starve each other:
`CityItemSpawn_Think` runs every frame and a due piece short-circuits the ordinary spawn timer, so
they take carriers on consecutive frames, one item per box.

The set arms only in `CITYMODE_TRIAL`. Free Run, the stadiums and the title screen's attract demo
(`Gm_IsAutoDemo`) leave it off: in the demo no sphere is registered, no schedule is rolled, and
`CollectPiece` and `Assemble` refuse. Outside the demo nothing asks who the player is - a CPU that
assembles the star gets the cutscene and the machine exactly as it gets Hydra's - so a consumer
that only cares about humans filters on the `ply` its assemble handler receives.

## Gating

A sphere whose gate bit is clear is held out of the item registry. At `On3DLoadStart` the mod
calls `CustomItemsAPI.SetEnabled` on each sphere with its bit, and with the attract demo held out
alongside. That is early enough: `custom_items` registers its items at `CityItemSpawn_Init`'s
epilogue and skips a disabled one, so it never gets an `ItemKind`, no path can spawn it, and the
round's schedule leaves it out. A partial set still delivers but cannot complete.

`SetEnabled` writes `custom_items`' single per-item gate, shared by every consumer of that
registry, so the last writer before `CityItemSpawn_Init` decides whether a sphere reaches the
field. Opening a gate mid-round does nothing until the next round loads.

## Collection

`custom_items` calls the mod's pickup handler once a touch collects a sphere, with the item's id
hash and the collecting player. The handler sets a bit in a per-player six-bit mask kept in the
mod, beside the three-bit masks `Ply_GetHydraPieceMask` (0x8022cce8) and `Ply_GetDragoonPieceMask`
(0x8022cdac) read for the vanilla sets. The mask is cleared at every scene change, so each round
starts empty, the same scope the vanilla sets have.

Each pickup plays `Ply_OnLegendaryPieceCollect` (0x8027a4e8), whose ladder is written for three
pieces: counts 1, 2 and 3 play rising tones and 4 plays the completion pair. The first five
spheres climb the three rungs two at a time. The sixth plays no rung; it hands off to the
assembly, whose cutscene plays the completion pair itself as it ends.

## Dropping a sphere

A sphere comes loose the way a Hydra part does, which is what makes a single carrier each
survivable: a sphere a rival took can be knocked back out of them.

On the engine side, `Rider_DropPatches` (0x8019d330) queues a drop and `Rider_TickDropAllUp`
(0x8019d55c) throws one legendary piece per cooldown while `RiderData.allups_dropped` is
positive, capped against `Ply_GetHydraCollection + Ply_GetDragoonCollection`. Every part of it is
written against the two vanilla masks - the candidate list is their six bits, the thrown kind is
the list index plus `0x37`, and the bit cleared afterwards is indexed back off that kind - while a
sphere lives in this mod's mask under a custom `ItemKind`. Four seams put it in:

| Site | Patch | Behavior |
|---|---|---|
| 0x8019d4bc, the call to `Ply_GetDragoonCollection` in `Rider_DropPatches` | `REPLACECALL` | add the rider's droppable spheres to the drop quota |
| 0x8019d868, the `stw` of the kind about to be thrown | `HOOKCREATE` | re-roll over the vanilla pieces plus the rider's droppable spheres |
| 0x8019d8d4, the call to `Ply_DecrementItemCollectNum` | `REPLACECALL` | charge a sphere's decrement to `ITKIND_HYDRA1` |
| 0x8019d8f8, the `lwz` that reloads the kind for the mask clear | conditional hook | for a sphere, clear its own bit and resume at 0x8019d950, past both vanilla mask branches |

**The quota.** Without the spheres in the sum, a rider holding only spheres queues no legendary
drop at all. Only *droppable* spheres count - ones with an `ItemKind` this round. `CollectPiece`
ignores the gate, so a player can hold a sphere that was never registered, and there is no item
to throw for it.

**The roll.** Vanilla has already picked uniformly among its two masks by the time the kind is
stored, so the hook re-rolls over that count plus the droppable spheres, keeping every throwable
piece equally likely. It reads a kind of 54 as "no vanilla piece": the candidate list's first
entry is seeded to -1 and `0x37` is added even when the list is empty. Vanilla never throws that
value because its quota is zero with no piece held; putting spheres in the quota makes it
reachable, and left alone it would throw an `ITKIND_GORDO` and never clear a bit, so the quota
would never drain.

**The counter.** `PlayerStats.item_collect[]` has `0x45` entries indexed by `ItemKind` and
`Ply_DecrementItemCollectNum` (0x8022fb58) bounds neither end, so a sphere's own kind would write
past the struct; the pickup counted the clamped base kind, and the drop takes it off that slot.

**The mask.** Both vanilla branches XOR a piece mask by `1 << (kind - 0x37)` or
`1 << (kind - 0x3a)`, which a sphere kind runs off the end of. The conditional hook clears the
sphere's own bit instead and leaves the rest of the bookkeeping - progress, cooldown,
`allups_dropped`, the remaining count - to vanilla. The tracker diffs the mask once a frame, so
the icon leaves with the bit and no GObj is touched from inside the throw.

## The tracker

The vanilla piece HUD is a position model whose instance per viewport is
`Game3dData.legendary_hud_gobj[view]`: a root plus six geometry-less anchor joints in one row,
1-3 for the Hydra icons and 4-6 for the Dragoon ones. An icon is a separate HUD element placed on
the first free anchor of its half, so a slot says how many pieces are held, not which.

All six anchors are spoken for, so the sphere row is a second row of the same shape: an icon goes
where vanilla would put one on the same anchor, shifted 3.4 HUD units down. Reading the anchor's
world position off the viewport's own instance, rather than off the descriptor, is what carries
the split-screen placement: `HUD_CreatePlyElement` (0x80114ba4) moves an element's root to a
per-viewport offset whenever more than one viewport is up. Vanilla builds a row only for a
viewport that is on (`Gm_IsViewOn`, called at 0x80113ed8), so only a player with a screen gets
one; a CPU's spheres are tracked in the mask and drawn nowhere.

An icon is made the way a vanilla one is: `HUD_CreatePlyElement` on the collecting player's
viewport (its first argument is the viewport, not the player), relinked to `GAMEPLINK_PAUSEHUD`,
given element data of `HUDKIND_LEGENDARYPIECE` with the player and the viewport, and placed on its
anchor. Like the vanilla tracker, the row diffs each player's mask against what it shows once a
frame instead of reacting to the pickup, so no GObj is created inside the collision call that
collected the sphere. The diff runs from a proc-only GObj on p_link 0, which neither the match
pause nor a pickup hitstop freezes, so a sphere handed over during either shows at once.

The diff runs both ways. A dropped sphere's icon is destroyed and the icons behind it slide left
onto the freed anchors, so the row keeps collection order and never shows a color the player no
longer holds. Assembly clears the mask, which takes the row down on the next frame, the same
moment the vanilla mount clears its own masks.

The row is built for every City Trial round whatever the gate says, because a sphere handed over
through `CollectPiece` lands whether or not any sphere is in play. Its art is `ApPieceIcons.dat`,
one alpha-cut quad per color under the public `apPieceIcons_scene_models`.

## Assembly and the mount

Collecting the sixth sphere clears the player's mask, runs the assembly, and then calls every
handler added with `AddAssembleHandler`. The cutscene's engine side belongs to `custom_machines`,
which stands in at the vanilla legendary cutscene's seams for any registered machine whose
descriptor names an archive; this mod's whole share is one call,
`CustomMachinesAPI.StartAssembly(kind, ply)`, with the star's `MachineKind`.

`StartAssembly` returns 0 when the cutscene cannot run: the star has no cutscene, one is already
running, the star's cutscene already ran this scene, or the rider is not Kirby -
`RiderGObj_EnterLegendaryAssembly` (0x8019248c) is Kirby-only, and the cutscene would play and
hand back no machine. The mod then mounts the player with `CustomMachinesAPI.MountMachine` and
plays the completion pair (`Ply_OnLegendaryPieceCollect` at count 4) itself. So a second star
assembled in one round, a CPU's or an awarded one, arrives with no cutscene.

The run uses legendary index 1, so the rider gets Hydra's assembly pose and the motion script
that fires the machine swap, and the run gets Hydra's sound and sky preset. Nothing downstream
reads the index again: the vanilla mount fires `Rider_RespawnFullRecreate` (0x80193900) on the
`(is_bike, class slot)` pair the rider's assembly state staged, and `custom_machines` overwrites
that pair with the star's own and records the star as the player's respawn kind
(`CustomMachinesAPI.GetRespawnKind`), so a consumer's respawn hook can keep it.

The cutscene's archive is queued alongside `VsDragoon.dat` and `VsHydra.dat` when City Trial
loads, so the run's synchronous load never hits the disc. The star gets the same lead-in, world
freeze, HUD drop, rider pose, scripted camera, 150-frame run, audio bracket and legendary theme
Hydra and Dragoon get, with six pods flying in on six streaks.

A player already riding the star is re-mounted like anyone else and loses the patches on the
machine the recreate tears down, as a Hydra rider who collects three more Hydra parts gets a
fresh Hydra with base stats.

## The cutscene archive

`ApStarAssembly.dat` has one public, `apStarAssembly`, shaped like `VsHydra.dat`'s
`vsDataHydra` - three pointers:

| Slot | Points at |
|---|---|
| glow | a `{JOBJDesc*, FigaTree*, MatAnimJoint*}` triple: Hydra's streaks and flashes rebuilt for six pods |
| parts | a triple of the same shape: the star's own model and a 150-frame FigaTree that flies the pods in |
| camera | a word holding the camera-animation descriptor |

The archive is generated from two sources - the parts from the star's machine archive, the glow
and the camera from `VsHydra.dat` - and an edit to the star's model needs it regenerated.

**The parts model** is the star's main model, all 17 joints, with each joint's DObj chain trimmed
to its high LOD plus the pods' glow quads: the cutscene's render callback draws every DObj, so the
lower LODs would otherwise z-fight the one meant to be seen. Its material animation is NULL - the
pods' static colors are already right. The choreography is authored in the ring pivot's frame:
each pod starts 22 units out at height 12, 85 degrees around from its own azimuth, and spirals in
the way Hydra's parts do. Pod `i` lands on frame `70 + 4i`, filling the same 70-90 window Hydra's
three land in, with a damped radial overshoot and a scale pop; the ring pivot then turns one full
revolution over frames 95-150, the beat Hydra's wings unfold in.

**The glow model** is Hydra's, cut to one of its four-joint streak groups and copied back out
to six, each copy with its own DObj, POBJ and envelope array so its bones resolve to its own
joints. Each streak is aimed along its pod's approach and its head bone follows the pod in, so
the ribbon traces the pod, and its material animation holds it up until that pod lands. The flash
joints keep Hydra's tracks, with the flare pulsing once per pod.

**The camera** is Hydra's descriptor block byte for byte, with one retune. The eye path is a
spiral held by a joint that carries the spline, and because the spline lies in that joint's own
XY plane, its `scale.x` and `scale.y` are the orbit's two horizontal axes. Both come in to 0.85,
since the star measures about 3.0 x 3.5 across where Hydra measures 3.5 x 4.3. The eye height is
unchanged, which reads the flat ring of pods at a steeper angle than Hydra's bulk needs.
