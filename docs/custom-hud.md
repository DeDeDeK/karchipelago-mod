# Custom HUD System

Every HUD element in the game is a JObj model tree hanging off a GObj registered on a HUD GX link and drawn by an orthographic camera. Building a custom HUD element follows the same pattern: pull a model out of an `IfAll*` archive, load it into a GObj on a HUD GX link, and drive its child JObjs per frame. Digits, gauges and labels are all frame-indexed material animations, so "setting a value" means setting an animation frame.

## Architecture

A HUD element GObj carries:

| Field | Value |
|-------|-------|
| `entity_class` | 27, hardcoded by every creator |
| `p_link` | caller-supplied; per-player elements use `GAMEPLINK_HUD` (26), the pause stat chart uses `GAMEPLINK_PAUSEHUD` (27) |
| `gx_link` / `gx_pri` | caller-supplied; commonly `GAMEGX_HUD` (21) with `gx_pri` 1; the indicator path uses `GAMEGX_HUDORTHO` (18) |
| `hsd_object` | the JObj tree (the visual model) |
| `userdata` | optional `HUDElementData`, attached separately by `3DHud_AddData`, not by the creator |
| proc | per-frame animation/position callback, priority 20 |
| `gx_cb` | one of the `3DHud_*` render wrappers, or plain `JObj_GX` |

`HUD_CreateMiscGObj` (0x801147dc) is the engine's internal creator, signature `(JOBJDesc *jobjdesc, int p_link, int gx_link, int gx_pri)`. It runs, in order:

1. `GObj_Create(27, p_link, 0)` — `entity_class` is the literal 27
2. `HSD_JObjLoadJoint(jobjdesc)` (`JObj_LoadJoint`, 0x8040afe8) builds the JObj tree
3. `GObj_AddObject(gobj, obj_kind, jobj)` — `obj_kind` is read from the SDA global at `r13+0x1271`, not a literal
4. `GObj_AddGXLink(gobj, 3DHud_RenderIfVisible, gx_link, gx_pri)` — installs the visibility-gated callback at 0x8011500c
5. `JObj_SetAllMOBJFlags(jobj, 0x28000000)` (0x80052fb8) — material flags

Two adjacent variants, `HUD_CreateMiscGObj2` (0x8011487c) and `HUD_CreateMiscGObj3` (0x80114904), have the same shape with different fixed parameters.

### Symbol Aliases

Several HUD symbols carry a `link.ld` alias that differs from the `GKYE01.map` name.

| `link.ld` alias | `GKYE01.map` name | Address |
|-----------------|-------------------|---------|
| `HUD_CreateElement` | `3DHud_CreatePlayerElement` | 0x80114ba4 |
| `HUD_GXLink` | `3DHud_Render` | 0x80114f1c |
| `HUD_AddElementData` | `3DHud_AddData` | 0x80114e24 |
| `HUD_UpdateElement` | `3DHud_UpdateElement` | 0x8011503c |
| `CityHUD_CreateStatChart` | `City_CreateStatChart` | 0x80128bb8 |
| `CityHUD_CreateStatBar` | `City_CreateStatChartBar` | 0x80129154 |
| `JObj_GX` | `GObj_RenderJObj` | 0x8042a258 |
| `JObj_SetAllMOBJFlags` | `JOBJ_SetUnkFlags` | 0x80052fb8 |
| `CObj_SetOrtho` | `HSD_CObjSetOrtho` | 0x80402f08 |
| `Text_CreateCanvas` | `Text_CreateTextCanvas` | 0x8044f674 |

`HUD_CreateMiscGObj` (map: `HUD_CreateMiscGObj?`), `HUD_SetVisible`, `HUD_SetInvisible` and `CityHUD_DestroyAllStatCharts` are **not** in `link.ld`. Call `HUD_CreateMiscGObj` through a raw pointer cast:

```c
static GOBJ *(*HUD_CreateMiscGObj_)(JOBJDesc *, int, int, int) = (void *)0x801147dc;
```

## Key Functions

```c
GOBJ *HUD_CreateElement(int ply, JOBJDesc *j);                  // 0x80114ba4
void  HUD_AddElementData(GOBJ *g, int kind, int ply, int ply2); // 0x80114e24, hud.h names arg2 "size" but it is HUDKind
void  HUD_UpdateElement(JOBJ *j, int frame);                    // 0x8011503c, set JObj animation frame
void  HUD_GXLink(GOBJ *g, int pass);                            // 0x80114f1c, per-player viewport/scissor + visibility GX callback
void  HUD_SetVisible(GOBJ *g);                                  // 0x80114eec, sets is_visible = 1
void  HUD_SetInvisible(GOBJ *g);                                // 0x80114f04, sets is_visible = 0

void CityHUD_CreateStatChart(int ply, int ply2);                // 0x80128bb8
void CityHUD_CreateStatBar(int ply, int ply2, int stat_kind);   // 0x80129154
void CityHUD_DestroyAllStatCharts(void);                        // 0x801294a8

GOBJ *HUD_CreateMiscGObj(JOBJDesc *j, int p_link, int gx_link, int gx_pri); // 0x801147dc
GOBJ *3DHud_CreatePlayerElement(int ply, JOBJDesc *j);      // 0x80114ba4, GObj_Create(27,26,0) + GObj_AddGXLink(g, 3DHud_Render, 21, 1)
void  3DHud_CreateIndicatorGObjCustomGX(int ply, JOBJDesc *j, void *gx_cb); // 0x801149a0, GObj_Create(27,26,0) + GObj_AddGXLink(g, gx_cb, 18, 1)
void  3DHud_Render(GOBJ *g, int pass);                      // 0x80114f1c
void  3DHud_RenderIfVisible(GOBJ *g, int pass);             // 0x8011500c
void  JObj_AddSetAnim0_SetFrameAndRate(JOBJ *j, JOBJSet **sets, float frame, float rate); // 0x80114d9c
HSD_Archive **3DHud_GetIfAll1cArchive(void);                // 0x80112050 (link.ld: Gm_GetIfAllCityArchive)
HSD_Archive **3DHud_GetIfAllScreenArchive(void);            // 0x80112058 (link.ld: Gm_GetIfAllScreenArchive)
void *3D_GetData(void);                                     // 0x80112044, returns Game3dData
int   Gm_GetPlyViewNum(void);                               // 0x800092b4, human-player viewport count
```

## HUD Element Data

`HUDElementData` is the canonical `userdata` payload, defined in `externals/hoshi/include/hud.h`:

```c
typedef struct HUDElementData
{
    int x0;            // 0x0
    HUDKind kind;      // 0x4
    u8 x8_80 : 1;      // 0x8
    u8 x8_40 : 1;
    u8 x8_20 : 1;
    u8 x8_10 : 1;
    u8 ply : 2;        // 0x8, mask 0x0c
    u8 is_visible : 1; // 0x8, mask 0x02
    u8 x8_01 : 1;
    int xc;            // 0xc
    int x10;           // 0x10
    union {
        struct { ... } speedometer_outer;
        struct { ... } timer;
        struct { ... } hp_bar;
        struct {
            Vec3 pos[4];    // 0x14, per-player positions
            JOBJ *j[4];     // 0x34, per-player JOBJs
        } ply_hud;
        struct {
            int x14, x18, x1c;
            int ply;           // 0x20
            JOBJ *bar_j;       // 0x24, gauge fill bar (child index 1)
            JOBJ *num_right_j; // 0x28, a digit JObj   (child index 4)
            JOBJ *num_left_j;  // 0x2c, a digit JObj   (child index 5)
            JOBJ *sign_j;      // 0x30, minus sign     (child index 6)
        } city_stat_bar;
    };
} HUDElementData;
```

The `num_left_j` / `num_right_j` names encode struct storage order, **not** on-screen left/right position.

### HUDKind Values

| Value | Name | Description |
|-------|------|-------------|
| 1-2 | `SPEEDOMETEROUT` / `SPEEDOMETERIN` | Speed gauge |
| 3 | `TIMER` | Race/event timer with digit JObjs |
| 4 | `PLYNUM` | Player number display |
| 5 | `PLICON` | Player character icon |
| 12 | `HPBAR` | Health bar |
| 19 | `HUDCAM` | Camera HUD |
| 25 | `ITEMINDICATOR` | Current item display |
| 35 | `PLYHUDPOS` | Per-player viewport overlay |
| 66 (0x42) | `CITYSTATBG` | Stat bar background; `li r4,66` @ 0x80128c70 in `City_CreateStatChart` |
| 67 (0x43) | `CITYSTATBAR` | City Trial stat bar (bar + 2 digits + sign); `li r4,67` @ 0x801291e0 in `City_CreateStatChartBar` |

`kind` is only consulted by element-specific update procs, so a custom element may pass any value it does not collide on.

### Attaching Data and Visibility

`HUD_AddElementData` / `3DHud_AddData` (0x80114e24):

1. `HSD_ObjAlloc` a `HUDElementData`, then `memset(p, 0, 0xe4)` — the struct is 228 bytes
2. `GObj_AddUserData(gobj, 27, destructor = 0x801151e8, p)`
3. stores `kind` (arg 2) as a full int at +0x4
4. packs arg 3 into the top nibble of byte +0x8 (mask 0xf0) and arg 4 into the 2-bit `ply` field (mask 0x0c)
5. sets `is_visible` (mask 0x02) to 1

Step 5 is what makes the wrapper GX callbacks draw anything: both `3DHud_RenderIfVisible` (0x8011500c) and `3DHud_Render` (0x80114f1c) skip rendering when that bit is 0. `HUD_SetVisible` (0x80114eec) and `HUD_SetInvisible` (0x80114f04) toggle it afterwards.

## Rendering Pipeline

The game renders in two levels:

1. **Camera GObjs** — created via `GObj_InitCamera` (or `GOBJ_EZCreator` with `HSD_OBJKIND_COBJ`), each assigned a unique `gx_link` (64+). Their `cobj_links` field (u64 bitmask) selects which GX links they render.
2. **Renderable GObjs** — placed on a specific GX link (0-63) via `GObj_AddGXLink`, each with a `gx_cb` called during rendering.

Render loop:

1. For each camera GObj, `CObjThink_Common` (0x8042a29c) sets up projection from the camera's COBJ, then calls `CObj_RenderGXLinks(camera, pass_mask = 7)`.
2. `CObj_RenderGXLinks` (0x8042a0b4) iterates each set bit in the camera's `cobj_links`; for each GX link N it walks all objects on that link and calls `gobj->gx_cb(gobj, pass)`.
3. Pass 0 = OPA (opaque), pass 1 = XLU (translucent), pass 2 = additional.

`CObj_RenderGXLinks` does **not** look at the rendered object's `cobj_links` — only the camera's matters. `GObj_Create` zeroes `cobj_links` and nothing else writes it, so leave it at 0 on non-camera objects; setting it there has no effect.

Neither `CObjThink_Common` nor `CObj_RenderGXLinks` is in `link.ld`; call `CObjThink_Common` through a raw pointer cast when supplying it as a camera `gx_cb`.

### GX Callbacks

| Callback | Address | Used By | Behavior |
|----------|---------|---------|----------|
| `JObj_GX` (map: `GObj_RenderJObj`) | 0x8042a258 | `JObj_LoadSet_SetPri`, general | Unconditional: loads `gobj->hsd_object` as a JObj and renders it |
| `3DHud_RenderIfVisible` | 0x8011500c | `HUD_CreateMiscGObj` | Checks `is_visible` (0x02 of `userdata[0x8]`); if set, calls `JObj_GX` passing the **userdata** as the JObj arg |
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

`JObj_LoadJoint` copies flags straight from the `JOBJDesc`, including `JOBJ_HIDDEN` (0x10). Some archive models ship hidden because the game's own code shows them later via `HUD_SetVisible`. `JObj_LoadSet_SetPri` with `is_hidden = 0` only skips *setting* the flag — it never *clears* one already present in the descriptor. Always `JObj_ClearFlagsAll(root, JOBJ_HIDDEN)` after loading a HUD model.

## HUD Coordinate Space

All HUD orthographic cameras share one projection:

| Parameter | Value |
|-----------|-------|
| top | 0.0 |
| bottom | -480.0 |
| left | 0.0 |
| right | 640.0 |

X runs 0 (left edge) to 640 (right edge); Y runs 0 (top edge) to -480 (bottom edge), **negative going down**; Z is 0 for flat elements. Top-left is `(0, 0, 0)`, bottom-right `(640, -480, 0)`, center `(320, -240, 0)`.

`CObj_SetOrtho(COBJ *cobj, float top, float bottom, float left, float right)` overrides a descriptor's projection to match; the HUD archive COBJDescs already use these values.

## JObj Fields Used by HUD Code

The runtime `JOBJ` is defined in `externals/hoshi/include/obj.h`:

```c
struct JOBJ {
    HSD_Obj object;     // 0x00
    JOBJ *sibling;      // 0x08
    JOBJ *parent;       // 0x0C
    JOBJ *child;        // 0x10
    int flags;          // 0x14 (JOBJ_HIDDEN, JOBJ_OPA, JOBJ_XLU, ...)
    DOBJ *dobj;         // 0x18 (display objects: geometry/materials)
    Vec4 rot;           // 0x1C
    Vec3 scale;         // 0x2C
    Vec3 trans;         // 0x38 (position)
    Mtx rotMtx;         // 0x44 (computed world matrix)
    Vec3 *VEC;          // 0x74
    Mtx *MTX;           // 0x78
    AOBJ *aobj;         // 0x7C
    void *robj;         // 0x80
    JOBJDesc *desc;     // 0x84
};
```

Flags: `JOBJ_HIDDEN` = `1 << 4` (0x10), `JOBJ_OPA` = `1 << 18`, `JOBJ_XLU` = `1 << 19`. After writing `trans`, call `JObj_SetMtxDirtySub(jobj)` (0x8040d92c) so the world matrix is recomputed.

## Loading a Model From an Archive

1. Get the archive handle:
   ```c
   HSD_Archive **arch = Gm_GetIfAllCityArchive(); // IfAll1c
   ```
2. Fetch a model by public symbol:
   ```c
   JOBJSet **sets = Archive_GetPublicAddress(*arch, "ScInfPausegaugect_scene_models");
   ```
3. Create the GObj, either through the engine's creator:
   ```c
   GOBJ *gobj = HUD_CreateMiscGObj_(sets[0]->jobj, GAMEPLINK_HUD, GAMEGX_HUD, 1);
   GObj_AddProc(gobj, MyUpdateCallback, 20);
   ```
   (this installs `3DHud_RenderIfVisible`, so nothing draws until `HUD_AddElementData` or `HUD_SetVisible` sets `is_visible`), or through the hoshi helper, which uses plain `JObj_GX` and has no visibility gate:
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
       20);               // proc priority
   ```
4. Reach child JObjs by depth-first index: `JOBJ *child = GObj_GetJObjIndex(gobj, child_index);` (0x80055af0).

### Per-Frame Updates

```c
void MyUpdateCallback(GOBJ *g)
{
    JOBJ *root = g->hsd_object;

    JOBJ *digit = GObj_GetJObjIndex(g, digit_child_index);
    HUD_UpdateElement(digit, value);   // frame N shows digit N

    root->trans.X = new_x;
    root->trans.Y = new_y;
    JObj_SetMtxDirtySub(root);

    JObj_SetFlagsAll(digit, JOBJ_HIDDEN);    // hide
    JObj_ClearFlagsAll(digit, JOBJ_HIDDEN);  // show
}
```

`HUD_UpdateElement(jobj, value)` (0x8011503c) runs `HSD_JObjReqAnimAllFlags(jobj, (float)value)`, then `JObj_SetAllAOBJRateByFlags(jobj, 0xffff, 1.0)`, then `HSD_JObjAnimAll(jobj)`. The digit models are texture-swap material animations, so frame N selects image N. The `ScInfPausegaugect` digit `TexAnim` carries 11 `ImageDesc` entries (16x15, format 0), enough for 0-9 plus one spare.

### Enumerating Archive Symbols at Runtime

```c
HSD_Archive *arch = *Gm_GetIfAllCityArchive();
for (u32 i = 0; i < arch->header.nb_public; i++)
{
    char *name = arch->symbols + arch->public_info[i].symbol;
    void *addr = arch->data + arch->public_info[i].offset;
    OSReport("  [%d] %s = %p\n", i, name, addr);
}
```

## Archives

HSD archives (`.dat`) hold the JObj models, animations and textures. Naming is `IfAll{mode}{variant}.dat`, mode 1 = common HUD, 2 = City Trial, 3 = Top Ride; variant `c` = common, `1`/`2`/`4` = player count, `s` = scaled/secondary.

### IfAll1c - Common HUD

Accessed via `Gm_GetIfAllCityArchive()` (0x80112050). Loaded in all modes; the primary source for HUD elements. 41 symbols.

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
| `ScInfGpanel_scene_data` | Gauge panel (`scene_data`, not `scene_models`) |
| **Other** | |
| `ScInfEnemyAbility_scene_models` | Enemy ability indicator |
| `ScInfMmapply_scene_models` | Minimap player marker |
| `ScInfMmapstart_scene_models` | Minimap start point |
| `ScInfReplay_scene_models` | Replay |
| `ScInfWarningfall_scene_models` | Warning: falling |
| `ScInfWrongway_scene_models` | Wrong way |

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
| `ScInfEventct_scene_models` | Event notification banner (1P) |
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

### IfAll2X - City Trial Per-Player-Count

Stadium elements, 9 symbols each. Suffix `1`/`2`/`4`.

| Symbol Pattern | Description |
|----------------|-------------|
| `ScInfEventmark{N}ct` | Event marker |
| `ScInfSpDragoonA/B/C{N}` | Dragoon legendary machine pieces |
| `ScInfSpHydraA/B/C{N}` | Hydra legendary machine pieces |
| `ScInfSpEfx{N}` | Stadium special effect |
| `ScInfSpPos{N}` | Stadium position indicator |

### IfAll3c - Top Ride Common

| Symbol | Description |
|--------|-------------|
| `ScInfRound1_scene_models` | Round 1 indicator |
| `ScInfRound2_scene_models` | Round 2 indicator |
| `ScInfRound3_scene_models` | Round 3 indicator |
| `ScInfStarIcon_scene_models` | Star icon |

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

| Symbol | Description |
|--------|-------------|
| `ScInfResult_scene_models` | Results display |
| `ScInfResultM2d_scene_models` | Results (2D variant) |

## City Trial Stat Bar Internals

`CityHUD_CreateStatChart(ply, ply2)` (map `City_CreateStatChart`, 0x80128bb8):

1. Picks the background model by player count (`Gm_GetPlyViewNum` -> 1/2/4P slot) from `Game3dData` and loads it with `HUD_CreateMiscGObj(jobj, 0x1b, 0x15, 1)` — `p_link` 27 (`PAUSEHUD`), `gx_link` 21, `gx_pri` 1
2. Attaches `HUDElementData` via `3DHud_AddData(..., kind = 0x42)` (`CITYSTATBG`); the GObj's `entity_class` is still 27
3. Extracts world positions with `JObj_GetWorldPosition` (0x80053f34) from background child JObjs 1-9, the 9 stat slot positions
4. Stores those 9 `Vec3`s in the background's `HUDElementData` at +0x14, +0x20, +0x2c ... +0x74
5. Adds a per-frame proc at priority 20; the bars themselves are created separately, one per stat kind 0-8

`City_CreateStatChartBar(ply, ply2, stat_kind)` (0x80129154):

1. Loads the gauge from IfAll1c (`ScInfPausegaugect_scene_models`) with `HUD_CreateMiscGObj(jobj, 0x1b, 0x15, 1)`
2. Attaches `HUDElementData` via `3DHud_AddData(..., kind = 0x43)` and stores `stat_kind` at +0x14
3. Caches child JObjs by depth-first index: 1 -> +0x24 `bar_j`, 6 -> +0x30 `sign_j`, 5 -> +0x2c `num_left_j`, 4 -> +0x28 `num_right_j`
4. Reads its slot position out of the background's stored positions (`background_data + 0x14 + stat_kind * 0xc`), writes it into the gauge root JObj `trans` (0x38/0x3c/0x40), then `HSD_JObjSetMtxDirtySub`

### ScInfPausegaugect JObj Hierarchy

Depth-first indices as returned by `GObj_GetJObjIndex`:

```
Root (index 0) - positioned at the stat slot location
+- Child 1: bar_j - gauge fill bar (animated by fill frame)
|  +- Child 2: bar sub-element
|  +- Child 3: bar sub-element
+- Child 4: num_right_j - a digit JObj (struct +0x28)
+- Child 5: num_left_j  - a digit JObj (struct +0x2c)
+- Child 6: sign_j - minus sign (struct +0x30)
```

Indices 1, 4, 5 and 6 are all direct children of the root. Indices 4 and 5 are the two digit JObjs and index 6 is the sign; which digit reads as "left" or "right" on screen is a placement choice, not a property of the index.

### Value Display Logic

```
value = Patch_GetPlySavedValue(ply, stat_kind, 0)   // current stat value
value += 2  (for stat_kind 0-7; stat_kind 8 uses the raw value)
bar:    HUD_UpdateElement(bar_j, value) when value > 0
sign:   JOBJ_HIDDEN unless value <= -10 (a leading minus only for large negatives)
digits: the two digit JObjs (index 4/5) are updated/hidden per the |value| < 10 vs >= 10 split
```

The "+2" offset is why a fresh stat slot reads "2"; the bar only fills for positive values.

### Storage in Game3dData

| Object | Address |
|--------|---------|
| Background GObjs | `Game3dData + 0x63c + ply2 * 4` |
| Per-stat bar GObjs | `Game3dData + 0x650 + ply2 * 36 + stat_kind * 4` |

## Timer Model (ScInfTime)

Two variants live in IfAll1c: `ScInfTime_scene_models` (1-2 player, cached at `Game3dData + 0xb4`) and `ScInfTime4_scene_models` (3-4 player, `Game3dData + 0xb8`).

`3DHud_CreateTimer` (0x80119218) picks between them on `Gm_GetPlyViewNum()`: `<= 1` takes the +0xb4 model, otherwise +0xb8. It then calls `HUD_CreateMiscGObj(jobj, p_link, 0x15, 1)` with `p_link` = 27 in the Air Ride Free Run path (major 4, `airride_mode` 2) and 26 elsewhere, and adds `3DHud_TimerUpdate` (0x80118f50) as a proc at priority 20.

### Digit Slots

The model has 6 digit JObjs at child indices 1-6:

| Child Index | HUDElementData Offset | Display |
|-------------|----------------------|---------|
| 1 | +0x2c | Frames/centiseconds ones |
| 2 | +0x30 | Frames/centiseconds tens |
| 3 | +0x34 | Seconds ones |
| 4 | +0x38 | Seconds tens |
| 5 | +0x3c | Minutes ones |
| 6 | +0x40 | Minutes tens |

Format is MM:SS:FF. Each digit uses the same frame-based animation as the stat bar, so `HUD_UpdateElement(jobj, digit)` drives it. Repurposed as a numeric readout it gives 6 digits (max 999999); unused slots can be hidden with `JOBJ_HIDDEN`, but the model may carry visible separator geometry between digit pairs.

## Player Number Model (ScInfPlynum)

Cached at `Game3dData + 0xc0`, created by `3DHud_CreatePlyNum` (0x8011fd20). A single model whose animation frame selects "P1", "P2" and so on — a label, not a digit grid. Useful next to a score display, not as one.

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

JOBJ *bar_j  = GObj_GetJObjIndex(gauge, 1);
JOBJ *tens_j = GObj_GetJObjIndex(gauge, 4);
JOBJ *ones_j = GObj_GetJObjIndex(gauge, 5);
JOBJ *sign_j = GObj_GetJObjIndex(gauge, 6);

root->trans = (Vec3){ x, y, 0 };
JObj_SetMtxDirtySub(root);

void MyHUD_Update(void)
{
    int score = GetScore();
    HUD_UpdateElement(ones_j, score % 10);
    if (score >= 10)
    {
        JObj_ClearFlagsAll(tens_j, JOBJ_HIDDEN);
        HUD_UpdateElement(tens_j, score / 10);
    }
    else
        JObj_SetFlagsAll(tens_j, JOBJ_HIDDEN);
    sign_j->flags |= JOBJ_HIDDEN;  // never show minus
    bar_j->flags  |= JOBJ_HIDDEN;  // re-hide each frame, AnimAll clears it
}
```

Other models worth reusing: `ScInfPlynum_scene_models` (player label), `ScInfPlicon_scene_models` (character icon), `ScInfTime_scene_models` (multi-digit readout).

### Dedicated Camera

Rather than sharing a game HUD link, a custom element can own an ortho camera on an unused GX link, which makes rendering independent of the game's camera management. The COBJDesc at 0x805096a0 is a vanilla ortho camera descriptor.

```c
#define CUSTOM_GX_LINK 23

// CObjThink_Common, the camera render callback (not in link.ld)
static void (*CObj_GX)(GOBJ *g, int pass) = (void *)0x8042a29c;

GOBJ *cam = GOBJ_EZCreator(0, 0, 0,
                           0, 0,
                           HSD_OBJKIND_COBJ, (COBJDesc *)0x805096a0,
                           0, 0,
                           CObj_GX, 0, 5);
cam->cobj_links = (1ULL << CUSTOM_GX_LINK);

COBJ *cobj = cam->hsd_object;
CObj_SetOrtho(cobj, 0.0f, -480.0f, 0.0f, 640.0f);

GOBJ *hud = JObj_LoadSet_SetPri(0, sets[0], 0, 0.0f,
                                GAMEPLINK_HUD, CUSTOM_GX_LINK, 1, NULL, 0);
JObj_ClearFlagsAll(hud->hsd_object, JOBJ_HIDDEN);

GObj_Destroy(hud);
GObj_Destroy(cam);
```

The Gourmet Race score display uses exactly this shape: a camera on GX link 23 with `CObjThink_Common` as its `gx_cb`, `cobj_links = 1 << 23`, `CObj_SetOrtho(cobj, 0, -480, 0, 640)`, and per-player gauge + `ScInfPlynum` label GObjs loaded onto that link with `JObj_LoadSet_SetPri`. hoshi's `screen_cam.c` carries the same camera pattern commented out; its live screen-camera path goes through `Text_CreateCanvas` instead.

## Known Limitations

- Runtime per-JObj material/color overrides (for example per-player tinting) are not currently used by any mod HUD; the models are drawn with the colors baked into their MObjs.
- Screen positions of the game's own stat bars are only known indirectly, through the 9 slot positions the background model exposes to `City_CreateStatChart`. Custom elements placed by absolute coordinates can overlap them.
