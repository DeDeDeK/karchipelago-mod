# Volcano

The volcano layer of `custom_weather`: City Trial's volcano erupts a set number of times per round,
each eruption throwing volleys of copy-ability projectiles out of the crater on ballistic arcs.
The projectiles are real weapon actors with live hitboxes, so an eruption threatens riders,
machines and boxes alike. It ships on Volcanic (fire, four eruptions) and Storm (plasma, two) and
can be forced onto any preset from its menu. Spawning weapons with no owner is the interesting
part: most of the engine's projectile kinds assume one.

## Scheduling

`VolcanoDef` holds `theme`, `eruptions` (default 3), `duration` (default 270 frames), `interval`
(14 frames between volleys) and `burst` (3 projectiles per volley), `power` (launch speed scale)
and `spread` (cone width, default 0.65). Eruptions are spread over the round by match progress:
the round is cut into `n` equal slices and each eruption starts at `(i + 0.15 + 0.70 * rand) / n`,
so "three eruptions" means exactly three and never two on the same beat. The plan is drawn on the
round's first live frame and kept for the rest of it. A later preset change - a legendary
machine's sky - can switch the layer off, which cuts the eruption in progress, or keep it on (the
menu's On), but it never re-plans, so no eruption replays. Nothing erupts during the pre-round
intro.

## Launch

The crater mouth is at (-366.19, 114.97, -575.42) in City Trial's world, left of and behind the
map centre. Each shot starts there jittered by up to 18 units, along a random ray in an upward
cone: azimuth uniform, tilt 40-100% of 70 degrees times `spread`. Speed is 5.88 times `power`,
+/-30%. Each shot also rolls a size from 0.5 to 3.5 into `WeaponDesc.scale`, which scales the model
and the hitbox together, so a big one is more dangerous; size and speed roll independently.

Every shot falls at the stage's gravity (0.025 on City Trial), and range is roughly
`speed^2 / gravity`, about 1380 units: from the mouth that reaches the -X and -Z edges of the map
and blankets a wide area around the volcano without covering the far corners. `desc.up` is the
unit vector perpendicular to the launch ray in its vertical plane rather than world up, because
`Weapon_Create` builds the orientation from forward and up and a near-vertical launch would make
world up parallel to forward. `proj->vel` is rewritten after creation, since `Weapon_Create` only
snapshots `desc.vel` into `spawn_vel` and several kinds then derive their real velocity from their
own muzzle speed.

A volley is skipped while 64 or more weapons are live, so a long, dense eruption of sensor bombs
- which sit armed until their lifetime runs out - cannot exhaust the object pool.

## Ownerless projectiles

A volcano shot ends up with `owner_gobj` NULL, which `HitColl_CheckIfSamePlayer` reads as "never
the same player", so it hits everyone and no damage is credited to a player. That limits the
roster. The spit stars, plasma C and D dereference the owner inside `Weapon_Create` and home, and
every aura re-snaps to the owner's hand each frame, so the themes draw only from:

| Theme | Kinds |
|---|---|
| Fire (also Default) | the Fire ability's bullet |
| Plasma | plasma A and B, both spread shots |
| Bomb | bomb, sensor bomb |
| Star | the charged spit star |
| Chaos | any of the above, rolled per projectile |

Each kind needs a little help to survive:

- **Bomb and sensor bomb** spawn held in a hand that does not exist, so they are moved to their
  thrown / armed-flying state in the same call as the spawn, before their state-0 slot runs.
- **The fire bullet** reads rider fields through its owner in `init` and `post_init`, so it borrows
  the first live rider for the length of `Weapon_Create` and is made ownerless right after. Its
  `post_init` state change credits a Fire attack to that rider's player
  (`Ply_RecordAttackUsed`, 0x80231bdc), so that player's `PlayerStats` attack counts are saved
  before the spawn and put back after it. The borrowed rider is never holding a charged Fire
  ability, so the mod seeds the kind's scratch itself: word 0 (the knockback multiplier) 1.0, word
  1 (the burst scale) the shot's rolled size - otherwise the burst would land with a zero-radius
  hitbox. It flies until it hits something. The firecracker stays out of the roster because its
  own fuse bursts it mid-flight whatever its lifetime says.
- **Every launch** checks `wp_kind_data[kind]`, which is empty until the first rider registers
  the kinds and which `Weapon_Create` does not check.

## Arcs

Nothing in the shared projectile pipeline applies gravity, and the priority-0 proc zeroes
acceleration every frame. The fire bullet and both bombs add stage gravity (`Gr_GetDownVector`,
0x800ceb18) in their own flying state. The plasma shots and the spit star do not, so they get a
mod function in `proj->framestart_callback`, which runs at the tail of priority 0 - after the
zeroing, before priority 4 integrates - and writes the same stage gravity as their acceleration.
Every theme therefore arcs the same way. The callback is installed after any state change, since
`Weapon_StateChange` clears the user-hook slots.

Lifetime is overwritten on every shot (1680 frames, enough for the steepest arc), because the
per-kind defaults are unusable here: plasma A and B expire in 6-9 frames, and bomb and sensor bomb
never expire.

## Settings

The Volcano submenu carries the Volcano toggle (Preset / Off / On), Eruptions (Preset, 1, 2, 3, 5,
8), Duration (Preset, Brief 0.5x, Long 1.8x, Sustained 3x), Intensity (Preset, Sparse, Heavy,
Cataclysm - scaling the volley size, at most 8, and tightening the gap between volleys), Power
(Preset, Weak, Strong) and Projectiles (Preset or a theme). Each logs its new value.
