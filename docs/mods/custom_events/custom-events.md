# Custom City Trial Events

The `custom_events` mod adds mod-defined City Trial events. They run through the game's own event
state machine and get everything a vanilla event gets - the siren, the sky transition, the HUD
announcement, the event music and the same lifecycle phases - while the 16 vanilla events run
untouched. A custom event takes a kind value from `EVKIND_NUM` (16) up and is started either by the
game's own random pick or directly through the mod's exported API. This covers the framework the
events share, not the events themselves.

## Why the Vanilla Tables Cannot Just Grow

The game hardcodes 16 event kinds. Everything indexed by the current kind is sized to exactly 16,
often inside a larger struct, so an out-of-range kind does not fault - it silently reads or writes
the neighbouring data:

- `stc_event_function[16]` (0x804a5410) yields garbage function pointers past the end.
- `occurrence_count[16]` (`EventCheckData+0x44`) overruns into the `reserve[]` queue at +0x84.
- The 16-entry per-event parameters (`event->param`), the per-stadium-group weights and
  `bgm_sky[16]` in `GrCity1Event.dat` are read out of bounds.
- `CityEvent_PlayStartSound` (0x8027a5d8) reads a 16-entry table at 0x804b9190; index 16 is a code
  pointer.
- `stc_event_sis_id_table` (0x804a7b98, 40 entries: 16 events, 24 stadium names) is followed
  directly by the eye `WObjDesc` of a vanilla ortho camera descriptor that every 3D scene loads, so
  writing past index 39 corrupts that camera until reboot.

The count is also unrolled into code: the weight copy (0x800edda4-0x800ede20) and once-only filter
(0x800edf5c-0x800edff4) in `CityEvent_Decide`, the `occurrence_count` write in state 1
(0x800ee420-0x800ee434), the counter reset in `CityEvent_Init`, and the reserve bound in
`CityEvent_ForceStart`. `reserve[]` and `prev_kind[]` store kinds rather than being indexed by
them, but `CityEvent_Decide` indexes per-kind arrays through the `prev_kind` history, so a custom
kind must never enter it.

The framework's one rule follows: **a custom kind never reaches vanilla per-kind code.** It
replaces the state handlers rather than growing the tables.

## How It Works

At boot the mod's `OnBoot` saves slots 1-3 of the state dispatch table `stc_event_state_table`
(0x804a5604) and writes three wrappers in their place; slot 0, the idle state, never touches a
per-kind array and stays vanilla. Each wrapper hands a kind below `EVKIND_NUM` to the saved
original, and runs a custom kind entirely in mod code, mirroring what vanilla does for a siren
event:

- **State 1 (starting).** After `starting_delay` frames: state 2, timer reset, the HUD announcement
  through `CityEvent_ShowHudText`, the event's secondary BGM, then its `start` callback. It skips
  the occurrence count, the history push, the start sound and the function-table dispatch.
- **State 2 (active).** Calls `active` each frame. When the timer reaches the event's `duration`
  it moves to state 3 and does what `CityEvent_EndWithSkyRestore` (0x800ee660) does for a siren
  event: `Gm_FadeInMusic(cleanup_delay)`, and `Sky_RestoreGlobal` if the event changed the sky.
  The framework owns the duration, so a custom event does not end itself the way a vanilla
  `active` does.
- **State 3 (cleanup).** Calls `end` each frame; after `cleanup_delay` calls `end2`, stops the
  secondary BGM, rolls a fresh delay between `delay_min` and `delay_max`, and returns to state 0.

**Starting an event.** The API's `Do(kind)` returns 0 when the kind is not a custom one, when the
scene is not a City Trial round with events on, when an event is already running, or when the
event's `can_start` says it cannot run this round. The round's event GObj comes from the mod's own
copy, taken at `On3DLoadEnd` of a City Trial round and dropped at `On3DExit`, not from
`stc_eventcheck_gobj`: `CityEvent_Init` leaves that NULL with events off, but nothing clears it when
the round ends, so it points into a freed heap until the next stage load. Otherwise `Do` sets
state 1, the kind and the timer, and plays the siren intro `CityEvent_Decide` plays for a vanilla
pick: `Gm_FadeOutMusic(music_fadeout_frames)`, the siren (`SFX_PlayFullVolume(EVENT_SIREN_SFX)`)
and the sky transition.

**The natural roll.** `CODEPATCH_REPLACECALL` swaps the `bl Gm_Roll(chance_arr, 16)` inside
`CityEvent_Decide` (at 0x800ee098) for an extended roll. It sums the 16 vanilla weights - already
filtered by history and once-only - and the custom weights, counting an event whose `can_start`
fails as weight 0, and rolls over both. A roll in the vanilla range calls the real `Gm_Roll`, so
vanilla weighting is unchanged; one in the custom range picks the event with a `Gm_Roll` over the
custom weights, starts it as `Do` does and returns -1. On -1 `CityEvent_Decide` only sets a new
`event_time` and zeroes the timer - the same path it takes when `Gm_Roll` finds every weight zero -
and never writes the state or kind, so the custom event, already in state 1, proceeds under the
wrappers. A kind waiting in the reserve queue is retried before this call and never reaches it.

## Music

A siren event hands the music between two BGM slots, and a custom event needs all four steps:

| When | Call | Effect |
|---|---|---|
| Trigger | `Gm_FadeOutMusic` (0x80061df0) | Fades the main slot to volume 0 |
| State 1 -> 2 | `BGM_PlaySecondaryFile` (0x80061e7c) | Streams the event track in slot 2, sets bit 0x10 of the BGM flag byte, pauses slot 1 |
| State 2 -> 3 | `Gm_FadeInMusic` (0x80062004) | Clears the bit, resumes slot 1 and fades it in, fades slot 2 out |
| End of state 3 | `BGM_StopSecondary` (0x800620e8) | Ends slot 2 |

Skipping `Gm_FadeInMusic` leaves the City Trial music paused at volume 0 for the rest of the round,
and with the flag still set `City_PlayHurryBGM` (0x80061bfc) starts the hurry-up track paused too.

`BGM_PlaySecondaryFile` accepts an index from 1 to 0x43 and maps it through `stc_bgm_desc`
(0x80498750) to an `audio/jp/<name>.hps` stream; the table is 1:1, so the index is the BGM id:

| Range | Tracks |
|---|---|
| 0x01-0x02 | `stageauto`, `menu` |
| 0x03-0x24 | 2D and 3D course tracks, each with an `_ura` variant |
| 0x25-0x28 | `stadiumintro`, `city`, `city_isogi` (hurry-up), `city_ura` |
| 0x29-0x2c | `clearchecker`, `dragoon`, `ending`, `ending_city_us` |
| 0x2d-0x36 | Event tracks: `event_fog`, `event_gordo`, `event_itembound`, `event_kyoseki`, `event_meteo`, `event_monster`, `event_stationfire`, `event_supercharge`, `event_syoukinkubi`, `event_toudai` |
| 0x37-0x3e | `finish_1`, `finish_2`, `finish_cp`, `graph`, `howto`, `menu`, `opening`, `retire` |
| 0x3f-0x43 | Stadium tracks: `studium_04`, `studium_airgrider`, `studium_battle`, `studium_dedede`, `studium_point` |

## Scene Exit

Vanilla does no cleanup for an event still running when the round ends: the `EventCheckData`
destructor is a bare `HSD_Free`, so `end2` never runs. Idle stops starting events once fewer than
`min_time` (2200) frames remain, but a long event can still be cut off, and a direct trigger skips
that gate.

The framework tracks the running custom event from its trigger until state 3 finishes, and its
`On3DExit` - which runs at the end of minor 18's exit callback, before the scene heaps are reset -
calls the running event's optional `abort`. An `abort` resets only mod state that would outlive the
scene and must not touch GObjs, which are about to be reclaimed. An event whose `start` resets
every static a cut-off run leaves behind needs no `abort`.

## HUD Announcement

Custom announcement text is SIS data built by the mod and injected into City Trial's SIS pointer
array, so the vanilla popup renders it with its normal slide-in and no per-trigger hook.

- **The text.** At boot each event's `hud_text` is composed into a 128-byte SIS buffer: the body is
  wrapped in align-left, fit, kerning, gray color and ~0.70 scale, closed with the matching pops and
  a terminator, and written with `Text_WriteSisString`, which stops before overflowing the buffer.
  SIS treats bytes below 0x20 as commands and everything else as 2-byte character codes; a space is
  a command, not a character.
- **The array.** Every 3D scene reloads `stc_sis_data[0]` (0x8059a85c) with `SisCitytrial.dat`'s
  42 entries before `On3DLoadEnd`, so on every City Trial load with events on the mod copies those
  pointers into its own larger array with `Text_ExtendSis`, points slot 0 at it, and appends the
  custom buffers as SIS ids 42 and up. The next scene's reload discards the extension.
- **The id table.** `stadiumPrediction` resolves the argument of `CityEvent_ShowHudText` through
  `stc_event_sis_id_table`, which cannot grow in place. The mod copies its 40 ids into its own table,
  appends the custom ids at index 40 and up, and rewrites the `lis`/`addi` pair in each of the three
  readers - `CityEvent_HudPredictionShow` (0x80127624), `CityEvent_HudPredictionThink` (0x801276c0)
  and `stadiumPrediction` (0x80127864) - to point at the copy. The ids are read back as signed
  bytes, so 42 plus the custom event count must not exceed 128.

## Registering an Event

Events are entries of one designated-initializer table, indexed by `kind - EVKIND_NUM`, and the
public `CustomEventKind` enum continues contiguously from `EVKIND_NUM`. Every custom event is a
siren event. A `CustomEventDesc` holds:

| Field | Meaning |
|---|---|
| `label` | Name used in the log |
| `hud_text` | The announcement |
| `duration` | Frames in state 2 |
| `sky_preset` | Sky preset for the transition; -1 leaves the sky alone |
| `bgm_file` | Secondary BGM index |
| `weight` | Natural roll weight; 0 never rolls |
| `can_start` | Optional: whether the event can run this round; a failing event neither rolls nor starts through `Do` |
| `start` | Required: once, entering state 2 |
| `active`, `end`, `end2`, `abort` | Optional: every frame of state 2, every frame of state 3, once when state 3 finishes, round cut off |

Vanilla events bias item drops through `CityEvent_ModifyItemFallDesc(kind)`, and every step of that
path indexes by kind against 16-wide storage, so a custom kind matches nothing and reads past the
chance arrays. A custom event that wants biased drops passes a vanilla kind to borrow its profile,
or writes the box-spawn fields directly.

## API

`CustomEventsAPI` is published with `Hoshi_ExportMod` and resolved with
`Hoshi_ImportMod(CUSTOM_EVENTS_MOD_NAME, CUSTOM_EVENTS_API_MAJOR, CUSTOM_EVENTS_API_MINOR)`. It
exports `Do(kind)`, described above, and `DoDangerRain(rain)`, which starts Danger Rain with a
chosen `DangerRainKind` instead of a random one.
