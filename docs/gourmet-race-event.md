# Gourmet Race Event

A custom City Trial event (`CUSTOM_EVKIND_GOURMET_RACE`, kind 19) that scatters food items across the map for 60 seconds. Players compete to collect the most; each collected food respawns at the same spot after a cooldown, and the winner is handed All Up patches when the event ends. Implemented in `mods/custom_events/src/event_gourmet_race.c`.

Like every custom event it is a row in `custom_params[]` and `custom_functions[]` in `custom_events.c`; the mod's wrappers on the event state table call the kind's callbacks in place of the vanilla per-kind dispatch. Its parameters: 3600-frame duration (~60 s), siren intro, no sky change (`sky_preset` -1), BGM file 0x34 (`event_supercharge`), roll weight 20.

`GourmetRace_Start` resets scores, runs the spawner, and creates the watcher GObj and score HUD. `GourmetRace_Active` is a no-op - the watcher proc does all per-frame work. `GourmetRace_End2` tears everything down, tallies scores and grants the reward. There is no `check` and no per-frame `end`.

## Food Spawning

`GourmetRace_SpawnFood` targets `GOURMET_MAX_FOOD` = 60 foods in five passes. Every pass is implicitly capped by the game's concurrent item limit: `Item_Create` returns NULL once it is hit, and the spawner simply records fewer foods.

| Pass | What | Count | Y offset | `coll_kind` | Scale | Spacing filter |
|------|------|-------|----------|-------------|-------|----------------|
| 1 | Big foods at fixed landmarks | 5 | +1 | 2 | 4x (`GOURMET_BIG_ITEM_SCALE`) | no |
| 2 | Regular foods at pre-placed spots | 5-10 of 15 | +1 | 2 | 2x (`GOURMET_ITEM_SCALE`) | no |
| 3 | Surface random | half of remaining budget | +180 (`GOURMET_SURFACE_HEIGHT`) | 3 | 2x | yes |
| 4 | Underground random (candidate Y < 44.0) | other half | +5 (`GOURMET_ABOVE_SPLINE_HEIGHT`) | 2 | 2x | yes |
| 5 | Overflow surface (if pass 4 fell short) | remainder | +180 | 3 | 2x | yes |

Big foods are worth `GOURMET_BIG_POINTS` = 10, regular foods `GOURMET_REGULAR_POINTS` = 1. Kinds are picked per spawn by `RandomFoodKind()` from all 12 food `ItemKind`s (`ITKIND_FOODMAXIMTOMATO` .. `ITKIND_FOODAPPLE`).

### Big food positions

| Location | Coordinates |
|----------|-------------|
| Tower high | (71.00, 140.00, -345.00) |
| Tower low | (71.00, 88.00, -345.00) |
| Random panel | (-76.00, 133.00, -447.00) |
| Under building 1 | (-80.00, 53.00, -265.00) |
| Underground garage | (-2.00, 5.00, -87.00) |

Pass 2 draws from 15 hand-placed positions (ramps, rooftops, tunnels) through a shuffled index array, using a random 5-10 of them per event.

### Random-pass candidates

Passes 3-5 build their candidate set from stage spline midpoints, in `CollectCandidates`: `Spline_GetCount()` (0x800cf38c) gives the segment count, `Spline_GetForward(seg)` (0x800cf3ac) the spline, and `splGetSplinePoint(&out, spline, 0.5f)` (0x80414fc0) the midpoint. Candidates are kept only within 350 units (XZ, squared compare) of city center (15.0, -267.4); City Trial yields on the order of 233 such points and the buffer `MAX_CANDIDATES` is 802.

Each pass Fisher-Yates shuffles the candidate array, then greedily takes points at least 50 units (XZ) from every position recorded so far, so spacing is enforced across passes, not just within one. The pre-placed locations of passes 1-2 bypass both the radius and the spacing check.

## Item Setup

`SpawnFoodItem` calls `Item_InitDesc` (0x802509a0) then `Item_Create` (0x8024eef4) with the food kind, scale, position, `coll_kind` as the collision param and an explicit forward vector, then overrides `ItemData.lifetime` (+0x44) to `GOURMET_ITEM_LIFETIME` (30000, ~8 minutes) instead of the default `ItemCommonParam.lifetime_min` plus random variance. Items spawn on `p_link = GAMEPLINK_ITEM` (13); `entity_class` is set internally by `Item_Create`.

**The forward vector must stay non-zero, including after landing.** The engine builds each item's model render matrix (the user-defined matrix at the JObj root, `JOBJ_USER_DEFINED_MTX`) from `ItemData.up` (+0x10C) and `ItemData.forward` (+0x100). A zero forward makes the `up x forward` basis collapse to rank-1 and squashes the model into an invisible sliver - collision uses position data independently, so the food is still pickable, just unseen. Two places matter:

- **Spawn:** `SpawnFoodItem` passes forward `{0,0,1}` and leaves up NULL so the food tilts to the ground normal on landing.
- **Settle:** the item's settle state (state 4, on landing) **zeroes `ItemData.forward`**, so the spawn value is not enough. The watcher proc re-asserts `forward = {0,0,1}` on every live food each frame; up keeps the ground normal, so the food stays ground-aligned and visible.

`coll_kind` is the collision param of `Item_InitDesc` (`ItemDesc+0x4C`), landing in the `ItemData` bitfield at +0x359 bits 2-4. The spawner uses 3 for the high-drop surface spawns and 2 for underground and pre-placed spawns, treating them as "ground-snap with rejection" vs "no rejection"; per `item.h`, 3 is the point collision used by most items.

## Respawn and Scoring

A single watcher GObj is created on `GAMEPLINK_SYS` with `GourmetRace_WatcherProc` at priority 0. It owns a `FoodSlot` array holding, per food, the spawn position (Y offset pre-applied), kind, scale, `coll_kind`, the current item GObj (NULL while eaten), a respawn timer and a big-food flag.

**Eaten detection** walks the `GAMEPLINK_ITEM` GObj linked list once per frame (`(*stc_gobj_lookup)[GAMEPLINK_ITEM]`, capped at 128 entries) and marks which tracked GObjs are still present; any tracked GObj that has vanished counts as eaten. This is only safe because the forced 30000-frame lifetime far outlasts the 60-second event, so a food can disappear only through a pickup. No destructor hook is needed and the pointers are never dereferenced after they go missing.

**Points** go to the nearest player, by 3D distance from the food's *spawn position* to each `MachineData.pos` (`FindNearestPlayer`, 5 slots). Collection requires physical contact, so proximity is a reliable proxy.

**Respawn timers** are `GOURMET_RESPAWN_TIME_BIG` = 1200 frames (20 s) for big foods and `GOURMET_RESPAWN_TIME` = 600 frames (10 s) for regular ones, and the kind is re-randomized on respawn. If `Item_Create` returns NULL because the item cap is hit, the timer is set to 1 and the slot retries next frame.

**Reward:** `GourmetRace_End2` destroys remaining food, the watcher and the HUD, then takes the highest score across the 5 player slots. A score of 0 awards nothing; a solo winner gets 2x All Up via `SpawnItemPlayer(ply, ITKIND_ALLUP)`, and on a tie every tied player gets 1x.

## Score HUD

A per-player score row is drawn over the scene for the duration of the event, with the standard GObj/JObj/GX-link HUD pattern: models are instantiated as GObjs on `GAMEPLINK_HUD`, assigned a GX link, and rendered by an ortho camera GObj whose `cobj_links` bitmask selects that link.

- The camera is one dedicated GObj (`GOBJ_EZCreator` -> `HSD_OBJKIND_COBJ`, COBJDesc 0x805096a0, `CObj_SetOrtho(cobj, 0, -480, 0, 640)`) with `cobj_links` set to the single bit for GX link 23 and `CObjThink_Common` (0x8042a29c) as its GX callback.
- Per player (up to `HUD_MAX_PLAYERS` = 4, skipping `PKIND_NONE` slots): a `ScInfPlynum_scene_models` label and a `ScInfPausegaugect_scene_models` gauge, both pulled from the all-city archive (`Gm_GetIfAllCityArchive` -> `Archive_GetPublicAddress`) and instantiated with `JObj_LoadSet_SetPri` on `GAMEPLINK_HUD` at GX link 23. No backing panel.
- Gauge JObjs are cached by depth-first index via `GObj_GetJObjIndex`: 1 = fill bar, 4 and 5 = the digit slots, 6 = minus sign. Child 4 sits visually right and child 5 visually left, so a two-digit score writes the ones digit into 4 and the tens into 5, and a lone digit goes to 5 to read as centered. Bar and sign are hidden at create and **re-hidden on every update**, because AnimAll can clear the flags; slot 4 is additionally hidden below a score of 10.

`ScoreHUD_Update` runs from the watcher proc and re-renders digits (via `HUD_UpdateElement`) only when a player's score changes, clamped to 99. `ScoreHUD_Destroy` tears down the label, gauge and camera GObjs from `GourmetRace_End2`.

Respawn times, point values, food count, spacing, scales and HUD layout are all `#define`s at the top of `event_gourmet_race.c`.
