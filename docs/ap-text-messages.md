# Archipelago Text Messages

The in-game lines that report multiworld traffic: a location sent, an item received, a server hint, a goal or release announcement, chat. The Python client composes each line and the mod renders it; the mod side is `mods/archipelago/src/ap_text.c` plus the Messages settings menu, and the client side is `KARText.py` and the `_push_*_text` methods in `KARClient.py`.

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

## Wording

```
Check: sent Progressive Sword to Kirby64
Warp Star received from Kirby64
Warp Star received
Hint (priority): Progressive Sword is at Stadium DRAG RACE 2 (Kirby64)
Hint: Kirby64's Progressive Sword is at Stadium DRAG RACE 2
Archipelago client connected
```

A check line names the receiving player, which for a self-placed item is this slot; an item line names the sending player, and omits it when the item came from this slot or from the server's starting inventory.

Hints are reworded rather than relayed verbatim. The server only sends a hint to the two slots it concerns - the player receiving the item and the player whose world holds it - so exactly one of those is always this slot, and Archipelago's full phrasing spends most of one screen line restating it. The hint status becomes both the color and a word inside the `Hint:` prefix, which keeps the whole thing inside the five-run cap with the location name intact.

Goal, release, collect and chat lines arrive from the server as a single uncolored run of text; the client strips the `(Team #N)` stamp and colors the line by kind.

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

The mod announces its own grants - `Unlocked Machine: Warp Star`, `Received: Sleep` - and those restate what the client's item line already said. While a client is attached and item messages are on, `ap_item_quiet` is set around the application of any item that came from the AP mailbox, which turns those announces off, so one item produces one line.

Every grant announce goes through `APAnnounce_Grant` / `APAnnounce_GrantSegments` (`ap_announce.c`) rather than calling the text box itself. That is deliberate: suppression is a property of the whole category, and a new unlock handler that copies its neighbour gets it without anyone remembering a guard. Announces that carry something the AP item name does not - `Patch cap increased (50%)`, `Spawn rate increased (60%)` - are the exception and call the text box directly, which is what marks them as exceptional. So does every non-AP path: EnergyLink purchases, TrapLink traps, in-game pickups, gate prompts. The boot regrant suppresses the same category through the same funnel, via `ap_regrant_quiet`.

The mailbox origin is tracked per queued item in a RAM-only array parallel to `APSave.unprocessed_items`, so an item still queued across a reboot announces locally again - the client's line for it scrolled off in the previous session.

## Client Detection

`client_alive` is a counter the client bumps on every poll. The mod treats no change for 180 frames as "no client", which drives two behaviors: the connect and disconnect lines, and the fallback `Check: recorded` that `check_detection.c` posts when a check is recorded with nothing attached to report it to. With a client attached that line is skipped, because the client's richer line arrives about a poll later.

## Transport

One 128-byte record in `APData` plus a pending flag, the same mailbox handshake the item channel uses: the client writes the body, then sets the flag; the mod renders and clears it. The mod holds a pending message while the text box has no screen canvas, so a scene load backpressures the client instead of losing the message.

That caps delivery at one message per client poll, roughly 10 a second. The text box shows at most 8 at a time and holds each for several seconds, so it retires messages far slower than that, and a shared ring would only move the backlog from the client into game memory. The client queues composed messages in a bounded deque instead and writes one per poll, keeping all Dolphin access in its poll loop; if the deque overflows it drops the oldest and logs once. A burst of more than three checks in one poll collapses to a single count line before it ever reaches the deque.
