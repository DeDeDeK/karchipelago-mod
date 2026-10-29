# Kirby Color Gating

Kirby's eight colors are each locked behind an unlock item (880-887, `AP_COLOR_UNLOCK_BASE` +
`KirbyColor`). A locked color cannot be cycled to on any character select screen, is never given
to a CPU, and is replaced wherever a mode seeds the default colors. All three modes are covered:
Air Ride and Top Ride (race, Free Run, Time Attack) and City Trial.

The mask is the u8 `APSave.color_unlocked_mask`, set by `GateColors_UnlockColor`, which
announces "Unlocked Color: <name> Kirby". Pink is a normal unlock like the rest; with color
gating on, the apworld precollects one starting color, so the mask is never empty in a real
seed. If it ever were, every fallback below uses Pink. The vanilla checklist's color rewards
(Green, Purple, Brown, White) unlock through the same function, silently, so rewards and AP items
share one mask.

## The select struct fields

Each mode's select data keeps a per-slot `color[4]` byte array of `KirbyColor`s, which is what
the gate checks and rewrites:

| Mode | Color array | Slot kind array |
|---|---|---|
| Air Ride | `airride_select_ply.color` (GameData+0x15b) | `airride_select_ply.slot_kind` (+0x14f) |
| Top Ride | `topride_select_ply.color` (GameData+0x1ba) | `topride_select_ply.panel_pkind` (+0x1b2) |
| City Trial | `city_select_ply.ply_color` (GameData+0x221) | `city_select_ply.slot_kind` (+0x215) |

The Air Ride struct also has an `icon[4]` array beside `color[]`. It is not a color: it indexes
the available-machine list. Color gating never touches it.

## Cycling

Each screen's L/R color changer asks whether a candidate color is available, and its vanilla
paths (colors 0-3 always, 4-7 by checklist reward) meet at one instruction. A hook there answers
from the mask instead:

| Site | In |
|---|---|
| 0x8002176c | `CSS_airRide_colorChanger` (0x80021654) |
| 0x8002a510 | `CSS_topRide_colorChanger` (0x8002a400) |
| 0x8002f350 | `CitySelect_ChangeColor` (0x8002f238) |

## Default colors

Every mode seeds `color[0..3] = {0, 1, 2, 3}` when its select data initializes, which may name
locked colors. Any locked entry is replaced with the lowest unlocked color.

- **Air Ride** is validated at the point each select screen's init block rejoins the main path:
  0x800295e8 in `CSS_airRide_RaceUpdate` (0x80028888) and 0x80029e34 in
  `CSS_airRide_FreeTimeUpdate` (0x80029bd8).
- **Top Ride** is validated after each of its three inits: 0x8002d06c in
  `TopRide_InitSelectData`, 0x8002d704 in `TopRide_RaceInit` and 0x8002db8c in
  `TopRide_SoloInit`.
- **City Trial** seeds its colors in several places, each guarded so that re-entering the same
  sub-mode keeps the previous session's colors, and none makes a clean hook point. The mod
  validates instead on every load of the City Trial player select (`OnPlayerSelectLoad` with
  minor `MNRKIND_CITYPLYSELECT`), after the screen has drawn, and repaints any panel it changes.

## CPU colors

A CPU gets a random unlocked color that no other panel on screen shows
(`GateColors_RandomForPanel`), repeating one only when nothing else is left. Without it a CPU
would keep its slot's default - unlocked after validation, but the same every race, and the same
for several CPUs when few colors are unlocked. Human picks are never touched.

A CPU is re-colored only on the frame its panel becomes a CPU, so a color the player sets on a
CPU panel by hand stands until the panel is toggled away and back.

| Screen | Where the CPU appears | Hook |
|---|---|---|
| Air Ride | `loadCPU` (0x80023600) fills the CPU slots in order | 0x800236a8, the CPU kind store in its slot loop |
| City Trial | The one `slot_kind` 3 -> 2 branch in `CitySelect_InputUpdate` (0x80032d34) | 0x80033560, between the kind store and the engine's own repaint |
| Top Ride | `panel_pkind` reaches CPU from seven inlined writers, some by stepping | Each `TopRide_LobbyThink` frame diffs `panel_pkind` against a mirror (0x8002dd40); a hook in `TopRide_LobbyInit` (0x8002dca8) resets it |

The Top Ride mirror is seeded on the lobby's first think, after its own setup, so a panel that
opens as a CPU keeps its color; the lobby inits give their CPU panels random colors through the
machine gate's init fixups instead. Air Ride re-rolls every CPU on each confirm, as vanilla does.

Storing the byte does not redraw the panel. The City Trial CPU hook lands just before the
engine's own repaint; the Top Ride diff and the City Trial validation repaint themselves through
`TopRide_UpdatePanel` (0x80134a0c) and `CitySelect_UpdatePlayer` (0x801354d4), with the frame
from `Gm_GetColorAnimFrame` (0x80009630).

Colors are assigned on the select screen rather than when the round commits so that the panel
and the round always agree: City Trial copies `ply_color` into the round in
`CitySelect_InitPlayerMachines` just before leaving the screen, where a change would never be
drawn.
