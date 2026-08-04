#include "camera.h"
#include "hsd.h"
#include "os.h"
#include "code_patch/code_patch.h"

#include "settings_menu.h"
#include "onfoot_zoom.h"

// PlyCam_OnFootThink reads only the C-Stick's X axis, for rotation; the machine
// and rail cameras hand both axes to cameraControlThink (0x800b67cc), whose Y
// half drives the zoom. This reproduces that half for the on-foot camera.

// Deadzone cameraControlThink applies to each C-Stick axis: nothing below 0.4,
// then a linear ramp to 1.0 across the remaining 0.6.
#define CAM_STICK_DEADZONE 0.4f
#define CAM_STICK_RANGE    0.6f

static float CamStickAxis(float raw)
{
    float mag = (raw < 0.0f) ? -raw : raw;

    if (mag <= CAM_STICK_DEADZONE)
        return 0.0f;

    return (raw > 0.0f ? raw - CAM_STICK_DEADZONE : raw + CAM_STICK_DEADZONE) / CAM_STICK_RANGE;
}

// Hook body at 0x800cb4dc, past every input gate PlyCam_OnFootThink already
// cleared (rail transition, camera lock, HUD takeover) and after its C-Stick X
// rotation store. Only the accumulator and the x84_80 enable are set here:
// PlyCam_MachineZoomAdjust runs for every camera kind and turns them into the
// final eye position, so raising the flag is what actually connects the on-foot
// camera to the zoom. Nothing else on this path writes these three fields, so
// clearing them is all a mid-round toggle-off needs to snap back to vanilla.
void OnFootZoom_Update(CamData *cam, int pad_index)
{
    cmMainParamCommon *param = stc_plycam_lookup->param;
    float in;

    if (!ap_menu_settings.onfoot_zoom_enabled || !cam->target || !param)
    {
        cam->x84_80 = 0;
        cam->zoom_amt = 0.0f;
        cam->x90 = 0.0f;
        return;
    }

    cam->x84_80 = 1;

    in = CamStickAxis(stc_engine_pads[pad_index].fsubstickY);
    if (in != 0.0f)
        cam->zoom_amt -= in * param->zoom_speed;

    if (cam->zoom_amt < param->zoom_dist_min)
        cam->zoom_amt = param->zoom_dist_min;
    else if (cam->zoom_amt > param->zoom_dist_max)
        cam->zoom_amt = param->zoom_dist_max;

    cam->x90 = (cam->zoom_amt >= 0.0f) ? param->x350 * (cam->zoom_amt / param->zoom_dist_max) : 0.0f;
}

CODEPATCH_HOOKCREATE(0x800cb4dc,
    "mr 3,29\n\t"         // cam
    "clrlwi 4,30,24\n\t", // pad_index
    OnFootZoom_Update,
    "li 3,0\n\t", // restore the "camera needs no re-solve" return value
    0)

void OnFootZoom_OnBoot(void)
{
    CODEPATCH_HOOKAPPLY(0x800cb4dc);
    OSReport("[OnFootZoom] On-foot camera zoom hook installed\n");
}
