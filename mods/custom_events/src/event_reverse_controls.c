#include "game.h"
#include "os.h"
#include "rider.h"
#include "code_patch/code_patch.h"

#include "event_reverse_controls.h"

static int active;

static s8 Invert(s8 v)
{
    return v == -128 ? 127 : -v;
}

// Hook at 0x8018f0cc in RiderGObj_InputThink (0x8018ee28): a human's stick with the
// deadzone and D-Pad applied, just before 3DReplay_SaveInput records it, so a replay
// plays the inverted stick back as is. r31 = RiderData. A frame that kept last frame's
// input, already inverted, also lands here.
static void ReverseControls_InvertHuman(RiderData *rd)
{
    if (!active || Rider_CheckNoUpdateInputs(rd))
        return;

    rd->input.stickX = Invert(rd->input.stickX);
    rd->input.stickY = Invert(rd->input.stickY);
}

CODEPATCH_HOOKCREATE(0x8018f0cc,
                     "mr 3, 31\n\t",
                     ReverseControls_InvertHuman,
                     "",
                     0)

// The CPU branch of RiderGObj_InputThink reads its virtual pad through these two calls.
static int ReverseControls_CPUStickX(RiderData *rd)
{
    s8 x = Rider_GetCPUStickX(rd);
    return active ? Invert(x) : x;
}

static int ReverseControls_CPUStickY(RiderData *rd)
{
    s8 y = Rider_GetCPUStickY(rd);
    return active ? Invert(y) : y;
}

void ReverseControls_Start(void)
{
    active = 1;
    OSReport("[ReverseControls] Started, every stick inverted\n");
}

void ReverseControls_Stop(void)
{
    if (!active)
        return;
    active = 0;
    OSReport("[ReverseControls] Sticks restored\n");
}

void ReverseControls_InstallHooks(void)
{
    CODEPATCH_HOOKAPPLY(0x8018f0cc);
    CODEPATCH_REPLACECALL(0x8018eef4, ReverseControls_CPUStickX);
    CODEPATCH_REPLACECALL(0x8018ef00, ReverseControls_CPUStickY);
}
