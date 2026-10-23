# Reverse Controls Event

A custom City Trial event (`CUSTOM_EVKIND_REVERSE_CONTROLS`, kind 33) that inverts both axes of
every player's control stick for 15 seconds: left steers right, up pulls down, on a machine and on
foot alike. CPUs get the same inverted stick and mostly flounder. It runs through the
`custom_events` framework with a siren intro, sky preset 12 (Night), BGM file 0x36
(`event_toudai`), a 900-frame duration and roll weight 20.

## How It Works

Every rider reads its stick from `RiderGObj_InputThink` (0x8018ee28), which fills the byte stick
`RiderData.input.stickX` / `stickY` (+0x3ec / +0x3ed) from one of three sources and then derives
the float `lstick` that the rider and machine states read. Negating the bytes once, where each
source writes them, inverts everything downstream - steering, glide pitch, walking, quick spins -
while menus, the pause screen and the C-Stick camera read the pads directly and stay normal.

| Source | Where the mod inverts it |
|---|---|
| A human pad | Hook at 0x8018f0cc, after the deadzone and the D-Pad-to-stick conversion |
| A CPU's virtual pad | The calls to `Rider_GetCPUStickX` / `Rider_GetCPUStickY` at 0x8018eef4 / 0x8018ef00 |
| A replay | Not touched: the human hook runs before `3DReplay_SaveInput`, so the replay records the inverted stick |

The human path also reaches 0x8018f0cc on frames where `Rider_CheckNoUpdateInputs` (0x8019ec30)
keeps last frame's values, which are already inverted, so the hook skips those frames. Negating
-128 gives 127, as an s8 has no +128.

`start` sets the flag and `end` clears it on the first frame of state 3, as the sky and music go
back; `abort` clears it too, since the hooks run in every scene.
