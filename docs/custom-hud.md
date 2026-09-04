# Custom HUD System

Every HUD element in the game is a JObj model tree hanging off a GObj registered on a HUD GX link and drawn by an orthographic camera. Building a custom HUD element follows the same pattern: pull a model out of an `IfAll*` archive, load it into a GObj on a HUD GX link, and drive its child JObjs per frame. Digits, gauges and labels are all frame-indexed material animations, so "setting a value" means setting an animation frame.

The only live consumer of this pattern in the repo is the Gourmet Race score display (`mods/custom_events/src/event_gourmet_race.c`).

## Architecture

A HUD element GObj carries:

| Field | Value |
|-------|-------|
| `entity_class` | 27, hardcoded by every creator |
| `p_link` | caller-supplied; per-player elements use `GAMEPLINK_HUD` (26), the pause stat chart uses `GAMEPLINK_PAUSEHUD` (27), which is what keeps it drawn while paused |
| `gx_link` / `gx_pri` | caller-supplied; commonly `GAMEGX_HUD` (21) with `gx_pri` 1; the indicator path uses `GAMEGX_HUDORTHO` (18) |
| `hsd_object` | the JObj tree (the visual model) |
| `userdata` | optional `HUDElementData`, attached separately by `3DHud_AddData`, not by the creator |
| proc | per-frame animation/position callback, priority 20 |
| `gx_cb` | one of the `3DHud_*` render wrappers, or plain `JObj_GX` |

`HUD_CreateMiscGObj` (0x801147dc) is the engine's internal creator, signature `(JOBJDesc *jobjdesc, int p_link, int gx_link, int gx_pri)`. It runs, in order:

1. `GObj_Create(27, p_link, 0)` - `entity_class` is the literal 27
2. `HSD_JObjLoadJoint(jobjdesc)` (0x8040afe8) builds the JObj tree
3. `GObj_AddObject(gobj, obj_kind, jobj)` - `obj_kind` is read from the SDA byte at `r13+0x1271` (0x805de351), not a literal
4. `GObj_AddGXLink(gobj, 3DHud_RenderIfVisible, gx_link, gx_pri)` - installs the visibility-gated callback at 0x8011500c
5. `JOBJ_SetUnkFlags(jobj, 0x28000000)` (0x80052fb8) - material flags

Two adjacent variants, `HUD_CreateMiscGObj2` (0x8011487c) and `HUD_CreateMiscGObj3` (0x80114904), have the same shape with different fixed parameters.

## Functions and Symbol Aliases

Several HUD symbols carry a `link.ld` alias that differs from the `GKYE01.map` name; where the two columns differ, both names refer to the same address.

| Callable name | `GKYE01.map` name | Address | Notes |
|---------------|-------------------|---------|-------|
| `HUD_CreateElement(int ply, JOBJDesc *j)` | `3DHud_CreatePlayerElement` | 0x80114ba4 | `GObj_Create(27,26,0)` + `GObj_AddGXLink(g, 3DHud_Render, 21, 1)` |
| `HUD_AddElementData(GOBJ *g, int kind, int a, int ply)` | `3DHud_AddData` | 0x80114e24 | `hud.h` names arg 2 "size" but it is `HUDKind` |
| `HUD_UpdateElement(JOBJ *j, int frame)` | `3DHud_UpdateElement` | 0x8011503c | sets the JObj animation frame |
| `HUD_GXLink(GOBJ *g, int pass)` | `3DHud_Render` | 0x80114f1c | per-player viewport/scissor + visibility GX callback |
| `JObj_GX(GOBJ *g, int pass)` | `GObj_RenderJObj` | 0x8042a258 | unconditional JObj render |
| `JObj_SetAllMOBJFlags` | `JOBJ_SetUnkFlags` | 0x80052fb8 | |
| `CObj_SetOrtho(COBJ *c, float top, float bottom, float left, float right)` | `HSD_CObjSetOrtho` | 0x80402f08 | |
| `Text_CreateCanvas` | `Text_CreateTextCanvas` | 0x8044f674 | |
| `CityHUD_CreateStatChart(int ply, int ply2)` | `City_CreateStatChart` | 0x80128bb8 | |
| `CityHUD_CreateStatBar(int ply, int ply2, int stat_kind)` | `City_CreateStatChartBar` | 0x80129154 | |

Named identically in both, and in `link.ld`: `CObjThink_Common` (0x8042a29c), `CObj_RenderGXLinks` (0x8042a0b4), `GObj_GetJObjIndex` (0x80055af0), `JObj_SetMtxDirtySub` (0x8040d92c), `JObj_GetWorldPosition` (0x80053f34), `Gm_GetPlyViewNum` (0x800092b4), `Gm_GetIfAllCityArchive` (0x80112050, map `3DHud_GetIfAll1cArchive`), `Gm_GetIfAllScreenArchive` (0x80112058, map `3DHud_GetIfAllScreenArchive`), `Gm_Get3dData` (0x80112044, map `3D_GetData`).

**Not in `link.ld`** - call these through a raw pointer cast: `HUD_CreateMiscGObj` (0x801147dc, map name has a trailing `?`), `HUD_SetVisible` (0x80114eec), `HUD_SetInvisible` (0x80114f04), `CityHUD_DestroyAllStatCharts` (0x801294a8), `3DHud_RenderIfVisible` (0x8011500c), `3DHud_CreateIndicatorGObjCustomGX` (0x801149a0), `JObj_AddSetAnim0_SetFrameAndRate` (0x80114d9c).

Symbols starting with a digit (`3DHud_*`, `3D_GetData`) are present in `GKYE01.map` but `scripts/kar.py sym` will not resolve them by name or address; grep the map directly for those.

## HUD Element Data

`HUDElementData` is the canonical `userdata` payload, defined in `externals/hoshi/include/hud.h` with a union of per-element-kind layouts. Two parts of it matter outside the engine's own procs:

- **`is_visible`**, bit 0x02 of the byte at +0x8. Both wrapper GX callbacks skip rendering when it is 0, so this bit is what makes a HUD element draw at all.
- **The `city_stat_bar` arm**, which caches child JObj pointers so the update proc doesn't re-walk the tree: `bar_j` at +0x24, `num_right_j` at +0x28, `num_left_j` at +0x2c, `sign_j` at +0x30. The `num_left_j` / `num_right_j` names encode struct storage order, **not** on-screen left/right position.

`HUDKind` (also in `hud.h`) is only consulted by element-specific update procs, so a custom element may pass any value it does not collide on.

### Attaching Data and Visibility

`HUD_AddElementData` / `3DHud_AddData` (0x80114e24):

1. `HSD_ObjAlloc` a `HUDElementData`, then `memset(p, 0, 0xe4)` - the struct is 228 bytes
2. `GObj_AddUserData(gobj, 27, destructor = 0x801151e8, p)`
3. stores `kind` (arg 2) as a full int at +0x4
4. packs arg 3 into the top nibble of byte +0x8 (mask 0xf0) and arg 4 into the 2-bit `ply` field (mask 0x0c)
5. sets `is_visible` to 1

Step 5 is the one that makes anything appear. `HUD_SetVisible` (0x80114eec) and `HUD_SetInvisible` (0x80114f04) toggle the bit afterwards.

## Rendering Pipeline

The game renders in two levels:

1. **Camera GObjs** - created via `GObj_InitCamera` (or `GOBJ_EZCreator` with `HSD_OBJKIND_COBJ`), each assigned a unique `gx_link` (64+). Their `cobj_links` field (u64 bitmask) selects which GX links they render.
2. **Renderable GObjs** - placed on a specific GX link (0-63) via `GObj_AddGXLink`, each with a `gx_cb` called during rendering.

Render loop:

1. For each camera GObj, `CObjThink_Common` (0x8042a29c) sets up projection from the camera's COBJ, then calls `CObj_RenderGXLinks(camera, pass_mask = 7)`.
2. `CObj_RenderGXLinks` (0x8042a0b4) iterates each set bit in the camera's `cobj_links`; for each GX link N it walks all objects on that link and calls `gobj->gx_cb(gobj, pass)`.
3. Pass 0 = OPA (opaque), pass 1 = XLU (translucent), pass 2 = additional.

`CObj_RenderGXLinks` does **not** look at the rendered object's `cobj_links` - only the camera's matters. `GObj_Create` zeroes `cobj_links` and nothing else writes it, so leave it at 0 on non-camera objects; setting it there has no effect.

### GX Callbacks

| Callback | Address | Used By | Behavior |
|----------|---------|---------|----------|
| `JObj_GX` (map: `GObj_RenderJObj`) | 0x8042a258 | `JObj_LoadSet_SetPri`, general | Unconditional: loads `gobj->hsd_object` as a JObj and renders it |
| `3DHud_RenderIfVisible` | 0x8011500c | `HUD_CreateMiscGObj` | Checks `is_visible`; if set, calls `JObj_GX` passing the **userdata** as the JObj arg |
| `3DHud_Render` / `HUD_GXLink` | 0x80114f1c | `HUD_CreateElement` | Checks `is_visible`, sets the per-player `GXSetScissor` viewport, calls `JObj_GX`, restores the full scissor |

All three end in `JObj_GX`. The wrappers add visibility gating and viewport management; using `JObj_GX` directly bypasses both, so the object renders unconditionally in whatever viewport the camera set.

### HUD GX Links

| GX Link | Enum | Usage |
|---------|------|-------|
| 18 | `GAMEGX_HUDORTHO` | Player numbers, icons, per-player HUD (`3DHud_CreateIndicatorGObjCustomGX`) |
| 19 | `GAMEGX_HUDMAP` | Minimap |
| 20 | `GAMEGX_HUDMAPDOTS` | Minimap dots |
| 21 | `GAMEGX_HUD` | Timer, stat bars, misc HUD (`HUD_CreateMiscGObj`, `HUD_CreateElement`) |

All four have active cameras during City Trial gameplay.

Link 18 is the safest existing link for custom elements loaded with `JObj_LoadSet_SetPri` / `JObj_GX`, since the game's own elements on that link already use custom GX callbacks rather than the `is_visible`-checking wrappers. Everything on link 21 comes through `HUD_CreateMiscGObj` and its wrapper, so a custom element there must either attach `HUD_AddElementData` or use `JObj_GX` directly.

### JOBJ_HIDDEN From Model Descriptors

`HSD_JObjLoadJoint` copies flags straight from the `JOBJDesc`, including `JOBJ_HIDDEN` (0x10). Some archive models ship hidden because the game's own code shows them later via `HUD_SetVisible`. `JObj_LoadSet_SetPri` with `is_hidden = 0` only skips *setting* the flag - it never *clears* one already present in the descriptor. Always `JObj_ClearFlagsAll(root, JOBJ_HIDDEN)` after loading a HUD model.

Beware also that `HUD_UpdateElement`'s animation pass can clear `JOBJ_HIDDEN` on joints the animation touches, so an element that must stay hidden has to be re-hidden on every update, not once at creation.

## HUD Coordinate Space

All HUD orthographic cameras share one projection: top 0.0, bottom -480.0, left 0.0, right 640.0. X runs 0 (left edge) to 640 (right edge); Y runs 0 (top edge) to -480 (bottom edge), **negative going down**; Z is 0 for flat elements. Top-left is `(0, 0, 0)`, bottom-right `(640, -480, 0)`, center `(320, -240, 0)`.

`CObj_SetOrtho` overrides a descriptor's projection to match; the HUD archive COBJDescs already use these values.

## JObj Fields Used by HUD Code

The runtime `JOBJ` is defined in `externals/hoshi/include/obj.h`. HUD code touches `flags`, `trans` (position), `scale`, and the `child` / `sibling` pointers when walking the tree.

Flags: `JOBJ_HIDDEN` = `1 << 4` (0x10), `JOBJ_OPA` = `1 << 18`, `JOBJ_XLU` = `1 << 19`. After writing `trans` or `scale`, call `JObj_SetMtxDirtySub(jobj)` (0x8040d92c) so the world matrix is recomputed - without it the change never reaches the screen.

## Loading a Model From an Archive

1. Get the archive handle: `HSD_Archive **arch = Gm_GetIfAllCityArchive();` (IfAll1c).
2. Fetch a model by public symbol: `JOBJSet **sets = Archive_GetPublicAddress(*arch, "ScInfPausegaugect_scene_models");`
3. Create the GObj, either through the engine's creator (which installs `3DHud_RenderIfVisible`, so nothing draws until `HUD_AddElementData` or `HUD_SetVisible` sets `is_visible`):
   ```c
   GOBJ *gobj = HUD_CreateMiscGObj_(sets[0]->jobj, GAMEPLINK_HUD, GAMEGX_HUD, 1);
   GObj_AddProc(gobj, MyUpdateCallback, 20);
   ```
   or through the hoshi helper `JObj_LoadSet_SetPri(is_hidden, set, anim_id, start_frame, p_link, gx_link, is_add_anim, proc, proc_pri)`, which uses plain `JObj_GX` and has no visibility gate.
4. Reach child JObjs by depth-first index: `GObj_GetJObjIndex(gobj, child_index)` (0x80055af0).

`HUD_UpdateElement(jobj, value)` (0x8011503c) runs `HSD_JObjReqAnimAllFlags(jobj, (float)value)`, then `JObj_SetAllAOBJRateByFlags(jobj, 0xffff, 1.0)`, then `HSD_JObjAnimAll(jobj)`. The digit models are texture-swap material animations, so frame N selects image N. The `ScInfPausegaugect` digit `TexAnim` carries 11 `ImageDesc` entries (16x15, format 0), enough for 0-9 plus one spare.

To discover what a loaded archive holds at runtime, walk `arch->header.nb_public`, reading each name at `arch->symbols + arch->public_info[i].symbol` and its data at `arch->data + arch->public_info[i].offset`.

## Archives

HSD archives (`.dat`) hold the JObj models, animations and textures. Naming is `IfAll{mode}{variant}.dat`, mode 1 = common HUD, 2 = City Trial, 3 = Top Ride; variant `c` = common, `1`/`2`/`4` = player count, `s` = scaled/secondary.

### IfAll1c - Common HUD

Accessed via `Gm_GetIfAllCityArchive()` (0x80112050). Loaded in all modes; the primary source for HUD elements. 41 symbols, all suffixed `_scene_models` unless noted.

| Symbol | Description |
|--------|-------------|
| **Copy Ability Icons** | |
| `ScInfAbility` | Base/none ability |
| `ScInfAbilitybmb` / `fir` / `frz` / `mik` / `ndl` | Bomb, Fire, Freeze, Mike, Needle |
| `ScInfAbilitypla` / `slp` / `swd` / `trn` / `whl` / `win` | Plasma, Sleep, Sword, Tornado, Wheel, Wing |
| **Timer & Lap** | |
| `ScInfTime` / `ScInfTime4` | Timer display (1-2P / 3-4P) |
| `ScInfLaptime` / `ScInfLaprec` / `ScInfTimeup` | Lap time, lap record, time up |
| **Race State** | |
| `ScInfReadygo` / `ScInfFinish` / `ScInfFinallap` | Ready/Go, Finish, Final lap |
| **Player Display** | |
| `ScInfPlynum` | Player number (P1/P2/P3/P4 via animation frame) |
| `ScInfPlicon` / `ScInfPlicond` / `ScInfPliconm` | Player character icon (normal / alt / mini) |
| **Minimap** | |
| `ScInfCitymap` / `ScInfCitymapevent` | City map, event overlay |
| `ScInfMapbase4` / `ScInfMapd4` / `ScInfMapkirby4` / `ScInfMapm4` | Map base, dots, Kirby markers, markers (4P) |
| `ScInfMmapply` / `ScInfMmapstart` | Minimap player marker, start point |
| **Keys/Collectibles** | |
| `ScInfKeyBom` / `ScInfKeyLever` / `ScInfKeyPla` | Key: bomb, lever, plasma |
| **Stat Bar** | |
| `ScInfPausegaugect` | Stat gauge bar (bar + 2 digits + sign) |
| `ScInfGpanel_scene_data` | Gauge panel (`scene_data`, not `scene_models`) |
| **Other** | |
| `ScInfEnemyAbility` | Enemy ability indicator |
| `ScInfReplay` / `ScInfWarningfall` / `ScInfWrongway` | Replay, falling warning, wrong way |

### IfAll1X - Per-Player-Count HUD

Accessed via `Gm_GetIfAllScreenArchive()` (0x80112058). Suffix `1`/`2`/`4` for player count; `s` variants are the scaled versions.

| Symbol Pattern | Description |
|----------------|-------------|
| `ScInfHp{N}` | HP bar |
| `ScInfSpeed{N}` / `ScInfSpeedd{N}` / `ScInfSpeedm{N}` | Speedometer (normal/detailed/mini) |
| `ScInfBoost{N}` / `ScInfBoostd{N}` | Boost gauge |
| `ScInfBoostbreak{N}` / `ScInfBoostcut{N}` | Boost break/cut indicators |
| `ScInfDamageE{N}` / `ScInfDamageP{N}` | Damage indicators (enemy/player) |
| `ScInfAbilityefx{N}` | Ability effect indicator |
| `ScInfPause{N}` / `ScInfPausect{N}` | Pause screen |
| `ScInfPausepmct{N}` | Stat chart background (used by `CityHUD_CreateStatChart`) |
| `ScInfRetire{N}` | Retire prompt |
| `ScInfRanking{N}` | Race ranking display |
| `ScInfDistance{N}` | Distance display |
| `ScInfLapcount{N}` | Lap counter |
| `ScInfCPGoal{N}` | Checkpoint/goal indicator |
| `ScInfMap{N}` / `ScInfMapbase` | Minimap (1P/4P variants) |
| `ScInfFrame{N}` / `ScInfGpos{N}` | Frame/position overlays (2P/4P) |
| `ScInfPlynm{N}` / `ScInfPlynmd{N}` / `ScInfPlynmm{N}` | Player name (2P/4P variants) |
| `ScInfResultfree` / `ScInfResulttime*` | Result screen elements (1P only) |
| `ScInfWalkkby{N}` | Walking Kirby animation |

### IfAll2c - City Trial In-Game Indicators

24 symbols.

| Symbol | Description |
|--------|-------------|
| `ScInfEventct` / `ScInfEvent4ct` | Event notification banner (1P / 4P) |
| `ScInfBoxct` | Box/item pickup indicator |
| `ScInfWarpstarct` | Warp star indicator |
| `ScInfIget{Stat}ct` / `ScInfIget{Stat}dwnct` | Floating "+1"/"-1" feedback on collecting a stat item, one pair per stat: `Accel`, `Maxsp`, `Attack`, `Defense`, `Turn`, `Flight`, `Charge`, `Wait` (weight), `Hp`, `All` |

### IfAll2X - City Trial Per-Player-Count

Stadium elements, 9 symbols each. Suffix `1`/`2`/`4`.

| Symbol Pattern | Description |
|----------------|-------------|
| `ScInfEventmark{N}ct` | Event marker |
| `ScInfSpDragoonA/B/C{N}` | Dragoon legendary machine pieces |
| `ScInfSpHydraA/B/C{N}` | Hydra legendary machine pieces |
| `ScInfSpEfx{N}` | Legendary piece set-complete flourish |
| `ScInfSpPos{N}` | Legendary piece anchor model |

### IfAll3c - Top Ride Common

`ScInfRound1` / `ScInfRound2` / `ScInfRound3` (round indicators) and `ScInfStarIcon`.

### IfAll3X - Top Ride Per-Player-Count

14 symbols each.

| Symbol Pattern | Description |
|----------------|-------------|
| `ScInfScoreAg{N}` / `ScInfScoreHj{N}` / `ScInfScorePs{N}` / `ScInfScorePt{N}` | Score displays (per stadium) |
| `ScInfLapPt{N}` / `ScInfLapTen{N}` | Lap counters |
| `ScInfHeight{N}` / `ScInfWidth{N}` | Height/width displays |
| `ScInfDmIcon{N}` / `ScInfDmIconPos{N}` | Damage icons |
| `ScInfOut{N}` / `ScInfSafe{N}` | Out/safe indicators |
| `ScInfSignalGo{N}` | Go signal |
| `ScInfStIcon{N}` | Star icon |

### IfAllTMP - Results Screen

`ScInfResult_scene_models` and `ScInfResultM2d_scene_models`.

## City Trial Stat Bar Internals

`CityHUD_CreateStatChart(ply, ply2)` (map `City_CreateStatChart`, 0x80128bb8):

1. Picks the background model by player count (`Gm_GetPlyViewNum` -> 1/2/4P slot) from `Game3dData` and loads it with `HUD_CreateMiscGObj(jobj, 0x1b, 0x15, 1)` - `p_link` 27 (`PAUSEHUD`), `gx_link` 21, `gx_pri` 1
2. Attaches `HUDElementData` via `3DHud_AddData(..., kind = HUDKIND_CITYSTATBG)`; the GObj's `entity_class` is still 27
3. Extracts world positions with `JObj_GetWorldPosition` (0x80053f34) from background child JObjs 1-9, the 9 stat slot positions
4. Stores those 9 `Vec3`s in the background's `HUDElementData` starting at +0x14
5. Adds a per-frame proc at priority 20; the bars themselves are created separately, one per stat kind 0-8

`City_CreateStatChartBar(ply, ply2, stat_kind)` (0x80129154):

1. Loads the gauge from IfAll1c (`ScInfPausegaugect_scene_models`) with the same `HUD_CreateMiscGObj` parameters
2. Attaches `HUDElementData` via `3DHud_AddData(..., kind = HUDKIND_CITYSTATBAR)` and stores `stat_kind` at +0x14
3. Caches child JObjs by depth-first index: 1 -> `bar_j`, 6 -> `sign_j`, 5 -> `num_left_j`, 4 -> `num_right_j`
4. Reads its slot position out of the background's stored positions (`background_data + 0x14 + stat_kind * 0xc`), writes it into the gauge root JObj `trans`, then marks the matrix dirty

The resulting GObjs are stored in `Game3dData.cityui_statchart_gobj[ply]` (0x63c) and `Game3dData.cityui_statbar_gobj[ply][stat_kind]` (0x650).

### ScInfPausegaugect JObj Hierarchy

Depth-first indices as returned by `GObj_GetJObjIndex`:

```
Root (index 0) - positioned at the stat slot location
+- Child 1: bar_j - gauge fill bar (animated by fill frame)
|  +- Child 2: bar sub-element
|  +- Child 3: bar sub-element
+- Child 4: num_right_j - digit JObj, sits visually right
+- Child 5: num_left_j  - digit JObj, sits visually left
+- Child 6: sign_j - minus sign
```

Indices 1, 4, 5 and 6 are all direct children of the root. A lone digit drawn on child 5 reads as centered, which is why a single-digit readout should drive child 5 rather than child 4.

### Value Display Logic

The vanilla proc reads `Patch_GetPlySavedValue(ply, stat_kind, 0)`, adds 2 for stat kinds 0-7 (kind 8 uses the raw value), then drives the bar with `HUD_UpdateElement(bar_j, value)` when the value is positive, hides `sign_j` unless the value is `<= -10`, and updates or hides the two digit JObjs on the `|value| < 10` vs `>= 10` split. The "+2" offset is why a fresh stat slot reads "2"; the bar only fills for positive values.

## Legendary Piece HUD

The row of collected Hydra and Dragoon pieces. `3DHud_IndexLegendaryPiecesSymbols` (0x8012b39c) indexes all eight symbols out of IfAll2X when the HUD file loads and caches them in `Game3dData`: `legendary_hud_pos` (0xdec, the anchor model), `legendary_hud_efx` (0xdf0, the set-complete flourish), `legendary_hud_hydra[3]` (0xdf4), `legendary_hud_dragoon[3]` (0xe00), and `legendary_hud_gobj[4]` (0xe0c, the live element per viewport).

The anchor model is a root plus six geometry-less joints in one row: joints 1-3 carry the Hydra icons and 4-6 the Dragoon ones. An icon is packed into the first free anchor of its half **in collection order**, so a slot says how many pieces of that set are held, not which piece is in it. A mod reading the display back therefore learns a count, not an identity.

## Timer Model (ScInfTime)

Two variants live in IfAll1c: `ScInfTime_scene_models` (1-2 player, cached at `Game3dData + 0xb4`) and `ScInfTime4_scene_models` (3-4 player, `Game3dData + 0xb8`).

`3DHud_CreateTimer` (0x80119218) picks between them on `Gm_GetPlyViewNum()`: `<= 1` takes the +0xb4 model, otherwise +0xb8. It then calls `HUD_CreateMiscGObj(jobj, p_link, 0x15, 1)` with `p_link` = 27 in the Air Ride Free Run path (major 4, `airride_mode` 2) and 26 elsewhere, and adds `3DHud_TimerUpdate` (0x80118f50) as a proc at priority 20.

The model has 6 digit JObjs at child indices 1-6, in MM:SS:FF order from the least significant: index 1 is frames/centiseconds ones, 2 is their tens, 3 and 4 are seconds, 5 and 6 are minutes. Each uses the same frame-based animation as the stat bar, so `HUD_UpdateElement(jobj, digit)` drives it. Repurposed as a numeric readout it gives 6 digits (max 999999); unused slots can be hidden with `JOBJ_HIDDEN`, but the model may carry visible separator geometry between digit pairs.

## Player Number Model (ScInfPlynum)

Cached at `Game3dData + 0xc0`, created by `3DHud_CreatePlyNum` (0x8011fd20). A single model whose animation frame selects "P1", "P2" and so on - a label, not a digit grid. Useful next to a score display, not as one.

## Building a Custom HUD Element

`ScInfPausegaugect_scene_models` from IfAll1c can be loaded on its own, without the `CityHUD_CreateStatChart` pipeline, giving a fill bar, a two-digit numeric display (0-99) and a sign indicator. Multiple instances of the same model load independently, each with its own JObj tree, and `GObj_Destroy(gobj)` is sufficient cleanup. As long as `CityHUD_CreateStatChart` is not called there is no conflict with the real stat display.

```c
HSD_Archive **arch = Gm_GetIfAllCityArchive();
JOBJSet **sets = Archive_GetPublicAddress(*arch, "ScInfPausegaugect_scene_models");

// JObj_GX callback, no visibility gate
GOBJ *gauge = JObj_LoadSet_SetPri(0, sets[0], 0, 0.0f,
                                  GAMEPLINK_HUD, GAMEGX_HUD, 1, NULL, 0);
JOBJ *root = gauge->hsd_object;
JObj_ClearFlagsAll(root, JOBJ_HIDDEN);   // the model ships hidden
root->trans = (Vec3){ x, y, 0 };
JObj_SetMtxDirtySub(root);

JOBJ *bar_j   = GObj_GetJObjIndex(gauge, 1);
JOBJ *right_j = GObj_GetJObjIndex(gauge, 4);
JOBJ *left_j  = GObj_GetJObjIndex(gauge, 5);
JOBJ *sign_j  = GObj_GetJObjIndex(gauge, 6);
```

Per update, drive the digits with `HUD_UpdateElement` and re-hide anything that must stay invisible, since the animation pass clears `JOBJ_HIDDEN`. For a 0-99 counter, put the ones digit on child 5 when the value is below 10 (it reads centered) and split across children 4 and 5 above that.

Other models worth reusing: `ScInfPlynum_scene_models` (player label), `ScInfPlicon_scene_models` (character icon), `ScInfTime_scene_models` (multi-digit readout).

### Dedicated Camera

Rather than sharing a game HUD link, a custom element can own an ortho camera on an unused GX link, which makes rendering independent of the game's camera management. The COBJDesc at 0x805096a0 is a vanilla ortho camera descriptor.

```c
#define CUSTOM_GX_LINK 23

GOBJ *cam = GOBJ_EZCreator(0, 0, 0,
                           0, 0,
                           HSD_OBJKIND_COBJ, (COBJDesc *)0x805096a0,
                           0, 0,
                           CObjThink_Common, 0, 5);
cam->cobj_links = (1ULL << CUSTOM_GX_LINK);
CObj_SetOrtho((COBJ *)cam->hsd_object, 0.0f, -480.0f, 0.0f, 640.0f);

GOBJ *hud = JObj_LoadSet_SetPri(0, sets[0], 0, 0.0f,
                                GAMEPLINK_HUD, CUSTOM_GX_LINK, 1, NULL, 0);
JObj_ClearFlagsAll(hud->hsd_object, JOBJ_HIDDEN);
```

`GObj_Destroy` on both the element and the camera is the full teardown.

The Gourmet Race score display (`mods/custom_events/src/event_gourmet_race.c`) uses exactly this shape: a camera on GX link 23 with `CObjThink_Common` as its `gx_cb`, `cobj_links = 1 << 23`, `CObj_SetOrtho(cobj, 0, -480, 0, 640)`, and per-player gauge + `ScInfPlynum` label GObjs loaded onto that link with `JObj_LoadSet_SetPri`. hoshi's `externals/hoshi/src/screen_cam.c` carries the same camera pattern commented out; its live screen-camera path goes through `Text_CreateCanvas` instead.

## Constraints

- The models are drawn with the colors baked into their MObjs. There is no runtime per-JObj material or color override in use anywhere in the repo, so per-player tinting would need new work at the MObj level.
- Screen positions of the game's own stat bars are only knowable indirectly, through the 9 slot positions the background model exposes to `City_CreateStatChart`. Custom elements placed by absolute coordinates can overlap them.
