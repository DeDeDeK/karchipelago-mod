# Custom City Trial Events

The `custom_events` mod adds mod-defined City Trial events. They run through the vanilla state machine and get everything a vanilla event gets: the siren, sky transition, HUD announcement, secondary music and the same four lifecycle phases. The 16 vanilla events are left undisturbed. Custom events take kind values `>= EVKIND_NUM` (16) and are reachable either through an extended weighted roll or by a direct call through the exported API.

Sources live in `mods/custom_events/src/`: the framework in `custom_events.c`, the mod entry in `main.c`, and one `event_*.c/h` pair per event. The public API is `mods/custom_events/include/custom_events_api.h`.

## Why the Vanilla Tables Cannot Just Be Extended

The engine hardcodes 16 kinds. Anything indexed by `cur_kind` is sized to exactly 16, and several of those live inside larger structs. An out-of-range index therefore does not fault; it silently reads or writes neighbouring data:

- **`stc_event_function[16]`** (0x804a5410, `.data`): overruns into whatever follows, giving garbage function pointers.
- **`occurrence_count[16]`** (`EventCheckData+0x44`): overruns into the `reserve[]` array at `+0x84`.
- **`EventParam[16]`, `weights[STGROUP_NUM][16]` and `bgm_sky[16]`**: all in the `GrCity1Event.dat` archive, all out-of-bounds reads.
- **The per-kind start sound**: state 1 plays it through `CityEvent_PlayStartSound` (0x8027a5d8) from a 16-entry table at 0x804b9190, and index 16 reads a code pointer.
- **The event name SIS id table `stc_event_sis_id_table`** (0x804a7b98): 40 entries (16 event + 24 stadium).
  - It is followed directly by the eye `WObjDesc` of a vanilla ortho `CObjDesc` (0x804a7c60), which `3D_InitIndicatorHUD` and `3D_InitMapDotsCamera` load in every 3D scene.
  - Writing entries past index 39 corrupts that camera until reboot.

`reserve[16]` and `prev_kind[10]` store kind values rather than being indexed by them, so they can hold a custom kind. `CityEvent_Decide` does index per-kind arrays with the `prev_kind` history, though, so a custom kind must never be pushed into it.

The count is also baked into instruction sequences that cannot be widened:

| Site | Address | What it hardcodes |
|------|---------|-------------------|
| `CityEvent_Decide` | 0x800edda4-0x800ede20 | Unrolled copy of exactly 16 weights into the local chance array |
| `CityEvent_Decide` | 0x800edf5c-0x800edff4 | Once-only filter iterating `param + kind*0xC` for 16 kinds |
| State 1 handler (`CityEvent_StateStarting`) | 0x800ee420-0x800ee434 | `occurrence_count[cur_kind]` write - **overflows for kind >= 16** |
| `CityEvent_Init` | 0x800edc30-0x800edc6c | Zeros exactly 16 occurrence counters |
| `CityEvent_ForceStart` | 0x800ee80c | `reserve_kind_num < 16` bound |
| `stadiumPrediction` | 0x80127864 | Reads the HUD SIS id from `stc_event_sis_id_table[arg]` |

So the framework has one hard rule: a custom kind must never reach vanilla per-kind code. It enforces that by replacing the state handlers rather than growing the tables.

## Hook Strategy

`CustomEvents_OnBoot` (the mod's `OnBoot`) does five things:

1. **State handlers.** It saves slots 1, 2 and 3 of the state dispatch table `stc_event_state_table` (0x804a5604) and replaces them with `CustomEvent_State1Wrapper` / `_State2Wrapper` / `_State3Wrapper`. Slot 0 (idle) is left alone, since the idle logic never touches a per-kind array.
2. **The roll.** `CODEPATCH_REPLACECALL(0x800ee098, CustomEvents_ExtendedRoll)` replaces the `bl Gm_Roll` inside `CityEvent_Decide`, where `r3` is the stack chance array and `r4` is 16.
3. **HUD text.** It relocates the SIS id table and pre-composes the announcement text (see HUD Text below).
4. **Scale Change.** It calls `ScaleChange_InstallHooks`, which installs that event's camera shim.
5. **Export.** `Hoshi_ExportMod(&api)` publishes `CustomEventsAPI`.

`main.c` also wires two scene hooks:
- `.On3DLoadEnd` appends the announcements to the SIS array when `Gm_IsInCity()`.
- `.On3DExit` runs `CustomEvents_On3DExit` (see Scene Exit).

Each wrapper branches on `ev_chk->cur_kind < EVKIND_NUM`. A vanilla kind is delegated untouched to the saved original handler. A custom kind is handled entirely in mod code, mirroring what vanilla does for a siren event:

- **State 1 (starting).**
  - Waits out `starting_delay`, sets `state = 2` and resets the timer.
  - Calls `CityEvent_ShowHudText(40 + idx, hud_display_frames)` with the *relocated* table index rather than the raw kind.
  - Starts the event's secondary BGM and calls its `start`.
  - Skips the vanilla `occurrence_count` increment, the `prev_kind[]` history push, the per-kind start sound and the `stc_event_function` dispatch.
- **State 2 (active).**
  - Calls the event's `active` each frame.
  - Once `timer >= duration`, moves to state 3 and does what `CityEvent_EndWithSkyRestore` (0x800ee660) does for a siren event: `Gm_FadeInMusic(cleanup_delay)`, then `Sky_RestoreGlobal()` if the event changed the sky.
- **State 3 (cleanup).**
  - Calls `end` each frame.
  - When `cleanup_delay` expires, calls `end2` and stops the secondary BGM.
  - Rolls a fresh inter-event delay of `delay_min + HSD_Randi(delay_max - delay_min + 1)` and returns to state 0 with `cur_kind = -1`.

Because state 2 owns the duration check, custom events do not end themselves the way vanilla `active` functions do.

### Music

A siren event hands the music between two BGM slots, and all four calls are needed:

| When | Call | Effect |
|------|------|--------|
| At trigger | `Gm_FadeOutMusic` (0x80061df0) | Fades the main slot (1) to volume 0 |
| State 1 -> 2 | `BGM_PlaySecondaryFile` (0x80061e7c) | Streams the event track in slot 2, sets bit 0x10 of the BGM flag byte (0x805380ca), pauses slot 1 |
| State 2 -> 3 | `Gm_FadeInMusic` (0x80062004) | Clears that bit, resumes slot 1, fades it back in over `cleanup_delay`, fades slot 2 out |
| End of state 3 | `BGM_StopSecondary` (0x800620e8) | Only ends slot 2 |

Skipping `Gm_FadeInMusic` leaves the City Trial music paused at volume 0 for the rest of the round. With the flag still set, `City_PlayHurryBGM` (0x80011e90) also starts the hurry-up track paused.

## Scene Exit

**Vanilla does no cleanup for an event still running when the scene ends.** The `EventCheckData` destructor (`CityEvent_Destructor`, 0x800edb68) is a bare `HSD_Free`, so `end2` never runs.
- `CityEvent_StateIdle` stops starting events once fewer than `min_time` (2200) frames of the round remain.
- A running event can still be cut off: Gourmet Race's 60 seconds plus its siren and cleanup phases outlast that window.
- A direct trigger skips the gate entirely.

The framework tracks the running custom event in `running_idx`, set by `CustomEvent_Do` and cleared when state 3 finishes. `CustomEvents_On3DExit` is hooked to hoshi's `On3DExit`, which runs at the tail of `Stadium_ExitMinor` (minor 18's exit callback). It calls the running event's optional `abort` callback.

An `abort` resets only mod state that would otherwise outlive the scene, and must not touch GObjs:
- **Gravity Change** restores the stage gravity strength, since the stage archive can stay preloaded into the next round.
- **Scale Change** drops its camera dolly back to a passthrough, since the shim runs in every later 3D scene.

Waddle Dee Swarm and Gourmet Race need no `abort`: their `start` callbacks reset every static a cut-off event leaves behind.

## HUD Text

Custom announcement text is SIS binary built by the mod and injected into City Trial's SIS pointer array. The vanilla `stadiumPrediction` HUD path then renders it with no per-trigger hook and the normal slide-in animation. Two pieces make that work.

### The SIS entries

At boot, `ComposeSisText` converts each event's `hud_text` into a 128-byte SIS buffer:
- SIS treats bytes below 0x20 as commands (`TEXTCMD_*` in `text.h`) and everything else as 2-byte character codes from `Text_CharToCommand`.
- A literal space is not a character code but `TEXTCMD_SPACE`.
- The body is wrapped in align-left, fit, kerning, gray color and ~0.70 scale, then closed with the matching pops and a terminator.
- The glyph loop stops before it would overflow the buffer.

Every 3D scene reloads `stc_sis_data[0]` (0x8059a85c) with SisCitytrial.dat's 42-entry pointer array, in `3D_LoadHUDFile` before `On3DLoadEnd`. So `CustomEvents_InitSis` runs on each City Trial load:
1. It copies those 42 pointers into `extended_sis_ptrs`.
2. It appends the custom buffers as SIS ids 42 and up.
3. It points slot 0 at the extended array.

The next scene's reload discards the extended array.

### The SIS id table

`stadiumPrediction` stores the `CityEvent_ShowHudText` argument and later resolves it as `(signed char)stc_event_sis_id_table[arg]`. The table cannot grow in place (see above). Instead, `RelocateSisIdTable`:
1. Copies the 40 vanilla ids into a mod-owned `sis_id_table`.
2. Appends the custom ids at index 40 and up.
3. Repoints the three readers.

Each reader loads the table address with a `lis r3` / `addi r3,r3` pair, and both instructions are rewritten with `CODEPATCH_REPLACEINSTRUCTION`:

| Reader | `lis` | `addi` |
|--------|-------|--------|
| `CityEvent_HudPredictionShow` (0x80127624) | 0x80127660 | 0x80127664 |
| `CityEvent_HudPredictionThink` (0x801276c0) | 0x80127794 | 0x8012779c |
| `stadiumPrediction` (0x80127864) | 0x801278cc | 0x801278d4 |

The ids are read back sign-extended from a byte, so the SisCitytrial count plus the custom events must stay below 128. A `_Static_assert` enforces this.

## Registration

Events are registered in one designated-initializer table in `custom_events.c`, `events[]`, indexed by `kind - EVKIND_NUM`. Each `CustomEventDesc` carries:

| Field | Meaning |
|-------|---------|
| `label` | Name used in logs |
| `hud_text` | The announcement |
| `duration` | Frames in state 2 |
| `sky_preset` | -1 = leave the sky alone |
| `bgm_file` | Secondary BGM file index |
| `weight` | Natural roll weight, 0 = never rolled |

`start` and `end2` are required callbacks; `active`, `end` and `abort` are optional. Every custom event is a siren event.

The `CustomEventKind` enum in the API header is contiguous from `EVKIND_NUM`. Each registered event has its own reference doc named after the event.

| Kind | ID | File | Duration | Sky | BGM | Weight | Callbacks |
|------|---:|------|---------:|-----|-----|-------:|-----------|
| `CUSTOM_EVKIND_WADDLE_DEE_SWARM` | 16 | `event_waddle_dee_swarm.c` | 1800 (~30s) | 5 (Dark Vignette) | 0x34 | 20 | start, active, end2 |
| `CUSTOM_EVKIND_GRAVITY_CHANGE` | 17 | `event_gravity_change.c` | 900 (~15s) | 8 (Pink Sky) | 0x31 | 20 | start, end2, abort |
| `CUSTOM_EVKIND_SCALE_CHANGE` | 18 | `event_scale_change.c` | 900 (~15s) | 3 (Dusk 2) | 0x32 | 20 | start, active, end, end2, abort |
| `CUSTOM_EVKIND_GOURMET_RACE` | 19 | `event_gourmet_race.c` | 3600 (~60s) | -1 (no change) | 0x34 | 20 | start, end2 |

All four carry `weight = 20`, so all four take part in the natural roll.

## Triggering

### Direct: `CustomEventsAPI.Do(kind)`

`CustomEvent_Do` returns 0 in three cases:
- the kind is out of range;
- the event system GOBJ does not exist (`CityEvent_Init` leaves `stc_eventcheck_gobj` NULL when City Trial events are off);
- another event is already running (`state != 0`).

On success it:
1. Sets `state = 1`, `cur_kind`, `timer = 0` and `running_idx`.
2. Plays the siren intro that `CityEvent_Decide` plays for a vanilla pick: `Gm_FadeOutMusic(music_fadeout_frames)`, `SFX_PlayFullVolume(EVENT_SIREN_SFX)` (0x130002) and the sky transition.

The secondary BGM is started by the state 1 wrapper, not here.

### Natural: `CustomEvents_ExtendedRoll`

This replaces the `Gm_Roll(chance_arr, 16)` call inside `CityEvent_Decide`. A reserved kind (from `CityEvent_ForceStart`) never reaches that call: the reserve loop at 0x800ee060-0x800ee080 branches past it. So the roll only runs for an ordinary pick.

1. It sums the 16 vanilla weights (already filtered by history and once-only) into `vanilla_total`, and the custom `weight`s into `custom_total`.
2. It returns -1 if both are zero; otherwise it rolls `HSD_Randi(vanilla_total + custom_total)`.
3. A roll inside the vanilla range delegates to the real `Gm_Roll`, so vanilla weighting still applies, and returns its result.
4. Otherwise it walks the custom weights, starts the winner with `CustomEvent_Do`, and returns **-1**.

Returning -1 is what makes this work:
- On -1, `CityEvent_Decide` only sets a new `event_time` and zeroes `timer`. It never writes `state` or `cur_kind`, so the custom event, already in state 1, proceeds under the wrappers.
- `event_time` is only read in state 0, and the state 3 wrapper sets a fresh one.
- `Gm_Roll` (0x800db2b8) itself returns -1 for an all-zero array, because its pick loop skips zero weights and falls through.

## API Consumers

`CustomEventsAPI` (version 2.0, `CUSTOM_EVENTS_API_MAJOR`/`_MINOR`) exports only `Do`. It is published with `Hoshi_ExportMod` and resolved with `Hoshi_ImportMod(CUSTOM_EVENTS_MOD_NAME, ...)`.

The only importer is `archipelago_debug`, which fires Scale Change from a debug pad binding. The `archipelago` mod does **not** import it, so custom events are not AP-gated.

## Secondary BGM File Indices

`BGM_PlaySecondaryFile` (0x80061e7c) accepts `0 < idx < 0x44`. It maps the index through `stc_bgm_desc` (0x80498750, 69 entries x 0x10: `{int bgm_id; int source; char *name; ...}`) to an `audio/jp/<name>.hps` path and hands that to `BGM_PlayFile`. The table is 1:1, so `idx == bgm_id`.

| Range | Tracks |
|-------|--------|
| 0x01 | `stageauto` |
| 0x02 | `menu` |
| 0x03-0x24 | 2D/3D stage tracks (`2d_*`, `3d_*`, each with an `_ura` variant) |
| 0x25 | `stadiumintro` |
| 0x26-0x28 | `city`, `city_isogi` (hurry-up), `city_ura` |
| 0x29-0x2c | `clearchecker`, `dragoon`, `ending`, `ending_city_us` |
| **0x2d-0x36** | **event tracks** - `event_fog`, `event_gordo`, `event_itembound`, `event_kyoseki`, `event_meteo`, `event_monster`, `event_stationfire`, `event_supercharge`, `event_syoukinkubi`, `event_toudai` |
| 0x37-0x3e | `finish_1/2/cp`, `graph`, `howto`, `menu`, `opening`, `retire` |
| 0x3f-0x43 | stadium tracks (`studium_04`, `studium_airgrider`, `studium_battle`, `studium_dedede`, `studium_point`) |

The registered events draw from the event range (0x31 = `event_meteo`, 0x32 = `event_monster`, 0x34 = `event_supercharge`), but any index in the table is valid.

## Item Drop Biasing Is Unavailable to Custom Kinds

Vanilla events bias item drops through `CityEvent_ModifyItemFallDesc(kind)` (0x800ed784), and every step indexes by kind against 16-wide storage:
- The wrapper's special-flag step recognizes only kinds 15 and 7.
- The worker `_CityEvent_ModifyItemFallDesc` (0x800ed5b0) linear-searches an event table by `entry[0] == kind`.
- `CityItemSpawn_SetEventsItemFallChances` (0x800eb568) reads `*(short *)(entry + 4 + kind*2)` inside a `0x28`-byte entry.

A custom kind matches nothing and reads past the chance array into adjacent fields.

To bias drops from a custom event, either pass a vanilla kind to borrow its chance profile, or write `grBoxGeneInfo`/`grBoxGeneObj` fields directly. No registered event does; Gourmet Race spawns its food itself.

## Symbols

The event globals are declared as `static` address casts in `externals/hoshi/include/event.h`, and the text ones in `text.h`.

| Symbol | Address | Notes |
|--------|---------|-------|
| `stc_event_state_table` | 0x804a5604 | State handler dispatch table `[state0..state3]`; slots 1-3 replaced at boot |
| `stc_event_function[16]` | 0x804a5410 | Vanilla per-event function table; never index with a custom kind |
| `stc_event_sis_id_table` | 0x804a7b98 | `int[40]`; its three readers are repointed at a mod copy at boot |
| `stc_eventcheck_gobj` | 0x805dd6f8 | `GOBJ**` for the event system (r13+0x618) |
| `stc_sis_data[0]` | 0x8059a85c | `SISData**`; City Trial's SIS pointer array (42 original entries) |
| `CityEvent_Init` | 0x800edb88 | Creates the event GOBJ, or writes NULL when events are off |
| `CityEvent_Decide` | 0x800edcf8 | Event selection; its `Gm_Roll` call at 0x800ee098 is the extended-roll hook site |
| `CityEvent_StateIdle` | 0x800ee270 | State 0; gates on `min_time` frames remaining and `event_time` |
| `CityEvent_EndWithSkyRestore` | 0x800ee660 | Vanilla state 2 -> 3 for a siren event: `Gm_FadeInMusic` and `Sky_RestoreGlobal` |
| `Gm_Roll` | 0x800db2b8 | Weighted random selection; -1 for an all-zero array |
| `CityEvent_ShowHudText` | 0x80113fb4 | Gates on IsInCity/IsInStadium, calls `stadiumPrediction` |
| `CityEvent_SetSisText` | 0x801169fc | Creates or replaces the HUD text from a SIS entry |
| `stadiumPrediction` | 0x80127864 | Creates/updates the event-name HUD popup |
| `CityEvent_HudPredictionThink` | 0x801276c0 | Per-frame popup proc; resolves the stored index through the id table |
| `CityEvent_HudPredictionShow` | 0x80127624 | Re-shows the popup and re-resolves its text through the id table |
| `stc_bgm_desc` | 0x80498750 | BGM descriptor table (69 x 0x10) |
| `BGM_PlaySecondaryFile` | 0x80061e7c | Streams slot 2, sets BGM flag 0x10, pauses slot 1 |
| `BGM_StopSecondary` | 0x800620e8 | Ends slot 2 only |
| `Gm_FadeOutMusic` | 0x80061df0 | Siren fade-out of slot 1 |
| `Gm_FadeInMusic` | 0x80062004 | Clears flag 0x10, resumes and fades in slot 1, fades out slot 2 |
| `SFX_PlayFullVolume` | 0x8006176c | `EVENT_SIREN_SFX` (0x130002) is the event siren |
| `Sky_TransitionGlobal` | 0x800d5444 | Sky preset transition |
| `Sky_RestoreGlobal` | 0x800d546c | Restore the default sky |
