# On-Foot Camera Zoom

In vanilla Kirby Air Ride the C-Stick zooms the camera in and out only while riding a machine.
On foot the same stick still orbits the camera left and right, but the zoom axis is dead. A
menu toggle feeds the on-foot camera the C-Stick's Y axis using the machine camera's own zoom
math, so the two behave identically. It is a convenience enhancement, not an Archipelago gate:
no items, no save state.

Implementation: `mods/archipelago/src/onfoot_zoom.c`, with the menu option in
`mods/archipelago/src/settings_menu.c`.

## Game System

Every player camera is a `CamData` driven once per frame by `PlyCam_Think` (`0x800b3540`).
`CamData.kind` indexes a table of 6-word kind descriptors at `0x804a1f0c`; the relevant kinds
are 1 (riding a machine), 6 (on foot) and 9 (rail). Each descriptor holds the callbacks
`PlyCam_Think` dispatches, at these offsets:

| Offset | Role | Kind 1 | Kind 6 |
|---|---|---|---|
| `+0x04` | on-enter, run by `PlyCam_SwitchKind` | `PlyCam_OnEnterNormal` (`0x800c28c0`) | `PlyCam_OnFootEnter` (`0x800cc050`) |
| `+0x08` | input think, run last | `PlyCam_OnMachineThink` (`0x800c04b0`) | `PlyCam_OnFootThink` (`0x800cb3b4`) |
| `+0x0c` | per-frame solve into `CamData.xc0` | `0x800c05ac` | `PlyCam_OnFootSolve` (`0x800cb500`) |
| `+0x10` | smooth `xc0` into `CamData.xe8` | `0x800c24e8` | `PlyCam_OnFootBlend` (`0x800cbec8`) |

The frame order is solve -> smooth -> `PlyCam_MachineZoomAdjust` -> input think, so C-Stick
input read on one frame lands on the next.

Despite its name `PlyCam_MachineZoomAdjust` (`0x800b61f4`) runs for **every** kind. It reads
`CamData.xe8` and writes `CamData.x110`, the params that actually reach the COBJ. When
`CamData.x84_80` is clear it copies `xe8` straight through; when the bit is set it rebuilds the
eye position relative to `target->pos_high`, orbits it by `CamData.rotation_amt`, extends it by
`CamData.zoom_amt`, and raises the interest along the up vector by `CamData.x90`. At
`rotation_amt == 0` and `zoom_amt == 0` that rebuild is an identity transform, so raising the
bit alone changes nothing on screen.

`cameraControlThink` (`0x800b67cc`) is what fills those three fields from the C-Stick. Each
axis passes through a deadzone of 0.4 with a linear ramp over the remaining 0.6. X drives
`rotation_amt`; Y accumulates into `zoom_amt` at `cmMainParamCommon.zoom_speed` per frame, then
clamps to `zoom_dist_min .. zoom_dist_max` and derives `x90` as
`x350 * (zoom_amt / zoom_dist_max)`. The live values are `zoom_speed 0.2`, `zoom_dist_min -2.0`,
`zoom_dist_max 8.4`, `x350 4.0`. It also raises `x84_80` whenever `CamData.target` exists, which
is the only thing that enables the `PlyCam_MachineZoomAdjust` path.

`PlyCam_OnMachineThink` and `PlyCam_RailThink` call `cameraControlThink`.
`PlyCam_OnFootThink` does not - it applies the same deadzone curve to the C-Stick X itself and
accumulates into `CamData.x1c0`, an orbit budget `PlyCam_OnFootSolve` drains a fraction of per
frame. That is the whole of on-foot camera input in vanilla: no zoom axis, and `x84_80` never
raised. `PlyCam_SwitchKind` zeroes `rotation_amt`, `zoom_amt` and `x84_80` on every kind
change, so nothing leaks in from the machine camera on dismount either.

## Hook

One `CODEPATCH_HOOKCREATE` at `0x800cb4dc`, the `b` that ends `PlyCam_OnFootThink`'s C-Stick X
branch. Placing it there means every gate the function already applied still holds - rail
transition pending, the camera-locked flag on `PlayerCamData+0xc`, and the HUD takeover check -
so the zoom is live exactly when the rotation is. `r29` still holds the `CamData` and `r30` the
controller index; `r3` holds the function's `0` return, which the epilogue restores because the
call clobbers it.

The hook body reproduces `cameraControlThink`'s Y-axis half against the same
`cmMainParamCommon`, so the on-foot zoom range, speed and interest raise are the machine
camera's. `PlyCam_MachineZoomAdjust` needs no patch - raising `x84_80` is what connects it.

With the toggle off the body clears `x84_80`, `zoom_amt` and `x90` instead. Nothing else on the
on-foot path writes them, so a mid-round toggle-off snaps straight back to vanilla framing.

## Scope

The on-foot camera kind only exists where the rider can leave a machine, which is the open
City Trial map. Riders never dismount in the stadiums (Kirby Melee included) or in Air Ride,
and Top Ride uses a separate camera system entirely. Kind 1 and 9 already zoom in vanilla and
are untouched.

`PlyCam_MachineZoomAdjust` writes the per-player zoom back to
`PlayerCamLookup.ply_distance[ply]` only for kinds 1 and 9, so on-foot zoom is never saved and
never overwrites the machine camera's saved distance. Remounting restores the machine zoom as
usual, and each dismount starts from the default on-foot framing.

## Menu

`ap_menu_settings.onfoot_zoom_enabled` (`APMenuSettings`), an On/Off toggle in the Archipelago
Settings menu. Default **Off** (vanilla behavior); the player opts in. Changes are logged via
`OnToggleOnFootZoom`.
