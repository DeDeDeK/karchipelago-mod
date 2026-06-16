# Custom HUD System Research

Research into building custom HUD elements using the game's JOBJ-based rendering pipeline. Primary use case: real-time score display for the Gourmet Race event.

## Architecture Overview

The game renders HUD elements as JOBJ model hierarchies attached to GOBJs on the HUD render pipeline. Each HUD element is a GOBJ with:
- `entity_class = 27` (always hardcoded by the creators)
- `p_link` — passed in by the caller; the per-player indicator path uses `GAMEPLINK_HUD` (26), while the pause stat chart passes 27 (`GAMEPLINK_PAUSEHUD`)
- `gx_link` / `gx_pri` — passed in by the caller (commonly `GAMEGX_HUD` = 21, `gx_pri` = 1; the indicator path uses `GAMEGX_HUDORTHO` = 18)
- A JOBJ tree as `hsd_object` (the visual model)
- Optional `userdata` pointing to `HUDElementData` for state tracking (attached separately by `3DHud_AddData`, not by the creator)
- A per-frame proc (priority 20) for animation/position updates
- GX callback: one of the `3DHud_*` render wrappers (see the GX-callback table below)

> **Symbol-name note.** Several names below are the project's `link.ld` aliases; the `GKYE01.map` entry differs (and `HUD_CreateMiscGObj` is still a tentative `HUD_CreateMiscGObj?` in the map). Aliases vs map names: `HUD_CreateElement`/`HUD_GXLink`/`HUD_AddElementData`/`HUD_UpdateElement` = `3DHud_CreatePlayerElement`/`3DHud_Render`/`3DHud_AddData`/`3DHud_UpdateElement`; `JObj_GX` = `GObj_RenderJObj`; `CObj_SetOrtho` = `HSD_CObjSetOrtho`. `HUD_CreateMiscGObj` is **not** in `link.ld` (call it via a raw pointer cast as the gourmet code does — `HUD_CreateMiscGObj_`).

Created internally via `HUD_CreateMiscGObj` (0x801147dc, `HUD_CreateMiscGObj?` in the map) which calls, in order:
1. `GObj_Create(27, p_link, 0)` — entity_class hardcoded to 27; `p_link` is arg 1
2. `HSD_JObjLoadJoint(jobjdesc)` (`JObj_LoadJoint`, 0x8040afe8) — builds JOBJ tree from arg 0
3. `GObj_AddObject(gobj, obj_kind, jobj)` — `obj_kind` is read from an SDA global (`r13+0x1271`), not a literal
4. `GObj_AddGXLink(gobj, 3DHud_RenderIfVisible, gx_link, gx_pri)` — installs the visibility-gated callback `3DHud_RenderIfVisible` (0x8011500c); `gx_link`/`gx_pri` are args 2/3
5. `JObj_SetAllMOBJFlags(jobj, 0x28000000)` (`JOBJ_SetUnkFlags`, 0x80052fb8) — material flags

There are two adjacent variants, `HUD_CreateMiscGObj2` (0x8011487c) and `HUD_CreateMiscGObj3` (0x80114904), with the same shape but different fixed parameters.

## HUD Element Data Structure

```c
typedef struct HUDElementData
{
    int x0;            // 0x0
    HUDKind kind;      // 0x4
    u8 x8_80 : 1;      // 0x8
    u8 x8_40 : 1;
    u8 x8_20 : 1;
    u8 x8_10 : 1;
    u8 ply : 2;        // 0x8, player index
    u8 is_visible : 1; // 0x8
    u8 x8_01 : 1;
    int xc;            // 0xc
    int x10;           // 0x10
    union {
        struct { ... } speedometer;
        struct { ... } timer;
        struct { ... } hp_bar;
        struct {
            Vec3 pos[4];    // per-player positions
            JOBJ *j[4];     // per-player JOBJs
        } ply_hud;
        struct {
            int x14, x18, x1c;
            int ply;           // 0x20
            JOBJ *bar_j;       // 0x24 — gauge fill bar  (child index 1)
            JOBJ *num_right_j; // 0x28 — a digit JOBJ    (child index 4)
            JOBJ *num_left_j;  // 0x2c — a digit JOBJ    (child index 5)
            JOBJ *sign_j;      // 0x30 — minus sign       (child index 6)
        } city_stat_bar;
    };
} HUDElementData;
```

This mirrors the canonical layout in `externals/hoshi/include/hud.h` (the `city_stat_bar` union member). The child-index → field mapping above is the one used by `City_CreateStatChartBar`: index 1 → `bar_j` (0x24), index 4 → `num_right_j` (0x28), index 5 → `num_left_j` (0x2c), index 6 → `sign_j` (0x30). The field names encode struct storage order, **not** on-screen left/right position.

### HUDKind Values

| Value | Name | Description |
|-------|------|-------------|
| 1-2 | SPEEDOMETEROUT/IN | Speed gauge |
| 3 | TIMER | Race/event timer with digit JOBJs |
| 4 | PLYNUM | Player number display |
| 5 | PLICON | Player character icon |
| 12 | HPBAR | Health bar |
| 19 | HUDCAM | Camera HUD |
| 25 | ITEMINDICATOR | Current item display |
| 35 | PLYHUDPOS | Per-player viewport overlay |
| 64 | CITYSTATBAR | City Trial stat bar (bar + 2 digits + sign) |
| 66 | CITYSTATBG | Stat bar background |

Values mirror `HUDKind` in `externals/hoshi/include/hud.h`. **Discrepancy to be aware of:** the header names `HUDKIND_CITYSTATBAR = 64` (0x40), but `City_CreateStatChartBar` actually passes `0x43` (67) to `3DHud_AddData`, and the background passes `0x42` (66, = `CITYSTATBG`). So the bar's runtime `kind` is 67, not 64 — the header constant appears to be off by 3 for the bar entry. The `kind` field is only consulted by element-specific update procs, so this mismatch is cosmetic for custom HUD work but worth noting before relying on the enum.

## HUD Creation API

### Low-Level Functions

```c
GOBJ *HUD_CreateElement(int ply, JOBJDesc *j);                   // 0x80114ba4 (map: 3DHud_CreatePlayerElement)
void HUD_AddElementData(GOBJ *g, int kind, int ply, int ply2);   // 0x80114e24 (map: 3DHud_AddData) — hud.h prototype calls arg2 "size", but it is HUDKind
void HUD_UpdateElement(JOBJ *j, int frame);                       // 0x8011503c (map: 3DHud_UpdateElement) — set JOBJ animation frame
void HUD_GXLink(GOBJ *g, int pass);                              // 0x80114f1c (map: 3DHud_Render) — per-player viewport/scissor + visibility GX callback
void HUD_SetVisible(GOBJ *g);                                     // 0x80114eec — sets is_visible = 1
void HUD_SetInvisible(GOBJ *g);                                   // 0x80114f04 — sets is_visible = 0
```

These six are declared in `link.ld` (`HUD_*` aliases) except the visibility helpers, which are only in the map under their literal `HUD_SetVisible`/`HUD_SetInvisible` names.

### City Trial Stat Bar Functions

```c
void CityHUD_CreateStatChart(int ply, int ply2);                // 0x80128bb8 (map: City_CreateStatChart)
void City_CreateStatChartBar(int ply, int ply2, int stat_kind); // 0x80129154
void CityHUD_DestroyAllStatCharts(void);                        // 0x801294a8
```

(`hud.h` declares the bar creator as `CityHUD_CreateStatBar`, but the map / `link.ld` symbol at 0x80129154 is `City_CreateStatChartBar`.)

### Internal Functions

```c
GOBJ *HUD_CreateMiscGObj(JOBJDesc *j, int p_link, int gx_link, int gx_pri); // 0x801147dc (map: HUD_CreateMiscGObj?) — 4 args, entity_class hardcoded 27; NOT in link.ld
GOBJ *3DHud_CreatePlayerElement(int ply, JOBJDesc *j);  // 0x80114ba4 — GObj_Create(27,26,0); GObj_AddGXLink(g, 3DHud_Render, 21, 1)
void 3DHud_CreateIndicatorGObjCustomGX(int ply, JOBJDesc *j, void *gx_cb); // 0x801149a0 — GObj_Create(27,26,0); GObj_AddGXLink(g, gx_cb, 18, 1)
void 3DHud_AddData(GOBJ *gobj, int kind, int ply, int ply2); // 0x80114e24 — same as HUD_AddElementData
void 3DHud_Render(GOBJ *g, int pass);               // 0x80114f1c — per-player viewport/scissor + visibility GX callback
void 3DHud_RenderIfVisible(GOBJ *g, int pass);       // 0x8011500c — simple visibility-gated GX callback
void JObj_AddSetAnim0_SetFrameAndRate(JOBJ *j, JOBJSet **sets, float frame, float rate); // 0x80114d9c
HSD_Archive **3DHud_GetIfAll1cArchive(void);    // 0x80112050
HSD_Archive **3DHud_GetIfAllScreenArchive(void); // 0x80112058
```

## JOBJ Model Loading Pipeline

### From Archive to Screen

1. **Get archive handle:**
   ```c
   HSD_Archive **arch = Gm_GetIfAllCityArchive(); // IfAll1c archive
   ```

2. **Fetch model by symbol name:**
   ```c
   JOBJSet **sets = Archive_GetPublicAddress(*arch, "ScInfPausegaugect_scene_models");
   ```

3. **Create GOBJ with JOBJ (using game's internal pattern):**
   ```c
   // HUD_CreateMiscGObj(jobjdesc, p_link, gx_link, gx_pri) — 4 args
   GOBJ *gobj = HUD_CreateMiscGObj(sets[0]->jobj, GAMEPLINK_HUD, GAMEGX_HUD, 1); // p_link=26, gx_link=21, gx_pri=1
   GObj_AddProc(gobj, MyUpdateCallback, 20);               // priority 20
   ```
   (`HUD_CreateMiscGObj` is not in `link.ld`; declare it as a raw pointer cast, e.g. `static GOBJ *(*HUD_CreateMiscGObj)(JOBJDesc*,int,int,int) = (void *)0x801147dc;`. Note this installs the `3DHud_RenderIfVisible` callback, which renders nothing until `is_visible` is set — see "HUDElementData & Visibility" below. The gourmet ScoreHUD avoids this by using `JObj_LoadSet_SetPri` + `JObj_GX` instead.)

4. **Or using convenience helpers from inline.h:**
   ```c
   GOBJ *hud = JObj_LoadSet_SetPri(
       0,                 // is_hidden
       sets[0],           // JOBJSet
       0,                 // anim_id
       0.0f,              // start frame
       GAMEPLINK_HUD,     // p_link (26)
       GAMEGX_HUD,        // gx_link (21)
       1,                 // is_add_anim
       MyUpdateCallback,  // per-frame proc
       20                 // proc priority
   );
   ```

5. **Access child JOBJs by index:**
   ```c
   JOBJ *child = GObj_GetJObjIndex(gobj, child_index);
   ```

### Per-Frame Updates

```c
void MyUpdateCallback(GOBJ *g)
{
    JOBJ *root = g->hsd_object;

    // Set digit value (frame N = digit N)
    JOBJ *digit = GObj_GetJObjIndex(g, digit_child_index);
    HUD_UpdateElement(digit, value);  // internally: ReqAnimAll + AnimAll

    // Reposition root
    root->trans.X = new_x;
    root->trans.Y = new_y;
    JObj_SetMtxDirtySub(root);

    // Visibility
    JObj_SetFlagsAll(digit, JOBJ_HIDDEN);    // hide
    JObj_ClearFlagsAll(digit, JOBJ_HIDDEN);  // show
}
```

### Digit Animation

`HUD_UpdateElement(jobj, value)` works by:
1. `HSD_JObjReqAnimAllFlags(jobj, value_as_float)` — sets animation frame
2. `JObj_SetAllAOBJRateByFlags(jobj, 0xFFFF, 1.0)` — sets playback rate
3. `HSD_JObjAnimAll(jobj)` — runs animation tick

Frame N of the digit animation displays digit N (0-9).

## JOBJ Structure

```c
struct JOBJ {
    HSD_Obj object;     // 0x0
    JOBJ *sibling;      // 0x08
    JOBJ *parent;       // 0x0C
    JOBJ *child;        // 0x10
    int flags;          // 0x14 (JOBJ_HIDDEN, JOBJ_OPA, JOBJ_XLU, etc.)
    DOBJ *dobj;         // 0x18 (display objects — geometry/materials)
    Vec4 rot;           // 0x1C
    Vec3 scale;         // 0x2C
    Vec3 trans;         // 0x38 (position)
    Mtx rotMtx;         // 0x44 (computed world matrix)
    AOBJ *aobj;         // 0x7C (animation object)
    JOBJDesc *desc;     // 0x84
};
```

Key flags: `JOBJ_HIDDEN` (0x10, invisible), `JOBJ_OPA` (opaque), `JOBJ_XLU` (translucent).

## Archives

HSD archives (`.dat` files) contain JOBJ models, animations, textures, and other assets. Public symbols are accessed via `Archive_GetPublicAddress(archive, "symbol_name")`. Archives are organized by mode and player count.

Naming convention: `IfAll{mode}{variant}.dat` where mode = 1 (common HUD), 2 (City Trial), 3 (Top Ride), and variant = `c` (common), `1`/`2`/`4` (player count), `s` (scaled/secondary).

### IfAll1c — Common HUD (41 symbols)

Accessed via `Gm_GetIfAllCityArchive()`. Loaded in all modes. Primary source for HUD elements.

| Symbol | Description |
|--------|-------------|
| **Copy Ability Icons** | |
| `ScInfAbility_scene_models` | Base/none ability |
| `ScInfAbilitybmb_scene_models` | Bomb |
| `ScInfAbilityfir_scene_models` | Fire |
| `ScInfAbilityfrz_scene_models` | Freeze |
| `ScInfAbilitymik_scene_models` | Mike |
| `ScInfAbilityndl_scene_models` | Needle |
| `ScInfAbilitypla_scene_models` | Plasma |
| `ScInfAbilityslp_scene_models` | Sleep |
| `ScInfAbilityswd_scene_models` | Sword |
| `ScInfAbilitytrn_scene_models` | Tornado |
| `ScInfAbilitywhl_scene_models` | Wheel |
| `ScInfAbilitywin_scene_models` | Wing |
| **Timer & Lap** | |
| `ScInfTime_scene_models` | Timer display (1-2P) |
| `ScInfTime4_scene_models` | Timer display (3-4P) |
| `ScInfLaptime_scene_models` | Lap time |
| `ScInfLaprec_scene_models` | Lap record |
| `ScInfTimeup_scene_models` | Time up |
| **Race State** | |
| `ScInfReadygo_scene_models` | Ready/Go |
| `ScInfFinish_scene_models` | Finish |
| `ScInfFinallap_scene_models` | Final lap |
| **Player Display** | |
| `ScInfPlynum_scene_models` | Player number (P1/P2/P3/P4 via animation frame) |
| `ScInfPlicon_scene_models` | Player character icon |
| `ScInfPlicond_scene_models` | Player character icon (alt) |
| `ScInfPliconm_scene_models` | Player character icon (mini) |
| **Minimap** | |
| `ScInfCitymap_scene_models` | City map |
| `ScInfCitymapevent_scene_models` | City map event overlay |
| `ScInfMapbase4_scene_models` | Map base (4P) |
| `ScInfMapd4_scene_models` | Map dots (4P) |
| `ScInfMapkirby4_scene_models` | Map Kirby markers (4P) |
| `ScInfMapm4_scene_models` | Map markers (4P) |
| **Keys/Collectibles** | |
| `ScInfKeyBom_scene_models` | Key: bomb |
| `ScInfKeyLever_scene_models` | Key: lever |
| `ScInfKeyPla_scene_models` | Key: plasma |
| **Stat Bar** | |
| `ScInfPausegaugect_scene_models` | Stat gauge bar (bar + 2 digits + sign) |
| `ScInfGpanel_scene_data` | Gauge panel (note: `scene_data` not `scene_models`) |
| **Other** | |
| `ScInfEnemyAbility_scene_models` | Enemy ability indicator |
| `ScInfMmapply_scene_models` | Minimap player marker |
| `ScInfMmapstart_scene_models` | Minimap start point |
| `ScInfReplay_scene_models` | Replay |
| `ScInfWarningfall_scene_models` | Warning: falling |
| `ScInfWrongway_scene_models` | Wrong way |

### IfAll1X — Per-Player-Count HUD

Accessed via `Gm_GetIfAllScreenArchive()`. Suffix `1`/`2`/`4` for player count. Contains screen-scaled HUD elements. The `s` variants are secondary/scaled versions.

| Symbol Pattern | Description |
|----------------|-------------|
| `ScInfHp{N}` | HP bar |
| `ScInfSpeed{N}` / `ScInfSpeedd{N}` / `ScInfSpeedm{N}` | Speedometer (normal/detailed/mini) |
| `ScInfBoost{N}` / `ScInfBoostd{N}` | Boost gauge |
| `ScInfBoostbreak{N}` / `ScInfBoostcut{N}` | Boost break/cut indicators |
| `ScInfDamageE{N}` / `ScInfDamageP{N}` | Damage indicators (enemy/player) |
| `ScInfAbilityefx{N}` | Ability effect indicator |
| `ScInfPause{N}` / `ScInfPausect{N}` | Pause screen |
| `ScInfPausepmct{N}` | **Stat chart background** (used by `CityHUD_CreateStatChart`) |
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

### IfAll2c — City Trial Specific (24 symbols)

Contains City Trial in-game indicators. Likely loaded during CT gameplay.

| Symbol | Description |
|--------|-------------|
| `ScInfEventct_scene_models` | **Event notification banner** (1P) |
| `ScInfEvent4ct_scene_models` | Event notification banner (4P) |
| `ScInfBoxct_scene_models` | Box/item pickup indicator |
| `ScInfWarpstarct_scene_models` | Warp star indicator |
| **Stat Pickup Indicators** | Floating "+1" style feedback when collecting stat items |
| `ScInfIgetAccelct_scene_models` | Boost up |
| `ScInfIgetAcceldwnct_scene_models` | Boost down |
| `ScInfIgetMaxspct_scene_models` | Top speed up |
| `ScInfIgetMaxspdwnct_scene_models` | Top speed down |
| `ScInfIgetAttackct_scene_models` | Offense up |
| `ScInfIgetAttackdwnct_scene_models` | Offense down |
| `ScInfIgetDefensect_scene_models` | Defense up |
| `ScInfIgetDefensedwnct_scene_models` | Defense down |
| `ScInfIgetTurnct_scene_models` | Turn up |
| `ScInfIgetTurndwnct_scene_models` | Turn down |
| `ScInfIgetFlightct_scene_models` | Glide up |
| `ScInfIgetFlightdwnct_scene_models` | Glide down |
| `ScInfIgetChargect_scene_models` | Charge up |
| `ScInfIgetChargedwnct_scene_models` | Charge down |
| `ScInfIgetWaitct_scene_models` | Weight up |
| `ScInfIgetWaitdwnct_scene_models` | Weight down |
| `ScInfIgetHpct_scene_models` | HP up |
| `ScInfIgetHpdwnct_scene_models` | HP down |
| `ScInfIgetAllct_scene_models` | All stats up |
| `ScInfIgetAlldwnct_scene_models` | All stats down |

### IfAll2X — City Trial Per-Player-Count (9 symbols each)

Stadium-related elements. Suffix `1`/`2`/`4` for player count.

| Symbol Pattern | Description |
|----------------|-------------|
| `ScInfEventmark{N}ct` | Event marker |
| `ScInfSpDragoonA/B/C{N}` | Dragoon legendary machine pieces |
| `ScInfSpHydraA/B/C{N}` | Hydra legendary machine pieces |
| `ScInfSpEfx{N}` | Stadium special effect |
| `ScInfSpPos{N}` | Stadium position indicator |

### IfAll3c — Top Ride Common (4 symbols)

| Symbol | Description |
|--------|-------------|
| `ScInfRound1_scene_models` | Round 1 indicator |
| `ScInfRound2_scene_models` | Round 2 indicator |
| `ScInfRound3_scene_models` | Round 3 indicator |
| `ScInfStarIcon_scene_models` | Star icon |

### IfAll3X — Top Ride Per-Player-Count (14 symbols each)

| Symbol Pattern | Description |
|----------------|-------------|
| `ScInfScoreAg{N}` / `ScInfScoreHj{N}` / `ScInfScorePs{N}` / `ScInfScorePt{N}` | Score displays (per stadium) |
| `ScInfLapPt{N}` / `ScInfLapTen{N}` | Lap counters |
| `ScInfHeight{N}` / `ScInfWidth{N}` | Height/width displays |
| `ScInfDmIcon{N}` / `ScInfDmIconPos{N}` | Damage icons |
| `ScInfOut{N}` / `ScInfSafe{N}` | Out/safe indicators |
| `ScInfSignalGo{N}` | Go signal |
| `ScInfStIcon{N}` | Star icon |

### IfAllTMP — Results Screen (2 symbols)

| Symbol | Description |
|--------|-------------|
| `ScInfResult_scene_models` | Results display |
| `ScInfResultM2d_scene_models` | Results (2D variant) |

### Enumerating Symbols at Runtime

Public symbols can be enumerated from an `HSD_Archive`:

```c
HSD_Archive *arch = *Gm_GetIfAllCityArchive();
for (u32 i = 0; i < arch->header.nb_public; i++)
{
    char *name = arch->symbols + arch->public_info[i].symbol;
    void *addr = arch->data + arch->public_info[i].offset;
    OSReport("  [%d] %s = %p\n", i, name, addr);
}
```

## City Trial Stat Bar — Detailed Internals

### Creation Flow

`CityHUD_CreateStatChart(ply, ply2)` (map: `City_CreateStatChart`, 0x80128bb8):
1. Picks the background model by player count (`Gm_GetPlyViewNum` → 1/2/4P slot) from `Game3dData` and loads it via `HUD_CreateMiscGObj(jobj, 0x1b, 0x15, 1)` — i.e. **p_link = 0x1b (27, PAUSEHUD)**, gx_link = 0x15 (21), gx_pri = 1
2. Attaches `HUDElementData` via `3DHud_AddData(..., kind = 0x42)` — `0x42` is the **HUDKind** (`CITYSTATBG`), not an entity id; the GOBJ's `entity_class` is always 27
3. Extracts world positions (`JOBJ_GetWorldPosition`) from background child JOBJs **1-9** (the 9 stat slot positions)
4. Stores those positions in the background's `HUDElementData` at +0x14, +0x20, +0x2c … +0x74 (9 `Vec3`s)
5. Adds a per-frame proc at priority 0x14 (20); the bars themselves are created separately by `City_CreateStatChartBar` for each stat kind 0-8

`City_CreateStatChartBar(ply, ply2, stat_kind)` (0x80129154):
1. Loads gauge from **IfAll1c** (`ScInfPausegaugect_scene_models`) via `HUD_CreateMiscGObj(jobj, 0x1b, 0x15, 1)`
2. Attaches `HUDElementData` via `3DHud_AddData(..., kind = 0x43)` (67) and stores `stat_kind` at struct +0x14
3. Caches JOBJ children by depth-first index into the `HUDElementData`:
   - **Index 1** → +0x24 `bar_j` (gauge fill bar)
   - **Index 6** → +0x30 `sign_j` (minus sign indicator)
   - **Index 5** → +0x2c `num_left_j` (a digit JOBJ)
   - **Index 4** → +0x28 `num_right_j` (a digit JOBJ)
4. Reads the slot position out of the background's stored positions (`background->HUDElementData + 0x14 + stat_kind*0xc`) and writes it into the gauge root JOBJ at offsets 0x38/0x3C/0x40 (`trans`), then `HSD_JObjSetMtxDirtySub`

### JOBJ Hierarchy (ScInfPausegaugect)

Depth-first traversal indices from `GObj_GetJObjIndex`:

```
Root (index 0) — positioned at stat slot location
├── Child 1: bar_j — gauge fill bar (animated by fill frame)
│   ├── Child 2: bar sub-element
│   └── Child 3: bar sub-element
├── Child 4: num_right_j — a digit JOBJ (struct +0x28)
├── Child 5: num_left_j  — a digit JOBJ (struct +0x2c)
└── Child 6: sign_j — minus sign (struct +0x30)
```

**Index 4 and 5 are the two digit JOBJs; index 6 is the sign.** The `HUDElementData` field names `num_left_j`/`num_right_j` reflect their storage order in the struct, not their visual position — the gourmet ScoreHUD maps child 5 → "left/tens" and child 4 → "right/ones" (see `event_gourmet_race.c`). All children are siblings at the root level (parent = root).

### Value Display Logic

```
value = Patch_GetPlySavedValue(ply, stat_kind, 0)   // current stat value
value += 2  (for stat_kind 0-7; stat_kind 8 uses the raw value)
bar:   HUD_UpdateElement(bar_j, value) when value > 0
sign:  hidden (JOBJ_HIDDEN) unless value <= -10 (a leading minus only for large negatives)
digits: the two digit JOBJs (index 4/5) are updated/hidden per the |value| < 10 vs >= 10 split
```

(The "+2" offset is why a fresh stat slot reads "2"; the bar is only filled for positive values.)

### Storage in Game3dData

- Background GOBJs: `Game3dData + 0x63C + ply2 * 4` (decompile: `0x630 + (ply2 + 3) * 4`)
- Per-stat bar GOBJs: `Game3dData + 0x650 + ply2 * 36 + stat_kind * 4` (decompile: `0x64c + (ply2*9 + stat_kind + 1) * 4`)

## Building a Custom HUD Element

### Approach: Reuse ScInfPausegaugect

The stat gauge model (`ScInfPausegaugect_scene_models`) from IfAll1c can be loaded independently without going through the full `CityHUD_CreateStatChart` pipeline. This gives us:
- A fill bar (animatable)
- Two-digit numeric display (0-99)
- A sign indicator

We can create our own GOBJ, load this model, position it wherever we want, and update the digits per-frame with our score values.

### Minimal Creation Pattern

This is exactly what the gourmet ScoreHUD does, except it loads the model with `JObj_LoadSet_SetPri` (so the GX callback is plain `JObj_GX`, with no `is_visible` gating to worry about) rather than `HUD_CreateMiscGObj`:

```c
// Get archive
HSD_Archive **arch = Gm_GetIfAllCityArchive();
JOBJSet **sets = Archive_GetPublicAddress(*arch, "ScInfPausegaugect_scene_models");

// Create GOBJ with JOBJ on a HUD GX link (JObj_GX callback, no visibility gate)
GOBJ *gauge = JObj_LoadSet_SetPri(0, sets[0], 0, 0.0f,
                                  GAMEPLINK_HUD, GAMEGX_HUD, 1, NULL, 0);
JOBJ *root = gauge->hsd_object;
JObj_ClearFlagsAll(root, JOBJ_HIDDEN);   // model may ship hidden

// Cache child JOBJs (depth-first indices; same as City_CreateStatChartBar)
JOBJ *bar_j  = GObj_GetJObjIndex(gauge, 1);  // fill bar
JOBJ *tens_j = GObj_GetJObjIndex(gauge, 4);  // digit (used as tens here)
JOBJ *ones_j = GObj_GetJObjIndex(gauge, 5);  // digit (used as ones here)
JOBJ *sign_j = GObj_GetJObjIndex(gauge, 6);  // minus sign

// Position on screen
root->trans = (Vec3){ x, y, 0 };
JObj_SetMtxDirtySub(root);

// Per-frame update (the gourmet code drives this from its watcher proc, not GObj_AddProc)
void ScoreHUD_Update(GOBJ *g)
{
    int score = GetPlayerScore();
    HUD_UpdateElement(ones_j, score % 10);
    if (score >= 10)
    {
        JObj_ClearFlagsAll(tens_j, JOBJ_HIDDEN);
        HUD_UpdateElement(tens_j, score / 10);
    }
    else
        JObj_SetFlagsAll(tens_j, JOBJ_HIDDEN);
    sign_j->flags |= JOBJ_HIDDEN;  // never show minus
    bar_j->flags  |= JOBJ_HIDDEN;  // re-hide each frame in case AnimAll clears it
}
```

> The exact "which digit is left vs right" is a cosmetic choice; what matters is that **indices 4 and 5 are the digit JOBJs and index 6 is the sign**. See `event_gourmet_race.c` for the live mapping.

### Other Reusable Elements

| Symbol | Use Case |
|--------|----------|
| `ScInfPlynum_scene_models` | Player number label (P1, P2, etc.) |
| `ScInfPlicon_scene_models` | Player character icon |
| `ScInfTime_scene_models` | Timer display (multi-digit) |

### Considerations

- Multiple instances of the same model can be loaded independently — each gets its own JOBJ tree
- Positioning is in the HUD camera's coordinate space (orthographic)
- No conflict with the real stat display as long as we don't use `CityHUD_CreateStatChart`
- Cleanup: `GObj_Destroy(gauge)` is sufficient
- The gauge model includes a fill bar which could be repurposed (e.g., show time remaining)

## HUD Coordinate Space

All HUD orthographic cameras use the same projection:

| Parameter | Value |
|-----------|-------|
| **left** | 0.0 |
| **right** | 640.0 |
| **top** | 0.0 |
| **bottom** | -480.0 |

- **X**: 0 (left edge) to 640 (right edge)
- **Y**: 0 (top edge) to -480 (bottom edge) — **Y is negative going down**
- **Z**: 0 for flat HUD elements

Examples:
- Top-left corner: `(0, 0, 0)`
- Bottom-right corner: `(640, -480, 0)`
- Screen center: `(320, -240, 0)`

The gourmet ScoreHUD and hoshi's screen-cam code both call `CObj_SetOrtho(cobj, 0.0, -480.0, 0.0, 640.0)` (the cobj is the first arg; the four floats are top, bottom, left, right). The map symbol is `HSD_CObjSetOrtho` (0x80402f08); `Text_CreateCanvas` / `Text_CreateTextCanvas` are the same symbol (0x8044f674, map name `Text_CreateTextCanvas`). The HUD archive COBJDescs use matching projection parameters.

## Timer Model Hierarchy (ScInfTime)

Two variants loaded from IfAll1c:
- `ScInfTime_scene_models` — 1-2 player (stored at Game3dData +0xB4)
- `ScInfTime4_scene_models` — 3-4 player (stored at Game3dData +0xB8)

Selected at runtime by `3DHud_CreateTimer` (0x80119218) based on `Gm_GetPlyViewNum()` (human-player count): `< 2` → the +0xB4 (1-2P) model, else the +0xB8 (3-4P) model. It is then created via `HUD_CreateMiscGObj(jobj, p_link, 0x15, 1)` (p_link 0x1a/0x1b depending on scene) with a per-frame proc at priority 20.

### JOBJ Children: 6 Digit Slots

The timer model has at least 7 children (index 0-6), with **6 digit JOBJs** at indices 1-6:

| Child Index | HUDElementData Offset | Display |
|-------------|----------------------|---------|
| 1 | +0x2C | Frames/centiseconds ones |
| 2 | +0x30 | Frames/centiseconds tens |
| 3 | +0x34 | Seconds ones |
| 4 | +0x38 | Seconds tens |
| 5 | +0x3C | Minutes ones |
| 6 | +0x40 | Minutes tens |

Format: **MM:SS:FF** (3 pairs of 2 digits). Each digit JOBJ uses frame-based animation (frame N = digit N, 0-9), updated via `3DHud_UpdateElement`.

### Score Display Potential

The timer model has 6 digit slots — more than enough for score display. For a 3-digit score, use 3 of the 6 slots and hide the rest with `JOBJ_HIDDEN`. Maximum displayable: 999999.

The digit animation is identical to the stat bar — `HUD_UpdateElement(jobj, digit_value)` works the same way.

## Player Number Model (ScInfPlynum)

Stored at Game3dData +0xC0. Created by `3DHud_CreatePlyNum` (0x8011fd20).

Uses **animation frame selection on a single model** to display "P1", "P2", etc. (frame = player index). Not suitable for numeric score display — it's a label, not a digit grid.

Could be useful as a **player label** next to a score display.

## Design Options for Gourmet Race HUD

### Option 1: Stat Gauge Per Player
Load `ScInfPausegaugect_scene_models` once per player. Each gets a 2-digit score (0-99) with fill bar. Position manually in the HUD coordinate space.

**Pros:** Compact, game-native look, fill bar could show relative score.
**Cons:** Limited to 0-99.

### Option 2: Timer Model as Score Display
Load `ScInfTime_scene_models` once per player. Use 2-3 of the 6 digit slots for score, hide the rest.

**Pros:** Supports 3+ digits, same visual style as game timer.
**Cons:** May include colon separators between digit pairs that would look odd for a score.

### Option 3: Hybrid — Gauge + Player Label
Load gauge per player + `ScInfPlynum` label next to each. The gauge shows score, the label shows which player.

**Pros:** Clean presentation, reuses existing assets.
**Cons:** More GOBJs to manage.

### Option 4: Text System Overlay
Use `Hoshi_CreateScreenText()` for score display. Simpler but visually basic.

**Pros:** Easiest to implement, supports arbitrary text.
**Cons:** Doesn't match game visual style.

## Rendering Pipeline Internals

### GX Link / Camera Architecture

The game uses a two-level rendering system:

1. **Camera GOBJs** — Created via `GObj_InitCamera`, each assigned a unique `gx_link` (64+). Their `cobj_links` field (u64 bitmask) determines which GX links they render.
2. **Renderable GOBJs** — Placed on a specific GX link (0–63) via `GObj_AddGXLink`. Each has a `gx_cb` function pointer called during rendering.

**Render loop** (`CObj_GX` → `GObjGXLink`):
1. For each camera GOBJ, `CObj_GX` (0x8042a29c, map: **`CObjThink_Common`** — accessed as a raw pointer cast, not in `link.ld`) sets up projection via the camera's CObj, then calls `GObjGXLink(camera, pass_mask=7)`.
2. `GObjGXLink` (0x8042a0b4, map: **`CObj_RenderGXLinks`**) iterates each set bit in the camera's `cobj_links`. For each GX link N, it walks all objects on that link and calls `gobj->gx_cb(gobj, pass)`.
3. Pass 0 = OPA (opaque), pass 1 = XLU (translucent), pass 2 = additional.

(`CObj_GX` / `GObjGXLink` are descriptive names used in this doc; the matching `GKYE01.map` symbols are `CObjThink_Common` / `CObj_RenderGXLinks`.)

`GObjGXLink` does **NOT** check the rendered object's `cobj_links` field. Only the camera's `cobj_links` matters. Setting `cobj_links` on a non-camera GOBJ has no effect on rendering.

### `cobj_links` Initialization

`GObj_Create` initializes `cobj_links = 0` for all new GOBJs. Neither `GObj_AddGXLink` nor any other standard setup function modifies it. For non-camera objects, leave it at the default 0.

### GX Callbacks

| Callback | Address | Used By | Behavior |
|----------|---------|---------|----------|
| `JObj_GX` (map: `GObj_RenderJObj`) | 0x8042a258 | `JObj_LoadSet_SetPri`, general | Unconditional: loads `gobj->hsd_object` (a JOBJ) and renders it |
| `3DHud_RenderIfVisible` | 0x8011500c | `HUD_CreateMiscGObj` | Checks `is_visible` (bit 0x02 of `userdata[0x8]`); if set, calls `JObj_GX` passing the **userdata** as the JOBJ arg |
| `3DHud_Render` / `HUD_GXLink` | 0x80114f1c | `HUD_CreateElement` (`3DHud_CreatePlayerElement`) | Checks `is_visible`, sets per-player `GXSetScissor` viewport, calls `JObj_GX`, restores the full scissor |

All three ultimately call `JObj_GX`. The wrappers add visibility gating and viewport management. Using `JObj_GX` directly bypasses both — objects render unconditionally in whatever viewport the camera sets.

### HUD GX Links

| GX Link | Enum | Camera | Usage |
|---------|------|--------|-------|
| 18 | `GAMEGX_HUDORTHO` | Active during gameplay | Player numbers, icons, per-player HUD (`3DHud_CreateIndicatorGObjCustomGX`) |
| 19 | `GAMEGX_HUDMAP` | Active during gameplay | Minimap |
| 20 | `GAMEGX_HUDMAPDOTS` | Active during gameplay | Minimap dots |
| 21 | `GAMEGX_HUD` | Active during gameplay | Timer, stat bars, misc HUD (`HUD_CreateMiscGObj`, `HUD_CreateElement`) |

All four have active cameras during City Trial gameplay. The timer (GX link 21) and player numbers (GX link 18) are visible.

**GX link 18 (`GAMEGX_HUDORTHO`)** is the safest choice for custom HUD elements created with `JObj_LoadSet_SetPri` / `JObj_GX`, since the game's own per-player elements use this link with custom GX callbacks (not the `is_visible`-checking wrappers).

**GX link 21 (`GAMEGX_HUD`)** is used by elements created through `HUD_CreateMiscGObj` which all use the `is_visible`-checking wrapper. Elements on this link work but should either use the wrapper callback with `HUD_AddElementData`, or use `JObj_GX` directly.

### HUDElementData & Visibility

`HUD_AddElementData` / `3DHud_AddData` (0x80114e24) allocates a 228-byte (`0xe4`) `HUDElementData` struct via `HSD_ObjAlloc`, zeros it (`memset(p, 0, 0xe4)`), attaches it as `gobj->userdata` (`GObj_AddUserData`), and packs `kind`/`ply` into byte +0x8 along with **`is_visible = 1`** (the 0x02 bit). This is required for the wrapper GX callbacks (`3DHud_RenderIfVisible` at 0x8011500c, `3DHud_Render` at 0x80114f1c) which skip rendering when that bit is 0.

`HUD_AddElementData` signature (hud.h declaration says `size` but the second arg is actually `kind`):
```c
void HUD_AddElementData(GOBJ *g, int kind, int ply, int ply2);
```

Visibility helpers (small functions adjacent to `HUD_AddElementData`):
- `HUD_SetVisible` (0x80114eec) — sets `is_visible = 1`
- `HUD_SetInvisible` (0x80114f04) — sets `is_visible = 0`

### JOBJ_HIDDEN from Model Descriptors

`JObj_LoadJoint` copies flags from the `JOBJDesc`, including `JOBJ_HIDDEN`. Some archive models (e.g., `ScInfPausegaugect`) may have `JOBJ_HIDDEN` set in their descriptor since they're designed to start invisible (the stat chart system explicitly shows them later via `HUD_SetVisible`).

`JObj_LoadSet_SetPri` with `is_hidden=0` only skips *setting* the flag — it never *clears* a flag already present in the descriptor. Always call `JObj_ClearFlagsAll(root, JOBJ_HIDDEN)` after loading HUD models to ensure visibility.

### Creating a Custom Camera

If the game's built-in HUD cameras don't work for custom elements, create a dedicated ortho camera on a custom GX link. This guarantees rendering independent of the game's camera management.

```c
#define CUSTOM_GX_LINK 22  // pick an unused link

// CObj_GX is the standard camera render callback (not in link.ld; map name: CObjThink_Common)
static void (*CObj_GX)(GOBJ *g, int pass) = (void *)0x8042a29c;

// Create camera using vanilla COBJDesc at 0x805096a0
GOBJ *cam = GOBJ_EZCreator(0, 0, 0,
                            0, 0,
                            HSD_OBJKIND_COBJ, (COBJDesc *)0x805096a0,
                            0, 0,
                            CObj_GX, 0, 5);
cam->cobj_links = (1ULL << CUSTOM_GX_LINK);

// Set ortho projection matching HUD coordinate space
COBJ *cobj = cam->hsd_object;
CObj_SetOrtho(cobj, 0.0f, -480.0f, 0.0f, 640.0f);

// Create HUD objects on the custom link, clearing JOBJ_HIDDEN
GOBJ *hud = JObj_LoadSet_SetPri(
    0, sets[0], 0, 0.0f,
    GAMEPLINK_HUD, CUSTOM_GX_LINK, 1, NULL, 0);
JObj_ClearFlagsAll(hud->hsd_object, JOBJ_HIDDEN);

// Cleanup: destroy camera + HUD objects
GObj_Destroy(hud);
GObj_Destroy(cam);
```

The COBJDesc at 0x805096a0 is a vanilla ortho camera descriptor. The gourmet ScoreHUD uses it live (`event_gourmet_race.c`); hoshi's `screen_cam.c` has the same pattern but it is **commented out** there — the working hoshi screen camera path goes through `Text_CreateCanvas` instead. `CObj_SetOrtho` (0x80402f08, map: `HSD_CObjSetOrtho`) overrides the descriptor's projection to match the standard HUD coordinate space. Note the gourmet camera is created with `CObj_GX` / `CObjThink_Common` (0x8042a29c) as its gx_cb, and `cobj_links` is set to a bitmask of the custom GX links (22 + 23) it drives.

## Open Questions

- Can we change the color/material of individual JOBJs at runtime (e.g., different colors per player)?
- Does the timer model have visible colon/separator geometry between digit pairs?
- What is the exact screen position of existing stat bars? (Would help avoid overlap.)
- Can we load models from other archives (not just IfAll1c) for more visual variety?
