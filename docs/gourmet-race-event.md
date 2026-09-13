# Gourmet Race Event

A custom City Trial event (`CUSTOM_EVKIND_GOURMET_RACE`, kind 19). For 60 seconds, food is scattered across the map and players compete to collect the most. Each collected food respawns at the same spot after a cooldown, and the winner is handed All Up patches when the event ends. Implemented in `mods/custom_events/src/event_gourmet_race.c`.

Like every custom event, it is a row in `events[]` in `custom_events.c`. The mod's wrappers on the event state table call the kind's callbacks in place of the vanilla per-kind dispatch.

Its parameters:
- 3600-frame duration (~60 s)
- siren intro
- no sky change (`sky_preset` -1)
- BGM file 0x34 (`event_supercharge`)
- roll weight 20

It uses two callbacks:
- `GourmetRace_Start` resets scores, runs the spawner, and creates the watcher GObj and the score HUD.
- `GourmetRace_End2` tears everything down, tallies scores and grants the reward.

There is no `active`, `end` or `abort`. The watcher proc does all per-frame work, and `Start` resets everything that a cut-off event leaves behind.

## Food Spawning

`GourmetRace_SpawnFood` aims for `GOURMET_MAX_FOOD` = 60 foods, placed in five passes. `CityItem_Create` (0x8024eef4) returns NULL once more than 100 items are live, so any pass can come up short; the spawner simply records fewer foods.

| Pass | What | Count | Y offset | `coll_kind` | Scale | Spacing filter |
|------|------|-------|----------|-------------|-------|----------------|
| 1 | Big foods at fixed landmarks | 5 | +1 (`GOURMET_PREPLACED_HEIGHT`) | 2 | 4x (`GOURMET_BIG_ITEM_SCALE`) | no |
| 2 | Regular foods at pre-placed spots | 5-10 of 15 | +1 | 2 | 2x (`GOURMET_ITEM_SCALE`) | no |
| 3 | Surface random | half of remaining budget | +180 (`GOURMET_SURFACE_HEIGHT`) | 3 | 2x | yes |
| 4 | Underground random (candidate Y < `GOURMET_UNDERGROUND_Y`, 44.0) | other half | +5 (`GOURMET_ABOVE_SPLINE_HEIGHT`) | 2 | 2x | yes |
| 5 | Overflow surface (if pass 4 fell short) | remainder | +180 | 3 | 2x | yes |

Big foods are worth `GOURMET_BIG_POINTS` = 10, regular foods `GOURMET_REGULAR_POINTS` = 1. Each spawn picks a random kind from all 12 food `ItemKind`s (`ITKIND_FOODMAXIMTOMATO` .. `ITKIND_FOODAPPLE`).

### Big food positions

| Location | Coordinates |
|----------|-------------|
| Tower high | (71.00, 140.00, -345.00) |
| Tower low | (71.00, 88.00, -345.00) |
| Random panel | (-76.00, 133.00, -447.00) |
| Under building 1 | (-80.00, 53.00, -265.00) |
| Underground garage | (-2.00, 5.00, -87.00) |

Pass 2 shuffles a copy of 15 hand-placed positions (ramps, rooftops, tunnels) and uses a random 5-10 of them per event.

### Random-pass candidates

`CollectCandidates` builds the candidate set for passes 3-5 from stage spline midpoints:
- `Spline_GetCount()` (0x800cf38c) gives the segment count.
- `Spline_GetForward(seg)` (0x800cf3ac) gives each spline.
- `splGetSplinePoint(&out, spline, 0.5f)` (0x80414fc0) gives its midpoint.

A midpoint is kept only if it lies within 350 units (XZ, squared compare) of the city center (15.0, -267.4). City Trial yields on the order of 233 such points. The buffer size, `MAX_CANDIDATES` = 802, is City Trial's spline segment count.

Each random pass (`PlaceOnSplines`) Fisher-Yates shuffles the candidates, then greedily takes points at least 50 units (XZ) from every food placed so far. Big and pre-placed foods count toward that spacing, so it is enforced across passes, not just within one. The pre-placed locations themselves skip both the radius check and the spacing check.

## Item Setup

`FoodSlot_Spawn` calls `Item_InitDesc` (0x802509a0), then `CityItem_Create` (0x8024eef4), passing:
- the food kind and scale;
- the slot's spawn position;
- `coll_kind` as the collision param;
- an explicit forward vector.

It then overrides `ItemData.lifetime` (+0x44) to `GOURMET_ITEM_LIFETIME` (30000, about 8 minutes) instead of the default `ItemCommonParam.lifetime_min` plus random variance. Items go on `p_link = GAMEPLINK_ITEM` (13), and `CityItem_Create` sets `entity_class` internally.

**The forward vector must stay non-zero, including after landing.** The engine builds each item's model render matrix (the user-defined matrix at the JObj root, `JOBJ_USER_DEFINED_MTX`) from `ItemData.up` (+0x10C) and `ItemData.forward` (+0x100). A zero forward collapses the `up x forward` basis to rank 1 and squashes the model into an invisible sliver. Collision uses position data independently, so the food is still pickable, just unseen. Two places matter:

- **Spawn:** `FoodSlot_Spawn` passes forward `{0,0,1}` and leaves up NULL, so the food tilts to the ground normal on landing.
- **Settle:** the item's settle state (state 4, on landing) **zeroes `ItemData.forward`**, so the spawn value alone is not enough. The watcher proc re-asserts `forward = {0,0,1}` on every live food each frame. Up keeps the ground normal, so the food stays ground-aligned and visible.

`coll_kind` is the collision param of `Item_InitDesc` (`ItemDesc+0x4C`), which lands in the `ItemData` bitfield at +0x359 bits 2-4. The spawner uses 3 for the high-drop surface spawns and 2 for underground and pre-placed spawns. Per `item.h`, 3 is the point collision most items use.

## Respawn and Scoring

A single watcher GObj on `GAMEPLINK_1` runs `GourmetRace_WatcherProc` at priority 0. That link is chosen for two properties:
- **It freezes with the pause.** Pause kind 1 freezes plinks 1-17, while plink 0 (`GAMEPLINK_SYS`) keeps running, so respawn timers stop while the game is paused.
- **It runs before the event proc.** `GObj_UpdateAll` runs procs of equal priority in plink order, so the watcher runs ahead of `CityEvent_Think` on `GAMEPLINK_CITYEVENTSPAWN` (2). `End2` therefore never sees a food that was eaten after the last watcher pass.

The watcher owns a `FoodSlot` array. Each slot holds:
- the item GObj (NULL while eaten);
- its kind and `base_scale`;
- the spawn position, with the Y offset applied;
- the last position the food was seen at;
- `coll_kind`;
- a big-food flag;
- a respawn timer.

**Eaten detection.** For each tracked GObj, `FoodSlot_GetLive` walks the `GAMEPLINK_ITEM` list (`(*stc_gobj_lookup)[GAMEPLINK_ITEM]`, at most 101 entries).
- A GObj missing from the list counts as eaten.
- A GObj that is present must also match the slot's kind and `base_scale` (`ItemData+0x1C` / `+0xA8`). `HSD_ObjAlloc` hands out the most recently freed object first, so an eaten food's GObj can come straight back as a different item. The 2x/4x food scale never matches a vanilla item.
- This only works because the forced 30000-frame lifetime far outlasts the event, so a food can disappear only by being picked up.
- `GourmetRace_End2` runs the same check before destroying the remaining food.

**Points** go to the player whose rider is nearest in 3D to where the food was last seen.
- The rider position is `RiderData.pos`, which follows the machine while mounted, and all 5 slots are checked.
- The food position is `ItemData.pos` (+0xDC), sampled every frame while the food is alive.
- Collection requires contact, so proximity is a reliable proxy. Sampling the live position matters for the surface drops, which spawn 180 units up and then fall.

**Respawn timers** are `GOURMET_RESPAWN_TIME_BIG` = 1200 frames (20 s) for big foods and `GOURMET_RESPAWN_TIME` = 600 frames (10 s) for regular ones. The kind is re-randomized on respawn. If `CityItem_Create` returns NULL because the item cap is hit, the timer is set to 1 and the slot retries next frame.

**Reward.** `GourmetRace_End2` destroys the watcher, the remaining food and the HUD, then takes the highest score across the 5 player slots:
- a top score of 0 awards nothing;
- a solo winner gets 2x All Up via `SpawnItemPlayer(ply, ITKIND_ALLUP)`;
- on a tie, every tied player gets 1x.

## Score HUD

A per-player score row is drawn over the scene for the duration of the event. It uses the standard GObj/JObj/GX-link HUD pattern: models are instantiated as GObjs on `GAMEPLINK_HUD` and assigned a GX link, and an ortho camera GObj renders them because its `cobj_links` bitmask selects that link.

- **Camera.** One dedicated GObj (`GOBJ_EZCreator` -> `HSD_OBJKIND_COBJ`) built from `stc_text_cobjdesc` (0x805096a0), with `CObj_SetOrtho(cobj, 0, -480, 0, 640)`. Its `cobj_links` holds the single bit for GX link 23, and its GX callback is `CObjThink_Common` (0x8042a29c).
- **Rows.** Up to `HUD_MAX_PLAYERS` = 4, skipping `PKIND_NONE` slots. Each row has a `ScInfPlynum_scene_models` label and a `ScInfPausegaugect_scene_models` gauge, both pulled from the all-city archive (`Gm_GetIfAllCityArchive` -> `Archive_GetPublicAddress`) and instantiated with `JObj_LoadSet_SetPri` on `GAMEPLINK_HUD` at GX link 23. There is no backing panel.
- **Gauge parts.** Gauge JObjs are cached by depth-first index via `GObj_GetJObjIndex`:

  | Index | Part |
  |-------|------|
  | 1 | fill bar |
  | 4 | right digit |
  | 5 | left digit |
  | 6 | minus sign |

- **Digits.** A two-digit score writes the ones into the right digit and the tens into the left. A lone digit goes to the left slot so it reads as centered, and the right digit is hidden below 10.
- **Hidden parts.** Bar and sign are hidden at create and **re-hidden on every update**, because AnimAll can clear the flags.

`ScoreHUD_Update` runs from the watcher proc. It re-renders digits (via `HUD_UpdateElement`) only when a player's score changes, clamped to 99. `ScoreHUD_Destroy` tears down the label, gauge and camera GObjs from `GourmetRace_End2`.

Respawn times, point values, food count, spacing, scales and HUD layout are all `#define`s at the top of `event_gourmet_race.c`.
