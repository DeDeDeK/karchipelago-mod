#include "camera.h"
#include "hsd.h"
#include "os.h"
#include "inline.h"
#include "code_patch/code_patch.h"

#include "settings_menu.h"
#include "onfoot_zoom.h"

// PlyCam_OnFootThink reads only the C-Stick's X axis, for rotation; the machine and rail
// cameras hand both axes to cameraControlThink (0x800b67cc), whose Y half drives the zoom.

// cameraControlThink's per-axis C-Stick deadzone.
#define CAM_STICK_DEADZONE 0.4f
#define CAM_STICK_RANGE    0.6f

static float CamStickAxis(float raw)
{
    if (_fabs(raw) <= CAM_STICK_DEADZONE)
        return 0.0f;

    return (raw > 0.0f ? raw - CAM_STICK_DEADZONE : raw + CAM_STICK_DEADZONE) / CAM_STICK_RANGE;
}

// Hook body at 0x800cb4dc in PlyCam_OnFootThink (0x800cb3b4), past its input gates.
// PlyCam_SwitchKind clears zoom_enabled on entry to the on-foot kind, and nothing else
// raises it there, so with the toggle off the camera keeps its vanilla framing.
static void OnFootZoom_Update(CamData *cam, int pad_index)
{
    cmMainParamCommon *param = stc_plycam_lookup->param;
    float in;

    if (!ap_menu_settings.onfoot_zoom_enabled || !cam->target || !param)
        return;

    cam->zoom_enabled = 1;

    in = CamStickAxis(stc_engine_pads[pad_index].fsubstickY);
    if (in != 0.0f)
        cam->zoom_amt -= in * param->zoom_speed;

    if (cam->zoom_amt < param->zoom_dist_min)
        cam->zoom_amt = param->zoom_dist_min;
    else if (cam->zoom_amt > param->zoom_dist_max)
        cam->zoom_amt = param->zoom_dist_max;

    cam->interest_raise = (cam->zoom_amt >= 0.0f) ? param->zoom_interest_raise * (cam->zoom_amt / param->zoom_dist_max) : 0.0f;
}

CODEPATCH_HOOKCREATE(0x800cb4dc,
    "mr 3,29\n\t"         // cam
    "clrlwi 4,30,24\n\t", // pad_index
    OnFootZoom_Update,
    "li 3,0\n\t",         // the "no re-solve" return value the bl destroyed
    0)

void OnFootZoom_OnBoot(void)
{
    CODEPATCH_HOOKAPPLY(0x800cb4dc);
    OSReport("[OnFootZoom] Hooks installed\n");
}
