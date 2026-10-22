# Gravity Change Event

A custom City Trial event (`CUSTOM_EVKIND_GRAVITY_CHANGE`, kind 17) that halves or doubles the
stage's gravity for 15 seconds. Low gravity gives floaty machines, long high jumps and extended air
time; high gravity makes machines hug the ground with short jumps and snappy landings. It runs
through the `custom_events` framework like every custom event, with a siren intro, sky preset 8
(Pink Sky), BGM file 0x31 (`event_meteo`), a 900-frame duration and roll weight 20.

## How It Works

The game's gravity is a strength scalar and a unit direction in two adjacent `StageNode` fields,
and `Gr_GetDownVector` (0x800ceb18) hands both to every machine, item, enemy and the camera. City
Trial's open map has no gravity zones, so every query there falls through to those two fields.

`start` picks a multiplier with `HSD_Randi(2)` - 0.5 or 2.0 - saves the stage's
`gravity_strength` (+0x0C) and a pointer to it, and writes the scaled value once. Nothing in the
game writes that field at runtime, so the one write holds for the whole event. It scales the
strength and never `gravity_dir`: machines derive their up vector by negating the raw direction,
so a scaled direction would leave that up vector non-unit and break orientation and air control.

`end2` writes the saved value back and clears the pointer, so a second call does
nothing. It is also the event's `abort`, which the framework runs when the round ends mid-event:
`Preload_LoadStageFiles` (0x800ce964) preloads `GrCity1.dat` into a heap that can survive into the
next round, and the archive loader reuses a resident preload as-is, so a strength left scaled
would carry over. The abort runs at the 3D exit, before the scene heaps are reset, while the saved
pointer is still valid.

## Effect on Other Actors

- **Machines** feel the full change: their pull is `down * strength`, refreshed every frame.
- **Enemies** reorient to the global direction but fall by their own `param_gravity`, so they fall
  at the usual rate.
- **Items** follow the global direction, but their fall speed comes from their own item
  parameters, so they fall at the usual rate too.
