# AP Patches and AP Boxes

A location category made of two City Trial items. The **AP Box** is an Archipelago-branded item
box that falls as one more outcome of the city's own box roll; breaking it scatters **AP
Patches**, and every AP Patch collected is a multiworld location. No goal reads them: the slot
option `ap_patches` simply decides how many extra locations the seed carries, from a handful to
enough to hold the whole item pool. The category grants no stat and touches no vanilla counter.

## The two items

Both are custom_items drop-ins from the disc's `items/` folder, bound to this mod by display
name (`AP_PATCH_ITEM_NAME` = "AP Patch", `AP_BOX_ITEM_NAME` = "AP Box"); renaming one side
without the other silently unbinds it.

- **AP Patch** is carved from `ITKIND_OFFENSE`, so it has a stat patch's billboard, state
  script, pickup reaction and sound. Its effect record has a count of 0, so
  `Machine_OnTouchItem` applies nothing, and it renders at 0.9 scale with the Archipelago logo
  as its texture. All its box and event weights are 0: it only ever comes out of an AP Box.
- **AP Box** is carved from `ITKIND_BOXBLUE`, keeping the blue box's material, flags and shading
  and its material animation (named in the descriptor's `mat_anim`, since the base kind's own
  would swap the blue texture back in). Each face shows one of the logo's six sphere colors with
  a watermark of the mark, bordered like the vanilla box with a band in the opposite face's hue
  and black corner wedges; the crack stages darken the face by the vanilla box's own ratio. Its
  weights are 0 too - it arrives through the box roll, not a pool.

The art is drawn to the vanilla patch textures' rules: CMPR with binary alpha, one solid
silhouette, a white outer rim over a black outline (the material blends, so a dark outer edge
would filter into a gray ring), and box borders sized to the 4x4 CMPR block grid so no block
holds three colors.

`APPatches_OnSaveLoaded` imports custom_items and registers the pickup handler. At each
`On3DLoadStart` the mod finds both items in the registry by name (retrying each round until both
are found) and enables them only when `ap_patches > 0` and the round is a Trial on the city map
and not the title's attract demo - whose CPUs would otherwise claim locations. custom_items
registers enabled items at the end of `CityItemSpawn_Init` and skips disabled ones, so a held-out
item never gets an `ItemKind`. The kinds it hands out are read at `On3DLoadEnd` and are valid
for that scene only.

## The seams

| Site | Patch | Does |
|---|---|---|
| 0x800eb20c, `bl GrBoxGeneratorDetermine` in `CityItemSpawn_Think` (0x800eb108) | Replace call | On a winning roll, returns the AP Box's kind instead of the color the picker chose |
| 0x80258384, `bl Box_OutcomeLogic` in `ItemGObj_BoxBreak` (0x802582dc) | Replace call | An AP Box spawns its patches; anything else goes to vanilla |
| 0x80258344 and 0x802575f0, `bl ItemGObj_BoxSpawnImpactEffect` in `ItemGObj_BoxBreak` and `Box_OnTakeDamage` (0x80257158) | Replace call | An AP Box's hit and break bursts use recolored particles |
| 0x801db91c in `Machine_OnTouchItem` (0x801db34c) | Conditional hook | Skips `Ply_IncrementItemCollectNum` for both AP kinds |

By the time any of these runs, custom_items has rewritten `ItemData.kind` to the base kind, so
the kind cannot tell an AP item from a vanilla one. `CustomItemsAPI.GetItemKind` recovers the
custom kind from the instance's data pointer, and comparing it with the round's assigned kind is
the test.

The picker seam is a call-site patch because box gating already replaces
`GrBoxGeneratorDetermine`, and because that call is only reached on a tick that came up an item
box - patches and the legendary carrier never pass through it.

## Spawning AP Boxes

`GrBoxGeneratorDetermine` returns the box's `ItemKind`, which for the three vanilla colors is the
color, and `CityItemSpawn_Think` hands it straight to `PowerUp_SpawnFromSky`. So substituting the
AP Box's kind on a winning roll, with the color and size the roll wrote left in place, is the
whole spawn. The AP Box inherits the fall timer, the drop position, the field's item cap and the
spawn-rate scaling, and takes the place of the vanilla box that tick would have dropped.

A plain percentage of box ticks is not enough. The field's item cap decides which ticks are
live, but the AP Box is substituted after it and is exempt from the color gates - so the emptier
the field, the more ticks survive and the more AP Boxes fall, and an early seed with box and
patch gating on is exactly the case where little else spawns. Two limits apply instead:

- **A share** of box ticks, and
- **a floor** on the time between two AP Boxes, divided by the spawn-rate scale so Spawn Rate Up
  still moves it.

Both come from the **AP Box Rate** setting (Archipelago Settings, default Med):

| Setting | Share | Floor |
|---|---|---|
| Rare | 3% | 80 s |
| Low | 6% | 40 s |
| Med | 12% | 20 s |
| High | 20% | 12 s |

Each floor is paired with its share so the two bind at about the same count on an open field;
gated or not, a round then pays out at the same rate. The floor needs no timer of its own: the
seam stores a deadline in terms of `grBoxGeneInfo.match_frames_left`, the round clock the
spawner already rebuilds every frame, so pauses and round length are handled for free. The
deadline starts open at `On3DLoadStart`.

The roll is skipped while the round is not armed or no AP Patch is left to collect. When box
gating has left no vanilla color eligible the picker returns -1 and writes nothing; an AP Box
still lands then, as Blue with a size rolled over the whole table (`GateBoxes_RollSize(-1)`), so
a seed with box gating on can still reach the category.

## Breaking an AP Box

`Box_OutcomeLogic` (0x80250ae8) caps a custom kind at one item, so the mod reproduces the break:
the box's world position, the vanilla scatter parameters and per-slot yaw offsets, a
`CityItem_CanSpawnNMore` check before each patch, and `Box_SpawnContents` (0x80253378) for each,
linked into the box's child list the way `ItemGObj_BoxBreak` expects. The count follows the
box's size - 1, 2 or 4 - capped at `AP_BOX_MAX_PATCHES` (2), so one lucky large box cannot hand
over a run of checks, and capped again at the patches still uncollected, checked at the break
rather than the spawn.

The burst is recolored without touching the vanilla boxes. `ItemGObj_BoxSpawnImpactEffect`
(0x80251f64) picks the blue box's two particle generators (effects 50000 and 50001, bank 5) for
an AP Box. Once a round the mod copies both descriptors six times, forcing each copy's two color
operands to one face color's hue while keeping their brightness, and the seam points the bank's
descriptor slot at a copy just for the spawn. The generator keeps a pointer into the descriptor
it was spawned from, so the burst keeps its color while every other box allocates from the
vanilla one. The color steps per spawn, so a box hit twice and broken throws three colors. The
copies are rebuilt each round because `psInitDataBanks` reloads the banks, and the color opcodes
are checked first - a different program leaves the burst vanilla.

## Collecting

The pickup handler claims the **lowest clear bit** below `ap_patches` in
`APSave.ap_patch_collected` and mirrors it into `APData.ap_patch_checks`. Any collector counts,
CPUs included, so a solo player's rivals help clear the category. Claiming in order matters: the
apworld splits the block into consecutive groups of 20 and gates each behind the last, which
keeps progression out of a location two hundred pickups deep. With nothing left the handler does
nothing, which is normal - two boxes can each clamp against the same last patch. It does not
write the memory card; the bits reach it with the game's next save.

## Isolation

- **No stat.** The empty effect record means no `Machine_GivePatch`, so no patch cap, no
  permanent patch, and nothing for a hit to knock loose (`Rider_SpawnDropPatchSeq` only drops
  stats actually held).
- **No counters.** The collect seam skips `Ply_IncrementItemCollectNum`, the game's one producer
  of `PlayerStats.item_collect[]`, for both AP kinds. An AP Patch does not count as an Offense
  patch and an AP Box break as no box, so neither feeds any checklist cell - one location
  category cannot farm another.
- **No gating.** The AP Box overrides the picker's result, so no color gate sees it; both items
  have zero weights, so no spawn-pool filter does either.
- **No message.** The pickup plays the generic item sound and no "stat up" popup (that popup
  follows a stat change). A collected patch is an ordinary location check, so the client's check
  line reports it; the category has no in-game counter.

## Save and wire

`APSave.ap_patch_collected` is a u64[8] mask; there is no separate delivered state, so a patch
never picked up simply drops again in a later round. `APData.ap_patch_checks` mirrors it
(republished at save load, or the client would resend everything) and `ap_patch_backfill` brings
the server's checks back under `backfill_valid`. Bit `i` of word `w` is AP Patch `w * 64 + i`,
location `481 + w * 64 + i`. The mask is 512 bits whatever `ap_patches` says, so the count never
moves an `APData` offset; the apworld's option tops out at 200.

## Debug

archipelago_debug drops an AP Box in front of its target player on **D-Pad Down** during a
round (it fails while the items are unregistered, until a round loads with `ap_patches` above 0),
and its Checks page can override `ap_patches`, claim the next patch, or clear every collected
bit - clearing the save, the wire mirror and any pending backfill together, since any one left
would restore the others.
