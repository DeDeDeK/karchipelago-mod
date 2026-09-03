# AP Patches and AP Boxes

An Archipelago location category made of two City Trial items. The **AP Box** is an
AP-branded item box that falls alongside the ordinary ones as a fourth outcome of the city's own
box roll; breaking it scatters **AP Patches**, and every patch collected is a multiworld location
check. No goal reads them, so the count is purely how much extra location capacity the seed
carries - turned up, the same mechanism supplies the whole pool.

The category lives entirely in `mods/archipelago/src/ap_patches.c`. It touches no vanilla
counter and grants no stat.

## The item pair

Both are `custom_items` drop-ins, discovered in the disc's `items/` folder and bound to the
mod by display name:

| Name | Archive | Base kind | Authored by |
|---|---|---|---|
| `AP Patch` | `mods/archipelago/assets/items/ApPatch.dat` | `ITKIND_OFFENSE` (7) | `scripts/hsd/carve_custom_item.py` |
| `AP Box` | `mods/archipelago/assets/items/ApBox.dat` | `ITKIND_BOXBLUE` (0) | `scripts/authoring/make_ap_box.py` |

`make_ap_box.py` takes no arguments and writes its own output path. The patch carve is a
command line, and this is the one that produced the shipped archive:

```
uv run --with pillow python scripts/hsd/carve_custom_item.py iso/files/Item.dat 7 \
    mods/archipelago/assets/items/ApPatch.dat "AP Patch" \
    --scale 0.9 --no-effect --weight-blue 0 --texture art/ap-patch.png
```

The names are the handle: `AP_PATCH_ITEM_NAME` / `AP_BOX_ITEM_NAME` in `ap_patches.h` and the
`CustomItemDesc.name` the authoring scripts write. Changing one without the other silently
unbinds the item.

The registry import is deferred past `OnBoot` - mod load order follows FST order, so an
export is not available until its owner has booted - and the two hashes are resolved by
scanning the registry for those names. `On3DLoadStart` calls `SetEnabled` on both with
`ap_patches > 0 && Gm_IsInCity() && CITYMODE_TRIAL`, which is early enough: the registry
is written at `CityItemSpawn_Init`'s epilogue and skips a disabled item, so a held-out kind
never receives an `ItemKind` and no path can spawn it. The two assigned kinds are fetched at
`On3DLoadEnd` and are valid for that scene only, so they are re-fetched every round.

### AP Patch

Carved from `ITKIND_OFFENSE`'s own model, so the flat billboard, the state script, the pickup
reaction and the SFX all come along. Three overrides:

- the 138x114 texture is replaced with `art/ap-patch.png`, the Archipelago logo drawn the way a
  stat glyph is: a 3px black outline, a 5px white rim outside that, and a 4px transparent gutter
  past the rim so nothing reaches the billboard's edge;
- `effect_info` points at a `PatchEffectInfo` with `count = 0`. `Machine_OnTouchItem` applies
  grants by walking that record, so an empty one means the loop never runs;
- `scale` is 0.9, multiplied onto the cloned attribute record's `scale_factor`, so the billboard
  renders a tenth smaller than a stat patch.

All box and event weights are zero, so the patch never enters a spawn pool. The only route to
the field is an AP box.

#### Redrawing the texture

Both rim widths and their ordering come from the vanilla patch textures: all sixteen are CMPR
with binary alpha, one solid silhouette with no interior hole, and a white band as their
outermost opaque texels over a black outline of about the same width - ~5 texels each on kind
7's own 138x114. The material renders `RENDER_XLU`, so the texture's alpha is blended rather
than tested and every partial texel shows the track behind it. Five things follow, and a redraw
has to hold all of them:

- white is the outermost band. The hardware filters bilinearly into the transparent field, so a
  black outermost band blends toward translucent black and rings the billboard in gray;
- the white rim closes over the small gap at the logo's center, so the sprite is one solid
  silhouette with no interior hole;
- the logo's own edges carry no black fringe, which a resize on unpremultiplied alpha would
  bleed in from the transparent field;
- nothing is cut through a mask, since that multiplies the destination's alpha and leaves a
  see-through seam wherever a sphere's antialiased edge meets a rim;
- the rims stay round where the spheres cut into each other. Those arcs sag only ~3.6px at logo
  scale and flatten into visible chords under a square-edged widening.

The silhouette's alpha ramp is steep, leaving ~75 partial texels, all of them white.

### AP Box

Carved from `ITKIND_BOXBLUE` rather than generated, so the vanilla material, PE flags and vertex
shading are the vanilla box's. The model is a unit cube: 8 positions, 4
vertex colors and a 24-vertex `GX_QUADS` display list, with POS / CLR0 / TEX0 all `INDEX8`.
Vanilla's TEX0 index buffer holds four entries and every face maps the full 0-1 UV range, so
all six faces render the same image. Three rewrites turn that into six independent faces:

1. the TEX0 buffer grows to 24 entries, one UV pair per vertex, and the 24 TEX0 index bytes in
   the display list are re-indexed onto it;
2. each of the four `ImageDesc`s the box's damage animation swaps between is repointed at a
   generated 256x128 CMPR atlas of 64x64 cells - six of them a face of the box, and two spare
   cells copying the last face so bilinear filtering at a used cell's edge only ever blends in
   the band color that edge already carries. The four atlases are 64 KB of the 65 KB archive;
3. every cell is edged the way the vanilla box is, with a flat band around the face and a flat
   wedge cutting each corner off.

The atlas is four columns wide rather than three because TEX0 is stored as `u8` with a shift of
7: a cell edge has to land on a multiple of 1/128, which quarters do and thirds do not.

A face is one of the six Archipelago logo sphere colors, in the logo's own ring order (rose,
green, violet, tan, blue, yellow), carrying the Archipelago mark as a lightness lift of 26%
toward white at 58% of the cell's width. Only the logo's alpha is read, so the mark takes the
face's own hue and none of the logo's six, which is what keeps it a watermark rather than a
decal.

The blue box's material animation is carved alongside the model, unchanged. Carrying it is what
`CustomItemDesc.mat_anim` (v6) is for - the base kind's own material animation would swap the
vanilla blue texture back in the moment the box spawned, so the descriptor names the carve's
copy instead. The three crack stages are the base face multiplied by the vanilla box's own
luminance loss at the same stage, clamped so a crack can only darken. Nothing distinguishes
crack art from box art in the source texture, and the ratio does not have to: where the vanilla
box is unchanged the face survives untouched, and where it cracks the crack lands on the face at
the vanilla contrast. The model's own `ImageDesc` shares stage 0's buffer, which is what the
animation binds at frame 0 anyway.

#### The edges

The vanilla box's border is a flat 5-texel navy band on its 128x128 face with a flat yellow
wedge cutting each corner, and none of it cracks - the three damage stages break up the
interior and leave the border untouched. The AP Box copies that shape: a flat 4-texel band
around each 64x64 cell and a flat black wedge with 6-texel legs at each corner, both stamped
over the cell after the crack multiply so the border survives every stage.

A face's band is the color of the face opposite it on the hue wheel. The six face colors
sorted by hue are tan, yellow, green, blue, violet and rose, and pairing each with the one
three steps along gives three reciprocal pairs - tan/blue, yellow/violet, green/rose - so
every color appears exactly once as a face and exactly once as an edge, and every band sits
at the maximum hue distance from the face it rings. The color is then pushed 1.35x in
saturation and scaled to 0.95 in value: enough that the band reads as an edge rather than as
a second pastel abutting the first, and not so much that it stops reading as one of the box's
own six. Sorting by hue is what makes the pairing meaningful - opposite in the logo's ring
order would pair colors that happen to be hue neighbors.

The corners are black on all six faces, and being shared is the point. Six bands in six
different colors would otherwise read as six unrelated borders; the one element every face
holds in common is what ties them into a single treatment, which is the job vanilla's yellow
wedges do.

Both dimensions are picked against the 4x4 CMPR block grid. The band is exactly one block
deep, and the wedge's legs stop short of twice that, so the only block that can hold face
color is one whose nearest texel is already 4 in from both edges - past the wedge. No block
carries all three colors, which leaves the two endpoints a CMPR block stores enough for a
clean edge everywhere.

All box and event weights are zero here too - those are the pools a box opens into, and an AP box
is a box, not box contents. It never becomes one of the nine chance-table outcomes either; it
arrives through the picker seam below, which overrides the outcome the table already rolled.

## The four seams

| Site | Patch | Behavior |
|---|---|---|
| `0x800eb20c`, the `bl` into `GrBoxGeneratorDetermine` inside `CityItemSpawn_Think` | `REPLACECALL` | On a winning roll, return the AP box's `ItemKind` in place of the color the picker chose |
| `0x80258384`, the `bl` into `Box_OutcomeLogic` inside `Box_Break` | `REPLACECALL` | AP box -> spawn its contents directly; anything else -> forward to vanilla |
| `0x80258344` and `0x802575f0`, the `bl`s into `Box_SpawnImpactEffect` inside `Box_Break` and `Box_OnTakeDamage` | `REPLACECALL` | AP box -> swap in a recolored particle generator for the length of the spawn |
| `0x801db91c`, just ahead of the `bl` into `Ply_IncrementItemCollectNum` inside `Machine_OnTouchItem` | conditional hook | Skip the call for the two AP kinds; fall through for everything else |

Three of the four have to tell an AP item from a vanilla one, and by the time any runs the
`custom_items` behavior clamp has already rewritten `ItemData + 0x1c` to the base kind - so the
kind argument cannot answer it. The instance's `itData` pointer, which the clamp leaves alone,
can: `custom_items` repoints the engine at a grown `itData` array, so
`id->itData == &(*stc_it_common_data)->itData[my_kind]` is the whole test, and the mod already
knows its own two kinds for the round.

The picker seam is a call-site patch rather than a second `REPLACEFUNC` on
`GrBoxGeneratorDetermine` because `gate_boxes.c` already owns that function, and because
`0x800eb20c` is the one call site the spawner reaches on a tick that came up an item box -
patches (category 0) and the legendary carrier (category 2) never pass through it.

## Recoloring the burst

`Box_SpawnImpactEffect` (`0x80251f64`) throws the chunks a box scatters when it is hit and when
it breaks. It picks one of the six yakumono-bank particle effects `50000..50005` off
`ItemData.kind` and `is_break`, then `Effect_SpawnSync`s it onto joint 1 in anchor mode 205. The
behavior clamp has already made an AP Box a `BOXBLUE`, so it draws `50000` and `50001` - the
vanilla blue chunks.

The color is data. A yakumono generator descriptor is 136 bytes: numeric fields up to `+0x3c`,
then a particle program, and the six box descriptors differ only in a lifetime byte and their
color operands. Two of those are `PTCL_OP_COLOR` at `+0x3c` and `PTCL_OP_COLOR2` at `+0x48`,
each an opcode, a ramp duration and an RGBA quad, holding the box's bright and dark shades.

Rewriting them in place would recolor every blue box on the field, so the mod copies instead.
Once a round it takes both descriptors out of `psGeneratorDesc` and writes six recolored copies
of each, one per AP face color, forcing the tint's hue onto each operand while keeping the
operand's own value - so the bright primary stays bright and the dark secondary stays dark. The
seam then points `psGeneratorDesc[5][id]` at the copy for the length of the spawn and restores it
after. `Ptcl_Alloc` stores `descriptor + 0x3c` in the generator instance, so the burst reads the
mod's copy for its whole life, while every other box on the field still allocates off the vanilla
descriptor. The color advances one step per spawn, so a box that is hit twice and broken throws
three different faces' worth.

`psGeneratorDesc[bank]` is biased by the bank's base id and `psGeneratorCount[bank]` holds
`base + n`, so both are indexed by the whole effect id rather than a slot. `psInitDataBanks`
rebuilds them on every scene load, which is why the copies are rebuilt per round and the two
opcodes are checked rather than assumed - a descriptor that does not start with `0xcf` / `0xdf`
where it should logs once and leaves the burst vanilla.

The collect seam sits at `0x801db91c` rather than on the `bl` itself because the accept path
has to re-materialize the call's arguments: that instruction is `lwz r4, 28(r21)`, and falling
through to `0x801db920` re-runs the `mr r3, r26` and `lwz r5, 32(r21)` that follow it. The
reject path jumps to `0x801db92c`, past the call.

## The spawn

`GrBoxGeneratorDetermine` (`0x800ebc04`) returns the box's `ItemKind`, which for the three
vanilla colors is the color itself - `CityItemSpawn_Think` saves that return in `r30` and hands it
straight to `PowerUp_SpawnFromSky`. So substituting the AP box takes one value: the seam calls the
picker, and on a winning roll returns `box_kind` instead of what came back, leaving the `box_color`
and `box_size` the roll wrote in place.

That makes the AP box a fourth outcome of the vanilla box roll rather than a spawn of its own. It
inherits the fall timer, the position and slot picking and the 8-entry recent-slot ring buffer, and
the spawn-rate hooks scale it with everything else - the frequency knob is the Spawn Rate Up item,
not a setting. The cost is that an AP box displaces the blue, green or red one that tick would
otherwise have placed. What it does **not** inherit is the field's simultaneous-item cap, which is
checked upstream of the seam; the section below is what stands in for it.

### Why a percentage is not enough

`AP_BOX_PERCENT` is 6, and there is a second limit on top of it, because the quantity a
percentage is taken against is not throttled.

`CityItemSpawn_UpdateAndCheckToSpawn` (`0x800ea6e0`) runs three steps in this order, and the
order is the whole problem:

1. **Decrement the timer, and on expiry reset it** (`0x800ea990`). City Trial's six
   `ItemFallDesc` rows run `item_max` 45-60 with timers of 20-30, 30-40 and 40-50 frames across
   the round, so a tick lands every ~0.42-0.75 s - about **623 ticks in a five-minute round**.
2. **Check the field's simultaneous-item cap** (`0x800eaa8c`, `cur_num_items` vs `item_max`).
   On a full field this jumps to the skip-spawn branch, which does not even advance the script
   index - the tick is dead.
3. **Read the next byte of the spawn script** and map it through the category table at
   `0x805d617c`. City Trial's normal script is `[0, 0, 0, 0, 1]` (`box_spawn_chances + 9`), so
   **one tick in five is a box tick**, giving ~125 box ticks a round if none are dead.

The timer resets in step 1 regardless of what step 2 decides, so the cap never slows the
*cadence* - it only decides which ticks are live. That is fine for a vanilla color, which is
picked in step 3 and therefore only ever competes for ticks the cap already admitted. An AP box
is substituted on the picker's **return**, downstream of all three, and it is exempt from the
color gates besides. So the emptier the field, the more ticks survive step 2 and the more AP
boxes land - and an early seed with box and patch gating on is exactly the case where nothing
else is spawning to fill the field at all. Gating makes the category *more* generous, which is
backwards.

At the old 16% an unthrottled round pays out ~20 AP boxes and ~38 patches. That is the go-mode
reading, and it is worst at the start of a seed.

### The two limits

**`AP_BOX_PERCENT` is 6**, well under the 14-in-71 share red holds in the city's own chance table
(`[20, 15, 10, 5, 4, 3, 7, 7, 0]`). Matching a real color's share only makes sense for something
the cap throttles like a real color.

**`AP_BOX_MIN_INTERVAL` is 2400 frames (40 s)**, a floor on the gap between two winning rolls. It
is divided by `SpawnRate_GetScale()` so the Spawn Rate Up item still moves the cadence and a
sub-vanilla `spawn_rate_min` still slows it - the floor bounds the category against *gating*, not
against the knob that is supposed to control it.

Nothing counts the interval down. The spawner already keeps a frame clock for the round in
`grBoxGeneInfo.match_frames_left` (`+0x29c`), rebuilt at the top of
`CityItemSpawn_UpdateAndCheckToSpawn` every frame from the round timer as
`(min * 60 + sec) * 60 + subseconds * 0.6`, so the seam records the deadline as a value of that
clock and compares against it on the next box tick. `box_gate_frames` starts at `INT_MAX` at
`On3DLoadStart` - the clock is always below it, so the round's first roll is never held back - and
a winning roll sets it to `match_frames_left - interval`.

Reading the round's own clock rather than ticking one is what keeps the whole feature inside the
seam. There is no per-frame work for a value only a box tick reads, no state to reset besides the
one `On3DLoadStart` already writes, and a paused round, a round that has ended and a round of any
length all behave correctly for free, because they are already correct in the clock.

The floor is what makes the rate stop depending on how much of the game is locked. A fully
unthrottled round offers ~125 box ticks, which at 6% wants ~7.5 AP boxes; the floor allows at most
7 in five minutes. The two land in the same place by construction, so a gated round and an ungated
one pay out at the same rate - about **7 AP boxes and 11 patches** in a five-minute round, against
~20 boxes and ~38 patches before.

The roll is skipped entirely while the round is not armed or
`ap_patches - popcount(ap_patch_collected)` has reached zero, which leaves the category dormant for
the round without disabling the items, so a `!collect` or a backfill mid-session re-arms on the
next load.

`box_color` is only ever read as an index into the 3-wide `grBoxGeneObj.item_group_spawn[]`, and
the `Box_Break` seam takes an AP box before any pool lookup happens, so the color it carries does
not matter beyond staying in range. The one case the picker cannot answer is its own `-1`: box
gating has left no vanilla color eligible, and it wrote neither out-param. No gate ever sees the AP
box, so it still lands there - `RollBoxSize` re-rolls a size off the stage's chance table with the
three colors collapsed, and the color is `BOXKIND_BLUE`. Without that, a seed with box gating on
would make the whole category unobtainable until the first box unlock arrived.

## Breaking an AP box

`Box_OutcomeLogic` caps a custom kind at one item either way - `forced_item` by design, and the
pool roll because its 1 / 2 / 4 count only applies when the rolled kind is a vanilla patch
(3..0x12) - so the whole outcome is reproduced instead of steered:

- the box's joint 1 world position, via `GObj_GetJObjIndex` + `JObj_GetWorldPosition`;
- `CityItem_CanSpawnNMore` before each item, so an AP box respects the field's simultaneous-item
  cap like any other;
- the scatter, from `stc_item_param`'s `box_spawn_offset_min_h` / `max_h` /
  `box_spawn_offset_min_v` / `max_v` / `box_spawn_yaw_range`, with the per-slot yaw offsets
  `{0, 180, 90, -90}` degrees vanilla uses;
- `Box_SpawnContents` per patch, writing each child into `ItemData.child_gobjs[]` and setting the
  child's `parent_gobj`, which is what the rest of `Box_Break` walks.

The one branch the reproduction leaves out is vanilla's forced-item path. Only the red legendary
carrier ever has `forced_item` written, on the `CityItemSpawn_SpawnLegendaryPiece` seam, and an AP
box is spawned through `PowerUp_SpawnFromSky` instead, so it can never be carrying one.

The count comes off `ItemData + 0x40`, the box size the stage's `box_spawn_chances` table rolled
at spawn time. City Trial's table is `[20, 15, 10, 5, 4, 3, 7, 7, 0]` across 3 colors x 3 sizes,
which is 45.1% small / 36.6% medium / 18.3% large, so vanilla's 1 / 2 / 4 averages 1.92 items per
box.

Two clamps sit on top of that. `AP_BOX_MAX_PATCHES` is 2, which flattens the large box's 4 down to
the medium box's 2 and brings the average to **1.55 patches per box**: a single break can then only
ever be worth a pair of checks, so the category advances at a rate the box roll sets rather than
one a lucky size roll can spike. The second is a clamp against live `remaining` at **break** time,
not at arm time, so the last box of a run cannot spawn patches nothing can claim.

Only the first two entries of the `{0, 180, 90, -90}` yaw table can be reached under the cap, which
is the same pair a vanilla medium box uses.

## Collection

A `custom_items` pickup handler matched on the item name. Any collector counts, human or CPU,
which is the point - a solo player's three CPU rivals help clear the category.

The handler claims the **lowest clear bit** in `APSave.ap_patch_collected`, so the block is always
collected in index order, and bits at or above `ap_patches` are never set. That ordering is
load-bearing: the AP world splits the block into consecutive groups of 20 and gates each behind the
one before it, which is what keeps a progression item out of a location two hundred pickups deep.
Claiming an arbitrary free bit instead would let a player check a late group before an early one,
and the logic would stop describing the game.

On claim the handler sets the save bit and mirrors into `APData.ap_patch_checks`. It deliberately
does not write the memory card - `Hoshi_WriteSave` stalls the frame and this fires mid-round - so
the bits live in the save block until the game's own save point flushes them.

When no clear bit is left the handler does nothing, silently. Two AP boxes on the field with one
patch remaining each clamp their count to 1, so a patch with nothing to claim is a normal
outcome, not an error, and must not log per pickup.

The mask and its mirror move together in four places: the claim, the `OnSaveLoaded` mirror
(without it the client reads zeros after a reboot and re-sends the whole category), the
`OnFrameStart` backfill, and the debug clear / force-mark pair.

## Isolation

Three properties the category is required to hold, and what enforces each.

**The AP Patch is a patch in looks and feel only.** `base_kind = ITKIND_OFFENSE` supplies the
flat billboard model class, the state script, the pickup reaction and the SFX. It supplies no
stat, because the descriptor overrides `effect_info` with a `count = 0` record and
`Machine_OnTouchItem` applies grants by walking it. Downstream of that: no `Machine_GivePatch`,
no `Machine_SetStatCap`, so no patch-cap interaction and no permanent-patch entry; and
`Rider_TickDropAllUp` builds its candidate list from stats actually held, so an AP Patch can
never be knocked loose. Offense is the base kind precisely because it is the only one of the
eight stat patches whose `item_collect[]` slot no checklist cell counts, so nothing depends on it
staying clean - but the seam below keeps it clean anyway.

**Neither AP kind touches any counter.** The `0x801db91c` seam skips
`Ply_IncrementItemCollectNum`, the single producer of `PlayerStats.item_collect[]`. So an AP
Patch does not count as an Offense patch, an AP Box break does not count as a blue box or toward
the vanilla "break N boxes" cells, and neither reaches `Ply_GetItemCollectTotal`, the
first-20-seconds aggregate at `+0x804` or the Tac aggregate at `+0x808`. Suppressing the totals
is deliberate: letting AP Patches also advance "pick up a total of over 3000 items" would let one
location category farm another's.

**The AP Box is exempt from box-color gating.** The seam runs on the picker's return, after
`gate_boxes.c` has already zeroed every ineligible color, and it overrides that return rather than
adding a row to the chance table - so no gate ever sees the AP box, and it lands even on the tick
where the picker found nothing eligible at all. What it does share with vanilla boxes is
everything about cadence: the same fall timer, the same field item cap, and
`CityItem_CanSpawnNMore` again per patch on the break.

**Patch gating never sees either kind.** The spawn-pool filters run over pool entries, and both
descriptors carry all-zero box and event weights, which the registry's `PoolAppend` skips.

## Pickup feedback

Nothing from the mod. The AP Patch plays `SFX_city_item_get`, the generic pickup sound, and that
is the whole of it.

It arrives on its own. `Machine_OnTouchItem` dispatches audio after the effect loop, branching on
the clamped instance kind: `0x15` gets `SFX_city_item_parts_get`, `0x14` gets `SFX_city_copy_get`,
and anything else gets fgm `0x11` `SFX_city_item_get` on the machine's own emitter at volume 1.0.
`ITKIND_OFFENSE` takes the third row. The effect loop raises the id to `0x12`
`SFX_city_item_get_bad` only for a bad patch, which an empty record never reaches.

What a vanilla patch has on top is the per-stat "STAT UP" popup and its sound, and the AP Patch
correctly does not get them. `3DHud_CreateStatGet` (`0x80127234`) draws that popup and plays the
sound as part of drawing itself, and it is not driven by the pickup: the per-view stat watcher at
`0x80117154` fires it when a stat value moves, so an item that grants nothing produces neither.
The good-patch sparkle and the good-patch color animation stay off for the same reason - both
are set inside the effect loop.

`CityEvent_PlayItemPickupVoice` (`0x8027a318`) runs unconditionally and its City Trial gate
passes, but for a kind-7 clamp it only speaks during city events `0xd` and `0xf`. That matches a
vanilla Offense patch.

There is no textbox line either. An AP Patch pickup is an ordinary location check and gets the
client's ordinary check line; a mod-side line on top would be a second message for the same
event, and a large box would put several into a queue that shows 8 at a time. The consequence
accepted here is that the category has no progress readout in game - it is a countdown with no
board and no running count.

## Save state and the wire

`APSave` carries `u64 ap_patch_collected[8]`, bit `N` = AP Patch `N` collected. There is no
separate "delivered" or "in flight" state: uncollected means the bit is clear, which is what
makes re-delivery automatic - every round re-arms from the mask, so a patch that expired unpicked
or was never reached comes back next round.

`APData` carries the mirror pair `ap_patch_checks[8]` (game -> client, read-and-diff) and
`ap_patch_backfill[8]` (client -> game, ORed in and cleared each frame alongside the checklist
backfill). Bit `i` of word `w` is AP Patch `w * 64 + i`, location code `413 + w * 64 + i`.

One slot option drives the whole feature: `ap_patches` (0-512; 0 leaves both items
unregistered). The masks are a fixed 512 bits whatever the count is, which is what lets the
count move without touching an `APData` offset - the AP world sizes its own location block
independently, and its own option tops out at 200.

## Debug

`archipelago_debug` drops one AP Box in front of player 1 on each **D-Pad Down**, so a break and
its patches can be watched without waiting on the spawner. It goes through
`ArchipelagoAPI.DebugSpawnApBox`, which reads the box's `ItemKind` out of the `custom_items`
registry - an item held out when the scene loaded was never registered and cannot be spawned
until the round reloads. The AP Star sphere cycle is on **R + D-Pad Down**.

The Checks menu carries the two that are not spawns: **AP Patches**, which overrides
`ap_patches` (Off / 8 / 64 / 512) so a build with no AP Patch seed can still register the
drop-ins at the next round load, and **Collect AP Patch**, which claims the lowest unclaimed
patch outright. Lowering the count drops the collected bits above the new ceiling, so the
collected total never reads past the window the count describes.

## Logging

The `[APPatches]` component prints one "Hooks installed" line at boot, one line per claim, one
per round at arm time with the remaining count, the roll's share and the scaled interval floor,
and one at round end with the AP boxes the roll produced - enough to see whether the share is
landing where it should, and whether the floor or the percentage was the binding limit. Nothing
per spawn.
