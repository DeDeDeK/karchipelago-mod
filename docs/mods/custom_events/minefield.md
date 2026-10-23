# Minefield

A custom City Trial event (`CUSTOM_EVKIND_MINEFIELD`, kind 25, "A minefield has been laid!") that
embeds 40 armed sensor bombs in floors across the whole city, tunnels and the garage included. The
mines threaten every rider, CPUs included, and credit no one. They are vanilla sensor bombs, so they
blink, arm, sense and explode exactly like a thrown one, and they outlast the event: each goes off
when a rider comes near or when its own fuse runs out. The event runs through the `custom_events`
framework with a siren intro, sky preset `CITYSKY_GRAY_SKY`, BGM file 0x33 (`event_stationfire`),
an 1800-frame duration and roll weight 20, and registers only `start`.

## The Sensor Bomb

A sensor bomb is weapon kind `WPKIND_SENSORBOMB` (15). Its `WeaponData.kind_params` is the bomb's
own copy of the kind data's parameter block (`SensorBombParams`), so one bomb can be retuned without
touching the rest. Its life:

- **Create.** `SensorBomb_PostInit` (0x80228d8c) seeds the fuse,
  `WeaponData.kind_scratch.sensor_bomb.fuse`, from `SensorBombParams.fuse` (2400 frames) and
  leaves the bomb HELD, snapped to its owner's hand.
- **Flight.** `SensorBomb_TryThrow` (0x80228f08) writes the velocity and enters `ARMED_FLYING`,
  where the bomb falls under stage gravity.
- **Embed.** On a floor contact, `SensorBomb_Land` (0x802290bc) enters `ARMED_STATIONARY`, seeds the
  arm delay, `kind_scratch.sensor_bomb.arm_delay` (60 frames), starts the blink effect and installs
  `SensorBomb_SensorThink` (0x802292f8) as the bomb's `trigger_callback`.
- **Sensor.** Once the arm delay has run out, the sensor detonates the bomb
  (`SensorBomb_Detonate`, 0x802295a8) as soon as any rider's hand bone is within `sensor_radius`
  (30 units). It skips no rider, not even the owner.
- **Fuse.** `SensorBomb_FuseThink` (0x80229e64), the `anim_callback` of both armed states, burns the
  fuse, speeds the blink from anim rate 1.0 to 5.0 over its first 280 frames, and detonates the bomb
  when it reaches 0. A sensor bomb's `lifetime` is 0 (infinite), so the fuse is what ends a bomb no
  one touches.

## Placement

Mine spots come from the CPU road ranges, 802 pairs of linear splines in City Trial, each the left
and right road edge CPUs steer between, about 2 stage units above the floor. The event picks a
random pair, a random parameter on both edges and a random point between them, keeping 15% of the
width clear at each edge. A `Raycast_Ground` (0x800d1ac4) from 4 units above that point to 12 below
finds the floor under it. That keeps every mine on a surface machines drive over, and stacked floors
keep their own mines.

A floor is rejected when its triangle:

- has `GRCOLL_FLAG_MOVING` set, since a mine embedded on a lift would be left floating when the lift
  moves away;
- has a normal with a Y component below 0.8;
- is one of ground types 26-30: the grind rail, rail dash and water rail surfaces, the sea and the
  barrier.

A spot also has to be 60 units from every mine already placed, twice the sensor radius, and 60 from
every rider. Up to 400 picks are drawn; 40 mines usually take 50-70. Each spawn first checks
`OSCheckHeap(HSD_GetHeapID())` and stops once less than 512 KB would stay free, because an
allocation the heap cannot serve crashes the game rather than failing the spawn. A mine that
fails to spawn ends the field there.

## Spawning a Mine

`Weapon_Create` (0x8021f428) builds each mine 3 units above its floor with no owner, a random yaw,
scale 1.0 and zero velocity. A NULL owner makes `HitColl_CheckIfSamePlayer` answer "not the same
player" for everyone, so the explosion hits every rider and no player is credited. The mine moves
straight to `ARMED_FLYING` in the same call: in HELD its first proc would snap it to the missing
owner's hand. It then falls onto the floor, where `SensorBomb_Land` embeds it.

Each mine's fuse is shortened by up to 300 frames. Their self-detonations then spread over five
seconds instead of firing in one frame, 35 to 40 seconds after the field is laid, which is 5 to 10
seconds after the event ends.

## The Active-Bomb Cap

`SensorBomb_TryThrow` ends with `SensorBomb_EnforceMaxActive` (0x8022a270). It counts every sensor
bomb on the weapon list that is neither HELD nor EXPLODING, whatever its owner, and fades each one
past its own `SensorBombParams.max_active` (2). Left alone, a player throwing a Sensor Bomb item
would fade all but two of the mines.

At boot the event replaces that call (at 0x80228fa8) with a wrapper. The wrapper treats an ownerless
sensor bomb as a stage hazard rather than anyone's throw: for the length of the call it gives each
one the kind `WPKIND_BOMB`, then restores `WPKIND_SENSORBOMB`. Players' bombs keep the vanilla cap of
two between them. The rule covers every ownerless sensor bomb, the mod's own or another's.
