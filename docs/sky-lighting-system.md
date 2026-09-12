# City Trial Sky and Lighting

Stage lighting in Kirby Air Ride is driven by a per-stage table of **sky presets** that
interpolate fog, screen tint, sky ambient color, and a directional "area light" over a
transition window. This doc covers the vanilla engine system: preset data, the per-frame
update, fog, HSD lights and shadows.

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

Indices 17+ do not exist in the stage file; `custom_weather` appends its own presets
there at runtime by repointing the preset sub-header.

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
so a mod that repoints that pair extends the selector's range with it.

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
