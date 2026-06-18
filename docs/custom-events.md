# Custom City Trial Events

## Overview

The custom event system extends the vanilla City Trial event system with mod-defined events that run through the same state machine (siren, sky transitions, HUD announcements, lifecycle states) without breaking the existing events. Custom events are selected through the extended natural-selection roll and can also be triggered directly via `CustomEvent_Do`.

The framework is wired through `main.c`'s `ModDesc`: `.OnBoot` (`CustomEvents_OnBoot`) installs the state-handler wrappers, the extended-roll patch, and exports `CustomEventsAPI`; `.On3DLoadEnd` runs `CustomEvents_InitSis` on City Trial load. An event occurs naturally only with a nonzero selection `weight` — all four registered events currently carry weight 20.

Implementation: `mods/custom_events/src/custom_events.c` / `custom_events.h`. Mod entry/registration: `main.c`. Public API: `mods/custom_events/include/custom_events_api.h`. Per-event logic in separate `event_*.c/h` files. The shared spawn helpers live in `spawn_enemy.c` / `spawn_projectile.c`.

## Vanilla Event System

### State Machine

The event system is driven by `EventCheckData`, stored as userdata on a GOBJ at `stc_eventcheck_gobj` (r13+0x618). It runs a 4-state machine:

| State | Name | Description |
|-------|------|-------------|
| 0 | Idle | No event active. Timer counts up toward `event_time` delay. |
| 1 | Starting | Siren playing, music fading, sky transitioning. Waits for `starting_delay` frames. |
| 2 | Active | Event running. Duration controlled by `param[kind].duration`. Per-event start/active callbacks fire. |
| 3 | Ending | Cleanup phase. Waits for `cleanup_delay` frames. Per-event end/end2 callbacks fire. Resets to state 0. |

State handlers are function pointers in a dispatch table at `0x804a5604`:
```
[state0_handler, state1_handler, state2_handler, state3_handler]
```

### EventCheckData Layout

The canonical definitions live in `externals/hoshi/include/event.h`, where `EventCheckData.data` is a pointer to a separate `EventConfigData` typedef (root of `GrCity1Event.dat`). The flattened version below inlines that config for readability:

```c
typedef struct EventCheckData {
    struct {
        struct {
            int delay_min;              // 0x00
            int delay_max;              // 0x04
            int occur_chance;           // 0x08
            int skip_chance;            // 0x0C
            u8 x10[4];                  // 0x10, unknown
            int min_time;               // 0x14, min match time before events start
            int prev_kind_max;          // 0x18, max history entries
            int music_fadeout_frames;   // 0x1C
            int starting_delay;         // 0x20, frames in state 1
            int cleanup_delay;          // 0x24, frames in state 3
            int hud_display_frames;     // 0x28
            struct { int arr[STGROUP_NUM][EVKIND_NUM]; } *weights;  // 0x2C
            struct {                    // 0x30, per-event params (0xC per kind)
                int category;           //   0x00, diversity boost category
                int duration;           //   0x04, event duration frames
                u8 once_only;           //   0x08
                u8 is_siren;            //   0x09
                u8 xa, xb;
            } *param;
        } *event;
        struct {                        // per-event BGM/sky data (0x14 per kind)
            int bgm_file;              //   0x00
            int sky_preset;            //   0x04
            int location_idx;          //   0x08
            int location_count;        //   0x0C
            void *event_data;          //   0x10
        } *bgm_sky;
    } *data;
    int state;                          // 0x04
    EventKind cur_kind;                 // 0x08
    int xc;                             // 0x0C
    int timer;                          // 0x10, counts up each frame
    int event_time;                     // 0x14, delay until next event check
    int prev_kind[10];                  // 0x18, event history ring
    int prev_kind_num;                  // 0x40
    u8 x44[0x40];                       // 0x44, occurrence_count[16] lives here
    EventKind reserve[16];              // 0x84, queued events
    int reserve_kind_num;               // 0xC4
} EventCheckData;
```

### Fixed-Size Vanilla Arrays (Why We Can't Just Extend)

The vanilla system has 16 event kinds (0–15) with data structures hardcoded to that count:

| Structure | Location | Size | Overflow Risk |
|-----------|----------|------|---------------|
| `EventFunction[16]` | 0x804a5410 (.data) | 16 × 0x14 | Garbage function pointers |
| `occurrence_count[16]` | EventCheckData+0x44 | 16 × 4 | Corrupts `reserve[]` array |
| `EventParam[16]` | Archive data (via `data->event->param`) | 16 × 0xC | OOB read |
| `weights[STGROUP_NUM][16]` | Archive data (via `data->event->weights`) | 8 × 16 × 4 | OOB read |
| `bgm_sky[16]` | Archive data (via `data->bgm_sky`) | 16 × 0x14 | OOB read |
| Event name SIS IDs | 0x804a7b98 (.data) | 16 × 4 | Wrong HUD text |
| `reserve[16]` | EventCheckData+0x84 | 16 × 4 | OK (stores values, not indexed by kind) |
| `prev_kind[10]` | EventCheckData+0x18 | 10 × 4 | OK (stores values, not indexed by kind) |

### Hardcoded Assembly Checks

- `CityEvent_Decide`: copies exactly 16 weights to local array (unrolled memcpy at 0x800edda4–0x800ede20)
- `CityEvent_Decide`: once-only filter iterates `param + kind*0xC` for 16 kinds (0x800edf5c–0x800edff4)
- State 1→2 handler: increments `occurrence_count[cur_kind]` at ev_chk+0x44+kind*4 (0x800ee420–0x800ee434) — **overflows for kind >= 16**
- `stadiumPrediction` (0x80127864): reads SIS ID from `0x804a7b98[kind]` for HUD text
- `CityEvent_Init`: zeros exactly 16 occurrence_count entries (0x800edc30–0x800edc6c)
- `eventInit` (0x800ee778): checks `reserve_kind_num < 16` (0x800ee80c)

## Custom Event Architecture

### Hook Strategy: State Handler Replacement

Custom events use kind values >= `EVKIND_NUM` (16+) but **never pass through vanilla dispatch code**. The state handler function pointers in the dispatch table at `0x804a5604` are replaced with wrappers at boot time:

```
CustomEvents_OnBoot():   // wired via main.c ModDesc.OnBoot
    state_table = stc_event_state_table   // 0x804a5604
    Save original state handlers: orig_state1, orig_state2, orig_state3
    Replace state_table[1] = CustomEvent_State1Wrapper
    Replace state_table[2] = CustomEvent_State2Wrapper
    Replace state_table[3] = CustomEvent_State3Wrapper
    (State 0 is not hooked — idle logic is compatible)
    CODEPATCH_REPLACECALL(0x800ee098, CustomEvents_ExtendedRoll)  // hook the Gm_Roll call in CityEvent_Decide
    Hoshi_ExportMod(&api)                                        // export CustomEventsAPI
```

Each wrapper checks `ev_chk->cur_kind < EVKIND_NUM`:
- **Vanilla event**: delegate to original handler (no modification)
- **Custom event**: handle entirely in mod code, bypassing all vanilla array accesses

### State 1 Wrapper (Starting → Active)

For custom events, waits for `starting_delay` then:
1. Sets `state = 2`, resets timer
2. Calls `CityEvent_ShowHudText(CUSTOM_SIS_TABLE_OFFSET + idx, hud_frames)` — the vanilla HUD pipeline displays text using our pre-registered SIS entries
3. Calls `BGM_PlaySecondaryFile(bgm_file)` if `bgm_file != 0` (secondary BGM stream; pauses main BGM)
4. Calls `custom_functions[idx].start()` if defined

Bypasses vanilla code that would: increment `occurrence_count[cur_kind]` (overflow), push to `prev_kind[]` history, dispatch to `stc_event_function[cur_kind].start` (overflow).

### State 2 Wrapper (Active)

Calls `custom_functions[idx].active()` each frame. When timer reaches `custom_params[idx].duration`, transitions to state 3 and calls `Sky_RestoreGlobal()` if `is_siren && sky_preset != -1`.

Bypasses vanilla dispatch to `stc_event_function[cur_kind].active` (overflow).

### State 3 Wrapper (Ending)

Calls `custom_functions[idx].end()` each frame during cleanup. When `cleanup_delay` expires:
1. Calls `custom_functions[idx].end2()` (one-time final cleanup)
2. Calls `BGM_StopSecondary()` if `bgm_file != 0` (stops secondary stream, resumes main BGM)
3. Rolls new random delay: `delay_min + HSD_Randi(delay_max - delay_min + 1)`
4. Resets: `state = 0`, `cur_kind = -1`, `timer = 0`

Bypasses vanilla dispatch to `stc_event_function[cur_kind].end/end2` (overflow).

## HUD Text System

### Approach: SIS Pre-Placement

Custom event text is pre-composed as SIS binary text at init time and injected into an extended SIS pointer array. The vanilla `stadiumPrediction` HUD code automatically displays the correct text with no per-trigger hook needed.

### How It Works

`CustomEvents_InitSis()` is called from `On3DLoadEnd` when in City Trial:

1. `stc_sis_data[0]` points to the SIS pointer array for City Trial (42 original entries: 2 data + 16 event text + 24 prediction text)
2. All 42 original pointers are copied into `extended_sis_ptrs[42 + CUSTOM_EVENT_COUNT]`
3. For each custom event, `ComposeSisText()` converts the C string `hud_text` to SIS binary format and stores it in `custom_sis_text[i][128]`
4. Extended entries are appended: `extended_sis_ptrs[42 + i] = custom_sis_text[i]`
5. `stc_sis_data[0]` is replaced with the extended array pointer
6. SIS IDs are written into the event name lookup table at `0x804a7b98[CUSTOM_SIS_TABLE_OFFSET + i]` where `CUSTOM_SIS_TABLE_OFFSET = EVKIND_NUM + STKIND_NUM = 16 + 24 = 40`, so actual indices are 40, 41, etc.

The vanilla code path in `stadiumPrediction` reads `0x804a7b98[event_kind]` → gets the SIS index → looks up the text in `stc_sis_data[0]` → displays it. Since we've extended both the lookup table and the SIS array, custom event text renders through the vanilla slide-in animation pipeline.

### SIS Text Format

SIS text uses opcodes < 0x20 for commands and 2-byte character codes >= 0x20 (via `Text_CharToCommand`). `ComposeSisText` (in `custom_events.c`) emits, in order:

- Header: `0x12` (ALIGN_LEFT), `0x18` (FIT_ON), `0x16` (KERNING_ON), `0x0c bb bb bb` (COLOR, gray for event text), `0x0e 00 b3 00 b3` (SCALE ~0.70)
- Body: each char → `Text_CharToCommand` 2-byte code; a literal space → `0x1a` (SIS space command)
- Trailer: `0x03` (LINEBREAK), `0x0f` (SCALE_POP), `0x0d` (COLOR_POP), `0x17` (KERNING_OFF), `0x19` (FIT_OFF), `0x13` (ALIGN_POP), `0x00` (TERMINATE)

### HUD display paths (first vs. subsequent events)

`stadiumPrediction` (0x80127864) drives the popup through a cached HUD GObj pointer in the D-data global (`+0xbe8`), with two paths:

- **First event of the match** (cached GObj == 0): creates the popup GObj, stores the kind → SIS-table index at HUD-data `+0x18`, and installs the per-frame proc `CityEvent_HudPredictionThink` (0x801276c0), which calls `CityEvent_SetSisText(stc_event_sis_id_table[data+0x18])` once the slide-in animation finishes.
- **Subsequent events** (GObj cached): reuse the GObj and call `CityEvent_SetSisText(stc_event_sis_id_table[data+0x18])` directly.

Both paths resolve the text through `stc_event_sis_id_table` (`0x804a7b98[kind]`) — the same table `CustomEvents_InitSis` extends — so custom event text displays through whichever path runs. The first path additionally special-cases `kind == 10`/PREDICTION, remapping it to a stadium-name SIS id; this does not affect custom kinds (≥ 40).

## Custom Event Data Structures

### CustomEventParam

```c
typedef struct CustomEventParam {
    int duration;           // frames in state 2 (active phase)
    int is_siren;           // play siren SFX + fade music + sky transition
    int sky_preset;         // sky transition preset (-1 = no change)
    int bgm_file;           // secondary BGM file index 1..0x43 (0 = none)
    int weight;             // weight for natural selection (weighted roll; 0 = never naturally occurs)
    const char *label;      // short event name for menus/notifications (not composed to SIS)
    const char *hud_text;   // HUD popup text; composed to SIS binary by ComposeSisText at init
} CustomEventParam;
```

### CustomEventFunc

```c
typedef struct CustomEventFunc {
    void (*start)(EventCheckData *ev_chk);   // called once on state 1→2 transition
    void (*active)(EventCheckData *ev_chk);  // called every frame during state 2
    void (*end)(EventCheckData *ev_chk);     // called every frame during state 3
    void (*end2)(EventCheckData *ev_chk);    // called once when state 3 cleanup_delay expires
    int (*check)(EventCheckData *ev_chk);    // pre-trigger check (return 0 to block)
} CustomEventFunc;
```

All function pointers are optional (NULL = skip).

### CustomEventsAPI (public, exported via `Hoshi_ExportMod`)

Defined in `mods/custom_events/include/custom_events_api.h`. API version `1.0` (`CUSTOM_EVENTS_API_MAJOR`/`_MINOR`).

```c
typedef struct CustomEventsAPI {
    int (*Do)(int kind);                          // trigger by kind; 1 on success, 0 on failure
    const CustomEventParam *params;               // read-only, CUSTOM_EVENT_COUNT entries
    int event_count;                              // == CUSTOM_EVENT_COUNT (4)
    void (*SetWeightFilter)(CustomEventWeightFilter filter); // install/remove gating filter (NULL = remove)
} CustomEventsAPI;
```

The exported `api` instance (in `custom_events.c`) is `{ .Do = CustomEvent_Do, .params = custom_params, .event_count = CUSTOM_EVENT_COUNT, .SetWeightFilter = SetWeightFilter }`. It is imported via `Hoshi_ImportMod(CUSTOM_EVENTS_MOD_NAME, ...)`. No consumer installs a weight filter, so default `CustomEventParam.weight` values are used.

### Event Registration

Events are registered via designated initializer arrays in `custom_events.c`:

```c
CustomEventParam custom_params[CUSTOM_EVENT_COUNT] = {
    [CUSTOM_EVKIND_WADDLE_DEE_SWARM - EVKIND_NUM] = { .duration = 1800, .is_siren = 1, .sky_preset = 5,  .bgm_file = 0x34, .weight = 20, .label = "Waddle Dee Swarm", .hud_text = "Waddle Dee swarm incoming!" },
    [CUSTOM_EVKIND_GRAVITY_CHANGE   - EVKIND_NUM] = { .duration = 900,  .is_siren = 1, .sky_preset = 8,  .bgm_file = 0x31, .weight = 20, .label = "Gravity Change",   .hud_text = "Gravity is changing!" },
    // ... SCALE_CHANGE, GOURMET_RACE ...
};

static CustomEventFunc custom_functions[CUSTOM_EVENT_COUNT] = {
    [CUSTOM_EVKIND_WADDLE_DEE_SWARM - EVKIND_NUM] = { .start = WaddleDeeSwarm_Start, .active = WaddleDeeSwarm_Active, .end2 = WaddleDeeSwarm_End2 },
    // ...
};
```

(`custom_params` is non-`static` — it is exported through `CustomEventsAPI.params`; `custom_functions` is file-local.)

New events require:
1. Add enum value in `CustomEventKind` (custom_events.h)
2. Add param entry in `custom_params[]` (custom_events.c)
3. Add function entry in `custom_functions[]` (custom_events.c)
4. Include the event header in custom_events.c
5. Implement the event in its own `event_*.c/h` files

## Event Triggering

### Direct Trigger (`CustomEvent_Do`)

Custom events are triggered via `CustomEvent_Do(kind)`:

```c
int CustomEvent_Do(int kind)
```

Returns 1 on success, 0 if:
- Kind is out of range
- Event system GOBJ not initialized (`stc_eventcheck_gobj`)
- Another event is already active (`state != 0`)
- Custom `check()` function returns 0

On success:
1. Sets `state = 1`, `cur_kind = kind`, `timer = 0`
2. If `is_siren`: fades music (`Gm_FadeOutMusic`), plays siren SFX (`SFX_Play(0x130002)`), starts sky transition if `sky_preset != -1`

The secondary BGM is started later, in the State 1 → 2 wrapper, not here: when transitioning to the active phase, the wrapper calls `BGM_PlaySecondaryFile(bgm_file)` if `bgm_file != 0`, and the State 3 wrapper calls `BGM_StopSecondary()` on cleanup.

> **Callers.** `CustomEvent_Do` is reached from the natural-selection path (`CustomEvents_ExtendedRoll`) and through the exported `CustomEventsAPI.Do`. It is **not** routed from any AP item — the archipelago mod does not import `CustomEventsAPI`, so custom events are not yet AP-gated.

### Natural Selection

Custom events participate in the vanilla selection cycle. `CustomEvents_ExtendedRoll` is installed via `CODEPATCH_REPLACECALL(0x800ee098, CustomEvents_ExtendedRoll)` in `CustomEvents_OnBoot` (`0x800ee098` is the `bl Gm_Roll` site inside `CityEvent_Decide`, with `r4 = 16` set just before — i.e. `Gm_Roll(chance_arr, 16)`). The replacement sums the 16 vanilla weights and the custom weights, rolls once over the grand total, and either delegates to `Gm_Roll` (vanilla won) or calls `CustomEvent_Do(EVKIND_NUM + i)` and returns `-1` (custom won; vanilla interprets `-1` as "no event, set new delay"). Each custom event's `weight` field from `CustomEventParam` (optionally overridden by the weight filter) feeds the roll; a `weight = 0` event can never be selected.

### Weight Filter (gating hook)

The API exposes `SetWeightFilter(CustomEventWeightFilter)` (one filter at a time, `NULL` removes it). The filter is `int (*)(int event_index, int default_weight)` and is invoked per custom event inside `CustomEvents_ExtendedRoll` to override that event's weight (return 0 to disable). With no filter installed ("standalone mode"), default `CustomEventParam.weight` values are used. This is the intended integration point for the archipelago mod to gate which custom events can occur, but no consumer currently installs a filter.

## Registered Events

All four kinds below are registered in `custom_params[]` / `custom_functions[]` (`custom_events.c`) and enumerated in `CustomEventKind` (`custom_events_api.h`). They are reachable via `CustomEvents_ExtendedRoll` and `CustomEvent_Do`; an event occurs naturally only with a nonzero `weight`. All four currently carry equal weight (20). The enum is contiguous from `EVKIND_NUM` (16); `CUSTOM_EVENT_COUNT` = `CUSTOM_EVKIND_NUM - EVKIND_NUM` = 4.

| Kind | ID | Name | File | Duration | is_siren | sky_preset | bgm_file | weight | Notes |
|------|------|------|------|----------|----------|-----------|----------|--------|--------|
| `CUSTOM_EVKIND_WADDLE_DEE_SWARM` | 16 | Waddle Dee Swarm | `event_waddle_dee_swarm.c` | 1800f (~30s) | 1 | 5 (Dark Vignette) | 0x34 | 20 | Chase AI, fade-out despawn |
| `CUSTOM_EVKIND_GRAVITY_CHANGE` | 17 | Gravity Change | `event_gravity_change.c` | 900f (~15s) | 1 | 8 (Pink Sky) | 0x31 | 20 | Scales `stage_node->gravity_strength` — random 0.5x or 2x |
| `CUSTOM_EVKIND_SCALE_CHANGE` | 18 | Scale Change | `event_scale_change.c` | 900f (~15s) | 1 | 3 (Dusk 2) | 0x32 | 20 | Shrinks every player (model + machine collision sphere) + slows movement; world untouched |
| `CUSTOM_EVKIND_GOURMET_RACE` | 19 | Gourmet Race | `event_gourmet_race.c` | 3600f (~60s) | 1 | -1 (no change) | 0x34 | 20 | Food spawn / scoring / HUD |

See `event-waddle-dee-swarm.md`, `event-gravity-change.md`, `event-scale-change.md`, and `gourmet-race-event.md` for per-event detail.

> `cannon_event.c` is **not** a registered custom event (no `CustomEventKind` / `custom_params[]` entry) and is not wired into the framework's `On3DLoadEnd`. It is a reverse-engineering harness toward spawning a cannon yakumono in City Trial; its findings live in `docs/yakumono-system.md`.

## Symbols

### Custom Event System

All declared in `externals/hoshi/include/event.h` (as `static` casts) / `text.h`.

| Symbol | Address | Notes |
|--------|---------|-------|
| `stc_event_state_table` | 0x804a5604 | State handler dispatch table `[state0, state1, state2, state3]` (event.h) |
| `stc_event_function[16]` | 0x804a5410 | Vanilla per-event function table (DO NOT index with custom kinds) |
| `stc_event_sis_id_table` | 0x804a7b98 | `int[40+]`; custom event SIS IDs written at indices 40+ (`CUSTOM_SIS_TABLE_OFFSET + i`) |
| `stc_eventcheck_gobj` | r13+0x618 | GOBJ** for the event system |
| `stc_sis_data[0]` | 0x8059a85c (slot 0) | `SISData**`; City Trial SIS pointer array (42 original entries) |

### Vanilla Event Functions

| Symbol | Address | Notes |
|--------|---------|-------|
| `CityEvent_Decide` | 0x800edcf8 | Event selection, weight copy, `Gm_Roll`. The `Gm_Roll(chance_arr, 16)` call hooked by `CustomEvents_ExtendedRoll` is at 0x800ee098 |
| `CityEvent_ShowHudText` | 0x80113fb4 | Gates on IsInCity/IsInStadium, calls stadiumPrediction |
| `CityEvent_SetSisText` | 0x801169fc | Creates/replaces HUD text with SIS entry |
| `stadiumPrediction` | 0x80127864 | Creates/updates the event-name HUD popup, reads SIS ID from 0x804a7b98 |
| `CityEvent_HudPredictionThink` | 0x801276c0 | Per-frame proc the first-event path installs; calls `CityEvent_SetSisText` once the slide-in finishes (deferred text path) |
| `stc_bgm_desc` | 0x80498750 | BGM descriptor table (69 × 0x10: `bgm_id`, `source`, `char *name`); idx → `audio/jp/<name>.hps` |
| `Gm_FadeOutMusic` | 0x80061df0 | Music fadeout for siren events (frame count from `data->event->music_fadeout_frames`) |
| `SFX_Play` | 0x800615f0 | `SFX_Play(0x130002)` for event siren |
| `Sky_TransitionGlobal` | 0x800d5444 | Sky preset transition |
| `Sky_RestoreGlobal` | 0x800d546c | Restore default sky |
| `Gm_Roll` | 0x800db2b8 | Weighted random selection |
| `BGM_PlaySecondaryFile` | 0x80061e7c | Secondary BGM stream (pauses main BGM); called on state 1→2 |
| `BGM_StopSecondary` | 0x800620e8 | Stops secondary BGM, resumes main; called on cleanup |
| `Text_CharToCommand` | inline (text.h) | ASCII char → 2-byte SIS character code (not a linked symbol) |

## Secondary BGM File Indices

Each `CustomEventParam.bgm_file` (0 = none) is played via `BGM_PlaySecondaryFile` on the state 1→2 transition and stopped via `BGM_StopSecondary` on cleanup. `BGM_PlaySecondaryFile` (0x80061e7c) accepts an index `1 ≤ idx < 0x44` (the hard gate is `0 < idx && idx < 0x44`), maps it through the descriptor table `stc_bgm_desc` (0x80498750, 69 entries × 0x10: `{int bgm_id; int source; char *name; ...}`) to an `audio/jp/<name>.hps` path, and hands it to `BGM_PlayFile` → `_BGM_PlayFile` (which re-checks `idx ≤ stc_bgm_file_count`, the runtime count at r13+0x60). The table is 1:1 — `idx == bgm_id`. Full set:

| Range | Tracks |
|-------|--------|
| 0x01 | `stageauto` |
| 0x02 | `menu` |
| 0x03–0x24 | the 2D/3D stage tracks (`2d_*`, `3d_*`, each with an `_ura` variant) |
| 0x25 | `stadiumintro` |
| 0x26–0x28 | `city`, `city_isogi` (hurry-up), `city_ura` |
| 0x29–0x2c | `clearchecker`, `dragoon`, `ending`, `ending_city_us` |
| **0x2d–0x36** | **event tracks** — `event_fog`, `event_gordo`, `event_itembound`, `event_kyoseki`, `event_meteo`, `event_monster`, `event_stationfire`, `event_supercharge`, `event_syoukinkubi`, `event_toudai` |
| 0x37–0x3e | `finish_1/2/cp`, `graph`, `howto`, `menu`, `opening`, `retire` |
| 0x3f–0x43 | stadium tracks (`studium_04`, `studium_airgrider`, `studium_battle`, `studium_dedede`, `studium_point`) |

The event range 0x2D–0x36 is what the registered custom events draw from (0x31 = `event_meteo`, 0x32 = `event_monster`, 0x34 = `event_supercharge`). Any index in the table is valid; pick one whose mood fits.

## Item Spawn Modification (vanilla-kind only)

Vanilla events bias item drops through `CityEvent_ModifyItemFallDesc(kind)` (0x800ed784), and the path is keyed entirely on the vanilla event kind:

- the public wrapper's special-flag step (`grBoxGeneInfo+0x38`) only recognizes kind 15 (fake-powerups → bit 2) and kind 7 (same-item → bit 4);
- the worker `_CityEvent_ModifyItemFallDesc` (0x800ed5b0) linear-searches an event table by `entry[0] == kind`, so a custom kind matches nothing and falls back to default chances;
- `CityItemSpawn_SetEventsItemFallChances` (0x800eb568) reads each item's per-event chance at `*(short *)(entry + 4 + kind*2)`, an array sized to the 16 vanilla kinds inside each `0x28`-byte entry — a custom kind indexes past it into adjacent struct fields.

So a custom event cannot drive this path with its own kind; it yields meaningless chances. To bias item drops, pass a vanilla kind to borrow its chance profile, or write `grBoxGeneInfo`/`grBoxGeneObj` fields directly. None of the registered events use it — Gourmet Race spawns its food directly.

## Open Questions

Residual unknowns, all minor and non-blocking:

- `EventCheckData+0xC` (`xc`) — read but purpose unidentified.
- Inner-config `+0x10` (`x10[4]`) — present in the archive but not read by the state machine.
- The map symbol names for the state handlers are shifted by one relative to the state they handle: table slot 1 (the "Starting" state) is named `CityEvent_StateActive`, slot 2 (the "Active" state) is named `CityEvent_StateEnding`. The custom wrappers index by slot number so they are unaffected; the names are a vanilla-RE cosmetic snag (already annotated in the city-trial event doc).
