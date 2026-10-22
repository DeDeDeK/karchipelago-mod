---
name: stage-map
description: >
    Use the stage-map skill for anything spatial on a Kirby Air Ride stage - City Trial, the Air
    Ride courses, the stadiums. Activate when the user or the task needs to:
    - Place something in a stage (an event prop, a spawn point, a weather effect, a check volume)
      or pick a spot for it (`spots`)
    - Know what is at or under a coordinate: ground height, stacked floors, ceiling, ground type,
      zones, nearby props and landmarks (`ground`, `at`)
    - Find where a named place is ("the volcano", "the lighthouse", "the garden in the sky")
      (`find`, `at --landmark`)
    - Check a path or flight for clearance and clipping (`sweep`, `section`)
    - See a stage from above or in cross-section, with overlays (`render`, `section`)
    - List positions, zones, props, rails, splines or course laps in world units (`dump`,
      `course`)
    - Read where each player is in a running Dolphin, or capture a new landmark there (`live`)
    Wraps `scripts/hsd/stage.py` over `scripts/hsd/world.py`, the stage in world units, and the
    curated landmark table `docs/data/stage-landmarks.csv`.
---

# stage-map Skill

`scripts/hsd/stage.py` loads a stage's `Gr*.dat` and `Gr*Model.dat` from `iso/files/`, bakes the
collision, zones, positions and splines into **world units** the same way the game does, and
answers spatial questions over the result. World units are what a live machine or rider position
is in and what mod code compares against, so every coordinate the tool prints can go straight
into C.

Reach for it before writing a coordinate into a mod, before guessing a height, and before writing
a one-off script against the collision mesh. Past sessions rebuilt that script many times; most
copies got the frame wrong (see the traps below).

## Commands

Stages are named by file stem with or without `Gr` (`City1`, `GrCity1`), a unique prefix
(`plants`), or an alias (`city`, `magma`, `derby2`); `stages` lists them.

| Command | Answers |
|---|---|
| `summary STAGE` | scale, out-of-bounds box, land extents, sea share, stacked share, lid, counts |
| `ground STAGE X Z` | every surface on the vertical line: height, floor/wall/ceiling, facing, ground type, joint |
| `at STAGE X Y Z` (or `X Z`, or `--landmark NAME`) | landmarks it is inside or near, floor below, ceiling above, zones, land or sea and distance to it, the prop it is in or nearest to, nearest rail, nearest authored positions |
| `find STAGE TEXT` | landmarks whose name or alias contains TEXT (`''` for all, `all` for every stage) |
| `dump STAGE positions\|zones\|joints\|props\|rails\|splines\|landmarks [--csv]` | world-space tables; `--list start`, `--kind DashRing`, `--family rail` filter |
| `course STAGE` | lap length, main-route pieces, branches, default laps |
| `spots STAGE --near X,Z` or `--near-landmark NAME` | flat, dry, roofless spots clear of walls; tune `--radius`, `--clear`, `--wall-gap`, `--max-slope`, `--footprint`, `--count` |
| `sweep STAGE --points X,Y,Z ...` or `--path-file F.csv` | height above ground along a path and the first collision it crosses |
| `render STAGE` (needs `--with pillow`) | top-down PNG, see below |
| `section STAGE X0 Z0 X1 Z1` (needs `--with pillow`) | vertical cut: floors, undersides, walls, sea, plus an optional path |
| `live` (needs `--with dolphin-memory-engine`) | each player's position and `at` report from a running game; `--watch 1`, `--capture NAME --shape sphere --r 5` |
| `stages [--detail]`, `ground-types` | the roster with scales and laps; every ground type's name, traction and sound row |

```bash
uv run python scripts/hsd/stage.py at city -366 115 -575
uv run python scripts/hsd/stage.py spots city --near-landmark lighthouse --radius 120
uv run --with pillow python scripts/hsd/stage.py render city --landmark "castle hall" --radius 250 --overlay props -o out/stage/castle.png
```

### Pictures

`render` writes `out/stage/<stem>-<layer>.png` unless `-o` says otherwise and prints the path;
read the PNG to see it. +X is right and +Z is down, with a labeled grid in world units, so a point
read off the picture is a valid query. Landmarks and the barrier loops are drawn by default.

- `--layer height` (default, hillshaded), `kind` (ground types with a legend), `layers` (how many
  floors are stacked at each spot).
- `--center X,Z --radius R`, `--landmark NAME --radius R` or `--window X0 Z0 X1 Z1` to crop.
- `--max-y Y` drops everything above Y, to see under the garden in the sky or a roof.
- `--overlay` (repeatable): a position list (`start`, `item`, `event`, `vehicle`, `item_area`,
  ...), `zones` (or `--zone-kind SuperJump`), `props`, `joints`, `walls`, `course`, `rails`,
  `cpu`, `conveyor`, `heavy`. `--labels` labels markers.
- `--mark X,Z,label` and `--path X,Y,Z ...` / `--path-file F.csv` (repeatable) draw a candidate
  placement or flight.

## Landmarks

`docs/data/stage-landmarks.csv` is the one table of named places: `stage,name,aliases,shape,x,y,z,
r,hx,hy,hz,source,note`. Shapes are `point`, `sphere` (r), `cylinder` (r, half-height hy) and
`box` (half extents hx, hy, hz), all centered on x, y, z in world units. `source` is `live`
(captured standing there in game), `stage` (read from the stage data: a zone, a prop's
collision, an authored position) or `derived` (computed, or a mod's chosen position checked
against the floor).

- Use the names players use, the way the checklist and the docs do. When a place has no name
  yet, ask the user what they call it rather than inventing one: an invented name looks like a
  known place to the next reader.
- Add a place when a task needed it and it was not there, by `live --capture` while the user
  stands on it, or by hand with a `stage` or `derived` source and a note saying what it marks.
- A coordinate a mod hardcodes for a place belongs in the table too, so the next task finds it.

## Frame traps

The tool handles all of these; a hand-written script must too.

- **Collision vertices are joint-local.** Each collision joint's slice sits in its own joint's
  frame. Only the terrain root's slice is "file vertex times stage scale"; houses, trees, the
  lighthouse and the star pole need their joint's world matrix from the terrain model. Reading
  raw vertices puts every prop near the origin.
- **The root's scale is replaced, not multiplied.** The terrain root's scale becomes the stage
  scale; its rotation and translation stay, and child matrices use HSD's parent-scale
  correction.
- **Positions and splines are scaled at load.** Authored positions and spline CVs are file units
  times the stage scale (City Trial 0.7). A coordinate copied from `explore.py grdata` or a hex
  dump is not a world coordinate.
- **The out-of-bounds box is not scaled.** It is compared with world positions as stored.
- **Ground rays are one-sided.** `Raycast_Ground` only hits floor triangles from the side they
  face, and only the floor category; the sea (ground type 29) is a floor and the barrier lid
  (type 30) a ceiling. `ground` lists both faces so a stacked floor or an underside is visible.
- **The rest pose.** Moving geometry (lifts, gates, the waterwheel's cars) is where the model's
  desc puts it, which is where the game bakes it at load.
- **Props.** `props` lists City Trial's placed props only; other stages place theirs from code the
  tool does not model, though their collision still loads.

## Checking a placement

1. `find` or `at` the place, and `spots` for open ground near it.
2. `render` with `--mark` (and `--path-file` for anything that moves) to see it in context, and
   `section` through it for heights.
3. `sweep` any path or flight: a path that crosses collision stops a machine against it.
4. Say in the result what was checked statically; ask the user to confirm in game, and
   `live --capture` what they report.

`scripts/authoring/check_cannon_flights.py` is a worked example: it replays the cannon event's
placement math for every row of its placement table and sweeps each flight.
