# The Archipelago Star Mod

`mods/ap_star/` is the Archipelago Star: the machine archive, the charge-release sphere
shot, the six-sphere City Trial assembly and its cinematic, packaged as one drop-in mod.
Nothing Archipelago-specific is built into it. Built on its own it is a complete
Hydra-style legendary machine - every sphere spawns, the set assembles, the star drives -
and a consumer that wants the spheres behind its own progression narrows that through the
mod's API rather than replacing any of the machinery.

It is the first machine to ship this way, and the shape is meant to be copied.

## Layout

```
mods/ap_star/
  include/ap_star_api.h            what consumers import
  assets/
    machines/VcStarAp.dat          the machine, discovered by custom_machines
    machines/VcStarAp.ssm          its sound bank
    machines/VcStarAp.art          its select-screen UI frames
    items/ApSphere*.dat            the six spheres, discovered by custom_items
    ApStarShot.dat                 the fired sphere's model
    ApStarGlow.dat, ApStarParts.dat  the assembly cutscene's models and camera,
                                   named by the machine descriptor and run by custom_machines
    ApPieceIcons.dat               the collection tracker's art
  src/
    main.c                         ModDesc, hoshi callbacks, settings page
    ap_star.c / .h                 kind lookup, sphere gate, handler lists, API export
    ap_star_shot.c / .h            the charge-release projectile
    ap_star_pieces.c / .h          sphere delivery, collection, tracker, mount
```

`mods/*/assets/` is copied to the FST root by the ordinary asset step, so a machine mod
needs no packaging change to be found: `assets/machines/VcStarAp.dat` lands at
`machines/VcStarAp.dat`, which is the folder `custom_machines` scans at boot.

The four loose archives sit at the FST root rather than under `machines/`, and have to.
The discovery scan probes every `.dat` in `machines/` for a `customMachine` public and
reports the ones that have none, so a companion archive parked there would be reported as
a broken machine every boot. The root is flat and shared, which makes a distinctive
filename prefix - `ApStar*`, `ApPiece*` - the only namespacing available; the engine's own
loaders reach these by bare filename in any case, through `Gm_LoadGameFile`,
`lbLoadArchive` and `Preload_CreateEntry`.

`VcStarAp.dat` and `VcStarAp.ssm` carry data cloned out of the retail disc, so neither is
authored by hand: `scripts/hsd/make_ap_star.py` and `scripts/audio/machine_audio.py` build
them from an `iso/` extraction, and `scripts/hsd/make_machine_art.py` builds the `.art`
side-car. Every `assets/` tree is `.gitignore`d, so none of them are in the repo.

## Binding to the machine

`custom_machines` assigns appended kinds in FST scan order, so the star's `MachineKind` and
class slot are whatever the registry handed it that boot. `AP_STAR_MACHINE_NAME`
("Archipelago Star") is the `CustomMachineDesc.name` in the archive, and the only thing
tying `machines/VcStarAp.dat` to this code - `ApStar_MachineKind()` resolves it through
`CustomMachinesAPI.FindKindByName`. The string lives in `ap_star_api.h` and is written into
the archive by the authoring script; changing one without the other unbinds the machine,
and the code then runs as if the archive were absent.

Resolution is lazy, not done at `OnBoot`. Mods run in the order their `.bin` files sit in
the FST, `ap_star` sorts before `custom_machines`, and a mod's export is not available
until its own `OnBoot` has run - so an `OnBoot` lookup always answers -1. Everything that
needs the kind asks for it at `On3DLoadEnd` or later, and every entry point tolerates -1 by
doing nothing.

## The API

Consumers import it with
`Hoshi_ImportMod(AP_STAR_MOD_NAME, AP_STAR_API_MAJOR, AP_STAR_API_MINOR)`.

| call | what it does |
|---|---|
| `GetMachineKind` | the registered `MachineKind`, or -1 |
| `GetPieceName` | one sphere's display name, which is its archive's `CustomItemDesc.name` |
| `SetPieceEnabled` / `IsPieceEnabled` | one sphere's gate bit |
| `SetPieceMask` / `GetPieceMask` | all six at once, one bit per `APStarPieceKind` |
| `AddAssembleHandler` / `Remove...` | called as a player completes a set |
| `WasAssembled` | boot-sticky, 1 once anyone has assembled |
| `AssembledThisRound` | per-player, cleared on every 3D load |
| `DebugSpawnPiece` | drop one sphere in front of a machine |

The gate starts at `AP_STAR_PIECE_ALL`. A closed sphere is held out of the item registry
entirely, so it never receives an `ItemKind` and no path can spawn it; a round arms only
the open ones and a partial set delivers but cannot complete. The gate is read at 3D load
start, because `custom_items` registers its items in `CityItemSpawn_Init`'s epilogue and
that is the last moment a held item can be skipped.

The API is a gate, not an unlock: whether a sphere is earned, bought or awarded is the
consumer's idea, and all this mod knows is which spheres are in play. It says nothing to
the player about one arriving for the same reason, and leaves the textbox alone. It does
own the sphere names, though - they are the `CustomItemDesc.name` of the archives it binds
by, so a consumer naming a sphere takes it from `GetPieceName` rather than keeping a copy
that can drift out of step with `items/ApSphere*.dat`.

## The Archipelago consumer

`mods/archipelago/src/gate_ap_star.c` is the whole of the Archipelago side, and the only
file in that mod that imports this one. It holds `APSave.ap_star_piece_unlocked_mask` -
reached from the client through the ordinary `AP_UNLOCK_AP_STAR_PIECE` category - announces
an arriving sphere with the same `"Unlocked Item: "` textbox every other unlock uses, and
latches `APCK_ASSEMBLE_AP_STAR` from an assemble handler. Archipelago numbers the spheres
itself, as `APStarPiece` alongside the 820-825 item IDs in `archipelago_api.h`, so its
public header stands on its own; the two orders are a contract, since a sphere unlock item
is applied by index.

It **pushes** the mask into the gate on every write rather than letting `ap_star` read it
back. `ap_star` sorts before `archipelago` in the FST, so by the time `archipelago`'s own
load-start callback runs, `ap_star` has already armed the round. Every writer of the mask
therefore ends in `GateApStar_PushMask()`: the boot restore in `OnSaveLoaded`, the
per-sphere unlock, and `Unlock_SetMask`, which is the single choke point the client and the
ungated pre-fill both go through.

Archipelago also chooses the star as the title screen's idle demo machine, in `main_menu.c`,
through `GateApStar_MachineKind()`; it falls back to a vanilla star when that answers -1.
Whether an assembled star turns up loose on the field is not an ap_star-specific path at all:
`GateMachines_SpawnWeight` in `gate_machines.c` gates every kind, vanilla or registered, on
the machine-unlock mask, and the star's unlock item falls in the appended range that starts at
856 and is assigned in the registry's discovery order.

## Settings

The mod carries its own `ModDesc` settings page, **Archipelago Star**, with the **Star
Shot** toggle (default on) and its own hoshi save slot. Nothing pushes an Archipelago slot
option into it - the toggle is the player's, so a standalone build can turn the shot off
and an Archipelago build does not override the player's choice on connect.

## Shipping a second machine

A machine that needs no code of its own is not a mod at all - dropping its `.dat` into any
built mod's `assets/machines/` registers it, and it spawns, drives and (if its descriptor
asks) takes a select-screen cell. `CUSTOM_MACHINE_MAX` caps the registry at 13.

A machine that wants behavior copies this mod's shape: a folder under `mods/`, its archive
under `assets/machines/`, its companion archives at `assets/` root under a distinctive
prefix, a `ModDesc` in `src/main.c`, and a lazy `FindKindByName` bind on its own
descriptor name. Per-machine behavior hangs off `CustomMachinesAPI.SetStarInitHandler` and
`SetStarThinkHandler`, which layer on top of the handler the descriptor's `clone_kind`
inherited rather than replacing it. The only build registration needed is adding
`include/` to the Makefile's `INCLUDES` list, and only if the mod publishes an API -
sources under `src/` are globbed.
