# Custom City Trial Events

> **⚠️ WIP — framework wired; only Gourmet Race enabled; mod excluded from the default build.** `mods/custom_events/src/main.c` wires both `.OnBoot` and `.On3DLoadEnd` in its `ModDesc`, so when the mod is built `CustomEvents_OnBoot()` installs the state-handler wrappers + the extended-roll `CODEPATCH_REPLACECALL` and exports `CustomEventsAPI` via `Hoshi_ExportMod`; `CustomEvents_InitSis()` runs on City Trial load. **Only Gourmet Race is enabled** — the other three events carry `weight = 0` (never naturally occur). `archipelago_debug` imports `CustomEventsAPI` and triggers Gourmet Race via `CustomEvent_Do` on a plain D-Pad Up press. The cannon investigation harness is left off (the `CannonEvent_On3DLoadEnd()` call in `On3DLoadEnd` is commented out). **Build note:** `custom_events` and `archipelago_debug` are in the Makefile's default `EXCLUDE_MODS`, so the standard `make deploy` omits them — include them with `make deploy EXCLUDE_MODS=custom_weather`.

## Overview

The custom event system is designed to extend the vanilla City Trial event system to support mod-defined events with custom behavior, triggered via the extended natural-selection roll (and directly via `CustomEvent_Do`). Custom events integrate with the vanilla event state machine (siren, sky transitions, HUD announcements, lifecycle states) without breaking existing events.

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

- Header: `0x12` (ALIGNLEFT), `0x18` (command_18), `0x16` (KERNING), `0x0c bb bb bb` (COLOR, gray for event text), `0x0e 00 b3 00 b3` (SCALE ~0.70)
- Body: each char → `Text_CharToCommand` 2-byte code; a literal space → `0x1a` (SIS space command)
- Trailer: `0x03 0f 0d 17 19 13 00` (`0x13` = end align, `0x00` = TERMINATE)

### Edge Case: First Event in Match

The very first event in a match takes a different code path in `stadiumPrediction` (0x80127950 area) that creates HUD infrastructure from a hardcoded 20-byte data block and does NOT call `CityEvent_SetSisText` (0x801169fc, the SIS text setter). If the first event is custom, the initial text may not display correctly. Subsequent events always use the SIS lookup path and display correctly. This edge case has not been addressed.

## Custom Event Data Structures

### CustomEventParam

```c
typedef struct CustomEventParam {
    int duration;           // frames in state 2 (active phase)
    int is_siren;           // play siren SFX + fade music + sky transition
    int sky_preset;         // sky transition preset (-1 = no change)
    int bgm_file;           // BGM file index to play during event (-1 = no change)
    int weight;             // weight for natural selection (weighted roll)
    const char *label;      // short event name (ASCII, converted to SIS at init)
    const char *hud_text;   // HUD popup text displayed when event starts
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

The exported `api` instance (in `custom_events.c`) is `{ .Do = CustomEvent_Do, .params = custom_params, .event_count = CUSTOM_EVENT_COUNT, .SetWeightFilter = SetWeightFilter }`. `archipelago_debug` imports it (`Hoshi_ImportMod(CUSTOM_EVENTS_MOD_NAME, ...)`) and calls `Do(CUSTOM_EVKIND_GOURMET_RACE)` from its D-Pad Up handler. No consumer installs a weight filter, so default `CustomEventParam.weight` values are used.

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

### AP Item Trigger

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

> **Callers.** `CustomEvent_Do` is invoked from two places: `archipelago_debug`'s D-Pad Up handler (plain Up → `Do(CUSTOM_EVKIND_GOURMET_RACE)`), and `CustomEvents_ExtendedRoll` (the natural-selection path). It is **not** routed from any AP item — there is no `AP_ITEM_EVENT_CUSTOM` in `ap_item_handler.c`; the archipelago mod itself does not import `CustomEventsAPI`.

### Natural Selection

Custom events participate in the vanilla selection cycle. `CustomEvents_ExtendedRoll` is installed via `CODEPATCH_REPLACECALL(0x800ee098, CustomEvents_ExtendedRoll)` in `CustomEvents_OnBoot` (`0x800ee098` is the `bl Gm_Roll` site inside `CityEvent_Decide`, with `r4 = 16` set just before — i.e. `Gm_Roll(chance_arr, 16)`). The replacement sums the 16 vanilla weights and the custom weights, rolls once over the grand total, and either delegates to `Gm_Roll` (vanilla won) or calls `CustomEvent_Do(EVKIND_NUM + i)` and returns `-1` (custom won; vanilla interprets `-1` as "no event, set new delay"). Each custom event's `weight` field from `CustomEventParam` feeds the roll. With the other three events at `weight = 0`, Gourmet Race (`weight = 20`) is the only custom event the roll can select.

### Weight Filter (gating hook)

The API exposes `SetWeightFilter(CustomEventWeightFilter)` (one filter at a time, `NULL` removes it). The filter is `int (*)(int event_index, int default_weight)` and is invoked per custom event inside `CustomEvents_ExtendedRoll` to override that event's weight (return 0 to disable). With no filter installed ("standalone mode"), default `CustomEventParam.weight` values are used. This is the intended integration point for the archipelago mod to gate which custom events can occur, but no consumer currently installs a filter.

## Registered Events

All four kinds below are registered in `custom_params[]` / `custom_functions[]` (`custom_events.c`) and enumerated in `CustomEventKind` (`custom_events_api.h`). They are reachable via `CustomEvents_ExtendedRoll` and `CustomEvent_Do`, but only Gourmet Race is enabled — the other three carry `weight = 0` (see the status banner at top). The enum is contiguous from `EVKIND_NUM` (16); `CUSTOM_EVENT_COUNT` = `CUSTOM_EVKIND_NUM - EVKIND_NUM` = 4.

| Kind | ID | Name | File | Duration | is_siren | sky_preset | bgm_file | weight | Status |
|------|------|------|------|----------|----------|-----------|----------|--------|--------|
| `CUSTOM_EVKIND_WADDLE_DEE_SWARM` | 16 | Waddle Dee Swarm | `event_waddle_dee_swarm.c` | 1800f (~30s) | 1 | 5 (Dark Vignette) | 0x34 | 0 | Implemented (chase AI, fade-out despawn); disabled (weight 0) |
| `CUSTOM_EVKIND_GRAVITY_CHANGE` | 17 | Gravity Change | `event_gravity_change.c` | 900f (~15s) | 1 | 8 (Pink Sky) | 0x31 | 0 | Implemented (air physics weird at low multiplier); disabled (weight 0) |
| `CUSTOM_EVKIND_SCALE_CHANGE` | 18 | Scale Change | `event_scale_change.c` | 900f (~15s) | 1 | 3 (Dusk 2) | 0x32 | 0 | Implemented, visual only (collision doesn't scale); disabled (weight 0) |
| `CUSTOM_EVKIND_GOURMET_RACE` | 19 | Gourmet Race | `event_gourmet_race.c` | 3600f (~60s) | 1 | -1 (no change) | 0x34 | 20 | Implemented (food spawn / scoring / HUD); **enabled** |

See `event-waddle-dee-swarm.md`, `event-gravity-change.md`, `event-scale-change.md`, and `gourmet-race-event.md` for detailed per-event research.

> Note: `cannon_event.c` is **not** a registered custom event — it has no `CustomEventKind` enum value and no `custom_params[]`/`custom_functions[]` entry. It is a separate debug/RE harness (see "Cannon Event (investigation harness)" below).

## Cannon Event (investigation harness)

`cannon_event.c` / `cannon_event.h` is **not a registered custom event** — it has no `CustomEventKind` enum value and no `custom_params[]`/`custom_functions[]` entry, so it can never be selected or triggered through the framework. It is a reverse-engineering harness toward eventually spawning a working cannon yakumono in City Trial, currently gated behind compile-time flags:

- `CANNON_SPAWN_ENABLED` (default 1): from `CannonEvent_On3DLoadEnd`, dumps yakumono param/ydata. In City Trial it hijacks spare `data_array` slot 31 for a zeroed-param "ghost" spawn (`DumpZeroParamCT`); in Machine Passage (grkind 6) it dumps the real cannon param + every vanilla cannon's ydata, then spawns a duplicate (`DumpHealthyMP`).
- `CANNON_LOAD_ENABLED` (default 0): cross-loads `GrMachine2Model.dat` + `GrMachine2.dat` (`CrossLoadCT`) and a render path (`CannonEvent_TryRender`). Disabled because loading the 1.6 MB model archive in CT exhausts heap 1.

`CannonEvent_On3DLoadEnd` is only reachable from the custom_events `On3DLoadEnd`. Although that hook is now wired, the `CannonEvent_On3DLoadEnd()` call inside it is **commented out** — so the dump harness does not run even with the framework active. See `docs/yakumono-system.md` for the cannon-spawn investigation. A standalone doc for the cannon event is **not yet warranted**: it's pure RE scaffolding with no shippable behavior; its findings belong in `yakumono-system.md` until a real cannon event is implemented.

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
| `stadiumPrediction` | 0x80127864 | HUD slide-in animation, reads SIS ID from 0x804a7b98 |
| `Gm_FadeOutMusic` | 0x80061df0 | Music fadeout for siren events (frame count from `data->event->music_fadeout_frames`) |
| `SFX_Play` | 0x800615f0 | `SFX_Play(0x130002)` for event siren |
| `Sky_TransitionGlobal` | 0x800d5444 | Sky preset transition |
| `Sky_RestoreGlobal` | 0x800d546c | Restore default sky |
| `Gm_Roll` | 0x800db2b8 | Weighted random selection |
| `BGM_PlaySecondaryFile` | 0x80061e7c | Secondary BGM stream (pauses main BGM); called on state 1→2 |
| `BGM_StopSecondary` | 0x800620e8 | Stops secondary BGM, resumes main; called on cleanup |
| `Text_CharToCommand` | inline (text.h) | ASCII char → 2-byte SIS character code (not a linked symbol) |

## Open Questions

### Secondary BGM
Custom events **do** use secondary BGM: each `CustomEventParam.bgm_file` (0 = none) is played via `BGM_PlaySecondaryFile` on the state 1→2 transition and stopped via `BGM_StopSecondary` on cleanup. Currently registered events use 0x31 (Meteor), 0x32 (Dyna Blade), and 0x34 (Runamok). Open question: the full set of valid BGM file indices (vanilla events use roughly 0x2D–0x40).

### Item Spawn Modification
Vanilla events call `CityEvent_ModifyItemFallDesc` to change item spawn rates. The `event_spawn` field in `grBoxGeneObj` has per-event-kind chance arrays sized to `EVKIND_NUM` — custom kinds can't index into these. Custom events that want to modify item spawns would need their own mechanism.

### First-Event Edge Case
The very first event in a match takes a different `stadiumPrediction` code path that doesn't call the SIS text setter. If the first event is custom, HUD text may be wrong for that one instance. Not yet addressed.
