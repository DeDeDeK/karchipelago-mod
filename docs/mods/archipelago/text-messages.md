# Text Messages

The in-game lines that report multiworld traffic - a location sent, an item received, a hint, a
goal or release, chat, a DeathLink or TrapLink crossing. They appear in the textbox mod's text
box. Most are written by the Python client and only rendered by the mod; a few the mod writes
itself. This doc covers who writes what, the wording and colors, and how the two sides avoid
printing every event twice.

## Why the client writes them

The mod knows its own items and nothing else. A seed puts items from every world on this slot's
checks, and the names of those items, the locations that hold them and the players who own them
exist only in the seed data the client has. So the client composes each whole line - resolved,
colored, and folded to the glyphs the font can draw - and hands it over as one fixed 256-byte
record through the `APData` text mailbox; the mod renders it.

## Kinds

Each kind has an Off / On switch under Archipelago Settings -> Messages. The mod checks the
switch when it renders, so the menu wins even against a client a poll behind; the client also
reads the switches (`text_menu_mask`) and does not compose a kind that is off.

| `APTextKind` | Default | Line |
|---|---|---|
| `APTEXT_KIND_CHECK` | On | A checkbox this slot completed and the item it sent |
| `APTEXT_KIND_ITEM` | On | An item arriving for this slot |
| `APTEXT_KIND_HINT` | On | A hint concerning this slot |
| `APTEXT_KIND_STATUS` | On | Goal, release, collect, and the client connecting or disconnecting |
| `APTEXT_KIND_CHAT` | Off | Player and server chat |
| `APTEXT_KIND_LINK` | On | A DeathLink or TrapLink sent or received, and from whom |

The lines the mod writes itself sit under Messages -> Local, by `APLocalKind`:

| `APLocalKind` | Default | Line |
|---|---|---|
| `APLOCAL_CHECK` | Off | "Check recorded", as a checkbox completes |
| `APLOCAL_ITEM` | Off | An item or unlock being applied: "Unlocked Machine: Warp Star", "Received: Sleep ability", "Patch cap increased! (19/30)" |
| `APLOCAL_GOAL` | On | "Air Ride goal complete!", "All Goals complete!" |
| `APLOCAL_LINK` | Off | "DeathLink sent!", "TrapLink received!" |

Every item and unlock line goes through one funnel, `APAnnounce_Grant` /
`APAnnounce_GrantSegments`, which applies the Items switch and stays silent during the quiet
re-grant of checklist rewards; a new unlock that copies its neighbour inherits both. The check,
goal and link lines test `APAnnounce_LocalEnabled` at their few call sites. Only lines that
answer a menu action print unconditionally: the EnergyLink shop's replies and the gates' refusal
messages.

## Printing each event once

Both sides see the same events: the mod knows an item is being applied or a checkbox recorded;
the client knows which item, whose, and where it went. So the mod's Check, Items and Links lines
start off and each event prints the client's one line. Turning a Local row on gives both lines -
the mod's when the effect lands, the client's a poll later - which is also how a player with no
client gets any feedback. Goals starts on because the server's goal broadcast names the slot,
not the row. The mod never tries to tell whether a client is attached; the switch is the whole
decision.

## Wording

```
Kirby sent Progressive Sword to Kirby64
Kirby found their Warp Star
Warp Star received from Kirby64
Warp Star received
Hint (priority): Progressive Sword is at Stadium DRAG RACE 2 (Kirby64)
Hint: Kirby64's Progressive Sword is at Stadium DRAG RACE 2
Archipelago client connected
DeathLink sent
TrapLink from Kirby64 (Ice Trap)
```

- A **check** line reads like the server's own, collapsing to "found their" for an item this
  slot placed for itself.
- An **item** line names the sender, and omits it for starting inventory. An item this slot
  placed for itself gets no item line while check lines are on - its check line named it.
- **Hints** are reworded: a hint only ever reaches the two slots it concerns, so one side is
  always this slot, and the status becomes a word in the prefix as well as a color.
- **Server lines** (goal, release, collect, chat) arrive as one run of text; the client drops the
  `(Team #N)` stamp and colors them by type - goal green, release and collect yellow, server
  chat orange. It does not relay `ItemSend`, which the server sends the whole team for every
  check anyone makes.
- **Link** lines name the direction, the other player and the trap name; a DeathLink's cause is
  dropped as too long.
- **Status**: "Archipelago client connected" once the handshake completes, and "disconnected" on
  a clean shutdown, written straight into the mailbox and skipped if it is still full. A client
  killed outright posts neither.

## Colors

Each run carries an `APTextColor`: Archipelago's own GUI colors by name, plus a default that
follows the text box's. The client resolves them with Archipelago's own `JSONtoTextParser`, so
item, player, location and hint-status colors match every other Archipelago client (progression
plum, useful slateblue, trap salmon, this slot magenta, other players yellow, locations green).
The mod lifts black to a dark gray against the dark text box. With the text box's colored-names
setting off every run draws in the default color, which is why a hint's status is also a word.

## Length and characters

The text box draws at the player's chosen font size, wraps onto at most three lines at spaces,
and ends the overflow with `..` - nothing is scaled down. The record carries up to 243
characters in up to eight runs, more than any font size shows, so the mod decides the fit. The
client only trims a message that would not fit the record, cutting the longest run first (the
location name, in lines that have one) and marking it `..`.

The font draws letters, digits, space and ``!"#$%&'()*+,-./:;<=>?@[]_``. The client folds text
before packing: Unicode decomposition first, so an accented letter becomes its base letter, then
anything else is dropped and whitespace runs collapse. A name with nothing left becomes `?`.

## Pacing

The mailbox holds one message and the client writes one per 0.1 s poll at most, far faster than
the text box retires them (it shows up to eight at a time for several seconds each). So the
client keeps an unbounded queue of composed messages and drains it at the poll's pace; nothing is
merged or dropped on the way, and a burst of checks - a goal releasing every location at once -
simply queues. The mod holds a pending message while no screen canvas exists, so a scene load
delays a message rather than losing it.

## Testing without a client

`ArchipelagoAPI.DebugSendText(kind)` writes a canned line of each `APTextKind` into the mailbox
the way the client does - the record first, `text_pending` last - with the client's wording and
colors, so rendering, the per-kind filter, the palette and the hold across a scene load all
behave as in a live session. `DebugSendOverlongText` fills all eight runs past the third line,
the only local way to see the wrap and the `..`. Both return 0 while a message is still pending,
the client's own rule. archipelago_debug exposes them as its Messages page.
