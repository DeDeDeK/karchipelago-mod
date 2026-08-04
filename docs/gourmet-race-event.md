# Gourmet Race Event

A custom City Trial event (`CUSTOM_EVKIND_GOURMET_RACE`, kind 19) that scatters food items across the map for 60 seconds. Players compete to collect the most; each collected food respawns at the same spot after a cooldown, and the winner is handed All Up patches when the event ends. Implemented in `mods/custom_events/src/event_gourmet_race.c`.

## Registration

`custom_params[CUSTOM_EVKIND_GOURMET_RACE - EVKIND_NUM]` in `custom_events.c`:

| Field | Value |
|-------|-------|
| `duration` | 3600 (~60 s) |
| `is_siren` | 1 |
| `sky_preset` | -1 (no sky change) |
| `bgm_file` | 0x34 (`event_supercharge`) |
| `weight` | 20 |
| `label` | `"Gourmet Race"` |
| `hud_text` | `"Gourmet Race!"` |

`custom_functions[...]` registers `.start = GourmetRace_Start`, `.active = GourmetRace_Active`, `.end2 = GourmetRace_End2`. There is no `check` and no per-frame `end`.

## Entry Points

| Symbol | Role |
|--------|------|
| `GourmetRace_Start` | Reset scores, run the spawn passes, create the watcher GObj + score HUD |
| `GourmetRace_Active` | No-op - the watcher proc does all per-frame work |
| `GourmetRace_End2` | Tear down food/watcher/HUD, tally scores, grant All Up |
| `GourmetRace_WatcherProc` (static) | Per-frame eaten-detection, scoring, respawn, HUD update |
| `GourmetRace_SpawnFood` (static) | The five-pass spawner |

## Food Spawning

Target is `GOURMET_MAX_FOOD` = 60 foods, in five passes. Each pass is capped by the game's concurrent item limit - `Item_Create` returns NULL once it is hit and the spawner simply records fewer foods.

| Pass | What | Count | Y offset | `coll_kind` | Scale | Radius filter |
|------|------|-------|----------|-------------|-------|---------------|
| 1 | Big foods at fixed landmarks | 5 | +1 | 2 | 4x (`GOURMET_BIG_ITEM_SCALE`) | no |
| 2 | Regular foods at pre-placed spots | 5-10 of 15 | +1 | 2 | 2x (`GOURMET_ITEM_SCALE`) | no |
| 3 | Surface random | half of remaining budget | +180 (`GOURMET_SURFACE_HEIGHT`) | 3 | 2x | yes |
| 4 | Underground random (spline Y < 44.0) | other half | +5 (`GOURMET_ABOVE_SPLINE_HEIGHT`) | 2 | 2x | yes |
| 5 | Overflow surface (if pass 4 fell short) | remainder | +180 | 3 | 2x | yes |

Big foods are worth `GOURMET_BIG_POINTS` = 10, regular foods `GOURMET_REGULAR_POINTS` = 1.

### Big food positions

| Location | Coordinates |
|----------|-------------|
| Tower high | (71.00, 140.00, -345.00) |
| Tower low | (71.00, 88.00, -345.00) |
| Random panel | (-76.00, 133.00, -447.00) |
| Under building 1 | (-80.00, 53.00, -265.00) |
| Underground garage | (-2.00, 5.00, -87.00) |

Pass 2 draws from 15 hand-placed positions (ramps, rooftops, tunnels) selected through a shuffled index array; a random 5-10 of them are used per event.

### Random-pass candidates

Passes 3-5 build their candidate set from stage spline midpoints:

- `Spline_GetCount()` (0x800cf38c) gives the segment count, `Spline_GetForward(seg)` (0x800cf3ac) the spline, `splGetSplinePoint(&out, spline, 0.5f)` (0x80414fc0) the midpoint.
- Candidates are filtered to a city radius of 350 units (squared compare) around XZ center (15.0, -267.4). City Trial yields on the order of 233 such points; the buffer is `MAX_CANDIDATES` = 802.
- Each pass Fisher-Yates shuffles the candidate array, then greedily picks points at least 50 units (XZ) from every previously recorded spawn.

The pre-placed locations (passes 1-2) bypass the radius and spacing checks.

### Food kinds

All 12 food `ItemKind`s (`ITKIND_FOODMAXIMTOMATO` .. `ITKIND_FOODAPPLE`, values 39-50 per `item.h`) are used, picked at random per spawn by `RandomFoodKind()`.

## Item Setup

`SpawnFoodItem` calls `Item_InitDesc` (0x802509a0) + `Item_Create` with the food kind, scale, position, `coll_kind` as the collision param, and an explicit forward vector, then overrides `ItemData.lifetime` (+0x44) to `GOURMET_ITEM_LIFETIME` (30000, ~8 minutes) - the default would come from `ItemCommonParam.lifetime_min` + a random variance.

**The forward vector must stay non-zero, including after landing.** The engine builds each item's model render matrix (the user-defined matrix at the JObj root, `JOBJ_USER_DEFINED_MTX`) from `ItemData.up` (+0x10C) and `ItemData.forward` (+0x100). A zero forward makes the basis (`up x forward`) collapse to rank-1, squashing the model into an invisible sliver. Collision uses position data independently, so such a food is still pickable, just unseen. Two places matter:

- **Spawn:** `SpawnFoodItem` passes forward `{0,0,1}` and leaves up `NULL` so the food tilts to the ground normal on landing.
- **Settle:** the item's settle state (state 4, on landing) **zeroes `ItemData.forward`**, so a one-time spawn value is not enough. `GourmetRace_WatcherProc` re-asserts `forward = {0,0,1}` on every live food each frame; up keeps the ground normal, so the food stays ground-aligned and visible.

`coll_kind` is the collision param of `Item_InitDesc` (`ItemDesc+0x4C`), landing in the `ItemData` bitfield at +0x359 bits 2-4. The spawner uses 3 for surface (high-drop) spawns and 2 for underground/pre-placed spawns, treating these as "ground-snap with rejection" vs "no rejection". Per `item.h`, `coll_kind` 3 is the point collision used by most items; the 2-vs-3 snap/reject distinction reflects the spawner's intent.

Items spawn on `p_link = GAMEPLINK_ITEM` (13); `entity_class` is set internally by `Item_Create`.

## Respawn and Scoring

A single watcher GObj is created on `GAMEPLINK_SYS` with a priority-0 proc. It owns a `FoodSlot` array:

```c
typedef struct FoodSlot {
    Vec3 spawn_pos;     // position to spawn at (Y offset pre-applied)
    ItemKind kind;      // food kind (re-randomized on respawn)
    float scale;
    int coll_kind;
    GOBJ *gobj;         // current item GObj, NULL if eaten/pending
    int respawn_timer;  // frames until respawn, 0 = not pending
    int is_big;         // affects points + respawn time
} FoodSlot;
```

### Eaten detection

Each frame the proc walks the `GAMEPLINK_ITEM` GObj linked list once (`(*stc_gobj_lookup)[GAMEPLINK_ITEM]`, capped at 128 entries) and marks which tracked food GObjs are still present. Any tracked GObj that has vanished counts as eaten.

This is safe because item lifetime is forced to 30000 frames, far beyond the 60-second event, so a food can only disappear through a player pickup. No destructor hook is needed and there is no dangling-pointer risk.

### Point attribution

When a food disappears, the watcher awards its points to the nearest player - 3D distance from the food's *spawn position* to each `MachineData.pos`. Collection requires physical contact, so proximity is a reliable proxy.

### Respawn timers

- Big foods: `GOURMET_RESPAWN_TIME_BIG` = 1200 frames (20 s)
- Regular foods: `GOURMET_RESPAWN_TIME` = 600 frames (10 s)
- If `Item_Create` returns NULL on respawn (item cap hit), the timer is set to 1 so it retries next frame.

### End-of-event reward

`GourmetRace_End2` destroys the remaining food, the watcher and the HUD, then finds the highest score across the 5 player slots. If it is 0, nothing is awarded. A solo winner gets 2x All Up via `SpawnItemPlayer(ply, ITKIND_ALLUP)`; on a tie every tied player gets 1x.

## Score HUD

A per-player score row is drawn over the scene for the duration of the event, using the general GObj/JObj/GX-link HUD pattern documented in `custom-hud.md`. Event-specific choices:

- One dedicated ortho camera GObj (`GOBJ_EZCreator` -> `HSD_OBJKIND_COBJ`, COBJDesc 0x805096a0, `CObj_SetOrtho(cobj, 0, -480, 0, 640)`), with `cobj_links` set to a single bit for GX link 23 and `CObjThink_Common` (0x8042a29c, accessed as a raw pointer cast - it is not in `link.ld`) as its GX callback.
- Per player (up to `HUD_MAX_PLAYERS` = 4, skipping `PKIND_NONE` slots): a `ScInfPlynum_scene_models` label and a `ScInfPausegaugect_scene_models` gauge, both pulled from the all-city archive (`Gm_GetIfAllCityArchive` -> `Archive_GetPublicAddress`) and instantiated with `JObj_LoadSet_SetPri` on `GAMEPLINK_HUD` at GX link 23. No backing panel.
- Gauge JOBJs are cached by depth-first index via `GObj_GetJObjIndex`: 1 = fill bar, 4 and 5 = the two digit slots, 6 = minus sign. Child 4 sits visually right and child 5 visually left, so a two-digit score writes the ones into 4 and the tens into 5, and a lone digit goes to 5 to read as centered. Bar and sign are kept hidden and re-hidden on every update, because AnimAll can clear the flags; slot 4 is additionally hidden below a score of 10.

`ScoreHUD_Update` runs from the watcher proc and re-renders digits (via `HUD_UpdateElement`) only when a player's score changes, clamped to 99. `ScoreHUD_Destroy` tears down the label, gauge and camera GObjs in `GourmetRace_End2`.

## Future Work

- **Feedback:** collection sound effects, point popup text, winner announcement.
- **Tuning:** respawn times, point values, food count and event duration are all `#define`s at the top of `event_gourmet_race.c`.
