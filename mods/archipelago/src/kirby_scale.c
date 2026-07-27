#include "game.h"
#include "rider.h"
#include "topride.h"
#include "inline.h"

#include "kirby_scale.h"
#include "main.h"
#include "ap_item_handler.h"
#include "textbox_api.h"

// Both 3D (RiderData) and Top Ride (TopRideKirby) expose a model_scale float the
// engine multiplies into the model JObj each frame. A per-frame applier rewrites
// it so the scale survives respawns.

// Effects are multiplicative, kept inside [0.5, 2.0] so the model never grows
// large enough to break the camera / collision feel. Neutral matches the vanilla
// model_scale initializer.
#define KIRBY_SCALE_MIN 0.5f
#define KIRBY_SCALE_MAX 2.0f
#define KIRBY_SCALE_GROW 1.5f
#define KIRBY_SCALE_SHRINK 0.5f
#define KIRBY_SCALE_NEUTRAL 1.0f

// ~1 second at 60 fps.
#define KIRBY_SCALE_ANIM_FRAMES 60

// Ease state, shared across human players. target == neutral means "no item this
// scene" and the appliers leave vanilla scaling alone.
static float kirby_scale_target  = KIRBY_SCALE_NEUTRAL;
static float kirby_scale_current = KIRBY_SCALE_NEUTRAL;
static float kirby_scale_start   = KIRBY_SCALE_NEUTRAL;
static int   kirby_scale_anim    = KIRBY_SCALE_ANIM_FRAMES;

static float ClampScale(float s)
{
    if (s < KIRBY_SCALE_MIN)
        return KIRBY_SCALE_MIN;
    if (s > KIRBY_SCALE_MAX)
        return KIRBY_SCALE_MAX;
    return s;
}

// True in a 3D (CT/AR) gameplay scene, or a Top Ride scene with the race active.
// TR has no intro (GMINTRO_END is always true), so it gates on round_state == 2 -
// applying during the countdown would burn the ease off-screen before "GO".
static int InScalableScene(void)
{
    MajorKind major = Scene_GetCurrentMajor();
    if (major == MJRKIND_TOP)
    {
        TopRideKirbyMgr *mgr = *stc_topride_kirbymgr;
        return mgr != NULL && mgr->round_state == 2;
    }
    if (major == MJRKIND_CITY || major == MJRKIND_AIR)
        return Scene_GetCurrentMinor() == MNRKIND_3D;
    return 0;
}

// Applies the multiplier and reports APPLIED when in a scalable scene, otherwise
// RETRY so the item waits in the queue until the player reaches gameplay.
int KirbyScale_HandleItem(uint ap_item_id)
{
    if (!InScalableScene())
        return AP_ITEM_RETRY;

    // Wait for the intro/countdown so the model grows in during play. A no-op in
    // TR, where Gm_GetIntroState defaults to GMINTRO_END.
    if (Gm_GetIntroState() != GMINTRO_END)
        return AP_ITEM_RETRY;

    // Ease from the size currently on screen to the new target.
    kirby_scale_start = kirby_scale_current;
    kirby_scale_anim = 0;

    if (ap_item_id == AP_ITEM_BIG_KIRBY)
    {
        kirby_scale_target = ClampScale(kirby_scale_target * KIRBY_SCALE_GROW);
        tb_api->EnqueueColoredNoun("Received: ", "Big Kirby", tb_api->ItemColor, NULL);
    }
    else
    {
        kirby_scale_target = ClampScale(kirby_scale_target * KIRBY_SCALE_SHRINK);
        tb_api->EnqueueColoredNoun("Received: ", "Small Kirby", tb_api->ItemColor, NULL);
    }

    OSReport("[KirbyScale] %s received; model scale now %d/1000\n",
             ap_item_id == AP_ITEM_BIG_KIRBY ? "Big Kirby" : "Small Kirby",
             (int)(kirby_scale_target * 1000.0f));
    return AP_ITEM_APPLIED;
}

// Advance the displayed multiplier one frame toward the target and return it.
// Once settled it sits exactly on target.
static float KirbyScale_Tick(void)
{
    if (kirby_scale_anim < KIRBY_SCALE_ANIM_FRAMES)
    {
        kirby_scale_anim++;
        float t = (float)kirby_scale_anim / (float)KIRBY_SCALE_ANIM_FRAMES;
        t = t * t * (3.0f - 2.0f * t); // smoothstep
        kirby_scale_current = kirby_scale_start + (kirby_scale_target - kirby_scale_start) * t;
    }
    return kirby_scale_current;
}

static void KirbyScale_3DPerFrame(GOBJ *g)
{
    if (kirby_scale_target == KIRBY_SCALE_NEUTRAL)
        return;

    float s = KirbyScale_Tick();
    for (int i = 0; i < 5; i++)
    {
        if (Ply_GetPKind(i) != PKIND_HMN)
            continue;
        GOBJ *rg = Ply_GetRiderGObj(i);
        if (!rg)
            continue;
        RiderData *rd = rg->userdata;
        rd->model_scale = s;
    }
}

// Same contract as the 3D applier, against TopRideKirby+0x524.
static void KirbyScale_TopRidePerFrame(GOBJ *g)
{
    if (kirby_scale_target == KIRBY_SCALE_NEUTRAL)
        return;

    TopRideKirbyMgr *mgr = *stc_topride_kirbymgr;
    if (!mgr)
        return;

    float s = KirbyScale_Tick();
    for (int i = 0; i < 4; i++)
    {
        TopRideKirby *kirby = mgr->kirbys[i];
        if (!kirby)
            continue;
        if (TopRide_GetPlayerKind(kirby->player_slot) != TR_PKIND_HMN)
            continue;
        *TopRide_KirbyModelScalePtr(kirby) = s;
    }
}

void KirbyScale_On3DLoadEnd(void)
{
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, KirbyScale_3DPerFrame, 0, 0, 0, 0);
}

void KirbyScale_OnTopRideLoadEnd(void)
{
    GOBJ_EZCreator(0, 0, 0, 0, 0, HSD_OBJKIND_NONE, 0, KirbyScale_TopRidePerFrame, 0, 0, 0, 0);
}

void KirbyScale_OnSceneChange(void)
{
    // Scenes recreate Kirby objects at model_scale 1.0, so snap to neutral with no
    // ease. This bounds the effect to the current scene.
    kirby_scale_target  = KIRBY_SCALE_NEUTRAL;
    kirby_scale_current = KIRBY_SCALE_NEUTRAL;
    kirby_scale_start   = KIRBY_SCALE_NEUTRAL;
    kirby_scale_anim    = KIRBY_SCALE_ANIM_FRAMES;
}
