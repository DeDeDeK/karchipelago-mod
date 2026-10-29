# Sky Presets

City Trial's look - fog, the colour of the void past it, the sky tint, the full-screen tint
overlay and the key light on riders and machines - comes from a table of **sky presets** in the
stage file. One preset is active at a time and the engine lerps toward a new one over its
transition window. City Trial events, the legendary machine assembly and the Destruction Derby
stadiums on the City Trial ground all switch skies this way, and it is the lever a mod pulls to
change the weather. This doc covers the preset data, how a preset is chosen, the per-frame
update, fog, and the fade overlay.

## The pieces

- **Sky preset** - a 0x48-byte `GrFogDesc` in the stage's `GrData.fog`, reached as
  `fog->colors->{desc_arr, num}`. The block's other half, `fog->data->desc`, is the initial
  `HSD_FogDesc`.
- **The sky GObj** at `GrObj+0x168` (`fog_gobj`), built by `Sky_InitFog` (0x800dbfa8). Its
  object is the live `HSD_Fog` and its userdata the `GrFogData` lerp state: target preset,
  transition counter, the lerp start of every field, the current sky colour (+0x18) and the
  stored preset index (+0x1c).
- **The AreaLight** at `GrObj+0x718`, a KAR-specific directional light that shades riders and
  machines. It is not an HSD `LOBJ` and drives no hardware light slot.
- **The fade overlay**, lbfade slot 3, whose slot id the stage keeps at `GrObj+0x714`.

A preset carries `transition_frames` (the lerp length), the fog colour, `fog_near` and `fog_far`,
`map_color` (the overlay target, used only on transitions), `sky_color` (the sky tint), a
0x2c-byte `AreaLightData` at +0x18 (colour, specular colour, direction) and `light_vis_flag` at
+0x44. Colours are packed RGBA8888 with red in the high byte, a `GXColor`'s byte order.

## The vanilla presets

`GrCity1.dat` ships 19. Names for 0-16 are descriptive; 17 and 18 are named for the events that
use them.

| # | Name | Frames | Fog | Start | End | Fade | Sky | Light | Specular | Direction | Vis |
|---|---|---|---|---|---|---|---|---|---|---|---|
| 0 | Day | 120 | 9FCFFFFF | 210 | 665 | 00000000 | 00000000 | D7D7FFFF | FFFFFFFF | -0.4, 0.8, 0.5 | 1 |
| 1 | Midnight | 240 | 1E0005FF | 140 | 560 | 00000080 | 1E0005C8 | D2CDD2FF | A096A0FF | 0, -1, 0 | 0 |
| 2 | Light Fog | 120 | 969696FF | 140 | 1000 | 00000000 | A0A0A0AA | BEBEE6FF | D2D2F0FF | -0.4, 0.8, 0.5 | 1 |
| 3 | Dusk 2 | 120 | C8461EFF | 240 | 900 | 3200006E | 7832198C | E6DCD2FF | B4AAAAFF | 0, 1, 0 | 0 |
| 4 | Dusky Clouds | 120 | C8B4E6FF | 1 | 1000 | 00000000 | C8B4E600 | D2D2E6FF | DCD2FAFF | -0.4, 0.8, 0.5 | 1 |
| 5 | Dark Vignette | 120 | 000000FF | 140 | 500 | 0000003C | 00143CB2 | A0A0AAFF | 9696AAFF | -0.4, 0.8, 0.5 | 0 |
| 6 | Day 2 | 120 | 32A0C8FF | 240 | 1000 | 00000000 | 32A0C800 | DCE6FAFF | C8F0FFFF | -0.4, 0.8, 0.5 | 1 |
| 7 | Blue Sky | 120 | 82AAFFFF | 300 | 1000 | 00000000 | 1450DC80 | DCDCFFFF | FAFAFFFF | -0.4, 0.8, 0.5 | 1 |
| 8 | Pink Sky | 120 | FFA0D9FF | 180 | 900 | 00000000 | FFA0D964 | F0E6F0FF | FFEBF5FF | -0.4, 0.8, 0.5 | 1 |
| 9 | Dense Fog | 120 | E6E6E6FF | 20 | 90 | 00000000 | E6E6E6FF | D2D2F0FF | DCDCFAFF | 0, 1, 0 | 0 |
| 10 | Foggy | 120 | E6E6E6FF | 130 | 800 | 80808050 | E6E6D2C8 | D2D2E6FF | AAAABEFF | -0.4, 0.8, 0.5 | 1 |
| 11 | Dusk | 120 | DC783CFF | 300 | 900 | 785A3C00 | F0965080 | DCC8BEFF | FFAA6EFF | -0.4, 0.3, 0.5 | 1 |
| 12 | Night | 120 | 00143CFF | 140 | 665 | 00001080 | 00143CC6 | B4B4D2FF | AAB4BEFF | 0, 1, 0 | 0 |
| 13 | Gray Sky | 120 | 785A32FF | 500 | 1300 | 00000000 | 785A32AA | E6DCC8FF | FADCB4FF | -0.4, 0.8, 0.5 | 1 |
| 14 | Dark Purple | 120 | 000000FF | 500 | 1300 | 0000005A | 3C0000A0 | DCC8C8FF | F0DCFFFF | -0.4, 0.8, 0.5 | 0 |
| 15 | Red Vignette | 30 | C8461EFF | 100 | 500 | 32000000 | 783219B4 | DCC8BEFF | FAD2AAFF | 0, 1, 0 | 1 |
| 16 | Dark Low Vis | 30 | 000000FF | 90 | 360 | 2800006E | 1E0005C8 | F0DCC8FF | DCA078FF | 0, -1, 0 | 1 |
| 17 | Meteor | 80 | 000000FF | 140 | 480 | 2800006E | 1E0005C8 | C8B4A0FF | FFF0B4FF | 0, 1, 0 | 0 |
| 18 | Lighthouse | 105 | 7878C8FF | 140 | 800 | 00000080 | 0F0A4664 | BEBEE6FF | F0DCFAFF | 0, 1, 0 | 0 |

The AreaLightData flags byte is `0x3b` in every vanilla preset: valid, but with bit 2 (lerp
enable) clear, so `AreaLight_Lerp` (0x800797a8) snaps the light to the target rather than easing
it.

## Choosing a preset

`Sky_Init` (0x8010f114) runs at stage load and switches on `stGetCurrentStageKind` - the
StageKind, not the ground. Any other kind asserts.

| StageKind | Glow joints shown | Preset |
|---|---|---|
| 9, City Trial | both | weighted roll over `{0, 10, 11, 12}` |
| 52, City Trial Free Run | both | 0 |
| 22, Destruction Derby 4 | second | 15 |
| 23, Destruction Derby 5 | first | 16 |

The roll is `Gm_Roll` (0x800db2b8) over four weights pointed to by `GrData+0x08` sub-block
`+0x0c`, indexing the table `{0, 10, 11, 12}` at 0x804a77e4. `Sky_SetPresetIndex` (0x800dc630)
stores the result in `GrFogData.fog_kind` and `Sky_LoadPreset` (0x800dc1b4) applies it at once: no
transition and no overlay. `Gm_Roll` returns -1 when every weight is 0.

`Sky_SetupLights` (0x800db774), called from `Sky_Init`, creates no light despite its name. It sets
render flag bit 4 on one of two stage joints (indices from the same sub-block, `+0x04` and
`+0x08`) and disables collision on that joint's zone. The two joints are map-wide additive glow
overlays laid over the city, and they stay on for the whole round whatever the preset.

The stored index is the round's base sky; everything else transitions away from it and back:

- **City Trial events.** `CityEvent_ForceStart` (0x800ee778) calls `Sky_TransitionGlobal`
  (0x800d5444) at 0x800ee8a4 for an event whose config byte +0x09 (`is_siren`) is set, passing
  `sky_preset` from its `GrCity1Event.dat` row (-1 = no change). `Sky_RestoreGlobal`
  (0x800d546c, through `CityEvent_EndWithSkyRestore`, 0x800ee660) returns to the stored index.
  The event skies are Dyna Blade 1, Pillar 2, Rail Fire 3, Fake Power-ups 4, Tac 5, Same Item 6,
  UFO 7, Bounce 8, Dense Fog 9, Meteor 17 and Lighthouse 18.
- **Legendary machines.** Finishing the assembly cinematic calls `Sky_SetDragoonPreset`
  (0x800d5490) or `Sky_SetHydraPreset` (0x800d54d8), which store 13 or 14 as the new base and
  transition to it at once unless an event is running.

`Sky_TransitionGlobal` and `Sky_RestoreGlobal` are the only routes events take, so replacing the
two is enough to keep events from touching the sky. `Sky_BeginTransition` (0x800dc354) never
writes `GrFogData.fog_kind`: code that watches that field sees the round's base preset, not the event
skies laid over it.

The Dense Fog event (`event_denseFog`, 0x801118dc) does nothing to fog itself. Its visual is
entirely the transition to preset 9; the handler only counts down its duration.

## The per-frame update

`Sky_Update` (0x800dc640) runs from the stage think `Gr_Think` (0x800ce618). It returns early when
there is no target preset or its `transition_frames` is 0. Otherwise it advances the counter
(capped) and, with `ratio = counter / transition_frames`:

1. Lerps the fog colour from `GrFogData+0x08` toward the target's with `GXColor_Lerp`
   (0x80079c04) and writes it to `HSD_Fog.color` and to the global EFB clear colour at
   0x80557484 (`stc_global_fog_color`).
2. Lerps `HSD_Fog.start` and `HSD_Fog.end`.
3. Lerps the sky colour from `GrFogData+0x14` toward `sky_color` into `GrFogData+0x18`.
4. Lerps the AreaLight through `AreaLight_LerpToLive` (0x800ef864).

`Sky_GetCurrentSkyColor` (0x800dc7a4) returns `GrFogData+0x18`, and `Sky_DrawTintQuad`
(0x800d7e78) draws it as a translucent, fogged quad across the view behind the scene; an alpha of
0 draws nothing. That quad is the visible sky tint. The skybox's own material colour is set once
at load and never touched by a preset.

`Sky_Update` writes `HSD_Fog.start`, `end` and `color` every frame and never `type` or `scale`.
Anything a mod wants to hold across frames has to be written after it runs - the end of
`Gr_Think`'s call to it is the natural place - or be one of the fields it leaves alone.

## Fog

Two paths carry the fog colour to the screen, and `Sky_Update` feeds both:

```
HSD_Fog -> Fog_GX (0x800dbf84) -> HSD_FogSetCurrent -> HSD_FogSet -> GXSetFog, per-pixel blend
0x80557484 -> World_CObj (0x800b04a8) -> CObj_SetEraseColor -> the EFB clear on the next copy
```

Changing only `HSD_Fog.color` re-tints fogged geometry but leaves the void past the fog wall at
the old colour; changing only the clear colour does the reverse.

`HSD_FogSet` (0x8041b0fc) reads the current camera's near and far and emits
`GXSetFog(type, start, end * scale, near, far, color)`; a NULL fog emits `GX_FOG_NONE`. `scale`
(`HSD_Fog+0x20`, 1.0 on City Trial) is therefore the cleanest global lever on the fog wall.
City Trial's fog descriptor sets `type` to `GX_FOG_PERSP_LIN` at load and nothing changes it, so a
single write of an exponential type holds for as long as the mod wants it. City Trial has no fog
range adjust table.

Render callbacks that draw fog-free (`Map_GX`, `SimpleShadow_GX`, `ScreenFade_GX` and others)
call `HSD_FogSet(NULL)` and restore with `HSD_FogSet(HSD_FogGetCurrent())`. Bit 0 of the stage's
fog flags (`StageNode+0x1c`, read by `grGetStageFogFlag1`, 0x800d1d48) gates that pair around the
backdrop in `Map_GX` (0x800d81e4). City Trial's bit is 0, so the backdrop draws fog-free and the
re-enable fogs the terrain drawn after it; a 1 would leave the terrain fog-free.

## The fade overlay

`ScreenFade` (lbfade) draws slot-based full-screen tints. `ScreenFade_Alloc` (0x80065a80) builds
the slot's GObj with its GX link equal to the slot number, so the sky's slot 3 draws on GX link 3.
The sky wrappers are `Sky_AllocFade` (0x800eef04), `Sky_BeginFade(grobj, &color, frames)`
(0x800eef50) and `Sky_FreeFade` (0x800eefb0).

- **Only transitions fire it.** `Sky_BeginTransition` calls `Sky_BeginFade` with the target's
  `map_color` and `transition_frames`; `Sky_LoadPreset` does not, so the round's first preset
  never tints.
- **It holds.** `ScreenFade_Begin` (0x800665f8) lerps to the target and stays there; clearing it
  means fading to `0x00000000`.
- **It is the global darkener.** It is a translucent quad over the framebuffer, so it darkens
  terrain, backdrop, sky tint and fog in one step, which lighting cannot do on City Trial's mostly
  unlit geometry. The dark event presets get their darkness from it. Alphas in vanilla sit
  around 60-110; much above that flattens the scene.
- **Draw order limits it.** GX link 3 draws before riders and machines (links 5 and 6) and the HUD,
  so those stay untinted on top.

The stage's GrObj is reused when City Trial is left and re-entered, so a test for a changed GrObj
pointer never fires on the second round. The fade slot id at `GrObj+0x714` is fresh every entry,
because each `ScreenFade_Alloc` increments a global counter, which makes it the signal for per-round
state - including whether `Sky_BeginFade` has been called on this slot yet.

## Key functions

| Function | Address | Role |
|---|---|---|
| `Sky_Init` | 0x8010f114 | pick and load the round's first preset |
| `Sky_SetPresetIndex` | 0x800dc630 | store the base preset index |
| `Sky_LoadPreset` | 0x800dc1b4 | apply the stored preset at once |
| `Sky_BeginTransition` | 0x800dc354 | lerp to a preset and fire the overlay |
| `Sky_ApplyStoredIndex` | 0x800dc4c0 | transition back to the stored preset |
| `Sky_Update` | 0x800dc640 | per-frame lerp |
| `Sky_TransitionGlobal` / `Sky_RestoreGlobal` | 0x800d5444 / 0x800d546c | the event start and end wrappers |
| `Sky_GetPresetCount` | 0x800d5414 | `fog->colors->num` of the live stage |
| `Sky_BeginFade` | 0x800eef50 | drive the lbfade overlay |
| `HSD_FogSet` | 0x8041b0fc | emit the fog to GX |
