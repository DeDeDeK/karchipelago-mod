# Gourmet Race Event

Custom City Trial event where food items are scattered across the map. Players compete to collect the most food within the time limit. The winner receives All Up patches as a reward. It is the only custom event currently enabled (`weight = 20`, vs `weight = 0` for the other three); see `custom-events.md` for the framework.

## Event Overview

- **Type:** Competitive collection event (custom event `CUSTOM_EVKIND_GOURMET_RACE`, index 19 = `EVKIND_NUM + 3`)
- **Duration:** 60 seconds (3600 frames)
- **Mechanic:** Up to 60 food items spawn across the city in four passes. Players ride around collecting them. Each collected food respawns at the same location after a cooldown. Big foods are worth 10 points, regular foods 1 point.
- **Reward:** Winner receives 2x All Up (via `SpawnItemPlayer`, `ITKIND_ALLUP`). On tie, all tied players receive 1x All Up each.

### Registration (`custom_events.c`)

`custom_params[CUSTOM_EVKIND_GOURMET_RACE - EVKIND_NUM]`:

| Field | Value |
|-------|-------|
| `duration` | 3600 (~60 s) |
| `is_siren` | 1 |
| `sky_preset` | -1 (no sky change) |
| `bgm_file` | 0x34 (Runamok BGM) |
| `weight` | 20 |
| `label` | `"Gourmet Race"` |
| `hud_text` | `"Gourmet Race!"` (composed into a SIS entry by `CustomEvents_InitSis`) |

`custom_functions[...]`: `.start = GourmetRace_Start`, `.active = GourmetRace_Active`, `.end2 = GourmetRace_End2`.

Selection: `CustomEvents_ExtendedRoll` adds each custom event's `weight` to the vanilla `Gm_Roll` pool; on a Gourmet Race win it calls `CustomEvent_Do(19)`. Gourmet Race carries `weight = 20`; the other three custom events are at `weight = 0`, so only Gourmet Race participates in the natural roll.

### Entry points

| Symbol | File | Role |
|--------|------|------|
| `GourmetRace_Start` | `event_gourmet_race.c` | reset scores, spawn food, create watcher + score HUD |
| `GourmetRace_Active` | `event_gourmet_race.c` | no-op (watcher proc does the work) |
| `GourmetRace_End2` | `event_gourmet_race.c` | tear down food/watcher/HUD, tally scores, grant All Up |
| `GourmetRace_WatcherProc` (static) | `event_gourmet_race.c` | per-frame eaten-detection, scoring, respawn |
| `GourmetRace_SpawnFood` (static) | `event_gourmet_race.c` | the five-pass spawner |

## Files

| File | Purpose |
|------|---------|
| `mods/custom_events/src/event_gourmet_race.h` | Header: Start/Active/End2 declarations |
| `mods/custom_events/src/event_gourmet_race.c` | Full event implementation |
| `mods/custom_events/include/custom_events_api.h` | `CUSTOM_EVKIND_GOURMET_RACE` enum entry (= 19; public API header) |
| `mods/custom_events/src/custom_events.c` | Event params (duration, sky, BGM, weight, SIS label) + function-table registration; `CustomEvents_ExtendedRoll` selection hook |
| `mods/custom_events/src/main.c` | `ModDesc` — wires `.OnBoot`/`.On3DLoadEnd`, activating the framework |
| `mods/archipelago_debug/src/main.c` | Imports `CustomEventsAPI` to trigger the event for testing |

## Food Spawning — Four-Pass System

Total target: up to 60 foods (hard-capped by the game's item limit of ~100 concurrent items).

### Pass 1: Big Foods (5 items)
- 5 pre-placed locations at notable City Trial landmarks
- Spawned 1 unit above the coordinate, `coll_kind=2` (no ground rejection)
- Scale: 4x (`GOURMET_BIG_ITEM_SCALE`)
- Not subject to city radius check
- Worth 10 points each

| Location | Coordinates |
|----------|-------------|
| Tower high | (71.00, 140.00, -345.00) |
| Tower low | (71.00, 88.00, -345.00) |
| Random panel | (-76.00, 133.00, -447.00) |
| Under building 1 | (-80.00, 53.00, -265.00) |
| Underground garage | (-2.00, 5.00, -87.00) |

### Pass 2: Pre-placed Regular Foods (5-10 of 15 locations)
- 15 candidate positions at interesting map features (ramps, rooftops, tunnels, etc.)
- 5-10 randomly selected each event via shuffled index array
- Spawned 1 unit above the coordinate, `coll_kind=2`
- Scale: 2x (`GOURMET_ITEM_SCALE`)
- Not subject to city radius check

### Pass 3: Surface Random Foods (half of remaining budget)
- Spline midpoint candidates within city radius (350 units from city center)
- Spawned at spline Y + 180 units, `coll_kind=3` (raycast ground snap, rejects if no ground found)
- Fisher-Yates shuffle + greedy minimum-spacing selection (50 unit minimum)
- Scale: 2x

### Pass 4: Underground Random Foods (other half of remaining)
- Same spline candidates, filtered to Y < 44.0 (underground tunnels/caves)
- Spawned at spline Y + 5 units, `coll_kind=2` (no ground rejection)
- Scale: 2x

### Pass 5: Overflow Surface (if pass 4 falls short)
- If not enough underground candidates exist, remaining budget fills as surface spawns (same as pass 3)
- Ensures the full 60-food target is reached

### City Radius Filter
- Center: (15.0, -267.4) in XZ
- Radius: 350 units (squared comparison)
- Applied to passes 3-5 only; pre-placed locations bypass this check

### Food Types
All 12 food item kinds are used, randomly selected per spawn (`RandomFoodKind()` → `HSD_Randi`):
`ITKIND_FOODMAXIMTOMATO` through `ITKIND_FOODAPPLE` (ItemKind values **39-50**, per `item.h`).

## Respawn System

### Watcher GObj Architecture
A single watcher GObj is created on `GAMEPLINK_SYS` with a proc that runs every frame. It manages a `FoodSlot` array tracking all spawned foods.

```c
typedef struct FoodSlot {
    Vec3 spawn_pos;     // position to spawn at (Y offset pre-applied)
    ItemKind kind;      // food kind (re-randomized on respawn)
    float scale;        // item scale (4x for big, 2x for regular)
    int coll_kind;      // collision kind (2 or 3)
    GOBJ *gobj;         // current item GObj, NULL if eaten/pending
    int respawn_timer;  // frames until respawn, 0 = not pending
    int is_big;         // big food flag (affects points + respawn time)
} FoodSlot;
```

### Detection Method: GObj List Validation
The watcher detects eaten food by walking the `GAMEPLINK_ITEM` GObj linked list once per frame and checking which tracked food GObjs are no longer present. Access via `(*stc_gobj_lookup)[GAMEPLINK_ITEM]`.

This works because:
- Item lifetime is set to 30000 frames (~8 minutes), far exceeding the 60-second event duration
- Therefore any food disappearance during the event must be a player pickup
- No destructor hook needed; no dangling pointer risk

### Respawn Timers
- Big foods: 20 seconds (1200 frames)
- Regular foods: 10 seconds (600 frames)
- If `Item_Create` returns NULL on respawn (item cap hit), retries next frame

## Scoring

### Point Attribution
When a food disappears, the watcher finds the nearest player (by 3D distance from the food's spawn position to each `MachineData.pos`) and awards them the points. This is reliable because food collection requires physical contact.

### Point Values
- Regular food: 1 point
- Big food: 10 points

### End-of-Event Rewards
1. Find the highest score among all players
2. If no food was eaten (best_score == 0), no reward
3. Count how many players tied for the highest score
4. **Solo winner:** 2x All Up (via `SpawnItemPlayer`)
5. **Tie:** All tied players receive 1x All Up each

## Event Lifecycle

### GourmetRace_Start
1. Reset scores array to 0
2. Run four-pass food spawn (registers each food into `FoodSlot` array)
3. Create watcher GObj on `GAMEPLINK_SYS`

### GourmetRace_Active
- Respawn and scoring handled entirely by the watcher GObj proc
- No work done in the Active callback itself

### GourmetRace_End2
1. Set `gourmet_active = 0`
2. Destroy all remaining food GObjs
3. Destroy watcher GObj
4. Calculate scores, determine winner(s), spawn All Up rewards

## Technical Details

### Item System Integration
- Items created via `Item_InitDesc` + `Item_Create` (standard item spawning API). `SpawnFoodItem` passes the food kind, scale, `pos`, the `coll_kind` as the collision param, and an explicit forward vector (`{0,0,1}`; up left `NULL`), then overrides `ItemData.lifetime` (0x48) to `GOURMET_ITEM_LIFETIME` (30000).
- **The forward vector must stay non-zero, including after landing.** The engine builds each item's model render matrix (the user-defined matrix at the JObj root, `JOBJ_USER_DEFINED_MTX`) from `ItemData.up` (0x10C) and `ItemData.forward` (0x100). A zero forward makes the basis (`up × forward`) collapse to rank-1, squashing the model into an invisible sliver. Collision uses point/position data independently, so such a food is still pickable - just unseen. Two places matter:
  - **Spawn:** `SpawnFoodItem` passes an explicit forward (`{0,0,1}`) and leaves up `NULL` so the food tilts to the ground normal on landing.
  - **Settle:** the item's settle state (state 4, on landing) **zeroes `ItemData.forward`**, so a one-time spawn value is not enough. `GourmetRace_WatcherProc` re-asserts `forward = {0,0,1}` on every live food each frame; up keeps the ground normal, so the food stays ground-aligned and visible.
- `coll_kind` is the collision param of `Item_InitDesc` (stored to `ItemData.coll_kind`, 0x4C). The code uses `coll_kind=3` for surface (high-up) spawns and `coll_kind=2` for underground/pre-placed spawns, treating these as "ground-snap with rejection" vs "no rejection" respectively. (Per `item.h`, `coll_kind` 3 = point collision used by most items; the precise snap/reject behavior for 2 vs 3 reflects the spawner's intent.)
- Item hard cap: `Item_Create` returns NULL once the city item limit is hit; the spawner simply records fewer foods and the respawn path retries next frame.
- Items spawn on `p_link = 13` (`GAMEPLINK_ITEM`); `entity_class` is set internally by `Item_Create`.

### Spline System
- `Spline_GetCount()` returns number of spline segments in current stage
- `Spline_GetForward(seg)` returns HSD spline pointer for a segment
- `splGetSplinePoint(&out, spline, 0.5f)` evaluates spline midpoint
- Candidates buffer is capped at `MAX_CANDIDATES` (802); `CollectCandidates` keeps only midpoints inside the city radius. City Trial yields on the order of a couple hundred such points (~233).

## Score HUD

A real-time per-player score display (`ScoreHUD`) is implemented and shown on-screen during the event. See `custom-hud.md` for the general GObj/JObj/GX-link HUD pattern this follows.

`ScoreHUD_Create` builds, for each non-`PKIND_NONE` player (up to `HUD_MAX_PLAYERS` = 4):
- A dedicated ortho camera GObj (`GOBJ_EZCreator` → `HSD_OBJKIND_COBJ`, `CObj_SetOrtho`) driving a single GX link, FG = 23.
- A player-number label model (`ScInfPlynum_scene_models`) and a gauge model (`ScInfPausegaugect_scene_models`), both pulled from the all-city archive (`Gm_GetIfAllCityArchive` → `Archive_GetPublicAddress`) and instantiated via `JObj_LoadSet_SetPri` on `GAMEPLINK_HUD`.

The rows render directly over the scene with no backing panel.

The gauge's digit JOBJs are cached by depth-first index via `GObj_GetJObjIndex`:

```c
typedef struct ScoreHUD
{
    GOBJ *label_gobj;   // ScInfPlynum model
    GOBJ *gauge_gobj;   // ScInfPausegaugect model
    JOBJ *ones_j;       // child index 5
    JOBJ *tens_j;       // child index 4
    JOBJ *sign_j;       // child index 6 (minus, hidden)
    JOBJ *bar_j;        // child index 1 (fill bar, hidden)
    int prev_score;     // change detection
} ScoreHUD;
```

`ScoreHUD_Update` (called from the watcher proc each frame) re-renders digits only when a player's score changes, clamped to 99, using `HUD_UpdateElement`. The bar and sign JOBJs are kept hidden (re-hidden each update in case AnimAll re-clears the flags). `ScoreHUD_Destroy` tears down all label/gauge/camera GObjs in `GourmetRace_End2`.

Note: `CObj_GX` (`0x8042a29c`) is accessed as a raw function-pointer cast in the source (not in `link.ld`); it corresponds to `CObjThink_Common` in `GKYE01.map`, used here as the HUD camera's GX callback.

## Future Work

- **Sound/Visual Feedback:** Collection sound effects, point popup text, winner announcement.
- **Tuning:** Respawn times, point values, food count, and event duration are all configurable via defines and may need adjustment after playtesting.
