# Custom City Trial Events

The `custom_events` mod adds mod-defined City Trial events that run through the vanilla state machine - siren, sky transition, HUD announcement, the same four lifecycle phases - without disturbing the 16 vanilla events. Custom events take kind values `>= EVKIND_NUM` (16) and are reachable either through an extended weighted roll or by a direct `CustomEvent_Do` call.

Sources live in `mods/custom_events/src/`: the framework in `custom_events.c`, the mod entry in `main.c`, one `event_*.c/h` pair per event, and shared spawn helpers in `spawn_enemy.c` / `spawn_projectile.c`. The public API is `mods/custom_events/include/custom_events_api.h`.

## Why the Vanilla Tables Cannot Just Be Extended

The engine hardcodes 16 kinds. Anything indexed by `cur_kind` is sized to exactly 16, and several of those live inside larger structs, so an out-of-range index does not fault - it silently reads or writes neighbouring data:

- `stc_event_function[16]` (0x804a5410, `.data`) - overruns into whatever follows, giving garbage function pointers.
- `occurrence_count[16]` (`EventCheckData+0x44`) - overruns into the `reserve[]` array at `+0x84`.
- `EventParam[16]`, `weights[STGROUP_NUM][16]` and `bgm_sky[16]` - all in the `GrCity1Event.dat` archive, all out-of-bounds reads.
- The event name SIS id table (0x804a7b98) - 40 entries (16 event + 24 stadium); a kind past 15 lands in the stadium block and shows the wrong HUD text.

`reserve[16]` and `prev_kind[10]` are safe: they *store* kind values rather than being indexed by them.

The count is also baked into instruction sequences that cannot be widened:

| Site | Address | What it hardcodes |
|------|---------|-------------------|
| `CityEvent_Decide` | 0x800edda4-0x800ede20 | Unrolled copy of exactly 16 weights into the local chance array |
| `CityEvent_Decide` | 0x800edf5c-0x800edff4 | Once-only filter iterating `param + kind*0xC` for 16 kinds |
| State 1 -> 2 handler | 0x800ee420-0x800ee434 | `occurrence_count[cur_kind]` write - **overflows for kind >= 16** |
| `CityEvent_Init` | 0x800edc30-0x800edc6c | Zeros exactly 16 occurrence counters |
| `CityEvent_ForceStart` | 0x800ee80c | `reserve_kind_num < 16` bound |
| `stadiumPrediction` | 0x80127864 | Reads the HUD SIS id from `0x804a7b98[kind]` |

So the framework's one hard rule is that a custom kind must never reach vanilla per-kind dispatch code. It guarantees that by replacing the state handlers, not by trying to grow the tables.

## Hook Strategy

`CustomEvents_OnBoot` (wired through `main.c`'s `ModDesc.OnBoot`):

1. Saves slots 1, 2 and 3 of the state dispatch table `stc_event_state_table` (0x804a5604) and replaces them with `CustomEvent_State1Wrapper` / `_State2Wrapper` / `_State3Wrapper`. Slot 0 (idle) is left alone - the idle logic never touches a per-kind array.
2. `CODEPATCH_REPLACECALL(0x800ee098, CustomEvents_ExtendedRoll)` - the `bl Gm_Roll` site inside `CityEvent_Decide`, where the instruction before sets `r4 = 16`.
3. Calls `ScaleChange_InstallHooks`, which installs the Scale Change event's camera shim.
4. `Hoshi_ExportMod(&api)` to publish `CustomEventsAPI`.

`main.c`'s `.On3DLoadEnd` calls `CustomEvents_InitSis` when the loaded stage is City Trial.

Each wrapper branches on `ev_chk->cur_kind < EVKIND_NUM`: a vanilla kind is delegated to the saved original handler untouched; a custom kind is handled entirely in mod code.

- **State 1 (Starting -> Active)** waits out `starting_delay`, sets `state = 2`, resets the timer, calls `CityEvent_ShowHudText(CUSTOM_SIS_TABLE_OFFSET + idx, hud_frames)` with the *remapped* table index rather than the raw kind, starts the secondary BGM if `bgm_file != 0`, and calls the event's `start`. It deliberately skips the vanilla `occurrence_count` increment, the `prev_kind[]` history push, and the `stc_event_function` dispatch.
- **State 2 (Active)** calls the event's `active` each frame and, once `timer >= duration`, moves to state 3 and calls `Sky_RestoreGlobal()` for a siren event with a real sky preset.
- **State 3 (Cleanup)** calls `end` each frame; when `cleanup_delay` expires it calls `end2`, stops the secondary BGM, rolls a fresh `delay_min + HSD_Randi(delay_max - delay_min + 1)` inter-event delay, and returns to state 0 with `cur_kind = -1`.

Because state 2 owns the duration check itself, custom events do not need to end themselves the way vanilla `active` functions do.

## HUD Text: SIS Pre-Placement

Custom event text is pre-composed as SIS binary at City Trial load and injected into an extended SIS pointer array, so the vanilla `stadiumPrediction` HUD path renders it with no per-trigger hook and the normal slide-in animation.

`CustomEvents_InitSis()` copies the 42 entries of City Trial's SIS pointer array (`stc_sis_data[0]`, 0x8059a85c slot 0 - 2 data + 16 event text + 24 prediction text) into a static `extended_sis_ptrs[42 + CUSTOM_EVENT_COUNT]`, appends one pointer per custom event to a locally composed 128-byte SIS buffer, and points `stc_sis_data[0]` at the extended array. It then writes the new SIS ids into the event name lookup table at `stc_event_sis_id_table[CUSTOM_SIS_TABLE_OFFSET + i]`, where `CUSTOM_SIS_TABLE_OFFSET = EVKIND_NUM + STKIND_NUM = 40` - past the stadium block, so nothing vanilla is clobbered.

`ComposeSisText` does the C-string -> SIS conversion. SIS treats bytes below 0x20 as commands and everything else as 2-byte character codes produced by `Text_CharToCommand` (an inline in `text.h`, not a linked symbol). A literal space is not a character code but the `0x1a` space command. The composer wraps the body in a fixed prologue (align-left, fit-on, kerning-on, gray color, ~0.70 scale) and the matching pops plus a terminator.

## Registration

Two designated-initializer arrays in `custom_events.c`, both indexed by `kind - EVKIND_NUM`: `custom_params[]` (the `CustomEventParam` tuning record) and `custom_functions[]` (the `CustomEventFunc` callback set). `custom_params` is non-`static` because it is exported through the API; `custom_functions` is file-local.

`CustomEventParam` (in `custom_events_api.h`) carries `duration`, `is_siren`, `sky_preset` (-1 = leave the sky alone), `bgm_file` (0 = no secondary music), `weight` (0 = never selected naturally), a short `label` for logs and menus, and `hud_text` for the popup. `CustomEventFunc` holds `start` / `active` / `end` / `end2` / `check`, all optional.

The `CustomEventKind` enum is contiguous from `EVKIND_NUM`, and `CUSTOM_EVENT_COUNT` derives from it. Each registered event has its own reference doc named after the event.

| Kind | ID | File | Duration | Sky | BGM | Weight | Callbacks |
|------|---:|------|---------:|-----|-----|-------:|-----------|
| `CUSTOM_EVKIND_WADDLE_DEE_SWARM` | 16 | `event_waddle_dee_swarm.c` | 1800 (~30s) | 5 (Dark Vignette) | 0x34 | 20 | start, active, end2 |
| `CUSTOM_EVKIND_GRAVITY_CHANGE` | 17 | `event_gravity_change.c` | 900 (~15s) | 8 (Pink Sky) | 0x31 | 20 | start, active, end2 |
| `CUSTOM_EVKIND_SCALE_CHANGE` | 18 | `event_scale_change.c` | 900 (~15s) | 3 (Dusk 2) | 0x32 | 20 | start, active, end, end2 |
| `CUSTOM_EVKIND_GOURMET_RACE` | 19 | `event_gourmet_race.c` | 3600 (~60s) | -1 (no change) | 0x34 | 20 | start, active, end2 |

All four are `is_siren = 1` and carry `weight = 20`, so all four participate in the natural roll. None defines a `check`, so none can block its own trigger.

Two source files in the mod are not registered events:

- `cannon_event.c` has no `CustomEventKind` or `custom_params[]` entry, and its `CannonEvent_On3DLoadEnd` call in `main.c` is commented out, because the cannon scaffolding runs diagnostic spawns and memory dumps on every City Trial load.
- `spawn_enemy.c` / `spawn_projectile.c` are standalone trap spawners (meteor, bomb, Gordo, sensor bomb) that nothing currently calls. Their guards are live, though: `SpawnEnemy_OnBoot` runs from the mod's `OnBoot` and installs the null-safety `REPLACEFUNC`s that standalone actor spawns depend on - `EventActor_GetParentAnimRate` (0x802049b8) faults on a NULL `parent_gobj`, `splArcLengthPoint` (0x80415958) on a NULL spline. Both replacements are faithful to vanilla for non-NULL inputs, but `splArcLengthPoint` has 116 call sites across stage splines, CPU AI routing and the HSD object update funcs, so the guard is engine-wide rather than scoped to custom spawns.

## Triggering

### Direct: `CustomEvent_Do(kind)`

Returns 1 on success, 0 if the kind is out of range, the event system GOBJ does not exist (`stc_eventcheck_gobj` NULL - which is the case whenever City Trial events are disabled), another event is already running (`state != 0`), or the event's `check` returns 0.

On success it sets `state = 1`, `cur_kind`, `timer = 0`, and for a siren event fades the music (`Gm_FadeOutMusic`), plays the siren SFX (`SFX_Play(0x130002)`) and starts the sky transition. The secondary BGM is deliberately **not** started here - the state 1 -> 2 wrapper owns `BGM_PlaySecondaryFile`, and the state 3 wrapper owns the matching `BGM_StopSecondary`.

### Natural: `CustomEvents_ExtendedRoll`

Replaces the `Gm_Roll(chance_arr, 16)` call inside `CityEvent_Decide`:

1. Sums the 16 vanilla weights (already filtered by history, once-only and any gate hook) into `vanilla_total`.
2. Sums each custom event's `weight`, passed through the weight filter if one is installed, into `custom_total`.
3. Rolls `HSD_Randi(vanilla_total + custom_total)`, or returns -1 if the grand total is zero.
4. A roll inside the vanilla range delegates to the real `Gm_Roll` so vanilla weighting still applies, and returns its result.
5. Otherwise it walks the custom weights, calls `CustomEvent_Do` on the winner, and returns **-1** - which vanilla reads as "nothing selected, set a new delay". That is the trick: `CityEvent_Decide` walks away while the custom event, already in state 1, proceeds under the wrappers. If `CustomEvent_Do` fails it falls back to `Gm_Roll`.

### Weight filter

`SetWeightFilter` installs a single `int (*)(int event_index, int default_weight)` filter (NULL removes it), consulted per custom event inside the extended roll; returning 0 disables that event. This is the intended integration point for AP-gating custom events. No consumer installs one today, so the `CustomEventParam.weight` defaults apply.

## API Consumers

`CustomEventsAPI` (version 1.0, `CUSTOM_EVENTS_API_MAJOR`/`_MINOR`) exports `Do`, a read-only pointer to `custom_params`, `event_count`, and `SetWeightFilter`. It is published with `Hoshi_ExportMod` and resolved with `Hoshi_ImportMod(CUSTOM_EVENTS_MOD_NAME, ...)`.

The only importer is `archipelago_debug`, which uses it to fire Scale Change from a debug pad binding. The `archipelago` mod does **not** import it, so custom events are not AP-gated.

## Secondary BGM File Indices

`BGM_PlaySecondaryFile` (0x80061e7c) accepts `0 < idx < 0x44`, maps it through `stc_bgm_desc` (0x80498750, 69 entries x 0x10: `{int bgm_id; int source; char *name; ...}`) to an `audio/jp/<name>.hps` path, and hands it to `BGM_PlayFile`. The table is 1:1 - `idx == bgm_id`.

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

Vanilla events bias item drops through `CityEvent_ModifyItemFallDesc(kind)` (0x800ed784), and every step indexes by kind against 16-wide storage: the wrapper's special-flag step recognizes only kinds 15 and 7, the worker `_CityEvent_ModifyItemFallDesc` (0x800ed5b0) linear-searches an event table by `entry[0] == kind`, and `CityItemSpawn_SetEventsItemFallChances` (0x800eb568) reads `*(short *)(entry + 4 + kind*2)` inside a `0x28`-byte entry. A custom kind matches nothing and reads past the chance array into adjacent fields.

To bias drops from a custom event, either pass a vanilla kind to borrow its chance profile or write `grBoxGeneInfo`/`grBoxGeneObj` fields directly. No registered event does - Gourmet Race spawns its food itself.

## Symbols

The event globals are declared as `static` address casts in `externals/hoshi/include/event.h`; the text ones in `text.h`.

| Symbol | Address | Notes |
|--------|---------|-------|
| `stc_event_state_table` | 0x804a5604 | State handler dispatch table `[state0..state3]`; slots 1-3 replaced at boot |
| `stc_event_function[16]` | 0x804a5410 | Vanilla per-event function table - never index with a custom kind |
| `stc_event_sis_id_table` | 0x804a7b98 | `int[40+]`; custom SIS ids written at indices 40+ |
| `stc_eventcheck_gobj` | 0x805dd6f8 | `GOBJ**` for the event system (r13+0x618) |
| `stc_sis_data[0]` | 0x8059a85c | `SISData**`; City Trial's SIS pointer array (42 original entries) |
| `CityEvent_Decide` | 0x800edcf8 | Event selection; its `Gm_Roll` call at 0x800ee098 is the extended-roll hook site |
| `Gm_Roll` | 0x800db2b8 | Weighted random selection |
| `CityEvent_ShowHudText` | 0x80113fb4 | Gates on IsInCity/IsInStadium, calls `stadiumPrediction` |
| `CityEvent_SetSisText` | 0x801169fc | Creates or replaces the HUD text from a SIS entry |
| `stadiumPrediction` | 0x80127864 | Creates/updates the event-name HUD popup |
| `CityEvent_HudPredictionThink` | 0x801276c0 | Per-frame proc for the deferred first-event text path |
| `stc_bgm_desc` | 0x80498750 | BGM descriptor table (69 x 0x10) |
| `BGM_PlaySecondaryFile` | 0x80061e7c | Secondary BGM stream; pauses the main BGM |
| `BGM_StopSecondary` | 0x800620e8 | Stops the secondary stream and resumes the main BGM |
| `Gm_FadeOutMusic` | 0x80061df0 | Siren-event music fadeout (map name `City_FadeOutMusic`) |
| `SFX_Play` | 0x800615f0 | `SFX_Play(0x130002)` is the event siren (map name `zz_800615f0_`) |
| `Sky_TransitionGlobal` | 0x800d5444 | Sky preset transition |
| `Sky_RestoreGlobal` | 0x800d546c | Restore the default sky |
