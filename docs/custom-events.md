# Custom City Trial Events

The `custom_events` mod extends the vanilla City Trial event system with mod-defined events that run through the same state machine - siren, sky transition, HUD announcement, lifecycle states - without breaking the 16 vanilla events. Custom events take kind values >= `EVKIND_NUM` (16) and are selected through an extended natural-selection roll, or triggered directly via `CustomEvent_Do`.

Implementation: `mods/custom_events/src/custom_events.c` / `custom_events.h`, mod entry in `main.c`, public API in `mods/custom_events/include/custom_events_api.h`. Per-event logic lives in separate `event_*.c/h` files; shared spawn helpers in `spawn_enemy.c` / `spawn_projectile.c`.

## Why the Vanilla Tables Cannot Just Be Extended

The vanilla system hardcodes 16 event kinds. Several structures are indexed by kind and sized to exactly 16:

| Structure | Location | Size | Overflow Risk |
|-----------|----------|------|---------------|
| `EventFunction[16]` | 0x804a5410 (.data) | 16 x 0x14 | Garbage function pointers |
| `occurrence_count[16]` | EventCheckData+0x44 | 16 x 4 | Corrupts `reserve[]` array |
| `EventParam[16]` | Archive data (via `data->event->param`) | 16 x 0xC | OOB read |
| `weights[STGROUP_NUM][16]` | Archive data (via `data->event->weights`) | 8 x 16 x 4 | OOB read |
| `bgm_sky[16]` | Archive data (via `data->bgm_sky`) | 16 x 0x14 | OOB read |
| Event name SIS IDs | 0x804a7b98 (.data) | 40 x 4 (16 event + 24 stadium) | Wrong HUD text |
| `reserve[16]` | EventCheckData+0x84 | 16 x 4 | OK (stores values, not indexed by kind) |
| `prev_kind[10]` | EventCheckData+0x18 | 10 x 4 | OK (stores values, not indexed by kind) |

The 16-count is also baked into instruction sequences:

| Site | Address | What it hardcodes |
|------|---------|-------------------|
| `CityEvent_Decide` | 0x800edda4-0x800ede20 | Unrolled copy of exactly 16 weights into the local chance array |
| `CityEvent_Decide` | 0x800edf5c-0x800edff4 | Once-only filter iterating `param + kind*0xC` for 16 kinds |
| State 1->2 handler | 0x800ee420-0x800ee434 | `occurrence_count[cur_kind]` at `ev_chk+0x44+kind*4` - **overflows for kind >= 16** |
| `CityEvent_Init` | 0x800edc30-0x800edc6c | Zeros exactly 16 occurrence counters |
| `CityEvent_ForceStart` | 0x800ee80c | `reserve_kind_num < 16` bound |
| `stadiumPrediction` | 0x80127864 | Reads the HUD SIS ID from `0x804a7b98[kind]` |

So custom kinds must never reach vanilla per-kind dispatch code. The framework guarantees that by replacing the state handlers.

## Hook Strategy

`CustomEvents_OnBoot` (wired via `main.c`'s `ModDesc.OnBoot`) does four things:

1. Saves the original handlers from the state dispatch table `stc_event_state_table` (0x804a5604) and replaces slots 1, 2 and 3 with `CustomEvent_State1Wrapper` / `_State2Wrapper` / `_State3Wrapper`. Slot 0 (idle) is left alone - the idle logic touches no per-kind array.
2. `CODEPATCH_REPLACECALL(0x800ee098, CustomEvents_ExtendedRoll)` - the `bl Gm_Roll` site inside `CityEvent_Decide` (with `r4 = 16` set the instruction before).
3. Calls `ScaleChange_InstallHooks`, which installs the Scale Change event's camera shim.
4. `Hoshi_ExportMod(&api)` to publish `CustomEventsAPI`.

`main.c`'s `.On3DLoadEnd` runs `CustomEvents_InitSis` when the current stage is City Trial.

Each wrapper checks `ev_chk->cur_kind < EVKIND_NUM`:

- **Vanilla event**: delegate to the saved original handler, unmodified.
- **Custom event**: handle entirely in mod code, touching no vanilla per-kind array.

### State 1 Wrapper (Starting -> Active)

Waits for `starting_delay`, then sets `state = 2`, resets the timer, and:

1. `CityEvent_ShowHudText(CUSTOM_SIS_TABLE_OFFSET + idx, hud_frames)` - passes the remapped SIS table index, not the raw kind.
2. `BGM_PlaySecondaryFile(bgm_file)` if `bgm_file != 0` (secondary stream; pauses main BGM).
3. `custom_functions[idx].start()` if defined.

Bypasses the vanilla `occurrence_count[cur_kind]` increment, the `prev_kind[]` history push, and the `stc_event_function[cur_kind].start` dispatch.

### State 2 Wrapper (Active)

Calls `custom_functions[idx].active()` each frame. When `timer >= custom_params[idx].duration`, transitions to state 3 and calls `Sky_RestoreGlobal()` if `is_siren && sky_preset != -1`.

### State 3 Wrapper (Ending)

Calls `custom_functions[idx].end()` each frame during cleanup. When `cleanup_delay` expires:

1. `custom_functions[idx].end2()` (one-time final cleanup)
2. `BGM_StopSecondary()` if `bgm_file != 0`
3. Rolls a new delay: `delay_min + HSD_Randi(delay_max - delay_min + 1)`
4. Resets `state = 0`, `cur_kind = -1`, `timer = 0`

## HUD Text: SIS Pre-Placement

Custom event text is pre-composed as SIS binary at City Trial load and injected into an extended SIS pointer array, so the vanilla `stadiumPrediction` HUD path displays it with no per-trigger hook.

`CustomEvents_InitSis()`:

1. `stc_sis_data[0]` (0x8059a85c slot 0) points to the City Trial SIS pointer array - 42 original entries (2 data + 16 event text + 24 prediction text).
2. All 42 original pointers are copied into `extended_sis_ptrs[42 + CUSTOM_EVENT_COUNT]`.
3. For each custom event, `ComposeSisText()` converts the C string `hud_text` to SIS binary and stores it in `custom_sis_text[i][128]`.
4. Extended entries are appended: `extended_sis_ptrs[42 + i] = custom_sis_text[i]`.
5. `stc_sis_data[0]` is replaced with the extended array pointer.
6. SIS IDs are written into the event name lookup table at `0x804a7b98[CUSTOM_SIS_TABLE_OFFSET + i]`, where `CUSTOM_SIS_TABLE_OFFSET = EVKIND_NUM + STKIND_NUM = 16 + 24 = 40` - so actual indices are 40, 41, ...

Because both the lookup table and the SIS array are extended, custom event text renders through the vanilla slide-in animation pipeline unchanged.

### SIS Text Format

SIS text uses opcodes < 0x20 for commands and 2-byte character codes >= 0x20 (via `Text_CharToCommand`). `ComposeSisText` emits, in order:

- Header: `0x12` (ALIGN_LEFT), `0x18` (FIT_ON), `0x16` (KERNING_ON), `0x0c bb bb bb` (COLOR, gray), `0x0e 00 b3 00 b3` (SCALE ~0.70)
- Body: each char -> `Text_CharToCommand` 2-byte code; a literal space -> `0x1a` (SIS space command)
- Trailer: `0x03` (LINEBREAK), `0x0f` (SCALE_POP), `0x0d` (COLOR_POP), `0x17` (KERNING_OFF), `0x19` (FIT_OFF), `0x13` (ALIGN_POP), `0x00` (TERMINATE)

## Data Structures

### CustomEventParam

```c
typedef struct CustomEventParam {
    int duration;           // frames in state 2 (active phase)
    int is_siren;           // play siren SFX + fade music + sky transition
    int sky_preset;         // sky transition preset (-1 = no change)
    int bgm_file;           // secondary BGM file index 1..0x43 (0 = none)
    int weight;             // weight for natural selection (0 = never naturally occurs)
    const char *label;      // short event name for menus/notifications (not composed to SIS)
    const char *hud_text;   // HUD popup text; composed to SIS binary at init
} CustomEventParam;
```

### CustomEventFunc

```c
typedef struct CustomEventFunc {
    void (*start)(EventCheckData *ev_chk);   // once on state 1->2 transition
    void (*active)(EventCheckData *ev_chk);  // every frame during state 2
    void (*end)(EventCheckData *ev_chk);     // every frame during state 3
    void (*end2)(EventCheckData *ev_chk);    // once when state 3 cleanup_delay expires
    int (*check)(EventCheckData *ev_chk);    // pre-trigger check (return 0 to block)
} CustomEventFunc;
```

All function pointers are optional (NULL = skip).

### CustomEventsAPI

Defined in `custom_events_api.h`, API version 1.0 (`CUSTOM_EVENTS_API_MAJOR` / `_MINOR`), exported via `Hoshi_ExportMod` and imported with `Hoshi_ImportMod(CUSTOM_EVENTS_MOD_NAME, ...)`.

```c
typedef struct CustomEventsAPI {
    int (*Do)(int kind);                          // trigger by kind; 1 on success, 0 on failure
    const CustomEventParam *params;               // read-only, CUSTOM_EVENT_COUNT entries
    int event_count;                              // == CUSTOM_EVENT_COUNT
    void (*SetWeightFilter)(CustomEventWeightFilter filter); // NULL removes the filter
} CustomEventsAPI;
```

The only consumer is `archipelago_debug`, which imports it to trigger Scale Change from the debug pad bindings. The archipelago mod does **not** import it, so custom events are not AP-gated and no weight filter is installed.

## Event Registration

Events are registered via designated-initializer arrays in `custom_events.c`, both indexed by `kind - EVKIND_NUM`:

```c
CustomEventParam custom_params[CUSTOM_EVENT_COUNT] = {
    [CUSTOM_EVKIND_WADDLE_DEE_SWARM - EVKIND_NUM] = { .duration = 1800, .is_siren = 1, .sky_preset = 5, .bgm_file = 0x34, .weight = 20, .label = "Waddle Dee Swarm", .hud_text = "Waddle Dee swarm incoming!" },
    // ...
};

static CustomEventFunc custom_functions[CUSTOM_EVENT_COUNT] = {
    [CUSTOM_EVKIND_WADDLE_DEE_SWARM - EVKIND_NUM] = { .start = WaddleDeeSwarm_Start, .active = WaddleDeeSwarm_Active, .end2 = WaddleDeeSwarm_End2 },
    // ...
};
```

`custom_params` is non-`static` - it is exported through `CustomEventsAPI.params`. `custom_functions` is file-local.

Adding an event requires: an enum value in `CustomEventKind` (`custom_events_api.h`), a `custom_params[]` entry, a `custom_functions[]` entry, the event header included in `custom_events.c`, and the implementation in its own `event_*.c/h`.

### Registered Events

The `CustomEventKind` enum is contiguous from `EVKIND_NUM` (16); `CUSTOM_EVENT_COUNT` = `CUSTOM_EVKIND_NUM - EVKIND_NUM` = 4. All four carry `weight = 20`, so all four participate in the natural roll. Each has its own reference doc named after the event.

| Kind | ID | Name | File | Duration | is_siren | sky_preset | bgm_file | weight | Callbacks |
|------|------|------|------|----------|----------|-----------|----------|--------|--------|
| `CUSTOM_EVKIND_WADDLE_DEE_SWARM` | 16 | Waddle Dee Swarm | `event_waddle_dee_swarm.c` | 1800f (~30s) | 1 | 5 (Dark Vignette) | 0x34 | 20 | start, active, end2 |
| `CUSTOM_EVKIND_GRAVITY_CHANGE` | 17 | Gravity Change | `event_gravity_change.c` | 900f (~15s) | 1 | 8 (Pink Sky) | 0x31 | 20 | start, active, end2 |
| `CUSTOM_EVKIND_SCALE_CHANGE` | 18 | Scale Change | `event_scale_change.c` | 900f (~15s) | 1 | 3 (Dusk 2) | 0x32 | 20 | start, active, end, end2 |
| `CUSTOM_EVKIND_GOURMET_RACE` | 19 | Gourmet Race | `event_gourmet_race.c` | 3600f (~60s) | 1 | -1 (no change) | 0x34 | 20 | start, active, end2 |

No registered event defines a `check` callback, so none can block its own trigger.

`cannon_event.c` is **not** a registered custom event (no `CustomEventKind` / `custom_params[]` entry) and its `CannonEvent_On3DLoadEnd` call in `main.c` is commented out, because the cannon scaffolding runs diagnostic spawns and memory dumps on every City Trial load. Its findings live in `yakumono-system.md`.

`spawn_enemy.c` / `spawn_projectile.c` hold standalone trap spawners (meteor / bomb / Gordo / sensor bomb) that no current code path calls. `SpawnEnemy_OnBoot` - which installs the null-safety `REPLACEFUNC`s for `EventActor_GetParentScale` (0x802049b8) and `splArcLengthPoint` (0x80415958) that standalone actor spawns depend on - is **not called from any boot path**, so those guards are not installed at runtime.

## Triggering

### Direct Trigger (CustomEvent_Do)

`int CustomEvent_Do(int kind)` returns 1 on success, 0 if:

- Kind is out of range (`< EVKIND_NUM` or `>= CUSTOM_EVKIND_NUM`)
- The event system GOBJ is not initialized (`stc_eventcheck_gobj`)
- Another event is already active (`state != 0`)
- The event's `check()` returns 0

On success it sets `state = 1`, `cur_kind = kind`, `timer = 0`, and for a siren event fades music (`Gm_FadeOutMusic`), plays the siren SFX (`SFX_Play(0x130002)`), and starts the sky transition if `sky_preset != -1`.

The secondary BGM is **not** started here - the State 1 -> 2 wrapper calls `BGM_PlaySecondaryFile`, and the State 3 wrapper calls `BGM_StopSecondary` on cleanup.

### Natural Selection (CustomEvents_ExtendedRoll)

The replacement for the `Gm_Roll(chance_arr, 16)` call in `CityEvent_Decide`:

1. Sums the 16 vanilla weights (already filtered by any gate hook, plus history and once-only) into `vanilla_total`.
2. Sums each custom event's `weight` (through the weight filter if one is installed) into `custom_total`.
3. If the grand total is 0, returns -1.
4. Rolls `HSD_Randi(grand_total)`. If the roll lands in the vanilla range, delegates to the real `Gm_Roll` so vanilla weighting applies and returns its result.
5. Otherwise walks the custom weights, calls `CustomEvent_Do(EVKIND_NUM + i)` for the winner and returns **-1** - which vanilla interprets as "no event selected, set a new delay", leaving the custom event to run under the wrappers. If `CustomEvent_Do` fails, it falls back to `Gm_Roll`.

A `weight = 0` event can never be selected naturally.

### Weight Filter

`SetWeightFilter(CustomEventWeightFilter)` installs one filter at a time (`NULL` removes it). The filter signature is `int (*)(int event_index, int default_weight)`; it is invoked per custom event inside `CustomEvents_ExtendedRoll` to override that event's weight (return 0 to disable). This is the intended integration point for AP-gating custom events. No consumer currently installs one, so the default `CustomEventParam.weight` values are used.

## Secondary BGM File Indices

`BGM_PlaySecondaryFile` (0x80061e7c) accepts an index in `0 < idx < 0x44`, maps it through the descriptor table `stc_bgm_desc` (0x80498750, 69 entries x 0x10: `{int bgm_id; int source; char *name; ...}`) to an `audio/jp/<name>.hps` path, and hands it to `BGM_PlayFile` -> `_BGM_PlayFile` (which re-checks `idx <= stc_bgm_file_count`, the runtime count at r13+0x60). The table is 1:1 - `idx == bgm_id`.

| Range | Tracks |
|-------|--------|
| 0x01 | `stageauto` |
| 0x02 | `menu` |
| 0x03-0x24 | the 2D/3D stage tracks (`2d_*`, `3d_*`, each with an `_ura` variant) |
| 0x25 | `stadiumintro` |
| 0x26-0x28 | `city`, `city_isogi` (hurry-up), `city_ura` |
| 0x29-0x2c | `clearchecker`, `dragoon`, `ending`, `ending_city_us` |
| **0x2d-0x36** | **event tracks** - `event_fog`, `event_gordo`, `event_itembound`, `event_kyoseki`, `event_meteo`, `event_monster`, `event_stationfire`, `event_supercharge`, `event_syoukinkubi`, `event_toudai` |
| 0x37-0x3e | `finish_1/2/cp`, `graph`, `howto`, `menu`, `opening`, `retire` |
| 0x3f-0x43 | stadium tracks (`studium_04`, `studium_airgrider`, `studium_battle`, `studium_dedede`, `studium_point`) |

The registered events draw from the event range (0x31 = `event_meteo`, 0x32 = `event_monster`, 0x34 = `event_supercharge`). Any index in the table is valid.

## Item Drop Biasing Is Unavailable to Custom Kinds

Vanilla events bias item drops through `CityEvent_ModifyItemFallDesc(kind)` (0x800ed784), and every step of that path indexes by kind against 16-wide storage: the wrapper's special-flag step recognizes only kinds 15 and 7, the worker `_CityEvent_ModifyItemFallDesc` (0x800ed5b0) linear-searches an event table by `entry[0] == kind`, and `CityItemSpawn_SetEventsItemFallChances` (0x800eb568) reads `*(short *)(entry + 4 + kind*2)` inside a `0x28`-byte entry. A custom kind matches nothing and reads past the chance array into adjacent fields.

To bias item drops from a custom event, pass a vanilla kind to borrow its chance profile, or write `grBoxGeneInfo`/`grBoxGeneObj` fields directly. None of the registered events use it - Gourmet Race spawns its food directly.

## Symbols

### Custom Event System

Declared in `externals/hoshi/include/event.h` (as `static` casts) and `text.h`.

| Symbol | Address | Notes |
|--------|---------|-------|
| `stc_event_state_table` | 0x804a5604 | State handler dispatch table `[state0, state1, state2, state3]` |
| `stc_event_function[16]` | 0x804a5410 | Vanilla per-event function table (DO NOT index with custom kinds) |
| `stc_event_sis_id_table` | 0x804a7b98 | `int[40+]`; custom event SIS IDs written at indices 40+ |
| `stc_eventcheck_gobj` | 0x805dd6f8 (r13+0x618) | `GOBJ**` for the event system |
| `stc_sis_data[0]` | 0x8059a85c (slot 0) | `SISData**`; City Trial SIS pointer array (42 original entries) |

### Vanilla Functions Used

| Symbol | Address | Notes |
|--------|---------|-------|
| `CityEvent_Decide` | 0x800edcf8 | Event selection; its `Gm_Roll(chance_arr, 16)` call at 0x800ee098 is the extended-roll hook site |
| `Gm_Roll` | 0x800db2b8 | Weighted random selection |
| `CityEvent_ShowHudText` | 0x80113fb4 | Gates on IsInCity/IsInStadium, calls `stadiumPrediction` |
| `CityEvent_SetSisText` | 0x801169fc | Creates/replaces HUD text with a SIS entry |
| `stadiumPrediction` | 0x80127864 | Creates/updates the event-name HUD popup |
| `CityEvent_HudPredictionThink` | 0x801276c0 | Per-frame proc for the deferred (first-event) text path |
| `stc_bgm_desc` | 0x80498750 | BGM descriptor table (69 x 0x10) |
| `BGM_PlaySecondaryFile` | 0x80061e7c | Secondary BGM stream (pauses main BGM); called on state 1->2 |
| `BGM_StopSecondary` | 0x800620e8 | Stops secondary BGM, resumes main; called on cleanup |
| `Gm_FadeOutMusic` | 0x80061df0 | Music fadeout for siren events (map name `City_FadeOutMusic`) |
| `SFX_Play` | 0x800615f0 | `SFX_Play(0x130002)` for the event siren (map name `zz_800615f0_`) |
| `Sky_TransitionGlobal` | 0x800d5444 | Sky preset transition |
| `Sky_RestoreGlobal` | 0x800d546c | Restore default sky |
| `Text_CharToCommand` | inline (`text.h`) | ASCII char -> 2-byte SIS character code (not a linked symbol) |
