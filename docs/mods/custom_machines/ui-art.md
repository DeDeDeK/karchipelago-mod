# Custom Machines: UI Art

How a registered machine gets a picture everywhere the game draws one. Every machine picture on
the select screens, the four results screens, the stadium select and the time-attack board is a
frame of a TexAnim whose animation frame is the `CharacterKind`; the City Trial field blip and
the stadium HUD icon are frames picked by machine kind. None of those banks has a frame past the
vanilla roster, so the `custom_machines` mod grows each one as its archive loads, filling the new
frames from a shared placeholder side-car and from each machine's own `.art` file.

## The banks

Twenty banks across eight menu archives are keyed by `CharacterKind`, and two HUD banks by machine.
They come in five geometries, and the geometry is what says which role a bank plays:

| Role | Geometry | Banks | Drawn as |
|---|---|---|---|
| portrait | 64x64 CMPR | `MnSelplyAll`, `MnSelplyctAll` | the select-grid tile: opaque, the machine fitted inside the square over a flat warm-gray backdrop with a soft contact shadow |
| picture | 80x48 C8 | both select archives and `MnResultAll`, `MnResult2All`, `MnResult4All`, `MnResultCtAll` | the large three-quarter hero view beside the cursor and on the results screens, alpha cutout, no backdrop |
| silhouette | 80x48 I4 | the same six | a soft white bloom drawn under the picture: the picture's alpha under a small Gaussian blur |
| icon | 40x40 C4 | the four results archives, `MnSelstadiumAll`, `MnBestrapAll`, and `IfAll3c`'s stadium HUD icon | a top-down view with a hard black outline and a white halo |
| blip | 32x32 C4 | `IfAll2c` | the City Trial field blip: the top-down silhouette in one flat pastel, no outline |

Image sizes vary frame to frame within a bank; the geometry is the source frame's, the frame
every appended image is encoded at. `MnSelruleAll.dat` also holds a 20-image bank, but its frame
is a rule-screen option value, not a `CharacterKind`, so it is left alone.

## How the frames are laid out

Banks grow by a fixed 13 frames (`CUSTOM_MACHINE_MAX`), not by however many machines are
registered: a bank's frame ramps are authored keyframe data, so their width is fixed when the
side-car is built. An unclaimed frame holds the placeholder and nothing ever selects it.

- **Character banks** add frames 20-32, one per appended `CharacterKind`. The portrait banks and
  the time-attack board's icon bank index frame for frame.
- **Picture and silhouette banks** hold one frame per character color, and the engine diverts
  King Dedede to frame `20 + color` and Meta Knight to `30 + color` - right where the appended
  frames go. Both runs move up by 13, to 33-40 and 43-50, and the mod rewrites the sixteen `addi`
  immediates that form them: King Dedede's in `AirRideSelect_SetSIcon2Color`,
  `AirRideSelect_SetSIcon2Character`, `CitySelect_SetSIcon2Color`,
  `CitySelect_SetSIcon2Character`, `MnResult_CreateSiconBig` and its three results-screen
  siblings, and Meta Knight's `0x14` past each. These run whether or not a machine is registered,
  which is why every bank those sites drive must grow together. The widest bank goes from 34 frames to 47.
- **The results and stadium-select icon banks** hold 18 images - one per `CharacterKind` with a
  machine of its own, since `Machine_GetCKind` never answers King Dedede or Meta Knight - so
  frames 18 and 19 stay unkeyed and appended frames still start at 20. Ungrown, an appended
  character would run past the last key and draw the Flight Warp Star on every results row.
- **HUD banks** (`IfAll2c` blip, `IfAll3c` stadium icon) are keyed by machine: appended frame n
  is the n-th registered machine, whether or not it took a `CharacterKind`.

A vanilla image-index ramp is a `u8` over a `2^3` divisor, which cannot hold an index past 31.
The values are whole numbers, so the widened ramp drops the fractional bits as far as the new keys
need, keeping the format and the buffer length; the fitted `value_flag` travels with the buffer.

The select and results Sicon models also size their quad per character - the Slick Star's
picture is 80x48, King Dedede's 56x80 and Meta Knight's 104x52 - through a pair of `SCAX`/`SCAY`
tracks on the same timeline as the image ramp. Those tracks are widened too, their new keys
holding the source frame's scale; left alone, an appended frame would draw at the shape of
whatever frame it displaced.

## The side-cars

Growth is applied to the archives as they load rather than shipped as rewritten copies, which
would mean carrying whole vanilla archives to deliver a few frames. Only two edits are
position-independent and can be baked: an `ImageDesc` with its texels, and a replacement keyframe
buffer per ramp. The image and TLUT pointer arrays point into the loaded archive, so they are
rebuilt at runtime in a static pool with a fixed slot per bank.

`CmUiFrames.dat`, at the disc root, carries one placeholder image per bank, the widened ramps and
the widened quad-scale tracks, each bank named by archive basename and data-section offset (fixed
for GKYE01). It is loaded once at boot and held for the run of the game. A machine's own art is
`machines/<name>.art` beside its archive: a `customMachineArt` public, magic `CMAR`, holding one
image per bank geometry keyed by the source frame's `(width, height, format)`. Matching on all
three means art built for a different geometry fails to match rather than writing a wrong-sized
image. A machine with no `.art` shows the placeholder, so art never gates a select cell.

Whatever the source, an image is re-encoded for its bank: RGB5A3 in a picture bank, I4 in a
silhouette bank. `HSD_TObjSetup` (0x803f7158) reads the format of the one frame it is drawing, so
an RGB5A3 frame in a CMPR, C4 or C8 bank leaves every other frame alone; its TLUT slot is filled
with the source frame's and never consulted.

## The splice

All ten archives load through `Gm_LoadGameFile` (0x80059818) and are rebuilt each time - the
eight menu archives on every entry into their scene, `IfAll2c.dat` and `IfAll3c.dat` on every City
Trial and stadium 3D load - so the splice hangs off two hooks in that loader. The first, at
0x80059834, stashes the basename while it is still in `r25`, since the preload-hit path reuses that
register before the tail. The second, at 0x800599f8 where the preload and cold-read paths converge
with the archive in `r30`, matches the name and patches each bank: appended `ImageDesc`s (and a
TLUT entry apiece where a TLUT ramp indexes them), the rebuilt pointer arrays, the bumped counts in
the TexAnim header, and the replacement ramps. The engine passes the name with a trailing `.` and
not always in the disc's case (`MnSelplyCtAll` for `MnSelplyctAll.dat`), so the match ignores case
and stops at the dot. Each load records whether every bank in the archive grew, counting a
preloaded archive already spliced; the field blip asks before it builds custom templates.

## Making the art

The HSD toolchain's machine-art script builds a `.art` from two renders, because the icon's angle
is not the picture's: a three-quarter hero view from slightly above, and a straight top-down. Each
wants a generous margin and at least 4x the target size (320x192 or better for the 80x48 slots);
it is cropped to its own alpha, scaled to fit and padded, never stretched. A render arriving on a
flat viewport backdrop can be keyed out by color, with alpha taken from distance to the backdrop
so an additive glow keeps its falloff. The blip color is given or taken from the render's average,
brightest channel at 255 and the rest pulled halfway to white, as vanilla gives every machine its
own pastel. A preview of the finished images, magnified, is the only practical way to judge a
32x32 blip before it is on screen.

A machine archive does not open in a model viewer as shipped, because its one public names
nothing a viewer can draw. The toolchain's machine-preview script writes a viewer-only copy whose
public table names the model root, the shadow and each animation-bank slot the way the viewer
expects, keeping only the DObjs one LOD table asks for. That copy is not a disc asset: pruning
leaves the LOD tables indexing a DObj order the tree no longer has.
