# City Trial Sky / Lighting System

## Overview

There are **three** independent objects working together for stage lighting,
and confusing them is the easiest way to misread the system:

1. **Sky preset** — a 0x48-byte record in the stage file (`GrCity1.dat`).
   Drives fog, screen tint, sky ambient color, and a single "area light"
   directional vector.
2. **AreaLight** — a runtime, KAR-proprietary HSD-style object stored at
   `GrObj+0x718`. Per-frame interpolation target for the preset's directional
   light fields. **Not** a standard `LOBJ` and **not** registered in HSD's
   active-light list.
3. **HSD `LOBJ` chain** — the actual GameCube hardware lights, loaded from
   the stage file's `LObjDesc**` array at `gr_data->stage_resource[+0x14]`.
   These get GX hardware light slots assigned dynamically each frame by
   `HSD_LObjSetCurrentAll`. This chain is what most stage geometry's
   `GXSetChanCtrl` light_mask references.

The "AreaLight" is animated by sky presets, but the AreaLight's interpolated
output then has to be pushed into one or more LOBJs in the HSD chain (or its
values consumed elsewhere) for it to affect rendering. Both objects coexist
during gameplay.

There is **no `LOBJ` creation in `Sky_SetupLights`** — that function only
toggles JOBJ render-node visibility flags on a per-stage helper table.

The fog GObj (`grobj+0x168`) is a separate HSD object with its own GX
callback (`Fog_GX`). The lbfade screen-tint overlay is a fourth subsystem.

```
GrObj  (gr_kind=9, City Trial)
 ├── +0x20  collision/zone pool header (counts + carved sub-arrays)
 ├── +0x54  collision-zone descriptor (→ 0x98 records at +0x64)  ┐ Sky_SetupLights shows
 ├── +0x104 per-joint table ({JOBJ*, JOBJDesc*}, 317 in CT)      ┘  glow billboards 62/63
 │   (128 per-zone AreaLights live in the registry at *(0x805DD618), not a GrObj field)
 ├── +0x168 fog/sky GObj  ────────────┐
 │            ├── +0x28 HSD_Fog       ├── written each frame by Sky_Update
 │            └── +0x2C SkyState      │  start/end/color of fog;
 │                                    │  sky_state at +0x2C; lerp output mirror
 │                                    ▼
 │   global EFB clear at 0x80557484   World_CObj reads this for next clear
 │
 ├── +0x714 ScreenFade slot 3 (lbfade) — only fired by Sky_BeginTransition
 │
 ├── +0x718 AreaLight (one per stage)
 │            └── header/color/direction/intensity/attn (interpolated)
 │
 └── (gr_data->stage_resource[+0x14])  LObjDesc** — hardware lights
              └── consumed by Light_CreateForStage at stage load
                  → produces real LOBJs in the active list, rendered by LObj_GX
```

## Key Functions

| Address     | Symbol                                     | Description |
|-------------|--------------------------------------------|-------------|
| 0x8010f114  | `Sky_Init`                                 | Per-stage initial sky setup; calls `Sky_SetupLights` for each declared JOBJ index, then loads an initial preset (random for CT, fixed for everything else). |
| 0x800db774  | `Sky_SetupLights(grobj, jobj_idx)`         | **Toggles JOBJ visibility + collision only.** Forces render flag bit 4 on the stage JOBJ subtree at `grobj[+0x104] + jobj_idx*8` (showing the glow billboard), then disables collision on every face of that joint's zone under `grobj[+0x54]`. Does **not** touch any HSD light. |
| 0x800dc630  | `Sky_SetPresetIndex`                       | Stores a preset index into sky state +0x1C without applying it. |
| 0x800dc1b4  | `Sky_LoadPreset`                           | Snaps to the preset *immediately* (no transition, no fade). Copies the preset's AreaLightData into the live AreaLight at `grobj+0x718`, sets fog start/end/color directly, and broadcasts `light_vis_flag` via `0x80079948`. |
| 0x800dc354  | `Sky_BeginTransition(grobj, idx)`          | Begins a smooth interpolation: captures current values as start, fires `Sky_BeginFade` for the lbfade overlay, sets target. |
| 0x800dc4c0  | `Sky_ApplyStoredIndex`                     | Like `Sky_BeginTransition` but reads the index from sky state +0x1C. |
| 0x800dc640  | `Sky_Update`                               | Per-frame interpolation. Writes seven memory regions (see "Per-Frame Sky_Update" below). |
| 0x800dc7a4  | `Sky_GetCurrentSkyColor(grobj, &out)`      | Returns `current_output_sky_color` (`sky_state+0x18`, the per-frame lerp toward the preset's `sky_ambient_color`). Returns RGBA(0,0,0,0) if no sky GObj or no target preset. Called at `zz_800d8148_+0x64` (0x800d81ac) and `Map_GX+0xa0` (0x800d8284); each call feeds the result straight into `Sky_DrawTintQuad` on the next line. This is the consumer of step-7's write. |
| 0x800d7e78  | `Sky_DrawTintQuad(cobj, &color)`           | Renders an alpha-blended screen-aligned quad at the camera's far plane using the supplied RGBA. Early-outs if `color.a == 0`. Fog-enabled — distance fog attenuates the tint. This is how `sky_ambient_color` becomes a visible sky tint. |
| 0x800dbfa8  | `Sky_InitFog`                              | Builds the fog GObj: `GObj_Create(0x1E,1,0)`, `Fog_LoadDesc`, `GObj_AddObject`, `GObj_AddGXLink(Fog_GX, 0, 1)`. Seeds the global EFB clear color at 0x80557484. |
| 0x800dbf84  | `Fog_GX`                                   | GX callback. One-liner: `HSD_FogSet(gobj->object[0x28])`. |
| 0x800797a8  | `AreaLight_Lerp`                           | Interpolates the AreaLight (lbarealight.c). Asserts validity bits, snap-copies header/colors/direction from target, lerps if `flags & 0x04` is set. |
| 0x80079c04  | `GXColor_Lerp`                             | Linearly interpolates 4-byte RGBA colors by ratio. |
| 0x80079428  | `AreaLight_Create`                         | Allocates a live AreaLight object, registers it in the global registry at `r13[+0x538]`, copies fields from a source `AreaLightData`. Asserts `flags & 0x03 == 0x03`. |
| 0x8007a4d0  | `AreaLight_Create_Default`                 | Thin wrapper: `AreaLight_Create(class=0, src, extra=0)`. |
| 0x8007a598  | `AreaLight_LerpThunk`                      | Pure thunk to `AreaLight_Lerp` (0x800797a8). |
| 0x800ef618  | `AreaLight_StageInit`                      | Stage-init helper that stack-builds a default `AreaLightData` and stores the resulting AreaLight at `grobj+0x718`. |
| 0x800ef864  | `AreaLight_LerpToLive`                     | Adapter: extracts `grobj+0x718` and dispatches to `AreaLight_Lerp`. Called from `Sky_Update`. |
| 0x80079948  | `AreaLight_BroadcastVisFlag`               | Walks the AreaLight registry and writes bit 0x80 of byte +0x38 on every entry from `light_vis_flag` bit 0. |
| 0x800eef04  | `Sky_AllocFade`                            | Allocates `ScreenFade` slot 3 at `grobj+0x714`. |
| 0x800eef50  | `Sky_BeginFade(grobj, &color, frames)`     | Calls `ScreenFade_Begin` on the slot. |
| 0x800eefb0  | `Sky_FreeFade`                             | Frees the lbfade slot at scene teardown. |
| 0x80065140  | `ScreenFade_Draw`                          | Full-screen quad emitter for the fade overlay (lbfade GX path). |
| 0x8006541c  | `ScreenFade_GX`                            | GX callback for the fade GObj. |
| 0x800b04a8  | `World_CObj`                               | World-camera GObj GX callback. At +0x144 it loads the global fog color from 0x80557484 and pushes it through `HSD_SetEraseColor`. |
| 0x8040f884  | `HSD_SetEraseColor`                        | Writes the active GX clear color at `r13[-1368]` (0x805dcb88). |
| 0x8041b0fc  | `HSD_FogSet`                               | Reads the live `HSD_Fog`, queries current CObj near/far, emits `GXSetFog` and `GXSetFogColor`. |
| 0x80057468  | `LObj_CreateAll` (matches obj.h:885)       | Walks a NULL-terminated `LObjDesc**` array, calls `HSD_LObjLoadDesc` per entry, links them via `LOBJ.next` (+0x0C). |
| 0x80400238  | `HSD_LObjLoadDesc` (link.ld: `LObj_LoadDesc`) | Allocates an LOBJ, copies desc fields. |
| 0x803ff080  | `HSD_LObjAddCurrent`                       | Adds an LOBJ to the global active-light list at `r13[+0x112C]`. |
| 0x803ff570  | `HSD_LObjSetCurrentAll`                    | Each frame: clears the 9-slot table at 0x805899B0 (`stc_lobj_hw_slot_table` in `obj.h`), re-walks the list, assigns each LOBJ a hardware slot. |
| 0x803fe4b8  | `HSD_LObjSetupInit`                        | Bakes each active LOBJ into a hardware light register via `GXInitLight*` + `GXLoadLightObjImm`. |
| 0x803ffc64  | `HSD_LObjSetPosition` (link.ld: `LObj_SetPosition`) | Sets WObj-backed light position; allocates a WObj on first call. |
| 0x803ffd2c  | `HSD_LObjSetInterest` (link.ld: `LObj_SetInterest`) | Sets WObj-backed light interest (target). |
| 0x8042a22c  | `LObj_GX`                                  | GX callback for an LObj-bearing GObj. Calls `HSD_LObjSetCurrentAll` then `HSD_LObjSetupInit`. |
| 0x800d5ed4  | `Light_StageInit`                          | Stage-init driver. Calls `Light_CreateForStage`, `Light_CreateForStageSecondary`, and `Light_CreateForMenu` back-to-back. Sole caller from `grLoadStage`. |
| 0x800d5fd0  | `Light_CreateForStage`                     | Creates the class-1 / p_link-1 stage-light GObj at gx_link 0. Loads `(*stage_resource[+0x14])[+0x00]` (a `LightGroup**` chain) via `LObj_CreateAll`, registers `Light_GX` (0x800d5fb0). Per-LOBJ AddProc at 0x800d5f3c does `HSD_LObjAnim` (skipping LOBJs with AOBJ flag 0x40000000) and applies stage scale via 0x80057620. Also writes `stc_main_light` at `r13[+0x5fc]`. |
| 0x800d60d8  | `Light_CreateForStageSecondary`            | Creates a **class-20 (0x14), p_link-1** GObj at **gx_link 8**. Loads `(*stage_resource[+0x14])[+0x08]` via `LObj_CreateAll`. GX callback at 0x800d60b8 is a byte-identical thunk to `Light_GX` — just a separate entry point. AddProc at 0x800d6094 is `HSD_LObjAnimAll` (no AOBJ filter). Built unconditionally on every stage load (no menu/debug gate). For City Trial, the secondary chain has 2 LObjs (file 0xC1910). |
| 0x800d6188  | `Light_CreateAreaLightDefaults`            | Third LOBJ-creating site. Calls `grGetStageLight_Kirby` (0x800cea5c) to fetch the **character/rider default light chain** at `(*stage_resource[+0x14])+0x04`. Loads via `LObj_CreateAll` and stashes the chain head at `r13[+0x5F8]`, the first non-hidden ambient at `r13[+0x5F0]`, and the first non-hidden infinite at `r13[+0x5F4]`. **Not GX-rendered** — no GObj is created and no GX_Link is registered. Read by `Light_GetAreaLightDefaults` (0x800d61e8) to seed `AreaLight_StageInit` and the per-zone AreaLight array (`grobj+0x20`). |
| 0x800d61e8  | `Light_GetAreaLightDefaults(out_amb, out_inf, out_pos)` | Getter that returns the three default values stashed by `Light_CreateAreaLightDefaults`. Calls `HSD_LObjGetColor` on `r13[+0x5F0]` and `r13[+0x5F4]`, `HSD_LObjGetPosition` on `r13[+0x5F4]`. If `r13[+0x5F8]==0` (no chain), writes (0,0,0,0xFF) defaults. |
| 0x800d5444  | `Sky_TransitionGlobal(idx)`                | `Sky_BeginTransition` using `*stc_grobj` (0x805dd6cc-ish; see `stc_grobj` in stage.h:171). |
| 0x800d546c  | `Sky_RestoreGlobal`                        | Restores the original preset after an event. |
| 0x800d5414  | `Sky_GetPresetCount`                       | Returns total preset count from stage data: `(*stc_grobj)->gr_data->sky_block->preset_header->preset_count`. |
| 0x800db2b8  | `Gm_Roll(weights, count)`                  | Weighted random selection. |

## Sky Preset Entry (0x48 bytes)

Stored in the stage file, accessed via
`grobj->gr_data->sky_block->preset_header->preset_array`. The C struct in use
is `SkyPresetEntry` in `externals/hoshi/include/stage.h`
(with the embedded `AreaLightData` at `externals/hoshi/include/obj.h:742-756`).
`mods/custom_weather/src/custom_weather.c` builds its custom entries against
this struct in `ExtendPresetArray`.

```
+0x00  int    transition_frames     - frames to interpolate to this preset
+0x04  RGBA   fog_color             - target fog/background color (interpolated)
+0x08  float  fog_start             - fog near distance (interpolated)
+0x0C  float  fog_end               - fog far distance (interpolated)
+0x10  RGBA   fade_color            - lbfade screen tint, fired only on transitions
+0x14  RGBA   sky_ambient_color     - target skybox tint (interpolated)
+0x18  AreaLightData (0x2C bytes)   - directional light parameters (see below)
+0x44  u8     light_vis_flag        - bit 0 → AreaLight registry +0x38 bit 0x80
+0x45  (3 bytes pad)
```

### AreaLightData (0x2C bytes at preset +0x18)

```
+0x00  u32    header              - metadata for AreaLight_Create, raw-copied
+0x04  u8     unk_04              - raw-copied
+0x05  u8     flags               - bits 0+1 validity (asserted), bit 2 lerp enable
+0x06  u16    unk_06              - raw-copied
+0x08  RGBA   light_color         - diffuse light color (GXColor_Lerp interpolated)
+0x0C  RGBA   light_hw_color      - specular/hardware light color (GXColor_Lerp)
+0x10  Vec3   light_direction     - direction (Vec3 lerp, 12 bytes)
+0x1C  u8[3]  unk_1C              - raw-copied (HSD attenuation type/flags)
+0x1F  u8     light_intensity     - byte interpolation if flags bit 2 set
+0x20  u32    attn_param_0        - raw-copied (HSD light attn/spot params)
+0x24  u32    attn_param_1        - raw-copied
+0x28  u32    attn_param_2        - raw-copied
```

The interpolation enable bit (`flags & 0x04`) is what controls whether
`AreaLight_Lerp` runs the per-frame color/direction lerps; without it the
fields snap to target immediately every call.

## Sky State Struct (runtime, 0x48 bytes)

Allocated as part of the fog/sky GObj. Accessed via `grobj->gobj + 0x168 -> +0x2C`.

```
+0x00  void*  target_preset_ptr          - current target preset
+0x04  int    transition_frame_counter   - 0 → target.transition_frames
+0x08  RGBA   start_fog_color / lerp_out - interpolation start; reused as Sky_Update
                                           output mirror for the fog RGBA write
+0x0C  float  start_fog_start
+0x10  float  start_fog_end
+0x14  RGBA   start_sky_color
+0x18  RGBA   current_output_sky_color   - per-frame lerp of start_sky_color →
                                           preset.sky_ambient_color; read by
                                           Sky_GetCurrentSkyColor and painted as the
                                           far-plane sky tint by Sky_DrawTintQuad
+0x1C  int    current_preset_index
+0x20  AreaLightData (0x2C bytes)        - start values for AreaLight_Lerp
```

## Live AreaLight (runtime, ~0x40 bytes)

At `grobj+0x718`. Built by `AreaLight_Create` (0x80079428) with fields copied
from a source `AreaLightData`. Registered into a global registry at
`r13[+0x538]`, which `AreaLight_BroadcastVisFlag` walks.

```
+0x00  HSD vtable (class-table 1336(r13))
+0x04  class_ptr  - matches the class passed to AreaLight_Create
+0x08  u32 header
+0x0C  u8 unk_04
+0x0D  u8 flags
+0x10  GXColor light_color
+0x14  GXColor light_hw_color
+0x18  Vec3 light_direction (12 bytes)
+0x24  u8[3] unk_1C
+0x27  u8 light_intensity
+0x28  u32 attn_param_0
+0x2C  u32 attn_param_1
+0x30  u32 attn_param_2
+0x34  u32 extra (=0 from stage init)
+0x38  u8 flags  - bit 0x80 toggled by light_vis_flag broadcast
```

`Sky_Update` writes the lerp output directly into this object's
color/direction/intensity fields each frame; values written to the *sky state*
mirror at `+0x20` are the lerp-start side, not the live state.

## HSD_Fog (runtime, at gobj+0x168→+0x28)

See `externals/hoshi/include/obj.h:738-748` (`HSD_Fog`). Sky_Update writes
`start`/`end`/`color` only; it never touches `type` (+0x08) or `scale` (+0x20),
which is why the weather mod can own those two fields:

```
+0x10  float start    ← lerp(state.start_fog_start, preset.fog_start, ratio)
+0x14  float end      ← lerp(state.start_fog_end,   preset.fog_end,   ratio)
+0x18  GXColor color  ← GXColor_Lerp(state.start_fog_color, preset.fog_color, ratio)
```

`Fog_GX` runs once per camera/render-pass during world rendering and pushes
these into GX. Mid-frame fog color changes are visible immediately on the
next pass; the EFB clear color is a separate path (see below).

## Stage File Data Layout

```
*stc_grobj_ptr (stage.h:171, *(0x805dd0e0 + 0x5ec))
  +0x04  GroundKind gr_kind
  +0x08  GrData *gr_data
           +0x08  void *stage_resource (mistyped as `int x8` in stage.h:146)
                    +0x04  fog_flags ...
                    +0x08  sub_block:
                              +0x04  jobj_index_1   - first arg to Sky_SetupLights
                              +0x08  jobj_index_2   - second arg (CT/TR)
                              +0x0C  int weights[4] - Gm_Roll table (CT initial select)
                    +0x14  light_chains *           - small struct of LightGroup** chains:
                              +0x00  LightGroup**   - primary GX light chain
                                                       (class-1 GObj, gx_link 0)
                              +0x04  LightGroup**   - AreaLight defaults chain
                                                       (NOT GX-rendered; seeds
                                                        AreaLight via
                                                        Light_CreateAreaLightDefaults)
                              +0x08  LightGroup**   - secondary GX light chain
                                                       (class-20 GObj, gx_link 8)
                              (each chain is a NULL-terminated array of
                               LightGroup* per obj.h:684; each LightGroup is
                               {LObjDesc *desc, LightAnim *anim})
           +0x0C  ModelSection {terrain, backdrop, ...} - see sky-backdrop-system.md
           +0x30  EventConfigData *event_config (CT only)
           +0x34  SkyBlock *sky_block (named in stage.h):
                    +0x00  HSD_FogDesc *fog_desc       - initial fog parameters
                    +0x04  SkyPresetSubHeader *preset_header:
                            +0x00  SkyPresetEntry *preset_array (0x48 each)
                            +0x04  s32 preset_count
  +0x20   collision/zone pool header (counts + descriptors for the carved sub-arrays:
            the 0x98-stride collision zones and the 128 per-zone AreaLights)
  +0x54   collision-zone descriptor (base/count of 0x98-stride zone records at +0x10/+0x14)
  +0x64   collision-zone record backing array (0x98 stride; key JOBJ* at +0x00)
  +0x104  per-joint table (8 bytes/entry: {JOBJ* +0x00, JOBJDesc* +0x04}; 317 entries for CT)
  +0x108  per-joint DObj* list
  +0x10C  per-joint MObj* list
  +0x168  fog/sky GObj
            +0x28  HSD_Fog *
            +0x2C  SkyState
  +0x714  ScreenFade slot 3 *
  +0x718  AreaLight *
```

## Vanilla CT Preset Reference

The 17 presets shipped in `GrCity1.dat`, in the live preset array
(`(*stc_grobj)->gr_data->sky_block->preset_header->preset_array`). Useful as a tuning reference
when authoring new custom presets — each row's columns map 1-to-1 to the
`SkyPresetEntry` fields documented above.

```
                       fog          start  end   fade       sky        light      hw_light   dir                vis
 [ 0] Day             9FCFFFFF      210   665   00000000   00000000   D7D7FFFF   FFFFFFFF   (-0.40,0.80,0.50)   1
 [ 1] Midnight        1E0005FF      140   560   00000080   1E0005C8   D2CDD2FF   A096A0FF   ( 0.00,-1.0,0.00)   0
 [ 2] Light Fog       969696FF      140  1000   00000000   A0A0A0AA   BEBEE6FF   D2D2F0FF   (-0.40,0.80,0.50)   1
 [ 3] Dusk 2          C8461EFF      240   900   3200006E   7832198C   E6DCD2FF   B4AAAAFF   ( 0.00,1.00,0.00)   0
 [ 4] Dusky Clouds    C8B4E6FF        1  1000   00000000   C8B4E600   D2D2E6FF   DCD2FAFF   (-0.40,0.80,0.50)   1
 [ 5] Dark Vignette   000000FF      140   500   0000003C   00143CB2   A0A0AAFF   9696AAFF   (-0.40,0.80,0.50)   0
 [ 6] Day 2           32A0C8FF      240  1000   00000000   32A0C800   DCE6FAFF   C8F0FFFF   (-0.40,0.80,0.50)   1
 [ 7] Blue Sky        82AAFFFF      300  1000   00000000   1450DC80   DCDCFFFF   FAFAFFFF   (-0.40,0.80,0.50)   1
 [ 8] Pink Sky        FFA0D9FF      180   900   00000000   FFA0D964   F0E6F0FF   FFEBF5FF   (-0.40,0.80,0.50)   1
 [ 9] Dense Fog       E6E6E6FF       20    90   00000000   E6E6E6FF   D2D2F0FF   DCDCFAFF   ( 0.00,1.00,0.00)   0
[10] Foggy            E6E6E6FF      130   800   80808050   E6E6D2C8   D2D2E6FF   AAAABEFF   (-0.40,0.80,0.50)   1
[11] Dusk             DC783CFF      300   900   785A3C00   F0965080   DCC8BEFF   FFAA6EFF   (-0.40,0.30,0.50)   1
[12] Night            00143CFF      140   665   00001080   00143CC6   B4B4D2FF   AAB4BEFF   ( 0.00,1.00,0.00)   0
[13] Gray Sky         785A32FF      500  1300   00000000   785A32AA   E6DCC8FF   FADCB4FF   (-0.40,0.80,0.50)   1
[14] Dark Purple      000000FF      500  1300   0000005A   3C0000A0   DCC8C8FF   F0DCFFFF   (-0.40,0.80,0.50)   0
[15] Red Vignette     C8461EFF      100   500   32000000   783219B4   DCC8BEFF   FAD2AAFF   ( 0.00,1.00,0.00)   1
[16] Dark Low Vis     000000FF       90   360   2800006E   1E0005C8   F0DCC8FF   DCA078FF   ( 0.00,-1.0,0.00)   1
```

## Initial Sky Selection

In `Sky_Init` (0x8010f114), the initial preset is chosen by stage:

| GroundKind | Stages | Preset Selection |
|-----------|--------|-----------------|
| 9 (GR_CITY1) | City Trial | Random from 4 presets: {0, 10, 11, 12} via `Gm_Roll` |
| 10-22 (0x0A-0x16) | Air Ride stadiums/courses | Fixed: preset 15 (0x0F) |
| 23 (0x17) | Air Ride course | Fixed: preset 16 (0x10) |
| 52 (0x34) | Top Ride | Fixed: preset 0 |

### City Trial Random Selection

- Preset index table at 0x804a77e4: `{0, 0x0A, 0x0B, 0x0C}`
- Weights loaded from `gr_data + 0x08 + 0x08 + 0x0C` (4 ints)
- `Gm_Roll(weights, 4)` returns index 0-3
- The result indexes into the 4-entry table to get the actual preset index

The initial preset is applied via `Sky_LoadPreset` (no transition, no fade).
This is **not** `Sky_BeginTransition`, so the lbfade overlay does not run on
stage entry. To force a fade-in for a custom initial preset, swap the call to
`Sky_BeginTransition` (and call `Sky_AllocFade` ahead of time if not already
allocated).

## Event-Driven Sky Changes

During City Trial, events can trigger sky transitions:

- Per-event flag table: byte at offset +0x09 in each event's 0xC-byte config entry
  - If nonzero, the event triggers a sky change
- Per-event data: word at offset +0x04 in each event's 0x14-byte data entry
  - Contains the target sky preset index
- `Sky_TransitionGlobal(preset_index)` (0x800d5444) is called for a smooth transition
- When the event ends, `Sky_RestoreGlobal` (0x800d546c) returns to the original preset

Both transitions go through `Sky_BeginTransition`, which also fires the lbfade
overlay (see "Fade Overlay" below).

## Stadium Sky Transitions

When transitioning to stadium battle (in function 0x802839b8):
- Stadium type 0: preset 13 (0x0D) "Gray Sky"
- Stadium type 1: preset 14 (0x0E) "Dark Purple"

Beyond the preset switch, this function allocates two GObjs (size 33,
priority 32) with a custom GX callback at `0x80283ed8` for the stadium
proscenium decals, and resets fade timer fields.

## Per-Frame Sky_Update (0x800dc640)

Inputs: `grobj` (r3). Reads `grobj+0x168` → SkyState at +0x2C, HSD_Fog at +0x28.

Early-out when target preset is NULL or `transition_frames == 0`. Otherwise:

| Step | Address (in fn) | Memory write | Effect |
|------|-----------------|--------------|--------|
| 1 | 800dc6a0 | `sky_state.transition_frame_counter++` (capped) | drives `ratio = counter / target.transition_frames` |
| 2 | 800dc6f4 | `sky_state+0x08` ← `GXColor_Lerp(start, target.fog_color)` | start_fog_color slot reused as **lerp output mirror** |
| 3 | 800dc708 | `HSD_Fog.color` (+0x18 from HSD_Fog parent) ← lerped RGBA | feeds `Fog_GX → HSD_FogSet → GXSetFogColor` |
| 4 | 800dc71c | `*(u32*)0x80557484` ← lerped RGBA | global EFB clear color, consumed by `World_CObj` next clear |
| 5 | 800dc734 | `HSD_Fog.start` (+0x10) ← lerped float | fog near plane |
| 6 | 800dc750 | `HSD_Fog.end` (+0x14) ← lerped float | fog far plane |
| 7 | 800dc764 | `sky_state+0x18` ← `GXColor_Lerp(state+0x14, preset+0x14)` | `current_output_sky_color`. Read back the same frame by `Sky_GetCurrentSkyColor` → `Sky_DrawTintQuad` (the far-plane sky-tint quad) at two sites: `zz_800d8148_+0x64` (backdrop pass) and `Map_GX+0xa0`. |
| 8 | 800dc778 | `AreaLight_LerpToLive(grobj, sky_state+0x20, preset+0x18, ratio)` → `AreaLight_Lerp(0x800797a8)` | Writes light color, hw_color, direction, intensity (if flags bit 2) into the live AreaLight at `grobj+0x718`. |

## Fog Rendering Pipeline

Two separate paths feed pixels with the fog color each frame:

### GX fog (per render pass)

```
Sky_Update → HSD_Fog.start/end/color
                ↓
Fog_GX (gx_link 0, gx_pri 1) → HSD_FogSet
                                ↓
                     GXSetFog(...) + GXSetFogColor(...)
                                ↓
                       per-pixel TEV blend
```

`HSD_FogSet` (0x8041b0fc) reads the *current* COBJ's near/far via
`HSD_CObjGetCurrent`/`Get{Near,Far}`, then reads `HSD_Fog.start/end/type/color`
plus an optional AOBJ adjuster chain (`HSD_Fog.aobj` at +0x1C, set up by
`Fog_LoadDesc`).

### EFB clear color (per frame buffer copy)

```
Sky_Update → 0x80557484 (BSS, RGBA8888)
                ↓
World_CObj+0x144 → reads bytes at 0x80557484 → HSD_SetEraseColor
                                                    ↓
                                          0x805dcb88 (active erase color)
                                                    ↓
                                  GX next CopyDisp emits GX_SetCopyClear
```

`World_CObj` is at 0x800b04a8 (size 0x314). The instruction at +0x144
(0x800b05ec) is the load that pushes the global fog color into the EFB clear (a line inside
the function, not the function itself).

The two paths are independent: changing only `HSD_Fog.color` changes per-pixel
fog blending but leaves the EFB clear at the previous color (visible as
contrasting borders if the camera doesn't fill the viewport). Changing only
0x80557484 changes the clear but leaves the GX fog blend at the previous
color. `Sky_Update` writes both.

### Fog descriptor, type, and curve

City Trial's initial fog comes from the on-disk `HSD_FogDesc` at
`gr_data->sky_block->fog_desc`. Its decoded values in `GrCity1.dat`:

```
type    = 0x02   (GX_FOG_PERSP_LIN — perspective projection, linear curve)
fog_adj = NULL
start   = 200.0
end     = 950.0
color   = #9FCFFFFF
```

`HSD_FogInit` (0x8041b450) copies these into the runtime `HSD_Fog`. The engine
sets the type **once at load and never changes it** — `Sky_Update` lerps only
`start`/`end`/`color`, so by default all CT presets render with
perspective-linear fog. Because nothing else writes `HSD_Fog.type`, a single
mod write to it holds for the whole preset: `custom_weather`'s `fog_curve`
field exploits this to swap in exp/exp2/reverse-exp falloff per preset
(`ApplyFogCurve`). The fog *distance* values you actually see are the active
preset's (`preset[0] "Day"` = start 210 / end 665), not the descriptor's
200/950 seed.

`HSD_FogSet` (0x8041b0fc) emits `GXSetFog(type, start, end * HSD_Fog.scale,
near, far, &color)` — `start` from `HSD_Fog+0x10`, `end` from `+0x14`, the
extra `scale` multiplier from `HSD_Fog+0x20`, `near`/`far` from the current
COBJ. The disabled path passes `type = 0` (`GX_FOG_NONE`).

**Range-adjust table — unused on CT.** `HSD_FogSet` calls `GXInitFogAdjTable`
(0x803cf558) / `GXSetFogRangeAdj` (0x803cf720) only inside
`if (fog_adj != 0 && (fog_adj.flags & 7) != 0)`. CT's `fog_adj` is NULL, so the
function instead calls `GXSetFogRangeAdj(0, 0, NULL)` every frame — range
adjustment explicitly **off**. `HSD_Fog.fog_adj` (the per-column range-adjust
descriptor, loaded by `HSD_FogAdjLoadDesc` 0x8041b510, flags read by
`fn_HSD_FogAdjGetFlags` 0x80052c40) is a feature CT does not use.

### Stage fog flags

Two accessors mask the **big-endian high byte** of `StageNode.fog_flags`
(`int` at `+0x1C`, reached as `grobj->gr_data->stage_node`; both do
`lbz r0,28(r3)` then mask). CT's `fog_flags = 0x02000000`, so that byte is
`0x02`:

| Function | Addr | Returns | CT value | Gates |
|----------|------|---------|----------|-------|
| `grGetStageFogFlag1` | 0x800d1d48 | bit 0 | **0** | gates the `Map_DisableFog`/re-enable pair that wraps the backdrop draw in `Map_GX`. Entering `Map_GX` **fog is off**, so the value really decides whether the *terrain* passes after the backdrop get fog (see below). |
| `grGetStageFogFlag2` | 0x800d1d70 | bit 1 | **1** | gates fog for a separate render group (`zz_8023dc8c_`): 1 keeps fog on |

`Map_GX` (0x800d81e4) reads `grGetStageFogFlag1` at four sites, each guarding a
`Map_DisableFog`/re-enable pair around the backdrop JOBJ draw (`GrObj+0xF4`, the
first pair at 0x800d8248/0x800d8294). `Map_DisableFog` (0x800d1dcc) reads the fog
GObj (`SkyData+0x168`) and calls `HSD_FogSet(NULL)` to turn fog off; the
re-enable counterpart (0x800d1d98) restores the stored `HSD_Fog*` (kept at
`r13[+4616]`). The main terrain model is drawn **right after** this pair with no
fog calls of its own, so it inherits whatever state the pair left.

**Why bit 0 controls _terrain_ fog, not backdrop fog.** Fog is off when `Map_GX`
begins, so the backdrop itself always draws fog-free regardless of this bit. The
bit only decides the terrain that follows: with bit 0 == **0** (CT default) the
pair does `DisableFog` → draw backdrop → **`ReEnableFog`**, and that re-enable
turns fog back on so the terrain is fogged; with bit 0 == **1** both the disable
*and* the re-enable are skipped, so fog stays off and the terrain renders
fog-free (you can see clear across the map, while items/effects drawn later —
after `Fog_GX` re-enables — still fog). Setting this bit is therefore an
**un-fog-the-terrain** lever, the opposite of an "atmospheric backdrop haze," and
`custom_weather` leaves it at the vanilla 0 so terrain fog works.

### event_denseFog — no direct fog poke

The "Dense Fog" City Trial event does **not** touch `HSD_Fog` or the fog
descriptor. Its event-table slot (0x804a5528) has no-op (`blr`) start/end stubs;
the think handler `event_denseFog` (0x801118dc) is a generic duration counter
that calls `CityEvent_EndWithSkyRestore` (0x800ee660) when it expires. The fog
change is entirely a **sky-preset transition**: `CityEvent_Init` (0x800ee8a4)
reads the event's target preset index (event param struct, 0x14 stride, preset
index at `+0x04`) and calls `Sky_TransitionGlobal` → `Sky_BeginTransition`, and
the normal `Sky_Update` lerp drives the live fog to the dense-fog preset's
high-density `start`/`end`/`color`. Every custom event that changes fog works
this way — pick a preset, transition to it.

## Sky_SetupLights — what it really does

At 0x800db774, total size 0x5C bytes:

```
slwi   r0, r4, 3            ; r0 = jobj_index * 8
lwz    r3, 260(r3)          ; r3 = grobj->[0x104] (per-joint table)
lwzx   r31, r3, r0          ; r31 = joint_table[jobj_index].jobj
mr     r3, r31
bl     0x8040be00           ; HSD_JObjSetFlagsAll(jobj, 16) — render flag bit 4
mr     r4, r31
addi   r3, r30, 84          ; r3 = grobj+0x54 (collision-zone descriptor)
bl     0x800d7954           ; grScene_FindInstanceByKey(desc, jobj)
li     r4, 0
bl     0x800d7ad0           ; grScene_SetInstanceColl(zone, 0)
```

Net effect: pick a JOBJ from the stage's per-joint table at `grobj+0x104`,
force render flag bit 4 (0x10) on its whole subtree (making the billboard
**visible**), then find that JOBJ's collision zone and **disable collision on
every face of it**.

**No HSD light objects are created or modified.** This routine is a
*per-stage JOBJ visibility + collision* helper, not a light loader. For City
Trial it is called twice from `Sky_Init` (gr_kind 9) with `jobj_index = 62`
then `63` (the two indices come from the stage sub-block at
`gr_data+0x08 → +0x08`, fields `+0x04` and `+0x08`; CT's are 0x3E and 0x3F).

### The per-joint table (GrObj+0x104) and collision-zone records (GrObj+0x54)

Both structures `Sky_SetupLights` touches are general stage-load infrastructure,
not light-specific — it just *indexes into* them by joint number.

**`GrObj+0x104` — per-joint table.** Built once during `grLoadStage` by
`grparts.c` (`0x800d8b98 → 0x800d8a60`): allocates `joint_count << 3` bytes
(8 per entry) and walks the runtime stage-model joint tree (`GObj+0x28`, loaded
from `grModelCity1`) in pre-order via `HSD_JObjWalkTree`. Each 8-byte entry is
`{ JOBJ *jobj (+0x00), JOBJDesc *desc (+0x04) }`; the index is the joint's
pre-order position. Parallel per-joint `DObj*` / `MObj*` lists are built
alongside at `GrObj+0x108` / `GrObj+0x10C`. **City Trial's stage model has 317
joints, so the table has 317 entries.**

**`GrObj+0x54` — collision-zone descriptor.** An 18-word header whose `+0x10` /
`+0x14` hold the base pointer and count of an array of `0x98`-stride **collision
zone** records (the backing array is at `GrObj+0x64`), one per collidable joint.
`grScene_FindInstanceByKey` (0x800d7954) linear-scans those records matching
`record+0x00` (a `JOBJ*` key, copied from the `GrObj+0x104` table) against the
target joint. Each zone record is:

```
+0x00  JOBJ *key            - the joint this zone is parented to
+0x04  void *verts          - vertex-slice pointer
+0x08  int   vert_count
+0x0C  void *faces          - base of the 0x40-stride face sub-array
+0x10  int   face_count
```

`grScene_SetInstanceColl` (0x800d7ad0) walks `face_count` faces (stride 0x40)
and writes bit `0x40` of byte `face+0x3C` from its bool arg — the **per-face
collision-enable** flag. `Sky_SetupLights` passes `0`, so it *disables*
collision on the billboard's faces (so the glow geometry it just made visible
is not solid).

### The CT light-glow billboards (joints 62 / 63)

The two CT joints `Sky_SetupLights` enables are **map-wide light-glow / haze
overlay billboards**, not real lights. In `grModelCity1` joints 62 and 63 are
depth-1 root children at the origin (whole-map span, not localized props),
authored **out of the normal render passes** so they are off by default: joint
62's `JOBJDesc.flags` omit `OPA` (excluded from the opaque pass); both carry
`TEXEDGE` and their DObjs render with additive, depth-write-disabled material
modes (`0x40002011` = `XLU | CONSTANT | TEX0 | ALPHA_MAT`, and `0x60004011` =
`XLU | NO_ZUPDATE | CONSTANT | TEX0 | ALPHA_VTX`) over small glow textures —
joint 62: a 64×64 CMPR + a 32×32 C4-paletted; joint 63: a 32×32 C4 + a **64×32
I8 intensity map** on the additive `NO_ZUPDATE` layer (the classic light-haze
glow). `Sky_SetupLights` force-sets render-flag bit 4 to switch the pair **on**
for City Trial (live-confirmed: both joints read bit 4 set in a CT round). They are distinct from the nearby
lamp-post / traffic-light **props** (joints 56–69 carry opaque `OPA` geometry
with real world translations, e.g. the traffic-light cluster at
(-622, 69, -807)) — those props are solid geometry, the billboards are the
additive glow laid over the city.

This toggle is **preset-independent**: `Sky_SetupLights` runs only at
`Sky_Init`, never from `Sky_LoadPreset` / `Sky_Update` / `Sky_BeginTransition`,
so the glow billboards are on for the whole CT session regardless of day/night.
Day/night comes entirely from the AreaLight / sky-preset path below (color,
fog, and `light_vis_flag` → AreaLight +0x38), not from this geometry switch.

## light_vis_flag and the AreaLight Registry

Preset byte `+0x44` (`light_vis_flag`, bit 0). `Sky_LoadPreset` runs:

```
lbz   r4, 0x44(preset)
bl    AreaLight_BroadcastVisFlag (0x80079948)
```

`AreaLight_BroadcastVisFlag` walks the global object registry at
`r13[+0x538]`:

```
foreach obj in registry:
    if obj.class_ptr == r3:        ; class param matches sky's class
        obj[+0x38] = (obj[+0x38] & ~0x80) | (vis_flag_bit0 << 7)
```

Bit 0 of `vis_flag` is replicated into bit 0x80 of byte +0x38 of every
AreaLight in the registry (`rlwimi r0,r4,7,24,24`). The same +0x38 bit is
force-set to 1 inside `AreaLight_Create`, so the default state is "visible".

This broadcast is **only fired by Sky_LoadPreset** (via the same code path
called from inside `Sky_BeginTransition` after the start values are captured).
It is not interpolated each frame — it's a one-shot at preset apply.

## Fade Overlay (lbfade)

`ScreenFade` (a.k.a. lbfade) provides slot-based full-screen tint overlays.
Slot 3 is owned by the sky system.

| Address | Symbol | Purpose |
|---------|--------|---------|
| 0x80065a80 | `ScreenFade_Alloc(slot)` | Allocate a fade slot |
| 0x80065ae4 | `ScreenFade_Free(slot)` | Free a fade slot |
| 0x800665f8 | `ScreenFade_Begin(state, color, frames)` | Start a fade with target color, duration |
| 0x80066960 | `ScreenFade_GetState(slot)` | Get the per-slot state struct |
| 0x80065140 | `ScreenFade_Draw` | Full-screen quad emitter |
| 0x8006541c | `ScreenFade_GX` | GX callback |

The sky-side wrappers:

| Address | Symbol | Description |
|---------|--------|-------------|
| 0x800eef04 | `Sky_AllocFade(grobj)` | `grobj+0x714 = ScreenFade_Alloc(3)` |
| 0x800eef50 | `Sky_BeginFade(grobj, &color, frames)` | Reads `ScreenFade_GetState(3)`, calls `ScreenFade_Begin` |
| 0x800eefb0 | `Sky_FreeFade(grobj)` | Frees on scene teardown |

Fade state fields used by `ScreenFade_GX`: +0x04 base index, +0x0C id key,
+0x10 RGBA8888 base, +0x20 mode, +0x21 alpha-flag, +0x22..0x25 R/G/B/A,
+0x50 disabled-flag, +0x52 strip height, +0x54 x-base, +0x58 y-bound. The
overlay renders as zero or more 640×480 alpha-blended strips.

**Why the initial preset never fades:** `Sky_Init` calls `Sky_LoadPreset` at
its tail, and `Sky_LoadPreset` does **not** call `Sky_BeginFade`. Only
`Sky_BeginTransition` (0x800dc354) and `Sky_ApplyStoredIndex` (0x800dc4c0)
call it. To force a fade on initial sky load, replace the `Sky_LoadPreset`
call in `Sky_Init` with `Sky_BeginTransition(grobj, idx)` (or insert a manual
`Sky_BeginFade` call between them — `Sky_AllocFade` will already have run by
that point).

### lbfade as the "global darkening" mechanism

The lbfade overlay is the only mechanism in the engine that visibly darkens
**all stage geometry at once** — terrain, characters, machines, sky, fog —
because it composites a translucent quad over the final framebuffer rather
than going through HSD lights or material colors. This is what's used by:

- Event-driven preset transitions (`Sky_TransitionGlobal` →
  `Sky_BeginTransition` → `Sky_BeginFade` with the new preset's `fade_color`).
- Vanilla dark presets when entered via an event — Dark Vignette
  (`fade=0x0000003C`), Night (`00001080`), Dark Purple (`0000005A`), Dark
  Low Vis (`2800006E`) all rely on the overlay for their characteristic
  darkness, *not* on lighting changes alone.

Overlay behavior:

- **The overlay sustains at target color.** `ScreenFade_Begin` lerps from
  current to target over `frames`, then **holds at target indefinitely** —
  it does not fade back to clear. Setting it once is enough; no per-frame
  re-trigger needed.
- **HUD/UI is not covered.** The 640×480 quad is rendered before the HUD
  pass in z order, so timer/checklist/charge meters keep their original
  colors. This is exactly why event darkening reads as "scene-only."
- **It works on top of fog, not instead of it.** Fog still applies per-pixel
  during world rendering; the overlay then tints the composited result. So
  near geometry (inside `fog_start`) gets darkened by the overlay even
  though fog skipped it.

**Pitfall — the City Trial GrObj is reused across exit/re-entry.** The
same `grobj` pointer is reused for the *next* CT round, so per-stage
state guarded by `if (grobj != last_grobj)` won't reset on re-entry. The
`grobj+0x714` slot ID, however, is fresh on every entry (each
`Sky_AllocFade` → `ScreenFade_Alloc` increments the global counter at
`r13[-32248]`). Use the slot ID as the freshness signal for any state that
must reset per CT round — including, critically, "have I called
Sky_BeginFade on this slot yet?" Without it, the second entry uses a new
slot whose state is still 0x00000000 and your overlay never arms.

**Triggering it from custom code (no event needed):**

```c
// Once Sky_AllocFade has run during stage init (grobj+0x714 is non-NULL —
// true by the first per-frame tick):
u32 tint = 0x00000866;  // RGBA: dark blue, alpha = strength
Sky_BeginFade(grobj, &tint, 30);  // 30-frame lerp; then sustains
```

Alpha values from vanilla presets cluster around 60–110 (~24–43% strength)
— above that, the HUD-uncovered scene starts to look opaque rather than
dimly lit, and silhouettes lose detail. To clear the overlay, fade to
`0x00000000` (transparent) over a similar duration.

This is what `mods/custom_weather/src/custom_weather_runtime.c` uses for the
`screen_tint` field of any `CustomPresetDef` — when a custom preset becomes
active, `CustomWeatherRuntime_Tick` calls `Sky_BeginFade(grobj, &tint, 30)` once
(gated on `grobj->fade_slot_id` being non-zero). The Storm and Rain custom
presets set `screen_tint`; the rest leave it 0 (no overlay). Note the mod
clears each custom preset's vanilla `fade_color` (preset+0x10) to 0 in
`ExtendPresetArray` so the only overlay it drives is via `screen_tint`.

## CT Light Inventory & GX Hardware Allocation

The GameCube has 8 hardware light slots: `GX_LIGHT0`..`GX_LIGHT7` plus a
dedicated 9th slot for ambient (`GX_LIGHT8`, lightid `0x100`).
`HSD_LObjSetupInit` (0x803fe4b8) is more nuanced than pure FIFO:

- LOBJs with `flags & 3 != 0` (positional/directional) take slots `0..7` in
  active-list insertion order via the counter at `DAT_805de210`.
- LOBJs with `flags & 3 == 0` (ambient-only) are pinned to **slot 8**
  unconditionally (the dedicated ambient slot at `DAT_805899d0`).
- A second pass attaches specular auxiliary lights at `lobj+0x90` (the
  secondary `lightid`), filling more slots from the same counter.

Three stage-level LOBJ chains are loaded unconditionally by `Light_StageInit`
(0x800d5ed4). Two are GX-rendered (primary + secondary); the third is **not
rendered** and exists only as a default-value source for the AreaLight.

| Address | Role | GObj class | gx_link | gx_pri | LObjDesc source (file 0x… in GrCity1.dat) | AddProc |
|---------|------|------------|---------|--------|------------------------------------------|---------|
| 0x800d5fd0 (`Light_CreateForStage`)          | primary GX lights   | 1         | 0 | 1 | `(*stage_resource[+0x14])[+0x00]` (chain @ 0xC1790) | 0x800d5f3c — per-LOBJ `HSD_LObjAnim` (skips AOBJ flag 0x40000000) + stage-scale |
| 0x800d60d8 (`Light_CreateForStageSecondary`) | secondary GX lights | 20 (0x14) | 8 | 1 | `(*stage_resource[+0x14])[+0x08]` (chain @ 0xC1910) | 0x800d6094 — `HSD_LObjAnimAll`, no filter |
| 0x800d6188 (`Light_CreateAreaLightDefaults`) | **AreaLight defaults (NOT rendered)** | — | — | — | `(*stage_resource[+0x14])[+0x04]` (chain @ 0xC1870) via `grGetStageLight_Kirby` (0x800cea5c) | none — no GObj, no GX_Link |

The two GX wrappers (`Light_GX` at 0x800d5fb0 and the wrapper at 0x800d60b8)
are **byte-identical thunks** to `LObj_GX` (0x8042a22c). They differ only as
distinct callback entry points so each GObj can register its own pointer.
The actual rendering path (`HSD_LObjSetCurrentAll` + `HSD_LObjSetupInit`) is
shared.

**The primary and secondary chains are dispatched by the SAME camera,
World_CObj** — see "World_CObj GX-Link Dispatch" below. Both chains
contribute LObjs to the same world render pass via separate GObjs at
separate gx_link slots.

### Concrete CT chain contents

In `iso/files/GrCity1.dat`, all three chains are NULL-terminated
`LightGroup**` arrays of length 2:

**Primary chain (gx_link 0)** — LObjDescs at file 0xC176C, 0xC1730:

| # | flags | attnflags | color (RGBA) | position | u (light data) |
|---|-------|-----------|--------------|----------|----------------|
| 0 | 0x0004 (`AMBIENT \| DIFFUSE`) | 0 | FF FF FF 00 (white)        | NULL                  | NULL |
| 1 | 0x000D (`INFINITE \| DIFFUSE \| SPECULAR`) | 0 | FF FF D9 00 (warm white)   | (-1000, 700, 1500)    | cutoff=16, dist_func=13 |

**Secondary chain (gx_link 8)** — LObjDescs at file 0xC18EC, 0xC18B0:

| # | flags | attnflags | color (RGBA) | position | u (light data) |
|---|-------|-----------|--------------|----------|----------------|
| 0 | 0x0004 (`AMBIENT \| DIFFUSE`) | 0 | D8 D8 FF 00 (pale blue)    | NULL                  | NULL |
| 1 | 0x000D (`INFINITE \| DIFFUSE \| SPECULAR`) | 0 | FF FF FF 00 (white)        | (-1000, 1000, 1500)   | cutoff=16, dist_func=13 |

**AreaLight-defaults chain (NOT rendered)** — LObjDescs at file 0xC184C, 0xC1810:

| # | flags | attnflags | color (RGBA) | position | u (light data) |
|---|-------|-----------|--------------|----------|----------------|
| 0 | 0x0004 (`AMBIENT \| DIFFUSE`) | 0 | D8 D8 FF 00 (pale blue)    | NULL                  | NULL |
| 1 | 0x000D (`INFINITE \| DIFFUSE \| SPECULAR`) | 0 | FF FF FF 00 (white)        | (-1000, 1000, 1500)   | cutoff=16, dist_func=13 |

Per the slot rules, LOBJ[0] of each rendered chain (ambient-only) is pinned
to slot 8, and LOBJ[1] (infinite/directional) takes the next free slot 0..7 in active-list
insertion order. The defaults chain is never registered with the active GX
list, so it consumes no hardware slot.

LOBJ[1] is an **INFINITE** (directional) light, not a spot: `HSD_LObjSetupInit`
reads the light type from `flags & 3` (`clrlwi r,r,30`) and `0x000D & 3 == 1`.
Its position vector sets the light *direction*; the `cutoff`/`dist_func`
attenuation fields in the `u` block are spot/point parameters that the engine
does not apply to an INFINITE light.

**Note:** the AreaLight-defaults chain and the secondary chain have
**identical** LObjDescs — same flags, colors, positions, and spot params.
The defaults-chain values seed the AreaLight's diffuse/direction; the
secondary chain re-uses those exact values as additional GX hardware lights.

Of the 24 `bl HSD_LObjLoadDesc` call sites:
- 1 in `LObj_CreateAll` (the stage chain itself)
- 23 in menu / CSS / mode-select / HUD / effects code

**Zero gameplay-time non-stage HSD light spawns.** Every "light" you see
during gameplay (lighthouse, light tunnel, fireworks event, item glow,
projectile flash, etc.) is implemented as textured/animated geometry and
material color tricks — not a real GX light.

So during a CT session there are **4 GX hardware lights total**: primary
ambient (slot 8), primary spot (slot 0), secondary ambient (also bound to
slot 8 — second LOBJ for the slot, last writer wins per HSD setup), and
secondary spot (slot 1). The ambient slot collision means the primary white
fill and the secondary pale-blue fill compete; whichever LOBJ is later in
the active-list insertion order wins. **Roughly 5-6 hw slots remain free**
for custom additions.

### World_CObj GX-Link Dispatch

`World_CObj` (0x800b04a8) calls `CObj_RenderGXLinks` **twice** per frame
with different `cobj_links` masks (the GObj's u64 link bitmask written into
the parent GObj at +0x20/+0x24 just before each call):

| # | cobj_links high (bytes 32..36) | cobj_links low (bytes 36..40) | render_mode | Purpose |
|---|--------------------------------|-------------------------------|-------------|---------|
| 1 (@ 0x800b0630) | 0 | 0xE60 | 8 | Pre-pass — links 5, 6, 9, 10, 11 |
| 2 (@ 0x800b0740) | 0x1F | 0x0400FFFF | 7 | Main world — links 0..15, 26, 32..36 |

**gx_link 0 (primary lights) and gx_link 8 (secondary lights) are both
dispatched in the second pass.** They share the same world camera, view
matrix, and render mode. The split into two GObjs/chains is purely for
organizational and AddProc-filtering reasons (the secondary AddProc uses
`HSD_LObjAnimAll` without the AOBJ flag-0x40000000 filter and skips the
stage-scale step).

The primary `Light_GX` thunk and the secondary thunk at 0x800d60b8 both
ultimately invoke `LObj_GX` (0x8042a22c) → `HSD_LObjSetCurrentAll` +
`HSD_LObjSetupInit`. Each rebuilds the active LObj list and bakes hardware
slots from the chain held by the calling GObj's `GObj_AddObject`.

## Existing "Lights" That Aren't HSD Lights

### Lighthouse (yaku desc 68)

`Lighthouse_Create` (0x8010d228) and `Lighthouse_Init` (0x8010d260) are
plain yakumono. They iterate a per-instance joint-index list at
`param[0x0C]` and toggle bit 0 of byte at `grdata[+0x74][joint*0x140 + 0x13C]`
— a render-node visibility bit on a JOBJ. The visible "beam" is a
yellow alpha-blended cone mesh whose nodes are revealed/hidden via flag
toggles. **There is no HSD light, no GXLightID consumed, and surfaces are not
actually lit by it.**

The four anim slots in `YakumonoParam.lighthouse` (start, active, end,
inactive) drive matanim/jobj-anim swaps that animate the spinning beam.

### Light Tunnel (`YAKUKIND_LIGHTTUNNEL`)

The map has **no `Lighttunnel_*` symbols** (the C file is `gryakulighttunnel.c`
per the comment at yakumono.h:57, but its functions live anonymously inside
the gryaku block at 0x8010xxxx). The visual is a textured cylinder with
scrolling UVs. Same pattern: no HSD light involved.

### Other ambient-tinting effects

Bombs, fireworks, projectile flashes, charge auras: implemented as MOBJ
material color animation (AOBJ-driven), particle systems, and full-screen
TEV stages. None spawn HSD LObjs.

## Shadow System

City Trial shadows are **fixed straight-down textured blobs** that are
**completely independent of the lighting/sky system**. Changing the sky preset,
the AreaLight, or any LObj direction does not move, rotate, or darken a CT
shadow. There are two unrelated shadow systems in the binary:

| System | Functions | Used by | Nature |
|--------|-----------|---------|--------|
| **SimpleShadow** (blob) | `SimpleShadow_*` (0x8027ae50–0x8027c2xx) + per-entity `*_Shadow*` | **City Trial** (riders, machines, items, event actors) | textured soft-blob quad laid flat on the ground straight under the entity |
| **HSD projected shadow** (`lbshadow.c`) | `fn_shadowRendering` (0x8007ade8), `fn_makeShadow` (0x8007b284), `HSD_Shadow*` (0x8041cf1c+) | **Top Ride only** | real silhouette projected from a scene light's POV |

The lighting-aware path exists but is **not on the CT path** — `fn_makeShadow`
is reached only from a Top Ride mode-init routine (`zz_802823fc_` at 0x802823fc).
There it asserts "can't find shadow light", reads a light's direction via
`HSD_LObjGetPosition − HSD_LObjGetInterest` (normalized), and builds a
projection camera from it. CT never invokes it.

### SimpleShadow mechanics (the CT path)

- **One scene-wide manager GObj**, not one render GObj per entity.
  `SimpleShadow_CreateManager` (0x8027b294) zeroes the 0xCC-byte manager at
  `0x8055EFC0`, does `GObj_Create(class 0x18, pri 0xF)` +
  `GObj_AddGXLink(SimpleShadow_GX, gx_link 2, pri 1)` + an AddProc at pri 0x15.
  Called from `SceneLoad_3D` (0x80014700, the shared 3D-gameplay loader — the CT
  path) and the title/menu loaders.
- **Per-entity shadow GObjs** are lightweight class-8 objects holding only a
  blob JObj, created by `SimpleShadow_CreateGObj` (0x8027b418) and linked into
  the manager's list. The render flag lives at shadow-node `+0x30`
  (`SimpleShadow_SetRenderEnable`/`Disable`/`GetRenderFlag` at 0x8027b524/
  0x8027b534/0x8027b544).
- **Ground find = down-raycast.** Placement uses `EnvColl_Raycast` (0x800d1ac4)
  to drop the entity's position to the ground; the ray is a fixed **±Y** segment
  (`EventActor_ShadowInit` 0x80200208 builds `pos ± offset·+Y`), never a light
  vector.
- **Size fades with height.** Scale = base × `(maxHeight − height) / maxHeight`
  where `height` is the entity's distance above its ground point; the shadow is
  culled past `maxHeight`. `SimpleShadow_UpdateSize_` (0x8027b568) writes the
  scale, `SimpleShadow_UpdatePos_` (0x8027b588) the position;
  `CityItem_UpdateShadowSizeAndVis` (0x80261aa8) is the item variant.
- **Render** = `SimpleShadow_GX` (0x8027ae50) walks the manager's list and
  `HSD_JObjDispAll`s each enabled blob (wrapped in `HSD_FogSet`). The blob
  material/texture/blend is a static descriptor set by `Shadow_MObjCallback`
  (0x8027bd68) — a constant, not lighting-derived.
- **Per-entity enable gating** reads entity state bits:
  `Rider_UpdateSimpleShadowRender` (0x80195800) needs `RiderData+0x825`/`+0x821`
  bits; `Machine_UpdateSimpleShadowRender` (0x801d108c) reads
  `MachineData+0xc37`/`+0xc30`; `Item_UpdateSimpleShadowRender` (0x80261a54)
  needs the item's shadow GObj plus `item+0x35a`/`+0x358`. Event actors toggle
  via `EventActor_SetShadowActive`/`ClearShadowActive` (0x802000ac/0x802000c0,
  bit 4 of `EventActorData+0xb0b`; handle at `+0x954`).

**Implication for light features:** light-responsive shadows in CT would be a
*new* feature — either feed a light direction into the SimpleShadow placement
(replace the fixed `±Y` ray and slant the quad) or port the Top Ride projected
path into the CT scene. Neither exists today.

## Spawning a Custom Local Light (recipe)

The minimum HSD calls to put a real positioned light into the City Trial
scene (worked example, mirroring `CitySettings_CreateLObj` and similar
menu code):

```c
#include "obj.h"

// In hoshi link.ld (already exported):
//   LObj_LoadDesc      = 0x80400238  (HSD_LObjLoadDesc)
//   LObj_SetPosition   = 0x803FFC64
//   LObj_SetInterest   = 0x803FFD2C
//   LObj_GX            = 0x8042A22C
//
// HSD_LObjAddCurrent is NOT needed: LObj_GX → HSD_LObjSetCurrentAll rebuilds
// the active list every frame from the chain attached via GObj_AddObject.

static LObjDesc s_my_desc =
{
    .flags        = 0x000D,    // INFINITE | DIFFUSE | SPECULAR. Low 2 bits = type
                               // (0 AMBIENT, 1 INFINITE, 2 POINT, 3 SPOT — the type
                               // HSD_LObjSetupInit dispatches on via clrlwi r,r,30);
                               // bit 2 DIFFUSE, bit 3 SPECULAR, bit 4 ALPHA, bit 5 HIDDEN.
    .attnflags    = 0x0000,    // bit 0 = RAW_PARAM (u.attn is a raw 6-float block)
    .color        = { 0xFF, 0xC0, 0x80, 0xFF },  // warm orange diffuse
    .position     = NULL,                         // populated via LObj_SetPosition
    .interest     = NULL,                         // populated via LObj_SetInterest
    // .u.point / .u.spot / .u.attn — choose one based on light type
};

void SpawnPointLight(const Vec3 *world_pos, const Vec3 *interest)
{
    GOBJ *g = GObj_Create(38, 32, 0);          // class=38 mirrors menu code
    LOBJ *l = LObj_LoadDesc(&s_my_desc);
    GObj_AddObject(g, HSD_OBJKIND_LOBJ /* =2, see obj.h:184 */, l);
    GObj_AddGXLink(g, LObj_GX, /*gx_link*/ 0, /*gx_pri*/ 0);
    LObj_SetPosition(l, (Vec3 *)world_pos);
    LObj_SetInterest(l, (Vec3 *)interest);
    // First call to LObj_GX next frame will pick this LObj up via
    // HSD_LObjSetCurrentAll and bake it into a free hw slot.
}
```

### Visibility — the engine aggregates light_mask globally

There is **no per-material `light_mask` baked into stage geometry**. The
standard `HSD_SetupChannelMode` path (0x803f7d44 — used by every world MObj)
does not read a static `light_mask` field. Instead, just before emitting
`GXSetChanCtrl` it queries three globals that are rebuilt every frame at
the top of `HSD_LObjSetupInit`:

| Global         | Getter                               | Source                                    |
|----------------|--------------------------------------|-------------------------------------------|
| `DAT_805de214` | `HSD_LObjGetLightMaskDiffuse`  (0x803fdb14) | OR of `lobj.lightid` for every active LOBJ with `flags & LOBJ_DIFFUSE`  |
| `DAT_805de218` | `HSD_LObjGetLightMaskSpecular` (0x803fdb2c) | OR of `lobj.lightid` for every active LOBJ with `flags & LOBJ_SPECULAR` |
| `DAT_805de220` | `HSD_LObjGetLightMaskAlpha`    (0x803fdb24) | OR of `lobj.lightid` for every active LOBJ with `flags & LOBJ_ALPHA`    |

`lightid = HSD_Index2LightID(slot)` — `1 << slot` for slots 0..7 and `0x100`
for slot 8.

**Implication for spawning custom lights:** an LOBJ added to the active list
before `HSD_LObjSetupInit` runs that frame will:

1. Be assigned the next free slot (0..7 in insertion order; slot 8 if it's
   ambient-only via `flags & 3 == 0`).
2. Have its `lightid` OR'd into the appropriate channel global automatically.
3. Be visible to **every stage MObj whose rendermode enables lighting** —
   i.e. has `RENDER_DIFFUSE` (bit 2) set — without any chan-ctrl patching.
   **This is the critical qualifier:** in City Trial only ~5 of ~180 terrain
   materials are DIFFUSE; the rest render unlit and a new light does nothing to
   them (see "City Trial geometry is mostly unlit" below).

In other words: for the materials that opt into lighting, just call
`LObj_LoadDesc` + `GObj_AddObject(g, HSD_OBJKIND_LOBJ, l)` +
`GObj_AddGXLink(g, LObj_GX, ...)` with the right flags — no per-material work
needed. Riders/machines/items (separately flagged, lit) and the DIFFUSE terrain
subset pick it up; the unlit city geometry does not.

**Caveat:** a stage MObj could in principle override the global path by
carrying its own pre-built `MatColorChan` chain (read by `HSD_SetupChannel`
at +0x14/+0x1c/+0x28). `MObjLoad` (0x803f9f04) only copies a 0x14-byte
HSD_Material — no chan-ctrl chain is built — and the only callers of the
chain walker `HSD_SetupChannelAll` are at 0x8041d32c (a shadow/scratch
path, not stage geometry). So every CT stage MObj does go through the global
aggregation path — but `HSD_SetupChannelMode` only enables the lit channel for
the DIFFUSE-flagged ones; the rest take the `GX_DISABLE`d channel.

### City Trial geometry is mostly unlit — the DIFFUSE subset

The aggregation above only matters for materials that **enable lighting**.
`HSD_SetupChannelMode` (0x803f7d44) routes a material through the lit color
channel (the one that reads the global diffuse mask, `DAT_805de214`) only when
its rendermode has `RENDER_DIFFUSE` (bit 2, `0x4`) set; otherwise it takes the
**unlit** channel (`GX_DISABLE`d) and color comes straight from baked per-vertex
colors (`RENDER_VERTEX`) or the material register (`RENDER_CONSTANT`),
texture-modulated. No hardware light — existing or newly spawned — touches an
unlit material.

In `GrCity1Model.dat` the terrain is overwhelmingly unlit:

- **`RENDER_DIFFUSE` on 4 of ~180 terrain MObjs; `JOBJ_LIGHTING` on 3 of 317 joints.**
- Render-mode tally (terrain): `0x4012` VERTEX ×76, `0x2011` CONSTANT ×19,
  `0x40002011` CONSTANT|XLU ×14, `0x2012` VERTEX ×13, `0x60002011` ×11,
  `0x2015` CONSTANT|**DIFFUSE** ×4 (the only lit ones), plus a lone
  multi-textured `0x00ac00ad` CONSTANT|DIFFUSE|SPECULAR surface.
- **Vertex normals on only 6 of 208 terrain POBJs.** Diffuse/positional GX
  lighting is `matColor × (ambient + Σ light·(N·L))`; with no `N` the light term
  is zero, so even *forcing* `RENDER_DIFFUSE` on a normal-less surface can't make
  it respond to any hardware light — it just darkens to the ambient-only term.
  The 6 normal-bearing POBJs are exactly the DIFFUSE ones. Meanwhile **151 of 208
  POBJs carry baked per-vertex colors (DIRECT RGBA4, inline in the display list)**
  — that baked color *is* the city's appearance, and would be the only usable
  lever for a positional look.

So **adding an LOBJ lights riders/machines/items and those ~5 DIFFUSE city
surfaces — not the buildings, roads, or ground.** This reconciles the two
consumer mods: `custom_weather`'s `ApplyTerrainTint` (rewriting `*stc_main_light`)
only nudges that DIFFUSE handful, and `lightning.c` flashes terrain through the
**fog/EFB color** precisely because the unlit majority ignores hardware lights.
For a city-wide visual change you must drive the baked path (fog color, EFB
clear, lbfade overlay) or **rewrite the baked vertex colors directly** (those
live in the POBJ display lists, so a `DCFlushRange` is required after editing for
the GP to see the change) — flipping `RENDER_DIFFUSE` does *not* help, because the
normal-less geometry still computes no diffuse light.

### How to track a moving entity

`LObj_SetPosition` sets the light to a static Vec3 (allocates a backing
WObj on first call). To follow a JObj (player, machine, etc.), you'd need
to update the WObj's position each frame — either by writing the Vec3
directly into the WObj on every think tick, or by attaching the WObj to a
JOBJ via the broader `WOBJ` API (out of scope here).

## AreaLight Initial Values (defaults chain)

Before any sky preset is applied, `AreaLight_StageInit` (0x800ef618) seeds
the live AreaLight at `grobj+0x718` with default values fetched via
`Light_GetAreaLightDefaults` (0x800d61e8):

```
AreaLight_StageInit
  ├─ Light_GetAreaLightDefaults(&out_amb, &out_inf_color, &out_inf_pos)
  │     reads r13[+0x5F0] (first non-hidden ambient LOBJ in defaults chain)
  │     reads r13[+0x5F4] (first non-hidden infinite/spot LOBJ in defaults chain)
  │     reads r13[+0x5F8] (defaults chain head — used as null check)
  └─ writes those values into a stack AreaLightData, then AreaLight_Create
```

The same getter is also called by `zz_800ef70c_` (0x800ef70c) — the per-zone
AreaLight updater (see "Per-zone AreaLights" below). If
`r13[+0x5F8] == 0` (no defaults chain available, e.g. menu/CSS scenes),
the getter writes (0,0,0,0xFF) opaque-black for both colors and a fixed
default direction.

### Per-zone AreaLights (City Trial: 128 zones)

Beyond the single sky-driven AreaLight at `GrObj+0x718`, City Trial builds
**128 per-zone AreaLights** — one per collision region — carrying per-region
color data independent of the sky preset. Walking the registry in a CT round
returns 129 AreaLights (128 zone lights plus the one global). They are, however,
**render-inert**: nothing consumes their colors for the GX pass (detailed
below), so they are not a usable lever for tinting the map.

**Identity & registry.** Every AreaLight, global and per-zone, is created by
`AreaLight_Create` (0x80079428) and pushed onto one global registry list whose
head is `*(*(0x805DD618))` (`r13[+0x538]`). Each registry node:

```
+0x00  AreaLight *next   - push-front linked list
+0x04  u32 kind          - 0 = global (sky-driven), 3 = per-zone
+0x08  u32 header
+0x0D  u8  flags         - bit1 -> ambient writer, bit2 -> directional writer
+0x10  GXColor color     - diffuse/ambient
+0x14  GXColor hw_color  - specular/directional
+0x18  Vec3    direction
```

`kind` indexes the dispatch table at `0x8049ac60` (3 words each: walk / free /
lerp). **kind 3's walk handler is NULL** (`0x8049ac84 = {0, 0x8007acc8,
0x8007ad08}`) where kind 0's is `0x8007a51c`. `AreaLight_RegistryWalk`
(0x8007a2c0) guards on the walk handler, so **the 128 zone lights are skipped by
the per-character lighting accumulator** — only the kind-0 global tints
characters/machines (see "AreaLight → Character/Rider Bridge"). No other code
path samples a kind-3 zone's `color`/`hw_color` during rendering either:
**the zone lights are render-inert.** Overwriting a zone's color fields has no
visible effect on terrain, geometry, or riders — they are initialized and
updated, but nothing reads the result to emit pixels.

**Allocation.** Zones are carved from the stage collision pool by `grcoll.c`
(`zz_800d6774_`), sized by the grData CollisionNode descriptor word `[0xb]`
(parallel to the `[5]`-counted 0x98-stride collision zones at `GrObj+0x54/+0x64`
— a separate, independently-counted set). They are **not** a simple
`GrObj+offset` array; reach them through the registry above.

**Per-region values (preset "Day").** Most zones default to
`color=#9696AA hw=#8C8C96 dir=(-0.49,0.49,0.73)`, flag `0x3b` — the direction is
exactly the normalized defaults-chain infinite-light vector `(-1000,1000,1500)`,
i.e. these zones sampled "unset" and pulled `Light_GetAreaLightDefaults`. A
handful of **accent zones** carry distinct `hw_color`s — `#00FF00` (green),
`#B300FF` (magenta), `#DDC1EA` (lavender) — with flag `0x3c` (directional path).
This is authored per-district color data, but it is render-inert (below) and so
never reaches the screen.

**Update & lerp.** `zz_800ef70c_` (0x800ef70c; dispatch pair
`0x804a4078 = {create, update}`) refreshes one zone per call at setup / event
re-init (not per-frame): `zz_800d78b4_` samples the zone joint transform
(`record+0xE0`, matrix column 2 lanes `+0x08/+0x18/+0x28`) as RGB; if ≈black it
pulls `Light_GetAreaLightDefaults`, else negates the sample (constant `-1.0`);
then writes via `zz_80079648_` (bit1, ambient) or `zz_800796f8_` (bit2,
directional) and pushes the joint translation (`HSD_MtxGetTranslate →
zz_8007ad38_`). Between zone states the kind-3 lerp handler (0x8007ad08) is
driven by the City Trial zone-group state machine around `Gr_StateChange`
(0x800f5548) — **event/animation-driven, not lerped by `Sky_Update`**. Zone
lighting is therefore independent of the sky-preset system.

**Not a usable recolor lever.** Despite the per-region color data, the zone
AreaLights are render-inert (above): the registry walk skips kind-3 and nothing
else consumes their color fields, so writing a zone's color — or its joint
matrix-column-2 sample source (`record+0xE0`) — changes the struct but produces
no on-screen change. City Trial bakes terrain shading into TEV vertex colors, so
to tint a *region* visually there is no per-zone hook; you must use the global
levers KAR actually renders — the fog color / EFB clear (per-pixel,
distance-banded) and the lbfade screen overlay. (`custom_weather` recolors the
world through `*stc_main_light` + the fog/overlay path for exactly this reason.)
To make a *rider* pick up a custom color, register a kind-3 walk handler at
`0x8049ac84` or add a kind-0 AreaLight so `AreaLight_RegistryWalk`'s
nearest-lights accumulator picks it up.

Once the AreaLight is seeded, the sky-preset system overwrites it: every
frame, `Sky_Update` calls `AreaLight_LerpToLive` to interpolate from the
captured "start" values to the target preset's `AreaLightData`. So the
defaults chain only matters for the **first frame of the stage** before
the first preset apply — but it must be loaded for the AreaLight to exist
at all.

## AreaLight → Character/Rider Bridge

The live AreaLight at `grobj+0x718` (and the global registry at `r13[+0x538]`
that `AreaLight_Create` populates) **does not feed any LOBJ**. The two
systems are independent in static code. `HSD_LObjSetupInit` reads colors
exclusively from `LOBJ+0x10` / `LOBJ+0x14`; it never consults the AreaLight
registry. The per-LOBJ "update" vtable slot is `LObjUpdateFunc`
(0x803fdbb0) — an AOBJ animation hook, not an AreaLight bridge.

Instead, the AreaLight registry is consumed by **per-character / per-rider
lighting state**:

```
Sky_Update (per frame)
   └─ AreaLight_LerpToLive → AreaLight_Lerp → writes grobj+0x718
                                                       │
                                                       │ (one entry in registry
                                                       │  at r13[+0x538])
                                                       ▼
   AreaLight_RegistryWalk (0x8007a2c0)
   ├─ called from Rider_UnkThink (0x8018e9a8 → 0x80190340)
   │     → walk consumer = (rider+0x294); inserts records into rider+0x318
   ├─ called from 0x801d6c00 (per-object pose update)
   │     → walk consumer = (obj+0x300); inserts records into obj+0x400
   └─ called from Machine_Create (0x801c5888 → 0x801d6bd4)
         → machine init
```

The dispatcher indexes the class table at `0x8049ac60` (4 kinds × 3
fn-ptrs: walk / free / lerp) by the AreaLight's `kind` (registry node +0x04).
The kind-0 walk worker `AreaLight_InsertSorted` (0x80079a60) inserts a sorted
record (max 9 entries) into a 5-int-stride array on the consumer struct, with
the entry count at offset 0xC8 from the consumer base.

**Only the kind-0 global AreaLight reaches characters.** `AreaLight_RegistryWalk`
guards on a non-NULL walk handler, and **kind 3 (the 128 per-zone AreaLights)
has a NULL walk handler** (`0x8049ac84 = {0, 0x8007acc8, 0x8007ad08}`), so the
walk skips every zone light. The per-character nearest-lights accumulator
therefore only ever sees the single sky-driven global at `GrObj+0x718`; the zone
lights reach neither riders nor any render path and are inert (see "Per-zone
AreaLights").

**Practical consequence:** sky-preset color shifts affect **character/machine
shading** during render. Stage hardware LOBJs (the chains loaded by
`Light_CreateForStage` and `Light_CreateForStageSecondary`) keep their
original colors throughout the session — they are *not* repainted by sky
transitions. Visual changes you see on terrain across a preset transition
come from the fog blend (`HSD_FogSet` per-pixel) and the EFB clear color
(`World_CObj`), not from any per-frame light update.

## Known Sky Presets

| Index | Visual Name | Usage |
|-------|-------------|-------|
| 0     | Day | CT initial random, Top Ride default |
| 1     | Midnight | Event sky |
| 2     | Light Fog | Event sky |
| 3     | Dusk 2 | Event sky |
| 4     | Dusky Clouds | Event sky |
| 5     | Dark Vignette | Event sky |
| 6     | Day 2 | Event sky |
| 7     | Blue Sky | Event sky |
| 8     | Pink Sky | Event sky |
| 9     | Dense Fog | Event sky (fog event) |
| 10    | Foggy | CT initial random |
| 11    | Dusk | CT initial random |
| 12    | Night | CT initial random |
| 13    | Gray Sky | Stadium transition type 0 |
| 14    | Dark Purple | Stadium transition type 1 |
| 15    | Red Vignette | Air Ride courses (stages 10-22) |
| 16    | Dark Low Vis | Air Ride stage 23 |

Indices 17+ are appended at runtime by the `custom_weather` mod — see
"Custom Weather Mod" below.

## Custom Weather Mod

`mods/custom_weather/` replaces vanilla sky selection in City Trial and adds
its own presets. It is the consumer-side companion to everything above.

### Preset-array extension

`ExtendPresetArray` (`custom_weather.c:227`) copies the 17 vanilla
`SkyPresetEntry` records out of the stage file into a static
`extended_presets[WEATHER_TOTAL]` buffer, appends the custom presets, then
**repoints** the stage sub-header so the game itself sees the longer array:

```
SkyBlock *sky_block = grobj->gr_data->sky_block;
SkyPresetSubHeader *sub_header = sky_block->preset_header;
sub_header->preset_array = extended_presets;   // was: stage-file preset array base
sub_header->preset_count = WEATHER_TOTAL;       // was: 17 — now the extended count
```

This is the `SkyPresetSubHeader` (array base / count) pair documented
under "Sky Preset Entry" and read by `Sky_GetPresetCount` (0x800d5414) and
`Sky_BeginTransition`. After the repoint, vanilla code (event transitions,
`Sky_BeginTransition`, the debug selector) can index custom presets too.

Each custom entry is **cloned from a vanilla `base_preset`** (so it inherits
that preset's AreaLightData flags/attn/header) then overrides
`fog_color`/`fog_start`/`fog_end`/`sky_ambient_color`, the AreaLight
`color`/`hw_color`/`direction`, and `light_vis_flag`; `fade_color` is zeroed
and `transition_frames` forced to 1 (snap, no vanilla fade — the mod drives
its own overlay via `screen_tint`).

### Selection hooks

Two `CODEPATCH_HOOKCREATE` sites in `custom_weather.c` redirect vanilla
initial-sky selection to `CustomWeather_OverrideSky`, which extends the array
then uniformly picks a **random enabled** preset (falling back to Day if none
are enabled) and calls `Sky_SetPresetIndex`:

| Hook addr  | Where (inside `Sky_Init`, 0x8010f114) | Vanilla behavior replaced |
|------------|----------------------------------------|---------------------------|
| 0x8010f1a4 | City Trial (gr_kind 9) random block    | `Gm_Roll` over {0,10,11,12} → `Sky_SetPresetIndex` |
| 0x8010f224 | City Trial Free Run (gr_kind 52)       | hardcoded preset 0 |

### Per-frame customization runtime (`custom_weather_runtime.c`)

A third hook at **0x800ce648** — the instruction immediately *after*
`bl Sky_Update` (0x800dc640) in the CT per-frame driver — calls
`CustomWeatherRuntime_Tick(grobj)` (r31 holds grobj across the `bl`; the original
`lwz r0,4(r31)` is re-run by the trampoline). Running after `Sky_Update`
means the mod's writes layer on top of the per-frame sky writes instead of
being clobbered. The tick early-returns on any non-`GR_CITY1` ground. On a
preset change it applies several optional layers from the active
`CustomPresetDef`; the global fog scale and the animation run every frame:

| Layer | Mechanism | Code |
|-------|-----------|------|
| Terrain re-tint | writes `(*stc_main_light)->color`/`hw_color` (the primary chain's INFINITE LOBJ — terrain's light; sky presets never touch it) | `ApplyTerrainTint` |
| Ambient (slot-8) re-tint | writes the slot-8 ambient LOBJ resolved lazily from `stc_lobj_hw_slot_table[8]` (HW table lags think by a frame, so it retries) | `ApplyAmbientTint` |
| Fog curve | writes `HSD_Fog.type` (`fog_curve` → `GXFogType`); `Sky_Update` never lerps type, so one write per preset holds | `ApplyFogCurve` |
| Screen overlay | `Sky_BeginFade(grobj, &screen_tint, 30)` once per preset activation, gated on `grobj->fade_slot_id != 0` | `CustomWeatherRuntime_Tick` |
| Global fog distance | every frame: `HSD_Fog.scale = CustomWeather_GetFogScale()` — the menu-driven multiplier on the fog far wall (`end * scale`); covers vanilla presets too | `CustomWeatherRuntime_Tick` |
| Lightning | per-frame strike loop from `def->lightning`; lerps fog/EFB/its own LOBJ toward the flash color | `Lightning_Tick` (`lightning.c`) |
| Rain | per-frame world-space GX line field from `def->rain` | `Rain_Tick` (`rain.c`) |
| Wind | per-frame global wind vector from `def->wind`; slants the rain/hail, nudges airborne items, pushes gliding machines | `Wind_Tick` (`wind.c`) |
| Hail | damaging hailstone clouds over each machine, layered on the rain (global menu, gated on rain being active) | `Hail_Tick` (`hail.c`) |
| Puddles | per-frame field of roaming ground pools from `def->puddles` that drag machines | `Puddle_Tick` (`puddle.c`) |

The effect layers — **lightning**, **rain**, **wind**, **hail**, and **puddles** —
are each a self-contained module driven from this tick, independent of one another
and of the static sky/fog/light fields, so a preset can carry any combination.
Wind is ticked before rain and hail so both read the current frame's wind vector
for their slant. `event_sky.c` is a related sibling but installs its own boot hook
rather than running from this tick (see "Event sky suppression").

### Lightning (`lightning.c`)

A preset with `lightning.enabled` set gets a strike loop: long random lulls
punctuated by a brief, bright flash. KAR stage geometry doesn't read HSD
hardware light colors, so the **visible terrain flash comes from punching the
fog color and the EFB clear color** (`HSD_Fog.color` + `*stc_global_fog_color`)
toward the flash color on a triangular envelope, plus pulling `fog.start` in so
the brightness reaches near geometry rather than only the distance band. A spare
INFINITE LOBJ flashes alongside to catch characters/machines (which *do* read
hardware lights). All of these are safe to leave un-restored because
`Sky_Update` rewrites fog/EFB every frame, so the preset values return on their
own once a strike ends.

The look/cadence is per preset via the `LightningDef lightning` field
(`custom_weather.h`): `flash_color`, `flash_frames` (envelope length),
`min_lull`/`max_lull` (frames between strikes, randomized in that range), each
taking a `lightning.c` module default when left 0 (near-white flash, 18-frame
envelope, 3–7 s lulls). It composes with any preset — on its own or alongside
the rain layer.

Driven from the same per-frame tick as rain:

- `Lightning_SetActive(&def->lightning)` — latched on each preset change (NULL
  or `enabled == 0` turns it off); resolves defaults and re-arms the strike
  timer so the first strike fires a short beat into the preset.
- `Lightning_Tick(fog)` — every frame: advances the lull/flash state machine and,
  during a strike, lerps the passed `HSD_Fog`, the EFB clear, and its own LOBJ
  toward the flash color.
- `Lightning_Reset()` — called from `ResetPerStage` on CT teardown; re-arms the
  timer and drops the cached LOBJ handle (the engine frees the GObj itself).

`lightning.c` is a self-contained sibling of `rain.c`: it owns its LOBJ — one
INFINITE light (`flags=0x0D`) spawned overhead via the "Spawning a Custom Local
Light" recipe (`GObj_Create(38,32,0)` + `LObj_LoadDesc` + `GObj_AddObject` +
`GObj_AddGXLink(LObj_GX,0,0)`), illuminating characters/machines (which read GX
hardware lights) but not terrain (which does not) — its timing, and its flash
logic.

**Visible bolts (opt-in).** Beyond the flash, a preset can set `lightning.bolt`
(`LightningBoltMode`: off / augment / replace) to draw a jagged top-to-ground
polyline as depth-tested GX line segments on the world camera's XLU pass — occluded
by stage geometry exactly like the rain — with a POINT LOBJ at the bolt's midpoint
to light riders near the strike. Bolt geometry is regenerated per strike in world
space (so every split-screen camera draws the same bolt from its own pass), and the
flash and bolt strobe together on one shared multi-flicker envelope. `augment` keeps
the screen flash, `replace` draws only the bolt (terrain stays dim). The global
**Lightning Bolts** menu (`Auto` / `Off` / `Force`) overrides the per-preset `bolt`
across every preset: `Auto` honors each preset, `Off` suppresses all bolts, `Force`
adds a bolt to every lightning preset.

### World-space rain (`rain.c`)

A preset with `rain.enabled` set gets a field of falling translucent line
segments drawn **in the stage** — not as a flat screen overlay. The rain is
immediate-mode GX geometry on the world camera's pass, depth-tested against the
scene, so buildings and terrain occlude the drops behind them.

The look/motion is per preset via the `RainDef rain` field (`custom_weather.h`):
`color`, `density`, `fall_speed`, `line_width`, `streak`, with each numeric field
taking a `rain.c` module default when left 0. The horizontal slant is **not** a
RainDef field — `rain.c` reads the global wind vector (`wind.c`) every frame, so a
preset's `WindDef` drives both the rain's slant and the rest of the weather. Any
preset can carry rain — `WEATHER_RAIN` (a steady shower), `WEATHER_STORM` (a
heavier, wind-blown downpour that composes with its lightning), `WEATHER_BLOOD_RAIN`
(a deep-red downpour with lightning, no wind), and `WEATHER_PUDDLES` (a light
drizzle over its ground pools) all set it.

Driven from the same per-frame tick as the animation runtime:

- `Rain_SetActive(&def->rain)` — latched on each preset change from
  `CustomWeatherRuntime_Tick` (NULL or `enabled == 0` turns rain off); resolves each
  0 field to its `rain.c` default and clamps `density`/velocities.
- `Rain_Tick()` — every frame: seeds the drop pool once, lazily creates the
  render GObj, and advances the shared fall/wind drift.
- `Rain_Reset()` — called from `ResetPerStage` on CT teardown; the engine frees
  the world GObj itself, so this only drops the cached handle.

**Render path.** `Rain_Ensure` creates a GObj (`GObj_Create(201, 25, 0)`) and
attaches a GX callback on **gx_link 0** (the world camera's link, the same one
the spare LOBJ and the hypernova debug cone use). The callback runs only on the
**XLU sub-pass** (`pass == 1`) so it blends over opaque geometry, and emits the
whole field as one `GXBegin(GX_LINES, …, density*2)` batch. GX state
mirrors the inline `GX_DrawLine` / hypernova-cone recipe: flat per-vertex color
(no texture/lighting), `GX_BM_BLEND` src-alpha, **`GXSetZMode(GX_ENABLE,
GX_LEQUAL, GX_DISABLE)`** (depth-tested but not depth-writing — this is what
makes geometry occlude the rain), `GX_CULL_NONE`, and `GXSetLineWidth`. World
coordinates are placed by loading the active camera's view matrix as the
position matrix: `GXLoadPosMtxImm(&COBJ_GetCurrent()->view_mtx, GX_PNMTX0)`.

**Camera-following toroidal box.** The drops are a fixed pool of random offsets
in a cube of edge `RAIN_BOX` that re-centers on the camera every frame, so you
can never outrun the rain. All drops share one `drift` vector (advanced per
frame by the preset's resolved velocity — dominant `-Y` fall plus `wind_x`/`wind_z`);
each drop's world position is `eye + wrap(offset + drift) − RAIN_BOX/2` per axis,
where the single-subtract `wrap` recycles any drop that leaves the box. The
camera eye is derived from the view matrix as a rigid inverse
(`eye = −Rᵀ·t`), so the same pool serves every split-screen viewport from its
own GX pass with no per-camera state. Because positions are recomputed from the
camera each frame and the matrix is the same one the world is drawn with, the
rain stays correctly depth-sorted while the field tracks the player.

**Performance & the alternative.** A preset's `rain.density` (clamped to the
`RAIN_MAX_DROPS` pool cap) is the number of GX line primitives drawn and the
dominant cost lever; the batch is re-emitted once per camera, so 3–4P
split-screen multiplies it. If this
immediate-mode path proves too expensive, the **HSD point-particle pool**
(`particle-system.md`) is the natural fallback: its render driver
`psRenderParticles` (0x80433f00) already draws velocity-stretched `GX_LINES`
streaks (`Ptcl_EmitStreak`, 0x80436460) and `GX_POINTS` in world space from its
own decoupled tick/render walks, so rain could be emitted as particles and let
the engine batch them. The trade-off is that the pool copies a static
`PtclDesc` template from the fixed ROM descriptor table
(`descTable[type][sub]`, base 0x8058c708) — there is no clean runtime
descriptor registration — so it is less flexible to tune from mod code than
this self-contained module. Worth doing only if the GX-line approach measures
poorly.

### Wind (`wind.c`)

A single global horizontal wind vector that several systems read each frame so
they all blow the same way: `rain.c`/`hail.c` slant their streaks/stones to it,
airborne City Trial items are nudged sideways (`Wind_ApplyToItems`), and gliding
machines are pushed (`Wind_ApplyToMachines`, scaled by how airborne they are and
their glide stat). The vector is not static — its speed pulses (gustiness) and its
heading wanders (chaos), each a smoothed random walk around the preset's base (a
fresh target rolled every period, eased toward each frame) so it reads as gusting
rather than per-frame jitter.

Per preset via `WindDef wind` (`custom_weather.h`): `speed`, `heading` (degrees,
0 = +Z / 90 = +X), `gustiness`, `chaos`, each numeric field taking a `wind.c`
default when 0. The module owns no GObj and installs no hook — it is driven from
the runtime tick (`Wind_SetActive` on preset change, `Wind_Tick` each frame,
`Wind_Reset` on teardown) and exposes `Wind_GetVector` for the rain/hail slant.
The global **Wind** menu layers a strength multiplier, a **Randomize Direction**
toggle (rolls the base heading per round), and per-consumer **Affect Machines** /
**Affect Items** gates over the preset.

### Hail (`hail.c`)

A damaging layer that rides on the rain. While a rain preset is active and the
**Hail** menu (under Rain) is on, each machine carries a tight box of real
world-space hailstones that fall under gravity plus the global wind slant. Unlike
the rain — a camera-relative field with no persistent per-drop position — a
hailstone is a true world point, so the hit is honest: when one enters a machine's
body sphere it deals **1 damage** (`Machine_GiveDamage(md, 1, mg)`) and respawns at
the top of its box. A short per-machine cooldown caps the rate to chip damage; a
fully eroded machine dies through the engine's own death path.

The stones fall in true world space (they don't slide with a moving machine), but
the box that re-seeds them follows the machine, so it keeps driving into fresh hail
and can't outrun the storm by speed. It can by **cover**: a machine with stage
geometry overhead has its whole cloud suppressed. Shelter is found by casting a ray
**down** from the top of the playable volume (`stage_node->oob_max.Y`) to just
above the machine — a down-cast detects a roof by its walkable top face, so it works
regardless of triangle sidedness; any hit means something is overhead. The probe is
throttled (every few frames) and cached per machine. Stones draw as short, thick,
icy GX lines on the world XLU pass (`GObj_Create(204, 28, 0)`), depth-tested like
the rain. Hail holds no per-preset config — it is a single global menu knob (Off /
Light / Normal / Heavy) gated on `Rain_IsActive()`, scaling stones per machine, and
is driven from the tick via `Hail_Tick` / `Hail_Reset` (no `Hail_SetActive`).

### Ground puddles (`puddle.c`)

A preset with `puddles.enabled` gets a roaming field of shallow pools that lie on
the City Trial ground and drag any machine driving through them. Each pool slot
runs an independent lifecycle — dormant → fade in → hold → fade out — on random
timers, re-rolling a fresh spot each time it wakes by raycasting straight down and
keeping only near-flat hits (so pools never climb walls), then laying a translucent
disc flush in the surface plane via a tangent basis from the ground normal. Discs
draw on the world XLU pass (`GObj_Create(203, 27, 0)`), depth-tested like the rain.
Every frame, any grounded live machine whose XZ falls inside a pool ellipse has its
horizontal velocity damped by a factor < 1 scaled by that pool's opacity — a
self-correcting drag that recovers on exit.

Per preset via `PuddleDef puddles` (`custom_weather.h`): `color`, `count`,
`radius`, `slow_factor`, each taking a `puddle.c` default when 0. Driven from the
tick (`Puddle_SetActive` / `Puddle_Tick` / `Puddle_Reset`). The global **Puddles**
menu scales **Slowdown** / **Frequency** / **Size**, toggles **Roaming** (off = a
fixed field), and can hide the discs (**Show Puddles**) while keeping the drag.

### Event sky suppression (`event_sky.c`)

A standalone toggle (not a per-preset layer and not driven from the runtime tick)
that stops City Trial siren events (Meteor, Fog, Dyna Blade, …) from swapping the
sky to their themed preset and back mid-round — jarring when a custom preset is
already running. The engine routes every event sky change through two tiny global
wrappers, which this module replaces at boot via `CODEPATCH_REPLACEFUNC`:
`Sky_TransitionGlobal` (event start → `Sky_BeginTransition`) and `Sky_RestoreGlobal`
(event end → `Sky_ApplyStoredIndex`). The **Event Sky Changes** menu toggle gates
them: `On` (default) reimplements each wrapper faithfully (vanilla behavior), `Off`
early-outs both so the round's weather holds through every event. Suppressing the
sky change neuters the **Dense Fog** event specifically, whose visual is delivered
entirely by its sky preset; every other siren event has its own actors. Installed
from `EventSky_OnBoot` (called in `main.c`'s `OnBoot`); the only other caller of
`Sky_TransitionGlobal` is an inert debug preset-cycler.

### WeatherKind enum (`custom_weather.h`)

`WEATHER_VANILLA_NUM = 17`, `WEATHER_CUSTOM_NUM = 13`, `WEATHER_TOTAL = 30`.
Custom presets occupy indices **17–29** (9 themed + 4 effect-layer presets):

| Idx | WeatherKind | Name | base_preset | Effect layers |
|-----|-------------|------|-------------|---------------|
| 17 | `WEATHER_DEEP_BLUE`    | Deep Blue    | Night (12)         | — |
| 18 | `WEATHER_GOLDEN_HOUR`  | Golden Hour  | Dusk (11)          | — |
| 19 | `WEATHER_BLOOD_RED`    | Blood Red    | Red Vignette (15)  | — |
| 20 | `WEATHER_WHITEOUT`     | Whiteout     | Dense Fog (9)      | — |
| 21 | `WEATHER_TOXIC_GREEN`  | Toxic Green  | Dark Vignette (5)  | — |
| 22 | `WEATHER_NEON`         | Neon         | Dark Purple (14)   | — |
| 23 | `WEATHER_COTTON_CANDY` | Cotton Candy | Pink Sky (8)       | — |
| 24 | `WEATHER_FROZEN_DAWN`  | Frozen Dawn  | Blue Sky (7)       | — |
| 25 | `WEATHER_VOID`         | Void         | Night (12)         | — |
| 26 | `WEATHER_STORM`        | Storm        | Dark Vignette (5)  | `lightning` + `rain` (+`screen_tint`) |
| 27 | `WEATHER_RAIN`         | Rain         | Gray Sky (13)      | `rain` (+`screen_tint`) |
| 28 | `WEATHER_BLOOD_RAIN`   | Blood Rain   | Red Vignette (15)  | `rain` + `lightning` |
| 29 | `WEATHER_PUDDLES`      | Puddles      | Gray Sky (13)      | `rain` + `wind` + `puddles` |

### CustomPresetDef fields (`custom_weather.h`)

Fields are grouped by on-screen effect, not engine mechanism. `0` means
"inherit from base preset" for the optional fields.

| Field | Type | Drives |
|-------|------|--------|
| `base_preset` | int | vanilla WeatherKind (0–16) to clone unset fields from |
| `fog_color` / `fog_start` / `fog_end` | u32 / float / float | per-pixel distance fog; fog also seeds the EFB clear |
| `sky_color` | u32 (RGBA) | skybox tint (`sky_ambient_color`); A=opacity (0=vanilla skybox visible) |
| `terrain_diffuse` / `terrain_specular` | u32 | TEV-baked stage terrain via `*stc_main_light` (0=inherit) |
| `char_diffuse` / `char_specular` / `char_dir` / `char_dir_lit` | u32 / u32 / Vec3 / int | AreaLight key light for chars/machines |
| `char_ambient` / `char_ambient_specular` | u32 | slot-8 ambient fill for chars/machines (0=inherit) |
| `fog_curve` | u32 (`WeatherFogCurve`) | fog density falloff via `HSD_Fog.type` (0=inherit linear) |
| `screen_tint` | u32 (RGBA, A=strength) | lbfade slot-3 overlay (0=none) |
| `rain` | RainDef | per-preset world-space rain: `enabled`, `color`, `density`, `fall_speed`, `line_width`, `streak` (`enabled = 0` = none; 0 numeric fields take rain.c defaults; slant comes from the global wind) — see "World-space rain" below |
| `lightning` | LightningDef | per-preset lightning: `enabled`, `flash_color`, `flash_frames`, `min_lull`/`max_lull`, `bolt` (`LightningBoltMode`) (`enabled = 0` = none; 0 numeric fields take lightning.c defaults) — see "Lightning" above |
| `wind` | WindDef | per-preset global wind: `enabled`, `speed`, `heading`, `gustiness`, `chaos` (`enabled = 0` = calm; 0 numeric fields take wind.c defaults) — see "Wind" below |
| `puddles` | PuddleDef | per-preset ground pools: `enabled`, `color`, `count`, `radius`, `slow_factor` (`enabled = 0` = none; 0 numeric fields take puddle.c defaults) — see "Ground puddles" below |

The global **Fog Distance** menu setting is separate from the per-preset
fields: it scales `HSD_Fog.scale` for every CT preset (vanilla and custom) via
`CustomWeather_GetFogScale`.

### Settings menu

`main.c` registers the `custom_weather` mod settings menu ("City Trial Sky") with
seven entries:

- **Weather Presets** (`weather_menu`, `custom_weather.c`) — a **Fog Distance**
  value (50–200%, default 100%; the global `HSD_Fog.scale` multiplier), an
  Enable-All / Disable-All pair, then one Enabled/Disabled toggle per preset (all
  30), backing the `weather_enabled[WEATHER_TOTAL]` array that
  `CustomWeather_OverrideSky` filters its random pick against.
- **Backdrops** (`backdrop_menu`, `custom_backdrops.c`) — the parallel **Backdrop
  Distance** scale plus a per-backdrop enable set.
- **Rain** (`rain_menu`, `rain.c`) — **Rain Intensity** (master multiplier over
  every preset's drop count; Off disables rain), **Fall Speed**, **Wind Slant**
  (let the global wind bend the rain), and **Hail** (Off / Light / Normal / Heavy;
  defined in `hail.c`).
- **Wind** (`wind_menu`, `wind.c`) — **Wind Strength**, **Randomize Direction**,
  **Affect Machines**, **Affect Items**.
- **Lightning** (`lightning_menu`, `lightning.c`) — **Lightning Bolts** (Auto / Off
  / Force), overriding each preset's `bolt`.
- **Puddles** (`puddle_menu`, `puddle.c`) — **Slowdown**, **Frequency**, **Size**,
  **Roaming**, **Show Puddles**.
- **Event Sky Changes** (`event_sky_option`, `event_sky.c`) — On (vanilla) / Off
  (keep the weather through events).

These menus **augment** the per-preset configs rather than replacing them: each
preset authors its own character and the menus layer master multipliers, gates, and
toggles on top (an "Off" / 0 floor disables). All settings persist via hoshi's
keyed menu-save.

## Debug Sky Selector

Function 0x800a9cb4 contains a debug controller handler:
- Hold L-trigger + D-pad: cycles through sky presets and events
- Mode 0: cycle presets (0 to preset_count-1), trigger on A
- Mode 2: cycle events (0-15), trigger via eventInit on A

`preset_count` is read from the stage sub-header, which `custom_weather`'s
`ExtendPresetArray` repoints to `WEATHER_TOTAL` (28) — so with the mod loaded,
this selector also cycles the custom presets (17–27).

## Runtime Addresses

These structures are heap-allocated per City Trial session, so their absolute
addresses change every run. Resolve them live from the stage GObj rather than
hard-coding:

```
GrObj     = *(0x805DD6CC)        // *stc_grobj
skyGObj   = *(GrObj + 0x168)
HSD_Fog   = *(skyGObj + 0x28)    // type +0x08(word), fog_adj +0x0C, start +0x10,
                                 //   end +0x14, color +0x18, scale +0x20
SkyState  = *(skyGObj + 0x2C)    // start/lerp +0x08, start_sky +0x14,
                                 //   current_output_sky +0x18, preset_ix +0x1C
AreaLight = *(GrObj + 0x718)     // flags +0x0D, color +0x10, hw_color +0x14,
                                 //   dir +0x18, vis +0x38 (bit 0x80)
jointTbl  = *(GrObj + 0x104)     // entry[idx]: jobj +0x00, desc +0x04 (visibility
                                 //   = jobj->flags +0x14 bit 0x10)
backdrop  = *(GrObj + 0xF4)      // JObj → DObj +0x18 → MObj (skybox material color)
```

Concrete values in City Trial under preset 0 "Day": `HSD_Fog` carries `type=2`,
`start=210`, `end=665`, `color=#9FCFFFFF`, `scale=1.0`, `fog_adj=NULL`;
`SkyState.preset_ix=0`; the global `AreaLight` carries `color=#D7D7FFFF`,
`hw_color=#FFFFFFFF`, `dir=(-0.40,0.80,0.50)`, `vis=0x80` — the preset[0] row in
"Vanilla CT Preset Reference". `jointTbl[62]` and `[63]` (the glow billboards)
both carry render-flag bit 4 set (visible).

| Address | Identity | Source |
|---------|----------|--------|
| 0x80557484 | **Global EFB clear color (BSS)**, RGBA8888. Written by `Sky_Update` step 4; read by `World_CObj+0x144`. The one fog-pipeline address that is **stable** across runs (BSS, not heap). | static |
| skybox MOBJ color | The backdrop's first DObj material color register (allocated by `3D_CreateStageModel` from `ModelSection[1]`, reachable via `*(GrObj+0xF4)` → DObj → MObj). Written **once** by `MObjLoad` (0x803f9f04) at stage load from the .dat; **not** updated by sky-preset transitions — the visible sky tint comes from `Sky_DrawTintQuad` (a translucent far-plane quad), not from re-tinting this MObj. | static value, heap address |
| water reflection color | A separate water JObj in `ModelSection[0]` terrain, not the sky pipeline. Out of scope of this doc. | external |

0x80557484 is BSS (loaded with `main.dol`), not heap. The address 0x800b05ec is
the load instruction inside `World_CObj` (function entry 0x800b04a8) that reads
the global fog color into HSD_SetEraseColor.

## Cross-references

- `mods/custom_weather/src/custom_weather.c` — the C-side preset extender
  (`ExtendPresetArray` + custom presets 17–27; selection hooks at 0x8010f1a4
  and 0x8010f224) and the Weather Presets settings menu. See "Custom Weather
  Mod" above.
- `mods/custom_weather/src/custom_weather_runtime.c` — per-frame customization
  runtime (hook at 0x800ce648, after `bl Sky_Update`): terrain/ambient re-tint,
  fog curve/scale, and the lbfade overlay. Drives `lightning.c`
  and `rain.c` from the same tick.
- `mods/custom_weather/src/lightning.c` — per-preset lightning strike loop
  (fog/EFB/LOBJ flash); `LightningDef`-driven.
- `mods/custom_weather/src/rain.c` — per-preset world-space GX rain field;
  `RainDef`-driven, depth-tested against stage geometry.
- `mods/custom_weather/src/custom_weather.h` — `WeatherKind` enum,
  `RainDef`/`LightningDef`, and the `CustomPresetDef` struct.
- `mods/custom_weather/src/main.c` — mod registration + "City Trial Sky"
  settings menu wiring.
- `mods/custom_weather/src/custom_backdrops.c` — companion 3D backdrop
  swap system (see `docs/sky-backdrop-system.md`).
- `externals/hoshi/include/obj.h:613-791` — HSD light/fog struct
  definitions (LightPoint/Spot/Attn, LObjDesc, LOBJ, HSD_Fog incl. the
  `scale` field at +0x20, AreaLightData, AreaLight, HSD_FogDesc).
- `externals/hoshi/include/stage.h:179-194` — Sky_* function declarations.
- `externals/hoshi/include/yakumono.h:97-128, 352-353` — Lighthouse yaku.
- `externals/hoshi/packtool/link.ld:790-799` — sky symbols. The newly
  identified LObj/AreaLight/ScreenFade/Sky helpers above are not yet
  added to link.ld; they will need entries before mod code can call them.
- `docs/sky-backdrop-system.md` — sister doc on the 3D skybox geometry
  pipeline (`ModelSection[1]`, `3D_CreateStageModel`).
- `docs/yakumono-system.md` — yaku framework, including Lighthouse.
