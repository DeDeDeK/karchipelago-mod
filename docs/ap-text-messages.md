# Archipelago Text Messages

The in-game lines that report multiworld traffic: a location sent, an item received, a server hint, a goal or release announcement, chat, a DeathLink or TrapLink crossing the wire. The Python client composes each line and the mod renders it; the mod side is `mods/archipelago/src/ap_text.c` for the client's lines and `ap_announce.c` for its own, plus the Messages settings menu; the client side is `KARText.py` and the `_push_*_text` methods in `KARClient.py`.

## Why the client writes the text

The mod knows its own items by name and color, and nothing else. An Archipelago seed puts this slot's checks in front of items from every other world in the multiworld, and the names of those items, of the locations that hold them, and of the players who own them exist only in the seed's data - which lives in the client. Shipping that table into game memory would mean thousands of strings for a table the mod reads once per event.

So the mod ships no names. The client composes the whole line - already resolved, already colored, already folded to the glyphs the font can draw - and hands it over as a small fixed-size record.

## Message Kinds

Each kind has its own Off/On toggle under Archipelago Settings -> Messages. The mod filters by kind as it renders and is the authority, so a toggle takes effect immediately even if the client is a poll behind; the client also reads the toggle mask, so a disabled kind is never composed at all.

| Kind | Default | Content |
|------|---------|---------|
| `APTEXT_KIND_CHECK` | On | A checkbox this slot completed, and the item it sent |
| `APTEXT_KIND_ITEM` | On | An item arriving for this slot |
| `APTEXT_KIND_HINT` | On | A server hint concerning this slot |
| `APTEXT_KIND_STATUS` | On | Goal / release / collect, and client connect state |
| `APTEXT_KIND_CHAT` | Off | Player and server chat |
| `APTEXT_KIND_LINK` | On | A DeathLink or TrapLink sent or arriving, and who sent it |

Those six gate what the client writes. The lines the mod composes itself sit under Messages -> Local and are gated separately by `APLocalKind`, since nothing about them is on the wire:

| Kind | Default | Content |
|------|---------|---------|
| `APLOCAL_CHECK` | Off | `Check recorded`, as a checkbox is recorded |
| `APLOCAL_ITEM` | Off | `Unlocked Machine: Warp Star`, `Received: Sleep` - a grant being applied |
| `APLOCAL_GOAL` | On | `Air Ride goal complete!`, `All Goals complete!` |
| `APLOCAL_LINK` | Off | `DeathLink sent!`, `TrapLink received!` - a link firing or landing here |

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
DeathLink from Kirby64
TrapLink sent (Bad Patch)
TrapLink from Kirby64 (Ice Trap)
```

A check line is worded like the server's own ItemSend, naming this slot as the finder and the player the item went to; a self-placed item collapses to the `found their` form. An item line names the sending player, and omits it when the item came from the server's starting inventory. An item this slot placed for itself gets no item line at all while check messages are on - its check line already named it - so the `Warp Star received` form is what a starting-inventory item prints, or a self-placed one with checks turned off.

Hints are reworded rather than relayed verbatim. The server only sends a hint to the two slots it concerns - the player receiving the item and the player whose world holds it - so exactly one of those is always this slot, and Archipelago's full phrasing spends most of one screen line restating it. The hint status becomes both the color and a word inside the `Hint:` prefix, which leaves the room the location name needs.

Goal, release, collect and chat lines arrive from the server as a single uncolored run of text; the client strips the `(Team #N)` stamp and colors the line by kind.

Link lines name the direction and the other player, and carry the trap name the Bounce was tagged with - the outgoing one KAR chose, or whatever the sending world called its own. The incoming name is shown but not acted on: the mod rolls a local trap regardless. A DeathLink Bounce carries a free-text cause as well, which is dropped - it restates the source name in a sentence that would cost most of a screen line.

## Colors

Segments carry an `APTextColor`, which is Archipelago's own GUI palette by name plus a default that follows the text box's `DefaultColor`. The client resolves colors by subclassing Archipelago's `JSONtoTextParser` and overriding only the leaf handler, so item-flag colors, player colors, location colors and hint-status colors match every other Archipelago client without restating the rules:

| Element | Color |
|---------|-------|
| Progression item | plum |
| Useful item | slateblue |
| Trap | salmon |
| Filler item | cyan |
| This slot's own name | magenta |
| Another player's name | yellow |
| Location name | green |
| Entrance name | blue |
| Hint status found / unspecified / no priority / avoid / priority | green / white / slateblue / salmon / plum |

`black` is lifted off pure black in the mod's table, since the text box background is dark. Turning off the text box's own "Colored Names" setting collapses every segment to the default color, which is why the hint status is a word as well as a color.

## Length

The canvas is 640 px wide with a 10 px margin, and English glyphs average about 20 units at the font's natural size, so roughly 103 characters fit on a line at the Small font size, 77 at Med and 56 at Large. The text box wraps a longer message onto up to three lines, breaking at spaces, and replaces anything past the last line with `..`. Nothing is scaled down to fit, so a message always renders at the player's chosen font size.

That makes the mod the authority on length, because it is the side that knows the font size and the canvas. The wire record is sized past what any font size can show - 243 rendered characters across at most 8 colored runs - so the client's own trim is a transport limit that normal traffic never reaches. When it does fire it cuts the longest run first, which is the location name in the messages that have one, and marks it with a trailing `..`.

## Character Set

The game font draws alphanumerics and a fixed set of punctuation: `` !"#$%&'()*+,-./:;<=>?@[]_ `` and space. Anything else emits an undefined glyph code, so the client folds text before packing: NFKD decomposition first, so an accented letter degrades to its base letter rather than vanishing, then everything outside the renderable set is dropped and runs of whitespace collapse. A name with nothing renderable left becomes `?` rather than disappearing mid-sentence.

## Duplicate Suppression

Two sides narrate the same events. The mod knows an item is being applied and a checkbox is being recorded; the client knows which item, whose it was, and where it went. Left alone that prints each event twice, so the mod's half is off by default: `APLOCAL_CHECK` and `APLOCAL_ITEM` both start Off, and one event produces the client's one line. Turning either on gives both lines - the mod's at the moment the event lands, the client's a poll later - which is also what a player running without a client turns on to get any feedback at all. `APLOCAL_GOAL` starts On because the client has no equivalent: the server's goal broadcast names the slot, not the mode that just finished.

`APLOCAL_LINK` starts Off for the same reason, and the split is sharper there: DeathLink and TrapLink traffic only exists while a client is attached, so the client's line is never missing. The two halves narrate different moments - the client's fires when the traffic crosses the wire, the mod's when the effect actually fires or lands here, which is not the same frame when the mod holds a receive (a Top Ride countdown, the 3D intro) or drops it (City Trial Free Run).

The decision is the toggle and nothing else. The mod does not try to work out whether a client is attached, or whether one narrated any particular item.

Every grant announce goes through `APAnnounce_Grant` / `APAnnounce_GrantSegments` (`ap_announce.c`) rather than calling the text box itself. That is deliberate: the toggle is a property of the whole category, and a new unlock handler that copies its neighbour gets it without anyone remembering a guard. Announces that carry something the AP item name does not - `Patch cap increased (50%)`, `Spawn rate increased (60%)` - are the exception and call the text box directly, which is what marks them as exceptional. So does every non-AP path: EnergyLink purchases, in-game pickups, gate prompts. The boot regrant suppresses the same category through the same funnel, via `ap_regrant_quiet`.

The check and goal lines have one call site each, in `check_detection.c`, so they test `APAnnounce_LocalEnabled` directly instead of routing through a funnel of their own. So do the link lines, which sit at the send and receive points in `deathlink.c` and `traplink.c`.

## Client Status

The mod tracks no client state. `ap_text.c` renders what arrives in the mailbox, and the lines the mod composes itself turn on their own Local toggles and nothing else, so no game-side decision depends on whether a client is attached.

That leaves the connect and disconnect lines to the client, which knows both moments first hand. It posts "Archipelago client connected" as an ordinary `STATUS` message once the handshake completes, and "disconnected" on a clean shutdown - written straight into the mailbox rather than queued, since nothing drains the backlog after that, and skipped if the mod is still holding an earlier message. A client killed outright posts neither.

## Transport

One 256-byte record in `APData` plus a pending flag, the same mailbox handshake the item channel uses: the client writes the body, then sets the flag; the mod renders and clears it. The mod holds a pending message while the text box has no screen canvas, so a scene load backpressures the client instead of losing the message.

That caps delivery at one message per client poll, roughly 10 a second. The text box shows at most 8 at a time and holds each for several seconds, so it retires messages far slower than that, and a shared ring would only move the backlog from the client into game memory. The client queues composed messages in an unbounded deque instead and writes one per poll, keeping all Dolphin access in its poll loop. Nothing is collapsed or dropped on the way in: a burst of checks queues one line each and drains at the poll's own pace. Goaling a world releases every check this slot placed at once, which is the case the queue is sized for - the records are 256 bytes each and the client has the memory.
