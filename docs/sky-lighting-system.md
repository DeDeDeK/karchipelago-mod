# City Trial Sky and Lighting

Stage lighting in Kirby Air Ride is driven by a per-stage table of **sky presets** that
interpolate fog, screen tint, sky ambient color, and a directional "area light" over a
transition window. This doc covers the vanilla engine system (preset data, the per-frame
update, fog, HSD lights, shadows) and, in its final section, the `custom_weather` mod
built on top of it.

## Architecture

Three independent objects work together, and confusing them is the easiest way to misread
the system:

1. **Sky preset** - a 0x48-byte record in the stage file (`GrCity1.dat`). Drives fog,
   screen tint, sky ambient color, and a single "area light" directional vector.
   `SkyPresetEntry` in `externals/hoshi/include/stage.h`.
2. **AreaLight** - a runtime, KAR-proprietary HSD-style object stored at `GrObj+0x718`.
   Per-frame interpolation target for the preset's directional light fields. **Not** a
   standard `LOBJ` and **not** registered in HSD's active-light list. Its output is
   consumed by per-rider/per-machine shading, never by a hardware light slot.
3. **HSD `LOBJ` chain** - the actual GameCube hardware lights, loaded from the stage
   file's `LObjDesc**` arrays. These get GX hardware light slots assigned dynamically
   each frame by `HSD_LObjSetCurrentAll`. This chain is what stage geometry's
   `GXSetChanCtrl` light mask references.

`Sky_SetupLights` creates no `LOBJ` despite the name - it only toggles JOBJ render-node
visibility flags. The fog GObj (`GrObj+0x168`) is a separate HSD object with its own GX
callback (`Fog_GX`); the lbfade screen-tint overlay is a fourth subsystem.

```
GrObj  (gr_kind=9, City Trial)
 |- +0x54  collision-zone descriptor (-> 0x98-stride records at +0x64)
 |- +0x104 per-joint table ({JOBJ*, JOBJDesc*}, 317 entries in CT)
 |   (128 per-zone AreaLights live in the registry at r13[+0x538], not a GrObj field)
 |- +0xF4  backdrop JObj
 |- +0x168 fog/sky GObj
 |            +0x28 HSD_Fog   written each frame by Sky_Update: fog start/end/color
 |            +0x2C SkyState  lerp state; also mirrors the lerp output
 |            (the global EFB clear at 0x80557484 is written from the same step;
 |             World_CObj reads it for the next clear)
 |- +0x714 ScreenFade slot 3 (lbfade) - only fired by Sky_BeginTransition
 |- +0x718 AreaLight (one per stage)
 |
 +- gr_data->stage_resource[+0x14]  three LObjDesc** chains
              consumed by Light_StageInit at stage load
```

## Key Functions

| Address     | Symbol                                     | Description |
|-------------|--------------------------------------------|-------------|
| 0x8010f114  | `Sky_Init`                                 | Per-stage initial sky setup; dispatches on `stGetCurrentStageKind`, calls `Sky_SetupLights` for each declared JOBJ index, selects an initial preset, then `Sky_LoadPreset`. |
| 0x800db774  | `Sky_SetupLights(grobj, jobj_idx)`         | Toggles JOBJ visibility + collision only. Forces render flag bit 4 on the stage JOBJ subtree at `grobj[+0x104] + jobj_idx*8`, then disables collision on every face of that joint's zone. Touches no HSD light. |
| 0x800dc630  | `Sky_SetPresetIndex`                       | Stores a preset index into `SkyState+0x1C` without applying it. |
| 0x800dc1b4  | `Sky_LoadPreset`                           | Snaps to the preset immediately (no transition, no fade). Copies the preset's `AreaLightData` into the live AreaLight, sets fog start/end/color directly, and broadcasts `light_vis_flag`. |
| 0x800dc354  | `Sky_BeginTransition(grobj, idx)`          | Smooth interpolation: captures current values as the lerp start, fires `Sky_BeginFade` for the lbfade overlay, sets the target. |
| 0x800dc4c0  | `Sky_ApplyStoredIndex`                     | `Sky_BeginTransition` reading the index from `SkyState+0x1C`. |
| 0x800dc640  | `Sky_Update`                               | Per-frame interpolation. Writes seven memory regions (see "Per-Frame Update"). |
| 0x800dc7a4  | `Sky_GetCurrentSkyColor(grobj, &out)`      | Returns `SkyState.current_sky_color`, the per-frame lerp toward the preset's `sky_ambient_color`. RGBA(0,0,0,0) if there is no sky GObj or no target preset. Called from the backdrop pass (`zz_800d8148_+0x64`) and `Map_GX+0xa0`; each call feeds `Sky_DrawTintQuad`. |
| 0x800d7e78  | `Sky_DrawTintQuad(cobj, &color)`           | Renders an alpha-blended screen-aligned quad at the camera's far plane. Early-outs if `color.a == 0`. Fog-enabled, so distance fog attenuates the tint. This is how `sky_ambient_color` becomes a visible sky tint. |
| 0x800dbfa8  | `Sky_InitFog`                              | Builds the fog GObj: `GObj_Create(0x1E,1,0)`, `Fog_LoadDesc`, `GObj_AddObject`, `GObj_AddGXLink(Fog_GX, 0, 1)`. Seeds the global EFB clear color at 0x80557484. |
| 0x800dbf84  | `Fog_GX`                                   | GX callback; one-liner `HSD_FogSet(gobj->object)`. |
| 0x800797a8  | `AreaLight_Lerp`                           | Interpolates the AreaLight (lbarealight.c). Asserts validity bits, snap-copies header/colors/direction from target, lerps only if `flags & 0x04`. |
| 0x80079c04  | `GXColor_Lerp`                             | Linearly interpolates packed RGBA u32 colors by ratio. |
| 0x80079428 | `AreaLight_Create` | Allocates a live AreaLight, registers it in the global registry at `r13[+0x538]`, copies fields from a source `AreaLightData`. Asserts `flags & 0x03 == 0x03`. |
| 0x800ef618  | `AreaLight_StageInit`                      | Stage-init helper: stack-builds a default `AreaLightData` from the defaults chain and stores the resulting AreaLight at `grobj+0x718`. |
| 0x800ef864  | `AreaLight_LerpToLive`                     | Adapter called from `Sky_Update`: extracts `grobj+0x718` and dispatches to `AreaLight_Lerp`. |
| 0x8007a2c0  | `AreaLight_RegistryWalk`                   | Walks the AreaLight registry into a consumer's nearest-lights array. Guards on the kind's walk handler being non-NULL. |
| 0x80079948  | `AreaLight_BroadcastVisFlag`               | Walks the registry and writes bit 0x80 of byte +0x38 on every matching entry from `light_vis_flag` bit 0. |
| 0x800eef04  | `Sky_AllocFade`                            | `grobj+0x714 = ScreenFade_Alloc(3)`. |
| 0x800eef50  | `Sky_BeginFade(grobj, &color, frames)`     | `ScreenFade_GetState(3)` then `ScreenFade_Begin`. |
| 0x800eefb0  | `Sky_FreeFade`                             | Frees the lbfade slot at scene teardown. |
| 0x800b04a8  | `World_CObj`                               | World-camera GObj GX callback. At +0x144 (0x800b05ec) it loads the global fog color from 0x80557484 and pushes it through `HSD_SetEraseColor` (0x8040f884). |
| 0x8041b0fc  | `HSD_FogSet`                               | Reads the live `HSD_Fog`, queries current CObj near/far, emits `GXSetFog` and `GXSetFogColor`. |
| 0x80057468  | `LObj_CreateAll`                           | Walks a NULL-terminated `LObjDesc**` array, `HSD_LObjLoadDesc` per entry, links them via `LOBJ.next`. |
| 0x803ff570 | `HSD_LObjSetCurrentAll` | Each frame: clears the 9-slot table at 0x805899B0 (`stc_lobj_hw_slot_table` in `obj.h`), re-walks the list, assigns each LOBJ a hardware slot. |
| 0x803fe4b8  | `HSD_LObjSetupInit`                        | Bakes each active LOBJ into a hardware light register via `GXInitLight*` + `GXLoadLightObjImm`, and rebuilds the three global light-mask words. |
| 0x8042a22c  | `LObj_GX`                                  | GX callback for an LObj-bearing GObj: `HSD_LObjSetCurrentAll` then `HSD_LObjSetupInit`. |
| 0x800d5ed4  | `Light_StageInit`                          | Stage-init driver, called from `grLoadStage`. Calls `Light_CreateForStage`, `Light_CreateForStageSecondary`, `Light_CreateAreaLightDefaults` back-to-back. |
| 0x800d5fd0  | `Light_CreateForStage`                     | Primary GX light chain. Also writes `stc_main_light` (`r13[+0x5fc]`) - the handle the weather mod re-tints. |
| 0x800d60d8  | `Light_CreateForStageSecondary`            | Secondary GX light chain, built unconditionally on every stage load. |
| 0x800d6188  | `Light_CreateAreaLightDefaults`            | Loads the third chain (via `grGetStageLight_Kirby`, 0x800cea5c) purely as a default-value source; no GObj, no GX link. Stashes the chain head / first ambient / first infinite at `r13[+0x5F8/+0x5F0/+0x5F4]`. |
| 0x800d61e8  | `Light_GetAreaLightDefaults`               | Returns those three defaults. Writes (0,0,0,0xFF) if there is no chain (menu/CSS scenes). |
| 0x800d5444  | `Sky_TransitionGlobal(idx)`                | `Sky_BeginTransition` on `*stc_grobj`. The wrapper City Trial events call. |
| 0x800d546c  | `Sky_RestoreGlobal`                        | Restores the pre-event preset. |
| 0x800d5414  | `Sky_GetPresetCount`                       | Preset count from `(*stc_grobj)->gr_data->sky_block->preset_header`. |
| 0x800db2b8  | `Gm_Roll(weights, count)`                  | Weighted random selection. |

## Data

The runtime structs are declared in hoshi headers and are not repeated here:
`SkyPresetEntry`, `SkyState`, `SkyBlock`, `SkyPresetSubHeader`, `GrData` and `GrObj` in
`stage.h`; `AreaLightData`, `AreaLight`, `HSD_Fog`, `HSD_FogDesc`, `LObjDesc` and
`LightGroup` in `obj.h`.

The facts that matter for working on this system:

- A preset is 0x48 bytes and embeds a 0x2C-byte `AreaLightData` at +0x18. Its
  `transition_frames` is the lerp denominator; `fade_color` fires the lbfade overlay and
  is used *only* on transitions.
- `light_vis_flag` at preset +0x44 is a single bit broadcast (not lerped) into bit 0x80
  of AreaLight +0x38 by `AreaLight_BroadcastVisFlag`, from `Sky_LoadPreset` only.
  `AreaLight_Create` force-sets that bit, so the default state is "visible".
- `AreaLightData.flags & 0x04` decides whether `AreaLight_Lerp` interpolates at all;
  without it the fields snap to target on every call.
- `Sky_Update` writes `HSD_Fog.start`/`end`/`color` and never touches `type` (+0x08) or
  `scale` (+0x20). That is why a mod can own those two fields with a single write.
- `SkyState+0x08` is the fog-color lerp *start* slot, reused as the per-frame lerp output
  mirror. `SkyState+0x20` is the AreaLight lerp start, not the live state - the live
  values are in the AreaLight object itself.
- The preset array is reached as
  `grobj->gr_data->sky_block->preset_header->{preset_array, preset_count}`. Repointing
  that `{base, count}` pair swaps the whole preset table for the engine.
- The three stage light chains hang off `gr_data->stage_resource[+0x14]`: `+0x00` primary
  GX chain, `+0x04` AreaLight-defaults chain (not rendered), `+0x08` secondary GX chain.
  Each is a NULL-terminated array of `LightGroup*` (`{LObjDesc *desc, LightAnim *anim}`).
- `Sky_Init`'s per-stage JOBJ indices come from the sub-block at
  `gr_data->stage_resource[+0x08]`, fields `+0x04` and `+0x08`; the CT `Gm_Roll` weights
  are the four ints at `+0x0C`.

## Sky Presets

### Vanilla City Trial preset table

The 17 presets shipped in `GrCity1.dat`. Columns map 1-to-1 onto `SkyPresetEntry`, so
this doubles as a tuning reference when authoring new presets.

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
[13] Gray Sky        785A32FF      500  1300   00000000   785A32AA   E6DCC8FF   FADCB4FF   (-0.40,0.80,0.50)   1
[14] Dark Purple     000000FF      500  1300   0000005A   3C0000A0   DCC8C8FF   F0DCFFFF   (-0.40,0.80,0.50)   0
[15] Red Vignette    C8461EFF      100   500   32000000   783219B4   DCC8BEFF   FAD2AAFF   ( 0.00,1.00,0.00)   1
[16] Dark Low Vis    000000FF       90   360   2800006E   1E0005C8   F0DCC8FF   DCA078FF   ( 0.00,-1.0,0.00)   1
```

Where each index is used:

- **0** - City Trial initial random pool; also the fixed pick for City Trial Free Run.
- **1-9** - event skies (9 "Dense Fog" is the fog event's).
- **10, 11, 12** - City Trial initial random pool.
- **13 / 14** - stadium transition types 0 / 1.
- **15 / 16** - stage kinds 22 and 23.

Indices 17+ are appended at runtime by the `custom_weather` mod.

### Initial selection

`Sky_Init` (0x8010f114) dispatches on `stGetCurrentStageKind` (0x80261ecc, the
`r13[0x7F8]` cache) - **StageKind, not GroundKind**. Any value other than the four below
falls into an `__assert`.

| StageKind | Sky_SetupLights calls | Preset selection |
|-----------|-----------------------|------------------|
| 9 | both jobj indices | Random from `{0, 10, 11, 12}` via `Gm_Roll` |
| 22 | second index only | Fixed: 15 |
| 23 | first index only | Fixed: 16 |
| 52 (City Trial Free Run) | both jobj indices | Fixed: 0 |

City Trial's random draw indexes the 4-entry table at 0x804a77e4 (`{0, 0x0A, 0x0B, 0x0C}`)
with the result of `Gm_Roll` over the four weights in the stage sub-block.

The initial preset is applied via `Sky_LoadPreset`, not `Sky_BeginTransition`, so the
lbfade overlay does not run on stage entry. To force a fade-in for a custom initial
preset, swap the call to `Sky_BeginTransition` (and call `Sky_AllocFade` first if it has
not run yet).

### Event-driven changes

During City Trial, siren events can trigger sky transitions. The per-event flag is the
byte at +0x09 of the event's 0xC-byte config entry (nonzero = this event changes the sky);
the target preset index is the word at +0x04 of its 0x14-byte data entry. `CityEvent_Init`
(0x800ee8a4) reads that index and calls `Sky_TransitionGlobal` (0x800d5444);
`Sky_RestoreGlobal` (0x800d546c) returns to the original preset when the event ends. Both
go through `Sky_BeginTransition`, so both fire the lbfade overlay.

### Stadium transitions

The stadium-battle transition (0x802839b8) picks preset 13 "Gray Sky" for stadium type 0
and 14 "Dark Purple" for type 1. It also allocates two GObjs (size 33, priority 32) with a
GX callback at 0x80283ed8 for the stadium proscenium decals, and resets fade timer fields.

### Debug selector

The debug controller handler at 0x800a9cb4 cycles presets (mode 0) and events (mode 2) on
L-trigger + D-pad, firing on A. Its upper bound is the stage sub-header's `preset_count`,
which `custom_weather` repoints, so with the mod loaded the selector reaches the custom
presets too.

## Per-Frame Update

`Sky_Update` (0x800dc640) takes `grobj`, reads the sky GObj at `grobj+0x168` (SkyState at
+0x2C, `HSD_Fog` at +0x28) and early-outs when the target preset is NULL or
`transition_frames == 0`. Otherwise, in order:

| Step | Address | Write | Effect |
|------|---------|-------|--------|
| 1 | 800dc6a0 | `SkyState.transition_frame_counter++` (capped) | drives `ratio = counter / target.transition_frames` |
| 2 | 800dc6f4 | `SkyState+0x08` <- `GXColor_Lerp(start, target.fog_color)` | start-color slot reused as the lerp output mirror |
| 3 | 800dc708 | `HSD_Fog.color` <- lerped RGBA | feeds `Fog_GX -> HSD_FogSet -> GXSetFogColor` |
| 4 | 800dc71c | `*(u32*)0x80557484` <- lerped RGBA | global EFB clear color, consumed by `World_CObj` on the next clear |
| 5 | 800dc734 | `HSD_Fog.start` <- lerped float | fog near plane |
| 6 | 800dc750 | `HSD_Fog.end` <- lerped float | fog far plane |
| 7 | 800dc764 | `SkyState.current_sky_color` <- `GXColor_Lerp(start_sky, preset.sky_ambient_color)` | read back the same frame by `Sky_GetCurrentSkyColor` -> `Sky_DrawTintQuad`, at the backdrop pass and `Map_GX` |
| 8 | 800dc778 | `AreaLight_LerpToLive(...)` -> `AreaLight_Lerp` | writes color, hw_color, direction and (if `flags & 0x04`) intensity into the live AreaLight at `grobj+0x718` |

Anything a mod wants to hold across frames must be written **after** `Sky_Update` runs, or
be a field `Sky_Update` does not touch (`HSD_Fog.type`, `HSD_Fog.scale`, LOBJ colors).

## Fog

Two independent paths feed pixels with the fog color each frame, and `Sky_Update` writes
both:

```
Sky_Update -> HSD_Fog.start/end/color -> Fog_GX (gx_link 0, pri 1) -> HSD_FogSet
                -> GXSetFog + GXSetFogColor -> per-pixel TEV blend

Sky_Update -> 0x80557484 (BSS) -> World_CObj+0x144 -> HSD_SetEraseColor -> 0x805dcb88
                -> GX_SetCopyClear on the next CopyDisp
```

Changing only `HSD_Fog.color` changes per-pixel fog blending but leaves the EFB clear at
the previous color (visible as contrasting borders where the camera does not fill the
viewport); changing only 0x80557484 does the reverse.

`HSD_FogSet` (0x8041b0fc) reads the *current* COBJ's near/far via
`HSD_CObjGetCurrent`/`Get{Near,Far}`, then emits
`GXSetFog(type, start, end * HSD_Fog.scale, near, far, &color)`. The `scale` multiplier at
`HSD_Fog+0x20` is the cleanest global lever on the fog far wall. `Fog_GX` runs once per
camera/render-pass, so mid-frame fog color changes are visible on the next pass.

### Fog type and curve

City Trial's initial fog comes from the on-disk `HSD_FogDesc` at
`gr_data->sky_block->fog_desc`: `type = 0x02` (`GX_FOG_PERSP_LIN`), `fog_adj = NULL`,
`start = 200.0`, `end = 950.0`, `color = #9FCFFFFF`. `HSD_FogInit` (0x8041b450) copies
those into the runtime `HSD_Fog`. The engine sets the type once at load and never changes
it, so by default every CT preset renders perspective-linear. Because nothing else writes
`HSD_Fog.type`, a single mod write holds for the whole preset - that is how
`custom_weather` swaps in exp/exp2/reverse-exp falloff. The exp curves back-load the
density (near and mid field stay clearer, the wall forms close to `fog_end`); the reverse
variants make fog densest at the camera. The live fog *distances* are the active preset's,
not the descriptor's seed.

**Range adjust is unused on CT.** `HSD_FogSet` calls `GXInitFogAdjTable` /
`GXSetFogRangeAdj` only when `fog_adj != NULL && (fog_adj.flags & 7) != 0`. CT's `fog_adj`
is NULL, so it calls `GXSetFogRangeAdj(0, 0, NULL)` every frame instead.

### Stage fog flags

Two accessors mask the big-endian high byte of `StageNode.fog_flags` (the `int` at +0x1C,
reached as `grobj->gr_data->stage_node`; both do `lbz r0,28(r3)` then mask). CT's
`fog_flags` is `0x02000000`, so that byte is `0x02`:

| Function | Addr | Returns | CT value | Gates |
|----------|------|---------|----------|-------|
| `grGetStageFogFlag1` | 0x800d1d48 | bit 0 | **0** | the `Map_DisableFog`/re-enable pair wrapping the backdrop draw in `Map_GX` |
| `grGetStageFogFlag2` | 0x800d1d70 | bit 1 | **1** | fog for a separate render group (`zz_8023dc8c_`); 1 keeps fog on |

`Map_GX` (0x800d81e4) reads `grGetStageFogFlag1` at four sites, each guarding a
`Map_DisableFog` (0x800d1dcc, which does `HSD_FogSet(NULL)`) / re-enable (0x800d1d98,
restoring the `HSD_Fog*` kept at `r13[+4616]`) pair around the backdrop JOBJ draw. The
terrain model is drawn right after the pair with no fog calls of its own, so it inherits
whatever state the pair left.

**Bit 0 controls terrain fog, not backdrop fog.** Fog is off when `Map_GX` begins, so the
backdrop always draws fog-free regardless of the bit. With bit 0 == **0** (CT's value) the
pair does `DisableFog` -> backdrop -> `ReEnableFog`, and that re-enable is what fogs the
terrain. With bit 0 == **1** both the disable and the re-enable are skipped, fog stays off,
and the terrain renders fog-free (clear across the map, while items/effects drawn later -
after `Fog_GX` re-enables - still fog). It is an un-fog-the-terrain lever, the opposite of
an atmospheric backdrop haze.

### event_denseFog does not poke fog directly

The "Dense Fog" City Trial event never touches `HSD_Fog` or the fog descriptor. Its
event-table slot (0x804a5528) has `blr` start/end stubs and the think handler
`event_denseFog` (0x801118dc) is a generic duration counter that calls
`CityEvent_EndWithSkyRestore` (0x800ee660) when it expires. The fog change is entirely a
sky-preset transition to preset 9, driven by the normal `Sky_Update` lerp. Every custom
event that wants to change fog works the same way: pick a preset and transition to it.

## Fade Overlay (lbfade)

`ScreenFade` (lbfade) provides slot-based full-screen tint overlays; slot 3 is the sky
system's. `ScreenFade_Alloc` (0x80065a80) builds a `GObj_Create(7, 0x1d, 1)` whose GX link
number **is the slot number**, so slot 3 draws on gx_link 3. Supporting entry points:
`ScreenFade_Free` (0x80065ae4), `ScreenFade_Begin(state, color, frames)` (0x800665f8),
`ScreenFade_GetState(slot)` (0x80066960), `ScreenFade_Draw` (0x80065140), `ScreenFade_GX`
(0x8006541c). The sky wrappers are `Sky_AllocFade` / `Sky_BeginFade` / `Sky_FreeFade`.

Only `Sky_BeginTransition` (0x800dc354) and `Sky_ApplyStoredIndex` (0x800dc4c0) fire the
overlay; `Sky_Init` ends in `Sky_LoadPreset`, so the initial preset never fades.

### The global-darkening mechanism

The overlay is the only mechanism in the engine that darkens the composited world in one
step - terrain, backdrop, sky tint and fogged geometry alike - because it composites a
translucent 640x480 quad over the framebuffer rather than going through HSD lights or
material colors. Vanilla dark presets entered via an event (Dark Vignette `0000003C`,
Night `00001080`, Dark Purple `0000005A`, Dark Low Vis `2800006E`) get their darkness from
this, not from lighting changes.

Behavior and constraints:

- **It sustains at the target color.** `ScreenFade_Begin` lerps current -> target over
  `frames` and then holds indefinitely; it never fades back to clear. Setting it once is
  enough, and clearing it means fading to `0x00000000`.
- **Draw order limits what it covers.** Slot 3's gx_link puts it ahead of the
  character/machine links (5/6) and well ahead of the HUD (21) in the world camera's main
  pass, so riders, machines and UI are composited on top of the tint. It reads as
  scene/atmosphere darkening rather than a uniform screen dim.
- **It works on top of fog, not instead of it.** Fog still applies per-pixel during world
  rendering; the overlay then tints the result, so near geometry inside `fog_start` is
  darkened even though fog skipped it.
- Alpha values in vanilla presets cluster around 60-110 (~24-43%). Above that the scene
  goes flat and silhouettes lose detail.

**Pitfall - the City Trial GrObj is reused across exit/re-entry.** The same `grobj`
pointer comes back for the next CT round, so per-stage state guarded by
`if (grobj != last_grobj)` will not reset on re-entry. The `grobj+0x714` slot ID *is* fresh
every entry (each `ScreenFade_Alloc` increments the global counter at `r13[-32248]`), so
use the slot ID as the freshness signal for anything that must reset per round - including
"have I called `Sky_BeginFade` on this slot yet?". Without it, the second entry uses a new
slot whose state is still zeroed and the overlay never arms.

## Stage Lights

### GX hardware allocation

The GameCube has 8 hardware light slots plus a dedicated 9th ambient slot (`GX_LIGHT8`,
lightid `0x100`). `HSD_LObjSetupInit` (0x803fe4b8) is not pure FIFO:

- LOBJs with `flags & 3 != 0` (positional/directional) take slots 0..7 in active-list
  insertion order via the counter at `DAT_805de210`.
- LOBJs with `flags & 3 == 0` (ambient-only) are pinned to **slot 8** unconditionally.
- A second pass attaches specular auxiliary lights at `lobj+0x90`, filling more slots from
  the same counter.

### The three stage chains

All three are loaded unconditionally by `Light_StageInit` (0x800d5ed4). Two are
GX-rendered; the third exists only as a default-value source for the AreaLight.

| Creator | Role | GObj class | gx_link | Chain source | AddProc |
|---------|------|------------|---------|--------------|---------|
| `Light_CreateForStage` (0x800d5fd0) | primary GX lights | 1 | 0 | `stage_resource[+0x14][+0x00]` | 0x800d5f3c - per-LOBJ `HSD_LObjAnim` (skips AOBJ flag 0x40000000) + stage scale |
| `Light_CreateForStageSecondary` (0x800d60d8) | secondary GX lights | 20 | 8 | `stage_resource[+0x14][+0x08]` | 0x800d6094 - `HSD_LObjAnimAll`, no filter |
| `Light_CreateAreaLightDefaults` (0x800d6188) | AreaLight defaults, not rendered | - | - | `stage_resource[+0x14][+0x04]` | none |

`Light_GX` (0x800d5fb0) and the secondary's callback (0x800d60b8) are byte-identical
thunks to `LObj_GX` (0x8042a22c) - distinct entry points only so each GObj can register its
own pointer. The rendering path is shared.

In `iso/files/GrCity1.dat` each chain is a 2-entry array: an ambient-only LOBJ
(`flags 0x0004`, `AMBIENT | DIFFUSE`, no position) followed by an infinite directional one
(`flags 0x000D`, `INFINITE | DIFFUSE | SPECULAR`). The primary chain's pair is white /
warm-white `FFFFD9` at (-1000, 700, 1500); the secondary and the AreaLight-defaults chain
carry **identical** descs - pale blue `D8D8FF` ambient plus a white infinite at
(-1000, 1000, 1500). The infinite LOBJs' `cutoff`/`dist_func` fields are spot/point
attenuation parameters the engine does not apply to an INFINITE light: `HSD_LObjSetupInit`
reads the type from `flags & 3` and `0x000D & 3 == 1`, so the position vector is the light
*direction*, not a location.

A CT session therefore runs **4 GX hardware lights**: primary ambient and secondary ambient
both bound to slot 8 (last writer in insertion order wins), primary infinite in slot 0, and
secondary infinite in slot 1. The defaults chain is never registered with the active list
and consumes no slot. Roughly 5-6 hardware slots stay free for custom lights.

Of the 24 `bl HSD_LObjLoadDesc` call sites, 1 is `LObj_CreateAll` and 23 are menu / CSS /
mode-select / HUD / effects code. **There are zero gameplay-time non-stage HSD light
spawns.** Every "light" you see during gameplay (lighthouse, light tunnel, fireworks event,
item glow, projectile flash) is textured or animated geometry and material-color tricks.

### World_CObj GX-link dispatch

`World_CObj` (0x800b04a8) calls `CObj_RenderGXLinks` twice per frame with different
`cobj_links` masks (written into the parent GObj at +0x20/+0x24 just before each call):

| Pass | Site | Mask | render_mode | Links |
|------|------|------|-------------|-------|
| 1 | 0x800b0630 | `0x00000000_00000E60` | 8 | 5, 6, 9, 10, 11 |
| 2 | 0x800b0740 | `0x0000001F_0400FFFF` | 7 | 0..15, 26, 32..36 |

Both light chains (gx_link 0 and 8) dispatch in the second pass, sharing the same world
camera, view matrix and render mode. The split into two GObjs/chains is organizational and
AddProc-filtering only.

### Sky_SetupLights and the CT glow billboards

`Sky_SetupLights` (0x800db774, 0x5C bytes) picks `joint_table[jobj_index].jobj` from
`grobj+0x104`, calls `HSD_JObjSetFlagsAll(jobj, 16)` to force render flag bit 4 on the
whole subtree, then finds that JOBJ's collision zone via `grScene_FindInstanceByKey`
(0x800d7954) and calls `grScene_SetInstanceColl(zone, 0)` (0x800d7ad0) to disable collision
on every one of its faces. No HSD light object is created or touched.

For City Trial it runs twice from `Sky_Init` with joint indices 62 and 63 (CT's `0x3E` and
`0x3F` from the stage sub-block). Those two joints are map-wide light-glow / haze overlay
billboards, not props: in `grModelCity1` they are depth-1 root children at the origin with
whole-map span, authored out of the normal render passes so they are off by default. Joint
62's `JOBJDesc.flags` omit `OPA`; both carry `TEXEDGE`, and their DObjs use additive,
depth-write-disabled material modes (`0x40002011` = `XLU | CONSTANT | TEX0 | ALPHA_MAT`,
`0x60004011` = `XLU | NO_ZUPDATE | CONSTANT | TEX0 | ALPHA_VTX`) over small glow textures.
They are distinct from the nearby lamp-post / traffic-light props (joints 56-69 with opaque
`OPA` geometry and real world translations) - those are solid geometry, these are the
additive glow laid over the city.

The toggle is preset-independent: `Sky_SetupLights` runs only from `Sky_Init`, never from
`Sky_LoadPreset` / `Sky_Update` / `Sky_BeginTransition`, so the glow billboards stay on for
the whole CT session regardless of day/night. Day/night comes entirely from the sky-preset
path.

#### Per-joint table and collision zones

Both are general stage-load infrastructure, not light-specific.

**`GrObj+0x104` - per-joint table.** Built once during `grLoadStage` by `grparts.c`
(0x800d8b98 -> 0x800d8a60): allocates `joint_count << 3` bytes and walks the runtime stage
model's joint tree in pre-order via `HSD_JObjWalkTree`. Each 8-byte entry is
`{JOBJ *jobj, JOBJDesc *desc}` and the index is the joint's pre-order position; parallel
per-joint `DObj*` / `MObj*` lists live at `GrObj+0x108` / `+0x10C`. City Trial's stage model
has 317 joints.

**`GrObj+0x54` - collision-zone descriptor.** An 18-word header whose `+0x10`/`+0x14` hold
the base and count of an array of `0x98`-stride zone records (backing array at
`GrObj+0x64`), one per collidable joint. Each record leads with the `JOBJ*` key copied from
the per-joint table, followed by vertex slice / count and a `0x40`-stride face sub-array
with its count. `grScene_FindInstanceByKey` linear-scans on the key;
`grScene_SetInstanceColl` walks the faces and writes bit `0x40` of byte `face+0x3C`, the
per-face collision-enable flag.

### Effects that are not HSD lights

- **Lighthouse (yaku desc 68).** `Lighthouse_Create` (0x8010d228) / `Lighthouse_Init`
  (0x8010d260) are plain yakumono. They iterate a per-instance joint-index list at
  `param[0x0C]` and toggle a render-node visibility bit on a JOBJ. The visible beam is a
  yellow alpha-blended cone mesh revealed by flag toggles - no HSD light, no GXLightID
  consumed, no surface actually lit. The four `YakumonoParam.lighthouse` anim slots (start,
  active, end, inactive) drive matanim / jobj-anim swaps for the spinning beam.
- **Light Tunnel (`YAKUKIND_LIGHTTUNNEL`).** A textured cylinder with scrolling UVs. Its
  functions live anonymously inside the gryaku block at 0x8010xxxx (no `Lighttunnel_*`
  symbols in the map).
- **Bombs, fireworks, projectile flashes, charge auras.** AOBJ-driven MOBJ material color
  animation, particle systems, and full-screen TEV stages. None spawn HSD LObjs.

## AreaLights

### Initial values

Before any preset applies, `AreaLight_StageInit` (0x800ef618) seeds the live AreaLight at
`grobj+0x718` from `Light_GetAreaLightDefaults` (0x800d61e8), which returns the first
non-hidden ambient LOBJ's color (`r13[+0x5F0]`), the first non-hidden infinite's color and
position (`r13[+0x5F4]`), or opaque-black defaults when there is no chain (`r13[+0x5F8]`
NULL, e.g. menu/CSS scenes). From the next frame on, `Sky_Update` overwrites it every
frame - so the defaults chain only matters for the first frame, but it must be loaded for
the AreaLight to exist at all.

### Per-zone AreaLights (City Trial: 128 zones)

Beyond the single sky-driven AreaLight, City Trial builds 128 per-zone AreaLights - one per
collision region - carrying per-district color data. Walking the registry in a CT round
returns 129 (128 zone lights plus the global one). They are **render-inert**: nothing
consumes their colors during the GX pass, so they are not a usable lever for tinting the
map.

**Registry and dispatch.** Every AreaLight, global or per-zone, is created by
`AreaLight_Create` and pushed onto one global push-front list at `r13[+0x538]`. The node's
`kind` word (+0x04) is 0 for the sky-driven global and 3 for a zone light, and indexes the
class table at 0x8049ac60 (3 words each: walk / free / lerp). **Kind 3's walk handler is
NULL** (`0x8049ac84 = {0, 0x8007acc8, 0x8007ad08}`) where kind 0's is `0x8007a51c`, and
`AreaLight_RegistryWalk` (0x8007a2c0) guards on it - so the 128 zone lights are skipped by
the per-character lighting accumulator entirely.

**Allocation.** Zones are carved from the stage collision pool by `grcoll.c`
(`zz_800d6774_`), sized by the grData CollisionNode descriptor word `[0xb]`. This is a
separate, independently-counted set from the `[5]`-counted 0x98-stride collision zones at
`GrObj+0x54/+0x64`. They are not a simple `GrObj+offset` array - reach them through the
registry.

**Authored values (preset "Day").** Most zones read
`color=#9696AA hw=#8C8C96 dir=(-0.49,0.49,0.73)`, flag `0x3b` - the direction is exactly
the normalized defaults-chain infinite vector (-1000,1000,1500), i.e. those zones sampled
"unset" and fell back to `Light_GetAreaLightDefaults`. A handful of accent zones carry
distinct `hw_color`s (`#00FF00`, `#B300FF`, `#DDC1EA`) with flag `0x3c`. None of it reaches
the screen.

**Update.** `zz_800ef70c_` (0x800ef70c) refreshes one zone per call at setup / event
re-init, not per frame: `zz_800d78b4_` samples the zone joint's transform (`record+0xE0`,
matrix column 2) as RGB, falls back to the defaults if approximately black, else negates
the sample, then writes via `zz_80079648_` (ambient) or `zz_800796f8_` (directional) and
pushes the joint translation. The kind-3 lerp handler (0x8007ad08) is driven by the City
Trial zone-group state machine around `Gr_StateChange` (0x800f5548), not by `Sky_Update`.

**Not a recolor lever.** Writing a zone's color - or its matrix-column-2 sample source -
changes the struct and produces no on-screen change. City Trial bakes terrain shading into
TEV vertex colors, so to tint a region visually there is no per-zone hook; use the global
levers KAR actually renders (fog color / EFB clear, and the lbfade overlay). To make a
*rider* pick up a custom color, register a kind-3 walk handler at 0x8049ac84 or add a
kind-0 AreaLight so `AreaLight_RegistryWalk`'s accumulator picks it up.

### AreaLight to character/rider bridge

The live AreaLight **does not feed any LOBJ**. `HSD_LObjSetupInit` reads colors exclusively
from `LOBJ+0x10`/`+0x14` and never consults the registry; the per-LOBJ "update" vtable slot
(`LObjUpdateFunc`, 0x803fdbb0) is an AOBJ animation hook, not a bridge.

Instead the registry is consumed by per-character / per-rider lighting state.
`AreaLight_RegistryWalk` is called from `Rider_UnkThink` (0x8018e9a8 -> 0x80190340, consumer
at `rider+0x294`, records into `rider+0x318`), from a per-object pose update (0x801d6c00,
consumer at `obj+0x300`, records into `obj+0x400`), and from `Machine_Create`
(0x801c5888 -> 0x801d6bd4). The kind-0 worker `AreaLight_InsertSorted` (0x80079a60) inserts
a sorted record (max 9) into a 5-int-stride array on the consumer, with the count at
consumer +0xC8.

**Practical consequence:** sky-preset color shifts affect character and machine shading, but
the stage hardware LOBJs keep their original colors for the whole session - they are never
repainted by a sky transition. Visual change on terrain across a preset transition comes
from the fog blend and the EFB clear color, not from the AreaLight.

## Shadows

City Trial shadows are fixed straight-down textured blobs, completely independent of the
lighting/sky system. Changing the sky preset, the AreaLight, or any LObj direction does not
move, rotate, or darken a CT shadow. There are two unrelated shadow systems in the binary:

| System | Functions | Used by | Nature |
|--------|-----------|---------|--------|
| **SimpleShadow** (blob) | `SimpleShadow_*` (0x8027ae50-0x8027c2xx) + per-entity `*_Shadow*` | **City Trial** (riders, machines, items, event actors) | textured soft-blob quad laid flat on the ground straight under the entity |
| **HSD projected shadow** (`lbshadow.c`) | `fn_shadowRendering` (0x8007ade8), `fn_makeShadow` (0x8007b284), `HSD_Shadow*` (0x8041cf1c+) | **Top Ride only** | real silhouette projected from a scene light's POV |

The lighting-aware path is not on the CT path: `fn_makeShadow` is reached only from a Top
Ride mode-init routine (0x802823fc), where it asserts "can't find shadow light", reads a
light's direction as normalized `HSD_LObjGetPosition - HSD_LObjGetInterest`, and builds a
projection camera from it. CT never invokes it.

### SimpleShadow mechanics (the CT path)

- **One scene-wide manager GObj**, not one render GObj per entity.
  `SimpleShadow_CreateManager` (0x8027b294) zeroes the 0xCC-byte manager at 0x8055EFC0, does
  `GObj_Create(class 0x18, pri 0xF)` + `GObj_AddGXLink(SimpleShadow_GX, gx_link 2, pri 1)` +
  an AddProc at pri 0x15. Called from `SceneLoad_3D` (0x80014700, the shared 3D-gameplay
  loader) and the title/menu loaders.
- **Per-entity shadow GObjs** are lightweight class-8 objects holding only a blob JObj,
  created by `SimpleShadow_CreateGObj` (0x8027b418) and linked into the manager's list. The
  render flag lives at shadow-node +0x30 (`SimpleShadow_SetRenderEnable`/`Disable`/
  `GetRenderFlag`, 0x8027b524/0x8027b534/0x8027b544).
- **Ground find is a down-raycast, never a light vector.** Placement uses `EnvColl_Raycast`
  (0x800d1ac4) on a fixed +/-Y segment (`EventActor_ShadowInit` 0x80200208 builds
  `pos +/- offset*+Y`).
- **Size fades with height.** Scale = base x `(maxHeight - height) / maxHeight`, culled past
  `maxHeight`. `SimpleShadow_UpdateSize_` (0x8027b568) writes the scale,
  `SimpleShadow_UpdatePos_` (0x8027b588) the position; `CityItem_UpdateShadowSizeAndVis`
  (0x80261aa8) is the item variant.
- **Render** is `SimpleShadow_GX` (0x8027ae50) walking the manager's list and
  `HSD_JObjDispAll`ing each enabled blob, wrapped in `HSD_FogSet`. The blob
  material/texture/blend is a static descriptor set by `Shadow_MObjCallback` (0x8027bd68).
- **Per-entity enable gating** reads entity state bits:
  `Rider_UpdateSimpleShadowRender` (0x80195800) on `RiderData+0x825`/`+0x821`;
  `Machine_UpdateSimpleShadowRender` (0x801d108c) on `MachineData+0xc37`/`+0xc30`;
  `Item_UpdateSimpleShadowRender` (0x80261a54) on the item's shadow GObj plus
  `item+0x35a`/`+0x358`. Event actors toggle via `EventActor_SetShadowActive`/
  `ClearShadowActive` (0x802000ac/0x802000c0, bit 4 of `EventActorData+0xb0b`).

Light-responsive shadows in CT would be new work: either feed a light direction into the
SimpleShadow placement (replacing the fixed +/-Y ray and slanting the quad) or port the Top
Ride projected path into the CT scene.

## Adding a Custom Light

Putting a real positioned light into City Trial takes four calls, mirroring the menu code
in `CitySettings_CreateLObj`: `GObj_Create(38, 32, 0)`, `LObj_LoadDesc(&desc)`,
`GObj_AddObject(g, HSD_OBJKIND_LOBJ, l)`, `GObj_AddGXLink(g, LObj_GX, 0, 0)`, then
`LObj_SetPosition` / `LObj_SetInterest` to place it (each allocates a backing WObj on first
call). `HSD_LObjAddCurrent` is **not** needed - `LObj_GX -> HSD_LObjSetCurrentAll` rebuilds
the active list every frame from the chain attached via `GObj_AddObject`. All four symbols
are already exported in hoshi's `link.ld`.

`LObjDesc.flags` low 2 bits are the type (0 AMBIENT, 1 INFINITE, 2 POINT, 3 SPOT - what
`HSD_LObjSetupInit` dispatches on); bit 2 DIFFUSE, bit 3 SPECULAR, bit 4 ALPHA, bit 5
HIDDEN. `attnflags` bit 0 is RAW_PARAM (the `u.attn` block is 6 raw floats). Choose exactly
one of `u.point` / `u.spot` / `u.attn` for the type.

To follow a moving entity, the WObj's position must be rewritten every frame - either by
writing the Vec3 directly into the WObj on each think tick, or by attaching the WObj to a
JOBJ through the broader `WOBJ` API.

### Visibility: the engine aggregates light_mask globally

There is **no per-material `light_mask` baked into stage geometry**. The standard
`HSD_SetupChannelMode` path (0x803f7d44, used by every world MObj) does not read a static
`light_mask` field. Just before emitting `GXSetChanCtrl` it queries three globals rebuilt
every frame at the top of `HSD_LObjSetupInit`:

| Global | Getter | Source |
|--------|--------|--------|
| `DAT_805de214` | `HSD_LObjGetLightMaskDiffuse` (0x803fdb14) | OR of `lobj.lightid` over active LOBJs with `LOBJ_DIFFUSE` |
| `DAT_805de218` | `HSD_LObjGetLightMaskSpecular` (0x803fdb2c) | same for `LOBJ_SPECULAR` |
| `DAT_805de220` | `HSD_LObjGetLightMaskAlpha` (0x803fdb24) | same for `LOBJ_ALPHA` |

`lightid = HSD_Index2LightID(slot)` - `1 << slot` for slots 0..7, `0x100` for slot 8. So an
LOBJ added to the active list before `HSD_LObjSetupInit` runs is assigned a slot, has its
`lightid` OR'd into the right channel global, and becomes visible to every MObj whose
rendermode has `RENDER_DIFFUSE` - with no chan-ctrl patching anywhere.

A stage MObj could in principle override this by carrying its own `MatColorChan` chain (read
by `HSD_SetupChannel` at +0x14/+0x1c/+0x28), but `MObjLoad` (0x803f9f04) only copies a
0x14-byte `HSD_Material` and builds no chain, and the only callers of the chain walker
`HSD_SetupChannelAll` are at 0x8041d32c (a shadow/scratch path). Every CT stage MObj goes
through the global aggregation path.

### City Trial geometry is mostly unlit

`HSD_SetupChannelMode` routes a material through the lit color channel only when its
rendermode has `RENDER_DIFFUSE` (bit 2). Otherwise the channel is `GX_DISABLE`d and color
comes straight from baked per-vertex colors (`RENDER_VERTEX`) or the material register
(`RENDER_CONSTANT`), texture-modulated. No hardware light touches an unlit material.

In `GrCity1Model.dat` the terrain is overwhelmingly unlit:

- `RENDER_DIFFUSE` on 4 of ~180 terrain MObjs; `JOBJ_LIGHTING` on 3 of 317 joints.
- Render-mode tally: `0x4012` VERTEX x76, `0x2011` CONSTANT x19, `0x40002011`
  CONSTANT|XLU x14, `0x2012` VERTEX x13, `0x60002011` x11, `0x2015`
  CONSTANT|**DIFFUSE** x4 (the only lit ones), plus one multi-textured `0x00ac00ad`
  CONSTANT|DIFFUSE|SPECULAR surface.
- **Vertex normals on only 6 of 208 terrain POBJs.** Diffuse GX lighting is
  `matColor x (ambient + sum light*(N.L))`; with no `N` the light term is zero, so forcing
  `RENDER_DIFFUSE` onto a normal-less surface cannot make it respond to any hardware light -
  it just darkens to the ambient-only term. The 6 normal-bearing POBJs are exactly the
  DIFFUSE ones. Meanwhile **151 of 208 POBJs carry baked per-vertex colors** (DIRECT RGBA4,
  inline in the display list) - that baked color *is* the city's appearance.

So a new LOBJ lights riders, machines, items and those ~5 DIFFUSE city surfaces, not the
buildings, roads or ground. For a city-wide visual change you must drive the baked path -
fog color, EFB clear, lbfade overlay - or rewrite the baked vertex colors directly (those
live in POBJ display lists, so a `DCFlushRange` is required after editing for the GP to see
it). Flipping `RENDER_DIFFUSE` does not help.

## Runtime Addresses

These structures are heap-allocated per City Trial session, so their absolute addresses
change every run. Resolve them live from the stage GObj rather than hard-coding:

```
GrObj     = *(0x805DD6CC)        // *stc_grobj
skyGObj   = *(GrObj + 0x168)
HSD_Fog   = *(skyGObj + 0x28)    // type +0x08, fog_adj +0x0C, start +0x10,
                                 //   end +0x14, color +0x18, scale +0x20
SkyState  = *(skyGObj + 0x2C)    // lerp-out/start +0x08, start_sky +0x14,
                                 //   current_sky +0x18, preset_ix +0x1C
AreaLight = *(GrObj + 0x718)     // flags +0x0D, color +0x10, hw_color +0x14,
                                 //   dir +0x18, vis +0x38 (bit 0x80)
jointTbl  = *(GrObj + 0x104)     // entry[idx]: jobj +0x00, desc +0x04
                                 //   (visibility = jobj->flags +0x14 bit 0x10)
backdrop  = *(GrObj + 0xF4)      // JObj -> DObj +0x18 -> MObj (skybox material color)
```

City Trial under preset 0 "Day": `HSD_Fog` carries `type=2`, `start=210`, `end=665`,
`color=#9FCFFFFF`, `scale=1.0`, `fog_adj=NULL`; `SkyState.preset_ix=0`; the global
`AreaLight` carries `color=#D7D7FFFF`, `hw_color=#FFFFFFFF`, `dir=(-0.40,0.80,0.50)`,
`vis=0x80` - i.e. the preset[0] row of the table above. `jointTbl[62]` and `[63]` (the glow
billboards) both have render-flag bit 4 set.

**0x80557484** is the one fog-pipeline address stable across runs: the global EFB clear
color (BSS, RGBA8888, `stc_global_fog_color` in `stage.h`), written by `Sky_Update` step 4
and read by `World_CObj+0x144`.

The skybox MOBJ's material color register is written once by `MObjLoad` at stage load and is
never updated by sky-preset transitions - the visible sky tint comes from
`Sky_DrawTintQuad`'s translucent far-plane quad, not from re-tinting that material.

## Custom Weather Mod

`mods/custom_weather/` replaces vanilla sky selection in City Trial, appends its own
presets, and layers world-space weather effects on top of the engine system above.
Everything below is mod code, not vanilla behavior.

### Preset-array extension

`ExtendPresetArray` (`custom_weather.c`) copies the 17 vanilla `SkyPresetEntry` records out
of the stage file into a static `extended_presets[WEATHER_TOTAL]` buffer, appends the custom
presets, then repoints `sky_block->preset_header->{preset_array, preset_count}` at the
longer array. Because that pair is exactly what `Sky_GetPresetCount` and
`Sky_BeginTransition` read, vanilla code (event transitions, the debug selector) can index
custom presets after the repoint. The function is idempotent and runs on every stage load.

Each custom entry is cloned from a vanilla `base_preset` (inheriting its `AreaLightData`
flags/attn/header), then overrides fog color/start/end, `sky_ambient_color`, the AreaLight
color/hw_color/direction, and `light_vis_flag` (from `char_dir_lit`). `fade_color` is zeroed
and `transition_frames` forced to 1 - the mod snaps rather than using the vanilla fade, and
drives its own overlay through `screen_tint`.

### Hooks

| Site | Where | Replaces |
|------|-------|----------|
| 0x8010f1a4 | `Sky_Init`, City Trial (stage kind 9) random block | `Gm_Roll` over `{0,10,11,12}` -> `Sky_SetPresetIndex` |
| 0x8010f224 | `Sky_Init`, City Trial Free Run (stage kind 52) | hardcoded preset 0 |
| 0x800ce648 | the instruction right after `bl Sky_Update` in the CT per-frame driver | nothing - appends `CustomWeatherRuntime_Tick(grobj)` |
| `Sky_TransitionGlobal`, `Sky_RestoreGlobal` | `CODEPATCH_REPLACEFUNC` from `event_sky.c` | event-driven sky swaps |

Both `Sky_Init` hooks land on `CustomWeather_OverrideSky`, which extends the array then
picks uniformly among the **enabled** presets (falling back to Day if none are) and calls
`Sky_SetPresetIndex`.

The per-frame hook is placed *after* `Sky_Update` on purpose: the mod's writes have to layer
on top of the per-frame sky writes rather than be clobbered by them. `r31` holds `grobj`
across the `bl`, and the trampoline re-runs the clobbered `lwz r0,4(r31)`.

### Per-frame runtime

`CustomWeatherRuntime_Tick` (`custom_weather_runtime.c`) early-returns on any non-`GR_CITY1`
ground. On a preset change it applies the optional static layers of the active
`CustomPresetDef`; the global fog scale and the effect ticks run every frame.

| Layer | Mechanism | Code |
|-------|-----------|------|
| Terrain re-tint | writes `(*stc_main_light)->color`/`hw_color` - the primary chain's INFINITE LOBJ, which sky presets never touch | `ApplyTerrainTint` |
| Ambient (slot-8) re-tint | writes the slot-8 ambient LOBJ resolved from `stc_lobj_hw_slot_table[8]`; the HW table lags think by a frame, so it retries | `ApplyAmbientTint` |
| Fog curve | writes `HSD_Fog.type` from `fog_curve`; `Sky_Update` never lerps type, so one write per preset holds | `ApplyFogCurve` |
| Screen overlay | `Sky_BeginFade(grobj, &screen_tint, 30)` once per preset activation, gated on `grobj->fade_slot_id != 0` | `CustomWeatherRuntime_Tick` |
| Global fog distance | every frame: `HSD_Fog.scale = CustomWeather_GetFogScale()`, the menu-driven multiplier on the fog far wall; covers vanilla presets too | `CustomWeatherRuntime_Tick` |

Effect ticks then run in a fixed order - lightning, wind, rain, snow, hail, puddles, trees,
clouds, moon, stars, volcano, tornado - with wind before every layer that reads its vector.
`event_sky.c` is a sibling module that installs its own boot hook rather than running from
this tick, and `Tornado_OnFrameEnd` runs from the mod's `ModDesc.OnFrameEnd` instead.

Every effect module follows the same shape: `X_SetActive(def)` latches the preset's config on
a preset change (NULL or `enabled == 0` turns it off, and each 0 numeric field resolves to
the module's default), `X_Tick()` advances the layer and lazily creates its render GObj, and
`X_Reset()` runs from `ResetPerStage` on CT teardown and only drops cached handles - the
engine frees the GObjs itself. Modules are independent of each other and of the static
sky/fog/light fields, so a preset can carry any combination.

Shared GX plumbing is in `weather_fx.c`. `WeatherGX_BeginXlu(cam, additive, line_width)` sets
flat per-vertex color, alpha or additive blend, `GXSetZMode(GX_ENABLE, GX_LEQUAL,
GX_DISABLE)` (depth-tested but not depth-writing, so stage geometry occludes the layer),
`GX_CULL_NONE`, and loads the active camera's view matrix as the position matrix so world
coordinates work from every split-screen viewport. `WeatherGX_EnsureLayer` creates the layer
GObj. All layers draw on the world camera's gx_link 0, XLU sub-pass (`pass == 1`), gx_pri 0.

### WeatherKind presets

`WEATHER_VANILLA_NUM = 17`, `WEATHER_CUSTOM_NUM = 11`, `WEATHER_TOTAL = 28`
(`custom_weather.h`). Custom presets occupy indices 17-27:

| Idx | WeatherKind | Name | base_preset | Effect layers |
|-----|-------------|------|-------------|---------------|
| 17 | `WEATHER_BLOOD_RAIN`   | Blood Rain   | Red Vignette (15)  | rain + lightning |
| 18 | `WEATHER_STORM`        | Storm        | Dark Vignette (5)  | lightning + rain + wind + clouds + volcano (plasma) |
| 19 | `WEATHER_RAIN`         | Rain         | Gray Sky (13)      | rain + wind + puddles |
| 20 | `WEATHER_HAILSTORM`    | Hailstorm    | Gray Sky (13)      | rain + hail + wind + clouds |
| 21 | `WEATHER_SNOWSTORM`    | Snowstorm    | Dense Fog (9)      | snow + wind + clouds, exp2 fog curve |
| 22 | `WEATHER_MOONLIGHT`    | Moonlight    | Midnight (1)       | moon + stars (frequent meteors) |
| 23 | `WEATHER_COTTON_CANDY` | Cotton Candy | Pink Sky (8)       | clouds |
| 24 | `WEATHER_TOXIC`        | Toxic        | Dark Vignette (5)  | light rain + wind + puddles |
| 25 | `WEATHER_BUBBLEGUM`    | Bubblegum    | Pink Sky (8)       | clouds |
| 26 | `WEATHER_VOLCANIC`     | Volcanic     | Dark Vignette (5)  | volcano (fire) + snow (ashfall) + wind + clouds |
| 27 | `WEATHER_TORNADO`      | Tornado      | Gray Sky (13)      | tornado + rain + lightning + strong wind + low clouds; green supercell sky |

### CustomPresetDef

Declared in `custom_weather.h` and grouped by on-screen effect, not by engine mechanism. The
static fields are `base_preset`, `fog_color`/`fog_start`/`fog_end`, `sky_color`,
`terrain_diffuse`/`terrain_specular`, `char_diffuse`/`char_specular`/`char_dir`/
`char_dir_lit`, `char_ambient`/`char_ambient_specular`, `fog_curve` and `screen_tint`; the
layer configs are `rain`, `hail`, `snow`, `lightning`, `wind`, `puddles`, `clouds`, `moon`,
`stars`, `volcano` and `tornado`.

Two conventions run through the whole struct: `0` on an optional static field means "inherit
from `base_preset`", and inside a nested def `enabled = 0` disables that layer while a 0
numeric field takes the owning module's default. Alpha in a color field is meaningful -
`sky_color`'s A is skybox opacity (0 leaves the vanilla skybox visible) and `screen_tint`'s A
is overlay strength.

The global **Fog Distance** menu setting is not one of these fields: it scales
`HSD_Fog.scale` for every CT preset, vanilla and custom, via `CustomWeather_GetFogScale`.

### Lightning (`lightning.c`)

Long random lulls punctuated by a brief, bright flash. Because KAR stage geometry does not
read hardware light colors, the terrain flash comes from punching `HSD_Fog.color` and
`*stc_global_fog_color` toward the flash color on a decaying strobe envelope, plus pulling
`fog.start` in so the brightness reaches near geometry rather than only the distance band. A
spare INFINITE LOBJ flashes alongside to catch characters and machines, which *do* read
hardware lights. None of this needs restoring - `Sky_Update` rewrites fog and the EFB clear
every frame, so the preset values return on their own when a strike ends.

`LightningDef` carries `flash_color`, `flash_frames`, `min_lull`/`max_lull` and `bolt`.
Defaults are a near-white flash, an 18-frame envelope and 180-420 frame lulls, with the first
strike 30 frames into the preset. Each strike additionally rolls its own length scale, peak
intensity and strobe on/off/floor values so no two flashes read alike.

The module owns two lights, both built with the custom-light recipe above: an INFINITE flash
light overhead and a POINT light (computed ref-brightness attenuation) parked at the bolt
midpoint.

**Visible bolts** are opt-in via `lightning.bolt` (`LightningBoltMode`: off / augment /
replace). A bolt is a jagged top-to-ground polyline of depth-tested GX line segments on the
world XLU pass - occluded by stage geometry exactly like the rain - lit by the midpoint POINT
light. Geometry is regenerated per strike in **world space**, so every split-screen camera
draws the same bolt from its own pass: a 13-segment main channel from y=820 to y=-40 with
per-step horizontal jitter, plus one 4-segment fork off the upper third, anchored at a
uniform random XZ inside the stage OOB box. It draws in two passes - a wide dim glow in the
flash color and a thin white-hot core - both strobing on the shared flash envelope.
`augment` keeps the screen flash, `replace` draws only the bolt. Storm and Blood Rain both
set `augment`.

### Rain (`rain.c`)

A field of falling translucent line segments drawn *in the stage*, not as a screen overlay:
immediate-mode GX geometry on the world camera's pass, depth-tested, so buildings and terrain
occlude the drops behind them. The whole field is one
`GXBegin(GX_LINES, ..., density*2)` batch after `WeatherGX_BeginXlu`.

**Camera-following toroidal box.** The drops are a fixed pool (cap `RAIN_MAX_DROPS` = 1600)
of random offsets in a cube of edge `RAIN_BOX` (1000) that re-centers on the camera every
frame, so you can never outrun the rain. All drops share one `drift` vector advanced per
frame by the resolved velocity (dominant `-Y` fall plus the wind's X/Z); a drop's world
position is `eye + wrap(offset + drift) - RAIN_BOX/2` per axis, where the single-subtract
`wrap` recycles anything that leaves the box. The camera eye is derived from the view matrix
as a rigid inverse (`eye = -R^T * t`), so one pool serves every split-screen viewport with no
per-camera state. Rain, snow, hail and the celestial layers all use this same eye derivation.

Slant is not a `RainDef` field: `rain.c` reads the global wind vector every frame, so a
preset's `WindDef` drives both the rain's slant and the rest of the weather. Defaults are
pale blue-gray, 900 drops, 26 units/frame fall, line width 10, streak 1.5.

**Performance.** `rain.density` (clamped to the pool cap) is the number of GX line primitives
and the dominant cost lever; the batch is re-emitted once per camera, so 3-4P split-screen
multiplies it. If this immediate-mode path ever proves too expensive, the HSD point-particle
pool is the natural fallback - `psRenderParticles` (0x80433f00) already draws
velocity-stretched `GX_LINES` streaks (`Ptcl_EmitStreak`, 0x80436460) and `GX_POINTS` in
world space from decoupled tick/render walks. The trade-off is that the pool copies a static
`PtclDesc` template from the fixed ROM descriptor table (`descTable[type][sub]`, base
0x8058c708), with no clean runtime descriptor registration, so it is far less tunable from
mod code.

### Snow (`snow.c`)

Same camera-following box model as the rain (`SNOW_MAX` 1000 pool, `SNOW_BOX` 1000 edge,
shared wrapped drift), but each flake draws as a small camera-facing glow instead of a line
streak, and falls far slower (default 3 units/frame against rain's 26). Riders pass through
it. Only the first `density` flakes are drawn.

**Flutter** is the one thing snow adds on top of the shared drift: each flake owns a phase, an
angular speed and a unit horizontal sway direction, all seeded once, and gets a sideways
offset of `flutter * sin(time * freq + phase)` along that direction added to its X/Z in the GX
callback. A shared clock advances once per `Snow_Tick`. Flutter = None freezes the sway to a
straight fall.

A flake is a `GX_TRIANGLEFAN` with an opaque center and 6 transparent rim vertices,
billboarded from rows 0/1 of the world->view rotation, drawn with straight alpha blend
(`additive = 0`) so flakes read white over the world rather than glowing. Fog is left on, so
distant flakes tint toward the world fog. Per-flake size varies +/-0.5 about the resolved
base, seeded once. Defaults: soft white, 600 flakes, fall 3.0, flutter 1.6, radius 4.0.

Snowstorm is the authored preset. The **Snow** menu's Intensity and Fall Speed are latched by
`Snow_SetActive` (so they apply on the next preset change or CT re-entry) while Flutter and
Wind Slant are read live.

### Wind (`wind.c`)

A single global horizontal vector several systems read each frame so they all blow the same
way: rain, snow and hail slant to it, clouds drift with it, trees lean to it, airborne City
Trial items are nudged (`Wind_ApplyToItems`), and gliding machines are pushed
(`Wind_ApplyToMachines`, airborne only, scaled by the machine's glide stat). The module owns
no GObj and installs no hook; it exposes `Wind_GetVector` for everyone else.

The vector is not static. Speed pulses (gustiness) and heading wanders (chaos), each a
smoothed random walk around the preset's base: a fresh target is rolled every period (40
frames for gust, 90 for heading) and eased toward each frame (0.04 / 0.02), with heading
deviation bounded by 75 degrees x chaos. `WindDef` defaults are 6.0 units/frame at heading 90
(0 = +Z, 90 = +X), gustiness 0.35, chaos 0.25. Coupling constants: 0.08 of the wind added to
an airborne item's velocity per frame; 0.012 at full glide for machines, with a 0.40 floor on
the glide-stat scale.

**The two physics pushes are held until the round timer starts** (`Weather_RoundProgress()` is
negative through the intro). Riders are still boarding then and read as airborne, so a push
would shove them off the start line, and the opening item drop would be blown across the city
before play begins. Precipitation slant, cloud drift and tree lean run throughout, so the
weather still looks alive during the countdown.

An item counts as airborne when `ItemData.is_airborne != 0` - the engine writes 0 at every
land transition and 1 (or -1, meaning airborne with the ground raycast suppressed) whenever it
leaves a surface. The `x35a` bit 0x10 is **not** usable for this: it latches the first time
the envcoll raycast finds ground beneath the item, which on a sky drop happens on its first
frame hundreds of units up, and it is never cleared afterwards.

### Hail (`hail.c`)

A damaging layer that rides on the rain (stone count is gated on `Rain_IsActive()`). While a
rain preset is active and hail is on, each machine carries a tight box of real world-space
hailstones falling under gravity plus the wind slant. Unlike a raindrop - camera-relative,
with no persistent world position - a hailstone is a true world point, so the hit is honest:
entering a machine's body sphere (radius 20, lifted 10 off the machine origin) deals
`Machine_GiveDamage(md, 1, mg)` and respawns the stone at the top of its box. A 10-frame
per-machine cooldown caps it to chip damage; a fully eroded machine dies through the engine's
own death path.

The stones fall in true world space (they do not slide with a moving machine) but the box that
re-seeds them follows the machine, so speed cannot outrun the storm. **Cover** can: a machine
with stage geometry overhead has its whole cloud suppressed. Shelter is found by casting a ray
*down* from the top of the playable volume (`stage_node->oob_max.Y` + 50) to just above the
machine - a down-cast detects a roof by its walkable top face, so it works regardless of
triangle sidedness. The probe is throttled to every 8 frames and cached per machine.

Box geometry is a 120-unit XZ half-extent, spawning 220 above the machine, recycling 90 below
or 300 horizontally away, falling 32/frame. Stones draw as short thick icy GX lines, depth
tested like the rain. `HailDef` is `enabled` plus `amount`, a density multiplier over
`HAIL_BASE_STONES` (20) capped at `HAIL_MAX_STONES` (32), where 1.0 is Normal. Only Hailstorm
authors it.

### Puddles (`puddle.c`)

A roaming field of shallow pools lying on the City Trial ground that drag machines driving
through them. Each pool slot runs an independent lifecycle - dormant, fade in (24 frames),
hold (300-900), fade out (36), gap (120-480) - with first appearances staggered over the
round's opening (up to 240 frames).

Each time a slot wakes it re-rolls a spot: up to 6 attempts raycasting straight down inside
`PUDDLE_PLAY_FRACTION` (0.72) of the OOB box, keeping only hits whose normal Y is at least
0.85 so pools never climb walls, then laying a translucent ellipse (short axis 0.55-1.0 of the
radius) flush in the surface plane via a tangent basis from the ground normal, lifted 2.0
along the normal to beat z-fighting. Discs are `GX_TRIANGLEFAN`s (22 rim segments, rim alpha
half the center alpha) on the world XLU pass.

Every frame, any grounded live machine whose XZ falls inside a pool ellipse has its horizontal
velocity damped by `1 - (1 - slow_factor) * menu_scale * pool_alpha`, capped at 0.99 - a
self-correcting drag that recovers on exit and bites less while a pool is forming or drying.
One pool's drag applies per frame even where pools overlap.

`PuddleDef` defaults are a bright reflective pool, 24 pools, radius 32, factor 0.90 (10%
velocity damp per frame), with the slot count capped at `PUDDLE_MAX` (64). The menu's
**Roaming** off pins the hold timer so the field stays put, and **Show Puddles** can hide the
discs while keeping the drag.

### Clouds (`clouds.c`)

A low deck of soft clouds drifting over the map. Each cloud is a cluster of 5 overlapping
flattened translucent spheroids - real world-space geometry (coarse UV spheres, 4 latitude
bands x 8 sectors, vertical squash 0.55), not billboards - so riders fly straight through a
cloud and vision inside one is heavily obscured. Puff 0 anchors the cluster core at full
radius; the rest scatter within 1.6 / 0.45 of the radius horizontally / vertically and shrink
by up to 0.85 (floored at 0.15) according to `puff_var`. `puff_var` is the puff-to-puff
spread within a cluster, distinct from `size_var` which spreads whole cloud sizes, so a cloud
reads as lumpy rather than as a stack of equal blobs.

The deck sits at either the preset's absolute `height` or 0.35 of the way up the OOB box, plus
the menu height offset, with a per-cloud spread. Clouds inherit the world fog, so a deck placed
above a preset's `fog_end` fogs out - keep it below the fog wall.

Drift is 0.30 of the wind's magnitude, floored at 0.45/frame and capped at 3.5; a calm preset
still drifts them slowly along a fixed 40-degree heading. A cloud reaching an OOB wall wraps
to the opposite wall, where a horizontal-clearance edge fade (260 units) holds it invisible so
re-rolling its shape and height is hidden, then it ghosts back in as it drifts inward.

Each spheroid is one `GX_TRIANGLESTRIP` per latitude band of flat-color, alpha-blended,
depth-tested-but-not-depth-writing geometry with `GX_CULL_NONE`, so it still fills the view
from the inside. A per-vertex silhouette alpha (`|normal . camera-forward|`, floored at 0.12)
feathers each spheroid toward a soft edge and hides most of the unsorted-translucency noise.
No texture asset. Defaults are 12 clouds, base puff radius 58 x a global 1.2 scale, size_var
0.35, puff_var 0.6, height_var 90, capped at `CLOUD_MAX` (30).

### Moon (`moon.c`)

A distant fog-free disc fixed on the City Trial sky dome that crosses the sky over the round,
shows craters and one of the eight canonical lunar phases, and can cast a directional
moonlight that makes it the scene's dominant light.

**Placement - camera-anchored, dome-clamped.** Each frame the moon sits at
`P = eye + skydir * dist`. Anchoring to the eye gives a consistent apparent elevation and no
parallax swim. `dist` is the minimum of three limits: `MOON_MAX_DIST` (1800, the desired
anchor distance); `MOON_DOME_FRAC` (0.82) x the eye-to-dome distance along `skydir`; and
`MOON_FAR_FRAC` (0.85) x the camera far plane, so it is never frustum-clipped. The City Trial
backdrop is a depth-writing sphere at the world origin of modelled radius `MOON_DOME_R`
(2500); a moon beyond it would be occluded and pop out as the camera nears the edge, so the
ray-march `t_dome = -e.d + sqrt((e.d)^2 + R^2 - |e|^2)` and the 0.82 fraction keep it inside
from every camera position. The disc radius scales with the final `dist` about `MOON_REF_DIST`
(1800), so apparent size is constant even when clamped. Terrain nearer than `dist` occludes
the moon through the normal depth test; the fog-free far sky and the dome (always farther) do
not. The disc is a camera-facing billboard from rows 0/1 of `COBJ.view_mtx`.

**Motion - synced to the match clock.** The moon crosses the sky once over the round, tied to
the City Trial match timer rather than a private counter. `grBoxGeneInfo` (`*stc_grBoxGeneInfo`,
`*(r13+0x610)`) exposes a pre-normalized `float match_progress` at +0x2a4 that the game
advances 0.0 -> 1.0 over the round and freezes during pause / match end; bit 0x40 of
`flags_x2a8` marks the pre-round intro. `MoonProgress()` returns 0 during the intro or when the
info pointer is null, holding the moon at its rise point. From progress `p`:

```
el  = arc_height_deg * sin(p * PI)      // 0 at the ends, peak at mid-round
az  = rise_bearing_deg + 180 * p        // rises at rise_bearing, sets opposite
dir = (cos el * sin az, sin el, cos el * cos az)
```

`arc_height` is the peak elevation in degrees (default 26, a low horizon-hugging arc);
`rise_bearing` defaults to 95, roughly east. When `dir.Y <= 0` the moon and its light are
skipped.

**Phase geometry.** The lit region is drawn as 28 horizontal scanline bands. For a band at
billboard height `v` the disc half-width is `w = sqrt(r^2 - v^2)`; the terminator is a
per-scanline ellipse of horizontal half-width `|k|*r`, `k` in `[-1, 1]`. Lit on camera-right
means `u` in `[-k*w, +w]`, lit on camera-left means `u` in `[-w, +k*w]` - the far edge is
always the disc rim, the near edge the terminator. `PhaseParams` maps each `MoonPhase` to its
`(k, side)`: Full `+1.0`, Waxing Crescent `-0.5` right, First Quarter `0.0` right, Waxing
Gibbous `+0.5` right, Waning Gibbous `+0.5` left, Last Quarter `0.0` left, Waning Crescent
`-0.5` left, New `-1.0` (not drawn). The lit side is camera-relative, matching a
player-selected phase rather than tracking a sun position. Each band is split into 6 columns
and emitted as a `GX_TRIANGLESTRIP`; empty scanlines (deep crescents) collapse to zero width
and are skipped.

**Soft rim and craters.** Each disc vertex's alpha is scaled by `MoonRimFade` - full inside
0.85 of the radius, falling linearly to 0 at the rim. Splitting each band into columns keeps
the fade localized to the rim and crescent tips instead of gradient-washing the lit face; the
terminator stays crisp because its interior vertices sit well inside the fade radius. Craters
are 13 darker translucent `GX_TRIANGLEFAN` patches seeded once over the inner disc
(`rad = sqrt(rand) x 0.62`) so they stay off the soft rim, each drawn only when `CraterFits`
confirms the whole circle is inside the opaque interior and on the lit side of the terminator -
so a crater never overhangs into the dark or off the disc edge.

**Fog-free draw.** At ~1800 units the disc is far past the fog wall (`fog_end` ~420-665), so
the GX callback brackets the draw with `HSD_FogSet(NULL)` ... `HSD_FogSet(live_fog)`, restoring
the live fog from `(*stc_grobj)->sky_gobj->hsd_object` so later geometry stays fogged. Depth is
kept (not forced to `GX_ALWAYS`) so terrain occludes the moon; visibility against the sky comes
from the dome clamp, not from disabling depth.

**Moonlight and distant-light suppression.** With `moon.light` set, the module creates one
`LOBJ_INFINITE | LOBJ_DIFFUSE | LOBJ_SPECULAR` light, points it along the moon's sky direction
each frame the moon is up, and colors it from `light_color x Brightness`. It lights riders,
machines, items and the few DIFFUSE terrain materials - the bulk of the stage is unlit, so a
moonlit preset's darkness comes from its fog / ambient / screen-tint, not from the light.

To make the moon dominant it also removes the leftover distant stage light. City Trial has two
distant INFINITE lights; the primary (`*stc_main_light`) is already owned by the runtime's
terrain tint, so the moon module owns only the **secondary**: it walks
`stc_lobj_hw_slot_table[0..7]` for the INFINITE light that is neither the primary nor its own,
caches its color, and zeroes it. Zeroing is durable because `HSD_LObjSetCurrentAll` rebuilds
slot assignment each frame but never rewrites `LOBJ.color`. Splitting ownership this way keeps
exactly one owner per light. `RestoreSecondary` puts the cached color back when moonlight turns
off or the preset changes; on teardown `Moon_Reset` only drops handles, so nothing is written
through a stale pointer. The slot table lags the think hook by a frame, so the secondary
resolves lazily with retry.

### Stars (`stars.c`)

A field of faint camera-anchored dots over the City Trial sky dome, drawn additively as soft
glows with per-star size and brightness variance, each shimmering on its own phase. Like the
moon, a star is a celestial billboard fixed on a world sky direction and clamped inside the
backdrop dome: each star owns a fixed unit direction seeded once uniformly over the sky cap
above 10 degrees elevation, and `dist` is the minimum of `STAR_MAX_DIST` (6000, always clamped
smaller), `STAR_DOME_FRAC` (0.9) x the eye-to-dome distance, and 0.9 x the camera far plane.
Each dot's world radius scales with its final `dist` about 1800, so apparent size is constant.
Panning sweeps across a world-fixed field with no parallax swim.

A star is a `GX_TRIANGLEFAN` with a bright center and 6 transparent rim vertices,
camera-facing, drawn after `WeatherGX_BeginXlu(cam, additive=1, 0)`. Additive blend means dots
glow, never darken the sky, and draw order against the other translucent layers is irrelevant.
The draw is bracketed with `HSD_FogSet(NULL)` / restore so distant dots are not washed to the
fog color.

**Twinkle.** `Star_Tick` advances a shared clock by 1 each frame. Each star has a random phase
and angular speed (0.05-0.14 rad/frame, roughly 0.75-2 s per cycle), so the field shimmers out
of sync; per frame its brightness is multiplied by `1 + tw * 0.7 * sin(time * speed + phase)`,
with the additive blend clamping the overshoot. Twinkle = None freezes the field.

**Field composition.** The field is scattered once per preset activation by `Star_Arm`, with no
stage dependency, so Density and Size Variance apply on the next preset change or CT re-entry
while Twinkle, Luminosity and Color are read live. Count is `density x menu factor` clamped to
`STAR_MAX` (220). `SeedStar` rolls a sky-cap direction (uniform via `z` in
`[sin(min_elev), 1]`), a size, a base brightness in 0.35-1.0 so some dots are dim, and a
twinkle phase/speed. A dot's additive alpha is `color.a x luminosity x star.bright x twinkle`,
clamped to 255 and skipped below 1. Defaults: 120 stars, twinkle 0.5, luminosity 1.0, size 5.5,
size_var 0.5.

**Shooting stars.** Meteors ride along with the starfield - same GX callback, same additive
fog-free draw - gated on the star feature being active and the effective cadence not being Off.
A pool of 4 holds concurrent meteors; `Shoot_Tick` (called from `Star_Tick`, so aging happens
once per frame rather than once per viewport) ages live ones and launches a new one when a
random lull timer expires, re-seeding it from the effective cadence (Rare 1200-3000, Occasional
600-1500, Frequent 240-600 frames). `StarDef.shoot` (`ShootFreq`: Default/Off/Rare/Occasional/
Frequent, Default = Occasional) is latched by `Star_SetActive`; the menu's **Preset** value
honors it, any other value forces a level.

A meteor is a great-circle arc: a start direction `d0` high in the sky (25-75 degrees
elevation) and a unit tangent `t` biased downward, `head(p) = d0*cos(arc*p) + t*sin(arc*p)` for
`p = age/life`, where `arc` (0.4-1.0 rad) is how far it crosses and `life` (26-46 frames) x the
Speed menu factor sets the pace. It draws as an 8-segment `GX_LINESTRIP` trail spanning 0.15 of
the arc behind the head, per-vertex alpha fading to transparent at the tail, plus a
`GX_TRIANGLEFAN` head glow, both additive with a 4-frame fade-in / 12-frame fade-out envelope.
The pool clears and the timer re-seeds on every preset change and CT teardown.

### Wind-bent trees (`tree.c`)

The global wind can lean the City Trial forest trees. Each forest tree (yakumono `desc_id` 34,
53 instances in CT) renders from its own `JOBJ_SKELETON` joint whose world matrix is rebuilt
from the joint SRT every frame by `HSD_JObjSetupMatrixSub`, so a small tilt written into that
joint's Euler rotation each frame is honored automatically - no user matrix, no dirty flag, no
vertex work. Only the visual model is touched; collision is never moved.

The tree joints are enumerated once per stage. `Tree_Enumerate` walks the stage's placed-instance
pool (`Gr_GetCollRecords`) and keeps records whose `yaku_gobj` owner is one of the tree-family
yakumono GObjs, gathered by walking the `GAMEPLINK_YAKUMONO` GObj list for `desc_id` 34. The
owner slot is matched by pointer only and never dereferenced (it is meaningless for non-break
instances), and each kept joint's authored base rotation is cached so the lean is always
relative to it.

`Tree_Tick` reads the wind vector, derives a lean angle (0.018 rad per world-unit of wind speed,
capped at 0.25 rad / ~14 degrees), and tips every intact trunk toward the downwind heading by
writing its joint `rot.X` / `rot.Z`; a calm wind leaves the trees at their base rotation. A
per-tree sinusoidal gust (0.22 amplitude, 0.09 rad/frame, 0.7 rad phase step between adjacent
trees) breaks the grove out of lockstep so it reads as wind through foliage rather than one
rigid block pivoting. A knocked-down tree is skipped via the `grScene_IsInstanceCollAll(record, 1)`
gate - its collision is retired and the break tail owns the joint from then on.

Trees carry no per-preset config; they are a global menu effect gated on the wind.

### Volcano (`volcano.c`)

The City Trial volcano erupts a set number of times over the round, each eruption throwing
volleys of themed copy-ability projectiles out of the crater on ballistic arcs. The projectiles
are real `GAMEPLINK_PROJECTILE` actors with live hitboxes, so an eruption genuinely threatens
riders, machines and boxes.

**Scheduling against the round.** `Volcano_Tick` reads `grBoxGeneInfo.match_progress` (0 -> 1
over the match, -1 during the intro), so "3 eruptions per game" means exactly 3. `SeedSchedule`
divides progress into `n` equal slices and rolls one start point inside each
(`(i + 0.15 + 0.70*rand) / n`), so eruptions spread out but never land on the same beat twice.
Changing the count mid-round re-plans from the current progress and skips entries already behind
it, so no eruption replays. An eruption holds for `duration` frames and fires `burst`
projectiles every `interval` frames.

**Launch geometry.** The crater mouth is `(-366.19, 114.97, -575.42)`, surveyed at the rim -
left-and-back of map center in the `+/-1300` X/Z play box. Shots start there jittered by 18 in
X/Z, each along a random upward cone ray: azimuth uniform over the circle, tilt rolled in
0.4-1.0 of `VOLC_MAX_TILT` (70 degrees) x the preset's `spread`. Speed is 5.5 x `power` x
`1 +/- 0.3`. Every shot also rolls a size uniformly over 0.5-3.5 into `desc.velocity_scale`
which, despite the name, is a size scale driving the model and the hitbox together - so a big
one is genuinely more dangerous. Size and speed roll independently, so a boulder is no slower
than a pebble.

Ballistic range is roughly `speed^2 / gravity` ~= 1380 units, which from that mouth reaches the
-X and -Z map edges and blankets a wide radius around the volcano without covering the far
corners. Speed and `VOLC_GRAVITY` are tuned as a pair - gravity scales with the square of the
speed, so changing how fast an arc plays out leaves where shots land alone. Flight is around 8
seconds at the steepest tilt, hence the generous `VOLC_LIFETIME` (1680) backstop. `desc.up` is
built as the unit vector perpendicular to the launch ray in the same vertical plane rather than
world up, because `Projectile_Create` crosses forward with up to build the orientation basis and
a near-vertical launch would make those parallel.

**Ownerless projectiles.** Volcano shots end up with `owner_gobj` NULL, which
`HitColl_CheckIfSamePlayer` reads as "never the same player" - so the volcano is excluded from
nobody and threatens everyone, with no damage misattributed. That constrains the usable kinds:
plain sword stars and plasma C/D dereference the owner inside `Projectile_Create` and also home,
and all three auras re-snap to the owner's hand bone every frame. The themes therefore draw from
plasma A/B, the two spread shots, bomb, sensor bomb, the charged sword star, and the Fire
ability's bullet. Bomb and sensor bomb are transitioned to their thrown state in the same call as
the spawn, before their hand-snapping state-0 slot can run.

Fire bullet is the one kind that cannot be created with a null owner - its `init` and
`post_init` read rider fields through it - so `LaunchOne` lends it the first live rider from
`stc_playerdata` for the duration of `Projectile_Create` and clears `proj->owner_gobj`
immediately after; nothing in its per-frame slots reads the owner again. It also seeds the fire
bullet's charge scratch, because the borrowed rider is never holding a charged Fire ability and
its `init` would otherwise cache a zero there - which the kind later turns into a zero-radius
hitbox and a zero-scale model on impact. `proj+0x1b8` (the hitbox-radius multiplier) gets 1.0,
vanilla's full-charge ceiling, and `proj+0x1bc` gets the shot's rolled size since the kind
assigns it to `cur_scale` on impact. Every launch guards on `((void **)0x8055a9a8)[kind] != NULL`,
since that table is empty until the first rider is created and `Projectile_Create` does not check
it.

**Arcs.** Nothing in the projectile pipeline applies gravity, and prio 0 zeroes the accel vector
every frame, so every shot gets `VolcanoGravity` installed on `proj->user_hook_0` - invoked at
the tail of prio 0, right after the zeroing and before prio 4 integrates. It goes on every kind,
not just the plasma and sword-star ones whose pre-physics slot is all `blr`: bomb, firecracker
and sensor bomb only ever *add* the stage air current to accel in their flying state, so a
hook-written value survives on them and all themes arc identically. Lifetime is overwritten on
every shot because the per-kind defaults are unusable here - plasma A/B expire in 6-9 frames, and
bomb and sensor bomb never expire at all. The Fire theme fires the Fire ability's bullet rather
than the firecracker for the same reason: the firecracker carries its own fuse in kind scratch
that bursts it mid-flight no matter what `lifetime` says, while fire bullet's state-0 `fn0` is a
stub and it flies until it hits something.

Authored on **Volcanic** (fire, 4 eruptions) and **Storm** (plasma, 2).

### Tornado (`tornado.c`)

A funnel that wanders City Trial on a random path, drawing loose items, breakable props and
parked machines into an orbit around its core, dragging at riders who stray inside, and shaking
the camera of anyone nearby. Appearances are spread across the round by the match timer, the
same way volcano eruptions are.

**Two entry points, deliberately.** `Tornado_Tick` runs from the weather runtime (inside the
stage think, GObj proc priority 1) and decides *where the funnel is*: schedule, touchdown,
wander, model placement and target claiming. `Tornado_OnFrameEnd` runs from the mod's
`ModDesc.OnFrameEnd` hook and decides *what the funnel does to the world*: the orbit's position
writes, the rider push and the camera shake. The split is forced - item physics
(`CityItem_PhysicsThink`, priority 4), the item ground snap (priority 5),
`Machine_PhysicsThink` (priority 4) and `PlyCam_Think` (priority 13) all run after the
priority-1 weather tick, so a position written there is recomputed before render. `OnFrameEnd`
is the only place those overrides survive.

**Path.** The funnel roams a **disc** centered on the OOB box, its radius the shorter half-extent
x `TORN_PLAY_FRACTION` (0.68) - 884 units on City Trial's `+/-1300` box. Motion is
waypoint-driven: a waypoint is drawn uniformly over the disc's *area* (`r * sqrt(u)`, so the
middle of the city is as likely as the rim), the heading blends toward it by `TORN_STEER` each
frame, a small random rotation jitters the track, and a fresh waypoint is drawn on arrival. The
heading is carried as a **unit direction vector** rotated by `cosf`/`sinf`, never an angle plus
`atan2f` - the freestanding libm here has no `atan2f`. Nothing in the path reads the wind.

Waypoints are what keep the funnel off the rim. A heading walk that only turns when it *would*
leave the box grazes the boundary and then curves straight back out, so the funnel spends the
whole tornado sliding around the perimeter; steering toward interior waypoints makes it cross
the city instead. The radial clamp at the end of the wander only ever catches jitter nudging it
slightly past the rim. Base height follows the ground via `Raycast_Ground` each frame, falling
back to the previous height where the cast finds nothing.

**The funnel model is the Kirby inhale whirlwind (`Effect 0x3a982`), spawned detached.**
`Effect_SpawnSync` is called with a NULL parent and **anchor mode 1**, which takes a single
vararg: a post-spawn callback handed the spawn node. That mode skips the joint-attach path
entirely, so no follow proc is installed and the model root belongs to the mod. The `efgroup`
argument asserts on -1 and is borrowed from a live rider (`RiderData+0x440`) - the group the
inhale itself spawns into.

The model is authored **along its local +Z**: narrow mouth at the origin, flaring to radius 8.07
at `z = 9.95`. Mode 1 applies no orientation, so a raw spawn renders lying flat.
`TornadoPlaceModel` therefore writes the root's **world matrix** (`JObj+0x44`) with
`JOBJ_USER_DEFINED_MTX` set rather than its SRT: the matrix maps local +Z onto world +Y, scales
length to the funnel height and cross-section to `TORN_MODEL_WIDTH` of the capture reach
independently, and rolls the whole thing about its axis in the same sense as the orbit. Writing
the matrix keeps euler order out of it and overrides whatever the effect's own animation does to
the root joint.

Two more consequences of mode 1. The effect's one-shot init never runs, so looping is armed by
hand (`JObj_SetAllAOBJLoopByFlags(root, ALL_ANIM)`) and the tick calls `JObj_AnimAll(root)`
itself, because no proc advances a detached effect's animation. And the effect **must not be
destroyed by hand**: the spawn node still points at the GObj, and the engine's per-node kill
destroys it again when the group is retired - a double free that corrupts the GObj free list and
asserts out of an unrelated `GObj_AddUserData` seconds later. A lifting tornado hides the model
tree (`JObj_SetFlagsAll(root, JOBJ_HIDDEN)`) and the same model is reused for the rest of the
stage, re-validated against the p_link-16 bucket each frame and respawned if a group kill took
it.

**The funnel forms and ropes out** over `TORN_FADE_FRAMES` (120) at each end of its life instead
of popping. The ramp does two things. It **narrows the column** - the matrix's radial scale is
multiplied by the ramp (floored at `TORN_FADE_MIN_WIDTH` so the matrix never goes degenerate)
while the height stays put, so the funnel thins to a thread rather than blinking out. And it
**scales each material's authored opacity**: `MObjLoad` (0x803f9f04) allocates an `HSD_Material`
per MObj and memcpys the desc into it, so writing `MObj.mat->alpha` dims this funnel alone and
never a player's inhale whirlwind sharing the same model.

The width ramp is the load-bearing half. Material alpha reaches the screen only through the
rasterized alpha, and this model's TEV alpha stage is `alpha_a..d = ZERO/ZERO/ZERO/GX_CA_TEXA` -
it takes the texture's alpha and discards `RASA`, so the material write may never be visible.
Both are applied because the opacity write costs nothing when it is inert. Authored alphas are
captured with the rest of the one-time model setup, which is deferred to the first frame the root
JObj is reachable rather than done in the spawn callback - a root not yet attached there would
silently leave both the swirl unanimated and the fade with nothing to scale. The scaled values
are written *after* the frame's `JObj_AnimAll`, the only thing that would put them back. A
tornado shorter than two ramps never reaches full width.

**Orbit.** A claimed target's polar position is re-derived from its live world position every
frame (so anything nudged by another system self-corrects rather than drifting out of the swirl),
rotated about the axis, pulled inward by a fraction of the distance to its ring, and lifted.
Angular speed comes from a constant *tangential* speed (`TORN_TANGENT / r`, capped at
`TORN_SPIN_MAX`) so the outer edge does not whip round faster than the core.

Because that is a pure function of position, every target at the same radius would lap at the
same rate and converge on the same ring - one rigid spiral. So each claim also draws a fixed
**orbit variation**: a multiplier on angular speed (and on its cap, or everything resynchronizes
near the core), the ring radius it settles into as a multiple of the core radius, a multiplier on
climb rate, and a slow radial breathing with a random phase. The draw happens once at claim time
and is stored with the claim; re-rolling per frame would read as jitter rather than as different
debris.

| Target | Mechanism |
|---|---|
| Items | Claimed by `ItemData` pointer (boxes skipped via `item_category == 0`), position overridden, `vel` zeroed, `is_airborne = -1` and the grounded bit cleared so the ground snap stops fighting it. Re-validated against the item bucket each frame, so a collected item self-heals out of the claim set. |
| Yakumono | Claimed per **scene-instance record**, not per GObj (the break families are one GObj to N props). Collision is retired at claim time so nobody runs into an airborne prop, `JOBJ_USER_DEFINED_MTX` is set so the skeleton families stop rebuilding from their authored SRT, and the record's world matrix translation is orbited. After `TORN_YAKU_HOLD` (110) frames the prop is destroyed through its own family `coll_func`. |
| Unridden machines | Claimed by `MachineData` pointer (`rider_gobj == NULL`), position overridden with `accel` and `velocity` zeroed - `Machine_PhysicsThink` integrates both into `pos` every frame. |
| Ridden machines | **Pushed, never possessed.** An inward + tangential + upward acceleration is *added* to `MachineData.velocity` with linear falloff to nothing at the reach. Velocity is a persistent accumulator the movement controllers only read-modify-write and drag down, so an impulse decays naturally and a fast machine can drive or boost its way back out. |

Nothing takes damage - the tornado is purely kinetic.

**The prop break is synthesized**: a zeroed `CollData` with `radius` cranked to `1.0e9` (force
is `radius * impactSpeed^2`, so any prop's HP falls in one hit) and `pos_delta` set to
`-normalize(region normal) * 100`. The delta *must* point into the surface, because
`grScene_GetImpactSpeed` negates the projection onto the outward normal and clamps a
non-positive result to zero. The record's collision is re-armed just before the call so the
family tail's "still collidable?" guard passes, the target region's refine bit is cleared for the
duration so the geometry-refined path cannot rewrite the synthetic delta, and `coll.g` is left
NULL so the break is credited to nobody. On a successful break the weak families (coral, trees,
rocks) get `JOBJ_USER_DEFINED_MTX` cleared, since they never hide their dragged mesh inline and
would otherwise leave a whole tree frozen in mid-air. Weak-family debris still spawns at the
prop's baked ground spot rather than at altitude - that anchor node is per-family and is not
relocated here.

**Camera shake drives the engine's own per-view shake record** rather than patching the camera
solve. `CamData.eye_pos` / `interest_pos` are *not* in the render path (the param that reaches
the COBJ is `CamData.x14`, snapshotted into a stack local inside `PlyCam_Think`), so writing them
does nothing. The record hangs off `PlayerCamData+0x74`, past the end of the declared struct, and
`PlyCam_Think` applies it after building the camera basis, gated on `rec+0x44` being positive:

```
eye += right * rec[+0x28] * rec[+0x20]  +  up * rec[+0x2c] * rec[+0x24]
```

Both scale fields initialise to 1.0, so writing `+0x28`/`+0x2c` in world units and raising
`+0x44` is the whole mechanism - no code patch, and therefore no conflict with `custom_events`,
which already replaces the `HSD_CObjSetEyePosition` call at 0x800b3900. The pointer is
sanity-checked as an aligned MEM1 address before anything is written through it, distance is
measured from the view's own aim point, and the gate is sticky so it is explicitly lowered when
the funnel lifts.

`TornadoDef` is `enabled`, `count` (touchdowns per round), `duration` (frames), `size` (funnel
scale, which also scales core radius, reach and height), `strength` (spin/pull/drag scalar) and
`speed` (wander speed). Authored on the **Tornado** preset with `count = 3`, so a test round
always has one in reach.

### Event sky suppression (`event_sky.c`)

A standalone toggle - not a per-preset layer and not driven from the runtime tick - that stops
City Trial siren events (Meteor, Fog, Dyna Blade, ...) from swapping the sky to their themed
preset and back mid-round, which is jarring when a custom preset is already running.

The engine routes every event sky change through two tiny global wrappers, so the module replaces
both at boot with `CODEPATCH_REPLACEFUNC`: `Sky_TransitionGlobal` (event start ->
`Sky_BeginTransition`) and `Sky_RestoreGlobal` (event end -> `Sky_ApplyStoredIndex`). The
**Event Sky Changes** menu toggle gates them: `On` (default) reimplements each wrapper
faithfully, `Off` early-outs both so the round's weather holds through every event. Suppressing
the sky change neuters the **Dense Fog** event specifically, whose visual is delivered entirely
by its sky preset; every other siren event has its own actors. Installed from `EventSky_OnBoot`,
called from `main.c`'s `OnBoot`. The only other caller of `Sky_TransitionGlobal` is the inert
debug preset-cycler.

### Settings menu

`main.c` registers the mod settings menu ("City Trial Sky") with 14 entries: **Weather Presets**
(`custom_weather.c`), **Backdrops** (`custom_backdrops.c`), then one submenu per effect module -
Rain (with a Hail sub-value from `hail.c`), Snow, Wind, Lightning, Puddles, Trees, Clouds, Moon,
Stars, Volcano, Tornado - and the single **Event Sky Changes** option. Each submenu lives in its
own module's source file and is named `<module>_menu`.

The menus augment the per-preset configs rather than replacing them. Every layer setting defaults
to **Preset** (index 0), the pass-through value: a scaling knob resolves it to 1.0x so the
preset's authored value shows through unchanged, and a categorical knob (Phase, Color, Arc, bolt
mode) honors the preset's own field. The remaining options are explicit overrides applied to
whichever preset the round rolls - a multiplier scales that preset's value, an "Off" floor
disables the layer everywhere, and the "On" / "Force" values on Moon / Stars / Lightning /
Volcano / Tornado force the layer onto every preset. **Hail** and Shooting Stars **Frequency**
have matching per-preset fields, so their **Preset** index resolves to the active preset's
authored value; the global-only knobs with no per-preset field (Wind Slant, Snow Wind Slant,
Affect Machines / Items, Roaming, Show Puddles, Bend in Wind, and the other Shooting Stars knobs)
resolve **Preset** to the module's built-in default behavior instead, with Off / On as hard
overrides.

Two entries are pool selectors rather than layer overrides. **Weather Presets** carries the
**Fog Distance** value (Preset = 1.0x, then 50-200% overrides of the global `HSD_Fog.scale`
multiplier), an Enable-All / Disable-All pair, and one plain Enabled/Disabled toggle per preset,
backing the `weather_enabled[WEATHER_TOTAL]` array that `CustomWeather_OverrideSky` filters its
random pick against. **Backdrops** carries the parallel Backdrop Distance scale plus a
per-backdrop enable set.

All settings persist via hoshi's keyed menu-save.
