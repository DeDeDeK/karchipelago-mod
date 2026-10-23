# Course Gating

Air Ride's nine courses and Top Ride's seven are each locked behind an unlock item - 870-878 for
Air Ride (`AirRideCourse` order) and 890-896 for Top Ride (`TopRideCourse` order) - setting a bit
in `APSave.airride_stage_unlocked_mask` or `topride_stage_unlocked_mask` and announcing
"Unlocked Course: <name>". The two course selects end up behaving alike: a locked course shows a
gray "no" icon and a blank preview, the cursor can rest on it, and confirming it is refused. The
random button needs at least one course unlocked, and only picks unlocked courses.

The two modes share nothing else. Air Ride has a vanilla per-course unlock check to repurpose;
Top Ride's course select assumes every course is open, so it is gated through the table the
screen draws from.

## Air Ride

Vanilla locks one course, Nebula Belt (8), behind a checklist reward.
`AirRide_CheckCourseUnlocked` (0x8000c0e0) checks only that course, and every caller wraps the
call in `if (stage_kind == 8)`. The mod replaces the function with a mask test - a course is
unlocked when its bit is set, and the random button (kind 9) when any bit is - and removes the
guard at each caller, turning `cmpwi rX, 8` / `bne` / `li r3, 8` into `mr r3, rX` / `nop` /
`nop` so every course reaches the check:

| Caller | Guard |
|---|---|
| `AirRideSelect_Init` (0x8003c114) | 0x8003c210-0x8003c218 |
| `AirRideSelect_StartRandomCourse` (0x8003b4e8) | 0x8003b520-0x8003b528 |
| `AirRide_RandomStageSelect` (0x8000dd4c) | 0x8000ddc4-0x8000ddcc |
| `gmLanMenu_RenderMainMenuUI` (0x80052028) | 0x80052070-0x80052080, a five-instruction guard |

Refusing the random button on an empty mask is what keeps `AirRide_RandomStageSelect` from
spinning with no candidate. The grid order differs from the course order - the table at
0x80496e60 is `{0, 5, 2, 3, 1, 4, 6, 7, 8, 9}`, position 9 the random button - but the check
works on the course kind, so the order does not matter to the gate.

## Top Ride

The course select is its own minor scene (major 5, minor 7), shared by Start Game, Free Run and
Time Attack, before the lobby. `TopRide_CourseSelectThink` (0x8003c8bc) moves the cursor and, on
a rising A, Start, L or R, launches the course under it. Everything the screen shows or launches
for a grid position goes through one table, `stc_topride_course_grid` (0x805d51a8): grid
position to value, `{0, 1, 2, 3, 4, 5, 6, 8}` in vanilla, where 0-6 are the courses and 8 the
random button. The value is also the animation frame of the position's icon and of the preview
panorama.

Frame 9 (`TOPRIDE_GRID_LOCKED`) is a state vanilla never uses: the icon animation draws a gray
circle-and-slash there (the art Air Ride uses for a locked course) and every panorama an empty
image. So writing 9 into the table is enough to draw a locked course. The mod wraps three of the
scene's callbacks:

- **Load** writes 9 for every position that is not selectable, then runs the vanilla init.
- **Think** (before `TopRide_CourseSelectThink`) compares the table with the live mask every
  frame and, for a position whose state changed while the screen is up, rewrites its table entry,
  its lap count, its icon frame and - under the cursor - the panorama.
- **Exit** restores the vanilla table. With course selection off, `TopRide_MinorExit`
  (0x8003eed8) picks a course through `TopRide_CourseSelectRandomInit` (0x8003c754) in place of
  this scene, which also maps through the table.

Five places in the scene test a grid value with `cmpwi 8` / `bne` before indexing per-course
data - the default laps and the Time Attack and Free Run best times, at load and on cursor move
(0x8003d150, 0x8003d20c, 0x8003d354, 0x8003cdbc, 0x8003cf04). Each `bne` becomes `blt`, so a
locked position takes the random button's path and shows no laps or time instead of indexing
past the seven-course tables.

**Launch.** A conditional hook at 0x8003ca78, the launch-button test, refuses a launch press on
an unselectable position with the error sound. It runs every frame, so it only acts on a launch
press; the prologue saves the launch and direction registers (`r7`, `r5`) that both exits need.

**Random pick.** Both `HSD_Randi(7)` course rolls - 0x8003c798 in
`TopRide_CourseSelectRandomInit` and 0x8003cac0 for A on the random button - are replaced with a
pick from courses that are both unlocked and not in the recent-use mask
(`topride_course_select.used_history_mask`). When every unlocked course has been used it clears
their used bits and starts over. The caller re-rolls until the pick reads unused, so the pick
always is; with nothing unlocked it returns course 0 and clears that course's used bit so the
caller cannot spin forever.
