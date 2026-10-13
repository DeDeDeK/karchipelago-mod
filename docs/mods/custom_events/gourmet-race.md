# Gourmet Race Event

A custom City Trial event (`CUSTOM_EVKIND_GOURMET_RACE`, kind 19). For 60 seconds food is scattered
across the city and players race to eat the most; each eaten food respawns at its spot after a
cooldown, and the winner gets All Up patches when the event ends. It runs through the
`custom_events` framework with a siren intro, no sky change, BGM file 0x34
(`event_supercharge`), a 3600-frame duration and roll weight 20.

It uses two callbacks. `GourmetRace_Start` resets the scores, spawns the food, and creates a
watcher GObj and the score display; `GourmetRace_End2` tears all of it down and awards the prize.
A watcher proc does every per-frame job, and `Start` resets everything a cut-off run leaves behind,
so the event needs no `active`, `end` or `abort`.

## Spawning Food

The spawner aims for 60 foods in five passes, each picking a random kind from the 12 food
`ItemKind`s. `CityItem_Create` (0x8024eef4) returns NULL once 101 items are live, so any pass can
come up short, and the event simply tracks fewer foods.

| Pass | What | Count | Height above the point | Collision | Scale |
|---|---|---|---|---|---|
| 1 | Big foods at five fixed landmarks (tower, panel, under a building, the underground garage) | 5 | 1 | 2 | 4x |
| 2 | Regular foods at hand-placed spots (ramps, rooftops, tunnels), a random 5-10 of 15 | 5-10 | 1 | 2 | 2x |
| 3 | Drops from above spline points | half the remaining budget | 180 | 3 | 2x |
| 4 | Underground spline points (Y below 44) | the other half | 5 | 2 | 2x |
| 5 | More drops, if pass 4 fell short | the shortfall | 180 | 3 | 2x |

Big foods score 10 points, regular foods 1.

The candidates for passes 3-5 are the midpoints of the stage's splines (`Spline_GetCount`
0x800cf38c, `Spline_GetForward` 0x800cf3ac, `splGetSplinePoint` 0x80414fc0) within 350 units, on
the ground plane, of the city center. Each pass shuffles them and greedily takes points at least 50
units from every food placed so far, across all passes; the fixed spots skip both checks.

## Item Setup

Each food goes through `Item_InitDesc` (0x802509a0) and `CityItem_Create` with its kind, scale,
spawn position, collision kind and a forward vector, then has its `ItemData.lifetime` (+0x44)
forced to 30000 frames, far longer than the event.

**The forward vector must stay non-zero.** The engine builds an item's render matrix from
`ItemData.up` (+0x10C) and `ItemData.forward` (+0x100); a zero forward collapses the basis and
squashes the model into an invisible sliver that can still be picked up. The spawn passes forward
(0, 0, 1) and no up, so the food tilts to the ground on landing - but landing zeroes
`ItemData.forward`, so the watcher writes it back on every live food every frame.

The collision kind is `Item_InitDesc`'s collision parameter: 3, the point collision most items
use, for the high drops; 2 for the underground and fixed spots.

## Scoring and Respawn

The watcher GObj sits on `GAMEPLINK_1` with its proc at priority 0, for two reasons: that p_link
freezes with the match pause, so respawn timers stop while paused, and procs of one priority run in
p_link order, so the watcher runs ahead of the event proc on p_link 2 and `End2` never sees a food
eaten after the last watcher pass.

For each food it tracks the GObj, kind, scale, spawn point, last seen position, collision kind and a
respawn timer:

- **Eaten.** A food whose GObj is gone from the `GAMEPLINK_ITEM` list was eaten - nothing else can
  remove it within its forced lifetime. The GObj must also still have the food's kind and scale,
  because the allocator hands a freed GObj out again first and the next item could take it; no
  vanilla item has the 2x or 4x food scale.
- **Points** go to the player whose rider is nearest, in 3D, to where the food was last seen. Eating
  takes contact, so nearest is reliable, and the last seen position follows a drop as it falls.
- **Respawn** comes after 1200 frames for a big food and 600 for a regular one, with a fresh random
  kind. At the item cap the timer is set to 1 and the spawn retries next frame.

`End2` destroys the watcher, the remaining food and the score display, then awards the prize: no
points, no prize; a single winner gets two All Up patches; tied winners get one each.

## Score Display

A score row per occupied player slot, up to four, drawn over the round while the event runs. It
uses its own ortho camera: a camera GObj built from `stc_text_cobjdesc` with
`CObj_SetOrtho(0, -480, 0, 640)`, `CObjThink_Common` as its draw callback and `cobj_links`
holding only GX link 23. Each row is a `ScInfPlynum_scene_models` label and a
`ScInfPausegaugect_scene_models` gauge from the common HUD archive, loaded with
`JObj_LoadSet_SetPri` onto link 23.

The gauge's parts are depth-first joints 1 (fill bar), 4 (right digit), 5 (left digit) and 6
(minus sign). A score of 10 or more puts its tens on the left digit and ones on the right; a lower
score goes on the left digit alone, with the right one hidden, so it reads centered. The bar and
sign are hidden at creation and again after every update, because the digit animation can clear
their hidden flags. Digits change only when a score does, capped at 99.
